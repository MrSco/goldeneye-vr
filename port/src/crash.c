#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "platform.h"

#define CRASH_LOG_FNAME "pd.crash.log"
#define CRASH_MAX_MSG 8192
#define CRASH_MAX_SYM 256
#define CRASH_MAX_FRAMES 32
#define CRASH_MSG(...) \
	if (msglen < CRASH_MAX_MSG) msglen += snprintf(msg + msglen, CRASH_MAX_MSG - msglen, __VA_ARGS__)

/* gfx_pc.cpp: logs the display list it was running when this thread crashed drawing */
int gevrGfxCrashContext(const void *fault);

#if defined(PLATFORM_WIN32) || (defined(GEVR_PC) && defined(_WIN32))

#include <windows.h>
#include <dbghelp.h>
#include <inttypes.h>
#include <excpt.h>

// NOTE: game builds with gcc, which means we have no PDBs for the windows version
// this means that you generally won't get any symbol names in the main executable

static LPTOP_LEVEL_EXCEPTION_FILTER prevExFilter;

static void *crashGetModuleBase(const void *addr)
{
	HMODULE h = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, addr, &h);
	return (void *)h;
}

static void crashStackTrace(char *msg, PEXCEPTION_POINTERS exinfo)
{
	CONTEXT context = *exinfo->ContextRecord;
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	SymSetOptions(SymGetOptions() | SYMOPT_DEBUG | SYMOPT_LOAD_LINES);
	SymInitialize(process, NULL, TRUE);

	STACKFRAME64 stackframe;
	memset(&stackframe, 0, sizeof(stackframe));

	DWORD image;
#ifdef PLATFORM_X86
	image = IMAGE_FILE_MACHINE_I386;
	stackframe.AddrPC.Offset = context.Eip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Ebp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Esp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#elif defined(PLATFORM_X86_64) || defined(__x86_64__)
	image = IMAGE_FILE_MACHINE_AMD64;
	stackframe.AddrPC.Offset = context.Rip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Rsp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Rsp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#else
	snprintf(msg, CRASH_MAX_MSG, "no stack trace available on this arch\n");
	return;
#endif

	DWORD disp = 0;
	DWORD64 disp64 = 0;
	IMAGEHLP_LINE64 line;
	DWORD msglen = 0;

	CRASH_MSG("EXCEPTION: 0x%08lx\n", exinfo->ExceptionRecord->ExceptionCode);
	CRASH_MSG("PC: %p", exinfo->ExceptionRecord->ExceptionAddress);
	if (SymGetLineFromAddr64(process, (uintptr_t)exinfo->ExceptionRecord->ExceptionAddress, &disp, &line)) {
		CRASH_MSG(": %s:%lu+%lu", line.FileName, line.LineNumber, disp);
	}
	CRASH_MSG("\nMODULE: [%p]\n", crashGetModuleBase(exinfo->ExceptionRecord->ExceptionAddress));
	CRASH_MSG("MAIN MODULE: [%p]\n", crashGetModuleBase(crashInit));
	CRASH_MSG("\nBACKTRACE:\n");

	char symbuf[sizeof(SYMBOL_INFO) + CRASH_MAX_SYM * sizeof(TCHAR)];
	PSYMBOL_INFO sym = (PSYMBOL_INFO)symbuf;
	sym->SizeOfStruct = sizeof(*sym);
	sym->MaxNameLen = CRASH_MAX_SYM;

	s32 i;
	for (i = 0; i < CRASH_MAX_FRAMES; ++i) {
		const BOOL res = StackWalk64(image, process, thread, &stackframe, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
		if (!res) {
			break;
		}

		CRASH_MSG("#%02d: %p", i, (void *)(uintptr_t)stackframe.AddrPC.Offset);

		if (SymFromAddr(process, stackframe.AddrPC.Offset, &disp64, sym)) {
			CRASH_MSG(": %s+%llu", sym->Name, disp64);
		} else if (process, stackframe.AddrPC.Offset) {
			const uintptr_t modbase = (uintptr_t)crashGetModuleBase((void *)(uintptr_t)stackframe.AddrPC.Offset);
			const uintptr_t modofs = (uintptr_t)stackframe.AddrPC.Offset - modbase;
			CRASH_MSG(": [%p]+%p", (void *)modbase, (void *)modofs);
		}

		if(SymGetLineFromAddr64(process, stackframe.AddrPC.Offset, &disp, &line)) {
			CRASH_MSG(" (%s:%lu+%lu)", line.FileName, line.LineNumber, disp);
		}

		CRASH_MSG("\n");
	}

	if (i <= 1) {
		CRASH_MSG("no information\n");
	} else if (i == CRASH_MAX_FRAMES) {
		CRASH_MSG("...\n");
	}

	SymCleanup(process);
}

#ifdef GEVR_PC
/* port/pc/pc_reports.cpp: what the launcher offers to report on the next start */
void pcCrashRecord(const char *summary, const char *trace);
void pcReportStartup(void);
#endif

static long __stdcall crashHandler(PEXCEPTION_POINTERS exinfo)
{
	char msg[CRASH_MAX_MSG + 1] = { 0 };

	if (IsDebuggerPresent()) {
		if (prevExFilter) {
			return prevExFilter(exinfo);
		}
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p CODE=0x%08lx", exinfo->ExceptionRecord->ExceptionAddress, exinfo->ExceptionRecord->ExceptionCode);
	{
		/* the display list the renderer was running, if it crashed drawing (gfx_pc.cpp) */
		const EXCEPTION_RECORD *rec = exinfo->ExceptionRecord;
		gevrGfxCrashContext(rec->NumberParameters >= 2 ? (const void *)rec->ExceptionInformation[1] : NULL);
	}

	fflush(stderr);
	fflush(stdout);

	crashStackTrace(msg, exinfo);

#ifdef GEVR_PC
	{
		/* the address in the exe, for the release's .debug file (llvm-addr2line) */
		char summary[256];
		const uintptr_t pc = (uintptr_t)exinfo->ExceptionRecord->ExceptionAddress;
		const uintptr_t base = (uintptr_t)crashGetModuleBase((void *)pc);
		snprintf(summary, sizeof(summary), "EXCEPTION 0x%08lx at %p (module %p + 0x%llx)",
			exinfo->ExceptionRecord->ExceptionCode, (void *)pc, (void *)base,
			(unsigned long long)(base ? pc - base : 0));
		pcCrashRecord(summary, msg);
	}
#endif

	// open log file for the crash dump if one hasn't been opened yet
	if (!sysLogIsOpen()) {
		FILE *f = fopen(CRASH_LOG_FNAME, "wb");
		if (f) {
			fprintf(f, "Crash!\n\n%s", msg);
			fclose(f);
		}
	}

	sysFatalError("Crash!\n\n%s", msg);

	return EXCEPTION_CONTINUE_EXECUTION;
}

#elif defined(PLATFORM_LINUX)

#include <ucontext.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>
#include <sys/fcntl.h>

static struct sigaction prevSigAction;

static s32 crashIsDebuggerPresent(void)
{
	static s32 result = -1;

	if (result >= 0) {
		return result;
	}

	char buf[4096] = { 0 };

	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0) {
		result = 0;
		return 0;
	}

	int rx = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (rx <= 0) {
		result = 0;
		return 0;
	}

	buf[rx] = 0;

	char *str = strstr(buf, "TracerPid:");
	if (!str) {
		result = 0;
		return 0;
	}

	str += 10;

	while (*str && !isdigit(*str)) {
		if (*str == '\n') {
			result = 0;
			return 0;
		}
		++str;
	}

	result = (atoi(str) != 0);
	return result;
}

static void *crashGetModuleBase(const void *addr)
{
	Dl_info info;
	if (dladdr(addr, &info)) {
		return info.dli_fbase;
	}
	return NULL;
}

static void crashStackTrace(char *msg, s32 sig, void *pc)
{
	u32 msglen = 0;
#ifdef ANDROID
	CRASH_MSG("SIGNAL: %d\n", sig);
	CRASH_MSG("PC: %p\n", pc);
	CRASH_MSG("Stack trace not available on Android\n");
	return;
#else
	void *frames[CRASH_MAX_FRAMES] = { NULL };

	const s32 nframes = backtrace(frames, CRASH_MAX_FRAMES);
	if (nframes <= 0) {
		CRASH_MSG("no information\n");
		return;
	}

	char **strings = backtrace_symbols(frames, nframes);

	CRASH_MSG("SIGNAL: %d\n", sig);
	CRASH_MSG("PC: ");
	if (pc) {
		CRASH_MSG("%p\n", pc);
	} else if (strings) {
		CRASH_MSG("%s\n", strings[0]);
	} else {
		CRASH_MSG("%p\n", frames[0]);
	}

	CRASH_MSG("MODULE: %p\n", crashGetModuleBase(frames[0]));
	CRASH_MSG("MAIN MODULE: %p\n", crashGetModuleBase(crashInit));
	CRASH_MSG("\nBACKTRACE:\n");

	s32 i;
	for (i = 0; i < nframes; ++i) {
		CRASH_MSG("#%02d: ", i);
		if (strings && strings[i]) {
			CRASH_MSG("%s\n", strings[i]);
		} else {
			CRASH_MSG("%p\n", frames[i]);
		}
	}

	if (i == CRASH_MAX_FRAMES) {
		CRASH_MSG("...\n");
	} else if (i <= 1) {
		CRASH_MSG("no information\n");
	}

	free(strings);
#endif
}

static void crashHandler(s32 sig, siginfo_t *siginfo, void *ctx)
{
	char msg[CRASH_MAX_MSG + 1] = { 0 };

	if (crashIsDebuggerPresent()) {
		return;
	}

	void *pc = NULL;
	if (ctx) {
		ucontext_t *ucontext = (ucontext_t *)ctx;
#ifdef PLATFORM_X86
		pc = (void *)ucontext->uc_mcontext.gregs[REG_EIP];
#elif defined(PLATFORM_X86_64)
		pc = (void *)ucontext->uc_mcontext.gregs[REG_RIP];
#elif defined(PLATFORM_ARM) && defined(PLATFORM_64BIT)
		pc = (void *)ucontext->uc_mcontext.pc;
#elif defined(PLATFORM_ARM)
		pc = (void *)ucontext->uc_mcontext.arm_pc;
#endif
	}

	sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p SIGNAL=%d", pc, sig);

	fflush(stderr);
	fflush(stdout);

	crashStackTrace(msg, sig, pc);

#ifdef GEVR_PC
	{
		/* the PC game on Linux: what the launcher offers to report on the next start */
		void pcCrashRecord(const char *summary, const char *trace);
		char summary[160];
		Dl_info info;
		const uintptr_t base = (pc && dladdr(pc, &info)) ? (uintptr_t)info.dli_fbase : 0;
		snprintf(summary, sizeof(summary), "SIGNAL %d at %p (module %p + 0x%llx)", (int)sig, pc, (void *)base,
			(unsigned long long)(base ? (uintptr_t)pc - base : 0));
		pcCrashRecord(summary, msg);
	}
#endif

	sysFatalError("Crash!\n\n%s", msg);
}

#elif defined(ANDROID)

/*
 * The Quest and the Android phone: Android's crash reporter (debuggerd) writes
 * the tombstone a crash report sends. This handler only logs the renderer's
 * display list first (reports repo #19/#20: the tombstone alone could not
 * name what wrote a bad vertex load), then calls the handler that was there
 * before it, as crash reporters chain: debuggerd's dumps, then lets the fault
 * end the process (or sends a sent signal, the watchdog's, again). A fault
 * inside the logging jumps back out of it; a handler entered over and over
 * gives up to the default action, so it can never hold the process up.
 */
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>

unsigned long gevrCrashThreadId(void);
void gevrCrashLog(const char *fmt, ...);

static struct sigaction s_prevSegv, s_prevBus;
static sigjmp_buf s_dumpJump;
static volatile unsigned long s_dumpThread;   /* 0 before the log, the thread during it, ~0 after */
static volatile int s_chainEntries;

static void crashChainHandler(int sig, siginfo_t *info, void *ctx)
{
	const unsigned long self = gevrCrashThreadId();
	const struct sigaction *prev = sig == SIGBUS ? &s_prevBus : &s_prevSegv;

	if (s_dumpThread == self) {
		siglongjmp(s_dumpJump, 1);   /* only while the log below runs on this thread */
	}
	if (++s_chainEntries > 4) {
		signal(sig, SIG_DFL);   /* a loop: let the default end it */
		return;
	}
	if (s_dumpThread == 0 && info && info->si_code > 0) {   /* a fault, not a signal sent */
		s_dumpThread = self;
		if (sigsetjmp(s_dumpJump, 1) != 0) {
			gevrCrashLog("crashdl: the context log faulted itself; the system's report follows");
		} else {
			gevrGfxCrashContext(info->si_addr);
		}
		s_dumpThread = ~0ul;
	}
	if ((prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction && prev->sa_sigaction != crashChainHandler) {
		prev->sa_sigaction(sig, info, ctx);
	} else if (!(prev->sa_flags & SA_SIGINFO) && prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN) {
		prev->sa_handler(sig);
	} else {
		signal(sig, SIG_DFL);   /* nothing before us: the default, on the fault again */
		if (info && info->si_code <= 0) raise(sig);
	}
}

#endif

s32 g_CrashEnabled = 0;

static char crashMsg[1024];

#ifdef GEVR_PC
void pcReportStartup(void);   /* port/pc/pc_reports.cpp */
#endif

void crashInit(void)
{
#ifdef GEVR_PC
	pcReportStartup();   // the last run's crash or unexpected end, before this run's marker
#endif
#if defined(PLATFORM_WIN32) || (defined(GEVR_PC) && defined(_WIN32))
	SetErrorMode(SEM_FAILCRITICALERRORS);
	prevExFilter = SetUnhandledExceptionFilter(crashHandler);
	g_CrashEnabled = 1;
#elif defined(PLATFORM_LINUX)
	struct sigaction sigact = { 0 };
	sigact.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigact.sa_sigaction = crashHandler;
	sigaction(SIGSEGV, &sigact, &prevSigAction);
	sigaction(SIGABRT, &sigact, &prevSigAction);
	sigaction(SIGBUS,  &sigact, &prevSigAction);
	sigaction(SIGILL,  &sigact, &prevSigAction);
	g_CrashEnabled = 1;
#elif defined(ANDROID)
	static int installed;
	if (!installed) {
		struct sigaction sigact;
		installed = 1;
		memset(&sigact, 0, sizeof(sigact));
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;   /* NODEFER: a fault in the log comes back here */
		sigact.sa_sigaction = crashChainHandler;
		sigaction(SIGSEGV, &sigact, &s_prevSegv);
		sigaction(SIGBUS, &sigact, &s_prevBus);
		{
			Dl_info dl;
			const void *before = (s_prevSegv.sa_flags & SA_SIGINFO) ? (const void *)s_prevSegv.sa_sigaction : (const void *)s_prevSegv.sa_handler;
			sysLogPrintf(LOG_NOTE, "crash: renderer context handler on; SIGSEGV went to %p (%s)%s", before,
				before && dladdr(before, &dl) && dl.dli_sname ? dl.dli_sname : "?",
				before == (const void *)crashChainHandler ? ", ours already" : "");
		}
	} else {
		sysLogPrintf(LOG_NOTE, "crash: renderer context handler already on");
	}
#endif
}

void crashShutdown(void)
{
	if (!g_CrashEnabled) {
		return;
	}
#if defined(PLATFORM_WIN32) || (defined(GEVR_PC) && defined(_WIN32))
	if (prevExFilter) {
		SetUnhandledExceptionFilter(prevExFilter);
	}
#elif defined(PLATFORM_LINUX)
	sigaction(SIGSEGV, &prevSigAction, NULL);
	sigaction(SIGABRT, &prevSigAction, NULL);
	sigaction(SIGBUS,  &prevSigAction, NULL);
	sigaction(SIGILL,  &prevSigAction, NULL);
#elif defined(ANDROID)
	if (s_prevSegv.sa_sigaction || s_prevSegv.sa_handler) {
		sigaction(SIGSEGV, &s_prevSegv, NULL);
		sigaction(SIGBUS, &s_prevBus, NULL);
	}
#endif
	g_CrashEnabled = 0;
}

void crashCreateThread(void)
{

}

void crashSetMessage(char *string)
{

}

void crashReset(void)
{

}

void crashAppendChar(char c)
{

}

/* --- for gfx_pc.cpp's crash context --- */
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#ifdef __linux__
#include <sys/syscall.h>
#else
#include <pthread.h>
#endif
#endif

#ifdef ANDROID
#include <android/log.h>
#endif
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdarg.h>

/* A line from the crash handler: a static buffer (the signal stack is small)
 * and logcat, which the next start's report takes; elsewhere the game's log. */
void gevrCrashLog(const char *fmt, ...)
{
	static char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
#ifdef ANDROID
	__android_log_write(ANDROID_LOG_ERROR, "GoldenEye", line);
#else
	sysLogPrintf(LOG_ERROR, "%s", line);
#endif
}

/* this thread, to tell the drawing thread's crash from another's */
unsigned long gevrCrashThreadId(void)
{
#if defined(_WIN32)
	return (unsigned long)GetCurrentThreadId();
#elif defined(__linux__)
	return (unsigned long)syscall(SYS_gettid);
#else
	return (unsigned long)(uintptr_t)pthread_self();
#endif
}

/* a page nobody may read: the crash test's (gfx_pc.cpp gevrMaybeCrashTest) */
void *gevrCrashNoAccessPage(void)
{
#ifdef _WIN32
	return VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
#else
	void *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? NULL : p;
#endif
}

/*
 * What the address is in: Linux and Android, the /proc/self/maps lines that
 * hold it and lie on each side (a freed block shows as a gap, or as someone
 * else's mapping now); Windows, VirtualQuery's region. Plain syscalls and
 * static buffers: this runs in the crash handler.
 */
void gevrCrashLogMaps(const char *what, const void *addr)
{
#if defined(__linux__)
	static char buf[4096];
	static char line[512], below[512];
	const unsigned long long a = (unsigned long long)(uintptr_t)addr;
	int fd = open("/proc/self/maps", O_RDONLY);
	int len = 0, held = 0;
	below[0] = 0;
	if (fd < 0) {
		gevrCrashLog("crashdl: %s %p: no /proc/self/maps", what, addr);
		return;
	}
	for (;;) {
		const int got = (int)read(fd, buf, sizeof(buf));
		if (got <= 0) break;
		for (int i = 0; i < got; i++) {
			if (buf[i] != '\n') {
				if (len < (int)sizeof(line) - 1) line[len++] = buf[i];
				continue;
			}
			line[len] = 0;
			len = 0;
			char *dash;
			const unsigned long long start = strtoull(line, &dash, 16);
			const unsigned long long end = *dash == '-' ? strtoull(dash + 1, NULL, 16) : 0;
			if (end <= a) {
				memcpy(below, line, sizeof(below));
				continue;
			}
			if (start <= a) {
				if (below[0]) gevrCrashLog("crashdl: %s %p: below  %s", what, addr, below);
				gevrCrashLog("crashdl: %s %p: holds  %s", what, addr, line);
				held = 1;
				continue;
			}
			if (!held) {
				if (below[0]) gevrCrashLog("crashdl: %s %p: below  %s", what, addr, below);
				gevrCrashLog("crashdl: %s %p: unmapped", what, addr);
			}
			gevrCrashLog("crashdl: %s %p: above  %s", what, addr, line);
			close(fd);
			return;
		}
	}
	close(fd);
	if (!held) gevrCrashLog("crashdl: %s %p: past every mapping (last %s)", what, addr, below);
#elif defined(_WIN32)
	MEMORY_BASIC_INFORMATION mbi;
	if (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
		gevrCrashLog("crashdl: %s %p: region %p + 0x%llx, allocation %p, state 0x%lx, protect 0x%lx, type 0x%lx",
			what, addr, mbi.BaseAddress, (unsigned long long)mbi.RegionSize, mbi.AllocationBase,
			(unsigned long)mbi.State, (unsigned long)mbi.Protect, (unsigned long)mbi.Type);
	} else {
		gevrCrashLog("crashdl: %s %p: VirtualQuery failed", what, addr);
	}
#else
	(void)what;
	(void)addr;
#endif
}

