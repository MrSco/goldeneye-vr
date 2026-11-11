"""The LiveSplit client (port/src/gevr_livesplit.c) against a mock of LiveSplit's TCP
server (CommandServer.cs: a line a command, a line back for the get* ones), on a port of
the test's own, so a runner's LiveSplit on this computer is never touched. A two-mission
run with a failed try, the run over, and a new run lost on its first mission."""
from pathlib import Path
import re, shutil, socket, subprocess, sys, tempfile, threading

root = Path(__file__).resolve().parents[2]

# SDL's mutex, thread, clock and delay, on pthreads: all the client uses
FAKE_SDL = r'''
#ifndef FAKE_SDL_H
#define FAKE_SDL_H
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
typedef unsigned int Uint32;
typedef int Sint32;
typedef pthread_mutex_t SDL_mutex;
typedef struct { pthread_t t; } SDL_Thread;
static inline SDL_mutex *SDL_CreateMutex(void) { SDL_mutex *m = malloc(sizeof(*m)); pthread_mutex_init(m, NULL); return m; }
static inline int SDL_LockMutex(SDL_mutex *m) { return pthread_mutex_lock(m); }
static inline int SDL_UnlockMutex(SDL_mutex *m) { return pthread_mutex_unlock(m); }
typedef int (*SDL_ThreadFunction)(void *);
struct fake_sdl_start { SDL_ThreadFunction fn; void *arg; };
static inline void *fake_sdl_run(void *p) { struct fake_sdl_start s = *(struct fake_sdl_start *)p; free(p); s.fn(s.arg); return NULL; }
static inline SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *arg) {
    SDL_Thread *t = malloc(sizeof(*t)); struct fake_sdl_start *s = malloc(sizeof(*s)); (void)name;
    s->fn = fn; s->arg = arg; pthread_create(&t->t, NULL, fake_sdl_run, s); return t; }
static inline void SDL_DetachThread(SDL_Thread *t) { pthread_detach(t->t); free(t); }
static inline Uint32 SDL_GetTicks(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (Uint32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }
static inline void SDL_Delay(Uint32 ms) { usleep(ms * 1000); }
#endif
'''


class MockLiveSplit:
    """LiveSplit's timer as its server sees it: a run of `segments` splits."""
    def __init__(self, segments=2):
        self.segments, self.phase, self.index, self.game = segments, 'NotRunning', -1, 0.0
        self.log = []
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind(('127.0.0.1', 0))
        self.server.listen(1)
        self.port = self.server.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    @staticmethod
    def fmt(seconds):
        h, rest = divmod(seconds, 3600)
        m, s = divmod(rest, 60)
        return '%d:%02d:%05.2f' % (h, m, s)

    def serve(self):
        conn, _ = self.server.accept()
        buf = b''
        while True:
            try:
                data = conn.recv(4096)
            except OSError:   # the client's exit resets the connection on Windows
                return
            if not data:
                return
            buf += data
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                line = line.decode().rstrip('\r')
                self.log.append(line)
                cmd, _, arg = line.partition(' ')
                reply = None
                if cmd == 'getcurrenttimerphase':
                    reply = self.phase
                elif cmd == 'getsplitindex':
                    reply = str(self.index)
                elif cmd == 'getcurrentgametime':
                    reply = self.fmt(self.game)
                elif cmd == 'starttimer' and self.phase == 'NotRunning':
                    self.phase, self.index, self.game = 'Running', 0, 0.0
                elif cmd == 'reset':
                    self.phase, self.index = 'NotRunning', -1
                elif cmd == 'split' and self.phase == 'Running':
                    self.index += 1
                    if self.index == self.segments:
                        self.phase = 'Ended'
                elif cmd == 'setgametime':
                    h, m, s = arg.split(':')
                    self.game = int(h) * 3600 + int(m) * 60 + float(s)
                if reply is not None:
                    conn.sendall((reply + '\n').encode())


def main():
    mock = MockLiveSplit()
    with tempfile.TemporaryDirectory(prefix='gevr-livesplit-') as temp:
        temp = Path(temp)
        (temp / 'SDL.h').write_text(FAKE_SDL, encoding='utf-8')
        exe = temp / 'livesplit.exe'
        args = [shutil.which('gcc') or 'gcc', '-std=gnu11', '-O1', '-pthread', '-DWIN32_LEAN_AND_MEAN', '-D_LANGUAGE_C=1',
                '-DGEVR_LIVESPLIT_TEST_PORT=%d' % mock.port, '-I' + str(temp), '-Iport/include', '-Iinclude',
                'port/tests/livesplit_native.c', 'port/src/gevr_livesplit.c', '-o', str(exe)]
        if sys.platform == 'win32':
            args.append('-lws2_32')
        subprocess.run(args, cwd=root, check=True)
        out = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True, timeout=60)
    print(out.stdout, end='')
    assert out.returncode == 0 and 'done' in out.stdout, out.stderr

    def check(ok, what):
        if not ok:
            print('\n'.join(mock.log))
            raise SystemExit('FAIL: ' + what)

    commands = [line for line in mock.log if not line.startswith('setgametime')]
    expected = ['alwayspausegametime',
                'getcurrenttimerphase', 'starttimer', 'split',                                # mission 1
                'getcurrenttimerphase', 'getcurrentgametime', 'getsplitindex',               # mission 2 fails
                'getcurrenttimerphase', 'getcurrentgametime', 'split',                       # mission 2 completes
                'getcurrenttimerphase', 'reset', 'starttimer', 'getsplitindex', 'reset']     # a new run, lost
    check(commands == expected, 'the commands, in order: %s' % commands)
    # game time: each mission's clock on the run's total, a failed try kept
    times = [line.split(' ', 1)[1] for line in mock.log if line.startswith('setgametime')]
    check(all(re.fullmatch(r'\d+:\d\d:\d\d\.\d\d', t) for t in times), 'H:MM:SS.ff as TimeSpanParser reads it')
    at = lambda command, nth=0: [i for i, line in enumerate(mock.log) if line == command][nth]
    check(mock.log[at('split') - 1] == 'setgametime 0:00:01.16', 'mission 1 splits at the debrief\'s 70 sixtieths')
    fail = at('getsplitindex')
    check(mock.log[fail - 1] == 'setgametime 0:00:01.66', 'the failed try ends at 1.16 + 0.50')
    check(mock.log[at('split', 1) - 1] == 'setgametime 0:00:02.41', 'mission 2 splits on the total: 1.66 + 0.75')
    streamed = [t for t in times[:times.index('0:00:01.16')]]
    check(len(streamed) >= 5, 'the clock streams while a mission runs (%d sets)' % len(streamed))
    check(mock.phase == 'NotRunning', 'the new run lost on its first mission is reset')
    print('PASS: LiveSplit client: start, game time, splits, a failed try kept in the total, '
          'a finished run restarted, a first-mission loss reset (%d commands)' % len(mock.log))


main()
