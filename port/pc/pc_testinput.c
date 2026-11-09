/*
 * Scripted input for testing the PC game without touching the desktop
 * (docs/pc-platforms.md). The headset has its control files (gevr_cheat.txt,
 * gevr_warp.txt); here a script feeds mouse moves, clicks and held keys into
 * the game's own input, and writes the window's frames to files, so a test
 * runs while someone uses the computer: the game never grabs the real mouse
 * or raises its window.
 *
 *   GoldenEyeVR.exe --boot-stage 33 --input-script test.txt
 *
 * One command a line, at a time in milliseconds from the first game frame:
 *
 *   30000 capture            take the mouse for looking, as a click would
 *   30500 click 120          left button held 120 ms (fires)
 *   30400 point 640 360      the launcher's pointer at that spot of its canvas
 *   31000 text ABCD1234      typed into whatever text box has the focus
 *   30500 rclick 400         right button held 400 ms (aims)
 *   31000 move 15 0 40       15, 0 counts a frame for 40 frames
 *   32000 key Up 1200        hold a key (an SDL scancode name) 1200 ms
 *   33000 shot dam_turned    the window to shots/dam_turned.bmp
 *   34000 pad a 200          a virtual gamepad's button (SDL's names: a, b,
 *                            x, y, start, leftshoulder, dpup...) for 200 ms
 *   34000 axis leftx 0.8 900 a virtual gamepad's axis (leftx, lefty, rightx,
 *                            righty, lefttrigger, righttrigger), -1..1 with
 *                            y down positive as SDL has it, for 900 ms
 *   40000 quit
 *   40000 crash              a deliberate access violation (the crash report test)
 *
 * '#' starts a comment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "system.h"

#define TI_MAX 512

enum { TI_CAPTURE, TI_RELEASE, TI_CLICK, TI_RCLICK, TI_MOVE, TI_KEY, TI_SHOT, TI_QUIT, TI_PAD, TI_AXIS, TI_POINT, TI_TEXT, TI_CRASH };

struct ticmd {
	u32 at;
	s32 kind;
	s32 a, b, c;            /* click: ms; move: dx, dy, frames; key: scancode, ms; pad: button, ms; axis: axis, ms */
	f32 value;              /* axis */
	char name[48];          /* shot */
};

static struct ticmd s_cmds[TI_MAX];
static s32 s_count;
static s32 s_next;
static s32 s_active = -1;   /* -1: not read yet */
static u64 s_start;          /* SDL_GetTicks64 at the first frame */
static s32 s_started;
static u32 s_leftUntil, s_rightUntil;
static s32 s_moveDx, s_moveDy, s_moveFrames;
static u32 s_keyUntil[SDL_NUM_SCANCODES];
static char s_shotPending[48];
static s32 s_pointX = -1, s_pointY = -1;   /* the launcher's scripted pointer, canvas units */
static s32 s_wantPad;                    /* the script drives a virtual gamepad */
static SDL_Joystick *s_pad;
static u32 s_padUntil[SDL_CONTROLLER_BUTTON_MAX];
static u32 s_axisUntil[SDL_CONTROLLER_AXIS_MAX];
static f32 s_axisValue[SDL_CONTROLLER_AXIS_MAX];

static void tiLoad(void)
{
	const char *path = sysArgGetString("--input-script");
	char line[256];
	FILE *f;

	s_active = 0;
	if (!path || !*path) {
		return;
	}
	f = fopen(path, "r");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "input script: can't open %s", path);
		return;
	}
	while (fgets(line, sizeof(line), f) && s_count < TI_MAX) {
		struct ticmd *c = &s_cmds[s_count];
		char word[32] = "", arg[48] = "", extra[32] = "";
		char *hash = strchr(line, '#');
		u32 at;
		s32 n;

		if (hash) *hash = '\0';
		memset(c, 0, sizeof(*c));
		n = sscanf(line, "%u %31s %47s %31s %d", &at, word, arg, extra, &c->c);
		c->b = atoi(extra);
		if (n < 2) continue;
		c->at = at;
		if (!strcmp(word, "capture")) c->kind = TI_CAPTURE;
		else if (!strcmp(word, "release")) c->kind = TI_RELEASE;
		else if (!strcmp(word, "click")) { c->kind = TI_CLICK; c->a = n >= 3 ? atoi(arg) : 100; }
		else if (!strcmp(word, "rclick")) { c->kind = TI_RCLICK; c->a = n >= 3 ? atoi(arg) : 100; }
		else if (!strcmp(word, "move")) { c->kind = TI_MOVE; c->a = atoi(arg); if (n < 5) c->c = 1; }
		else if (!strcmp(word, "key")) {
			c->kind = TI_KEY;
			c->a = SDL_GetScancodeFromName(arg);
			if (c->a == SDL_SCANCODE_UNKNOWN) {
				sysLogPrintf(LOG_ERROR, "input script: no key called %s", arg);
				continue;
			}
		}
		else if (!strcmp(word, "shot")) { c->kind = TI_SHOT; snprintf(c->name, sizeof(c->name), "%s", n >= 3 ? arg : "shot"); }
		else if (!strcmp(word, "quit")) c->kind = TI_QUIT;
		else if (!strcmp(word, "crash")) c->kind = TI_CRASH;
		else if (!strcmp(word, "text")) { c->kind = TI_TEXT; snprintf(c->name, sizeof(c->name), "%s", arg); }
		else if (!strcmp(word, "point")) { c->kind = TI_POINT; c->a = atoi(arg); c->b = atoi(extra); }
		else if (!strcmp(word, "pad")) {
			c->kind = TI_PAD;
			c->a = SDL_GameControllerGetButtonFromString(arg);
			c->b = n >= 4 ? atoi(extra) : 100;
			if (c->a == SDL_CONTROLLER_BUTTON_INVALID) {
				sysLogPrintf(LOG_ERROR, "input script: no pad button called %s", arg);
				continue;
			}
			s_wantPad = 1;
		}
		else if (!strcmp(word, "axis")) {
			c->kind = TI_AXIS;
			c->a = SDL_GameControllerGetAxisFromString(arg);
			c->value = (f32)atof(extra);
			if (c->a == SDL_CONTROLLER_AXIS_INVALID || n < 5) {
				sysLogPrintf(LOG_ERROR, "input script: axis needs a name, a value and ms (%s)", arg);
				continue;
			}
			c->b = c->c;
			s_wantPad = 1;
		}
		else {
			sysLogPrintf(LOG_ERROR, "input script: unknown command %s", word);
			continue;
		}
		s_count++;
	}
	fclose(f);
	s_active = 1;
	sysLogPrintf(LOG_NOTE, "input script: %d commands from %s", s_count, path);
}

/* A script is running: the game must not grab the real mouse or the focus. */
s32 pcTestInputActive(void)
{
	if (s_active < 0) tiLoad();
	return s_active;
}

/*
 * Once a frame from inputUpdate: this frame's scripted mouse motion and
 * buttons replace the real ones. *lock is set to 1 or 0 when the script
 * captures or releases the mouse, and left alone otherwise.
 */
void pcTestInputFrame(s32 *mdx, s32 *mdy, u32 *buttons, s32 *lock)
{
	u32 now;

	if (!pcTestInputActive()) return;
	if (!s_started) {
		s_started = 1;
		s_start = SDL_GetTicks64();
	}
	now = (u32)(SDL_GetTicks64() - s_start);

	while (s_next < s_count && s_cmds[s_next].at <= now) {
		const struct ticmd *c = &s_cmds[s_next++];
		switch (c->kind) {
		case TI_CAPTURE: *lock = 1; break;
		case TI_RELEASE: *lock = 0; break;
		case TI_CLICK: s_leftUntil = now + (u32)c->a; break;
		case TI_RCLICK: s_rightUntil = now + (u32)c->a; break;
		case TI_MOVE: s_moveDx = c->a; s_moveDy = c->b; s_moveFrames = c->c; break;
		case TI_KEY: if (c->a > 0 && c->a < SDL_NUM_SCANCODES) s_keyUntil[c->a] = now + (u32)(c->b > 0 ? c->b : 100); break;
		case TI_SHOT: memcpy(s_shotPending, c->name, sizeof(s_shotPending)); break;
		case TI_PAD: s_padUntil[c->a] = now + (u32)c->b; break;
		case TI_POINT: s_pointX = c->a; s_pointY = c->b; break;
		case TI_TEXT: {
			/* as a keyboard types it: SDL's text event (the launcher's keyboardWatch reads it) */
			SDL_Event e;
			memset(&e, 0, sizeof(e));
			e.type = SDL_TEXTINPUT;
			snprintf(e.text.text, sizeof(e.text.text), "%s", c->name);
			SDL_PushEvent(&e);
			break;
		}
		case TI_AXIS: s_axisUntil[c->a] = now + (u32)c->b; s_axisValue[c->a] = c->value; break;
		case TI_QUIT:
			sysLogPrintf(LOG_NOTE, "input script: quit");
			exit(0);
		case TI_CRASH:
			sysLogPrintf(LOG_NOTE, "input script: crash");
			*(volatile int *)(uintptr_t)8 = 1;
			break;
		}
		sysLogPrintf(LOG_NOTE, "input script: %u ms: command %d", now, c->kind);
	}

	if (s_wantPad && !s_pad) {
		/* SDL's virtual game controller, attached once the game has its
		 * controller subsystem: input.c sees it as a pad plugged in */
		const s32 dev = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
				SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
		s_pad = dev >= 0 ? SDL_JoystickOpen(dev) : NULL;
		sysLogPrintf(s_pad ? LOG_NOTE : LOG_ERROR, "input script: virtual gamepad %s (device %d, a game controller: %d, %s)",
				s_pad ? "attached" : SDL_GetError(), dev, dev >= 0 ? SDL_IsGameController(dev) : 0,
				dev >= 0 && SDL_GameControllerNameForIndex(dev) ? SDL_GameControllerNameForIndex(dev) : "-");
		if (!s_pad) s_wantPad = 0;
	}
	if (s_pad) {
		for (s32 i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
			SDL_JoystickSetVirtualButton(s_pad, i, now < s_padUntil[i]);
		}
		for (s32 i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
			const f32 v = now < s_axisUntil[i] ? s_axisValue[i] : 0.0f;
			if (i == SDL_CONTROLLER_AXIS_TRIGGERLEFT || i == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
				/* a trigger's whole raw range is 0..1 to the game controller: released is -32768 */
				SDL_JoystickSetVirtualAxis(s_pad, i, (Sint16)(v * 65535.0f - 32768.0f));
			} else {
				SDL_JoystickSetVirtualAxis(s_pad, i, (Sint16)(v < 0.0f ? v * 32768.0f : v * 32767.0f));
			}
		}
	}

	*mdx = *mdy = 0;
	if (s_moveFrames > 0) {
		*mdx = s_moveDx;
		*mdy = s_moveDy;
		s_moveFrames--;
	}
	*buttons = 0;
	if (now < s_leftUntil) *buttons |= SDL_BUTTON_LMASK;
	if (now < s_rightUntil) *buttons |= SDL_BUTTON_RMASK;
}

/* A key the script holds (an SDL scancode). */
s32 pcTestInputKey(s32 scancode)
{
	if (s_active <= 0 || scancode <= 0 || scancode >= SDL_NUM_SCANCODES || !s_started) return 0;
	return (u32)(SDL_GetTicks64() - s_start) < s_keyUntil[scancode];
}

/* The shot the script asked for, once: its name, or NULL. */
const char *pcTestInputTakeShot(void)
{
	static char name[48];

	if (!s_shotPending[0]) return NULL;
	memcpy(name, s_shotPending, sizeof(name));
	s_shotPending[0] = '\0';
	return name;
}

/* The launcher's scripted pointer (canvas units), 1 once a point command ran. */
s32 pcTestInputPointer(s32 *x, s32 *y)
{
	if (s_pointX < 0) return 0;
	*x = s_pointX;
	*y = s_pointY;
	return 1;
}

/* The scripted left button, for the launcher (input.c reads it through pcTestInputFrame). */
s32 pcTestInputLeftDown(void)
{
	return s_started && (u32)(SDL_GetTicks64() - s_start) < s_leftUntil;
}
