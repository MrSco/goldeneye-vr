package com.gevr.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;

/**
 * Restarts the game into the in-VR launcher (issue #16, the menu-button hold;
 * MainActivity.restartToLauncher). The pattern of the ProcessPhoenix library:
 * this activity runs in its own process, so it outlives the game process it
 * kills, then starts the app afresh from the foreground, where Android allows
 * an activity start. An alarm cannot do that on API 34: by the time it fires
 * the app is dead and in the background. It never draws.
 *
 * It starts the game again only once the old process has gone and the VR
 * runtime has had a moment to drop its session. Started at once, the new
 * process came up before the old one died (Horizon OS of 2026-11-17): it made
 * an XR session but never got the headset's focus, and sat there until the
 * player quit it from the Quest menu (owner, 2026-11-23, twice).
 */
public class RelaunchActivity extends Activity {
    static final String EXTRA_PID = "com.gevr.port.RELAUNCH_PID";
    private static final String TAG = "GEVR";
    private static final long STEP_MS = 50;      // how often it looks for the old process
    private static final long SETTLE_MS = 500;   // after it has gone: the runtime drops its session
    private static final long MOST_MS = 3000;    // never longer than this in all

    private final Handler handler = new Handler(Looper.getMainLooper());
    private int pid;
    private long startedAt, goneAt = -1;
    private boolean started;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        pid = getIntent().getIntExtra(EXTRA_PID, -1);
        startedAt = SystemClock.uptimeMillis();
        if (pid > 0) {
            Process.killProcess(pid);
        }
        handler.post(this::step);
    }

    // The same app's processes show in /proc; once the old one's entry is gone it has exited.
    private void step() {
        if (started) return;
        final long now = SystemClock.uptimeMillis();
        if (goneAt < 0 && (pid <= 0 || !new File("/proc/" + pid).exists())) {
            goneAt = now;
            Log.i(TAG, "Relaunch: the game's process ended after " + (now - startedAt) + " ms");
        }
        if ((goneAt >= 0 && now - goneAt >= SETTLE_MS) || now - startedAt >= MOST_MS) {
            if (goneAt < 0) Log.w(TAG, "Relaunch: the game's process is still there after " + MOST_MS + " ms; starting anyway");
            startAgain();
            return;
        }
        handler.postDelayed(this::step, STEP_MS);
    }

    private void startAgain() {
        started = true;
        Intent again = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (again == null) {
            again = new Intent(this, MainActivity.class);
        }
        again.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        Log.i(TAG, "Relaunch: starting the game again");
        startActivity(again);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
