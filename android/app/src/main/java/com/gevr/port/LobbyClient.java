package com.gevr.port;

import android.util.Base64;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Async lobby signaling for the native launcher. Never runs network I/O on its render thread. */
final class LobbyClient {
    private static final String TAG = "GEVR-Lobbies";
    private static final String BASE = "https://lobbies.goldeneyevr.com/v1/lobbies";
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread thread = new Thread(r, "lobby-client");
        thread.setDaemon(true);
        return thread;
    });
    private final ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();
    private final Set<String> seenRequests = new HashSet<>();
    private String code = "";
    private String ownerToken = "";
    private String joinId = "";
    private String joinToken = "";
    private boolean hosting;
    private boolean offerSent;
    private boolean answerReceived;
    private int players = 1;
    private int maxPlayers = 4;
    private boolean open = true;
    private String phase = "waiting";
    private long nextHeartbeat;
    private long nextRequests;
    private String lastError = "";
    private JSONObject pendingCreate;

    LobbyClient() {
        worker.scheduleWithFixedDelay(this::poll, 2, 2, TimeUnit.SECONDS);
    }

    String event() {
        String event = events.poll();
        return event == null ? "" : event;
    }

    void command(String raw) {
        worker.execute(() -> {
            try { execute(raw); }
            catch (Exception e) { error("Lobby service: " + e.getMessage()); }
        });
    }

    private void execute(String raw) throws Exception {
        String[] fields = raw.split("\\|", -1);
        switch (fields[0]) {
            case "create": {
                if (fields.length != 7) return;
                stop();
                players = 1;
                maxPlayers = Integer.parseInt(fields[6]);
                open = true;
                phase = "waiting";
                pendingCreate = new JSONObject()
                        .put("visibility", fields[1]).put("name", fields[2])
                        .put("version", Integer.parseInt(fields[3]))
                        .put("stage", Integer.parseInt(fields[4]))
                        .put("weapons", Integer.parseInt(fields[5]))
                        .put("maxPlayers", Integer.parseInt(fields[6]));
                registerPending();
                break;
            }
            case "stop": stop(); break;
            case "refresh":
                if (fields.length == 3) {
                    int newPlayers = Integer.parseInt(fields[1]);
                    boolean newOpen = "1".equals(fields[2]);
                    if (newPlayers != players || newOpen != open) {
                        players = newPlayers;
                        open = newOpen;
                        nextHeartbeat = 0;
                    }
                }
                break;
            case "phase":
                if (fields.length == 3 && ("warmup".equals(fields[1]) || "in_progress".equals(fields[1]))) {
                    phase = fields[1];
                    players = Integer.parseInt(fields[2]);
                    open = players < maxPlayers;
                    nextHeartbeat = 0;
                    nextRequests = 0;
                }
                break;
            case "list": {
                int version = Integer.parseInt(fields[1]);
                JSONArray rows = http("GET", BASE + "?version=" + version, null, "").getJSONArray("lobbies");
                events.add("LIST_BEGIN");
                for (int i = 0; i < rows.length(); i++) {
                    JSONObject row = rows.getJSONObject(i);
                    events.add("LOBBY|" + row.getString("code") + "|" + clean(row.getString("name")) + "|"
                            + row.getInt("stage") + "|" + row.getInt("weapons") + "|"
                            + row.getInt("players") + "|" + row.getInt("maxPlayers") + "|"
                            + row.optString("phase", "waiting"));
                }
                events.add("LIST_END");
                break;
            }
            case "join": {
                if (fields.length != 3) return;
                joinId = "";
                joinToken = "";
                offerSent = false;
                answerReceived = false;
                String requestedCode = fields[1].trim().toUpperCase();
                int version = Integer.parseInt(fields[2]);
                JSONObject body = new JSONObject().put("version", version);
                JSONObject result = http("POST", BASE + "/" + requestedCode + "/joins", body, "");
                joinId = result.getString("id");
                joinToken = result.getString("joinToken");
                code = requestedCode;
                JSONObject turn = http("POST", BASE + "/" + code + "/turn", new JSONObject().put("id", joinId), joinToken);
                events.add("JOINED|" + joinId + "|" + turn.getString("username") + "|" + turn.getString("credential"));
                break;
            }
            case "offer": {
                if (fields.length != 2 || joinId.isEmpty()) return;
                String sdp = decode(fields[1]);
                http("PUT", BASE + "/" + code + "/joins/" + joinId + "/offer", new JSONObject().put("sdp", sdp), joinToken);
                offerSent = true;
                break;
            }
            case "answer": {
                if (fields.length != 3 || !hosting) return;
                http("PUT", BASE + "/" + code + "/joins/" + fields[1] + "/answer", new JSONObject().put("sdp", decode(fields[2])), ownerToken);
                break;
            }
            default: break;
        }
    }

    private void poll() {
        try {
            if (pendingCreate != null) registerPending();
            if (hosting && !code.isEmpty()) {
                if (System.currentTimeMillis() >= nextHeartbeat) {
                    http("PUT", BASE + "/" + code, new JSONObject()
                            .put("players", players).put("open", open).put("phase", phase), ownerToken);
                    nextHeartbeat = System.currentTimeMillis() + 15_000;
                }
                if (open && System.currentTimeMillis() >= nextRequests) {
                    JSONArray requests = http("GET", BASE + "/" + code + "/joins", null, ownerToken).getJSONArray("requests");
                    nextRequests = System.currentTimeMillis() + ("waiting".equals(phase) ? 4_000 : 10_000);
                    for (int i = 0; i < requests.length(); i++) {
                        JSONObject request = requests.getJSONObject(i);
                        String id = request.getString("id");
                        if (seenRequests.contains(id)) continue;
                        JSONObject turn = http("POST", BASE + "/" + code + "/turn", new JSONObject(), ownerToken);
                        events.add("HOST_PEER|" + id + "|" + encode(request.getString("offer")) + "|"
                                + turn.getString("username") + "|" + turn.getString("credential"));
                        seenRequests.add(id);
                    }
                }
            } else if (!joinId.isEmpty() && offerSent && !answerReceived) {
                JSONObject answer = http("GET", BASE + "/" + code + "/joins/" + joinId + "/answer", null, joinToken);
                if (!answer.isNull("answer")) {
                    events.add("ANSWER|" + joinId + "|" + encode(answer.getString("answer")));
                    answerReceived = true;
                }
            }
            lastError = "";
        } catch (Exception e) {
            String message = "Lobby service: " + e.getMessage();
            if (!message.equals(lastError)) error(message);
            lastError = message;
        }
    }

    private void registerPending() throws Exception {
        if (pendingCreate == null) return;
        JSONObject result = http("POST", BASE, pendingCreate, "");
        code = result.getString("code");
        ownerToken = result.getString("ownerToken");
        pendingCreate = null;
        hosting = true;
        nextHeartbeat = 0;
        nextRequests = 0;
        seenRequests.clear();
        events.add("CREATED|" + code);
    }

    private void stop() {
        pendingCreate = null;
        if (hosting && !code.isEmpty()) {
            try { http("DELETE", BASE + "/" + code, null, ownerToken); }
            catch (Exception e) { Log.w(TAG, "Failed to remove lobby", e); }
        }
        hosting = false;
        phase = "waiting";
        code = "";
        ownerToken = "";
        joinId = "";
        joinToken = "";
        offerSent = false;
        answerReceived = false;
        seenRequests.clear();
    }

    void shutdown() {
        command("stop");
        worker.shutdown();
    }

    private void error(String message) {
        Log.w(TAG, message);
        events.add("ERROR|" + clean(message));
    }

    private static String clean(String value) { return value.replace('|', ' ').replace('\n', ' ').replace('\r', ' ').replace('\t', ' '); }
    private static String encode(String value) { return Base64.encodeToString(value.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP | Base64.URL_SAFE); }
    private static String decode(String value) { return new String(Base64.decode(value, Base64.NO_WRAP | Base64.URL_SAFE), StandardCharsets.UTF_8); }

    private static JSONObject http(String method, String path, JSONObject data, String token) throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(path).openConnection();
        connection.setRequestMethod(method);
        connection.setConnectTimeout(5000);
        connection.setReadTimeout(5000);
        connection.setRequestProperty("Accept", "application/json");
        if (!token.isEmpty()) connection.setRequestProperty("Authorization", "Bearer " + token);
        if (data != null) {
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json");
            try (OutputStream output = connection.getOutputStream()) { output.write(data.toString().getBytes(StandardCharsets.UTF_8)); }
        }
        int status = connection.getResponseCode();
        try (InputStream input = status < 400 ? connection.getInputStream() : connection.getErrorStream()) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            if (input != null) {
                byte[] chunk = new byte[4096];
                int read;
                while ((read = input.read(chunk)) != -1 && bytes.size() < 65536) bytes.write(chunk, 0, read);
            }
            JSONObject response = bytes.size() == 0 ? new JSONObject() : new JSONObject(bytes.toString("UTF-8"));
            if (status >= 400) throw new Exception(response.optString("error", "HTTP " + status));
            return response;
        } finally { connection.disconnect(); }
    }
}
