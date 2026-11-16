package com.gevr.port;

import android.util.Base64;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.concurrent.atomic.AtomicBoolean;

import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * The lobby Worker's push signaling socket (GET /v1/lobbies/:code/socket): a small RFC 6455
 * client for text frames, with its own reader thread. The lobby's messages are a few KB of
 * JSON, so fragmentation, extensions and binary frames are not needed. Listener calls come
 * on the reader thread; closed() comes once, whatever ended the socket.
 */
final class LobbySocket {
    interface Listener {
        void message(String text);
        void closed(String why);
    }

    private static final int MAX_MESSAGE = 65536;
    private static final SecureRandom RANDOM = new SecureRandom();
    private final Socket socket;
    private final OutputStream output;
    private final Listener listener;
    private final AtomicBoolean done = new AtomicBoolean();

    private LobbySocket(Socket socket, Listener listener) throws IOException {
        this.socket = socket;
        this.output = socket.getOutputStream();
        this.listener = listener;
    }

    /**
     * Connects and upgrades (blocking, up to connectMs each for the TCP connect and the
     * handshake); throws if the Worker answers anything but 101. readMs is how long the
     * socket may stay silent before it is given up: the caller pings well inside it.
     */
    static LobbySocket open(String url, String token, Listener listener, int connectMs, int readMs) throws IOException {
        URI uri = URI.create(url);
        boolean tls = "wss".equals(uri.getScheme());
        String host = uri.getHost();
        int port = uri.getPort() > 0 ? uri.getPort() : (tls ? 443 : 80);
        Socket socket;
        if (tls) {
            Socket plain = new Socket();
            plain.connect(new InetSocketAddress(host, port), connectMs);
            SSLSocket wrapped = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault()).createSocket(plain, host, port, true);
            SSLParameters parameters = wrapped.getSSLParameters();
            parameters.setEndpointIdentificationAlgorithm("HTTPS");   // the certificate must name the host
            wrapped.setSSLParameters(parameters);
            wrapped.setSoTimeout(connectMs);
            wrapped.startHandshake();
            socket = wrapped;
        } else {
            socket = new Socket();
            socket.connect(new InetSocketAddress(host, port), connectMs);
            socket.setSoTimeout(connectMs);
        }
        try {
            byte[] nonce = new byte[16];
            RANDOM.nextBytes(nonce);
            String key = Base64.encodeToString(nonce, Base64.NO_WRAP);
            String path = uri.getRawPath() + (uri.getRawQuery() != null ? "?" + uri.getRawQuery() : "");
            String request = "GET " + path + " HTTP/1.1\r\n"
                    + "Host: " + host + (uri.getPort() > 0 ? ":" + port : "") + "\r\n"
                    + "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                    + "Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n"
                    + (token.isEmpty() ? "" : "Authorization: Bearer " + token + "\r\n")
                    + "User-Agent: GoldenEyeVR\r\n\r\n";
            OutputStream out = socket.getOutputStream();
            out.write(request.getBytes(StandardCharsets.UTF_8));
            out.flush();
            InputStream in = new BufferedInputStream(socket.getInputStream());
            String status = readLine(in);
            String accept = null;
            for (String line = readLine(in); !line.isEmpty(); line = readLine(in)) {
                int colon = line.indexOf(':');
                if (colon > 0 && line.substring(0, colon).trim().equalsIgnoreCase("Sec-WebSocket-Accept"))
                    accept = line.substring(colon + 1).trim();
            }
            if (!status.startsWith("HTTP/1.1 101")) throw new IOException("Upgrade refused: " + status);
            String expected = Base64.encodeToString(MessageDigest.getInstance("SHA-1").digest(
                    (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").getBytes(StandardCharsets.US_ASCII)), Base64.NO_WRAP);
            if (!expected.equals(accept)) throw new IOException("Bad upgrade reply");
            socket.setSoTimeout(readMs);
            LobbySocket result = new LobbySocket(socket, listener);
            Thread reader = new Thread(() -> result.read(in), "lobby-socket");
            reader.setDaemon(true);
            reader.start();
            return result;
        } catch (IOException e) {
            socket.close();
            throw e;
        } catch (Exception e) {
            socket.close();
            throw new IOException(e.getMessage(), e);
        }
    }

    /** False once the socket is closed or broken (the caller then polls). */
    boolean isOpen() { return !done.get(); }

    void send(String text) throws IOException {
        if (done.get()) throw new IOException("Socket closed");
        write(0x1, text.getBytes(StandardCharsets.UTF_8));
    }

    void close() {
        if (done.get()) return;
        try { write(0x8, new byte[] { 0x03, (byte) 0xE8 }); } catch (IOException ignored) { }   // 1000, normal
        finish("closed here");
    }

    private synchronized void write(int opcode, byte[] payload) throws IOException {
        int n = payload.length;
        ByteArrayOutputStream frame = new ByteArrayOutputStream(n + 14);
        frame.write(0x80 | opcode);
        if (n < 126) frame.write(0x80 | n);
        else { frame.write(0x80 | 126); frame.write(n >> 8); frame.write(n); }   // MAX_MESSAGE fits in 16 bits
        byte[] mask = new byte[4];
        RANDOM.nextBytes(mask);
        frame.write(mask, 0, 4);
        for (int i = 0; i < n; i++) frame.write(payload[i] ^ mask[i & 3]);
        try {
            output.write(frame.toByteArray());
            output.flush();
        } catch (IOException e) {
            finish("send failed: " + e.getMessage());
            throw e;
        }
    }

    private void read(InputStream stream) {
        DataInputStream in = new DataInputStream(stream);
        ByteArrayOutputStream text = new ByteArrayOutputStream();
        try {
            while (!done.get()) {
                int b0 = in.readUnsignedByte(), b1 = in.readUnsignedByte();
                int opcode = b0 & 0x0F;
                long length = b1 & 0x7F;
                if (length == 126) length = in.readUnsignedShort();
                else if (length == 127) length = in.readLong();
                if (length < 0 || length > MAX_MESSAGE) throw new IOException("Frame too large");
                byte[] mask = null;
                if ((b1 & 0x80) != 0) { mask = new byte[4]; in.readFully(mask); }
                byte[] payload = new byte[(int) length];
                in.readFully(payload);
                if (mask != null) for (int i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
                switch (opcode) {
                    case 0x0: case 0x1:   // continuation, text
                        text.write(payload, 0, payload.length);
                        if (text.size() > MAX_MESSAGE) throw new IOException("Message too large");
                        if ((b0 & 0x80) != 0) {
                            String message = text.toString("UTF-8");
                            text.reset();
                            listener.message(message);
                        }
                        break;
                    case 0x8: {   // close: answer it and stop
                        int code = payload.length >= 2 ? ((payload[0] & 0xFF) << 8) | (payload[1] & 0xFF) : 1005;
                        String reason = payload.length > 2 ? new String(payload, 2, payload.length - 2, StandardCharsets.UTF_8) : "";
                        try { write(0x8, payload.length >= 2 ? new byte[] { payload[0], payload[1] } : new byte[0]); } catch (IOException ignored) { }
                        finish("closed by the service: " + code + (reason.isEmpty() ? "" : " " + reason));
                        return;
                    }
                    case 0x9: write(0xA, payload); break;   // ping: pong
                    default: break;                        // pong, binary: not used
                }
            }
        } catch (EOFException e) {
            finish("connection ended");
        } catch (Exception e) {
            finish(e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage());
        }
    }

    private void finish(String why) {
        if (!done.compareAndSet(false, true)) return;
        try { socket.close(); } catch (IOException ignored) { }
        listener.closed(why);
    }

    private static String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream line = new ByteArrayOutputStream();
        for (int c; (c = in.read()) != '\n'; ) {
            if (c < 0) throw new EOFException("Connection ended during the upgrade");
            if (c != '\r') line.write(c);
            if (line.size() > 8192) throw new IOException("Upgrade reply too long");
        }
        return line.toString("UTF-8");
    }
}
