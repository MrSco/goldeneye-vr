package com.gevr.port;

import org.json.JSONObject;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLStreamHandler;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.*;

/** Exercises the real client with fake HTTP; no requests can reach production. */
public class LobbyClientTest {
    private static final List<FakeConnection> requests = new ArrayList<>();
    private static int putStatus, getStatus, deleteStatus, postStatus;
    private static String failureBody;
    private static CountDownLatch removalGate;
    private LobbyClient client;
    private ScheduledExecutorService executor;

    @BeforeClass public static void interceptHttp() {
        URL.setURLStreamHandlerFactory(protocol -> "https".equals(protocol) ? new URLStreamHandler() {
            @Override protected java.net.URLConnection openConnection(URL url) {
                FakeConnection connection = new FakeConnection(url);
                requests.add(connection);
                return connection;
            }
        } : null);
    }

    @Before public void setup() throws Exception {
        requests.clear();
        putStatus = getStatus = deleteStatus = postStatus = 200;
        failureBody = "{\"error\":\"Lobby idle timeout\"}";
        removalGate = null;
        client = new LobbyClient();
        ((ScheduledExecutorService) get("worker")).shutdownNow();
        // Drive polling explicitly rather than waiting for the two-second timer.
        executor = Executors.newSingleThreadScheduledExecutor();
        set("worker", executor);
    }

    @After public void teardown() throws Exception {
        if (removalGate != null) removalGate.countDown();
        client.shutdown();
        assertTrue(executor.awaitTermination(2, TimeUnit.SECONDS));
    }

    private Object get(String name) throws Exception {
        Field field = LobbyClient.class.getDeclaredField(name);
        field.setAccessible(true);
        return field.get(client);
    }
    private void set(String name, Object value) throws Exception {
        Field field = LobbyClient.class.getDeclaredField(name);
        field.setAccessible(true);
        field.set(client, value);
    }
    private void command(String command) throws Exception {
        client.command(command);
        executor.submit(() -> {}).get(2, TimeUnit.SECONDS);
    }
    private void poll() throws Exception {
        Method method = LobbyClient.class.getDeclaredMethod("poll");
        method.setAccessible(true);
        executor.submit(() -> {
            try { method.invoke(client); }
            catch (Exception e) { throw new RuntimeException(e); }
        }).get(2, TimeUnit.SECONDS);
    }
    private void resume() throws Exception {
        command("resume|ABCDEFGH|owner-token|4|warmup|1|B's game");
        assertEquals("CREATED|ABCDEFGH|owner-token", client.event());
    }

    @Test public void migrationSendsNameAndSupportsOldResume() throws Exception {
        resume();
        poll();
        assertEquals("B's game", new JSONObject(requests.get(0).body.toString("UTF-8")).getString("name"));
        command("resume|ABCDEFGH|owner-token|4|warmup|1");
        poll();
        assertFalse(new JSONObject(requests.get(2).body.toString("UTF-8")).has("name"));
    }

    @Test public void staleKeepalivePausesAllHostRequestsAndUnchangedRefreshResumes() throws Exception {
        resume();
        set("lastKeepalive", -60_001L);  // mocked Android elapsedRealtime is zero
        poll();
        poll();
        assertTrue(requests.isEmpty());
        assertEquals(true, get("heartbeatPaused"));
        command("refresh|1|1");
        poll();
        assertEquals(false, get("heartbeatPaused"));
        assertEquals(2, requests.size());
        assertEquals("PUT", requests.get(0).getRequestMethod());
        assertEquals("GET", requests.get(1).getRequestMethod());
    }

    @Test public void unchangedPhaseAlsoRenewsKeepalive() throws Exception {
        resume();
        set("lastKeepalive", -60_001L);
        command("phase|warmup|1");
        poll();
        assertEquals(2, requests.size());
    }

    @Test public void missingAndExpiredHeartbeatForgetLobbyOnce() throws Exception {
        for (int status : new int[] {404, 410}) {
            resume();
            putStatus = status;
            poll();
            assertEquals(false, get("hosting"));
            assertEquals("", get("code"));
            assertEquals("LOBBY_LOST|ABCDEFGH|Lobby idle timeout", client.event());
            int count = requests.size();
            poll();
            assertEquals(count, requests.size());
            assertEquals("", client.event());
        }
    }

    @Test public void missingOfferPollAndNonJsonErrorsRetainHttpStatus() throws Exception {
        resume();
        getStatus = 404;
        failureBody = "not-json";
        poll();
        assertEquals("LOBBY_LOST|ABCDEFGH|HTTP 404", client.event());
        assertEquals(false, get("hosting"));
    }

    @Test public void transientHeartbeatFailureDoesNotForgetLobby() throws Exception {
        resume();
        putStatus = 503;
        poll();
        assertEquals(true, get("hosting"));
        assertTrue(client.event().startsWith("ERROR|"));
    }

    @Test public void pendingRegistrationDoesNotRetryWithoutKeepalive() throws Exception {
        postStatus = 503;
        command("create|public|A's game|6|1|2|4");
        assertNotNull(get("pendingCreate"));
        int count = requests.size();
        set("lastKeepalive", -60_001L);
        poll();
        assertEquals(count, requests.size());
        postStatus = 200;
        command("refresh|1|1");
        poll();
        assertEquals(true, get("hosting"));
        assertNull(get("pendingCreate"));
    }

    @Test public void queuedLeavePreservesHandedOverLobby() throws Exception {
        resume();
        client.command("leave");
        client.stopAndWait(1000);
        assertTrue(requests.isEmpty());
        assertEquals(false, get("hosting"));
    }

    @Test public void queuedStopAndAlreadyGoneDeleteCompleteBeforeReturn() throws Exception {
        for (int status : new int[] {200, 404, 410}) {
            resume();
            deleteStatus = status;
            int count = requests.size();
            client.command("stop");
            client.stopAndWait(1000);
            assertEquals(count + 1, requests.size());
            assertEquals("DELETE", requests.get(count).getRequestMethod());
            assertEquals(false, get("hosting"));
        }
    }

    @Test public void slowRemovalHasBoundedWaitAndCanFinishAfterTimeout() throws Exception {
        resume();
        removalGate = new CountDownLatch(1);
        long started = System.nanoTime();
        client.stopAndWait(50);
        assertTrue(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - started) < 1000);
        removalGate.countDown();
        executor.submit(() -> {}).get(2, TimeUnit.SECONDS);
        assertEquals(false, get("hosting"));
        assertEquals(1, requests.size());
    }

    private static final class FakeConnection extends HttpURLConnection {
        final ByteArrayOutputStream body = new ByteArrayOutputStream();
        FakeConnection(URL url) { super(url); }
        @Override public void connect() {}
        @Override public void disconnect() {}
        @Override public boolean usingProxy() { return false; }
        @Override public OutputStream getOutputStream() { return body; }
        @Override public int getResponseCode() throws java.io.IOException {
            if ("DELETE".equals(method) && removalGate != null) {
                try { removalGate.await(); }
                catch (InterruptedException e) { throw new java.io.IOException(e); }
            }
            return "PUT".equals(method) ? putStatus : "GET".equals(method) ? getStatus :
                "DELETE".equals(method) ? deleteStatus : postStatus;
        }
        @Override public InputStream getErrorStream() {
            return new ByteArrayInputStream(failureBody.getBytes(StandardCharsets.UTF_8));
        }
        @Override public InputStream getInputStream() {
            String json = "GET".equals(method) ? "{\"requests\":[]}" :
                "POST".equals(method) ? "{\"code\":\"ABCDEFGH\",\"ownerToken\":\"owner-token\"}" : "{}";
            return new ByteArrayInputStream(json.getBytes(StandardCharsets.UTF_8));
        }
    }
}
