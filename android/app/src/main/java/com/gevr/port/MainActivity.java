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
        try {
            System.loadLibrary("openxr_loader");
        } catch (UnsatisfiedLinkError e) {
            // the phone/tablet APK (the "mobile" flavor) ships no OpenXR loader
        }
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

        updater = new UpdateChecker(this);
        String version = "0";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            Log.w(TAG, "no versionName", e);
        }
        mods = new ModManager(this, version);
        lobbies = new LobbyClient();
        friends = new FriendsClient(new File(dataDir, "gevr_player.json"));
        takeJoinLink(getIntent());
        // Discord's Social SDK (port/vr/vr_discord.cpp), when this build has it: its sign-in
        // and connection run against this activity.
        try {
            Class.forName("com.discord.socialsdk.DiscordSocialSdkInit")
                    .getMethod("setEngineActivity", android.app.Activity.class).invoke(null, this);
            Log.i(TAG, "Discord Social SDK ready");
        } catch (ClassNotFoundException e) {
            // a build without the SDK (no build/discord/discord_partner_sdk.aar)
        } catch (Exception e) {
            Log.w(TAG, "Discord Social SDK did not start", e);
        }
        reporter = new LogReporter(this);

        Log.i(TAG, "Starting GEVR VR mode");
        initializeGame();
    }

    // --- Update check (GitHub releases), driven by the in-VR launcher ---
    static final int REQUEST_INSTALL_PERMISSION = 0x6f;
    static final int REQUEST_INSTALLER = 0x70;
    private UpdateChecker updater;

    /** Tab-separated state for port/vr/vr_launcher.cpp; see UpdateChecker.status. */
    public String updaterStatus() {
        return updater != null ? updater.status() : "";
    }

    /** "check", "update", "testbuilds:0" / "testbuilds:1". */
    public void updaterCommand(String cmd) {
        if (updater != null) updater.command(cmd);
    }

    // --- Mods page (texture packs from their authors' sites), driven by the in-VR launcher ---
    private ModManager mods;
    private LobbyClient lobbies;
    private FriendsClient friends;
    private LogReporter reporter;

    public String reportStatus() { return reporter != null ? reporter.status() : "idle"; }
    public void reportCommand(String cmd) { if (reporter != null) reporter.command(cmd); }

    public String lobbyEvent() { return lobbies != null ? lobbies.event() : ""; }
    public void lobbyCommand(String cmd) { if (lobbies != null) lobbies.command(cmd); }

    // Friends, presence and invites (port/vr/vr_friends.cpp; see FriendsClient).
    /**
     * A goldeneyevr://join/CODE link (the goldeneyevr.com/join page's "Open in GoldenEye VR"):
     * kept in data/gevr_pending_join.txt, which the launcher reads and joins (vr_friends.cpp
     * gevrFriendsLoadPendingJoin), the way an invite accepted in a mission is.
     */
    private void takeJoinLink(Intent intent) {
        Uri data = intent == null ? null : intent.getData();
        if (data == null || !"goldeneyevr".equals(data.getScheme()) || !"join".equals(data.getHost())) return;
        String code = data.getLastPathSegment();
        if (code == null) return;
        code = code.trim().toUpperCase(java.util.Locale.ROOT);
        if (!code.matches("[A-Z2-9]{8}")) return;
        File file = new File(new File(getExternalFilesDir(null), "data"), "gevr_pending_join.txt");
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
            out.write((code + " " + (System.currentTimeMillis() / 1000) + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
            Log.i(TAG, "Join link for lobby " + code);
        } catch (java.io.IOException e) {
            Log.w(TAG, "Could not keep the join link", e);
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        takeJoinLink(intent);   // the app was already running: the launcher picks it up within a second
    }

    public String friendsEvent() { return friends != null ? friends.event() : ""; }
    public void friendsCommand(String cmd) { if (friends != null) friends.command(cmd); }

    /** One tab-separated line per pack for port/vr/vr_launcher.cpp; see ModManager.status. */
    public String modsStatus() {
        return mods != null ? mods.status() : "";
    }

    /** "check", "install:<id>", "remove:<id>", "cancel"; see ModManager.command. */
    public void modsCommand(String cmd) {
        if (mods != null) mods.command(cmd);
    }

    /**
     * Back to the in-VR launcher from a level (issue #16, the menu-button hold).
     * The launcher runs before the ROM loads and the game's state cannot be
     * reset in place, so the process ends and the system starts the app again
     * a moment later, with the very intent the Library uses.
     */
    public void restartToLauncher() {
        if (reporter != null) reporter.foreground(false);
        if (lobbies != null) lobbies.stopAndWait(
                android.os.Looper.myLooper() == android.os.Looper.getMainLooper() ? 1000 : 2000);
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

    public void quitApp() {
        if (reporter != null) reporter.foreground(false);
        if (lobbies != null) lobbies.stopAndWait(
                android.os.Looper.myLooper() == android.os.Looper.getMainLooper() ? 1000 : 2000);
        if (friends != null) friends.shutdown();
        runOnUiThread(() -> {
            Log.i(TAG, "Quitting app from launcher");
            finishAndRemoveTask();
            android.os.Process.killProcess(android.os.Process.myPid());
        });
    }

    // --- ROM file picker (called from the in-VR launcher, port/vr/vr_launcher.cpp) ---
    private static final int REQUEST_PICK_ROM = 0x6e;
    private static final int REQUEST_PICK_XBLA = 0x71;
    private static final int REQUEST_PICK_CE = 0x72;

    /** What happened to the last pick, for the launcher to show; it reads and clears it. */
    public static volatile String pickResult = null;

    /** Where the next pick is copied in data/: the launcher checks and adopts it. */
    private volatile String pickTarget = "picked.z64";

    /**
     * Opens the system file picker; one chosen file is copied to data/picked.z64,
     * several to data/picked-import/. Either may be a .zip of ROMs: the launcher
     * sorts them by header (port/src/gevr_romimport.c).
     */
    public void openRomPicker() {
        pickTarget = "picked.z64";
        openDocument(REQUEST_PICK_ROM, true);
    }

    /** The Mods page: the player's GoldenEye X ROM, copied to data/picked-gex.z64. */
    public void openGexPicker() {
        openPicker("picked-gex.z64");
    }

    /** Goldfinger 64, copied to data/picked-goldfinger.z64 and kept beside ge.z64. */
    public void openGoldfingerPicker() {
        openPicker("picked-goldfinger.z64");
    }

    /** Tomorrow Never Dies 64, copied to data/picked-tnd.z64 and kept beside ge.z64. */
    public void openTndPicker() {
        openPicker("picked-tnd.z64");
    }

    /** Wreck's Mirror Mode, kept beside the original cartridge as data/mirror.z64. */
    public void openMirrorPicker() {
        openPicker("picked-mirror.z64");
    }

    /** GoldenEye with Mario Characters V3.17, kept beside the original cartridge as data/mario.z64. */
    public void openMarioPicker() {
        openPicker("picked-mario.z64");
    }

    private void openPicker(String target) {
        pickTarget = target;
        openDocument(REQUEST_PICK_ROM);
    }

    /**
     * The Mods page's XBLA / Bean: the player's own copy of GoldenEye XBLA (its
     * .7z, a .zip, or the Xbox 360 package). Nothing is copied whole: ModManager
     * reads it in place and keeps only the files Bean draws.
     */
    public void openXblaPicker() {
        openDocument(REQUEST_PICK_XBLA);
    }

    /**
     * The Mods page's CE...: the GoldenEye XBLA Community Edition updater zip,
     * applied to the imported Bean copy (ModManager.applyCe).
     */
    public void openCePicker() {
        openDocument(REQUEST_PICK_CE);
    }

    private void openDocument(int request) {
        openDocument(request, false);
    }

    private void openDocument(int request, boolean several) {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            if (several) intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            try {
                startActivityForResult(intent, request);
            } catch (Exception e) {
                Log.e(TAG, "No file picker available", e);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        // The updater's 2D panels: "install unknown apps" settings, or the
        // installer activity when it was closed without updating.
        if (requestCode == REQUEST_INSTALL_PERMISSION) {
            // the updater decides: on Quest this result comes while the page is still up
            if (updater != null) updater.onPermissionPageClosed();
            else comeBackToVr();
            return;
        }
        if (requestCode == REQUEST_INSTALLER) {
            comeBackToVr();
            if (updater != null) updater.onInstallerClosed(resultCode == RESULT_OK);
            return;
        }
        if (requestCode == REQUEST_PICK_XBLA) {
            // the Mods page shows the import's progress; no pick, nothing to do
            if (resultCode == RESULT_OK && data != null && data.getData() != null && mods != null) {
                mods.importOwn("ge007-bean", data.getData());
            }
            comeBackToVr();
            return;
        }
        if (requestCode == REQUEST_PICK_CE) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null && mods != null) {
                mods.applyCe(data.getData());
            }
            comeBackToVr();
            return;
        }
        if (requestCode != REQUEST_PICK_ROM) {
            return;
        }
        final java.util.List<Uri> uris = new java.util.ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            android.content.ClipData clip = data.getClipData();
            if (clip != null) {
                for (int i = 0; i < clip.getItemCount(); i++) {
                    if (clip.getItemAt(i).getUri() != null) uris.add(clip.getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
        }
        if (!uris.isEmpty() && "picked.z64".equals(pickTarget)) {
            new Thread(() -> takeMainPick(uris), "rom-copy").start();
        } else if (!uris.isEmpty()) {
            final Uri uri = uris.get(0);
            new Thread(() -> copyPickedRom(uri), "rom-copy").start();
        } else {
            pickResult = "No file chosen.";
        }
        comeBackToVr();
    }

    private int returnToVrAttempts;
    private boolean hasWindowFocusNow;
    // One handler and one runnable, so a new request or a cancel replaces any
    // retry still queued rather than running alongside it.
    private final android.os.Handler returnToVrHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable returnToVrStep = this::returnToVrUntilFocused;

    /**
     * After a 2D panel (file picker, updater prompts) closes. Not straight
     * away: while the panel is still closing, Quest's shell takes focus back
     * after our relaunch. Retry until we have it. UI thread.
     */
    void comeBackToVr() {
        returnToVrHandler.removeCallbacks(returnToVrStep);
        returnToVrAttempts = 0;
        returnToVrHandler.postDelayed(returnToVrStep, 700);
    }

    /** Another 2D panel is about to open: a pending retry would cover it. UI thread. */
    void cancelComeBackToVr() {
        returnToVrHandler.removeCallbacks(returnToVrStep);
    }

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
        returnToVrHandler.postDelayed(returnToVrStep, 1500);
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

    // What a picker copies whole (the hack pickers; a document that cannot seek).
    private static final long PICK_MAX = 256L << 20;

    private static native void nativeRomImportTakeBegin(String dataDir);
    private static native String nativeRomImportTake(int fd, String label, String dataDir);
    private static native int nativeRomImportTakeEnd(String dataDir);

    /**
     * The Play page's picker (port/src/gevr_romimport.c gevrRomImportTake):
     * each file chosen is read where it is. ROMs, loose or in a zip (a zip of
     * everything, a zip in it), are taken into data/picked-import/ for the
     * launcher to sort; the GoldenEye XBLA copy goes to the Mods page's Bean
     * import and the Community Edition updater to its apply, after that import
     * when both were chosen. Off the UI thread: it reads every file.
     */
    private void takeMainPick(java.util.List<Uri> uris) {
        final File dir = new File(getExternalFilesDir(null), "data");
        final String data = dir.getAbsolutePath();
        dir.mkdirs();
        nativeRomImportTakeBegin(data);
        Uri xbla = null, ce = null;
        for (int i = 0; i < uris.size(); i++) {
            final Uri u = uris.get(i);
            final String label = pickName(u, i);
            String r = null;
            try {
                android.os.ParcelFileDescriptor fd = getContentResolver().openFileDescriptor(u, "r");
                if (fd != null && fd.getStatSize() >= 0) r = nativeRomImportTake(fd.detachFd(), label, data);
                else if (fd != null) fd.close();
            } catch (Exception e) {
                Log.w(TAG, "Could not read a picked file", e);
            }
            if (r == null || r.isEmpty()) {
                // a stream only: copied whole for the sort to read (ROMs and zips of them)
                File to = new File(new File(dir, "picked-import.part"), label);
                try {
                    copyUri(u, to);
                } catch (Exception e) {
                    Log.e(TAG, "ROM copy failed", e);
                    to.delete();
                }
                continue;
            }
            final String[] f = r.split("\t", -1);
            if (f.length < 4) continue;
            if ("1".equals(f[0]) && xbla == null) xbla = u;
            if (ce == null && !f[2].isEmpty()) ce = Uri.fromFile(new File(f[2]));
            else if (ce == null && "1".equals(f[1])) ce = u;
        }
        final int staged = nativeRomImportTakeEnd(data);
        // their progress is on the Play page beside the ROM (vr_launcher.cpp gevrBeanImportLine)
        if (xbla != null && mods != null) mods.importOwn("ge007-bean", xbla);
        String note = null;
        if (ce != null && (mods == null || !mods.applyCeWhenReady(ce))) {
            note = "The Community Edition patches Bean: choose your GoldenEye XBLA copy too.";
        }
        if (staged == 0) {   // nothing for the ROM check: the message ends the pick
            pickResult = note != null ? note
                : xbla != null ? "Importing Bean from your GoldenEye XBLA copy."
                : ce != null ? "Applying the Community Edition."
                : "Nothing this knows in that: choose ROMs, a .zip of them or your GoldenEye XBLA copy.";
        }
        Log.i(TAG, "Main pick: " + uris.size() + " file(s), " + staged + " for the sort"
                + (xbla != null ? ", the XBLA copy" : "") + (ce != null ? ", the Community Edition" : ""));
    }

    private long copyUri(Uri uri, File to) throws java.io.IOException {
        long total = 0;
        try (java.io.InputStream in = getContentResolver().openInputStream(uri);
             java.io.OutputStream out = new java.io.FileOutputStream(to)) {
            if (in == null) throw new java.io.IOException("cannot open");
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
                if (total > PICK_MAX) throw new java.io.IOException("file too large");
            }
        }
        return total;
    }

    /** The picked document's name, for the launcher's "Not recognised: ..." line. */
    private String pickName(Uri uri, int index) {
        String name = null;
        try (android.database.Cursor c = getContentResolver().query(uri,
                new String[] { android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception e) {
            // no name: the index alone
        }
        String safe = name == null ? "" : name.replaceAll("[^A-Za-z0-9 ._()+-]", "_");
        if (safe.endsWith(".part")) safe += "_";
        if (safe.length() > 80) safe = safe.substring(safe.length() - 80);
        return index + "-" + (safe.isEmpty() ? "file" : safe);
    }

    // Copies through a .part file so the launcher never sees half a ROM; it
    // checks the header and adopts or rejects the file.
    private void copyPickedRom(Uri uri) {
        final String target = pickTarget;
        File dir = new File(getExternalFilesDir(null), "data");
        dir.mkdirs();
        File part = new File(dir, target + ".part");
        long total = 0;
        try {
            total = copyUri(uri, part);
        } catch (Exception e) {
            Log.e(TAG, "ROM copy failed", e);
            part.delete();
            pickResult = "Could not copy that file: " + e.getMessage();
            return;
        }
        if (!part.renameTo(new File(dir, target))) {
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
        if (reporter != null) reporter.foreground(true);
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
        if (reporter != null) reporter.foreground(false);
        /* SDLActivity skips pauseNativeThread on API>=24 (multi-window).
         * Our XR pump never waits on Android_PauseSem, so clear/pause SDL
         * audio here or music keeps playing on the Quest home screen. */
        nativeAudioPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "MainActivity onDestroy");
        if (reporter != null) reporter.foreground(false);
        if (lobbies != null) lobbies.shutdown();
        if (friends != null) friends.shutdown();
        nativeAudioPause();
        nativeDestroy();
        // Not super.onDestroy(): SDLActivity's waits on the game thread, which
        // never returns on Android (SDL_QUIT is ignored there), so a quit held
        // this thread until the system's ten-second destroy timeout killed the
        // process. A launch from the Library in those seconds waited on the
        // stuck process, and its input timeout became an ANR blamed on the next
        // one (report 37093815). SIGKILL is not a crash to CrashExitPolicy, and
        // the foreground marker was cleared above.
        Log.i(TAG, "MainActivity onDestroy: ending the process");
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    private static final int VOICE_PERMISSION_REQUEST = 4137;

    public void requestVoicePermission(String ignored) {
        runOnUiThread(() -> {
            if (android.os.Build.VERSION.SDK_INT < 23 ||
                checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                nativeVoicePermissionResult(true);
            } else {
                requestPermissions(new String[]{android.Manifest.permission.RECORD_AUDIO}, VOICE_PERMISSION_REQUEST);
            }
        });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == VOICE_PERMISSION_REQUEST) {
            nativeVoicePermissionResult(grantResults.length > 0 &&
                grantResults[0] == android.content.pm.PackageManager.PERMISSION_GRANTED);
        }
    }

    // Native methods
    public native void nativeInit(String dataPath);
    private static native void nativeVrResume();
    public native void nativeDestroy();
    public native void nativeAudioPause();
    public native void nativeAudioResume();
    public native void nativeVoicePermissionResult(boolean granted);
}
