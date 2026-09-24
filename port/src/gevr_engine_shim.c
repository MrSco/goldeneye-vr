/**
 * Port-side stand-ins for the engine services that lived in N64 hardware drivers.
 *
 * src/sched.c, src/motor.c and src/tlb_manage.c are excluded from this build
 * (see CMakeLists.txt): they drive the RSP, the VI and the MIPS TLB through
 * libultra, none of which exist on a headset. Excluding them removes those
 * dependencies but leaves the game code calling their front doors, so those
 * doors are defined here.
 *
 * Audio (music.c / snd.c / audi.c) is linked for real; the frame pump drives
 * gevrAudioFrame each retrace because OS threads are stubs.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/ultratypes.h>
#include <PR/libaudio.h>
#include <bondtypes.h>
#include <sched.h>
#include <tlb_manage.h>
#include <stdarg.h>
#include <stdio.h>
#include "system.h" /* sysGetMicroseconds */
#include <PR/sptask.h>
#include "input.h"
#include "video.h"
#include "gevr_sched.h"
#include "gevr_rom_segments.h"
#include <music.h> /* musicFadeTick: the retrace-driven music cross-fade */

/* ---------------------------------------------------------------- video */

/*
 * The VI mode tables. fr.c copies entries out of osViModeTable into g_ViModes
 * and then edits width and scale; on this port nothing reads the result,
 * because frame timing and scanout belong to OpenXR and the swapchain. They are
 * defined so that copying stays harmless.
 */
/* Indices run 0..41 (see the OS_VI_* defines in PR/os.h). */
#define GEVR_VI_MODE_COUNT 42
OSViMode osViModeTable[GEVR_VI_MODE_COUNT];
OSViMode g_ViModes[2];

/* ------------------------------------------------------------ scheduler */

/*
 * The N64 scheduler handed RSP and RDP tasks to the hardware and paced on VI
 * retrace. The scheduler thread, and the boot code in init.c that created it
 * and registered the game as a client, are not in this build. What the game
 * still does is talk to it through two message queues, and the frame pump
 * below answers on the scheduler's behalf - see gevr_sched.h for the protocol.
 */
void osCreateScheduler(OSSched *s, void *stack, u8 mode, u32 numFields)
{
	(void)stack;
	(void)mode;
	(void)numFields;

	if (s) {
		memset(s, 0, sizeof(*s));
		s->retraceMsg.type = OS_SC_RETRACE_MSG;
		s->prenmiMsg.type = OS_SC_PRE_NMI_MSG;
		osCreateMesgQueue(&s->interruptQ, s->intBuf, OS_SC_MAX_MESGS);
		osCreateMesgQueue(&s->cmdQ, s->cmdMsgBuf, OS_SC_MAX_MESGS);
	}
}

/* ------------------------------------------------------- video, remaining */

s32 g_ViChangeVideoModes[2];
OSViMode *g_ViModePtrs[2];
f32 g_ViXScales[2] = { 1.0f, 1.0f };
f32 g_ViYScales[2] = { 1.0f, 1.0f };

/* --------------------------------------------------- scheduler, remaining */

extern OSMesgQueue g_ContInputMessageQueue; /* joy.c */
void joyPoll(void);

OSSched os_scheduler;
OSScClient gfxClient[3];
OSMesgQueue gfxFrameMsgQ;
OSMesgQueue *sched_cmdQ = &os_scheduler.cmdQ; /* what rspGfxTaskStart() sends tasks to */

static OSMesg gevrFrameMsgBuf[OS_SC_MAX_MESGS];
static s32 gevrSchedInitDone;
static s32 gevrFrameOpen;        /* videoStartFrame() has run and videoEndFrame() has not */
static u32 gevrTasksThisFrame;   /* display lists drawn since the last retrace */
static u32 gevrFramesLogged;


/* ------------------------------------------------------------- watchdog */

/*
 * A silent spin on the game thread shows as a frozen last frame and 100% CPU
 * with nothing in the log. This thread watches the retrace count and, when it
 * has not moved for five seconds, delivers SIGSEGV to the game thread so the
 * tombstone carries that thread's stack. Diagnostic; costs one wake-up a second.
 */
#include <pthread.h>
static u64 gevrPerfNs(void);
void gevrPerfAdd(int section, u64 ns);
static void gevrPerfFrameDone(void);
#include <unistd.h>
#include <signal.h>
static s32 gevrVrFrameBegun; /* tentative; defined with the VR lifecycle below */
static pthread_t gevrGameThread;
static volatile u32 gevrPumpStage, gevrPumpEntries, gevrPumpLoops; /* PORT probe: where the pump is when it stalls */
void gevrSchedTraceMenu(s32 menu, s32 afterTick)
{
	static s32 lastMenu[2] = { -999, -999 };
	static u64 lastUs[2];
	u64 now = sysGetMicroseconds();
	s32 phase = afterTick != 0;
	/* Menu 5 is file select. Limit steady-state logging to one pair/sec. */
	if (menu != lastMenu[phase] || (menu == 5 && now - lastUs[phase] >= 1000000)) {
		sysLogPrintf(LOG_NOTE, "menu-pump: %s menu=%d sched=%p frame=%u pump=%u/%u/%u queue=%d/%d first=%d buf=%p",
			phase ? "after" : "before", menu, (void *)&os_scheduler, os_scheduler.frameCount,
			gevrPumpStage, gevrPumpEntries, gevrPumpLoops,
			gfxFrameMsgQ.validCount, gfxFrameMsgQ.msgCount, gfxFrameMsgQ.first, (void *)gfxFrameMsgQ.msg);
		lastMenu[phase] = menu;
		lastUs[phase] = now;
	}
}

static void *gevrWatchdog(void *arg)
{
	u32 last = 0, stalled = 0, reported = 0;
	(void)arg;
	for (;;) {
		sysSleep(10000000); /* 1 s in 100 ns units */
		/*
		 * Arm only once the game is well into its frame loop: the first frames
		 * can legitimately wait several seconds for the runtime (the loading
		 * transition when launched from the library), which is where an
		 * earlier version of this killed the app on the user's own launch.
		 */
		if (last == 0) {
			if (os_scheduler.frameCount >= 30) {
				last = os_scheduler.frameCount;
			}
			continue;
		}
		if (os_scheduler.frameCount == last) {
			stalled++;
			if (stalled >= 5 && !reported) {
				reported = 1;
				sysLogPrintf(LOG_ERROR, "watchdog: no retrace for %u s (pump stage %u, entries %u, xr loops %u, frame open %d, xr begun %d)",
						stalled, gevrPumpStage, gevrPumpEntries, gevrPumpLoops, gevrFrameOpen, gevrVrFrameBegun);
			}
			/* A stack costs the process; only take one when asked for by a marker file. */
			if (stalled >= 5 && access("/sdcard/Android/data/com.gevr.port/files/gevr_watchdog_kill.txt", F_OK) == 0) {
				sysLogPrintf(LOG_ERROR, "watchdog: marker present, signalling the game thread for a stack");
				pthread_kill(gevrGameThread, SIGSEGV);
				return NULL;
			}
		} else {
			stalled = 0;
			reported = 0;
			last = os_scheduler.frameCount;
		}
	}
}

static void gevrSchedEnsureInit(void)
{
	if (gevrSchedInitDone) {
		return;
	}

	gevrSchedInitDone = 1;
	osCreateScheduler(&os_scheduler, NULL, 0, 1);
	{
		pthread_t t;
		gevrGameThread = pthread_self();
		if (pthread_create(&t, NULL, gevrWatchdog, NULL) == 0) {
			pthread_detach(t);
		}
	}
	osCreateMesgQueue(&gfxFrameMsgQ, gevrFrameMsgBuf, OS_SC_MAX_MESGS);
	osScAddClient(&os_scheduler, &gfxClient[0], &gfxFrameMsgQ, NULL);
}

void osScAddClient(OSSched *s, OSScClient *c, OSMesgQueue *msgQ, OSScClient *next)
{
	(void)next;
	c->msgQ = msgQ;
	c->next = s->clientList;
	s->clientList = c;
}

OSMesgQueue *osScGetCmdQ(OSSched *s)
{
	gevrSchedEnsureInit();
	return &s->cmdQ;
}

/*
 * A task arriving on the command queue. Graphics tasks are drawn now, inside
 * the current frame, and the task's reply is queued for the game as the
 * scheduler would have done when the RCP finished. Audio tasks have no
 * RSP work left: the classic mixer macros execute during alAudioFrame.
 */
s32 gevrSchedSend(OSMesgQueue *mq, OSMesg msg)
{
	OSScTask *t = (OSScTask *)msg;

	if (mq != &os_scheduler.cmdQ || !t) {
		return 0;
	}

	gevrSchedEnsureInit();

	if (t->list.t.type == M_GFXTASK && t->list.t.data_ptr) {
		if (!gevrFrameOpen) {
			videoStartFrame();
			gevrFrameOpen = 1;
		}

		{
			const u64 t0 = gevrPerfNs();
			videoSubmitCommands((Gfx *)t->list.t.data_ptr);
			gevrPerfAdd(1, gevrPerfNs() - t0);
		}
		gevrTasksThisFrame++;
	}

	if (t->msgQ) {
		osSendMesg(t->msgQ, t->msg, OS_MESG_NOBLOCK);
	}

	return 1;
}


/* ------------------------------------------------------------- headset */

/*
 * The OpenXR session lives in port/vr/vr_openxr.cpp and was driven by Perfect
 * Dark's main loop (port/src/pdmain.c), which this build does not use. Until
 * these calls were made from here, no session was ever begun and nothing the
 * game drew reached the headset: the runtime kept showing its loading
 * environment while the game ran on. The order is the one pdmain.c used:
 * initialise once the GL context exists, then per game frame poll events,
 * begin the XR frame (xrWaitFrame paces us to the display), let the game
 * draw, and submit at the next retrace.
 */
#ifdef ANDROID
#include <EGL/egl.h>
#endif
extern void vr_initialize(void);
extern bool vr_is_initialized(void);
extern void vr_poll_events(void);
extern bool vr_begin_frame_and_update_poses(void);
extern bool vr_end_frame_and_submit(void);
extern bool vr_apply_pending_scale(void);
extern void vrSettingsLoad(void);
extern bool get_button_state(int hand_index, const char *button_name); /* vr_input.cpp */
extern void vr_screen_recenter(void);                                    /* vr_openxr.cpp */

static s32 gevrVrInitDone;
static s32 gevrVrFrameBegun;
static u32 gevrXrFramesBegun; /* PORT probe */

static void gevrVrFrameEnd(void)
{
	if (gevrVrFrameBegun) {
		gevrVrFrameBegun = 0;
		vr_end_frame_and_submit();
		vr_apply_pending_scale();
	}
}

static void gevrVrFrameBegin(void);

/*
 * The in-VR launcher (port/vr/vr_launcher.cpp) drives XR frames itself before
 * the game boots: the same begin (which also brings the session up and loads
 * goldeneye-vr.ini) and end as the frame pump. Begin returns whether a frame
 * is open.
 */
int gevrVrPumpBegin(void)
{
	gevrVrFrameBegin();
	return gevrVrFrameBegun;
}

void gevrVrPumpEnd(void)
{
	gevrVrFrameEnd();
}

/*
 * Performance readout (Show stats): where a frame's CPU time goes, averaged
 * each second - WAIT for the headset (xrWaitFrame), DRAW (fast3d running the
 * display lists and issuing GL: videoSubmitCommands), END (finishing and
 * submitting the frame), GAME (the rest: game logic and building the display
 * lists) - and the draw calls and triangles per frame.
 */
#include <time.h>
extern uint32_t gevr_perf_draws, gevr_perf_tris;   /* fast3d gfx_pc.cpp */
static u64 gevrPerfNs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
static u64 gevrPerfAcc[3], gevrPerfFrameStart, gevrPerfSecStart, gevrPerfWorstGame;
static u64 gevrPerfFrameAcc[3];
static u32 gevrPerfFrames;
static char gevrPerfBuf[160];
const char *gevrPerfText(void) { return gevrPerfBuf; }
void gevrPerfAdd(int section, u64 ns)
{
	gevrPerfFrameAcc[section] += ns;
}
u64 gevrPerfNow(void) { return gevrPerfNs(); }
/* at the end of each presented frame */
static void gevrPerfFrameDone(void)
{
	const u64 now = gevrPerfNs();
	int i;
	if (gevrPerfFrameStart) {
		const u64 total = now - gevrPerfFrameStart;
		const u64 known = gevrPerfFrameAcc[0] + gevrPerfFrameAcc[1] + gevrPerfFrameAcc[2];
		const u64 game = total > known ? total - known : 0;
		if (game > gevrPerfWorstGame) gevrPerfWorstGame = game;
		for (i = 0; i < 3; i++) gevrPerfAcc[i] += gevrPerfFrameAcc[i];
		gevrPerfAcc[0] += 0;
		gevrPerfFrames++;
		if (!gevrPerfSecStart) gevrPerfSecStart = now;
		{
			static u64 gameAcc;
			gameAcc += game;
			if (now - gevrPerfSecStart >= 1000000000ull && gevrPerfFrames) {
				const double n = (double)gevrPerfFrames;
				snprintf(gevrPerfBuf, sizeof(gevrPerfBuf),
					"GAME %.1f MAX %.1f  DRAW %.1f  END %.1f  WAIT %.1f\nDRAWS %u  TRIS %uK",
					gameAcc / n / 1e6, gevrPerfWorstGame / 1e6, gevrPerfAcc[1] / n / 1e6,
					gevrPerfAcc[2] / n / 1e6, gevrPerfAcc[0] / n / 1e6,
					(unsigned)(gevr_perf_draws / gevrPerfFrames), (unsigned)(gevr_perf_tris / gevrPerfFrames / 1000));
				sysLogPrintf(LOG_NOTE, "perf: %s", gevrPerfBuf);
				gameAcc = 0;
				gevrPerfWorstGame = 0;
				gevrPerfFrames = 0;
				gevr_perf_draws = gevr_perf_tris = 0;
				for (i = 0; i < 3; i++) gevrPerfAcc[i] = 0;
				gevrPerfSecStart = now;
			}
		}
	}
	for (i = 0; i < 3; i++) gevrPerfFrameAcc[i] = 0;
	gevrPerfFrameStart = now;
}

static void gevrVrFrameBegin(void)
{
	if (!gevrVrInitDone) {
#ifdef ANDROID
		if (eglGetCurrentDisplay() == EGL_NO_DISPLAY || eglGetCurrentContext() == EGL_NO_CONTEXT) {
			return;
		}
#endif
		vr_initialize();
		if (!vr_is_initialized()) {
			return;
		}
		vr_poll_events();
		vrSettingsLoad();
		gevrVrInitDone = 1;
		sysLogPrintf(LOG_NOTE, "vr: session driven from the frame pump");
	}

	vr_poll_events();
	{
		const u64 t0 = gevrPerfNs();
		gevrVrFrameBegun = vr_begin_frame_and_update_poses() ? 1 : 0;
		gevrPerfAdd(0, gevrPerfNs() - t0);
	}
	gevrXrFramesBegun++; /* false: no session yet, or an empty frame already closed */

	/* Hold the left stick click for about a second to bring the virtual screen back in front of you. */
	if (gevrVrFrameBegun) {
		static u32 held;
		if (get_button_state(0, "thumbstick_click")) {
			if (++held == 72) {
				vr_screen_recenter();
			}
		} else {
			held = 0;
		}
	}
}

/*
 * The game blocking on an empty frame queue: it has drawn what it is going to
 * draw this frame and is waiting for the next vsync. Present, poll input, and
 * hand it the retrace. If it drew nothing since the last retrace there is
 * nothing to present; yield briefly so a loop that is only waiting on the
 * clock does not spin flat out.
 */
s32 gevrSchedBlockedRecv(OSMesgQueue *mq, OSMesg *msg)
{
	if (mq != &gfxFrameMsgQ) {
		return 0;
	}

	gevrSchedEnsureInit();
	gevrPumpStage = 1; gevrPumpEntries++;

	if (gevrFrameOpen) {
		{
			const u64 t0 = gevrPerfNs();
			videoEndFrame();
			gevrPerfAdd(2, gevrPerfNs() - t0);
		}
		gevrFrameOpen = 0;
		gevrPerfFrameDone();

		if (gevrFramesLogged < 3) {
			gevrFramesLogged++;
			sysLogPrintf(LOG_NOTE, "frame %u presented (%u display lists)", os_scheduler.frameCount, gevrTasksThisFrame);
		}
	} else {
		sysSleep(10000); /* 1 ms, in 100 ns units */
	}

	gevrTasksThisFrame = 0;
	gevrPumpStage = 2;

	gevrVrFrameEnd();
	gevrPumpStage = 3;


	inputUpdate();

	/*
	 * The controller poll ran from the scheduler's retrace handler on the
	 * N64. joyPoll() consumes the sample the port's osContGetReadData()
	 * produces and starts the next read; if no read is outstanding, start
	 * one so the next retrace has something to consume.
	 */
	joyPoll();
	if (g_ContInputMessageQueue.validCount == 0) {
		osContStartReadData(&g_ContInputMessageQueue);
	}

	/*
	 * __scHandleRetrace() called this right after joyPoll(), and nothing in
	 * this port did. musicTrack*FadeOut/FadeIn only record a fade; the volume
	 * ramp and the alCSPStop that ends a fade-out live here, so every fade in
	 * the game was set and never run -- opening the watch started the pause
	 * track with the level track still at full volume under it, and closing
	 * it left the pause track playing. One tick per retrace, which is what
	 * FADE_FRAMERATE counts.
	 */
	musicFadeTick();


	/*
	 * The N64 delivered a retrace every 1/60 s and the game ticks once per
	 * retrace (bossMainloop only checks that at least half a frame passed).
	 * xrWaitFrame paces us at the display's rate, 72 Hz on this headset, so
	 * handing the game one retrace per XR frame ran it a fifth too fast.
	 * Frames that come before the next retrace is due re-present the last
	 * image instead.
	 */
	{
		static u64 nextRetraceUs;
		u64 now;

		gevrPumpStage = 4; gevrPumpLoops = 0;
		for (;;) {
			gevrPumpLoops++;
			gevrVrFrameBegin();
			gevrPumpStage = 5;
			now = sysGetMicroseconds();
			if (!gevrVrInitDone) {
				nextRetraceUs = now;
				break;
			}
			if (nextRetraceUs == 0 || now + 2000 >= nextRetraceUs) {
				break;
			}
			gevrVrFrameEnd();
		}
		if (nextRetraceUs == 0 || now > nextRetraceUs + 100000) {
			nextRetraceUs = now;
		}
		nextRetraceUs += 16667;

		{
			extern u32 gevrRetraceClockFrames;
			extern u64 gevrRetraceClockUs;
			gevrRetraceClockFrames++;
			gevrRetraceClockUs = now;
		}

		{
			/* PORT probe: measured pacing */
			static u64 sStatUs; static u32 sRetraces;
			sRetraces++;
			if (sStatUs == 0) sStatUs = now;
			if (now - sStatUs >= 1000000) {
				sysLogPrintf(LOG_NOTE, "pump: %u retraces, %u xr frames in %llu ms", sRetraces, gevrXrFramesBegun, (unsigned long long)((now - sStatUs) / 1000));
				sStatUs = now; sRetraces = 0; gevrXrFramesBegun = 0;
			}
		}
	}

	gevrPumpStage = 6;
	os_scheduler.frameCount++;

#ifdef GEVR
	{
		extern void gevrAudioFrame(void);
		gevrAudioFrame();
		gevrPumpStage = 7;
	}
#endif

	if (msg) {
		*msg = (OSMesg)&os_scheduler.retraceMsg;
	}

	return 1;
}

/*
 * sched.c (excluded) owned the RDP performance counters the LEFTOVERDEBUG speed
 * graph reads. There is no RDP here: clock 1, everything else 0, so the
 * graph's percentages divide safely.
 */
u32 *get_counters(void)
{
	static u32 counters[4] = { 1, 0, 0, 0 }; /* clock, cmd, pipe, tmem */
	return counters;
}

/* ------------------------------------------------------------ rumble pak */

/*
 * Perfect Dark's port routes the motor through libultra.c: osMotorProbe for
 * init, __osMotorAccess for start/stop, which drives inputRumble (and so the
 * Quest controller haptics). src/motor.c talks to the SI bus and is excluded.
 */
extern s32 osMotorProbe(OSMesgQueue *ctrlrqueue, OSPfs *pfs, s32 channel);
extern s32 __osMotorAccess(OSPfs *pfs, s32 cmd);

s32 osMotorInit(OSMesgQueue *q, OSPfs *pfs, int channel)
{
	return osMotorProbe(q, pfs, channel);
}

s32 osMotorStart(OSPfs *pfs) { return __osMotorAccess(pfs, MOTOR_START); }
s32 osMotorStop(OSPfs *pfs)  { return __osMotorAccess(pfs, MOTOR_STOP); }

/* ------------------------------------------------------------------- TLB */

/*
 * GoldenEye used the MIPS TLB to map a scratch block in and out. Here the
 * address space is flat, so there is nothing to map - but callers do use the
 * block as memory, so it is allocated once and handed back rather than
 * returning NULL and letting them write through it.
 */
void tlbmanageEstablishManagementTable(void) { }
void tlbmanageResetCurrentEntriesCount(void) { }
void initTLBPrepareContext(void) { }
void resolve_TLBaddress_for_InvalidHit(void) { }

u8 (*tlbmanageGetTlbAllocatedBlock(void))[TLB_BLOCK_SIZE]
{
	/* The scratch block sits at the top of the region the ROM loader allocates,
	   so that boss.c's (tlbBlock - bssEnd) still measures the heap. */
	return (u8 (*)[TLB_BLOCK_SIZE])gevrHeapTlbBlock();
}

/* --------------------------------------------------------- demo replays */

/*
 * The attract-mode demos are recorded controller streams stored in the
 * cartridge as their own segments (assets/ramrom). They are not in the file
 * table, so the manifest does not reach them; until a loader does, the table
 * points at nothing and the engine simply has no demo to play.
 */
u32 *ramrom_Dam_1, *ramrom_Dam_2;
u32 *ramrom_Facility_1, *ramrom_Facility_2, *ramrom_Facility_3;
u32 *ramrom_Runway_1, *ramrom_Runway_2;
u32 *ramrom_Silo_1, *ramrom_Silo_2;
u32 *ramrom_Frigate_1, *ramrom_Frigate_2;
u32 *ramrom_BunkerI_1, *ramrom_BunkerI_2;
u32 *ramrom_Train;

/* ---------------------------------------------------------- RSP microcode */

/*
 * The RSP microcode blobs (src/rspboot.s, src/gspboot.s). Fast3D interprets
 * display lists on the CPU, so no microcode is ever uploaded or run - these
 * exist only so the code that would have pointed the RSP at them still links.
 */
long long int rspbootTextStart[1];
long long int rspbootTextEnd[1];
long long int gsp3DTextStart[1];
long long int gsp3DDataStart[1];
long long int aspMainTextStart[1];
long long int aspMainDataStart[1];

/* Lived in init.c; only used to point OSThread stacks at the high end. */
void *setSPToEnd(u8 *stack, u32 size)
{
	return stack + size;
}

/* ---------------------------------------------------------------- misc */

/* Quiet NaN, declared by src/libultra/gu/guint.h for the trig helpers. */
float __libm_qnan_f = (float)(0.0 / 0.0);

/* Lived in the scheduler; gated debug output to stderr. */
void permit_stderr(u32 flag)
{
	(void)flag;
}

/* ------------------------------------------------------- debug output */

/*
 * The engine's debug print. On hardware it went down the SGI Indy dev link via
 * src/rmon.c, which is excluded here. Routing it to the port's log means the
 * game's own diagnostics show up in logcat next to everything else - which is
 * how anyone is going to find out what happens on the first boot.
 */
void osSyncPrintf(const char *fmt, ...)
{
	char line[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	sysLogPrintf(LOG_NOTE, "%s", line);
}

/* The dev-link transport itself. There is no host on the other end. */
void osReadHost(void *dramAddr, u32 nbytes)  { (void)dramAddr; (void)nbytes; }
void osWriteHost(void *dramAddr, u32 nbytes) { (void)dramAddr; (void)nbytes; }

s32 rmonGetToken(void) { return 0; }
s32 rmonStatus(void)   { return 0; }

/* --------------------------------------------------------------- printf */

/*
 * The formatter behind sprintf(). Upstream it lives in
 * src/libultrare/libc/xprintf.c, a hand-written IRIX-era implementation that is
 * not part of this build; the host C library does the same job correctly and
 * with fewer surprises around 64-bit types.
 *
 * The contract is libultra's: format, then hand the result to prout(), which
 * appends it wherever the caller wanted and returns the new write position.
 */
typedef u8 *(*gevrOutFun)(u8 *dst, const u8 *src, size_t count);

int _Printf(gevrOutFun prout, char *arg, const char *fmt, va_list args)
{
	char buf[1024];
	int n;

	n = vsnprintf(buf, sizeof(buf), fmt, args);
	if (n < 0) {
		return -1;
	}

	if (n > (int)sizeof(buf) - 1) {
		n = (int)sizeof(buf) - 1;   /* truncated, but report what was emitted */
	}

	if (prout) {
		prout((u8 *)arg, (const u8 *)buf, (size_t)n);
	}

	return n;
}
