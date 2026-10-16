#ifndef _PDPHYSICS_H
#define _PDPHYSICS_H

/**
 * MOD: ragdoll physics.
 *
 * Plain C interface to the Jolt-based physics module (physics/pdphysics.cpp).
 * The game code only ever talks to Jolt through this header, so no C++ or
 * Jolt types leak into the C codebase.
 *
 * Units: everything is in Perfect Dark world units (roughly centimetres),
 * Y up. Conversion to Jolt's metres happens inside the module.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define PDPHYS_LOG_NOTE    0
#define PDPHYS_LOG_WARNING 1
#define PDPHYS_LOG_ERROR   2

typedef void (*pdphysLogFn)(int level, const char *msg);

// Initialise Jolt and create the physics world. Returns 1 on success.
int pdphysInit(pdphysLogFn logfn);

// Destroy the physics world and shut Jolt down. Safe to call if not initialised.
void pdphysShutdown(void);

// 1 if pdphysInit succeeded and the world exists.
int pdphysIsReady(void);

// Human-readable Jolt version, e.g. "5.6.0".
const char *pdphysGetVersionString(void);

// Stage 1 smoke test: drops a 20-unit sphere from 200 units onto a static
// floor in a throwaway world and simulates up to 3 seconds at 60 Hz.
// Returns 1 if the sphere came to rest on the floor. Outputs the resting
// height of the sphere centre (expected ~20), steps simulated and wall time.
int pdphysSelfTest(float *outrestheight, int *outsteps, float *outms);

/*
 * Level collision (stage 2)
 *
 * The game hands over the static level geometry room by room as an indexed
 * triangle list (PD units). Each room becomes two static Jolt bodies: the
 * triangles as given and the same triangles flipped, because Jolt mesh
 * triangles are one-sided and PD's tile winding isn't consistent.
 */

typedef struct pdphysLevelStats {
	int rooms;        // rooms submitted
	int bodies;       // static bodies created
	int triangles;    // triangles submitted (one side)
	int failedrooms;  // rooms whose mesh could not be built
} pdphysLevelStats;

// Removes all level geometry from the world.
void pdphysLevelClear(void);

// Adds one room's static geometry. verts: numverts * 3 floats (x, y, z).
// indices: numtris * 3 vertex indices. Returns 1 on success.
int pdphysLevelAddRoom(int roomnum, const float *verts, int numverts, const int *indices, int numtris);

// Same, with a group tag (0-15; see PDPHYS_QUERY_GROUP_FIRST) so that part of a room can be replaced later
// with pdphysLevelRemoveRoomGroup. pdphysLevelAddRoom uses group 0.
int pdphysLevelAddRoomGroup(int roomnum, int group, const float *verts, int numverts, const int *indices, int numtris);

// Removes the room's level bodies that were added with that group.
void pdphysLevelRemoveRoomGroup(int roomnum, int group);

// MOD: clipping checks - level geometry that moves (lifts): one part per room,
// verts relative to pos, all parts sharing a key (>= 0) moved and removed together
int pdphysLevelAddMoverPart(int roomnum, int group, int key, const float *pos, const float *verts, int numverts, const int *indices, int numtris);
void pdphysLevelMoveMover(int key, const float *pos);
void pdphysLevelRemoveMover(int key);

// MOD: clipping checks - groups from this one up are query-only: they are
// found by pdphysRaycastLevelGroups but never collide with anything in the
// simulation (ragdolls, props) and are ignored by every other query.
#define PDPHYS_QUERY_GROUP_FIRST 8

// Call after the last room has been added. Fills stats if non-NULL.
void pdphysLevelFinish(pdphysLevelStats *stats);

// Casts a ray against the level geometry only (not ragdolls) from 'from' to 'to' (PD units).
// Returns 1 on hit, filling outhit (x, y, z), outnormal (unit vector) and
// outroom (room number of the body that was hit). Output pointers may be NULL.
int pdphysRaycast(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom);
// Same, but in any room (pdphysRaycast only sees the rooms ragdolls are in)
int pdphysRaycastAnyRoom(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom);
// Like pdphysRaycast, but only the level rooms that ragdoll collides with
int pdphysRagdollRaycast(int handle, const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom);

// Like pdphysRaycast, but against level geometry only, and only the groups
// in groupmask (bit n = group n of pdphysLevelAddRoomGroup)
int pdphysRaycastLevelGroups(const float from[3], const float to[3], unsigned int groupmask, float outhit[3], float outnormal[3]);

// Room filters: levels draw some rooms on top of each other in the same space
// (shown one at a time through portals), so like the game's own collision,
// level geometry only counts in the rooms an actor is in.
// - Level queries (pdphysRaycastLevelGroups, the capsule sweeps) only see
//   level bodies of these rooms until called again; num < 0: all rooms.
void pdphysSetQueryRooms(const int *rooms, int num);
// - A ragdoll only collides with level bodies of these rooms (num < 0: all,
//   the default for a new ragdoll).
void pdphysRagdollSetRooms(int handle, const int *rooms, int num);
// Which level groups ragdolls collide with (bit n = group n; default 0x7: the
// game's floor and wall tiles and the render walls). Query-only groups
// (PDPHYS_QUERY_GROUP_FIRST and up) can be included: they then collide with
// ragdolls (and still nothing else).
void pdphysSetRagdollLevelGroups(unsigned int groupmask);
// The room has query surfaces now. When ragdolls use query groups, the
// regular groups (the game's tiles) only count in rooms not marked: a body
// falling into a room never seen still lands on the game's collision.
void pdphysMarkRoomGenerated(int room);

// MOD: clipping checks - one contact of a swept shape
typedef struct pdphysContact {
	float fraction;   // of the move, where the shape first touches (0 = touching at the start)
	float point[3];   // on the surface (PD units)
	float normal[3];  // the surface's normal, facing the shape
} pdphysContact;

// Sweeps a vertical capsule (as below) and returns every contact, earliest
// first (at most maxcontacts). The caller decides which ones count.
int pdphysSweepCapsuleContacts(const float from[3], const float to[3], float radius, float halfheight,
		unsigned int groupmask, pdphysContact *out, int maxcontacts);

// MOD: clipping checks - sweeps a vertical capsule (centre at 'from', cylinder
// half height 'halfheight' (0: a sphere), radius) to 'to' against the level
// groups in groupmask. Contacts whose normal.y is >= maxnormaly or <=
// minnormaly, or that the move doesn't go into, are ignored, and so are
// contacts with geometry of the covermask groups within coverdist of them
// (along the contact normal, both sides; covermask 0: no such test). Returns
// 1 if something stops it: the fraction of the move (0..1) and the surface's
// horizontal normal.
int pdphysSweepCapsuleLevelGroups(const float from[3], const float to[3], float radius, float halfheight,
		unsigned int groupmask, float maxnormaly, float minnormaly, unsigned int covermask, float coverdist,
		float *outfraction, float outnormal[3]);

// Finds the ragdoll part along a ray (level geometry blocks it; kinematic
// props such as the player's own collision cylinder don't). With radius > 0,
// near misses within that many units also count. Returns 1 on hit, with the
// hit point (PD units), the ragdoll handle and the physics part index.
int pdphysRaycastRagdoll(const float from[3], const float to[3], float radius, float outhit[3], int *outhandle, int *outpart);

/*
 * Ragdolls (stage 3)
 *
 * A ragdoll is a set of parts, each a rigid body (capsule or sphere) joined
 * to its parent part by a constraint. All positions/axes are world space,
 * PD units. Matrices are 4x4 column-major (same memory layout as PD's Mtxf
 * and Jolt's Mat44): m[0..2] X axis, m[4..6] Y axis, m[8..10] Z axis,
 * m[12..14] translation.
 */

#define PDPHYS_MAX_RAGDOLL_PARTS 16

#define PDPHYS_SHAPE_CAPSULE 0
#define PDPHYS_SHAPE_SPHERE  1

#define PDPHYS_JOINT_NONE       0 // root part
#define PDPHYS_JOINT_SWINGTWIST 1
#define PDPHYS_JOINT_HINGE      2

typedef struct pdphysRagdollPart {
	int parent;          // index of the parent part in the array, -1 for the root
	float jointmtx[16];  // joint frame in world space (orthonormal rotation + translation)

	int shape;           // PDPHYS_SHAPE_*
	float a[3];          // capsule start / sphere centre
	float b[3];          // capsule end
	float radius;

	// Constraint to the parent, anchored at this part's joint position (jointmtx translation)
	int joint;           // PDPHYS_JOINT_*
	float axis[3];       // swing-twist: twist axis. hinge: hinge axis
	float axis2[3];      // swing-twist: plane axis. hinge: parent bone direction (zero-angle reference)
	float axis3[3];      // hinge: this part's bone direction
	float limits[4];     // swing-twist: normal cone, plane cone, twist min, twist max. hinge: min, max (radians)

	float linvel[3];     // initial linear velocity, PD units per second

	// A second capsule fixed to this part (capsules only), e.g. a foot on the
	// shin: from foota to footb, footradius thick. footradius 0 = none.
	float foota[3];
	float footb[3];
	float footradius;

	float mass;          // kg, 0 = from the shape's volume
} pdphysRagdollPart;

// Creates a ragdoll and adds it to the world. Returns a handle >= 0, or -1.
int pdphysRagdollCreate(const pdphysRagdollPart *parts, int numparts);

// Removes the ragdoll from the world and frees it.
void pdphysRagdollDestroy(int handle);

// Adds a velocity change (PD units per second) to one part, or to all parts if part < 0.
void pdphysRagdollAddVelocity(int handle, int part, const float vel[3]);
void pdphysRagdollClampSpeed(int handle, const float ref[3], float maxspeed);

// Writes each part's current joint frame (same convention as jointmtx). Returns part count.
int pdphysRagdollGetJoints(int handle, float (*outmtx)[16]);

// Stage 5 (powered ragdoll): joint motors drive each part towards the target
// joint frames (same convention as jointmtx, one per part; only rotations
// relative to the parent part are used). torques[i] = motor torque limit (N*m)
// of part i's joint, 0 = motor off. frequency = motor spring stiffness (Hz).
// Pass targetjointmtx = NULL to switch every motor off.
void pdphysRagdollDriveToPose(int handle, const float (*targetjointmtx)[16], const float *torques, float frequency);

// Animation blend: pulls each part's velocity towards following the target
// joint frames (world, PD units). weight 0..1, reach = seconds to close the
// remaining error. skip (may be NULL): non-zero = leave that part alone.
void pdphysRagdollTrackPose(int handle, const float (*targetjointmtx)[16], const int *skip, float weight, float dt, float reach);

// Dismemberment: cuts part (array index) from its parent.
void pdphysRagdollDetach(int handle, int part);

// For each part (PDPHYS_MAX_RAGDOLL_PARTS entries): fastest impact (PD units/s,
// along the contact normal) when it started touching the level, a prop or
// another body since the last call; 0 = none. Clears them. Used for thuds.
void pdphysRagdollTakeNewContacts(int handle, float *outspeeds);
// The same, and also each part's contact normal |y| for that impact (1 = a
// floor or ceiling, 0 = a wall). Either array may be NULL.
void pdphysRagdollTakeNewContactsEx(int handle, float *outspeeds, float *outny);
// Friction of every part of the ragdoll (the surface it's lying on)
void pdphysRagdollSetFriction(int handle, float friction);

// Ragdoll parts running into kinematic props (objects, doors): where, which
// way the prop was hit and how fast. Collected during pdphysStep.
typedef struct pdphysPropImpact {
	int prop;        // prop handle
	int ragdoll;     // ragdoll handle
	float speed;     // closing speed along dir (PD units/s)
	float dir[3];    // from the body into the prop (unit)
	float pos[3];    // contact point (PD units)
	float ownspeed;  // the body part's own speed along dir (PD units/s): it was moving, not the prop
} pdphysPropImpact;

// Copies out (up to max) and clears the impacts since the last call.
int pdphysTakePropImpacts(pdphysPropImpact *out, int max);
// For each part: 1 if it started touching something other than a floor (a
// wall, a ceiling, the side of a prop or character, another body) since the
// last call. Clears them. Used to end the animation blend.
void pdphysRagdollTakeNewBlocks(int handle, int *outflags);
// Keeps every part awake (they may not fall asleep) for this many seconds
void pdphysRagdollKeepAwake(int handle, float seconds);
// Water around the ragdoll: it floats on a surface at surfacey (PD units); inwater 0 = none
void pdphysRagdollSetWater(int handle, int inwater, float surfacey);
// The last non-floor contact: normal y and 0 level, 1 prop/character, 2 another body
void pdphysRagdollLastBlock(int handle, float *outny, int *outkind, int *outprop);
// Contacts with this prop don't end an animation blend (lifts)
void pdphysPropSetNoBlock(int handle, int noblock);

// 1 if every body of the ragdoll is asleep.
int pdphysRagdollIsAsleep(int handle);

// Pulls one part towards 'target' (PD units) with a critically damped spring
// (frequency in Hz) that also holds up the part's own weight. The force is
// capped at maxforce newtons, so heavy pulls lag and sag: the rest of the
// body hangs off the held part. Call every tick while holding; wakes the body.
void pdphysRagdollPull(int handle, int part, const float target[3], float maxforce, float frequency);

// Current position (PD units) of a part's centre of mass.
void pdphysRagdollGetPartPos(int handle, int part, float outpos[3]);

// Advances the simulation by dt seconds.
void pdphysStep(float dt);

/*
 * Props (stage 4): doors, lifts and furniture as kinematic bodies that only
 * ragdolls collide with. The game describes a prop as convex pieces in world
 * space; the body is built around 'origin' and can then be moved by moving
 * its origin (e.g. lifts and sliding doors).
 */

#define PDPHYS_MAX_PIECE_VERTS 128

typedef struct pdphysPiece {
	int numverts;
	const float *verts;   // numverts * 3 floats, world space
	float thickness;      // 0 = points already enclose a volume; > 0 = flat polygon, extruded by this much
} pdphysPiece;

// Returns a handle >= 0, or -1.
int pdphysPropCreate(const pdphysPiece *pieces, int numpieces, const float origin[3]);

// A prop made of triangles (an object's model): verts are world space,
// indices 3 per triangle. Both sides of each triangle are solid.
// Returns a handle >= 0, or -1.
int pdphysPropCreateMesh(const float *verts, int numverts, const int *indices, int numtris, const float origin[3]);

// Moves the prop so its origin is at 'origin'. dt > 0 moves it over that
// time (giving it velocity), dt <= 0 teleports it.
void pdphysPropMove(int handle, const float origin[3], float dt);
// Puts it in place with only velscale of the speed it moved at (a nudge)
void pdphysPropMoveScaled(int handle, const float origin[3], float dt, float velscale);
// Moves and turns it: 'origin' is where the body's origin goes and rot (x, y,
// z, w) its rotation from how it was built. dt > 0 over that time, else teleports.
void pdphysPropMoveRot(int handle, const float origin[3], const float rot[4], float dt);

void pdphysPropDestroy(int handle);

// Marks the prop as a living character's body (the player, a guard)
void pdphysPropSetLiving(int handle);
// passthrough 1: characters stepping on bodies don't touch them - a body part
// drifting into a character passes through. Only a part flying at one (a body
// thrown or falling onto someone, >= 220 PD units/s) still hits it.
// passthrough 0: characters are solid to bodies.
void pdphysSetLivingPassThrough(int passthrough);
// Friction of body parts on sloped level surfaces (6 to 60 degrees); 0 leaves
// the normal friction (Ragdoll.RagdollSlopeGrip)
void pdphysSetSlopeGrip(float grip);

#ifdef __cplusplus
}
#endif

#endif
