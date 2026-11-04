package com.gevr.port;

import android.os.SystemClock;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * Friends, presence and invites (docs/friends-and-invites.md) for the native launcher
 * and game (port/vr/vr_friends.cpp). Talks to the lobby Worker's /v1/players and /v1/me
 * on its own thread; the native side sends one-line commands and drains one-line events,
 * as with LobbyClient.
 *
 * The player is a device identity kept in data/gevr_player.json: an ID and a secret the
 * Worker made, the friend code, and the restore code that brings it back elsewhere.
 * Nothing is sent until the native side has sent a presence line (it carries the name),
 * and presence stops a minute after the last one: a sleeping headset goes offline.
 */
final class FriendsClient {
    interface Http { JSONObject call(String method, String url, JSONObject body, String token) throws Exception; }

    private static final String TAG = "GEVR-Friends";
    static final String BASE = "https://lobbies.goldeneyevr.com/v1";
    static final long KEEPALIVE_MS = 60_000;
    static final long PRESENCE_MS = 30_000;
    // A changed state or lobby goes out at once, but never closer than this to the last send.
    static final long MIN_GAP_MS = 5_000;
    static final long RETRY_MS = 60_000;
    private static final int MAX_EVENTS = 4000;

    private final File file;
    private final Http http;
    private ScheduledExecutorService worker;
    private final ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();

    // none (registers once a name arrives), ready, signed_out (the secret was replaced by a restore elsewhere)
    private String status = "none";
    private String id = "", secret = "", friendCode = "", restoreCode = "";
    private String state = "launcher", lobby = "", name = "", detail = "";
    private int version;
    private boolean showDetail = true, appearOffline;
    private boolean settingsDirty;
    private long lastKeepalive, lastSent, nextPresence, nextRegister;
    private String lastError = "";

    FriendsClient(File file) {
        this(file, LobbyClient::http, Executors.newSingleThreadScheduledExecutor(r -> {
            Thread thread = new Thread(r, "friends-client");
            thread.setDaemon(true);
            return thread;
        }));
        worker.scheduleWithFixedDelay(this::poll, 1, 1, TimeUnit.SECONDS);
    }

    /** For tests: a fake Http and an executor the test drives. */
    FriendsClient(File file, Http http, ScheduledExecutorService worker) {
        this.file = file;
        this.http = http;
        this.worker = worker;
        worker.execute(this::load);
    }

    String event() {
        String event = events.poll();
        return event == null ? "" : event;
    }

    void command(String raw) {
        worker.execute(() -> {
            try { execute(raw); }
            catch (LobbyClient.HttpException e) { failed(e); }
            catch (Exception e) { error("Friends: " + e.getMessage()); }
        });
    }

    void shutdown() { worker.shutdown(); }

    private void emit(String event) {
        if (events.size() > MAX_EVENTS) events.clear();   // nobody is draining: start over
        events.add(event);
    }

    private void execute(String raw) throws Exception {
        String[] f = raw.split("\\|", -1);
        long now = SystemClock.elapsedRealtime();
        switch (f[0]) {
            case "presence": {
                // presence|state|lobby|version|name|detail (detail last: it may hold anything but '|')
                if (f.length < 6) return;
                boolean changed = !f[1].equals(state) || !f[2].equals(lobby);
                boolean first = name.isEmpty();
                state = f[1];
                lobby = f[2];
                version = Integer.parseInt(f[3]);
                name = f[4];
                detail = f[5];
                lastKeepalive = now;
                if (changed || first) nextPresence = Math.min(nextPresence, Math.max(now, lastSent + MIN_GAP_MS));
                break;
            }
            case "settings":
                if (f.length != 3) return;
                showDetail = "1".equals(f[1]);
                appearOffline = "1".equals(f[2]);
                settingsDirty = true;
                if ("ready".equals(status)) putSettings();
                break;
            case "refresh":
                nextPresence = Math.max(now, lastSent + 1000);
                break;
            case "add": {
                if (f.length != 2 || !ready()) return;
                JSONObject r = http.call("POST", BASE + "/me/friends", new JSONObject().put("code", f[1]), token());
                emit("NOTICE|" + ("friends".equals(r.optString("status")) ? "You are now friends" : "Friend request sent"));
                soon();
                break;
            }
            case "answer":
                if (f.length != 3 || !ready()) return;
                http.call("PUT", BASE + "/me/friends/" + f[1], new JSONObject().put("accept", "1".equals(f[2])), token());
                soon();
                break;
            case "remove":
                if (f.length != 2 || !ready()) return;
                http.call("DELETE", BASE + "/me/friends/" + f[1], null, token());
                soon();
                break;
            case "decline-all":
                if (!ready()) return;
                http.call("DELETE", BASE + "/me/requests", null, token());
                soon();
                break;
            case "block":
                if (f.length != 2 || !ready()) return;
                http.call("POST", BASE + "/me/blocks", new JSONObject().put("id", f[1]), token());
                soon();
                break;
            case "unblock":
                if (f.length != 2 || !ready()) return;
                http.call("DELETE", BASE + "/me/blocks/" + f[1], null, token());
                soon();
                break;
            case "invite":
                // invite|playerId|lobbyCode
                if (f.length != 3 || !ready()) return;
                http.call("POST", BASE + "/me/invites", new JSONObject().put("to", f[1]).put("lobby", f[2]), token());
                emit("INVITED|" + f[1]);
                break;
            case "drop":
                if (f.length != 2 || !ready()) return;
                http.call("DELETE", BASE + "/me/invites/" + f[1], null, token());
                soon();
                break;
            case "restore": {
                // restore|friendCode|restoreCode
                if (f.length != 3) return;
                JSONObject r = http.call("POST", BASE + "/players/restore",
                        new JSONObject().put("friendCode", f[1]).put("restoreCode", f[2]), "");
                id = r.getString("id");
                secret = r.getString("secret");
                friendCode = r.getString("friendCode");
                restoreCode = f[2].toUpperCase().replaceAll("[\\s-]", "");
                status = "ready";
                save();
                settingsDirty = true;
                emitMe();
                emit("NOTICE|Restored GE-" + friendCode);
                nextPresence = 0;
                break;
            }
            case "reset":
                // Start over with a new friend code (after a sign-out, or by choice).
                id = secret = friendCode = restoreCode = "";
                status = "none";
                nextRegister = 0;
                if (file.exists() && !file.delete()) Log.w(TAG, "Could not delete " + file);
                emitMe();
                emit("FRIENDS_BEGIN");
                emit("FRIENDS_END");
                break;
            default: break;
        }
    }

    private boolean ready() { return "ready".equals(status); }
    private String token() { return id + "." + secret; }
    private void soon() { nextPresence = Math.max(SystemClock.elapsedRealtime(), lastSent + 1000); }

    private void poll() {
        try {
            long now = SystemClock.elapsedRealtime();
            if (name.isEmpty() || now - lastKeepalive > KEEPALIVE_MS) return;
            if ("none".equals(status)) {
                if (now < nextRegister) return;
                nextRegister = now + RETRY_MS;
                register();
            }
            if (!ready()) return;
            if (settingsDirty) putSettings();
            if (now >= nextPresence) presence(now);
            lastError = "";
        } catch (LobbyClient.HttpException e) {
            failed(e);
        } catch (Exception e) {
            String message = "Friends: " + e.getMessage();
            if (!message.equals(lastError)) error(message);
            lastError = message;
            nextPresence = SystemClock.elapsedRealtime() + PRESENCE_MS;
        }
    }

    private void failed(LobbyClient.HttpException e) {
        if (e.status == 401 && ready()) {
            Log.i(TAG, "Signed out: this player was restored on another headset");
            status = "signed_out";
            emitMe();
            return;
        }
        nextPresence = SystemClock.elapsedRealtime() + (e.status == 429 ? PRESENCE_MS * 2 : PRESENCE_MS);
        String message = e.getMessage();
        if (!message.equals(lastError)) error(message);
        lastError = message;
    }

    private void register() throws Exception {
        JSONObject r = http.call("POST", BASE + "/players", new JSONObject().put("name", name), "");
        id = r.getString("id");
        secret = r.getString("secret");
        friendCode = r.getString("friendCode");
        restoreCode = r.getString("restoreCode");
        status = "ready";
        save();
        Log.i(TAG, "Registered as GE-" + friendCode);
        settingsDirty = !showDetail || appearOffline;
        nextPresence = 0;
        emitMe();
    }

    private void putSettings() throws Exception {
        http.call("PUT", BASE + "/me/settings", new JSONObject().put("showDetail", showDetail).put("appearOffline", appearOffline), token());
        settingsDirty = false;
        emitMe();
    }

    private void presence(long now) throws Exception {
        JSONObject body = new JSONObject().put("state", state).put("version", version).put("name", name);
        if (!detail.isEmpty()) body.put("detail", detail);
        if (!lobby.isEmpty()) body.put("lobby", lobby);
        lastSent = now;
        nextPresence = now + PRESENCE_MS;
        JSONObject r = http.call("POST", BASE + "/me/presence", body, token());
        nextPresence = SystemClock.elapsedRealtime() + Math.max(15, r.optInt("next", 30)) * 1000L;
        JSONObject me = r.optJSONObject("me");
        if (me != null) {
            showDetail = settingsDirty ? showDetail : me.optBoolean("showDetail", showDetail);
            appearOffline = settingsDirty ? appearOffline : me.optBoolean("appearOffline", appearOffline);
        }
        emitMe();
        emit("FRIENDS_BEGIN");
        JSONArray friends = r.optJSONArray("friends");
        for (int i = 0; friends != null && i < friends.length(); i++) {
            JSONObject x = friends.getJSONObject(i);
            emit("FRIEND|" + x.getString("id") + "|" + clean(x.getString("name")) + "|" + x.getString("code") + "|"
                    + x.getString("state") + "|" + x.optInt("version") + "|" + x.optString("lobby") + "|"
                    + x.optInt("players") + "|" + x.optInt("maxPlayers") + "|" + x.optInt("lobbyVersion") + "|"
                    + clean(x.optString("detail")));
        }
        people(r.optJSONArray("requests"), "REQUEST");
        people(r.optJSONArray("sent"), "SENT");
        people(r.optJSONArray("blocked"), "BLOCKED");
        JSONArray invites = r.optJSONArray("invites");
        for (int i = 0; invites != null && i < invites.length(); i++) {
            JSONObject x = invites.getJSONObject(i);
            emit("INVITE|" + x.getString("id") + "|" + x.getString("from") + "|" + clean(x.getString("name")) + "|"
                    + x.getString("code") + "|" + x.getString("lobby") + "|" + x.optInt("stage") + "|"
                    + x.optInt("players") + "|" + x.optInt("maxPlayers") + "|" + x.optInt("lobbyVersion") + "|"
                    + x.optInt("expiresIn") + "|" + clean(x.optString("lobbyName")));
        }
        emit("FRIENDS_END");
    }

    private void people(JSONArray list, String kind) throws Exception {
        for (int i = 0; list != null && i < list.length(); i++) {
            JSONObject x = list.getJSONObject(i);
            emit(kind + "|" + x.getString("id") + "|" + clean(x.getString("name")) + "|" + x.getString("code"));
        }
    }

    private void emitMe() {
        emit("ME|" + status + "|" + friendCode + "|" + restoreCode + "|" + (showDetail ? 1 : 0) + "|" + (appearOffline ? 1 : 0));
    }

    private void error(String message) {
        Log.w(TAG, message);
        emit("ERROR|" + clean(message));
    }

    private static String clean(String value) { return value.replace('|', ' ').replace('\n', ' ').replace('\r', ' ').replace('\t', ' '); }

    private void load() {
        if (file.isFile()) {
            try (InputStream in = new FileInputStream(file)) {
                byte[] bytes = new byte[(int) Math.min(file.length(), 4096)];
                int n = 0, read;
                while (n < bytes.length && (read = in.read(bytes, n, bytes.length - n)) > 0) n += read;
                JSONObject x = new JSONObject(new String(bytes, 0, n, StandardCharsets.UTF_8));
                id = x.getString("id");
                secret = x.getString("secret");
                friendCode = x.getString("friendCode");
                restoreCode = x.optString("restoreCode");
                status = "ready";
            } catch (Exception e) {
                Log.w(TAG, "Unreadable " + file + "; a new player will be registered", e);
            }
        }
        emitMe();
    }

    /** A new file renamed over the old one, as the EEPROM is saved: a crash mid-write keeps the last good one. */
    private void save() throws IOException {
        File dir = file.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) throw new IOException("Could not create " + dir);
        File tmp = new File(file.getPath() + ".tmp");
        String text;
        try {
            text = new JSONObject().put("id", id).put("secret", secret).put("friendCode", friendCode)
                    .put("restoreCode", restoreCode).toString();
        } catch (Exception e) { throw new IOException(e); }
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        if (!tmp.renameTo(file)) {
            if (!file.delete() || !tmp.renameTo(file)) throw new IOException("Could not save " + file);
        }
    }
}
