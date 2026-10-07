package com.gevr.port;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Base64;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;
import java.util.zip.GZIPOutputStream;

/** Collects one explicitly approved report off the VR render thread. */
final class LogReporter {
    private static final String TAG = "GEVR-Report";
    private static final String ENDPOINT = "https://lobbies.goldeneyevr.com/v1/reports";
    private static final int MAX_LOG = 2_500_000;
    private static final int MAX_TRACE = 1_000_000;
    private static final Pattern ADDRESS = Pattern.compile("(?<![0-9.])(?!(?:127)\\.)\\d{1,3}(?:\\.\\d{1,3}){3}(?![0-9.])");
    private static final Pattern SIGNAL = Pattern.compile("(?im)^(.*(?:lobbyCommand|launcher: lobbyCommand).*(?:offer|answer)\\|).*$");
    private final Context context;
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "log-reporter");
        thread.setDaemon(true);
        return thread;
    });
    private final SharedPreferences prefs;
    private volatile String state = "idle";
    private long crashTimestamp;
    private ApplicationExitInfo crash;
    private boolean unexpectedExit;
    /** When this APK was installed or updated; exits before it belong to another build. */
    private long installTime;
    private long versionCode;
    /** The exit records as logged at startup, for the report's exit history section. */
    private final StringBuilder exitHistory = new StringBuilder();

    LogReporter(Context context) {
        this.context = context.getApplicationContext();
        prefs = this.context.getSharedPreferences("debug_reports", Context.MODE_PRIVATE);
        try {
            android.content.pm.PackageInfo info = this.context.getPackageManager()
                    .getPackageInfo(this.context.getPackageName(), 0);
            installTime = info.lastUpdateTime;
            versionCode = Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
        } catch (Exception e) { Log.w(TAG, "Could not read install time", e); }
        long offered = prefs.getLong("offered_crash", 0);
        long lastExit = 0;
        Log.i(TAG, "Install " + CrashExitPolicy.isoUtc(installTime) + " versionCode " + versionCode
                + (offered != 0 ? ", last offered crash " + CrashExitPolicy.isoUtc(offered) : ""));
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(null, 0, 32);
                for (ApplicationExitInfo exit : exits) {
                    String line = CrashExitPolicy.describe(exit.getTimestamp(), exit.getReason(),
                            exit.getStatus(), exit.getDescription(), installTime);
                    if (!this.context.getPackageName().equals(exit.getProcessName())) line += " process " + exit.getProcessName();
                    Log.i(TAG, "Previous exit: " + line);
                    exitHistory.append(line).append('\n');
                    if (this.context.getPackageName().equals(exit.getProcessName()))
                        lastExit = Math.max(lastExit, exit.getTimestamp());
                    if (this.context.getPackageName().equals(exit.getProcessName())
                            && CrashExitPolicy.qualifies(exit.getReason(), exit.getStatus(),
                                    exit.getTimestamp(), installTime, offered)) {
                        if (crash == null || CrashExitPolicy.isBetterCrash(exit.getReason(), exit.getTimestamp(),
                                crash.getReason(), crash.getTimestamp())) {
                            crash = exit;
                            crashTimestamp = exit.getTimestamp();
                        }
                    }
                }
                if (crash != null) state = "offer";
            } catch (Exception e) { Log.w(TAG, "Could not inspect previous exits", e); }
        }
        // Quest exit records can be missing. Persist only foreground runs and
        // ignore activity recreation within this same process. A marker older
        // than the install is the install itself killing the running app, and
        // one followed by an exit record already has its non-crash reason.
        // A marker from before the current boot belongs to a previous boot that
        // was terminated by shutdown/reboot (report 73a899cd), not a game crash.
        long previousRun = prefs.getLong("foreground_run", 0);
        long currentElapsed = android.os.SystemClock.elapsedRealtime();
        long bootAt = System.currentTimeMillis() - currentElapsed;
        long previousBoot = prefs.getLong("foreground_boot", 0);
        long previousElapsed = prefs.getLong("foreground_elapsed", 0);
        boolean rebooted = previousRun < bootAt
                || (previousBoot != 0 && Math.abs(bootAt - previousBoot) > 5000)
                || (previousElapsed != 0 && currentElapsed < previousElapsed);
        if (crash == null && !rebooted
                && CrashExitPolicy.unexpectedExit(previousRun, installTime, offered, lastExit, bootAt)
                && prefs.getInt("run_pid", 0) != android.os.Process.myPid()) {
            crashTimestamp = previousRun;
            unexpectedExit = true;
            state = "offer";
        }
        foreground(true);
    }

    void foreground(boolean active) {
        long now = System.currentTimeMillis();
        long elapsed = android.os.SystemClock.elapsedRealtime();
        prefs.edit().putLong("foreground_run", active ? now : 0)
                .putLong("foreground_boot", active ? (now - elapsed) : 0)
                .putLong("foreground_elapsed", active ? elapsed : 0)
                .putInt("run_pid", android.os.Process.myPid()).commit();
    }

    String status() { return state; }

    void command(String command) {
        if ("offered".equals(command)) {
            if (crashTimestamp != 0) prefs.edit().putLong("offered_crash", crashTimestamp).apply();
            if ("offer".equals(state)) state = "idle";
            return;
        }
        if (!command.startsWith("send|")) return;
        if ("sending".equals(state)) return;
        String[] parts = command.split("\\|", 4);
        if (parts.length != 4) { state = "error:Invalid report"; return; }
        final String note, player, build;
        try {
            note = redact(new String(Base64.decode(parts[1], Base64.URL_SAFE | Base64.NO_WRAP), StandardCharsets.UTF_8));
            player = redact(new String(Base64.decode(parts[2], Base64.URL_SAFE | Base64.NO_WRAP), StandardCharsets.UTF_8));
            build = parts[3];
        } catch (IllegalArgumentException e) { state = "error:Invalid report"; return; }
        if (note.length() > 500 || player.length() > 64 || build.length() > 64) {
            state = "error:Report details too long";
            return;
        }
        final boolean withCrash = crash != null || unexpectedExit;
        state = "sending";
        worker.execute(() -> send(withCrash, note, player, build));
    }

    private static byte[] readBounded(InputStream input, int max) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] chunk = new byte[8192];
        int n;
        while ((n = input.read(chunk)) != -1) {
            if (out.size() + n > max) throw new Exception("Report data exceeds limit");
            out.write(chunk, 0, n);
        }
        return out.toByteArray();
    }

    private static String tail(File file, int max) throws Exception {
        if (!file.isFile()) return "";
        long skip = Math.max(0, file.length() - max);
        try (FileInputStream in = new FileInputStream(file)) {
            while (skip > 0) skip -= in.skip(skip);
            return new String(readBounded(in, max), StandardCharsets.UTF_8);
        }
    }

    private String logcat() {
        Process process = null;
        try {
            process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "1500").start();
            byte[] data = readBounded(process.getInputStream(), 600_000);
            return new String(data, StandardCharsets.UTF_8);
        } catch (Exception e) {
            Log.w(TAG, "Logcat unavailable", e);
            return "";
        } finally {
            if (process != null) process.destroy();
        }
    }

    private static String redact(String value) {
        return ADDRESS.matcher(SIGNAL.matcher(value).replaceAll("$1[redacted signaling]"))
                .replaceAll("[redacted address]");
    }

    private static String b64(byte[] value) { return Base64.encodeToString(value, Base64.NO_WRAP); }

    private void send(boolean withCrash, String note, String player, String build) {
        try {
            File files = context.getExternalFilesDir(null);
            if (files == null) throw new Exception("Game files unavailable");
            StringBuilder text = new StringBuilder();
            File ini = new File(files, "data/goldeneye-vr.ini");
            if (!ini.isFile()) ini = new File(files, "goldeneye-vr.ini");
            if (ini.isFile()) {
                text.append("=== goldeneye-vr.ini ===\n").append(tail(ini, 50_000)).append("\n");
            }
            /* Gun fit's gadget poses (bondview2.c gevrGadgetFitEnd): "item left up fwd rx ry rz scale fist" */
            File poses = new File(files, "gevr_itempose.txt");
            if (poses.isFile()) {
                text.append("=== gevr_itempose.txt ===\n").append(tail(poses, 20_000)).append("\n");
            }
            text.append("=== exit history ===\n")
                    .append("install ").append(CrashExitPolicy.isoUtc(installTime))
                    .append(" versionCode ").append(versionCode).append('\n')
                    .append(exitHistory.length() > 0 ? exitHistory : "(no records)\n").append('\n');
            text.append("=== previous game run ===\n").append(tail(new File(files, "gevr.prev.log"), 1_200_000));
            text.append("\n=== current game run ===\n").append(tail(new File(files, "gevr.log"), 800_000));
            text.append("\n=== app logcat ===\n").append(logcat());
            byte[] plain = redact(text.toString()).getBytes(StandardCharsets.UTF_8);
            if (plain.length > MAX_LOG) {
                byte[] clipped = new byte[MAX_LOG];
                System.arraycopy(plain, plain.length - MAX_LOG, clipped, 0, MAX_LOG);
                plain = clipped;
            }
            ByteArrayOutputStream compressed = new ByteArrayOutputStream();
            try (GZIPOutputStream gzip = new GZIPOutputStream(compressed)) { gzip.write(plain); }
            byte[] trace = new byte[0];
            if (withCrash && crash != null) {
                try (InputStream input = crash.getTraceInputStream()) {
                    if (input != null) trace = readBounded(input, MAX_TRACE);
                }
            }
            String version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
            String crashSummary = withCrash && crash != null
                    ? redact(CrashExitPolicy.describe(crash.getTimestamp(), crash.getReason(),
                            crash.getStatus(), crash.getDescription(), installTime))
                    : unexpectedExit ? "Previous foreground run from "
                            + CrashExitPolicy.isoUtc(crashTimestamp) + " ended unexpectedly" : "";
            if (crashSummary.length() > 500) crashSummary = crashSummary.substring(0, 500);
            JSONObject body = new JSONObject()
                    .put("kind", withCrash ? "crash" : "manual")
                    .put("version", version).put("build", build)
                    .put("device", Build.MANUFACTURER + " " + Build.MODEL)
                    .put("player", player).put("note", note)
                    .put("crash_summary", crashSummary)
                    .put("log_gz_b64", b64(compressed.toByteArray()))
                    .put("tombstone_b64", b64(trace));
            HttpURLConnection connection = (HttpURLConnection) new URL(ENDPOINT).openConnection();
            try {
                connection.setRequestMethod("POST");
                connection.setConnectTimeout(10000);
                connection.setReadTimeout(20000);
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type", "application/json");
                try (OutputStream output = connection.getOutputStream()) {
                    output.write(body.toString().getBytes(StandardCharsets.UTF_8));
                }
                int code = connection.getResponseCode();
                if (code < 200 || code >= 300) throw new Exception("Server returned HTTP " + code);
                state = "sent";
                crash = null;
            } finally { connection.disconnect(); }
        } catch (Exception e) {
            Log.w(TAG, "Report failed", e);
            state = "error:" + e.getMessage();
        }
    }
}
