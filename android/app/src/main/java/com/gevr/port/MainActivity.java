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

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
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
