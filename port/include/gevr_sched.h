#ifndef _GEVR_SCHED_H_
#define _GEVR_SCHED_H_

#include <PR/ultratypes.h>
#include <PR/os.h>

/*
 * The frame pump: what stands in for the N64 scheduler thread.
 *
 * GoldenEye's main loop (bossMainloop) is written against the libultra
 * scheduler. Each vsync the scheduler thread posted a RETRACE message to the
 * game's frame queue; the game answered by building a display list and
 * sending an RSP task to the scheduler's command queue; when the RCP finished,
 * the scheduler posted the task's DONE message back to the frame queue. The
 * game blocks on that queue in between.
 *
 * There is no scheduler thread here and no hardware to wait on, so the two
 * halves of that protocol are run from inside the message calls themselves:
 *
 *   - a task sent to the command queue is drawn on the spot, through the
 *     renderer, and its DONE message is queued for the game straight away;
 *   - when the game blocks on an empty frame queue, the frame that was drawn
 *     is presented, input is polled, and a RETRACE message is handed back.
 *
 * So one trip round the game's loop is one displayed frame, and the pacing
 * is the renderer's (the VR runtime's frame wait).
 */

/* Called by osSendMesg. Returns 1 if the message was a task and was consumed. */
s32 gevrSchedSend(OSMesgQueue *mq, OSMesg msg);

/*
 * Called by osRecvMesg when a blocking receive finds the queue empty. Returns
 * 1 with *msg filled if the queue is the frame queue and a retrace was
 * produced, 0 for any other queue.
 */
s32 gevrSchedBlockedRecv(OSMesgQueue *mq, OSMesg *msg);

/* PORT probe: snapshots around menu transitions, including a stalled pump. */
void gevrSchedTraceMenu(s32 menu, s32 afterTick);

#endif
