package com.gevr.port;

import org.json.JSONObject;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.*;

/** The friends client against a fake lobby Worker; nothing reaches production. */
public class FriendsClientTest {
    private static final String ID = "0123456789abcdef0123456789abcdef";
    private static final String SECRET = "ab".repeat(32);

    private static final class Call {
        final String method, path, token;
        final JSONObject body;
        Call(String method, String path, JSONObject body, String token) {
            this.method = method; this.path = path; this.body = body; this.token = token;
        }
    }

    private final List<Call> calls = new ArrayList<>();
    private final Map<String, Object> answers = new HashMap<>();   // "METHOD path" -> JSONObject or HttpException
    private ScheduledExecutorService executor;
    private FriendsClient client;
    private File file;

    private JSONObject fake(String method, String url, JSONObject body, String token) throws Exception {
        String path = url.substring(FriendsClient.BASE.length());
        calls.add(new Call(method, path, body, token));
        Object answer = answers.get(method + " " + path);
        if (answer instanceof Exception) throw (Exception) answer;
        return answer == null ? new JSONObject("{\"ok\":true}") : (JSONObject) answer;
    }

    @Before public void setup() throws Exception {
        file = new File(Files.createTempDirectory("gevr-friends").toFile(), "gevr_player.json");
        executor = Executors.newSingleThreadScheduledExecutor();
        answers.put("POST /players", new JSONObject().put("id", ID).put("secret", SECRET)
                .put("friendCode", "7K3QX2").put("restoreCode", "ABCDEFGHJKLMNPQR"));
        answers.put("POST /me/presence", new JSONObject("{\"me\":{\"showDetail\":true,\"appearOffline\":false},"
                + "\"friends\":[{\"id\":\"f1\",\"name\":\"Nat|ty\",\"code\":\"AAAAAA\",\"state\":\"lobby\",\"version\":25,"
                + "\"detail\":\"Co-op, lobby, 2/4\",\"lobby\":\"ABCDEFGH\",\"players\":2,\"maxPlayers\":4,\"lobbyVersion\":25}],"
                + "\"requests\":[{\"id\":\"r1\",\"name\":\"Boris\",\"code\":\"BBBBBB\"}],\"sent\":[],\"blocked\":[],"
                + "\"invites\":[{\"id\":\"i1\",\"from\":\"f1\",\"name\":\"Natty\",\"code\":\"AAAAAA\",\"lobby\":\"ABCDEFGH\","
                + "\"lobbyName\":\"Natty's game\",\"stage\":33,\"players\":2,\"maxPlayers\":4,\"lobbyVersion\":25,\"expiresIn\":600}],"
                + "\"next\":30}"));
        client = new FriendsClient(file, this::fake, executor);
        settle();
    }

    @After public void teardown() throws Exception {
        client.shutdown();
        assertTrue(executor.awaitTermination(2, TimeUnit.SECONDS));
    }

    private void settle() throws Exception { executor.submit(() -> {}).get(2, TimeUnit.SECONDS); }
    private void command(String line) throws Exception { client.command(line); settle(); }
    private void set(String name, Object value) throws Exception {
        Field field = FriendsClient.class.getDeclaredField(name);
        field.setAccessible(true);
        field.set(client, value);
    }
    private void poll() throws Exception {
        Method method = FriendsClient.class.getDeclaredMethod("poll");
        method.setAccessible(true);
        executor.submit(() -> {
            try { method.invoke(client); }
            catch (Exception e) { throw new RuntimeException(e); }
        }).get(2, TimeUnit.SECONDS);
    }
    private List<String> drain() {
        List<String> out = new ArrayList<>();
        for (String e = client.event(); !e.isEmpty(); e = client.event()) out.add(e);
        return out;
    }

    @Test public void nothingIsSentBeforeTheGameSaysWhoItIs() throws Exception {
        assertEquals(List.of("ME|none|||1|0"), drain());
        poll();
        assertTrue(calls.isEmpty());
    }

    @Test public void registersOnceNamedThenPostsPresenceAndPublishesTheList() throws Exception {
        drain();
        command("presence|lobby|ABCDEFGH|25|Bond|Deathmatch, Facility, 1/4");
        poll();
        assertEquals("POST /players", calls.get(0).method + " " + calls.get(0).path);
        assertEquals("Bond", calls.get(0).body.getString("name"));
        JSONObject saved = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
        assertEquals(ID, saved.getString("id"));
        assertEquals("ABCDEFGHJKLMNPQR", saved.getString("restoreCode"));

        Call presence = calls.get(1);
        assertEquals("/me/presence", presence.path);
        assertEquals(ID + "." + SECRET, presence.token);
        assertEquals("lobby", presence.body.getString("state"));
        assertEquals("ABCDEFGH", presence.body.getString("lobby"));
        assertEquals(25, presence.body.getInt("version"));
        assertEquals("Deathmatch, Facility, 1/4", presence.body.getString("detail"));

        List<String> events = drain();
        assertTrue(events.contains("ME|ready|7K3QX2|ABCDEFGHJKLMNPQR|1|0"));
        int begin = events.indexOf("FRIENDS_BEGIN");
        assertEquals("FRIEND|f1|Nat ty|AAAAAA|lobby|25|ABCDEFGH|2|4|25|0|Co-op, lobby, 2/4", events.get(begin + 1));
        assertEquals("REQUEST|r1|Boris|BBBBBB", events.get(begin + 2));
        assertEquals("INVITE|i1|f1|Natty|AAAAAA|ABCDEFGH|33|2|4|25|600|Natty's game", events.get(begin + 3));
        assertEquals("FRIENDS_END", events.get(events.size() - 1));

        // Thirty seconds until the next one; a changed lobby goes sooner, but not at once again.
        poll();
        assertEquals(2, calls.size());
    }

    @Test public void aSavedIdentityIsUsedAndASignOutStopsPresence() throws Exception {
        Files.write(file.toPath(), new JSONObject().put("id", ID).put("secret", SECRET).put("friendCode", "7K3QX2")
                .put("restoreCode", "ABCDEFGHJKLMNPQR").toString().getBytes(StandardCharsets.UTF_8));
        client.shutdown();
        executor = Executors.newSingleThreadScheduledExecutor();
        client = new FriendsClient(file, this::fake, executor);
        settle();
        assertEquals("ME|ready|7K3QX2|ABCDEFGHJKLMNPQR|1|0", drain().get(0));
        answers.put("POST /me/presence", new LobbyClient.HttpException(401, "Unknown player"));
        command("presence|launcher||25|Bond|");
        poll();
        assertEquals(1, calls.size());
        assertEquals("/me/presence", calls.get(0).path);
        assertTrue(drain().contains("ME|signed_out|7K3QX2|ABCDEFGHJKLMNPQR|1|0"));
        set("nextPresence", 0L);
        poll();
        assertEquals(1, calls.size());
    }

    @Test public void commandsReachTheirEndpoints() throws Exception {
        command("presence|launcher||25|Bond|");
        poll();
        calls.clear();
        drain();
        answers.put("POST /me/friends", new JSONObject().put("status", "sent").put("id", "f2"));
        command("add|GE-7K3QX2");
        command("answer|r1|1");
        command("invite|f1|ABCDEFGH");
        command("block|r1");
        command("settings|0|1");
        assertEquals("POST /me/friends", calls.get(0).method + " " + calls.get(0).path);
        assertEquals("GE-7K3QX2", calls.get(0).body.getString("code"));
        assertEquals("PUT /me/friends/r1", calls.get(1).method + " " + calls.get(1).path);
        assertTrue(calls.get(1).body.getBoolean("accept"));
        assertEquals("POST /me/invites", calls.get(2).method + " " + calls.get(2).path);
        assertEquals("ABCDEFGH", calls.get(2).body.getString("lobby"));
        assertEquals("POST /me/blocks", calls.get(3).method + " " + calls.get(3).path);
        assertEquals("PUT /me/settings", calls.get(4).method + " " + calls.get(4).path);
        assertFalse(calls.get(4).body.getBoolean("showDetail"));
        assertTrue(calls.get(4).body.getBoolean("appearOffline"));
        List<String> events = drain();
        assertTrue(events.contains("NOTICE|Friend request sent"));
        assertTrue(events.contains("INVITED|f1"));
    }

    @Test public void anErrorIsReportedOnceAndNotRepeated() throws Exception {
        command("presence|launcher||25|Bond|");
        poll();
        drain();
        answers.put("POST /me/friends", new LobbyClient.HttpException(404, "No player has that friend code"));
        command("add|ZZZZZZ");
        assertEquals(List.of("ERROR|No player has that friend code"), drain());
    }

    @Test public void discordLinkKeepsTheRefreshTokenAndAdoptsALinkedPlayer() throws Exception {
        command("presence|launcher||25|Bond|");
        poll();
        drain();
        calls.clear();
        answers.put("POST /discord/link", new JSONObject().put("accessToken", "acc").put("refreshToken", "ref")
                .put("discordName", "Alpha").put("identity", new JSONObject().put("id", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")
                        .put("secret", "ef".repeat(32)).put("friendCode", "DDDDDD").put("name", "Bond")));
        command("discord-link|the-code|the-verifier|discord-1567606949862711468:/authorize/callback");
        Call link = calls.get(0);
        assertEquals("/discord/link", link.path);
        assertEquals(ID + "." + SECRET, link.token);
        assertEquals("the-verifier", link.body.getString("codeVerifier"));
        JSONObject saved = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
        assertEquals("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", saved.getString("id"));
        assertEquals("ref", saved.getString("discordRefresh"));
        List<String> events = drain();
        assertTrue(events.contains("DISCORD|linked|Alpha"));
        assertTrue(events.contains("DISCORD_TOKEN|acc"));
        assertTrue(events.contains("ME|ready|DDDDDD||1|0"));

        answers.put("POST /discord/refresh", new LobbyClient.HttpException(401, "Discord sign-in expired; link again"));
        command("discord-refresh");
        assertTrue(drain().contains("DISCORD|unlinked|"));
        command("discord-friends|123456789012345678");   // no link any more: nothing sent
        assertEquals("/discord/refresh", calls.get(calls.size() - 1).path);
    }

    @Test public void restoreReplacesTheIdentityAndResetForgetsIt() throws Exception {
        command("presence|launcher||25|Bond|");
        poll();
        drain();
        answers.put("POST /players/restore", new JSONObject().put("id", "ffffffffffffffffffffffffffffffff")
                .put("secret", "cd".repeat(32)).put("friendCode", "QQQQQQ").put("name", "Bond"));
        command("restore|GE-QQQQQQ|wxyz-wxyz-wxyz-wxyz");
        assertEquals("/players/restore", calls.get(calls.size() - 1).path);
        JSONObject saved = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
        assertEquals("ffffffffffffffffffffffffffffffff", saved.getString("id"));
        assertEquals("WXYZWXYZWXYZWXYZ", saved.getString("restoreCode"));
        assertTrue(drain().contains("ME|ready|QQQQQQ|WXYZWXYZWXYZWXYZ|1|0"));
        command("reset");
        assertFalse(file.exists());
        assertTrue(drain().contains("ME|none|||1|0"));
    }
}
