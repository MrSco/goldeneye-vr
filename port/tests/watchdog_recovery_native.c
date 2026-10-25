/* Callback scheduling reproduces recovery while the watchdog is logging or
 * reading XR focus. The actual watchdog loop decides whether to signal. */
#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef long long s64;
#define LOG_ERROR 1
#define F_OK 0

static struct { u32 frameCount; } os_scheduler;
static volatile sig_atomic_t gevrWatchdogStackReady, gevrWatchdogFrameWait;
static int g_gevrAppBackgrounded, gevrFrameOpen, gevrVrFrameBegun;
static u32 gevrPumpStage = 3, gevrPumpEntries = 53215, gevrPumpLoops = 1;
static int gevrGameThread, focused = 1, ticks, dumps, kills, abortLogs, focusReads;
static const char *scenario;
static jmp_buf done;

static int is(const char *name) { return strcmp(scenario, name) == 0; }
static void sysSleep(s64 delay)
{
    assert(delay == 10000000);
    ticks++;
    if ((is("recovery-log") && abortLogs) ||
        (is("recovery-focus") && focusReads >= 2)) {
        __atomic_add_fetch(&os_scheduler.frameCount, 1, __ATOMIC_RELAXED);
    }
    if (is("second-hang") && ticks == 8) {
        __atomic_add_fetch(&os_scheduler.frameCount, 1, __ATOMIC_RELAXED);
    }
    if (ticks > 20) longjmp(done, 1);
}
static void sysLogPrintf(int level, const char *fmt, ...)
{
    assert(level == LOG_ERROR);
    if (strstr(fmt, "aborting for a crash report")) {
        abortLogs++;
        if (is("recovery-log"))
            __atomic_add_fetch(&os_scheduler.frameCount, 1, __ATOMIC_RELAXED);
        if (is("pause-log")) g_gevrAppBackgrounded = 1;
        if (is("unfocus-log")) focused = 0;
    }
}
static int gevrVrSessionFocused(void)
{
    focusReads++;
    /* The warning performs the first focus read. Recovery during the abort
     * condition must invalidate that condition before it sends SIGSEGV. */
    if (is("recovery-focus") && focusReads == 2)
        __atomic_add_fetch(&os_scheduler.frameCount, 1, __ATOMIC_RELAXED);
    return focused;
}
static void gevrStackDumpHandler(int sig) { (void)sig; }
static int pthread_kill(int thread, int sig)
{
    (void)thread;
    if (sig == SIGUSR2) {
        dumps++;
        gevrWatchdogStackReady = !is("dump-pending");
        gevrWatchdogFrameWait = is("frame-wait");
    } else {
        assert(sig == SIGSEGV);
        kills++;
    }
    return 0;
}
static int access(const char *path, int mode)
{
    assert(strstr(path, "gevr_watchdog_kill.txt") && mode == F_OK);
    return is("marker") ? 0 : -1;
}

/* PRODUCTION */

int main(int argc, char **argv)
{
    assert(argc == 2);
    scenario = argv[1];
    os_scheduler.frameCount = 100;
    if (is("background")) g_gevrAppBackgrounded = 1;
    if (is("unfocused")) focused = 0;
    if (setjmp(done) == 0) gevrWatchdog(NULL);
    if (is("hang") || is("marker") || is("second-hang")) {
        assert(kills == 1);
        assert(dumps == (is("second-hang") ? 2 : 1));
        assert(ticks == (is("marker") ? 6 : is("second-hang") ? 16 : 9));
    } else if (kills) {
        fprintf(stderr, "stale abort: %s at tick %d\n", scenario, ticks);
        return 1;
    }
    if (is("recovery-log") || is("pause-log") || is("unfocus-log"))
        assert(abortLogs >= 1);
    return 0;
}
