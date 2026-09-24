package com.gevr.port;

import org.libsdl.app.SDLActivity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.util.Log;
import java.io.File;
import android.net.Uri;
import androidx.core.content.FileProvider;

public class MainActivity extends SDLActivity {
    private static final String TAG = "GEVR";

    private static native void nativeSetVrJavaContext(android.app.Activity activity, android.view.Surface surface);

    static {
        System.loadLibrary("openxr_loader");
        System.loadLibrary("SDL2");
        try {
            System.loadLibrary("gevr");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load the native game library (gevr)", e);
        }
    }

    /**
     * The native libraries SDL loads, and the last of which it treats as the
     * app's main shared object.
     *
     * SDLActivity's default list ends in "pd" - the Perfect Dark port this
     * harness came from. Leaving it there makes SDL dlopen a libpd.so that does
     * not exist, set mBrokenLibraries, and never start its main thread, which
     * surfaces only as "SDL Surface timeout after 5000ms" five seconds later.
     */
    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL2",
                "gevr"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.i(TAG, "MainActivity onCreate");

        // The app never leaves VR, even without a ROM: the in-VR launcher
        // (port/vr/vr_launcher.cpp) shows where to copy one and picks it up.
        // A 2D activity here stranded Quest's shell in the loading space. Make
        // the folder now so it is there to copy into over USB.
        File dataDir = new File(getExternalFilesDir(null), "data");
        if (!dataDir.isDirectory() && !dataDir.mkdirs()) {
            Log.w(TAG, "Could not create " + dataDir);
        }

        Log.i(TAG, "Starting GEVR VR mode");
        initializeGame();
    }

    /**
     * Back to the in-VR launcher from a level (issue #16, the menu-button hold).
     * The launcher runs before the ROM loads and the game's state cannot be
     * reset in place, so the process ends and the system starts the app again
     * a moment later, with the very intent the Library uses.
     */
    public void restartToLauncher() {
        runOnUiThread(() -> {
            // RelaunchActivity (its own process) kills this one and starts the
            // app again. An exit here hung on native threads until Android's
            // destroy timeout, and an alarm-started relaunch is refused once the
            // app is in the background (API 34): Quest sat in its loading space.
            Log.i(TAG, "Restarting into the launcher");
            Intent phoenix = new Intent(this, RelaunchActivity.class);
            phoenix.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            phoenix.putExtra(RelaunchActivity.EXTRA_PID, android.os.Process.myPid());
            startActivity(phoenix);
        });
    }

    // --- ROM file picker (called from the in-VR launcher, port/vr/vr_launcher.cpp) ---
    private static final int REQUEST_PICK_ROM = 0x6e;

    /** What happened to the last pick, for the launcher to show; it reads and clears it. */
    public static volatile String pickResult = null;

    /** Opens the system file picker; the chosen file is copied to data/picked.z64. */
    public void openRomPicker() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            try {
                startActivityForResult(intent, REQUEST_PICK_ROM);
            } catch (Exception e) {
                Log.e(TAG, "No file picker available", e);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_ROM) {
            return;
        }
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            final Uri uri = data.getData();
            new Thread(() -> copyPickedRom(uri), "rom-copy").start();
        } else {
            pickResult = "No file chosen.";
        }
        // Not straight away: while the picker panel is still closing, Quest's
        // shell takes focus back after our relaunch. Retry until we have it.
        returnToVrAttempts = 0;
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(this::returnToVrUntilFocused, 700);
    }

    private int returnToVrAttempts;
    private boolean hasWindowFocusNow;

    private void returnToVrUntilFocused() {
        if (hasWindowFocusNow && returnToVrAttempts > 0) {
            return;
        }
        if (returnToVrAttempts++ >= 4) {
            Log.w(TAG, "Could not get back to VR after the file picker");
            return;
        }
        Log.i(TAG, "Returning to VR after the file picker (attempt " + returnToVrAttempts + ")");
        returnToVr();
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(this::returnToVrUntilFocused, 1500);
    }

    // The picker is a 2D panel: when it closes, Android resumes this activity but
    // Quest's shell leaves the player in its own UI with the XR session visible
    // and unfocused, until the app is started again from the Library. Start it
    // again ourselves, with the very intent the Library uses (singleTask: the
    // running instance comes back to the front).
    private void returnToVr() {
        try {
            Intent back = getPackageManager().getLaunchIntentForPackage(getPackageName());
            if (back == null) {
                back = new Intent(this, MainActivity.class);
            }
            back.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
            startActivity(back);
        } catch (Exception e) {
            Log.e(TAG, "Could not bring the game back to the front", e);
        }
    }

    // Copies through a .part file so the launcher never sees half a ROM; it
    // checks the header and adopts or rejects the file.
    private void copyPickedRom(Uri uri) {
        File dir = new File(getExternalFilesDir(null), "data");
        dir.mkdirs();
        File part = new File(dir, "picked.z64.part");
        long total = 0;
        try (java.io.InputStream in = getContentResolver().openInputStream(uri);
             java.io.OutputStream out = new java.io.FileOutputStream(part)) {
            if (in == null) throw new java.io.IOException("cannot open");
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
                if (total > (64L << 20)) throw new java.io.IOException("file too large");
            }
        } catch (Exception e) {
            Log.e(TAG, "ROM copy failed", e);
            part.delete();
            pickResult = "Could not copy that file: " + e.getMessage();
            return;
        }
        if (!part.renameTo(new File(dir, "picked.z64"))) {
            Log.e(TAG, "ROM copy: rename failed");
            part.delete();
            pickResult = "Could not copy that file.";
            return;
        }
        Log.i(TAG, "ROM copied from picker (" + total + " bytes)");
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        hasWindowFocusNow = hasFocus;
        Log.i(TAG, "onWindowFocusChanged: " + hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().post(this::hideSystemUI);
        }
    }

    private void hideSystemUI() {
        View decorView = getWindow().getDecorView();
        int uiOptions = View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        decorView.setSystemUiVisibility(uiOptions);
    }

    private void initializeGame() {
        Log.i(TAG, "initializeGame start");

        File dataDir = new File(getExternalFilesDir(null), "data");
        Log.i(TAG, "Data dir: " + dataDir.getAbsolutePath());

        if (!dataDir.exists()) {
            dataDir.mkdirs();
        }

        Log.i(TAG, "Calling nativeInit");
        nativeInit(dataDir.getAbsolutePath());
        Log.i(TAG, "initializeGame complete");
    }

    @Override
    protected void onResume() {
        Log.i(TAG, "MainActivity onResume - VR mode");
        SDLActivity.mHasFocus = true;
        super.onResume();
        nativeAudioResume();

        new Thread(() -> {
            final int MAX_ATTEMPTS = 50;
            final int DELAY_MS = 100;
            for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
                try {
                    Thread.sleep(DELAY_MS);
                    android.view.Surface surface = org.libsdl.app.SDLActivity.getNativeSurface();
                    if (surface != null && surface.isValid()) {
                        Log.i(TAG, "SDL Surface found after " + (attempt * DELAY_MS) + "ms");
                        // Called directly from this background thread
                        // xrInitializeLoaderKHR is thread-safe and must NOT block the UI thread
                        nativeSetVrJavaContext(MainActivity.this, surface);
                        return;
                    }
                } catch (InterruptedException e) {
                    Log.e(TAG, "Surface polling interrupted", e);
                    break;
                }
            }
            Log.e(TAG, "SDL Surface timeout after " + (MAX_ATTEMPTS * DELAY_MS) + "ms");
        }, "SurfacePollingThread").start();
    }

    @Override
    protected void onPause() {
        Log.i(TAG, "MainActivity onPause - stop audio immediately");
        /* SDLActivity skips pauseNativeThread on API>=24 (multi-window).
         * Our XR pump never waits on Android_PauseSem, so clear/pause SDL
         * audio here or music keeps playing on the Quest home screen. */
        nativeAudioPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "MainActivity onDestroy");
        nativeAudioPause();
        nativeDestroy();
        super.onDestroy();
    }

    // Native methods
    public native void nativeInit(String dataPath);
    private static native void nativeVrResume();
    public native void nativeDestroy();
    public native void nativeAudioPause();
    public native void nativeAudioResume();
}
