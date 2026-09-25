package com.gevr.port;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Enumeration;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * The launcher's Mods page (port/vr/vr_launcher.cpp, through
 * MainActivity.modsStatus and MainActivity.modsCommand): fan-made texture
 * packs, downloaded from their authors' own sites when the player asks and
 * unpacked into files/texture-packs/&lt;id&gt;, where the renderer looks for
 * them (port/fast3d/gevr_texpack.cpp). Nothing of theirs ships with the app;
 * the list below only says where each pack lives.
 *
 * One job at a time on one worker thread; the launcher polls status().
 */
final class ModManager {
    private static final String TAG = "GEVR-Mods";
    private static final String MARKER = ".gevr-pack";

    static final class Pack {
        final String id, title, by, site, version, url;
        final long size;

        Pack(String id, String title, String by, String site, String version, String url, long size) {
            this.id = id;
            this.title = title;
            this.by = by;
            this.site = site;
            this.version = version;
            this.url = url;
            this.size = size;
        }
    }

    /*
     * GLideN64 "Rice" packs: PNGs named GOLDENEYE#<crc>#<fmt>#<siz>..., which
     * the renderer matches by the same checksum the emulator uses. The HD
     * build (135 MB) rather than the 4K one: the 4K pack is 1.6 GB of textures
     * the Quest would only shrink again.
     */
    static final Pack[] PACKS = {
        new Pack("ge007-hd", "GoldenEye 007 HD", "intermissionfb and GhostlyDark", "evilgames.eu",
                "v2025.12.30",
                "https://evilgames.eu/files/texture-packs/ge007-hd-v2025.12.30-gliden64-png-hd.zip",
                135256079L),
    };

    // Per-pack states the launcher shows (vr_launcher.cpp keeps the same names).
    static final String NONE = "none";
    static final String DOWNLOADING = "downloading";
    static final String INSTALLING = "installing";
    static final String INSTALLED = "installed";
    static final String ERROR = "error";

    private final Context app;
    private final String userAgent;
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "mods");
        t.setDaemon(true);
        return t;
    });

    private volatile String busyId;          // the pack being downloaded or installed
    private volatile String busyState = NONE;
    private volatile int progress = -1;
    private volatile boolean cancelled;
    private volatile String errorId;
    private volatile String errorMessage = "";

    ModManager(Context context, String appVersion) {
        this.app = context.getApplicationContext();
        this.userAgent = "GoldenEye-VR/" + appVersion + " (Android)";
        // a download or unpack the last run didn't finish
        worker.execute(this::tidy);
    }

    private File root() {
        File base = app.getExternalFilesDir(null);
        return new File(base != null ? base : app.getFilesDir(), "texture-packs");
    }

    private static Pack find(String id) {
        for (Pack p : PACKS) {
            if (p.id.equals(id)) return p;
        }
        return null;
    }

    private boolean installed(Pack p) {
        return new File(new File(root(), p.id), MARKER).isFile();
    }

    /**
     * One line per pack: id \t title \t by \t site \t version \t size MB \t
     * state \t progress \t message.
     */
    String status() {
        StringBuilder sb = new StringBuilder();
        for (Pack p : PACKS) {
            String state;
            String msg = "";
            int pr = -1;
            if (p.id.equals(busyId)) {
                state = busyState;
                pr = progress;
            } else if (p.id.equals(errorId)) {
                state = ERROR;
                msg = errorMessage;
            } else {
                state = installed(p) ? INSTALLED : NONE;
            }
            if (sb.length() > 0) sb.append('\n');
            sb.append(p.id).append('\t').append(clean(p.title)).append('\t').append(clean(p.by)).append('\t')
                    .append(p.site).append('\t').append(p.version).append('\t')
                    .append((p.size + (1 << 19)) >> 20).append('\t')
                    .append(state).append('\t').append(pr).append('\t').append(clean(msg));
        }
        return sb.toString();
    }

    /** "install:<id>", "remove:<id>", "cancel". */
    synchronized void command(String cmd) {
        if (cmd == null) return;
        if (cmd.equals("cancel")) {
            cancelled = true;
            return;
        }
        int colon = cmd.indexOf(':');
        if (colon < 0) return;
        final Pack p = find(cmd.substring(colon + 1));
        if (p == null || busyId != null) return;
        final String verb = cmd.substring(0, colon);
        if (verb.equals("install")) {
            errorId = null;
            cancelled = false;
            progress = 0;
            busyState = DOWNLOADING;
            busyId = p.id;
            worker.execute(() -> run(p, () -> install(p)));
        } else if (verb.equals("remove")) {
            errorId = null;
            progress = -1;
            busyState = INSTALLING;
            busyId = p.id;
            worker.execute(() -> run(p, () -> {
                deleteTree(new File(root(), p.id));
                Log.i(TAG, "removed " + p.id);
            }));
        }
    }

    private interface Job {
        void run() throws Exception;
    }

    private void run(Pack p, Job job) {
        try {
            job.run();
        } catch (Exception e) {
            if (cancelled) {
                Log.i(TAG, p.id + " cancelled");
            } else {
                Log.w(TAG, p.id + " failed", e);
                errorMessage = reason(e);
                errorId = p.id;
            }
        } finally {
            progress = -1;
            busyState = NONE;
            busyId = null;
        }
    }

    // ---- install -----------------------------------------------------------------

    private void install(Pack p) throws Exception {
        File root = root();
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("could not create " + root);
        // the zip and the unpacked files side by side, with room to spare
        long need = p.size * 23 / 10;
        if (root.getUsableSpace() < need) {
            throw new IOException("not enough free space (" + (need >> 20) + " MB needed)");
        }
        File zip = new File(root, "." + p.id + ".zip");
        File tmp = new File(root, "." + p.id + ".unpacking");
        File dest = new File(root, p.id);
        try {
            download(p, zip);
            busyState = INSTALLING;
            progress = 0;
            deleteTree(tmp);
            int n = unpack(zip, tmp);
            if (n < 100) throw new IOException("the download holds " + n + " textures, not a texture pack");
            try (OutputStream o = new FileOutputStream(new File(tmp, MARKER))) {
                o.write((p.version + "\n").getBytes("UTF-8"));
            }
            deleteTree(dest);
            if (!tmp.renameTo(dest)) throw new IOException("could not finish installing");
            Log.i(TAG, "installed " + p.id + ": " + n + " textures");
        } finally {
            zip.delete();
            deleteTree(tmp);
        }
    }

    private void download(Pack p, File out) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(p.url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", userAgent);
        try {
            int code = c.getResponseCode();
            if (code != 200) throw new IOException(p.site + " answered HTTP " + code);
            long got = 0;
            try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(out)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) {
                    if (cancelled) throw new IOException("cancelled");
                    o.write(buf, 0, n);
                    got += n;
                    if (got > p.size) throw new IOException("the download is bigger than expected");
                    progress = (int) Math.min(99, got * 100 / p.size);
                }
            }
            // the size the list was made with: a changed file is not the pack we checked
            if (got != p.size) {
                throw new IOException("the download is " + got + " bytes, expected " + p.size
                        + " (the pack may have been updated)");
            }
        } finally {
            c.disconnect();
        }
    }

    /** The zip's PNGs, paths kept; ZipFile checks each entry's CRC as it reads. */
    private int unpack(File zip, File dir) throws IOException {
        String base = dir.getCanonicalPath() + File.separator;
        int count = 0;
        try (ZipFile z = new ZipFile(zip)) {
            int total = z.size();
            int seen = 0;
            Enumeration<? extends ZipEntry> it = z.entries();
            byte[] buf = new byte[1 << 16];
            while (it.hasMoreElements()) {
                if (cancelled) throw new IOException("cancelled");
                ZipEntry e = it.nextElement();
                seen++;
                progress = total > 0 ? Math.min(99, seen * 100 / total) : -1;
                String name = e.getName();
                if (e.isDirectory() || !name.toLowerCase().endsWith(".png")) continue;
                File f = new File(dir, name);
                // no entry may land outside the pack's folder
                if (!f.getCanonicalPath().startsWith(base)) throw new IOException("bad path in the zip: " + name);
                File parent = f.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new IOException("could not create " + parent);
                }
                try (InputStream in = z.getInputStream(e); OutputStream o = new FileOutputStream(f)) {
                    int n;
                    while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
                }
                count++;
            }
        }
        return count;
    }

    // ---- helpers -------------------------------------------------------------------

    private void tidy() {
        File[] list = root().listFiles();
        if (list == null) return;
        for (File f : list) {
            if (f.getName().startsWith(".")) deleteTree(f);
        }
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null) {
            for (File k : kids) deleteTree(k);
        }
        if (!f.delete()) Log.w(TAG, "could not delete " + f);
    }

    private static String reason(Exception e) {
        String m = e.getMessage();
        if (e instanceof java.net.UnknownHostException) return "no internet connection";
        if (e instanceof java.net.SocketTimeoutException) return "the connection timed out";
        return m != null ? m : e.getClass().getSimpleName();
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ');
    }
}
