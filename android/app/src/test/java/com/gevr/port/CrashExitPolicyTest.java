package com.gevr.port;

import org.junit.Test;
import static org.junit.Assert.*;

public class CrashExitPolicyTest {
    @Test public void detectsCrashesAndQuestSignalExits() {
        assertTrue(CrashExitPolicy.isCrash(4, 0));
        assertTrue(CrashExitPolicy.isCrash(5, 0));
        assertTrue(CrashExitPolicy.isCrash(6, 0));
        for (int signal : new int[]{4,6,7,8,11}) assertTrue(CrashExitPolicy.isCrash(2, signal));
    }
    @Test public void excludesNormalRestartsAndSystemKills() {
        assertFalse(CrashExitPolicy.isCrash(2, 9));
        for (int reason : new int[]{0,1,3,7,8,10,11,12,13,14,15,16}) assertFalse(CrashExitPolicy.isCrash(reason, 0));
    }

    private static final long INSTALL = 1_790_000_000_000L;

    @Test public void crashBeforeTheInstallIsNotOffered() {
        // Report 5103ca8b: a v0.1.x SIGSEGV surfaced on the first v0.3.7 launch.
        assertFalse(CrashExitPolicy.qualifies(2, 11, INSTALL - 5L * 86_400_000, INSTALL, 0));
        assertFalse(CrashExitPolicy.qualifies(5, 0, INSTALL, INSTALL, 0));
    }
    @Test public void crashAfterTheInstallIsOfferedOnce() {
        long crashAt = INSTALL + 60_000;
        assertTrue(CrashExitPolicy.qualifies(2, 11, crashAt, INSTALL, 0));
        assertTrue(CrashExitPolicy.qualifies(5, 0, crashAt, INSTALL, crashAt - 1));
        assertFalse(CrashExitPolicy.qualifies(5, 0, crashAt, INSTALL, crashAt));
        assertFalse(CrashExitPolicy.qualifies(13, 0, crashAt, INSTALL, 0));
    }
    @Test public void foregroundMarkerFromBeforeTheInstallIsTheInstallKill() {
        assertFalse(CrashExitPolicy.unexpectedExit(INSTALL - 1, INSTALL, 0, 0));
        assertFalse(CrashExitPolicy.unexpectedExit(0, INSTALL, 0, 0));
        assertTrue(CrashExitPolicy.unexpectedExit(INSTALL + 1, INSTALL, 0, 0));
        assertFalse(CrashExitPolicy.unexpectedExit(INSTALL + 1, INSTALL, INSTALL + 1, 0));
    }
    @Test public void foregroundMarkerWithAnExitRecordIsNotUnexpected() {
        // Report 8bb89f82: resumed at 09:05:11, user-requested force stop at 09:05:12.
        long markerAt = INSTALL + 432_000;
        assertFalse(CrashExitPolicy.unexpectedExit(markerAt, INSTALL, 0, markerAt + 1_000));
        assertFalse(CrashExitPolicy.unexpectedExit(markerAt, INSTALL, 0, markerAt));
        // Only an earlier run's record: this run's own record is missing.
        assertTrue(CrashExitPolicy.unexpectedExit(markerAt, INSTALL, 0, markerAt - 60_000));
    }
    @Test public void foregroundMarkerFromBeforeBootIsNotUnexpected() {
        // Report 73a899cd: run at 23:42:58Z, container rebooted at 23:46:43Z, no exit records.
        long installAt = 1791330045000L;
        long offeredAt = 1791330049000L;
        long previousRun = 1791330178000L;
        long bootAt = 1791330403000L;
        assertFalse(CrashExitPolicy.unexpectedExit(previousRun, installAt, offeredAt, 0, bootAt));
    }
    @Test public void foregroundMarkerAfterBootIsUnexpected() {
        long bootAt = INSTALL + 10_000;
        long markerAt = INSTALL + 60_000;
        assertTrue(CrashExitPolicy.unexpectedExit(markerAt, INSTALL, 0, 0, bootAt));
        assertFalse(CrashExitPolicy.unexpectedExit(markerAt, INSTALL, 0, 0, markerAt + 1_000));
    }
    @Test public void preferNativeCrashWithTombstoneOverSignalNotification() {
        // Report 05ffc73d: crash-native(5) at 00:11:09Z followed by signaled(2) at 00:11:10Z.
        long tNative = 1791331869000L;
        long tSignal = 1791331870000L;
        assertTrue(CrashExitPolicy.isBetterCrash(5, tNative, 2, tSignal));
        assertFalse(CrashExitPolicy.isBetterCrash(2, tSignal, 5, tNative));
    }
    @Test public void preferNewerCrashWhenSeparatedByMoreThanFiveSeconds() {
        long tOld = INSTALL + 10_000;
        long tNew = INSTALL + 60_000;
        assertTrue(CrashExitPolicy.isBetterCrash(2, tNew, 5, tOld));
        assertFalse(CrashExitPolicy.isBetterCrash(5, tOld, 2, tNew));
    }
    @Test public void describesRecordsReadably() {
        assertEquals("2026-09-26T14:47:41Z signaled(2) status=11 [before install]",
                CrashExitPolicy.describe(1790434061432L, 2, 11, null, 1790434061432L + 1));
        assertEquals("2026-09-26T14:47:41Z crash-native(5) status=0 crash",
                CrashExitPolicy.describe(1790434061432L, 5, 0, "crash", INSTALL - 1));
        assertEquals("other", CrashExitPolicy.reasonName(13));
        assertEquals("package-updated", CrashExitPolicy.reasonName(16));
        assertEquals("reason99", CrashExitPolicy.reasonName(99));
    }
}
