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
 *   30500 rclick 400         right button held 400 ms (aims)
 *   31000 move 15 0 40       15, 0 counts a frame for 40 frames
 *   32000 key Up 1200        hold a key (an SDL scancode name) 1200 ms
 *   33000 shot dam_turned    the window to shots/dam_turned.bmp
 *   40000 quit
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

enum { TI_CAPTURE, TI_RELEASE, TI_CLICK, TI_RCLICK, TI_MOVE, TI_KEY, TI_SHOT, TI_QUIT };

struct ticmd {
	u32 at;
	s32 kind;
	s32 a, b, c;            /* click: ms; move: dx, dy, frames; key: scancode, ms */
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
		char word[32] = "", arg[48] = "";
		char *hash = strchr(line, '#');
		u32 at;
		s32 n;

		if (hash) *hash = '\0';
		memset(c, 0, sizeof(*c));
		n = sscanf(line, "%u %31s %47s %d %d", &at, word, arg, &c->b, &c->c);
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
		case TI_QUIT:
			sysLogPrintf(LOG_NOTE, "input script: quit");
			exit(0);
		}
		sysLogPrintf(LOG_NOTE, "input script: %u ms: command %d", now, c->kind);
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
