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

    LogReporter(Context context) {
        this.context = context.getApplicationContext();
        prefs = this.context.getSharedPreferences("debug_reports", Context.MODE_PRIVATE);
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(null, 0, 5);
                for (ApplicationExitInfo exit : exits) {
                    if (exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE
                            && exit.getTimestamp() > prefs.getLong("offered_crash", 0)
                            && exit.getTimestamp() > crashTimestamp) {
                        crash = exit;
                        crashTimestamp = exit.getTimestamp();
                    }
                }
                if (crash != null) state = "offer";
            } catch (Exception e) { Log.w(TAG, "Could not inspect previous exits", e); }
        }
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
        final boolean withCrash = crash != null;
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
                    ? redact(crash.getTimestamp() + " " + crash.getDescription()) : "";
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
