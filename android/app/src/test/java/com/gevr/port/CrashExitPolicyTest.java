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
        for (int reason : new int[]{0,1,3,7,8,10,11,12,13,14}) assertFalse(CrashExitPolicy.isCrash(reason, 0));
    }
}
