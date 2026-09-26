#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <PR/os_internal.h>
#include <PR/rcp.h>
#include "platform.h"
#include "gevr_sched.h"
#include "system.h"
#include "input.h"
#include "video.h"
#include "audio.h"
#include "fs.h"

#define EEPROM_SIZE (EEP16K_MAXBLOCKS * 8)
#define EEPROM_FNAME "eeprom.bin"
#define EEPROM_PATH "$S/" EEPROM_FNAME

#define OS_COUNTER_RATE 46875000ULL
#define OS_COUNTER_NUM (OS_COUNTER_RATE / 1000ULL)
#define OS_COUNTER_DEN (1000000ULL / 1000ULL)

u64 osClockRate = OS_CLOCK_RATE;
u32 osMemSize = 16 * 1024 * 1024; /* expansion pak installed plus some extra */
u32 osTvType = OS_TV_NTSC;        /* 0 = PAL, 1 = NTSC, 2 = MPAL */
u32 osResetType = 0;              /* 0 = cold reset */
s32 osViClock = VI_NTSC_CLOCK;

static u8 eeprom[EEPROM_SIZE];
static char eepromPath[FS_MAXPATH + 1];
static s32 eepromLoaded = 0;

/* Time */

OSTime osGetTime(void)
{
	// u64 should be enough to last a while
	return (sysGetMicroseconds() * OS_COUNTER_NUM) / OS_COUNTER_DEN;
}

/*
 * The game measures a tick's length with osGetCount() and turns it into a
 * whole number of 1/60 s frames (frametiming.c, boss.c). The N64 counter
 * advanced by exactly one frame's worth between retraces; the headset's
 * retraces land on 72 Hz frame boundaries, so real time between them
 * alternates 13.9 and 27.8 ms and the rounding ran the game fast. The count
 * therefore advances one frame per delivered retrace, plus real time inside
 * the current frame, capped so a tick never sees more than one frame.
 */
u32 gevrRetraceClockFrames;   /* set by the frame pump */
u64 gevrRetraceClockUs;
#define GEVR_COUNTS_PER_FRAME (OS_COUNTER_RATE / 60ULL)

u32 osGetCount(void)
{
	u64 sub = sysGetMicroseconds() - gevrRetraceClockUs;
	if (gevrRetraceClockUs == 0) {
		return (u32)osGetTime();
	}
	if (sub > 16000) {
		sub = 16000;
	}
	return (u32)((u64)gevrRetraceClockFrames * GEVR_COUNTS_PER_FRAME + sub * OS_COUNTER_RATE / 1000000ULL);
}

/* Thread */

void osCreateThread(OSThread *thrd, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri p)
{
	thrd->id = id;
	thrd->state = OS_STATE_STOPPED;
}

void osDestroyThread(OSThread *thrd)
{
	thrd->id = 0;
	thrd->state = 0;
}

void osYieldThread(void)
{

}

void osStartThread(OSThread *thrd)
{
	thrd->state = OS_STATE_RUNNING;
}

void osStopThread(OSThread *thrd)
{
	thrd->state = OS_STATE_STOPPED;
}

OSPri osGetThreadPri(OSThread *thrd)
{
	return 0;
}

void osSetThreadPri(OSThread *thrd, OSPri pri)
{

}

/* Mesg */

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msgBuf, s32 count)
{
	mq->validCount = 0;
	mq->first = 0;
	mq->msgCount = count;
	mq->msg = msgBuf;
}

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg)
{

}

/*
 * Real queues, single-threaded. Nothing here can wait for another thread, so
 * a blocking call on a full or empty queue cannot block: sends to a full queue
 * fail, and a receive from an empty queue is answered by the frame pump when
 * it is the frame queue (see gevr_sched.h) and fails otherwise. The callers
 * left in the build that block on other queues - the controller handshakes,
 * romReceiveMesg, the boot timer - ignore the result, which is what the
 * earlier stubs gave them too.
 *
 * A queue that was never created (msg == NULL) is treated as empty and
 * refuses sends; the scheduler's queues were created by the excluded N64
 * boot code and are now created by the pump on first use.
 */
s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
	(void)flag;

	if (gevrSchedSend(mq, msg)) {
		return 0;
	}

	if (!mq || !mq->msg || mq->validCount >= mq->msgCount) {
		return -1;
	}

	mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
	mq->validCount++;
	return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
	(void)flag;

	if (!mq || !mq->msg || mq->validCount >= mq->msgCount) {
		return -1;
	}

	mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
	mq->msg[mq->first] = msg;
	mq->validCount++;
	return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
	if (mq && mq->msg && mq->validCount > 0) {
		if (msg) {
			*msg = mq->msg[mq->first];
		}
		mq->first = (mq->first + 1) % mq->msgCount;
		mq->validCount--;
		return 0;
	}

	if (flag == OS_MESG_BLOCK && gevrSchedBlockedRecv(mq, msg)) {
		return 0;
	}

	return -1;
}

/* Vi */

void osCreateViManager(OSPri pri)
{

}

void osViSetMode(OSViMode *mode)
{

}

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount)
{

}

void osViBlack(u8 active)
{
	/*
	 * PORT: the cartridge blanked the VI output around a video-mode change
	 * (fr.c, MD_BLACK). The Perfect Dark version cleared the screen with a
	 * start/end-frame pair issued from the game thread, which the frame
	 * pump in gevr_engine_shim.c knows nothing about; on the title reload it
	 * left the folder screen black. Blanking is cosmetic here, so it is a
	 * no-op.
	 */
	(void)active;
}

void osViSetSpecialFeatures(u32 func)
{

}

void osViSwapBuffer(void *vaddr)
{

}

void osViSetXScale(f32 value)
{

}

void osViSetYScale(f32 value)
{

}

/* Ai */

s32 osAiSetFrequency(u32 frequency)
{
	// we can allow for any freq
	return (s32)frequency;
}

u32 osAiGetLength(void)
{
	return audioGetBytesBuffered();
}

s32 osAiSetNextBuffer(void *bufPtr, u32 size)
{
	audioSetNextBuffer(bufPtr, size);
	return 0;
}

/* Cont */

s32 osContInit(OSMesgQueue *mesgq, u8 *bitpattern, OSContStatus *data)
{
	if (bitpattern) {
		*bitpattern = inputControllerMask();
	}
	if (data) {
		osContGetQuery(data);
	}
	return 0;
}

s32 osContStartReadData(OSMesgQueue *mesgq)
{
	osSendMesg(mesgq, (OSMesg)0, OS_MESG_NOBLOCK);
	return 0;
}

/*
 * PORT test hook: a file named gevr_input.txt in the app's external files
 * directory holds a hex button mask (N64 layout: 1000 = START, 8000 = A,
 * 4000 = B) and an optional stick pair, e.g. "1000" or "0000 0 -80". It is
 * applied to pad 0 for a few frames and the file is deleted, so menus can be
 * driven from the PC: adb shell "echo 1000 > /sdcard/Android/data/com.gevr.port/files/gevr_input.txt".
 */
#include <stdio.h>
#include <unistd.h>
static u16 gevrInjectButtons; static s8 gevrInjectX, gevrInjectY; static s32 gevrInjectFrames;
static void gevrPollInjectedInput(void)
{
	static u32 sTick;
	const char *path = "/sdcard/Android/data/com.gevr.port/files/gevr_input.txt";
	if (gevrInjectFrames > 0) {
		gevrInjectFrames--;
		return;
	}
	if ((++sTick % 15) != 0) {
		return;
	}
	{
		FILE *f = fopen(path, "r");
		unsigned mask = 0; int x = 0, y = 0, frames = 4;
		if (!f) {
			return;
		}
		/* optional 4th field: how many frames to hold, e.g. "0010 0 0 120" to aim */
		if (fscanf(f, "%x %d %d %d", &mask, &x, &y, &frames) >= 1) {
			if (frames < 1) frames = 1;
			if (frames > 600) frames = 600;
			gevrInjectButtons = (u16)mask; gevrInjectX = (s8)x; gevrInjectY = (s8)y; gevrInjectFrames = frames;
			sysLogPrintf(LOG_NOTE, "input: injecting buttons %04x stick %d,%d for %d frames", mask, x, y, frames);
		}
		fclose(f);
		unlink(path);
	}
}

void osContGetReadData(OSContPad *pad)
{
	gevrPollInjectedInput();
	// game always passes in an array of 4 OSContPads
	for (s32 i = 0; i < MAXCONTROLLERS; ++i, ++pad) {
		pad->button = 0;
		pad->stick_x = 0;
		pad->stick_y = 0;
		pad->rstick_x = 0;
		pad->rstick_y = 0;
		if (inputReadController(i, pad) < 0) {
			pad->errnum = CONT_NO_RESPONSE_ERROR;
		} else {
			pad->errnum = 0;
		}
		if (i == 0 && gevrInjectFrames > 0) {
			pad->button |= gevrInjectButtons;
			pad->stick_x = gevrInjectX;
			pad->stick_y = gevrInjectY;
		}
		if (i == 0 && (pad->button != 0 || pad->stick_x != 0 || pad->stick_y != 0)) {
			/* PORT probe: proof that controller input reaches the game */
			static u64 sLastLogUs;
			u64 now = sysGetMicroseconds();
			if (now - sLastLogUs > 1000000) {
				sLastLogUs = now;
				sysLogPrintf(LOG_NOTE, "input: pad0 buttons %04x stick %d,%d", pad->button, pad->stick_x, pad->stick_y);
			}
		}
	}
}

s32 osContStartQuery(OSMesgQueue *mq)
{
	return 0;
}

void osContGetQuery(OSContStatus *status)
{
	// also always 4 status structs here
	for (s32 i = 0; i < MAXCONTROLLERS; ++i, ++status) {
		if (inputControllerConnected(i)) {
			status->errnum = 0;
			/* An N64 pad reports CONT_JOYPORT too; joyRumblePakInit tests it. */
			status->type = CONT_ABSOLUTE | CONT_JOYPORT;
			status->status = CONT_CARD_ON;
		} else {
			status->errnum = CONT_NO_RESPONSE_ERROR;
			status->type = 0;
			status->status = 0;
		}
	}
}

/* Motor */

s32 osMotorProbe(OSMesgQueue *ctrlrqueue, OSPfs* pfs, s32 channel)
{
	if (pfs && inputRumbleSupported(channel)) {
		pfs->queue = ctrlrqueue;
		pfs->channel = channel;
		pfs->activebank = 0xff;
		pfs->status = PFS_MOTOR_INITIALIZED;
		return 0;
	}
	return PFS_ERR_NOPACK;
}

s32 __osMotorAccess(OSPfs *pfs, s32 cmd)
{
	if (!pfs || pfs->channel < 0 || pfs->channel >= INPUT_MAX_CONTROLLERS) {
		return PFS_ERR_NOPACK;
	}

	const f32 strength = (f32)(cmd == MOTOR_START);
	inputRumble(pfs->channel, strength, 5.f); // hope someone turns it off in those 5 seconds

	return 0;
}

/* Eeprom */

static inline void osEepromSetPath(void)
{
	const char *extPath = sysArgGetString("--eeprom-file");
	if (extPath && extPath[0]) {
		if (extPath[0] == '$' || fsPathIsAbsolute(extPath) || fsPathIsCwdRelative(extPath)) {
			strncpy(eepromPath, extPath, FS_MAXPATH);
		} else {
			// just a filename, look for it in the save dir
			snprintf(eepromPath, FS_MAXPATH, "$S/%s", extPath);
		}
	} else {
		strncpy(eepromPath, EEPROM_PATH, FS_MAXPATH);
	}
}

static inline void osEeepromLoad(const char *fname)
{
	if (!eepromLoaded) {
		eepromLoaded = 1;
		FILE *fp = fsFileOpenRead(fname);
		if (fp) {
			fread(eeprom, 1, EEPROM_SIZE, fp);
			fsFileFree(fp);
		} else {
			sysLogPrintf(LOG_NOTE, "could not read EEPROM from `%s`: %s", fsFullPath(fname), strerror(errno));
		}
	}
}

static inline void osEeepromSave(const char *fname)
{
	FILE* fp = fsFileOpenWrite(fname);
	if (fp) {
		fwrite(eeprom, 1, EEPROM_SIZE, fp);
		fsFileFree(fp);
	} else {
		sysLogPrintf(LOG_ERROR, "could not save EEPROM to `%s`: %s", fsFullPath(fname), strerror(errno));
	}
}

s32 osEepromProbe(OSMesgQueue *mq)
{
	return EEPROM_TYPE_16K;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
	if (!eepromPath[0]) {
		osEepromSetPath();
	}

	osEeepromLoad(eepromPath);

	memcpy(buffer, eeprom + address * 8, nbytes);

	return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
	if (!eepromPath[0]) {
		osEepromSetPath();
	}

	osEeepromLoad(eepromPath);

	memcpy(eeprom + address * 8, buffer, nbytes);

	osEeepromSave(eepromPath);

	return 0;
}

/* Pfs */

s32 osPfsIsPlug(OSMesgQueue *queue, u8 *pattern)
{
	if (pattern) {
		*pattern = 0;
		for (s32 i = 0; i < MAXCONTROLLERS; ++i) {
			if (inputRumbleSupported(i)) {
				*pattern |= 1 << i;
			}
		}
	}
	return 0;
}

s32 osPfsInitPak(OSMesgQueue *queue, OSPfs *pfs, int channel)
{
	// if rumble is supported, indicate that we have a rumble pak instead
	return inputRumbleSupported(channel) ? PFS_ERR_DEVICE : PFS_ERR_NOPACK;
}

s32 osPfsChecker(OSPfs *pfs)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsFreeBlocks(OSPfs *pfs, s32 *remaining)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsNumFiles(OSPfs *pfs, s32 *max_files, s32 *files_used)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsAllocateFile(OSPfs *pfs, u16 company_code, u32 game_code, u8 *game_name, u8 *ext_name, int num_bytes, s32 *file_no)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsFindFile(OSPfs *pfs, u16 company_code, u32 game_code, u8 *game_name, u8 *ext_name, s32 *file_no)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsDeleteFile(OSPfs *pfs, u16 company_code, u32 game_code, u8 *game_name, u8 *ext_name)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsReSizeFile(OSPfs *pfs, u16 company_code, u32 game_code, u8 *game_name, u8 *ext_name, int length)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsFileState(OSPfs *pfs, s32 fileNo, OSPfsState *state)
{
	return PFS_ERR_NOPACK;
}

s32 osPfsReadWriteFile(OSPfs* pfs, s32 fileNo, u8 flag, int offset, int size, u8* data)
{
	return PFS_ERR_NOPACK;
}

/* Gbpak */

s32 osGbpakInit(OSMesgQueue *queue, OSPfs *pfs, s32 ch)
{
	return PFS_ERR_NOPACK;
}

s32 osGbpakPower(OSPfs *pfs, s32 flag)
{
	return PFS_ERR_NOPACK;
}

s32 osGbpakReadWrite(OSPfs *pfs, u16 flag, u16 addr, u8 *buf, u16 size)
{
	return PFS_ERR_NOPACK;
}

s32 osGbpakReadId(OSPfs *pfs, OSGbpakId *id, u8 *status)
{
	return PFS_ERR_NOPACK;
}

/* Misc */

void osWritebackDCacheAll(void)
{

}

void osWritebackDCache(void *a, s32 b)
{

}

void osInvalDCache(void *a, s32 b)
{

}

s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction, uintptr_t devAddr, void *vAddr, u32 nbytes, OSMesgQueue *mq)
{
	memcpy(vAddr, (const void *)devAddr, nbytes);
	return 0;
}

s32 osPiReadIo(u32 devaddr, u32 *data)
{
	return 0;
}

uintptr_t osVirtualToPhysical(void *addr)
{
	return (uintptr_t)addr;
}

u32 osGetMemSize(void)
{
	return osMemSize;
}

OSIntMask osGetIntMask(void)
{
	return 0;
}

OSIntMask osSetIntMask(OSIntMask mask)
{
	return 0;
}

/* libc compatibility wrappers */

#ifndef PLATFORM_OSX

void bzero(void *ptr, size_t size)
{
	memset(ptr, 0, size);
}

void bcopy(const void *src, void *dst, size_t n)
{
	memcpy(dst, src, n);
}

s32 bcmp(const void *s1, const void *s2, size_t n)
{
	return memcmp(s1, s2, n);
}

#endif

/*
 * The rest of libultra that GoldenEye's engine reaches for. These drove N64
 * hardware that has no counterpart here: the PI manager serialised cartridge
 * DMA (osPiStartDma above is a memcpy, so nothing needs serialising), EEPROM
 * held the saves (port/src/libultra.c keeps those in a file), and osSetTimer
 * paced on the VI, which OpenXR now owns.
 */

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt)
{
	(void)pri; (void)cmdQ; (void)cmdBuf; (void)cmdMsgCnt;
}

s32 osPiWriteIo(u32 devAddr, u32 data)
{
	(void)devAddr; (void)data;
	return 0;
}

s32 osPfsInit(OSMesgQueue *queue, OSPfs *pfs, int channel)
{
	(void)queue; (void)pfs;
	/*
	 * joyRumblePakInit only tries osMotorInit when osPfsInit says the pak is a
	 * device that is not a controller pak - which is what a Rumble Pak is. With
	 * NOPACK here the game never enabled rumble, so firing (gunfire.c, 0.1 s)
	 * and damage never reached the Quest controllers. Report a Rumble Pak
	 * wherever the port can vibrate; GoldenEye saves to EEPROM, not a pak.
	 */
	if (inputRumbleSupported(channel)) {
		return PFS_ERR_DEVICE;
	}
	return PFS_ERR_NOPACK;
}

s32 osEepromRead(OSMesgQueue *queue, u8 address, u8 *buffer)
{
	(void)queue;
	return osEepromLongRead(queue, address, buffer, 8);
}

s32 osEepromWrite(OSMesgQueue *queue, u8 address, u8 *buffer)
{
	(void)queue;
	return osEepromLongWrite(queue, address, buffer, 8);
}

int osSetTimer(OSTimer *t, OSTime countdown, OSTime interval, OSMesgQueue *mq, OSMesg msg)
{
	/*
	 * Fires at once. The only user left is the controller start-up delay in
	 * bossInitMainthreadData, which waits on the message and has nothing to
	 * wait for here.
	 */
	(void)t; (void)countdown; (void)interval;
	if (mq) {
		osSendMesg(mq, msg, OS_MESG_NOBLOCK);
	}
	return 0;
}
