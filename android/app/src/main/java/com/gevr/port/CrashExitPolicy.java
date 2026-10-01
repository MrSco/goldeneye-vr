package com.gevr.port;

/** Android exit reason values; kept free of framework calls for regression checks. */
final class CrashExitPolicy {
    static boolean isCrash(int reason, int status) {
        // Java crash, native crash, ANR, or a fatal native signal. SIGKILL
        // (force stop, restart, memory pressure) is deliberately excluded.
        return reason == 4 || reason == 5 || reason == 6
                || (reason == 2 && (status == 4 || status == 6 || status == 7 || status == 8 || status == 11));
    }
}
