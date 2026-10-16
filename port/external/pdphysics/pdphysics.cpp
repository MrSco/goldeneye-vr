/**
 * MOD: ragdoll physics - Jolt backend.
 *
 * Compiled as its own static library (pd_physics) with clean compiler flags,
 * separate from the game's N64-style flags and defines. See CMakeLists.txt.
 */

#include "pdphysics.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

JPH_SUPPRESS_WARNINGS

using namespace JPH;

namespace {

// Perfect Dark units are ~centimetres; Jolt is tuned for metres.
constexpr float kUnitsPerMetre = 100.0f;
inline float toMetres(float units) { return units / kUnitsPerMetre; }
inline float toUnits(float metres) { return metres * kUnitsPerMetre; }
template <class V> inline void storeUnits(const V &p, float out[3]) { out[0] = toUnits((float) p.GetX()); out[1] = toUnits((float) p.GetY()); out[2] = toUnits((float) p.GetZ()); }
inline Vec3 vecFromUnits(const float v[3]) { return Vec3(toMetres(v[0]), toMetres(v[1]), toMetres(v[2])); }
inline Vec3 dirFrom(const float v[3]) { return Vec3(v[0], v[1], v[2]).NormalizedOr(Vec3::sAxisY()); }
template <class V> inline void storeVec(const V &v, float out[3]) { out[0] = (float) v.GetX(); out[1] = (float) v.GetY(); out[2] = (float) v.GetZ(); }

pdphysLogFn g_LogFn = nullptr;

void logf(int level, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (g_LogFn) {
		g_LogFn(level, buf);
	} else {
		fprintf(level == PDPHYS_LOG_NOTE ? stdout : stderr, "%s\n", buf);
	}
}

void traceImpl(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	logf(PDPHYS_LOG_NOTE, "PHYSICS(jolt): %s", buf);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailedImpl(const char *expr, const char *msg, const char *file, uint line)
{
	logf(PDPHYS_LOG_ERROR, "PHYSICS(jolt) assert: %s:%u: (%s) %s", file, line, expr, msg ? msg : "");
	return false; // don't break into the debugger
}
#endif

// Object layers
// Which level groups ragdolls collide with (bit n = group n). The default is
// the game's own collision tiles (0, 1) and the ragdoll module's render walls
// (2); the clipping checks can switch bodies to their generated surfaces
// (query groups 8+), which then take part in the simulation for ragdolls only.
static unsigned int g_RagdollLevelGroups = 0x7;

namespace Layers {
	constexpr ObjectLayer STATIC = 0;  // level geometry
	constexpr ObjectLayer MOVING = 1;  // ragdoll parts
	constexpr ObjectLayer PROPS = 2;   // doors, lifts, furniture (kinematic, follow the game)
	constexpr ObjectLayer QUERY = 3;   // MOD: clipping checks - level surfaces for queries only (collide with nothing)
	constexpr ObjectLayer NUM_LAYERS = 4;
}

namespace BPLayers {
	constexpr BroadPhaseLayer STATIC(0);
	constexpr BroadPhaseLayer MOVING(1);
	constexpr BroadPhaseLayer PROPS(2);
	constexpr uint NUM_LAYERS = 3;
}

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
	bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override
	{
		// Query-only level surfaces only collide with ragdolls, and only when
		// ragdolls are set to use them
		if (a == Layers::QUERY || b == Layers::QUERY) {
			return (a == Layers::MOVING || b == Layers::MOVING) && (g_RagdollLevelGroups >> PDPHYS_QUERY_GROUP_FIRST) != 0;
		}

		// Level geometry and props only ever collide with ragdolls
		if (a == Layers::MOVING || b == Layers::MOVING) {
			return true;
		}
		return false;
	}
};

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
	BPLayerInterfaceImpl()
	{
		m_Map[Layers::STATIC] = BPLayers::STATIC;
		m_Map[Layers::MOVING] = BPLayers::MOVING;
		m_Map[Layers::PROPS] = BPLayers::PROPS;
		m_Map[Layers::QUERY] = BPLayers::STATIC;
	}

	uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }

	BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override { return m_Map[layer]; }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char *GetBroadPhaseLayerName(BroadPhaseLayer layer) const override
	{
		return layer == BPLayers::STATIC ? "STATIC" : layer == BPLayers::MOVING ? "MOVING" : "PROPS";
	}
#endif

private:
	BroadPhaseLayer m_Map[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bplayer) const override
	{
		if (layer == Layers::MOVING) {
			return true;
		}
		return bplayer == BPLayers::MOVING;
	}
};

// Ray casts only look at level geometry and props, never at ragdolls
class WorldOnlyBPFilter final : public BroadPhaseLayerFilter {
public:
	bool ShouldCollide(BroadPhaseLayer layer) const override
	{
		return layer == BPLayers::STATIC || layer == BPLayers::PROPS;
	}
};

class WorldOnlyObjectFilter final : public ObjectLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer) const override
	{
		return layer == Layers::STATIC || layer == Layers::PROPS;
	}
};

class RagdollOnlyBPFilter final : public BroadPhaseLayerFilter {
public:
	bool ShouldCollide(BroadPhaseLayer layer) const override
	{
		return layer == BPLayers::MOVING;
	}
};

class RagdollOnlyObjectFilter final : public ObjectLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer) const override
	{
		return layer == Layers::MOVING;
	}
};

// Level geometry + ragdolls, without kinematic props: the player's own
// collision cylinder (a prop) must not block their aim at a body
class RagdollAndLevelBPFilter final : public BroadPhaseLayerFilter {
public:
	bool ShouldCollide(BroadPhaseLayer layer) const override
	{
		return layer == BPLayers::MOVING || layer == BPLayers::STATIC;
	}
};

class RagdollAndLevelObjectFilter final : public ObjectLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer) const override
	{
		return layer == Layers::MOVING || layer == Layers::STATIC;
	}
};

class LevelOnlyBPFilter final : public BroadPhaseLayerFilter {
public:
	bool ShouldCollide(BroadPhaseLayer layer) const override
	{
		return layer == BPLayers::STATIC;
	}
};

class LevelAndQueryObjectFilter final : public ObjectLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer) const override
	{
		return layer == Layers::STATIC || layer == Layers::QUERY;
	}
};

class LevelOnlyObjectFilter final : public ObjectLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer) const override
	{
		return layer == Layers::STATIC;
	}
};

// Limits for the world. Generous for a handful of ragdolls plus level geometry.
constexpr uint kMaxBodies = 8192;
constexpr uint kNumBodyMutexes = 0; // default
constexpr uint kMaxBodyPairs = 16384;
constexpr uint kMaxContactConstraints = 8192;
constexpr uint kTempAllocatorSize = 16 * 1024 * 1024;

// Ragdoll bodies carry this flag + their ragdoll handle in their user data
constexpr uint64 kRagdollUserFlag = 1ull << 40;
constexpr int kLevelGroupShift = 48; // level bodies: user data = room | group << 48
// MOD: clipping checks - the back (reversed) copy of a generated surface: for
// queries only. Ragdolls collide with the drawn side alone; with both, a body
// part that ends up inside drawn geometry (a ledge, a step, a planter, often
// right at death) gets pushed further in by the far side and sucked against
// walls and onto ledges.
constexpr uint64 kLevelBackFlag = 1ull << 44;
constexpr uint64 kPropUserFlag = 1ull << 41; // kinematic prop bodies: user data = flag | prop handle

// MOD: clipping checks - room filters. Like the game's own collision, which
// only looks at the tiles of the rooms an actor is in (and the rooms reached
// through their portals), level geometry only counts in those rooms: levels
// have rooms drawn on top of each other in the same space, shown one at a
// time through portals, and the hidden one must not collide.
constexpr int kMaxFilterRooms = 4096;

struct RoomSet {
	bool enabled = false;
	uint8 bits[kMaxFilterRooms / 8] = {};

	void Set(const int *rooms, int num)
	{
		memset(bits, 0, sizeof(bits));
		enabled = num >= 0;

		for (int i = 0; i < num; i++) {
			if (rooms[i] >= 0 && rooms[i] < kMaxFilterRooms) {
				bits[rooms[i] >> 3] |= (uint8) (1u << (rooms[i] & 7));
			}
		}
	}

	bool Allows(uint64 userdata) const
	{
		if (!enabled) {
			return true;
		}

		int room = (int) (userdata & 0xffffffffull);

		return room >= 0 && room < kMaxFilterRooms && (bits[room >> 3] & (1u << (room & 7))) != 0;
	}
};

static RoomSet g_QueryRooms;
static RoomSet g_RagdollRooms[256];
static RoomSet g_GeneratedRooms; // rooms with query surfaces (pdphysMarkRoomGenerated)
constexpr int kMaxContactFlags = 256;
std::mutex g_ContactMutex;
float g_RagdollNewContact[kMaxContactFlags][PDPHYS_MAX_RAGDOLL_PARTS]; // fastest new impact per part since last read (m/s), 0 = none
float g_RagdollNewContactNY[kMaxContactFlags][PDPHYS_MAX_RAGDOLL_PARTS]; // ... its contact normal |y|
// Ragdoll parts running into props (pdphysTakePropImpacts)
constexpr int kMaxPropImpacts = 64;
static pdphysPropImpact g_PropImpacts[kMaxPropImpacts];
static int g_NumPropImpacts = 0;
// New contacts with something other than a floor (a wall, a ceiling, the
// side of a prop or a character, another body) per part since last read
unsigned char g_RagdollNewBlock[kMaxContactFlags][PDPHYS_MAX_RAGDOLL_PARTS];
float g_RagdollBlockNormalY[kMaxContactFlags]; // the last one's normal (y) and what it was (diagnostics)
int g_RagdollBlockProp[kMaxContactFlags];      // ... the prop handle, -1 = not a prop
int g_RagdollBlockKind[kMaxContactFlags];      // 0 level, 1 prop/character, 2 another body

static bool propNoBlock(int handle); // (g_PropBodies is further down)
static bool propLiving(int handle);
static float propLastMove(int handle);

// Characters stepping on bodies pass through them (pdphysSetLivingPassThrough)
constexpr float kLivingMovedMin = 0.005f;  // m: the character moved at least this much since the last sync
constexpr float kLivingSolidSpeed = 1.0f;  // m/s: a part moving into the character this fast still hits it
static bool g_LivingPassThrough = false;
static float g_SlopeGrip = 0.0f; // pdphysSetSlopeGrip
static std::unordered_set<uint64> g_LivingPassPairs; // body/character pairs being walked through (until they part)
static std::mutex g_LivingPassMutex;

static uint64 livingPairKey(BodyID a, BodyID b)
{
	uint32 x = a.GetIndexAndSequenceNumber();
	uint32 y = b.GetIndexAndSequenceNumber();
	return x < y ? ((uint64) x << 32) | y : ((uint64) y << 32) | x;
}

// Thud sounds: notes when a ragdoll body starts touching the level, a prop or
// another body (Jolt calls this once per new contact, not while resting)
class RagdollContactListener : public ContactListener {
public:
	// A ragdoll part only touches level geometry of the rooms it's in
	ValidateResult OnContactValidate(const Body &b1, const Body &b2, RVec3Arg baseoffset, const CollideShapeResult &result) override
	{
		const Body *rd = (b1.GetUserData() & kRagdollUserFlag) ? &b1 : ((b2.GetUserData() & kRagdollUserFlag) ? &b2 : nullptr);
		const Body *other = rd == &b1 ? &b2 : &b1;

		if (rd && !(other->GetUserData() & kRagdollUserFlag)
				&& (other->GetObjectLayer() == Layers::STATIC || other->GetObjectLayer() == Layers::QUERY)) {
			int handle = (int) (rd->GetUserData() & 0xffff);
			int group = (int) ((other->GetUserData() >> kLevelGroupShift) & 0xff);

			if (group >= 32 || (g_RagdollLevelGroups & (1u << group)) == 0) {
				return ValidateResult::RejectAllContactsForThisBodyPair;
			}

			if (other->GetUserData() & kLevelBackFlag) {
				return ValidateResult::RejectAllContactsForThisBodyPair;
			}

			// Bodies on generated surfaces: the regular groups only stand in
			// for rooms that have none yet (not seen)
			if (group < PDPHYS_QUERY_GROUP_FIRST && (g_RagdollLevelGroups >> PDPHYS_QUERY_GROUP_FIRST) != 0
					&& g_GeneratedRooms.Allows(other->GetUserData())) {
				return ValidateResult::RejectAllContactsForThisBodyPair;
			}

			if (handle >= 0 && handle < 256 && !g_RagdollRooms[handle].Allows(other->GetUserData())) {
				return ValidateResult::RejectAllContactsForThisBodyPair;
			}
		}

		return ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	// A body part and a living character with pass-through on (Characters
	// Push Bodies off): a character walking into or over a body goes through
	// it instead of shoving it. Everything else stays solid: a character
	// standing still, a body falling, sliding or blending into someone, a body
	// thrown at someone. A pair let through stays so until the two part (so
	// a body isn't shoved out when the character stops on top of it).
	// True when it's let through.
	static bool PassThrough(const Body &b1, const Body &b2, const ContactManifold &manifold, ContactSettings &settings)
	{
		if (!g_LivingPassThrough) {
			return false;
		}

		bool rd1 = (b1.GetUserData() & kRagdollUserFlag) != 0;
		bool rd2 = (b2.GetUserData() & kRagdollUserFlag) != 0;
		const Body *rdbody;
		int living;

		if (rd1 && !rd2 && (b2.GetUserData() & kPropUserFlag) && propLiving((int) (b2.GetUserData() & 0xffff))) {
			rdbody = &b1;
			living = (int) (b2.GetUserData() & 0xffff);
		} else if (rd2 && !rd1 && (b1.GetUserData() & kPropUserFlag) && propLiving((int) (b1.GetUserData() & 0xffff))) {
			rdbody = &b2;
			living = (int) (b1.GetUserData() & 0xffff);
		} else {
			return false;
		}

		uint64 key = livingPairKey(b1.GetID(), b2.GetID());

		{
			std::lock_guard<std::mutex> lock(g_LivingPassMutex);

			if (g_LivingPassPairs.count(key)) {
				settings.mIsSensor = true;
				return true;
			}
		}

		// Only the character's own movement is let through
		if (propLastMove(living) < kLivingMovedMin) {
			return false;
		}

		Vec3 toliving = rdbody == &b1 ? manifold.mWorldSpaceNormal : -manifold.mWorldSpaceNormal;
		RVec3 p = manifold.mRelativeContactPointsOn1.size() > 0 ? manifold.GetWorldSpaceContactPointOn1(0) : rdbody->GetCenterOfMassPosition();

		if (rdbody->GetPointVelocity(p).Dot(toliving) >= kLivingSolidSpeed) {
			return false; // the body is moving into the character: it hits them
		}

		{
			std::lock_guard<std::mutex> lock(g_LivingPassMutex);
			g_LivingPassPairs.insert(key);
		}

		settings.mIsSensor = true;
		return true;
	}

	// Level surfaces have Jolt's default friction (0.2), so bodies slid down
	// any slope over ~22 degrees. On slopes of 6 to 60 degrees the pair's
	// friction is raised to the slope grip (0.70 holds a body up to ~35
	// degrees); flat floors and walls keep the normal friction.
	static void SlopeGrip(const Body &b1, const Body &b2, const ContactManifold &manifold, ContactSettings &settings)
	{
		if (g_SlopeGrip <= 0.0f) {
			return;
		}

		bool rd1 = (b1.GetUserData() & kRagdollUserFlag) != 0;
		bool rd2 = (b2.GetUserData() & kRagdollUserFlag) != 0;

		if (rd1 == rd2) {
			return;
		}

		const Body &rdbody = rd1 ? b1 : b2;
		const Body &other = rd1 ? b2 : b1;

		if (other.GetObjectLayer() != Layers::STATIC && other.GetObjectLayer() != Layers::QUERY) {
			return;
		}

		float ny = std::fabs(manifold.mWorldSpaceNormal.GetY());

		if (ny > 0.995f || ny < 0.5f) {
			return;
		}

		// Slippery floor types (a lower part friction) grip less
		float grip = g_SlopeGrip * std::sqrt(rdbody.GetFriction() / 0.8f);

		if (settings.mCombinedFriction < grip) {
			settings.mCombinedFriction = grip;
		}
	}

	void OnContactPersisted(const Body &b1, const Body &b2, const ContactManifold &manifold, ContactSettings &settings) override
	{
		SlopeGrip(b1, b2, manifold, settings);
		PassThrough(b1, b2, manifold, settings);
	}

	void OnContactRemoved(const SubShapeIDPair &pair) override
	{
		uint64 key = livingPairKey(pair.GetBody1ID(), pair.GetBody2ID());
		std::lock_guard<std::mutex> lock(g_LivingPassMutex);
		g_LivingPassPairs.erase(key);
	}

	void OnContactAdded(const Body &b1, const Body &b2, const ContactManifold &manifold, ContactSettings &settings) override
	{
		SlopeGrip(b1, b2, manifold, settings);

		if (PassThrough(b1, b2, manifold, settings)) {
			return; // walked over: no thud, no impact, doesn't end an animation blend
		}

		// Impact speed: relative velocity along the contact normal
		float speed = 0.0f;

		if (manifold.mRelativeContactPointsOn1.size() > 0) {
			RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
			Vec3 rel = b1.GetPointVelocity(p) - b2.GetPointVelocity(p);
			speed = std::fabs(rel.Dot(manifold.mWorldSpaceNormal));
		}

		Check(b1, b2, speed, std::fabs(manifold.mWorldSpaceNormal.GetY()));
		Check(b2, b1, speed, std::fabs(manifold.mWorldSpaceNormal.GetY()));

		// A body running into a prop (an object, a door): the game decides
		// what that does to the prop (pushes it, breaks glass)
		if (manifold.mRelativeContactPointsOn1.size() > 0) {
			bool rd1 = (b1.GetUserData() & kRagdollUserFlag) != 0;
			bool rd2 = (b2.GetUserData() & kRagdollUserFlag) != 0;
			bool prop1 = (b1.GetUserData() & kPropUserFlag) != 0;
			bool prop2 = (b2.GetUserData() & kPropUserFlag) != 0;

			if ((rd1 && prop2) || (rd2 && prop1)) {
				RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
				Vec3 n = manifold.mWorldSpaceNormal; // from body 1 to body 2
				float approach = (b1.GetPointVelocity(p) - b2.GetPointVelocity(p)).Dot(n);

				if (approach > 0.5f) {
					const Body &rdbody = rd1 ? b1 : b2;
					const Body &propbody = rd1 ? b2 : b1;
					Vec3 dir = rd1 ? n : -n;
					std::lock_guard<std::mutex> lock(g_ContactMutex);

					if (g_NumPropImpacts < kMaxPropImpacts) {
						pdphysPropImpact &pi = g_PropImpacts[g_NumPropImpacts++];
						pi.prop = (int) (propbody.GetUserData() & 0xffff);
						pi.ragdoll = (int) (rdbody.GetUserData() & 0xffff);
						pi.speed = toUnits(approach);
						storeVec(dir, pi.dir);
						storeUnits(p, pi.pos);
						pi.ownspeed = toUnits(rdbody.GetPointVelocity(p).Dot(dir));
					}
				}
			}
		}

		// Anything but a floor under it (animation blend: the animation
		// stops where it runs into something)
		bool floor = std::fabs(manifold.mWorldSpaceNormal.GetY()) >= 0.7f;

		Block(b1, b2, floor, manifold.mWorldSpaceNormal.GetY());
		Block(b2, b1, floor, manifold.mWorldSpaceNormal.GetY());
	}

private:
	static void Block(const Body &body, const Body &other, bool floor, float ny)
	{
		uint64 data = body.GetUserData();

		if ((data & kRagdollUserFlag) == 0) {
			return;
		}

		bool otherragdoll = (other.GetUserData() & kRagdollUserFlag) != 0;

		if (otherragdoll && (other.GetUserData() & 0xffff) == (data & 0xffff)) {
			return; // its own parts
		}

		if (floor && !otherragdoll) {
			return;
		}

		int prop = (other.GetUserData() & kPropUserFlag) ? (int) (other.GetUserData() & 0xffff) : -1;

		if (prop >= 0 && propNoBlock(prop)) {
			return;
		}

		int handle = (int)(data & 0xffff);
		int part = (int)((data >> 16) & 0xff);

		if (handle >= 0 && handle < kMaxContactFlags && part < PDPHYS_MAX_RAGDOLL_PARTS) {
			g_RagdollNewBlock[handle][part] = 1; // a byte: no lock needed
			g_RagdollBlockNormalY[handle] = ny;
			g_RagdollBlockProp[handle] = prop;
			g_RagdollBlockKind[handle] = otherragdoll ? 2 : (other.GetObjectLayer() == Layers::STATIC || other.GetObjectLayer() == Layers::QUERY ? 0 : 1);
		}
	}

	static void Check(const Body &body, const Body &other, float speed, float ny)
	{
		uint64 data = body.GetUserData();

		if ((data & kRagdollUserFlag) == 0) {
			return;
		}

		// Not its own other parts
		if ((other.GetUserData() & kRagdollUserFlag) && (other.GetUserData() & 0xffff) == (data & 0xffff)) {
			return;
		}

		int handle = (int)(data & 0xffff);
		int part = (int)((data >> 16) & 0xff);

		if (handle >= 0 && handle < kMaxContactFlags && part < PDPHYS_MAX_RAGDOLL_PARTS
				&& speed > g_RagdollNewContact[handle][part]) {
			// Contacts are reported from the worker threads
			std::lock_guard<std::mutex> lock(g_ContactMutex);

			if (speed > g_RagdollNewContact[handle][part]) {
				g_RagdollNewContact[handle][part] = speed;
				g_RagdollNewContactNY[handle][part] = ny;
			}
		}
	}
};

struct World {
	BPLayerInterfaceImpl bpLayers;
	ObjectVsBroadPhaseLayerFilterImpl objVsBp;
	ObjectLayerPairFilterImpl objPair;
	RagdollContactListener contactListener;
	PhysicsSystem system;

	World()
	{
		system.Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints, bpLayers, objVsBp, objPair);
		system.SetContactListener(&contactListener);
		system.SetGravity(Vec3(0.0f, -9.81f, 0.0f));

		// More solver position iterations and less allowed penetration:
		// PD floors and walls have no thickness, so keep bodies well out of them
		PhysicsSettings settings = system.GetPhysicsSettings();
		settings.mNumPositionSteps = 4;
		settings.mPenetrationSlop = 0.01f;
		system.SetPhysicsSettings(settings);
	}
};

bool g_JoltInitialised = false;
TempAllocatorImpl *g_TempAllocator = nullptr;
JobSystem *g_JobSystem = nullptr;
static int g_JobThreads = 0;
World *g_World = nullptr;

char g_VersionString[32];

struct Ragdoll {
	bool used = false;
	int numparts = 0;
	BodyID bodies[PDPHYS_MAX_RAGDOLL_PARTS];
	Mat44 bodyToJoint[PDPHYS_MAX_RAGDOLL_PARTS]; // joint frame relative to body frame (metres)
	std::vector<Ref<Constraint>> constraints;
	Ref<GroupFilterTable> filter;
	Constraint *partConstraint[PDPHYS_MAX_RAGDOLL_PARTS]; // constraint to the parent (owned by 'constraints'), or null
	int partJoint[PDPHYS_MAX_RAGDOLL_PARTS];              // PDPHYS_JOINT_*
	int partParent[PDPHYS_MAX_RAGDOLL_PARTS];
	bool motorsOn = false;
	Mat44 lastTrack[PDPHYS_MAX_RAGDOLL_PARTS];   // animation blend: previous target body transforms
	bool lastTrackValid = false;
	float keepAwake = 0.0f; // seconds left during which its parts may not fall asleep
	bool inWater = false;   // floats: water surface at waterY (metres)
	float waterY = 0.0f;
	float partHalf[PDPHYS_MAX_RAGDOLL_PARTS];    // capsule half height (0 = sphere), metres
	float partRadius[PDPHYS_MAX_RAGDOLL_PARTS];  // metres
	struct OverlapPair { int a, b; float time; bool cut; }; // cut: a cut piece and the rest (see pdphysRagdollDetach)
	std::vector<OverlapPair> overlapPairs;       // parts that overlapped at death, collision off until they come apart
};

constexpr int kMaxRagdolls = 48; // GEVR: the largest Bodies stay setting
Ragdoll g_Ragdolls[kMaxRagdolls];

static bool ragdollLive(int handle)
{
	return handle >= 0 && handle < kMaxRagdolls && g_Ragdolls[handle].used;
}
int g_UnstraddleCount = 0; // parts put back on their parent's side of a wall (unstraddleWalls)

void destroyRagdoll(Ragdoll &rd);

// Kinematic prop bodies (doors, lifts, furniture)
struct PropBody {
	bool used = false;
	bool noBlock = false; // contacts with it don't end an animation blend (lifts)
	bool living = false;  // a living character's body (pdphysPropSetLiving)
	float lastMove = 0.0f; // metres it moved at the last pdphysPropMove/MoveScaled
	BodyID id;
	Vec3 origin = Vec3::sZero(); // metres, world position the shape was built around
};

constexpr int kMaxPropBodies = 256;
PropBody g_PropBodies[kMaxPropBodies];

static bool propLive(int handle)
{
	return handle >= 0 && handle < kMaxPropBodies && g_PropBodies[handle].used;
}

void destroyPropBody(PropBody &pb);

static bool propNoBlock(int handle)
{
	return propLive(handle) && g_PropBodies[handle].noBlock;
}

static bool propLiving(int handle)
{
	return propLive(handle) && g_PropBodies[handle].living;
}

static float propLastMove(int handle)
{
	return propLive(handle) ? g_PropBodies[handle].lastMove : 0.0f;
}

// Level geometry bodies currently in g_World
std::vector<BodyID> g_LevelBodies;
std::vector<int> g_LevelBodyTags; // room << 4 | group, parallel to g_LevelBodies
std::vector<int> g_LevelBodyKeys; // pdphysLevelAddMoverPart's key, or -1; parallel to g_LevelBodies
pdphysLevelStats g_LevelStats;

} // namespace

extern "C" const char *pdphysGetVersionString(void)
{
	snprintf(g_VersionString, sizeof(g_VersionString), "%d.%d.%d", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
	return g_VersionString;
}

extern "C" int pdphysInit(pdphysLogFn logfn)
{
	g_LogFn = logfn;

	if (g_JoltInitialised) {
		return g_World != nullptr;
	}

	RegisterDefaultAllocator();
	Trace = traceImpl;
	JPH_IF_ENABLE_ASSERTS(AssertFailed = assertFailedImpl;)

	Factory::sInstance = new Factory();
	RegisterTypes();
	g_JoltInitialised = true;

	g_TempAllocator = new TempAllocatorImpl(kTempAllocatorSize);
	// Worker threads for the physics step (the game's thread waits for the
	// step to finish, so it stays in lockstep with the game): up to 4, one
	// core left for the game. Single-threaded on a machine with 1-2 cores.
	{
		unsigned int cores = std::thread::hardware_concurrency();

		g_JobThreads = cores > 2 ? 2 : 0; // GEVR: leave cores for game, audio and XR
	}

	if (g_JobThreads > 0) {
		g_JobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, g_JobThreads);
	} else {
		g_JobSystem = new JobSystemSingleThreaded(cMaxPhysicsJobs);
	}

	g_World = new World();

	logf(PDPHYS_LOG_NOTE, "PHYSICS: Jolt %s initialised (max bodies %u, %d worker threads)",
			pdphysGetVersionString(), kMaxBodies, g_JobThreads);

	return 1;
}

extern "C" void pdphysShutdown(void)
{
	if (!g_JoltInitialised) {
		return;
	}

	for (Ragdoll &rd : g_Ragdolls) {
		destroyRagdoll(rd);
	}

	for (PropBody &pb : g_PropBodies) {
		destroyPropBody(pb);
	}

	pdphysLevelClear();

	delete g_World;
	g_World = nullptr;
	delete g_JobSystem;
	g_JobSystem = nullptr;
	delete g_TempAllocator;
	g_TempAllocator = nullptr;

	UnregisterTypes();
	delete Factory::sInstance;
	Factory::sInstance = nullptr;
	g_JoltInitialised = false;

	logf(PDPHYS_LOG_NOTE, "PHYSICS: shut down");
}

extern "C" int pdphysIsReady(void)
{
	return g_World != nullptr;
}

extern "C" int pdphysSelfTest(float *outrestheight, int *outsteps, float *outms)
{
	if (!g_JoltInitialised || !g_TempAllocator || !g_JobSystem) {
		return 0;
	}

	auto start = std::chrono::steady_clock::now();

	// Throwaway world so the real one stays untouched
	World *world = new World();
	BodyInterface &bi = world->system.GetBodyInterface();

	// Floor: top surface at y = 0
	BodyCreationSettings floorSettings(new BoxShape(Vec3(toMetres(1000.0f), toMetres(10.0f), toMetres(1000.0f))),
			RVec3(0.0f, toMetres(-10.0f), 0.0f), Quat::sIdentity(), EMotionType::Static, Layers::STATIC);
	BodyID floorId = bi.CreateAndAddBody(floorSettings, EActivation::DontActivate);

	// Sphere: radius 20 units, dropped from 200 units
	const float radius = 20.0f;
	BodyCreationSettings sphereSettings(new SphereShape(toMetres(radius)),
			RVec3(0.0f, toMetres(200.0f), 0.0f), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
	BodyID sphereId = bi.CreateAndAddBody(sphereSettings, EActivation::Activate);

	world->system.OptimizeBroadPhase();

	const float dt = 1.0f / 60.0f;
	int steps = 0;

	while (bi.IsActive(sphereId) && steps < 180) {
		world->system.Update(dt, 1, g_TempAllocator, g_JobSystem);
		steps++;
	}

	float resty = toUnits((float) bi.GetCenterOfMassPosition(sphereId).GetY());
	bool asleep = !bi.IsActive(sphereId);

	bi.RemoveBody(sphereId);
	bi.DestroyBody(sphereId);
	bi.RemoveBody(floorId);
	bi.DestroyBody(floorId);
	delete world;

	float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();

	if (outrestheight) *outrestheight = resty;
	if (outsteps) *outsteps = steps;
	if (outms) *outms = ms;

	// Resting centre should be ~radius above the floor
	return asleep && resty > radius - 2.0f && resty < radius + 2.0f;
}

extern "C" void pdphysLevelClear(void)
{
	{
		std::lock_guard<std::mutex> lock(g_LivingPassMutex);
		g_LivingPassPairs.clear();
	}

	if (g_World && !g_LevelBodies.empty()) {
		BodyInterface &bi = g_World->system.GetBodyInterface();
		bi.RemoveBodies(g_LevelBodies.data(), (int) g_LevelBodies.size());
		bi.DestroyBodies(g_LevelBodies.data(), (int) g_LevelBodies.size());
	}

	g_LevelBodies.clear();
	g_LevelBodyTags.clear();
	g_LevelBodyKeys.clear();
	g_LevelStats = pdphysLevelStats();
	g_GeneratedRooms.Set(nullptr, 0);
}

extern "C" void pdphysLevelRemoveRoomGroup(int roomnum, int group)
{
	if (!g_World) {
		return;
	}

	int tag = (roomnum << 4) | (group & 15);
	std::vector<BodyID> remove;

	for (size_t i = 0; i < g_LevelBodies.size();) {
		if (g_LevelBodyTags[i] == tag) {
			remove.push_back(g_LevelBodies[i]);
			g_LevelBodies.erase(g_LevelBodies.begin() + i);
			g_LevelBodyTags.erase(g_LevelBodyTags.begin() + i);
			g_LevelBodyKeys.erase(g_LevelBodyKeys.begin() + i);
		} else {
			i++;
		}
	}

	if (!remove.empty()) {
		BodyInterface &bi = g_World->system.GetBodyInterface();
		bi.RemoveBodies(remove.data(), (int) remove.size());
		bi.DestroyBodies(remove.data(), (int) remove.size());
	}
}

extern "C" int pdphysLevelAddRoom(int roomnum, const float *verts, int numverts, const int *indices, int numtris)
{
	return pdphysLevelAddRoomGroup(roomnum, 0, verts, numverts, indices, numtris);
}

static int levelAddRoomGroupAt(int roomnum, int group, int key, const float *pos, const float *verts, int numverts, const int *indices, int numtris);

extern "C" int pdphysLevelAddRoomGroup(int roomnum, int group, const float *verts, int numverts, const int *indices, int numtris)
{
	return levelAddRoomGroupAt(roomnum, group, -1, nullptr, verts, numverts, indices, numtris);
}

extern "C" int pdphysLevelAddMoverPart(int roomnum, int group, int key, const float *pos, const float *verts, int numverts, const int *indices, int numtris)
{
	return levelAddRoomGroupAt(roomnum, group, key, pos, verts, numverts, indices, numtris);
}

extern "C" void pdphysLevelMoveMover(int key, const float *pos)
{
	if (!g_World) {
		return;
	}

	BodyInterface &bi = g_World->system.GetBodyInterface();
	RVec3 p(toMetres(pos[0]), toMetres(pos[1]), toMetres(pos[2]));

	for (size_t i = 0; i < g_LevelBodies.size(); i++) {
		if (g_LevelBodyKeys[i] == key) {
			bi.SetPosition(g_LevelBodies[i], p, EActivation::DontActivate);
		}
	}
}

extern "C" void pdphysLevelRemoveMover(int key)
{
	if (!g_World) {
		return;
	}

	std::vector<BodyID> remove;

	for (size_t i = 0; i < g_LevelBodies.size();) {
		if (g_LevelBodyKeys[i] == key) {
			remove.push_back(g_LevelBodies[i]);
			g_LevelBodies.erase(g_LevelBodies.begin() + i);
			g_LevelBodyTags.erase(g_LevelBodyTags.begin() + i);
			g_LevelBodyKeys.erase(g_LevelBodyKeys.begin() + i);
		} else {
			i++;
		}
	}

	if (!remove.empty()) {
		BodyInterface &bi = g_World->system.GetBodyInterface();
		bi.RemoveBodies(remove.data(), (int) remove.size());
		bi.DestroyBodies(remove.data(), (int) remove.size());
	}
}

static int levelAddRoomGroupAt(int roomnum, int group, int key, const float *pos, const float *verts, int numverts, const int *indices, int numtris)
{
	if (!g_World || numverts <= 0 || numtris <= 0) {
		return 0;
	}

	g_LevelStats.rooms++;
	g_LevelStats.triangles += numtris;

	VertexList vertices;
	vertices.reserve(numverts);

	for (int i = 0; i < numverts; i++) {
		vertices.push_back(Float3(toMetres(verts[i * 3 + 0]), toMetres(verts[i * 3 + 1]), toMetres(verts[i * 3 + 2])));
	}

	IndexedTriangleList front;
	IndexedTriangleList back;
	front.reserve(numtris);
	back.reserve(numtris);

	for (int i = 0; i < numtris; i++) {
		uint32 a = (uint32) indices[i * 3 + 0];
		uint32 b = (uint32) indices[i * 3 + 1];
		uint32 c = (uint32) indices[i * 3 + 2];

		if (a >= (uint32) numverts || b >= (uint32) numverts || c >= (uint32) numverts) {
			continue;
		}

		front.push_back(IndexedTriangle(a, b, c));
		back.push_back(IndexedTriangle(a, c, b));
	}

	BodyInterface &bi = g_World->system.GetBodyInterface();
	bool ok = true;

	for (int side = 0; side < 2; side++) {
		MeshShapeSettings settings(vertices, side == 0 ? front : back);
		ShapeSettings::ShapeResult result = settings.Create();

		if (result.HasError()) {
			logf(PDPHYS_LOG_WARNING, "PHYSICS: room %d mesh failed: %s", roomnum, result.GetError().c_str());
			ok = false;
			break;
		}

		BodyCreationSettings bcs(result.Get(), pos ? RVec3(toMetres(pos[0]), toMetres(pos[1]), toMetres(pos[2])) : RVec3::sZero(), Quat::sIdentity(), EMotionType::Static,
				group >= PDPHYS_QUERY_GROUP_FIRST ? Layers::QUERY : Layers::STATIC);
		bcs.mUserData = (uint64) roomnum | ((uint64) (group & 0xff) << kLevelGroupShift)
				| (side == 1 && group >= PDPHYS_QUERY_GROUP_FIRST ? kLevelBackFlag : 0);

		BodyID id = bi.CreateAndAddBody(bcs, EActivation::DontActivate);

		if (id.IsInvalid()) {
			logf(PDPHYS_LOG_ERROR, "PHYSICS: out of bodies while adding room %d", roomnum);
			ok = false;
			break;
		}

		g_LevelBodies.push_back(id);
		g_LevelBodyTags.push_back((roomnum << 4) | (group & 15));
		g_LevelBodyKeys.push_back(key);
	}

	if (!ok) {
		g_LevelStats.failedrooms++;
	}

	return ok ? 1 : 0;
}

extern "C" void pdphysLevelFinish(pdphysLevelStats *stats)
{
	if (g_World) {
		g_World->system.OptimizeBroadPhase();
	}

	g_LevelStats.bodies = (int) g_LevelBodies.size();

	if (stats) {
		*stats = g_LevelStats;
	}
}

static bool ragdollPartFromBody(const BodyID &id, int *outhandle, int *outpart)
{
	BodyLockRead lock(g_World->system.GetBodyLockInterface(), id);

	if (!lock.Succeeded()) {
		return false;
	}

	uint64 data = lock.GetBody().GetUserData();

	if ((data & kRagdollUserFlag) == 0) {
		return false;
	}

	if (outhandle) {
		*outhandle = (int) (data & 0xffff);
	}

	if (outpart) {
		*outpart = (int) ((data >> 16) & 0xff);
	}

	return true;
}

extern "C" int pdphysRaycastRagdoll(const float from[3], const float to[3], float radius, float outhit[3], int *outhandle, int *outpart)
{
	if (!g_World) {
		return 0;
	}

	RVec3 origin = RVec3(vecFromUnits(from));
	Vec3 dir(toMetres(to[0] - from[0]), toMetres(to[1] - from[1]), toMetres(to[2] - from[2]));
	const NarrowPhaseQuery &query = g_World->system.GetNarrowPhaseQuery();

	// 1: a ray through ragdolls and level geometry; a body part has to come first
	{
		RRayCast ray(origin, dir);
		RayCastResult hit;
		RagdollAndLevelBPFilter bpfilter;
		RagdollAndLevelObjectFilter objfilter;

		if (query.CastRay(ray, hit, bpfilter, objfilter)) {
			if (ragdollPartFromBody(hit.mBodyID, outhandle, outpart)) {
				RVec3 point = ray.GetPointOnRay(hit.mFraction);

				if (outhit) {
					storeUnits(point, outhit);
				}

				return 1;
			}

			// A wall/floor first: shorten the search to it
			dir *= hit.mFraction;
		}
	}

	// 2: near misses: a sphere swept along the ray, against ragdolls only
	if (radius > 0.0f) {
		SphereShape sphere(toMetres(radius));
		sphere.SetEmbedded();
		RShapeCast cast(&sphere, Vec3::sReplicate(1.0f), RMat44::sTranslation(origin), dir);
		ShapeCastSettings settings;
		ClosestHitCollisionCollector<CastShapeCollector> collector;
		RagdollOnlyBPFilter bpfilter;
		RagdollOnlyObjectFilter objfilter;

		query.CastShape(cast, settings, RVec3::sZero(), collector, bpfilter, objfilter);

		if (collector.HadHit() && ragdollPartFromBody(collector.mHit.mBodyID2, outhandle, outpart)) {
			RVec3 point = RVec3(collector.mHit.mContactPointOn2);

			if (outhit) {
				storeUnits(point, outhit);
			}

			// Not through a wall to get there
			{
				RRayCast ray2(origin, Vec3(point - origin));
				RayCastResult hit2;
				LevelOnlyBPFilter bp2;
				LevelOnlyObjectFilter obj2;

				if (query.CastRay(ray2, hit2, bp2, obj2) && hit2.mFraction < 0.98f) {
					return 0;
				}
			}

			return 1;
		}
	}

	return 0;
}

namespace {

class RagdollWorldObjectFilter final : public ObjectLayerFilter {
public:
	bool ShouldCollide(ObjectLayer layer) const override
	{
		return layer == Layers::STATIC || layer == Layers::PROPS || layer == Layers::QUERY;
	}
};

// Level bodies as ragdolls collide with them (OnContactValidate's rules,
// without the per-ragdoll rooms); props always
// The rooms of every ragdoll together (for raycasts that don't say which
// ragdoll they're for): rebuilt when a ragdoll's rooms change
static RoomSet g_RagdollRoomsUnion;
static bool g_RagdollRoomsUnionDirty = true;

static const RoomSet &ragdollRoomsUnion()
{
	if (g_RagdollRoomsUnionDirty) {
		g_RagdollRoomsUnionDirty = false;
		memset(g_RagdollRoomsUnion.bits, 0, sizeof(g_RagdollRoomsUnion.bits));
		g_RagdollRoomsUnion.enabled = true;
		bool any = false;

		for (int h = 0; h < kMaxRagdolls && h < 256; h++) {
			if (!g_Ragdolls[h].used) {
				continue;
			}

			any = true;

			if (!g_RagdollRooms[h].enabled) {
				g_RagdollRoomsUnion.enabled = false; // one ragdoll sees everything
				break;
			}

			for (int i = 0; i < kMaxFilterRooms / 8; i++) {
				g_RagdollRoomsUnion.bits[i] |= g_RagdollRooms[h].bits[i];
			}
		}

		if (!any) {
			g_RagdollRoomsUnion.enabled = false; // no ragdolls: everything
		}
	}

	return g_RagdollRoomsUnion;
}

class RagdollWorldBodyFilter final : public BodyFilter {
public:
	// handle: the ragdoll the query is for (its rooms only), -1 for the
	// rooms of all ragdolls, -2 for any room
	explicit RagdollWorldBodyFilter(int handle = -1) : mHandle(handle) {}

	bool ShouldCollideLocked(const Body &body) const override
	{
		if (body.GetObjectLayer() != Layers::STATIC && body.GetObjectLayer() != Layers::QUERY) {
			return true;
		}

		// Rooms drawn in the same space but not shown from here (overlapping
		// rooms) don't count, as for the bodies themselves
		if (mHandle >= 0 && mHandle < 256) {
			if (!g_RagdollRooms[mHandle].Allows(body.GetUserData())) {
				return false;
			}
		} else if (mHandle == -1 && !ragdollRoomsUnion().Allows(body.GetUserData())) {
			return false;
		}

		int group = (int) ((body.GetUserData() >> kLevelGroupShift) & 0xff);

		if (group >= 32 || (g_RagdollLevelGroups & (1u << group)) == 0) {
			return false;
		}

		if (group < PDPHYS_QUERY_GROUP_FIRST && (g_RagdollLevelGroups >> PDPHYS_QUERY_GROUP_FIRST) != 0
				&& g_GeneratedRooms.Allows(body.GetUserData())) {
			return false;
		}

		return true;
	}

private:
	int mHandle;
};

class LevelGroupBodyFilter final : public BodyFilter {
public:
	explicit LevelGroupBodyFilter(unsigned int mask) : mMask(mask) {}

	bool ShouldCollideLocked(const Body &body) const override
	{
		int group = (int) ((body.GetUserData() >> kLevelGroupShift) & 0xff);
		return group < 32 && (mMask & (1u << group)) != 0 && g_QueryRooms.Allows(body.GetUserData());
	}

private:
	unsigned int mMask;
};

} // namespace

extern "C" int pdphysRaycastLevelGroups(const float from[3], const float to[3], unsigned int groupmask, float outhit[3], float outnormal[3])
{
	if (!g_World) {
		return 0;
	}

	RVec3 origin = RVec3(vecFromUnits(from));
	Vec3 dir(toMetres(to[0] - from[0]), toMetres(to[1] - from[1]), toMetres(to[2] - from[2]));
	RRayCast ray(origin, dir);
	RayCastResult hit;
	LevelOnlyBPFilter bpfilter;
	LevelAndQueryObjectFilter objfilter;
	LevelGroupBodyFilter bodyfilter(groupmask);

	if (!g_World->system.GetNarrowPhaseQuery().CastRay(ray, hit, bpfilter, objfilter, bodyfilter)) {
		return 0;
	}

	RVec3 point = ray.GetPointOnRay(hit.mFraction);

	if (outhit) {
		storeUnits(point, outhit);
	}

	if (outnormal) {
		BodyLockRead lock(g_World->system.GetBodyLockInterface(), hit.mBodyID);
		Vec3 n = Vec3::sAxisY();

		if (lock.Succeeded()) {
			n = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);

			if (n.Dot(dir) > 0.0f) {
				n = -n;
			}
		}

		storeVec(n, outnormal);
	}

	return 1;
}

// MOD: clipping checks - room filters (see RoomSet)
extern "C" void pdphysSetQueryRooms(const int *rooms, int num)
{
	g_QueryRooms.Set(rooms, num);
}

extern "C" void pdphysMarkRoomGenerated(int room)
{
	if (room >= 0 && room < kMaxFilterRooms) {
		g_GeneratedRooms.enabled = true;
		g_GeneratedRooms.bits[room >> 3] |= (uint8) (1u << (room & 7));
	}
}

extern "C" void pdphysSetRagdollLevelGroups(unsigned int groupmask)
{
	g_RagdollLevelGroups = groupmask;
}

extern "C" void pdphysRagdollSetRooms(int handle, const int *rooms, int num)
{
	if (handle >= 0 && handle < 256) {
		g_RagdollRooms[handle].Set(rooms, num);
		g_RagdollRoomsUnionDirty = true;
	}
}

// MOD: clipping checks - every contact of a vertical capsule swept against level groups
extern "C" int pdphysSweepCapsuleContacts(const float from[3], const float to[3], float radius, float halfheight,
		unsigned int groupmask, pdphysContact *out, int maxcontacts)
{
	if (!g_World || radius <= 0.0f || maxcontacts <= 0) {
		return 0;
	}

	Vec3 dir(toMetres(to[0] - from[0]), toMetres(to[1] - from[1]), toMetres(to[2] - from[2]));

	if (dir.LengthSq() < 1.0e-12f) {
		return 0;
	}

	RefConst<Shape> shape;

	if (halfheight > 0.5f) {
		shape = new CapsuleShape(toMetres(halfheight), toMetres(radius));
	} else {
		shape = new SphereShape(toMetres(radius));
	}

	RShapeCast cast(shape, Vec3::sReplicate(1.0f),
			RMat44::sTranslation(RVec3(vecFromUnits(from))), dir);
	ShapeCastSettings settings;
	settings.mBackFaceModeTriangles = EBackFaceMode::CollideWithBackFaces;
	settings.mBackFaceModeConvex = EBackFaceMode::CollideWithBackFaces;
	AllHitCollisionCollector<CastShapeCollector> collector;
	LevelOnlyBPFilter bpfilter;
	LevelAndQueryObjectFilter objfilter;
	LevelGroupBodyFilter bodyfilter(groupmask);

	g_World->system.GetNarrowPhaseQuery().CastShape(cast, settings, RVec3::sZero(), collector, bpfilter, objfilter, bodyfilter);
	collector.Sort();

	int n = 0;

	for (const ShapeCastResult &hit : collector.mHits) {
		Vec3 axis = hit.mPenetrationAxis;

		if (n >= maxcontacts) {
			break;
		}

		if (axis.LengthSq() < 1.0e-12f) {
			continue;
		}

		Vec3 nrm = -axis.Normalized();
		Vec3 p(hit.mContactPointOn2);

		out[n].fraction = hit.mFraction;
		storeUnits(p, out[n].point);
		storeVec(nrm, out[n].normal);
		n++;
	}

	return n;
}

// MOD: clipping checks - a vertical capsule swept against level groups
extern "C" int pdphysSweepCapsuleLevelGroups(const float from[3], const float to[3], float radius, float halfheight,
		unsigned int groupmask, float maxnormaly, float minnormaly, unsigned int covermask, float coverdist,
		float *outfraction, float outnormal[3])
{
	if (!g_World || radius <= 0.0f) {
		return 0;
	}

	Vec3 dir(toMetres(to[0] - from[0]), toMetres(to[1] - from[1]), toMetres(to[2] - from[2]));
	float dirlen = dir.Length();

	if (dirlen < 1.0e-6f) {
		return 0;
	}

	RefConst<Shape> shape;

	if (halfheight > 0.5f) {
		shape = new CapsuleShape(toMetres(halfheight), toMetres(radius));
	} else {
		shape = new SphereShape(toMetres(radius));
	}

	RShapeCast cast(shape, Vec3::sReplicate(1.0f),
			RMat44::sTranslation(RVec3(vecFromUnits(from))), dir);
	ShapeCastSettings settings;
	settings.mBackFaceModeTriangles = EBackFaceMode::CollideWithBackFaces;
	settings.mBackFaceModeConvex = EBackFaceMode::CollideWithBackFaces;
	AllHitCollisionCollector<CastShapeCollector> collector;
	LevelOnlyBPFilter bpfilter;
	LevelAndQueryObjectFilter objfilter;
	LevelGroupBodyFilter bodyfilter(groupmask);

	g_World->system.GetNarrowPhaseQuery().CastShape(cast, settings, RVec3::sZero(), collector, bpfilter, objfilter, bodyfilter);

	float best = 2.0f;
	Vec3 bestn = Vec3::sZero();
	Vec3 dirn = dir / dirlen;

	for (const ShapeCastResult &hit : collector.mHits) {
		Vec3 axis = hit.mPenetrationAxis;

		if (axis.LengthSq() < 1.0e-12f) {
			continue;
		}

		Vec3 n = -axis.Normalized();

		// Floors to stand on and flat ceilings are handled elsewhere
		if (n.GetY() >= maxnormaly || n.GetY() <= minnormaly) {
			continue;
		}

		// Only surfaces the move goes into (sliding along or moving away is fine)
		Vec3 nh(n.GetX(), 0.0f, n.GetZ());

		if (nh.LengthSq() < 1.0e-6f || dirn.Dot(nh.Normalized()) > -0.05f) {
			continue;
		}

		if (hit.mFraction >= best) {
			continue;
		}

		// Covered by other geometry (the game's own collision) right at it:
		// that geometry decides, not this surface
		if (covermask && coverdist > 0.0f) {
			Vec3 p(hit.mContactPointOn2);
			Vec3 span = n * toMetres(coverdist);
			RRayCast ray(RVec3(p + span), -2.0f * span);
			RayCastResult coverhit;
			LevelOnlyBPFilter coverbp;
			LevelAndQueryObjectFilter coverobj;
			LevelGroupBodyFilter coverbody(covermask);

			if (g_World->system.GetNarrowPhaseQuery().CastRay(ray, coverhit, coverbp, coverobj, coverbody)) {
				continue;
			}
		}

		{
			best = hit.mFraction;
			bestn = nh.Normalized();
		}
	}

	if (best > 1.0f) {
		return 0;
	}

	if (outfraction) {
		*outfraction = best;
	}

	if (outnormal) {
		storeVec(bestn, outnormal);
	}

	return 1;
}

static int raycastRagdollWorld(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom, int handle);

extern "C" int pdphysRaycast(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom)
{
	return raycastRagdollWorld(from, to, outhit, outnormal, outroom, -1);
}

extern "C" int pdphysRaycastAnyRoom(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom)
{
	return raycastRagdollWorld(from, to, outhit, outnormal, outroom, -2);
}

// The level as one ragdoll sees it: only the rooms it collides with
extern "C" int pdphysRagdollRaycast(int handle, const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom)
{
	return raycastRagdollWorld(from, to, outhit, outnormal, outroom, handle >= 0 && handle < 256 ? handle : -1);
}

static int raycastRagdollWorld(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom, int handle)
{
	if (!g_World) {
		return 0;
	}

	RVec3 origin = RVec3(vecFromUnits(from));
	Vec3 dir(toMetres(to[0] - from[0]), toMetres(to[1] - from[1]), toMetres(to[2] - from[2]));
	RRayCast ray(origin, dir);
	RayCastResult hit;

	// Level geometry and props only (never ragdoll parts)
	// MOD: clipping checks - the level geometry ragdolls collide with (the
	// generated surfaces, or the game's tiles), so the ragdoll's own checks
	// (ledges, sinking, reaching for the floor) see the same floor and
	// walls as its body does
	WorldOnlyBPFilter bpfilter;
	RagdollWorldObjectFilter objfilter;
	RagdollWorldBodyFilter bodyfilter(handle);

	if (!g_World->system.GetNarrowPhaseQuery().CastRay(ray, hit, bpfilter, objfilter, bodyfilter)) {
		return 0;
	}

	RVec3 point = ray.GetPointOnRay(hit.mFraction);

	if (outhit) {
		storeUnits(point, outhit);
	}

	BodyLockRead lock(g_World->system.GetBodyLockInterface(), hit.mBodyID);

	if (lock.Succeeded()) {
		const Body &body = lock.GetBody();

		if (outnormal) {
			Vec3 n = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);

			// Each surface exists twice (front + flipped), so report the
			// side facing the ray's origin
			if (n.Dot(dir) > 0.0f) {
				n = -n;
			}
			storeVec(n, outnormal);
		}

		if (outroom) {
			*outroom = (body.GetUserData() & kPropUserFlag) ? 0 : (int) (body.GetUserData() & 0xffffffffull);
		}
	}

	return 1;
}

namespace {

// PD units matrix (column-major) -> Jolt matrix in metres
Mat44 matFromUnits(const float m[16])
{
	Mat44 r(Vec4(m[0], m[1], m[2], 0.0f), Vec4(m[4], m[5], m[6], 0.0f), Vec4(m[8], m[9], m[10], 0.0f),
			Vec4(toMetres(m[12]), toMetres(m[13]), toMetres(m[14]), 1.0f));
	return r;
}

// Makes 'v' perpendicular to 'axis' (unit), falling back to any perpendicular
Vec3 perpendicular(Vec3 v, Vec3 axis)
{
	Vec3 p = v - axis * v.Dot(axis);

	if (p.LengthSq() < 1.0e-6f) {
		p = axis.GetNormalizedPerpendicular();
	}

	return p.Normalized();
}

void destroyRagdoll(Ragdoll &rd)
{
	if (!rd.used) {
		return;
	}

	if (g_World) {
		for (Ref<Constraint> &c : rd.constraints) {
			g_World->system.RemoveConstraint(c);
		}

		for (int i = 0; i < PDPHYS_MAX_RAGDOLL_PARTS; i++) {
			rd.partConstraint[i] = nullptr;
		}

		rd.motorsOn = false;
		rd.overlapPairs.clear();

		BodyInterface &bi = g_World->system.GetBodyInterface();

		for (int i = 0; i < rd.numparts; i++) {
			if (!rd.bodies[i].IsInvalid()) {
				bi.RemoveBody(rd.bodies[i]);
				bi.DestroyBody(rd.bodies[i]);
			}
		}
	}

	rd.constraints.clear();
	rd.filter = nullptr;
	rd.numparts = 0;
	rd.used = false;
	g_RagdollRoomsUnionDirty = true;
}

} // namespace

namespace {

// Closest distance between two segments p1-q1 and p2-q2
float segmentClosest(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, Vec3 *c1, Vec3 *c2)
{
	Vec3 d1 = q1 - p1;
	Vec3 d2 = q2 - p2;
	Vec3 r = p1 - p2;
	float a = d1.Dot(d1);
	float e = d2.Dot(d2);
	float f = d2.Dot(r);
	float sv, tv;

	if (a <= 1.0e-6f && e <= 1.0e-6f) {
		if (c1) *c1 = p1;
		if (c2) *c2 = p2;
		return (p1 - p2).Length();
	}

	if (a <= 1.0e-6f) {
		sv = 0.0f;
		tv = std::clamp(f / e, 0.0f, 1.0f);
	} else {
		float c = d1.Dot(r);

		if (e <= 1.0e-6f) {
			tv = 0.0f;
			sv = std::clamp(-c / a, 0.0f, 1.0f);
		} else {
			float b = d1.Dot(d2);
			float denom = a * e - b * b;
			sv = denom > 1.0e-6f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
			tv = (b * sv + f) / e;

			if (tv < 0.0f) {
				tv = 0.0f;
				sv = std::clamp(-c / a, 0.0f, 1.0f);
			} else if (tv > 1.0f) {
				tv = 1.0f;
				sv = std::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}

	if (c1) *c1 = p1 + d1 * sv;
	if (c2) *c2 = p2 + d2 * tv;

	return ((p1 + d1 * sv) - (p2 + d2 * tv)).Length();
}

float segmentDistance(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2)
{
	return segmentClosest(p1, q1, p2, q2, nullptr, nullptr);
}

// Distance between the core segments of two parts (PD units), including
// their extra capsules (feet)
float partDistance(const pdphysRagdollPart &x, const pdphysRagdollPart &y)
{
	Vec3 xa(x.a[0], x.a[1], x.a[2]);
	Vec3 xb = x.shape == PDPHYS_SHAPE_CAPSULE ? Vec3(x.b[0], x.b[1], x.b[2]) : xa;
	Vec3 ya(y.a[0], y.a[1], y.a[2]);
	Vec3 yb = y.shape == PDPHYS_SHAPE_CAPSULE ? Vec3(y.b[0], y.b[1], y.b[2]) : ya;
	float dist = segmentDistance(xa, xb, ya, yb);

	if (x.shape == PDPHYS_SHAPE_CAPSULE && x.footradius > 0.0f) {
		Vec3 fa(x.foota[0], x.foota[1], x.foota[2]), fb(x.footb[0], x.footb[1], x.footb[2]);

		dist = std::min(dist, segmentDistance(fa, fb, ya, yb));

		if (y.shape == PDPHYS_SHAPE_CAPSULE && y.footradius > 0.0f) {
			Vec3 ga(y.foota[0], y.foota[1], y.foota[2]), gb(y.footb[0], y.footb[1], y.footb[2]);

			dist = std::min(dist, segmentDistance(fa, fb, ga, gb));
			dist = std::min(dist, segmentDistance(xa, xb, ga, gb));
		}
	} else if (y.shape == PDPHYS_SHAPE_CAPSULE && y.footradius > 0.0f) {
		Vec3 ga(y.foota[0], y.foota[1], y.foota[2]), gb(y.footb[0], y.footb[1], y.footb[2]);

		dist = std::min(dist, segmentDistance(xa, xb, ga, gb));
	}

	return dist;
}

} // namespace

extern "C" int pdphysRagdollCreate(const pdphysRagdollPart *parts, int numparts)
{
	if (!g_World || numparts <= 0 || numparts > PDPHYS_MAX_RAGDOLL_PARTS) {
		return -1;
	}

	int handle = -1;

	for (int i = 0; i < kMaxRagdolls; i++) {
		if (!g_Ragdolls[i].used) {
			handle = i;
			break;
		}
	}

	if (handle < 0) {
		return -1;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();

	rd.used = true;
	rd.numparts = numparts;
	g_RagdollRooms[handle].enabled = false; // no room filter until told (pdphysRagdollSetRooms)
	g_RagdollRoomsUnionDirty = true;
	rd.motorsOn = false;
	rd.lastTrackValid = false;
	rd.keepAwake = 0.0f;
	rd.inWater = false;
	rd.overlapPairs.clear();

	for (int i = 0; i < PDPHYS_MAX_RAGDOLL_PARTS; i++) {
		g_RagdollNewContact[handle][i] = 0.0f;
		g_RagdollNewBlock[handle][i] = 0;
	}

	for (int i = 0; i < PDPHYS_MAX_RAGDOLL_PARTS; i++) {
		rd.partConstraint[i] = nullptr;
		rd.partJoint[i] = PDPHYS_JOINT_NONE;
		rd.partParent[i] = i < numparts ? parts[i].parent : -1;
	}

	// Body parts collide with each other (no arms through the torso), except
	// parts joined by a constraint and parts that already overlap in the
	// pose at death (those would otherwise be shoved apart violently)
	rd.filter = new GroupFilterTable(numparts);
	int disabledpairs = 0; (void) disabledpairs;

	for (int i = 0; i < numparts; i++) {
		for (int j = i + 1; j < numparts; j++) {
			bool disable = parts[i].parent == j || parts[j].parent == i;

			// The head (the sphere) never collides with the parts beside it
			// on the chest (the upper arms): arms flung back over the head
			// otherwise prop it up off the floor
			if (!disable && parts[i].parent >= 0 && parts[i].parent == parts[j].parent
					&& (parts[i].shape == PDPHYS_SHAPE_SPHERE || parts[j].shape == PDPHYS_SHAPE_SPHERE)) {
				disable = true;
			} else if (!disable) {
				// Only parts sunk well into each other in the pose at death
				// (a forearm across the chest); touching or slightly
				// overlapping parts keep colliding (the solver eases them
				// apart), so an arm can't be pulled into the chest later
				float dist = partDistance(parts[i], parts[j]);
				float slack = std::min(parts[i].radius, parts[j].radius) * 0.35f;
				disable = dist < parts[i].radius + parts[j].radius - slack;

				// Only for now: eased apart and back on once they've come
				// apart (unblockOverlaps)
				if (disable) {
					rd.overlapPairs.push_back({ i, j, 0.0f, false });
				}
			}

			if (disable) {
				rd.filter->DisableCollision(i, j);
				disabledpairs++;
			}
		}
	}

	for (int i = 0; i < numparts; i++) {
		const pdphysRagdollPart &part = parts[i];
		RefConst<Shape> shape;
		Vec3 pos;
		Quat rot = Quat::sIdentity();
		float radius = std::max(toMetres(part.radius), 0.01f);

		if (part.shape == PDPHYS_SHAPE_CAPSULE) {
			Vec3 a = vecFromUnits(part.a);
			Vec3 b = vecFromUnits(part.b);
			Vec3 ab = b - a;
			float len = ab.Length();
			float halfheight = std::max(len * 0.5f, 0.005f);

			pos = (a + b) * 0.5f;

			if (len > 1.0e-4f) {
				rot = Quat::sFromTo(Vec3::sAxisY(), ab / len);
			}

			shape = new CapsuleShape(halfheight, radius);
			rd.partHalf[i] = halfheight;
			rd.partRadius[i] = radius;

			// With a foot: both capsules in one shape, in the body's frame
			if (part.footradius > 0.0f) {
				Quat inv = rot.Conjugated();
				Vec3 fa = inv * (vecFromUnits(part.foota) - pos);
				Vec3 fb = inv * (vecFromUnits(part.footb) - pos);
				Vec3 fab = fb - fa;
				float flen = fab.Length();
				Quat frot = flen > 1.0e-4f ? Quat::sFromTo(Vec3::sAxisY(), fab / flen) : Quat::sIdentity();
				StaticCompoundShapeSettings compound;

				compound.AddShape(Vec3::sZero(), Quat::sIdentity(), shape);
				compound.AddShape((fa + fb) * 0.5f, frot,
						new CapsuleShape(std::max(flen * 0.5f, 0.005f), std::max(toMetres(part.footradius), 0.01f)));

				ShapeSettings::ShapeResult result = compound.Create();

				if (!result.HasError()) {
					shape = result.Get();
				}
			}
		} else {
			pos = vecFromUnits(part.a);
			shape = new SphereShape(radius);
			rd.partHalf[i] = 0.0f;
			rd.partRadius[i] = radius;
		}

		BodyCreationSettings bcs(shape, pos, rot, EMotionType::Dynamic, Layers::MOVING);
		bcs.mMotionQuality = EMotionQuality::LinearCast; // PD walls have no thickness
		bcs.mLinearDamping = 0.1f;
		bcs.mAngularDamping = 0.6f;
		bcs.mFriction = 0.8f;
		bcs.mRestitution = 0.0f;
		bcs.mCollisionGroup = CollisionGroup(rd.filter, (CollisionGroup::GroupID) handle, (CollisionGroup::SubGroupID) i);
		bcs.mLinearVelocity = vecFromUnits(part.linvel);
		bcs.mUserData = kRagdollUserFlag | ((uint64) i << 16) | (uint64) handle;

		// A real body's mass for the part, spread like the shape's
		if (part.mass > 0.0f) {
			bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
			bcs.mMassPropertiesOverride.mMass = part.mass;
		}

		BodyID id = bi.CreateAndAddBody(bcs, EActivation::Activate);

		if (id.IsInvalid()) {
			logf(PDPHYS_LOG_ERROR, "PHYSICS: out of bodies while creating ragdoll");
			rd.numparts = i;
			destroyRagdoll(rd);
			return -1;
		}

		rd.bodies[i] = id;

		Mat44 body = Mat44::sRotationTranslation(rot, pos);
		Mat44 joint = matFromUnits(part.jointmtx);
		rd.bodyToJoint[i] = body.InversedRotationTranslation() * joint;
	}

	// Constraints
	BodyLockInterfaceNoLock const &lockif = g_World->system.GetBodyLockInterfaceNoLock();

	for (int i = 0; i < numparts; i++) {
		const pdphysRagdollPart &part = parts[i];

		if (part.parent < 0 || part.parent >= numparts || part.joint == PDPHYS_JOINT_NONE) {
			continue;
		}

		Body *b1 = lockif.TryGetBody(rd.bodies[part.parent]);
		Body *b2 = lockif.TryGetBody(rd.bodies[i]);

		if (!b1 || !b2) {
			continue;
		}

		RVec3 anchor(toMetres(part.jointmtx[12]), toMetres(part.jointmtx[13]), toMetres(part.jointmtx[14]));
		Ref<TwoBodyConstraint> constraint;

		if (part.joint == PDPHYS_JOINT_HINGE) {
			Vec3 axis = dirFrom(part.axis);
			HingeConstraintSettings hs;
			hs.mSpace = EConstraintSpace::WorldSpace;
			hs.mPoint1 = hs.mPoint2 = anchor;
			hs.mHingeAxis1 = hs.mHingeAxis2 = axis;
			hs.mNormalAxis1 = perpendicular(dirFrom(part.axis2), axis);
			hs.mNormalAxis2 = perpendicular(dirFrom(part.axis3), axis);
			hs.mLimitsMin = part.limits[0];
			hs.mLimitsMax = part.limits[1];
			hs.mMaxFrictionTorque = 0.5f;
			constraint = hs.Create(*b1, *b2);
		} else {
			Vec3 twist = dirFrom(part.axis);
			Vec3 plane = perpendicular(dirFrom(part.axis2), twist);
			SwingTwistConstraintSettings ss;
			ss.mSpace = EConstraintSpace::WorldSpace;
			ss.mPosition1 = ss.mPosition2 = anchor;
			ss.mTwistAxis1 = ss.mTwistAxis2 = twist;
			ss.mPlaneAxis1 = ss.mPlaneAxis2 = plane;
			ss.mNormalHalfConeAngle = part.limits[0];
			ss.mPlaneHalfConeAngle = part.limits[1];
			ss.mTwistMinAngle = part.limits[2];
			ss.mTwistMaxAngle = part.limits[3];
			ss.mMaxFrictionTorque = 0.5f;
			constraint = ss.Create(*b1, *b2);
		}

		g_World->system.AddConstraint(constraint);
		rd.constraints.push_back(Ref<Constraint>(constraint.GetPtr()));
		rd.partConstraint[i] = constraint.GetPtr();
		rd.partJoint[i] = part.joint;
	}

	return handle;
}

extern "C" void pdphysRagdollDestroy(int handle)
{
	if (handle >= 0 && handle < kMaxRagdolls) {
		destroyRagdoll(g_Ragdolls[handle]);
	}
}

extern "C" void pdphysRagdollAddVelocity(int handle, int part, const float vel[3])
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();
	Vec3 v = vecFromUnits(vel);

	for (int i = 0; i < rd.numparts; i++) {
		// Wake every part, so the joints are solved together
		bi.ActivateBody(rd.bodies[i]);

		if (part < 0 || part == i) {
			bi.AddLinearVelocity(rd.bodies[i], v);
		}
	}
}

// Limits each part's speed relative to 'ref' (units/s, NULL = still) to
// maxspeed units/s (bodies squeezed by a lift, or let go by the animation
// blend while it was pulling them)
extern "C" void pdphysRagdollClampSpeed(int handle, const float ref[3], float maxspeed)
{
	if (!g_World || !ragdollLive(handle) || maxspeed <= 0.0f) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();
	Vec3 r = ref ? vecFromUnits(ref) : Vec3::sZero();
	float max = toMetres(maxspeed);

	for (int i = 0; i < rd.numparts; i++) {
		Vec3 v = bi.GetLinearVelocity(rd.bodies[i]) - r;

		if (v.Length() > max) {
			bi.SetLinearVelocity(rd.bodies[i], r + v.Normalized() * max);
		}
	}
}

extern "C" void pdphysRagdollPull(int handle, int part, const float target[3], float maxforce, float frequency)
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];

	if (part < 0 || part >= rd.numparts) {
		return;
	}

	BodyInterface &bi = g_World->system.GetBodyInterface();
	BodyID id = rd.bodies[part];
	float invmass = 0.0f;

	{
		BodyLockRead lock(g_World->system.GetBodyLockInterface(), id);

		if (!lock.Succeeded() || lock.GetBody().GetMotionProperties() == nullptr) {
			return;
		}

		invmass = lock.GetBody().GetMotionProperties()->GetInverseMass();
	}

	if (invmass <= 0.0f) {
		return;
	}

	for (int i = 0; i < rd.numparts; i++) {
		bi.ActivateBody(rd.bodies[i]);
	}

	float mass = 1.0f / invmass;
	float w = 2.0f * 3.14159265f * frequency;
	Vec3 x = Vec3(bi.GetCenterOfMassPosition(id));
	Vec3 v = bi.GetLinearVelocity(id);
	Vec3 t = vecFromUnits(target);
	Vec3 accel = (t - x) * (w * w) - v * (2.0f * w) - g_World->system.GetGravity();
	Vec3 force = accel * mass;
	float len = force.Length();

	if (len > maxforce && len > 0.0f) {
		force *= maxforce / len;
	}

	bi.AddForce(id, force);

	// Keep the held part from spinning wildly, and the whole thing from
	// being flung absurdly fast
	bi.SetAngularVelocity(id, bi.GetAngularVelocity(id) * 0.85f);

	if (v.Length() > 12.0f) {
		bi.SetLinearVelocity(id, v * (12.0f / v.Length()));
	}
}

extern "C" void pdphysRagdollGetPartPos(int handle, int part, float outpos[3])
{
	if (!g_World || !ragdollLive(handle)
			|| part < 0 || part >= g_Ragdolls[handle].numparts) {
		return;
	}

	RVec3 p = g_World->system.GetBodyInterface().GetCenterOfMassPosition(g_Ragdolls[handle].bodies[part]);
	storeUnits(p, outpos);
}

extern "C" int pdphysRagdollGetJoints(int handle, float (*outmtx)[16])
{
	if (!g_World || !ragdollLive(handle)) {
		return 0;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();

	for (int i = 0; i < rd.numparts; i++) {
		Mat44 joint = Mat44(bi.GetWorldTransform(rd.bodies[i])) * rd.bodyToJoint[i];
		float *m = outmtx[i];

		for (int c = 0; c < 3; c++) {
			Vec3 col = joint.GetColumn3(c);
			m[c * 4 + 0] = col.GetX();
			m[c * 4 + 1] = col.GetY();
			m[c * 4 + 2] = col.GetZ();
			m[c * 4 + 3] = 0.0f;
		}

		Vec3 t = joint.GetTranslation();
		m[12] = toUnits(t.GetX());
		m[13] = toUnits(t.GetY());
		m[14] = toUnits(t.GetZ());
		m[15] = 1.0f;
	}

	return rd.numparts;
}

// Stage 5: powered ragdoll ("muscle tone"). Joint motors drive each part
// towards the given target joint frames (only the rotations relative to the
// parent matter). torques[i] is the motor torque limit of part i's joint in
// N*m; 0 switches that joint's motor off (back to plain friction).
extern "C" void pdphysRagdollDriveToPose(int handle, const float (*targetjointmtx)[16], const float *torques, float frequency)
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();
	Quat bodyrot[PDPHYS_MAX_RAGDOLL_PARTS];
	bool anyon = false;

	if (frequency <= 0.0f) {
		frequency = 6.0f;
	}

	if (targetjointmtx) {
		for (int i = 0; i < rd.numparts; i++) {
			const float *m = targetjointmtx[i];
			Mat44 joint(Vec4(m[0], m[1], m[2], 0), Vec4(m[4], m[5], m[6], 0), Vec4(m[8], m[9], m[10], 0), Vec4(0, 0, 0, 1));
			// body = joint * inverse(bodyToJoint)
			Mat44 body = joint * rd.bodyToJoint[i].GetRotation().Transposed3x3();
			bodyrot[i] = body.GetQuaternion().Normalized();
		}
	}

	for (int i = 0; i < rd.numparts; i++) {
		Constraint *c = rd.partConstraint[i];
		int parent = rd.partParent[i];

		if (!c || parent < 0 || parent >= rd.numparts) {
			continue;
		}

		float torque = torques ? torques[i] : 0.0f;
		bool on = torque > 0.0f && targetjointmtx != nullptr;
		Quat rel = on ? (bodyrot[parent].Conjugated() * bodyrot[i]).Normalized() : Quat::sIdentity();

		if (rd.partJoint[i] == PDPHYS_JOINT_HINGE) {
			HingeConstraint *h = static_cast<HingeConstraint *>(c);

			if (on) {
				MotorSettings &ms = h->GetMotorSettings();
				ms.mSpringSettings = SpringSettings(ESpringMode::FrequencyAndDamping, frequency, 1.0f);
				ms.SetTorqueLimit(torque);
				h->SetMotorState(EMotorState::Position);
				h->SetTargetOrientationBS(rel);
				anyon = true;
			} else {
				h->SetMotorState(EMotorState::Off);
			}
		} else if (rd.partJoint[i] == PDPHYS_JOINT_SWINGTWIST) {
			SwingTwistConstraint *st = static_cast<SwingTwistConstraint *>(c);

			if (on) {
				MotorSettings &sw = st->GetSwingMotorSettings();
				MotorSettings &tw = st->GetTwistMotorSettings();
				sw.mSpringSettings = SpringSettings(ESpringMode::FrequencyAndDamping, frequency, 1.0f);
				tw.mSpringSettings = SpringSettings(ESpringMode::FrequencyAndDamping, frequency, 1.0f);
				sw.SetTorqueLimit(torque);
				tw.SetTorqueLimit(torque);
				st->SetSwingMotorState(EMotorState::Position);
				st->SetTwistMotorState(EMotorState::Position);
				st->SetTargetOrientationBS(rel);
				anyon = true;
			} else {
				st->SetSwingMotorState(EMotorState::Off);
				st->SetTwistMotorState(EMotorState::Off);
			}
		}
	}

	// Motors only act on awake bodies; keep the ragdoll awake while driven
	if (anyon) {
		for (int i = 0; i < rd.numparts; i++) {
			bi.ActivateBody(rd.bodies[i]);
		}
	}

	rd.motorsOn = anyon;
}

// Animation blend: each part's velocity is pulled towards what it needs to
// follow the target joint frames (world space, PD units): the animation's own
// motion between calls plus a correction for the remaining error (reached in
// about 'reach' seconds). weight 0 = untouched physics, 1 = follows the
// animation (while still colliding). skip[i] != 0 leaves part i alone.
extern "C" void pdphysRagdollTrackPose(int handle, const float (*targetjointmtx)[16], const int *skip, float weight, float dt, float reach)
{
	if (!g_World || !ragdollLive(handle) || dt <= 0.0f) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();

	weight = std::min(weight, 1.0f);

	reach = std::max(reach, dt);

	for (int i = 0; i < rd.numparts; i++) {
		Mat44 joint = matFromUnits(targetjointmtx[i]);
		Mat44 target = joint * rd.bodyToJoint[i].InversedRotationTranslation();
		Quat qt = target.GetQuaternion().Normalized();
		Vec3 pt = target.GetTranslation();

		if (weight > 0.0f && !(skip && skip[i])) {
			RMat44 cur = bi.GetWorldTransform(rd.bodies[i]);
			Quat qc = cur.GetQuaternion().Normalized();
			Vec3 pc = Vec3(cur.GetTranslation());

			// Animation's own velocity (from the previous target)
			Vec3 animlin = Vec3::sZero();
			Vec3 animang = Vec3::sZero();

			if (rd.lastTrackValid) {
				Quat ql = rd.lastTrack[i].GetQuaternion().Normalized();
				Quat dq = (qt * ql.Conjugated()).Normalized();
				Vec3 axis;
				float angle;

				if (dq.GetW() < 0.0f) {
					dq = -dq;
				}

				animlin = (pt - rd.lastTrack[i].GetTranslation()) / dt;
				dq.GetAxisAngle(axis, angle);

				if (angle > 1.0e-4f) {
					animang = axis * (angle / dt);
				}
			}

			// A target far from where the part is (an animation jump): leave
			// the part to the physics rather than fling it there
			if ((pt - pc).Length() > 1.0f) {
				rd.lastTrack[i] = target;
				continue;
			}

			// Error correction
			Quat eq = (qt * qc.Conjugated()).Normalized();
			Vec3 eaxis;
			float eangle;

			if (eq.GetW() < 0.0f) {
				eq = -eq;
			}

			eq.GetAxisAngle(eaxis, eangle);

			// Closing the gap to the animation. Limited, so a body held back
			// (a character, a lift's edge) isn't driven at the full speed
			// limit; not too much, or a body still catching up with the
			// shot's knock-back counted as blocked and the animation was cut
			// off after a few frames (1.5 m/s did that)
			Vec3 corr = (pt - pc) / reach;

			if (corr.Length() > 4.0f) {
				corr = corr.Normalized() * 4.0f;
			}

			Vec3 wantlin = animlin + corr;
			Vec3 wantang = animang + (eangle > 1.0e-4f ? eaxis * (eangle / reach) : Vec3::sZero());

			// Keep it sane (teleports, wild animation frames)
			// (6 m/s: death animations never move that fast, even on a lift;
			// the old 20 launched bodies when the target jumped)
			if (wantlin.Length() > 6.0f) {
				wantlin = wantlin.Normalized() * 6.0f;
			}

			if (wantang.Length() > 40.0f) {
				wantang = wantang.Normalized() * 40.0f;
			}

			Vec3 lin = bi.GetLinearVelocity(rd.bodies[i]);
			Vec3 ang = bi.GetAngularVelocity(rd.bodies[i]);

			bi.ActivateBody(rd.bodies[i]);
			bi.ResetSleepTimer(rd.bodies[i]); // a still animation frame mustn't let it fall asleep
			bi.SetLinearAndAngularVelocity(rd.bodies[i], lin + (wantlin - lin) * weight, ang + (wantang - ang) * weight);
		}

		rd.lastTrack[i] = target;
	}

	rd.lastTrackValid = true;
}

// Dismemberment: removes the constraint joining 'part' to its parent, so the
// part (and everything attached to it) becomes a separate piece.
extern "C" void pdphysRagdollDetach(int handle, int part)
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];

	if (part < 0 || part >= rd.numparts || !rd.partConstraint[part]) {
		return;
	}

	Constraint *c = rd.partConstraint[part];
	g_World->system.RemoveConstraint(c);

	for (size_t i = 0; i < rd.constraints.size(); i++) {
		if (rd.constraints[i].GetPtr() == c) {
			rd.constraints.erase(rd.constraints.begin() + i);
			break;
		}
	}

	rd.partConstraint[part] = nullptr;
	rd.partJoint[part] = PDPHYS_JOINT_NONE;

	// MOD: ragdoll physics - the cut piece collides with the rest of the body
	// (pairs that were off because they were joined, or sat side by side on
	// the chest like the head and the upper arms): a head carried by the
	// player went through its own body. On once the two have come apart, so
	// the piece isn't shoved out of the stump.
	if (rd.filter) {
		bool inpiece[PDPHYS_MAX_RAGDOLL_PARTS];
		int pairs = 0;

		for (int i = 0; i < rd.numparts; i++) {
			int a = i;

			while (a >= 0 && a != part) {
				a = rd.partParent[a];
			}

			inpiece[i] = a == part;
		}

		for (int i = 0; i < rd.numparts; i++) {
			for (int j = i + 1; j < rd.numparts; j++) {
				if (inpiece[i] == inpiece[j] || rd.filter->IsCollisionEnabled(i, j)) {
					continue;
				}

				bool listed = false;

				for (Ragdoll::OverlapPair &op : rd.overlapPairs) {
					if ((op.a == i && op.b == j) || (op.a == j && op.b == i)) {
						op.cut = true;
						listed = true;
					}
				}

				if (!listed) {
					rd.overlapPairs.push_back({ i, j, 0.0f, true });
				}

				pairs++;
			}
		}

		logf(PDPHYS_LOG_NOTE, "PHYSICS: ragdoll %d body %d cut off: %d pairs with the rest collide once apart", handle, part, pairs);
	}

	BodyInterface &bi = g_World->system.GetBodyInterface();

	for (int i = 0; i < rd.numparts; i++) {
		bi.ActivateBody(rd.bodies[i]);
	}
}

// Fastest new impact of each part since the last call, PD units/s (0 = none); clears them
extern "C" void pdphysRagdollTakeNewContactsEx(int handle, float *outspeeds, float *outny)
{
	for (int i = 0; i < PDPHYS_MAX_RAGDOLL_PARTS; i++) {
		float speed = 0.0f;
		float ny = 0.0f;

		if (handle >= 0 && handle < kMaxContactFlags) {
			speed = toUnits(g_RagdollNewContact[handle][i]);
			ny = g_RagdollNewContactNY[handle][i];
			g_RagdollNewContact[handle][i] = 0.0f;
		}

		if (outspeeds) {
			outspeeds[i] = speed;
		}

		if (outny) {
			outny[i] = ny;
		}
	}
}

extern "C" void pdphysRagdollTakeNewContacts(int handle, float *outspeeds)
{
	pdphysRagdollTakeNewContactsEx(handle, outspeeds, nullptr);
}

extern "C" int pdphysTakePropImpacts(pdphysPropImpact *out, int max)
{
	std::lock_guard<std::mutex> lock(g_ContactMutex);
	int n = g_NumPropImpacts < max ? g_NumPropImpacts : max;

	for (int i = 0; i < n; i++) {
		out[i] = g_PropImpacts[i];
	}

	g_NumPropImpacts = 0;

	return n;
}

// What the last non-floor contact was: its normal's y and 0 level, 1 prop or
// character, 2 another body (diagnostics)
extern "C" void pdphysRagdollLastBlock(int handle, float *outny, int *outkind, int *outprop)
{
	*outny = 0.0f;
	*outkind = -1;
	*outprop = -1;

	if (handle >= 0 && handle < kMaxContactFlags) {
		*outny = g_RagdollBlockNormalY[handle];
		*outkind = g_RagdollBlockKind[handle];
		*outprop = g_RagdollBlockProp[handle];
	}
}

extern "C" void pdphysPropSetLiving(int handle)
{
	if (propLive(handle)) {
		g_PropBodies[handle].living = true;
	}
}

extern "C" void pdphysSetLivingPassThrough(int passthrough)
{
	g_LivingPassThrough = passthrough != 0;
}

extern "C" void pdphysSetSlopeGrip(float grip)
{
	g_SlopeGrip = grip;
}

// Contacts with this prop don't end an animation blend (a lift the body rides)
extern "C" void pdphysPropSetNoBlock(int handle, int noblock)
{
	if (propLive(handle)) {
		g_PropBodies[handle].noBlock = noblock != 0;
	}
}

// Water around the ragdoll: its surface height (PD units), or inwater 0
extern "C" void pdphysRagdollSetWater(int handle, int inwater, float surfacey)
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];

	if (inwater && !rd.inWater) {
		BodyInterface &bi = g_World->system.GetBodyInterface();

		for (int i = 0; i < rd.numparts; i++) {
			bi.ActivateBody(rd.bodies[i]);
		}
	}

	rd.inWater = inwater != 0;
	rd.waterY = toMetres(surfacey);
}

// The surface under the body: snow and carpet grip, metal lets it slide
extern "C" void pdphysRagdollSetFriction(int handle, float friction)
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();

	for (int i = 0; i < rd.numparts; i++) {
		bi.SetFriction(rd.bodies[i], friction);
	}
}

// Keeps every part of the ragdoll awake (no sleeping) for this many seconds
extern "C" void pdphysRagdollKeepAwake(int handle, float seconds)
{
	if (!g_World || !ragdollLive(handle)) {
		return;
	}

	g_Ragdolls[handle].keepAwake = seconds;
}

// Parts that started touching something other than a floor since the last
// call (1/0 per part); clears them
extern "C" void pdphysRagdollTakeNewBlocks(int handle, int *outflags)
{
	for (int i = 0; i < PDPHYS_MAX_RAGDOLL_PARTS; i++) {
		outflags[i] = 0;

		if (handle >= 0 && handle < kMaxContactFlags) {
			outflags[i] = g_RagdollNewBlock[handle][i];
			g_RagdollNewBlock[handle][i] = 0;
		}
	}
}

extern "C" int pdphysRagdollIsAsleep(int handle)
{
	if (!g_World || !ragdollLive(handle)) {
		return 1;
	}

	Ragdoll &rd = g_Ragdolls[handle];
	BodyInterface &bi = g_World->system.GetBodyInterface();

	for (int i = 0; i < rd.numparts; i++) {
		if (bi.IsActive(rd.bodies[i])) {
			return 0;
		}
	}

	return 1;
}

namespace {

// Level geometry (walls) between two points: hit position and the wall's
// normal facing 'from'
bool levelHitBetween(RVec3 from, RVec3 to, RVec3 &outpoint, Vec3 &outnormal, int handle)
{
	Vec3 dir = Vec3(to - from);

	if (dir.LengthSq() < 1.0e-8f) {
		return false;
	}

	RRayCast ray(from, dir);
	RayCastResult hit;
	LevelOnlyBPFilter bpfilter;
	// MOD: clipping checks - the walls ragdolls collide with (generated
	// surfaces in generated rooms, the game's tiles elsewhere): with the game's
	// tiles only, parts were put back across walls the body can't touch
	// (invisible walls at window sills, ledges, railings) and pulled onto ledges
	LevelAndQueryObjectFilter objfilter;
	RagdollWorldBodyFilter bodyfilter(handle);

	if (!g_World->system.GetNarrowPhaseQuery().CastRay(ray, hit, bpfilter, objfilter, bodyfilter)) {
		return false;
	}

	outpoint = ray.GetPointOnRay(hit.mFraction);

	BodyLockRead lock(g_World->system.GetBodyLockInterface(), hit.mBodyID);

	if (!lock.Succeeded()) {
		return false;
	}

	outnormal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, outpoint);

	if (outnormal.Dot(dir) > 0.0f) {
		outnormal = -outnormal;
	}

	return true;
}

/**
 * Body parts that overlapped in the pose at death (a forearm across the
 * chest, a hand on the hip) can't collide at first, or they'd be shoved
 * apart violently. Left like that, a forearm later swings straight through
 * the chest. For the first second they're eased apart; each pair gets its
 * collision back as soon as the two capsules are apart. Pairs still inside
 * each other after 3 s are left without it for good: the joints hold them
 * there (the Skedar's legs, tail and trunk), and easing them on and on,
 * then switching the collision back on, had the body crawl about and then
 * fly apart.
 * The easing is scaled to the frame's length.
 */
#define OVERLAP_EASE_TIME 1.0f // s: how long overlapping pairs are eased apart
#define OVERLAP_GIVEUP    3.0f // s: still overlapping by then -> left without collision
void unblockOverlaps(float dt)
{
	BodyInterface &bi = g_World->system.GetBodyInterface();

	for (int h = 0; h < kMaxRagdolls; h++) {
		Ragdoll &rd = g_Ragdolls[h];

		if (!rd.used || rd.overlapPairs.empty() || !rd.filter) {
			continue;
		}

		for (size_t k = 0; k < rd.overlapPairs.size();) {
			Ragdoll::OverlapPair &op = rd.overlapPairs[k];
			RMat44 ta = bi.GetCenterOfMassTransform(rd.bodies[op.a]);
			RMat44 tb = bi.GetCenterOfMassTransform(rd.bodies[op.b]);
			Vec3 ua = ta.GetAxisY() * rd.partHalf[op.a];
			Vec3 ub = tb.GetAxisY() * rd.partHalf[op.b];
			Vec3 ca = Vec3(ta.GetTranslation());
			Vec3 cb = Vec3(tb.GetTranslation());
			Vec3 pa, pb;
			float dist = segmentClosest(ca - ua, ca + ua, cb - ub, cb + ub, &pa, &pb);
			float touch = rd.partRadius[op.a] + rd.partRadius[op.b];

			op.time += dt;

			if (dist > touch + 0.02f) {
				rd.filter->EnableCollision(op.a, op.b);
				rd.overlapPairs.erase(rd.overlapPairs.begin() + k);
			} else if (op.cut) {
				k++; // not eased apart, and never given up on
			} else if (op.time > OVERLAP_GIVEUP) {
				rd.overlapPairs.erase(rd.overlapPairs.begin() + k);
			} else if (op.time > OVERLAP_EASE_TIME) {
				k++;
			} else {
				// Still inside each other: eased apart (a forearm drifts out
				// of the chest instead of staying in it, or popping out
				// when the collision comes back on)
				Vec3 dir = pa - pb;
				float len = dir.Length();

				if (len > 1.0e-4f) {
					dir /= len;
				} else {
					dir = Vec3(ta.GetTranslation() - tb.GetTranslation());
					dir = dir.LengthSq() > 1.0e-8f ? dir.Normalized() : Vec3(0, 1, 0);
				}

				float sep = (bi.GetLinearVelocity(rd.bodies[op.a]) - bi.GetLinearVelocity(rd.bodies[op.b])).Dot(dir);
				const float want = 0.35f; // m/s apart

				if (sep < want) {
					Vec3 dv = dir * ((want - sep) * 0.5f * std::min(dt * 60.0f, 1.0f));
					bi.AddLinearVelocity(rd.bodies[op.a], dv);
					bi.AddLinearVelocity(rd.bodies[op.b], -dv);
				}

				k++;
			}
		}
	}
}

/**
 * PD's walls have no thickness. A body part can't pass through one, but two
 * parts joined together can end up on either side of it (a body slumped
 * against a railing: the hips on the walkway, the chest past the glass, the
 * joint between them through the wall). After each step, the line from each
 * part through its joint to its child is checked against the level; when it
 * crosses a wall, the child (and everything hanging off it) is put back on
 * the parent's side. Lines crossing within kTopEdge of the top of a wall are
 * left alone: that's a body folded over a railing, which is fine.
 */
void unstraddleWalls()
{
	const float kTopEdge = toMetres(15.0f);
	BodyInterface &bi = g_World->system.GetBodyInterface();

	for (int h = 0; h < kMaxRagdolls; h++) {
		Ragdoll &rd = g_Ragdolls[h];

		if (!rd.used || rd.numparts <= 1 || !bi.IsActive(rd.bodies[0])) {
			continue;
		}

		for (int i = 1; i < rd.numparts; i++) {
			int p = rd.partParent[i];

			if (p < 0 || p >= rd.numparts || rd.partConstraint[i] == nullptr) {
				continue;
			}

			RMat44 child = bi.GetWorldTransform(rd.bodies[i]);
			RVec3 pc = bi.GetCenterOfMassPosition(rd.bodies[p]);
			RVec3 cc = bi.GetCenterOfMassPosition(rd.bodies[i]);
			RVec3 joint = (child * rd.bodyToJoint[i]).GetTranslation();
			RVec3 hitpt;
			Vec3 n;

			if (!levelHitBetween(pc, joint, hitpt, n, h) && !levelHitBetween(joint, cc, hitpt, n, h)) {
				continue;
			}

			if (std::fabs(n.GetY()) > 0.7f) {
				continue; // floors and ceilings: the solver handles those
			}

			// Folded over the top of the wall. A railing can be well above the
			// hips: the chest hanging over it while the hips are still on this
			// side is a body going over, not through (the chest was being put
			// back on the hips' side). Tried at a few heights.
			{
				bool over = false;

				for (int lift = 1; lift <= 3 && !over; lift++) {
					RVec3 up(0.0f, kTopEdge * lift, 0.0f);
					RVec3 dummy;
					Vec3 dn;

					over = !levelHitBetween(pc + up, joint + up, dummy, dn, h) && !levelHitBetween(joint + up, cc + up, dummy, dn, h);
				}

				if (over) {
					continue;
				}
			}

			// n faces the parent. How far past the wall is the child?
			float behind = n.Dot(Vec3(cc - hitpt));

			if (behind >= 0.0f) {
				continue;
			}

			float radius = 0.0f;

			{
				BodyLockRead lock(g_World->system.GetBodyLockInterface(), rd.bodies[i]);

				if (lock.Succeeded()) {
					radius = lock.GetBody().GetShape()->GetInnerRadius();
				}
			}

			Vec3 delta = n * (-behind + radius + 0.01f);

			// Move the child and its descendants
			for (int j = i; j < rd.numparts; j++) {
				int a = j;

				while (a >= 0 && a != i) {
					a = rd.partParent[a];
				}

				if (a != i) {
					continue;
				}

				RVec3 pos = bi.GetPosition(rd.bodies[j]);
				Vec3 vel = bi.GetLinearVelocity(rd.bodies[j]);
				float into = vel.Dot(n);

				if (into < 0.0f) {
					vel -= n * into;
				}

				bi.SetPosition(rd.bodies[j], pos + delta, EActivation::Activate);
				bi.SetLinearVelocity(rd.bodies[j], vel);
			}

			g_UnstraddleCount++;
		}
	}
}

} // namespace

extern "C" void pdphysStep(float dt)
{
	if (!g_World || dt <= 0.0f) {
		return;
	}

	// Keep each step at or below 1/60 s for stable joints
	int steps = (int) std::ceil(dt * 60.0f - 0.001f);

	steps = std::max(steps, 1);

	// Up to 8 steps (7.5 fps): props were moved over the full dt, so a
	// shorter step would leave them behind (lifts jerking under bodies)
	if (steps > 8) {
		steps = 8;
		dt = 8.0f / 60.0f;
	}

	// Bodies just handed over by the animation blend: held still by the
	// animation, they'd fall asleep at once (floating, or frozen mid-pose)
	{
		BodyInterface &bi = g_World->system.GetBodyInterface();

		for (int h = 0; h < kMaxRagdolls; h++) {
			Ragdoll &rd = g_Ragdolls[h];

			if (!rd.used || rd.keepAwake <= 0.0f) {
				continue;
			}

			for (int i = 0; i < rd.numparts; i++) {
				bi.ActivateBody(rd.bodies[i]);
				bi.ResetSleepTimer(rd.bodies[i]);
			}

			rd.keepAwake -= dt;
		}
	}

	// Bodies in water float: the trunk and head lighter than water, the limbs
	// nearly as buoyant, all slowed down by it
	{
		BodyInterface &bi = g_World->system.GetBodyInterface();
		const float g = 9.81f;

		for (int h = 0; h < kMaxRagdolls; h++) {
			Ragdoll &rd = g_Ragdolls[h];

			if (!rd.used || !rd.inWater) {
				continue;
			}

			for (int i = 0; i < rd.numparts; i++) {
				RVec3 c = bi.GetCenterOfMassPosition(rd.bodies[i]);
				float r = rd.partRadius[i] > 0.01f ? rd.partRadius[i] : 0.05f;
				float f = ((rd.waterY - (float) c.GetY()) + r) / (2.0f * r);

				if (f <= 0.0f) {
					continue;
				}

				f = std::min(f, 1.0f);

				float lift = (rd.partParent[i] < 0 || i <= 2) ? 1.15f : 1.07f; // pelvis, chest, head / limbs
				Vec3 v = bi.GetLinearVelocity(rd.bodies[i]);
				Vec3 w = bi.GetAngularVelocity(rd.bodies[i]);
				float drag = 1.0f - std::min(1.0f, 2.5f * dt * f);

				v.SetY(v.GetY() + g * lift * f * dt);
				bi.SetLinearAndAngularVelocity(rd.bodies[i], v * drag, w * (1.0f - std::min(1.0f, 2.0f * dt * f)));
			}
		}
	}

	g_World->system.Update(dt, steps, g_TempAllocator, g_JobSystem);

	unblockOverlaps(dt);
	unstraddleWalls();
}

/* ---------------- Kinematic props (stage 4) ---------------- */

namespace {

void destroyPropBody(PropBody &pb)
{
	if (pb.used && g_World) {
		BodyInterface &bi = g_World->system.GetBodyInterface();
		bi.RemoveBody(pb.id);
		bi.DestroyBody(pb.id);
	}

	pb.used = false;
}

} // namespace

extern "C" int pdphysPropCreate(const pdphysPiece *pieces, int numpieces, const float origin[3])
{
	if (!g_World || numpieces <= 0) {
		return -1;
	}

	int handle = -1;

	for (int i = 0; i < kMaxPropBodies; i++) {
		if (!g_PropBodies[i].used) {
			handle = i;
			break;
		}
	}

	if (handle < 0) {
		return -1;
	}

	Vec3 org = vecFromUnits(origin);
	StaticCompoundShapeSettings compound;
	Ref<ShapeSettings> single;
	int numhulls = 0;

	for (int p = 0; p < numpieces; p++) {
		const pdphysPiece &piece = pieces[p];
		Array<Vec3> points;

		if (piece.numverts < 3 || piece.numverts > PDPHYS_MAX_PIECE_VERTS) {
			continue;
		}

		for (int v = 0; v < piece.numverts; v++) {
			points.push_back(vecFromUnits(&piece.verts[v * 3]) - org);
		}

		if (piece.thickness > 0.0f) {
			// Flat polygon: give it some thickness along its normal
			Vec3 n = (points[1] - points[0]).Cross(points[2] - points[0]);

			if (n.LengthSq() < 1.0e-12f) {
				continue;
			}

			n = n.Normalized() * (toMetres(piece.thickness) * 0.5f);
			int count = (int) points.size();

			for (int v = 0; v < count; v++) {
				points.push_back(points[v] - n);
				points[v] += n;
			}
		}

		Ref<ConvexHullShapeSettings> hull = new ConvexHullShapeSettings(points, 0.005f);

		if (hull->Create().HasError()) {
			continue;
		}

		compound.AddShape(Vec3::sZero(), Quat::sIdentity(), hull);
		single = hull;
		numhulls++;
	}

	if (numhulls == 0) {
		return -1;
	}

	ShapeSettings::ShapeResult result = numhulls == 1 ? single->Create() : compound.Create();

	if (result.HasError()) {
		logf(PDPHYS_LOG_WARNING, "PHYSICS: prop shape failed: %s", result.GetError().c_str());
		return -1;
	}

	BodyCreationSettings bcs(result.Get(), org, Quat::sIdentity(), EMotionType::Kinematic, Layers::PROPS);
	bcs.mFriction = 0.8f;
	bcs.mUserData = kPropUserFlag | (uint64) handle;
	BodyID id = g_World->system.GetBodyInterface().CreateAndAddBody(bcs, EActivation::DontActivate);

	if (id.IsInvalid()) {
		return -1;
	}

	g_PropBodies[handle].used = true;
	g_PropBodies[handle].noBlock = false;
	g_PropBodies[handle].living = false;
	g_PropBodies[handle].lastMove = 0.0f;
	g_PropBodies[handle].id = id;
	g_PropBodies[handle].origin = org;

	return handle;
}

extern "C" int pdphysPropCreateMesh(const float *verts, int numverts, const int *indices, int numtris, const float origin[3])
{
	if (!g_World || numverts < 3 || numtris <= 0) {
		return -1;
	}

	int handle = -1;

	for (int i = 0; i < kMaxPropBodies; i++) {
		if (!g_PropBodies[i].used) {
			handle = i;
			break;
		}
	}

	if (handle < 0) {
		return -1;
	}

	Vec3 org = vecFromUnits(origin);
	VertexList vertices;
	IndexedTriangleList front;
	IndexedTriangleList back;

	vertices.reserve(numverts);

	for (int i = 0; i < numverts; i++) {
		Vec3 v = vecFromUnits(&verts[i * 3]) - org;
		vertices.push_back(Float3(v.GetX(), v.GetY(), v.GetZ()));
	}

	for (int i = 0; i < numtris; i++) {
		uint32 a = (uint32) indices[i * 3 + 0];
		uint32 b = (uint32) indices[i * 3 + 1];
		uint32 c = (uint32) indices[i * 3 + 2];

		if (a >= (uint32) numverts || b >= (uint32) numverts || c >= (uint32) numverts || a == b || b == c || a == c) {
			continue;
		}

		front.push_back(IndexedTriangle(a, b, c));
		back.push_back(IndexedTriangle(a, c, b));
	}

	if (front.empty()) {
		return -1;
	}

	// Both windings, as for the level (a body can come from either side)
	StaticCompoundShapeSettings compound;
	compound.AddShape(Vec3::sZero(), Quat::sIdentity(), new MeshShapeSettings(vertices, front));
	compound.AddShape(Vec3::sZero(), Quat::sIdentity(), new MeshShapeSettings(vertices, back));
	ShapeSettings::ShapeResult result = compound.Create();

	if (result.HasError()) {
		logf(PDPHYS_LOG_WARNING, "PHYSICS: prop mesh failed: %s", result.GetError().c_str());
		return -1;
	}

	BodyCreationSettings bcs(result.Get(), org, Quat::sIdentity(), EMotionType::Kinematic, Layers::PROPS);
	bcs.mFriction = 0.8f;
	bcs.mUserData = kPropUserFlag | (uint64) handle;
	// Meshes have no volume: give the kinematic body made-up mass properties
	bcs.mOverrideMassProperties = EOverrideMassProperties::MassAndInertiaProvided;
	bcs.mMassPropertiesOverride.mMass = 1.0f;
	bcs.mMassPropertiesOverride.mInertia = Mat44::sIdentity();
	BodyID id = g_World->system.GetBodyInterface().CreateAndAddBody(bcs, EActivation::DontActivate);

	if (id.IsInvalid()) {
		return -1;
	}

	g_PropBodies[handle].used = true;
	g_PropBodies[handle].noBlock = false;
	g_PropBodies[handle].living = false;
	g_PropBodies[handle].lastMove = 0.0f;
	g_PropBodies[handle].id = id;
	g_PropBodies[handle].origin = org;

	return handle;
}

extern "C" void pdphysPropMove(int handle, const float origin[3], float dt)
{
	if (!g_World || !propLive(handle)) {
		return;
	}

	PropBody &pb = g_PropBodies[handle];
	Vec3 target = vecFromUnits(origin);
	BodyInterface &bi = g_World->system.GetBodyInterface();

	pb.lastMove = (target - Vec3(bi.GetPosition(pb.id))).Length();

	if (dt > 0.0f) {
		// Gives the body a velocity, so a ragdoll on a lift rides with it
		bi.MoveKinematic(pb.id, target, Quat::sIdentity(), dt);
	} else {
		// Teleport without velocity: solid, but doesn't shove (and doesn't
		// wake sleeping bodies)
		bi.SetPosition(pb.id, target, EActivation::DontActivate);
		bi.SetLinearAndAngularVelocity(pb.id, Vec3::sZero(), Vec3::sZero());
	}
}

// Like pdphysPropMove, but the body is put in place and only given velscale
// of the speed it moved at: a character walking into bodies nudges them
// instead of shoving them (the overlap is still resolved, without speed)
extern "C" void pdphysPropMoveScaled(int handle, const float origin[3], float dt, float velscale)
{
	if (velscale >= 1.0f || dt <= 0.0f) {
		pdphysPropMove(handle, origin, velscale > 0.0f ? dt : 0.0f);
		return;
	}

	if (!g_World || !propLive(handle)) {
		return;
	}

	PropBody &pb = g_PropBodies[handle];
	Vec3 target = vecFromUnits(origin);
	BodyInterface &bi = g_World->system.GetBodyInterface();
	Vec3 vel = (target - Vec3(bi.GetPosition(pb.id))) / dt * velscale;

	pb.lastMove = (target - Vec3(bi.GetPosition(pb.id))).Length();

	bi.SetPosition(pb.id, target, vel.LengthSq() > 1.0e-6f ? EActivation::Activate : EActivation::DontActivate);
	bi.SetLinearAndAngularVelocity(pb.id, vel, Vec3::sZero());
}

// Moves and turns a prop (a swinging door): rot is its rotation from how it
// was built, origin where the body's origin goes
extern "C" void pdphysPropMoveRot(int handle, const float origin[3], const float rot[4], float dt)
{
	if (!g_World || !propLive(handle)) {
		return;
	}

	PropBody &pb = g_PropBodies[handle];
	Vec3 target = vecFromUnits(origin);
	Quat q = Quat(rot[0], rot[1], rot[2], rot[3]).Normalized();
	BodyInterface &bi = g_World->system.GetBodyInterface();

	if (dt > 0.0f) {
		bi.MoveKinematic(pb.id, target, q, dt);
	} else {
		bi.SetPositionAndRotation(pb.id, target, q, EActivation::DontActivate);
		bi.SetLinearAndAngularVelocity(pb.id, Vec3::sZero(), Vec3::sZero());
	}
}

extern "C" void pdphysPropDestroy(int handle)
{
	if (handle >= 0 && handle < kMaxPropBodies) {
		destroyPropBody(g_PropBodies[handle]);
	}
}
