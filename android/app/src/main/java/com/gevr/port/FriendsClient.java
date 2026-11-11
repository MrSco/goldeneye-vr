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
    interface Http {
        JSONObject call(String method, String url, JSONObject body, String token) throws Exception;
        /** A ghost run (ghost-*): megabytes each way, and longer to send them. A test's fake answers both alike. */
        default JSONObject large(String method, String url, JSONObject body, String token) throws Exception {
            return call(method, url, body, token);
        }
    }
    // The longest run the game records (gevr_ghost.c GHOST_MAXSAMPLES) is 1.5 MB; base64 and JSON on top.
    static final int GHOST_MAX_FILE = 128 + 65536 * 24;
    private static final Http NETWORK = new Http() {
        @Override public JSONObject call(String method, String url, JSONObject body, String token) throws Exception {
            return LobbyClient.http(method, url, body, token);
        }
        @Override public JSONObject large(String method, String url, JSONObject body, String token) throws Exception {
            return LobbyClient.http(method, url, body, token, GHOST_MAX_FILE / 3 * 4 + 65536, 30_000);
        }
    };

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
    // Discord (vr_discord.cpp): the link's refresh token and the account's name, kept with the player.
    private String discordRefresh = "", discordName = "";
    private String state = "launcher", lobby = "", name = "", detail = "";
    private int version;
    private boolean showDetail = true, appearOffline, searchable = true;
    private String refusedName = "";   // a rename the Worker kept back (ten a day), said once
    private boolean settingsDirty;
    private long lastKeepalive, lastSent, nextPresence, nextRegister;
    private long nextSettings;   // a failed settings PUT waits RETRY_MS, not the next poll
    private String lastError = "";

    FriendsClient(File file) {
        this(file, NETWORK, Executors.newSingleThreadScheduledExecutor(r -> {
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
                // settings|showDetail|appearOffline[|searchable]
                if (f.length != 3 && f.length != 4) return;
                showDetail = "1".equals(f[1]);
                appearOffline = "1".equals(f[2]);
                if (f.length == 4) searchable = "1".equals(f[3]);
                settingsDirty = true;
                if ("ready".equals(status)) putSettings();
                break;
            case "search": {
                // search|text: players by name (vr_friends.cpp shows SEARCH lines between BEGIN and END)
                if (f.length != 2 || !ready()) return;
                emit("SEARCH_BEGIN|" + clean(f[1]));
                try {
                    JSONArray results = http.call("POST", BASE + "/me/search", new JSONObject().put("q", f[1]), token())
                            .optJSONArray("results");
                    for (int i = 0; results != null && i < results.length(); i++) {
                        JSONObject x = results.getJSONObject(i);
                        emit("SEARCH|" + x.getString("id") + "|" + clean(x.getString("name")) + "|" + x.getString("code") + "|"
                                + x.optString("relation", "none"));
                    }
                } finally {
                    emit("SEARCH_END");
                }
                break;
            }
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
            case "discord-link": {
                // discord-link|code|verifier|redirectUri: Client::Authorize's answer, exchanged by the Worker
                if (f.length != 4) return;
                JSONObject r = http.call("POST", BASE + "/discord/link", new JSONObject().put("code", f[1])
                        .put("codeVerifier", f[2]).put("redirectUri", f[3]), ready() ? token() : "");
                JSONObject identity = r.optJSONObject("identity");
                if (identity != null) {
                    // this Discord account was another player's: that player comes back, as with a restore code
                    id = identity.getString("id");
                    secret = identity.getString("secret");
                    friendCode = identity.getString("friendCode");
                    restoreCode = "";   // the one written down still works; this headset never saw it
                    status = "ready";
                    settingsDirty = true;
                    emitMe();
                    emit("NOTICE|Discord brought back GE-" + friendCode);
                }
                discordRefresh = r.optString("refreshToken");
                discordName = clean(r.optString("discordName"));
                save();
                emit("DISCORD|linked|" + discordName);
                emit("DISCORD_TOKEN|" + r.getString("accessToken"));
                if (identity == null) emit("NOTICE|Discord linked as " + discordName);
                nextPresence = 0;
                break;
            }
            case "discord-refresh": {
                if (discordRefresh.isEmpty()) return;
                JSONObject r;
                try {
                    r = http.call("POST", BASE + "/discord/refresh", new JSONObject().put("refreshToken", discordRefresh), "");
                } catch (LobbyClient.HttpException e) {
                    if (e.status != 401) throw e;
                    discordRefresh = "";   // Discord no longer takes it: link again
                    save();
                    emit("DISCORD|unlinked|");
                    emit("NOTICE|Discord sign-in expired; link it again");
                    return;
                }
                discordRefresh = r.optString("refreshToken", discordRefresh);
                save();
                emit("DISCORD_TOKEN|" + r.getString("accessToken"));
                break;
            }
            case "discord-unlink":
                // forget the link here first: offline, the DELETE fails and must not leave it saved
                discordRefresh = discordName = "";
                save();
                emit("DISCORD|unlinked|");
                if (ready()) http.call("DELETE", BASE + "/me/discord", null, token());
                soon();
                break;
            case "discord-friends": {
                // discord-friends|id,id,...: the user's Discord friends, from the SDK
                if (f.length != 2 || !ready() || discordRefresh.isEmpty()) return;
                JSONArray ids = new JSONArray();
                for (String x : f[1].split(",")) if (!x.isEmpty()) ids.put(x);
                http.call("PUT", BASE + "/me/discord/friends", new JSONObject().put("ids", ids), token());
                soon();
                break;
            }
            // The ghost boards (the Worker's ghosts.ts; the launcher's Ghosts tab, vr_ghosts.cpp).
            // Each answers with its own events, a failure included, so the tab is never left waiting.
            case "ghost-board": {
                // ghost-board|campaign|stage|difficulty|plays (vr, flat or empty)
                if (f.length != 5) return;
                String key = f[1] + "|" + f[2] + "|" + f[3] + "|" + f[4];
                try {
                    JSONArray entries = http.call("GET", BASE + "/ghosts?campaign=" + f[1] + "&stage=" + f[2] + "&difficulty=" + f[3]
                            + (f[4].isEmpty() ? "" : "&plays=" + f[4]), null, "").optJSONArray("entries");
                    emit("GHOSTS_BEGIN|" + key);
                    for (int i = 0; entries != null && i < entries.length(); i++) {
                        JSONObject x = entries.getJSONObject(i);
                        // the name last: it may hold anything but '|'
                        emit("GHOST|" + x.getLong("id") + "|" + x.getInt("time60") + "|" + x.optInt("flags") + "|"
                                + clean(x.getString("code")) + "|" + clean(x.getString("name")));
                    }
                    emit("GHOSTS_END|" + key);
                } catch (Exception e) {
                    emit("GHOST_FAIL|board|" + key + "|" + clean(String.valueOf(e.getMessage())));
                }
                break;
            }
            case "ghost-upload": {
                // ghost-upload|path: one of this headset's runs, its owner already this friend code
                if (f.length != 2) return;
                try {
                    if (!ready()) throw new Exception("Friends is still signing in; try again in a moment");
                    byte[] bytes = readFile(new File(f[1]), GHOST_MAX_FILE);
                    JSONObject r = http.large("POST", BASE + "/me/ghosts", new JSONObject().put("data", base64(bytes)), token());
                    emit("GHOST_UPLOADED|" + f[1] + "|" + (r.optBoolean("stored") ? 1 : 0) + "|" + r.optInt("rank") + "|"
                            + clean(r.optString("reason")));
                } catch (LobbyClient.HttpException e) {
                    if (e.status == 401) failed(e);
                    emit("GHOST_FAIL|upload|" + f[1] + "|" + clean(String.valueOf(e.getMessage())));
                } catch (Exception e) {
                    emit("GHOST_FAIL|upload|" + f[1] + "|" + clean(String.valueOf(e.getMessage())));
                }
                break;
            }
            case "ghost-get": {
                // ghost-get|id|path: a board's run, written where the race reads it
                if (f.length != 3) return;
                try {
                    JSONObject r = http.large("GET", BASE + "/ghosts/" + f[1], null, "");
                    byte[] bytes = unbase64(r.getString("data"));
                    if (bytes == null || bytes.length > GHOST_MAX_FILE) throw new Exception("Not a ghost file");
                    File out = new File(f[2]), part = new File(f[2] + ".part");
                    try (FileOutputStream stream = new FileOutputStream(part)) { stream.write(bytes); }
                    if (!part.renameTo(out) && !(out.delete() && part.renameTo(out))) throw new IOException("Could not save " + out);
                    emit("GHOST_SAVED|" + f[1] + "|" + f[2]);
                } catch (Exception e) {
                    emit("GHOST_FAIL|get|" + f[1] + "|" + clean(String.valueOf(e.getMessage())));
                }
                break;
            }
            case "reset":
                // Start over with a new friend code (after a sign-out, or by choice).
                id = secret = friendCode = restoreCode = discordRefresh = discordName = "";
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

    private static byte[] readFile(File file, int max) throws IOException {
        if (file.length() > max) throw new IOException("Too large: " + file.getName());
        byte[] bytes = new byte[(int) file.length()];
        try (InputStream in = new FileInputStream(file)) {
            int at = 0, n;
            while (at < bytes.length && (n = in.read(bytes, at, bytes.length - at)) > 0) at += n;
            if (at != bytes.length) throw new IOException("Short read: " + file.getName());
        }
        return bytes;
    }

    // Base64 by hand: java.util.Base64 is API 26 (the phone build allows 24), and the JVM tests stub android.util's.
    private static final char[] B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".toCharArray();

    static String base64(byte[] in) {
        StringBuilder out = new StringBuilder((in.length + 2) / 3 * 4);
        for (int i = 0; i < in.length; i += 3) {
            int n = (in[i] & 0xff) << 16 | (i + 1 < in.length ? (in[i + 1] & 0xff) << 8 : 0) | (i + 2 < in.length ? in[i + 2] & 0xff : 0);
            out.append(B64[n >> 18 & 63]).append(B64[n >> 12 & 63])
               .append(i + 1 < in.length ? B64[n >> 6 & 63] : '=').append(i + 2 < in.length ? B64[n & 63] : '=');
        }
        return out.toString();
    }

    /** The bytes, or null if the text is not standard base64. */
    static byte[] unbase64(String in) {
        int len = in.length();
        while (len > 0 && in.charAt(len - 1) == '=') len--;
        if (len % 4 == 1) return null;
        byte[] out = new byte[len * 3 / 4];
        int bits = 0, value = 0, at = 0;
        for (int i = 0; i < len; i++) {
            char c = in.charAt(i);
            int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                    : c == '+' ? 62 : c == '/' ? 63 : -1;
            if (d < 0) return null;
            value = (value << 6 | d) & 0xffffff;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out[at++] = (byte) (value >> bits);
            }
        }
        return out;
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
            if (settingsDirty && now >= nextSettings) {
                nextSettings = now + RETRY_MS;
                putSettings();
            }
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
        settingsDirty = !showDetail || appearOffline || !searchable;
        nextPresence = 0;
        emitMe();
    }

    private void putSettings() throws Exception {
        http.call("PUT", BASE + "/me/settings", new JSONObject().put("showDetail", showDetail).put("appearOffline", appearOffline)
                .put("searchable", searchable), token());
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
            searchable = settingsDirty ? searchable : me.optBoolean("searchable", searchable);
            // the Worker keeps a name back after ten renames in a day
            String kept = me.optString("name", name);
            if (!kept.equals(name) && !name.equals(refusedName)) {
                refusedName = name;
                emit("NOTICE|Names can change ten times a day; friends still see " + clean(kept));
            }
        }
        emitMe();
        emit("FRIENDS_BEGIN");
        JSONArray friends = r.optJSONArray("friends");
        for (int i = 0; friends != null && i < friends.length(); i++) {
            JSONObject x = friends.getJSONObject(i);
            emit("FRIEND|" + x.getString("id") + "|" + clean(x.getString("name")) + "|" + x.getString("code") + "|"
                    + x.getString("state") + "|" + x.optInt("version") + "|" + x.optString("lobby") + "|"
                    + x.optInt("players") + "|" + x.optInt("maxPlayers") + "|" + x.optInt("lobbyVersion") + "|"
                    + (x.optBoolean("discord") ? 1 : 0) + "|" + clean(x.optString("detail")));
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
        emit("ME|" + status + "|" + friendCode + "|" + restoreCode + "|" + (showDetail ? 1 : 0) + "|" + (appearOffline ? 1 : 0)
                + "|" + (searchable ? 1 : 0));
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
                discordRefresh = x.optString("discordRefresh");
                discordName = x.optString("discordName");
                status = "ready";
            } catch (Exception e) {
                Log.w(TAG, "Unreadable " + file + "; a new player will be registered", e);
            }
        }
        emitMe();
        if (!discordRefresh.isEmpty()) emit("DISCORD|linked|" + discordName);   // vr_discord.cpp asks for a fresh token
    }

    /** A new file renamed over the old one, as the EEPROM is saved: a crash mid-write keeps the last good one. */
    private void save() throws IOException {
        File dir = file.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) throw new IOException("Could not create " + dir);
        File tmp = new File(file.getPath() + ".tmp");
        String text;
        try {
            text = new JSONObject().put("id", id).put("secret", secret).put("friendCode", friendCode)
                    .put("restoreCode", restoreCode).put("discordRefresh", discordRefresh)
                    .put("discordName", discordName).toString();
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
