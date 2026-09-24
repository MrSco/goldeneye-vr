package com.gevr.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;

/**
 * Restarts the game into the in-VR launcher (issue #16, the menu-button hold;
 * MainActivity.restartToLauncher). The pattern of the ProcessPhoenix library:
 * this activity runs in its own process, so it outlives the game process it
 * kills, then starts the app afresh from the foreground, where Android allows
 * an activity start. An alarm cannot do that on API 34: by the time it fires
 * the app is dead and in the background. It never draws, and finishes at once.
 */
public class RelaunchActivity extends Activity {
    static final String EXTRA_PID = "com.gevr.port.RELAUNCH_PID";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        if (pid > 0) {
            Process.killProcess(pid);
        }
        Intent again = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (again == null) {
            again = new Intent(this, MainActivity.class);
        }
        again.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        Log.i("GEVR", "Relaunch: starting the game again");
        startActivity(again);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
