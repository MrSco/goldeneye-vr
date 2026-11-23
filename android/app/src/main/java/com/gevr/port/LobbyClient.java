package com.gevr.port;

import android.util.Base64;
import android.util.Log;
import android.os.SystemClock;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/** Async lobby signaling for the native launcher. Never runs network I/O on its render thread. */
final class LobbyClient {
    private static final String TAG = "GEVR-Lobbies";
    private static final String BASE = "https://lobbies.goldeneyevr.com/v1/lobbies";
    // Push signaling (the Worker's /socket): offers and answers arrive at once instead of on
    // the next poll, and the host's heartbeat rides the socket. Polling is the fallback.
    private static final String SOCKET_BASE = "wss://lobbies.goldeneyevr.com/v1/lobbies";
    private static final long SOCKET_RETRY_MS = 60_000;   // after a failed or dropped host socket
    private static final long SOCKET_PING_MS = 30_000;    // the Worker answers "ping" without waking
    private static final int SOCKET_SILENCE_MS = 75_000;  // nothing heard this long: the socket is dead
    private static final long STATE_REPLY_MS = 10_000;    // a heartbeat unanswered this long: back to HTTP
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread thread = new Thread(r, "lobby-client");
        thread.setDaemon(true);
        return thread;
    });
    private final ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();
    private final Set<String> seenRequests = new HashSet<>();
    private String code = "";
    private String ownerToken = "";
    private String name = "";
    private String joinId = "";
    private String joinToken = "";
    private boolean hosting;
    private boolean offerSent;
    private boolean answerReceived;
    private int players = 1;
    private int maxPlayers = 4;
    /** "VR players only" as last sent by the host: 1, 0, or -1 not known (a resume before its refresh). */
    private int vrOnly = -1;
    /** How many of the players play flat, as the host last counted them; -1 not known. */
    private int flatPlayers = -1;
    // devices|key@build,...: what each person runs (vr_launcher.cpp); null not said since create/resume
    private String devices = null;
    private boolean open = true;
    private String phase = "waiting";
    private long nextHeartbeat;
    private long nextRequests;
    private long lastKeepalive;
    private boolean heartbeatPaused;
    private String lastError = "";
    private JSONObject pendingCreate;
    private LobbySocket hostSocket;
    private LobbySocket joinSocket;
    private long nextSocketTry;
    private long lastSocketPing;
    private long stateSentAt;   // a heartbeat sent on the socket and not yet answered; 0 none

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
                // create|visibility|name|version|stage|weapons|maxPlayers[|vrOnly[|campaign]]
                if (fields.length < 7 || fields.length > 9) return;
                stop();
                name = fields[2];
                lastKeepalive = SystemClock.elapsedRealtime();
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
                vrOnly = fields.length >= 8 ? ("1".equals(fields[7]) ? 1 : 0) : -1;
                flatPlayers = -1;
                devices = null;
                if (vrOnly >= 0) pendingCreate.put("vrOnly", vrOnly == 1);
                // the game's campaign (gevr_romlayout.h GEVR_CAMPAIGN_*): a ROM hack's co-op game says which
                if (fields.length == 9) pendingCreate.put("campaign", Integer.parseInt(fields[8]));
                registerPending();
                break;
            }
            case "stop": stop(); break;
            case "leave": forget(); break;   // a host leaving players behind: the lobby stays for the one they elect
            case "resume": {
                // resume|code|ownerToken|maxPlayers|phase|players[|name]
                if (fields.length != 6 && fields.length != 7) return;
                pendingCreate = null;
                code = fields[1];
                ownerToken = fields[2];
                name = fields.length == 7 ? fields[6] : "";
                lastKeepalive = SystemClock.elapsedRealtime();
                heartbeatPaused = false;
                maxPlayers = Integer.parseInt(fields[3]);
                vrOnly = -1;   // the Worker keeps it; the next refresh says it again
                flatPlayers = -1;
                devices = null;
                phase = fields[4];
                players = Integer.parseInt(fields[5]);
                open = players < maxPlayers;
                joinId = "";
                joinToken = "";
                offerSent = false;
                answerReceived = false;
                hosting = true;
                nextHeartbeat = 0;
                nextRequests = 0;
                seenRequests.clear();
                events.add("CREATED|" + code + "|" + ownerToken);
                break;
            }
            case "refresh":
                // refresh|players|open[|maxPlayers[|vrOnly[|flatPlayers]]]: the host may change
                // the count, "VR players only" and who plays flat after registering
                lastKeepalive = SystemClock.elapsedRealtime();
                if (fields.length >= 3 && fields.length <= 6) {
                    int newPlayers = Integer.parseInt(fields[1]);
                    boolean newOpen = "1".equals(fields[2]);
                    int newMax = fields.length >= 4 ? Integer.parseInt(fields[3]) : maxPlayers;
                    int newVrOnly = fields.length >= 5 ? ("1".equals(fields[4]) ? 1 : 0) : vrOnly;
                    int newFlat = fields.length == 6 ? Math.max(0, Math.min(Integer.parseInt(fields[5]), newPlayers)) : flatPlayers;
                    if (newPlayers != players || newOpen != open || newMax != maxPlayers || newVrOnly != vrOnly
                            || newFlat != flatPlayers) {
                        players = newPlayers;
                        open = newOpen;
                        maxPlayers = newMax;
                        vrOnly = newVrOnly;
                        flatPlayers = newFlat;
                        nextHeartbeat = 0;
                    }
                }
                break;
            case "devices": {
                // devices|key@build,...: sent before each refresh, passed on when it changes
                String next = fields.length >= 2 ? fields[1] : "";
                if (!next.equals(devices)) {
                    devices = next;
                    nextHeartbeat = 0;
                }
                break;
            }
            case "phase":
                lastKeepalive = SystemClock.elapsedRealtime();
                if (fields.length == 3 && ("warmup".equals(fields[1]) || "in_progress".equals(fields[1]))) {
                    phase = fields[1];
                    players = Integer.parseInt(fields[2]);
                    open = players < maxPlayers;
                    nextHeartbeat = 0;
                    nextRequests = 0;
                }
                break;
            case "list": {
                // list|version[|inputClass[|hideFlat]]: a flat player is not shown "VR players
                // only" games; a VR player may hide games with flat players in them
                int version = Integer.parseInt(fields[1]);
                String input = fields.length >= 3 && !fields[2].isEmpty() ? "&inputClass=" + Integer.parseInt(fields[2]) : "";
                if (fields.length >= 4 && "1".equals(fields[3])) input += "&hideFlat=1";
                JSONArray rows = http("GET", BASE + "?version=" + version + input, null, "").getJSONArray("lobbies");
                events.add("LIST_BEGIN");
                for (int i = 0; i < rows.length(); i++) {
                    JSONObject row = rows.getJSONObject(i);
                    events.add("LOBBY|" + row.getString("code") + "|" + clean(row.getString("name")) + "|"
                            + row.getInt("stage") + "|" + row.getInt("weapons") + "|"
                            + row.getInt("players") + "|" + row.getInt("maxPlayers") + "|"
                            + row.optString("phase", "waiting") + "|"
                            + row.optInt("flatPlayers", -1) + "|" + (row.optBoolean("vrOnly", false) ? "1" : "0") + "|"
                            + row.optInt("campaign", 0) + "|" + (row.optBoolean("dedicated", false) ? "1" : "0"));
                }
                events.add("LIST_END");
                break;
            }
            case "join": {
                // join|code|version[|inputClass]: the Worker refuses a flat player a VR-only game
                if (fields.length != 3 && fields.length != 4) return;
                closeJoinSocket();
                joinId = "";
                joinToken = "";
                offerSent = false;
                answerReceived = false;
                String requestedCode = fields[1].trim().toUpperCase();
                int version = Integer.parseInt(fields[2]);
                JSONObject body = new JSONObject().put("version", version);
                if (fields.length == 4 && !fields[3].isEmpty()) body.put("inputClass", Integer.parseInt(fields[3]));
                JSONObject result = http("POST", BASE + "/" + requestedCode + "/joins", body, "");
                joinId = result.getString("id");
                joinToken = result.getString("joinToken");
                code = requestedCode;
                events.add("JOINED|" + joinId + "|" + relayCredentials(new JSONObject().put("id", joinId), joinToken));
                break;
            }
            case "offer": {
                if (fields.length != 2 || joinId.isEmpty()) return;
                String sdp = decode(fields[1]);
                http("PUT", BASE + "/" + code + "/joins/" + joinId + "/offer", new JSONObject().put("sdp", sdp), joinToken);
                offerSent = true;
                // An answer stored before the socket opens is sent on connect; without a socket, poll.
                joinSocket = openSocket(SOCKET_BASE + "/" + code + "/socket?join=" + joinId, joinToken);
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
            final long now = SystemClock.elapsedRealtime();
            final boolean keepalive = now - lastKeepalive <= 60_000;
            if (hosting || pendingCreate != null) {
                if (!keepalive && !heartbeatPaused) {
                    Log.i(TAG, "Heartbeat paused for lobby " + code + ": no native keepalive for 60 s");
                    heartbeatPaused = true;
                } else if (keepalive && heartbeatPaused) {
                    Log.i(TAG, "Heartbeat resumed for lobby " + code);
                    heartbeatPaused = false;
                    nextHeartbeat = 0;
                    nextRequests = 0;
                }
            }
            if (pendingCreate != null && keepalive) registerPending();
            if (hosting && !code.isEmpty()) {
                // Pause offer polling too: discovering expiry while asleep must
                // not make the native loop register a replacement in the background.
                if (!keepalive) {
                    closeSockets();
                    return;
                }
                if (hostSocket == null && now >= nextSocketTry) {
                    hostSocket = openSocket(SOCKET_BASE + "/" + code + "/socket", ownerToken);
                    if (hostSocket == null) nextSocketTry = SystemClock.elapsedRealtime() + SOCKET_RETRY_MS;
                }
                if (hostSocket != null && stateSentAt > 0 && SystemClock.elapsedRealtime() - stateSentAt > STATE_REPLY_MS) {
                    Log.w(TAG, "Lobby socket did not answer a heartbeat; polling again");
                    dropHostSocket();
                }
                if (SystemClock.elapsedRealtime() >= nextHeartbeat) {
                    JSONObject state = new JSONObject()
                            .put("players", players).put("open", open).put("phase", phase)
                            .put("maxPlayers", maxPlayers);
                    if (vrOnly >= 0) state.put("vrOnly", vrOnly == 1);
                    if (flatPlayers >= 0) state.put("flatPlayers", Math.min(flatPlayers, players));
                    if (devices != null) state.put("devices", devicesJson(devices));
                    if (!name.isEmpty()) state.put("name", name);
                    boolean sent = false;
                    if (hostSocket != null) {
                        try {
                            hostSocket.send(new JSONObject(state.toString()).put("type", "state").toString());
                            stateSentAt = SystemClock.elapsedRealtime();
                            sent = true;
                        } catch (IOException e) { dropHostSocket(); }
                    }
                    if (!sent) {
                        try { http("PUT", BASE + "/" + code, state, ownerToken); }
                        catch (HttpException e) {
                            if (lost(e)) return;
                            throw e;
                        }
                    }
                    nextHeartbeat = SystemClock.elapsedRealtime() + 15_000;
                }
                pingSockets();
                // With a socket the Worker sends each request itself (the pending ones on connect).
                if (hostSocket == null && open && SystemClock.elapsedRealtime() >= nextRequests) {
                    JSONArray requests;
                    try { requests = http("GET", BASE + "/" + code + "/joins", null, ownerToken).getJSONArray("requests"); }
                    catch (HttpException e) {
                        if (lost(e)) return;
                        throw e;
                    }
                    nextRequests = SystemClock.elapsedRealtime() + ("waiting".equals(phase) ? 4_000 : 10_000);
                    for (int i = 0; i < requests.length(); i++) {
                        JSONObject request = requests.getJSONObject(i);
                        String id = request.getString("id");
                        if (seenRequests.contains(id)) continue;
                        events.add("HOST_PEER|" + id + "|" + encode(request.getString("offer")) + "|"
                                + relayCredentials(new JSONObject(), ownerToken));
                        seenRequests.add(id);
                    }
                }
            } else if (!joinId.isEmpty() && offerSent && !answerReceived && joinSocket != null) {
                pingSockets();
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

    /** Opens a push socket, or null (and the caller polls) if the Worker cannot be reached that way. */
    private LobbySocket openSocket(String url, String token) {
        final LobbySocket[] self = new LobbySocket[1];
        try {
            self[0] = LobbySocket.open(url, token, new LobbySocket.Listener() {
                @Override public void message(String text) { post(() -> socketMessage(self[0], text)); }
                @Override public void closed(String why) { post(() -> socketClosed(self[0], why)); }
            }, 5000, SOCKET_SILENCE_MS);
            lastSocketPing = SystemClock.elapsedRealtime();
            Log.i(TAG, "Lobby socket open (" + (url.contains("?join=") ? "join" : "host") + ")");
            return self[0];
        } catch (Exception e) {
            Log.w(TAG, "Lobby socket unavailable, polling instead: " + e.getMessage());
            return null;
        }
    }

    private void post(Runnable job) {
        try { worker.execute(job); } catch (RejectedExecutionException ignored) { }
    }

    /** On the worker thread, as everything that touches the lobby state. */
    private void socketMessage(LobbySocket from, String text) {
        if (from == null || (from != hostSocket && from != joinSocket) || "pong".equals(text)) return;
        try {
            JSONObject message = new JSONObject(text);
            String type = message.optString("type");
            if (from == hostSocket && "request".equals(type)) {
                String id = message.getString("id");
                if (seenRequests.contains(id)) return;
                events.add("HOST_PEER|" + id + "|" + encode(message.getString("offer")) + "|"
                        + relayCredentials(new JSONObject(), ownerToken));
                seenRequests.add(id);
            } else if (from == hostSocket && "state".equals(type)) {
                stateSentAt = 0;
                int status = message.optInt("status", 200);
                if (status >= 400) {
                    HttpException e = new HttpException(status, message.optString("error", "HTTP " + status));
                    if (!lost(e)) error("Lobby service: " + e.getMessage());
                }
            } else if (from == joinSocket && "answer".equals(type) && !answerReceived) {
                events.add("ANSWER|" + joinId + "|" + encode(message.getString("answer")));
                answerReceived = true;
                closeJoinSocket();
            }
        } catch (Exception e) {
            Log.w(TAG, "Lobby socket message ignored: " + e.getMessage());
        }
    }

    private void socketClosed(LobbySocket from, String why) {
        if (from != null && from == hostSocket) {
            Log.i(TAG, "Lobby socket closed (" + why + "); polling, socket again in " + SOCKET_RETRY_MS / 1000 + " s");
            hostSocket = null;
            stateSentAt = 0;
            nextSocketTry = SystemClock.elapsedRealtime() + SOCKET_RETRY_MS;
            nextHeartbeat = 0;   // the last heartbeat may not have landed
            nextRequests = 0;
        } else if (from != null && from == joinSocket) {
            Log.i(TAG, "Lobby join socket closed (" + why + ")");
            joinSocket = null;
        }
    }

    private void dropHostSocket() {
        LobbySocket socket = hostSocket;
        socketClosed(socket, "dropped");
        if (socket != null) socket.close();
    }

    private void closeJoinSocket() {
        LobbySocket socket = joinSocket;
        joinSocket = null;
        if (socket != null) socket.close();
    }

    private void closeSockets() {
        LobbySocket socket = hostSocket;
        hostSocket = null;
        stateSentAt = 0;
        nextSocketTry = 0;
        if (socket != null) socket.close();
        closeJoinSocket();
    }

    private void pingSockets() {
        long now = SystemClock.elapsedRealtime();
        if (now - lastSocketPing < SOCKET_PING_MS) return;
        lastSocketPing = now;
        for (LobbySocket socket : new LobbySocket[] { hostSocket, joinSocket }) {
            if (socket == null) continue;
            try { socket.send("ping"); } catch (IOException ignored) { }   // closed() follows and polling resumes
        }
    }

    /**
     * TURN is a fallback for peers that STUN hole punching cannot reach. A refused or
     * exhausted relay must not block a join that can go direct, so a failure here yields
     * empty "user|credential" fields and the native ICE layer gathers STUN candidates only.
     */
    private String relayCredentials(JSONObject body, String token) {
        try {
            JSONObject turn = http("POST", BASE + "/" + code + "/turn", body, token);
            return clean(turn.getString("username")) + "|" + clean(turn.getString("credential"));
        } catch (Exception e) {
            Log.w(TAG, "Relay credentials unavailable; direct connection only: " + e.getMessage());
            return "|";
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
        events.add("CREATED|" + code + "|" + ownerToken);
    }

    private void stop() {
        pendingCreate = null;
        if (hosting && !code.isEmpty()) {
            try {
                http("DELETE", BASE + "/" + code, null, ownerToken);
                Log.i(TAG, "Removed lobby " + code);
            }
            catch (HttpException e) {
                if (e.status == 404 || e.status == 410) Log.i(TAG, "Lobby " + code + " already gone (HTTP " + e.status + ")");
                else Log.w(TAG, "Failed to remove lobby", e);
            }
            catch (Exception e) { Log.w(TAG, "Failed to remove lobby", e); }
        }
        forget();
    }

    /** Drops this client's part in the lobby without removing the lobby itself. */
    private void forget() {
        closeSockets();
        pendingCreate = null;
        hosting = false;
        phase = "waiting";
        code = "";
        ownerToken = "";
        name = "";
        lastKeepalive = 0;
        heartbeatPaused = false;
        joinId = "";
        joinToken = "";
        offerSent = false;
        answerReceived = false;
        seenRequests.clear();
    }

    /** Wait behind queued stop/leave commands, without holding up relaunch indefinitely. */
    void stopAndWait(long ms) {
        try {
            Future<?> removal = worker.submit(this::stop);
            removal.get(ms, TimeUnit.MILLISECONDS);
        } catch (TimeoutException e) {
            Log.w(TAG, "Lobby removal not confirmed within " + ms + " ms");
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            Log.w(TAG, "Lobby removal wait interrupted", e);
        } catch (RejectedExecutionException e) {
            Log.i(TAG, "Lobby client already shut down");
        } catch (Exception e) {
            Log.w(TAG, "Lobby removal failed", e);
        }
    }

    void shutdown() {
        stopAndWait(1500);
        worker.shutdown();
    }

    private boolean lost(HttpException e) {
        if (!hosting || (e.status != 404 && e.status != 410)) return false;
        String lostCode = code;
        Log.i(TAG, "Lobby " + lostCode + " is gone (HTTP " + e.status + ": " + e.getMessage() + ")");
        forget();
        events.add("LOBBY_LOST|" + lostCode + "|" + clean(e.getMessage()));
        return true;
    }

    static final class HttpException extends Exception {
        final int status;
        HttpException(int status, String message) {
            super(message);
            this.status = status;
        }
    }

    private void error(String message) {
        Log.w(TAG, message);
        events.add("ERROR|" + clean(message));
    }

    private static String clean(String value) { return value.replace('|', ' ').replace('\n', ' ').replace('\r', ' ').replace('\t', ' '); }
    private static String encode(String value) { return Base64.encodeToString(value.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP | Base64.URL_SAFE); }
    private static String decode(String value) { return new String(Base64.decode(value, Base64.NO_WRAP | Base64.URL_SAFE), StandardCharsets.UTF_8); }

    // Also FriendsClient's (FriendsClient.Http).
    static JSONObject http(String method, String path, JSONObject data, String token) throws Exception {
        return http(method, path, data, token, 65536, 5000);
    }

    /** As above, with a bigger reply and longer to wait for it (a ghost run: FriendsClient's ghost-*). */
    static JSONObject http(String method, String path, JSONObject data, String token, int maxReply, int timeoutMs) throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(path).openConnection();
        connection.setRequestMethod(method);
        connection.setConnectTimeout(5000);
        connection.setReadTimeout(timeoutMs);
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
                while ((read = input.read(chunk)) != -1 && bytes.size() < maxReply) bytes.write(chunk, 0, read);
            }
            JSONObject response;
            try { response = bytes.size() == 0 ? new JSONObject() : new JSONObject(bytes.toString("UTF-8")); }
            catch (Exception e) {
                if (status >= 400) throw new HttpException(status, "HTTP " + status);
                throw e;
            }
            if (status >= 400) throw new HttpException(status, response.optString("error", "HTTP " + status));
            return response;
        } finally { connection.disconnect(); }
    }

    /** devices|key@build,... as the Worker takes it: [{device, build}], at most eight, odd entries left out. */
    static JSONArray devicesJson(String devices) {
        JSONArray out = new JSONArray();
        if (devices == null || devices.isEmpty()) return out;
        for (String entry : devices.split(",")) {
            int at = entry.indexOf('@');
            String key = at < 0 ? entry : entry.substring(0, at);
            String build = at < 0 ? "" : entry.substring(at + 1);
            if (!key.matches("[a-z-]{1,16}") || !build.matches("[0-9a-z.-]{0,23}") || out.length() >= 8) continue;
            try { out.put(new JSONObject().put("device", key).put("build", build)); }
            catch (org.json.JSONException e) { /* never for these strings */ }
        }
        return out;
    }
}
