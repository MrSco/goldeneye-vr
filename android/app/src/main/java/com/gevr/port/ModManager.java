package com.gevr.port;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
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
 * packs, downloaded when the player asks (from their authors' site, or our
 * GitHub fork for the AI-filled one) and
 * unpacked into files/texture-packs/&lt;id&gt;, where the renderer looks for
 * them (port/fast3d/gevr_texpack.cpp). Nothing of theirs ships with the app;
 * the list below only says where each pack lives.
 *
 * One job at a time on one worker thread; the launcher polls status().
 *
 * A pack can follow a GitHub repository's releases. When the Mods page opens
 * ("check", once a run) the latest release is looked up; a release file other
 * than the version installed is offered as an update, and a new install takes
 * it. Pack updates then need no app release.
 */
final class ModManager {
    private static final String TAG = "GEVR-Mods";
    private static final String MARKER = ".gevr-pack";

    /** A version of a pack's zip: the list's own, or a newer release found on GitHub. */
    static final class Build {
        final String version, url;
        final long size;

        Build(String version, String url, long size) {
            this.version = version;
            this.url = url;
            this.size = size;
        }
    }

    static final class Pack {
        final String id, title, by, site;
        final Build listed;
        // GitHub releases to follow (repo null: none). The release's tag, less
        // tagPrefix and with '-' read as '.', is the version; its file is the
        // asset named assetPrefix...assetSuffix.
        final String repo, tagPrefix, assetPrefix, assetSuffix;
        volatile Build latest;

        Pack(String id, String title, String by, String site, String version, String url, long size,
             String repo, String tagPrefix, String assetPrefix, String assetSuffix) {
            this.id = id;
            this.title = title;
            this.by = by;
            this.site = site;
            this.listed = new Build(version, url, size);
            this.repo = repo;
            this.tagPrefix = tagPrefix;
            this.assetPrefix = assetPrefix;
            this.assetSuffix = assetSuffix;
        }

        /** What an install gets: a release newer than the list's, or the list's. */
        Build target() {
            Build l = latest;
            return l != null && !sameVersion(l.version, listed.version) ? l : listed;
        }
    }

    /*
     * GLideN64 "Rice" packs: PNGs named GOLDENEYE#<crc>#<fmt>#<siz>..., which
     * the renderer matches by the same checksum the emulator uses. The HD
     * build (135 MB) rather than the 4K one: the 4K pack is 1.6 GB of textures
     * the Quest would only shrink again.
     *
     * "+ AI" is our fork of it (github.com/MrSco/GoldenEye-007-HD): their latest
     * textures at their HD sizes, plus GOLDENEYE/AI - AI upscales of the game's
     * own textures for the ones the pack lacks (tools/texai, package.py).
     */
    static final Pack[] PACKS = {
        // the authors' own GitHub releases carry the same zip (tag v2025-12-30)
        new Pack("ge007-hd", "GoldenEye 007 HD", "intermissionfb and GhostlyDark", "evilgames.eu",
                "v2025.12.30",
                "https://evilgames.eu/files/texture-packs/ge007-hd-v2025.12.30-gliden64-png-hd.zip",
                135256079L,
                "GhostlyDark/GoldenEye-007-HD", "", "ge007-hd-", "-gliden64-png-hd.zip"),
        new Pack("ge007-hd-ai", "GoldenEye 007 HD + AI", "intermissionfb and GhostlyDark, AI by GoldenEye VR",
                "github.com/MrSco", "2026.09.29.2",
                "https://github.com/MrSco/GoldenEye-007-HD/releases/download/ai-2026.09.29.2/ge007-hd-ai-2026.09.29.2-gliden64-png.zip",
                373969887L,
                "MrSco/GoldenEye-007-HD", "ai-", "ge007-hd-ai-", "-gliden64-png.zip"),
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
    private boolean checked;                 // the latest releases were looked up this run

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

    /** The version an install wrote into the pack's marker ("" if none). */
    private String installedVersion(Pack p) {
        File m = new File(new File(root(), p.id), MARKER);
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(m), "UTF-8"))) {
            String line = r.readLine();
            return line != null ? line.trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    static boolean sameVersion(String a, String b) {
        return a != null && b != null && a.replace('-', '.').equalsIgnoreCase(b.replace('-', '.'));
    }

    /**
     * One line per pack: id \t title \t by \t site \t version \t size MB \t
     * state \t progress \t message \t update version. The version is the one
     * installed, else the one an install would get; the size is the download's.
     * The update version is set when a newer release than the installed one is
     * out (then size is its download).
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
                // a failed update leaves the installed pack in place (and usable)
                state = installed(p) ? INSTALLED : ERROR;
                msg = errorMessage;
            } else {
                state = installed(p) ? INSTALLED : NONE;
            }
            Build t = p.target();
            String version = t.version, update = "";
            if (installed(p)) {
                // what an install would get now (the list's, or a newer release)
                // against what the marker says is installed
                String have = installedVersion(p);
                if (!have.isEmpty()) {
                    if (!sameVersion(t.version, have)) update = t.version;
                    version = have;
                }
            }
            if (sb.length() > 0) sb.append('\n');
            sb.append(p.id).append('\t').append(clean(p.title)).append('\t').append(clean(p.by)).append('\t')
                    .append(p.site).append('\t').append(clean(version)).append('\t')
                    .append((t.size + (1 << 19)) >> 20).append('\t')
                    .append(state).append('\t').append(pr).append('\t').append(clean(msg)).append('\t')
                    .append(clean(update));
        }
        return sb.toString();
    }

    /** "check" (once a run), "install:<id>" (also updates), "remove:<id>", "cancel". */
    synchronized void command(String cmd) {
        if (cmd == null) return;
        if (cmd.equals("cancel")) {
            cancelled = true;
            return;
        }
        if (cmd.equals("check")) {
            if (!checked) {
                checked = true;
                worker.execute(this::checkLatest);
            }
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
        final Build b = p.target();
        File root = root();
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("could not create " + root);
        // the zip and the unpacked files side by side (with an old version of the
        // pack still there during an update), with room to spare
        long need = b.size * 23 / 10;
        if (root.getUsableSpace() < need) {
            throw new IOException("not enough free space (" + (need >> 20) + " MB needed)");
        }
        File zip = new File(root, "." + p.id + ".zip");
        File tmp = new File(root, "." + p.id + ".unpacking");
        File dest = new File(root, p.id);
        try {
            download(p, b, zip);
            busyState = INSTALLING;
            progress = 0;
            deleteTree(tmp);
            int n = unpack(zip, tmp);
            if (n < 100) throw new IOException("the download holds " + n + " textures, not a texture pack");
            try (OutputStream o = new FileOutputStream(new File(tmp, MARKER))) {
                o.write((b.version + "\n").getBytes("UTF-8"));
            }
            deleteTree(dest);
            if (!tmp.renameTo(dest)) throw new IOException("could not finish installing");
            Log.i(TAG, "installed " + p.id + " " + b.version + ": " + n + " textures");
        } finally {
            zip.delete();
            deleteTree(tmp);
        }
    }

    private void download(Pack p, Build b, File out) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(b.url).openConnection();
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
                    if (got > b.size) throw new IOException("the download is bigger than expected");
                    progress = (int) Math.min(99, got * 100 / b.size);
                }
            }
            // the size the list (or the release) gave: a changed file is not the pack we checked
            if (got != b.size) {
                throw new IOException("the download is " + got + " bytes, expected " + b.size
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

    // ---- updates ------------------------------------------------------------------

    /** Each followed pack's latest GitHub release and its file; a failure only logs. */
    private void checkLatest() {
        for (Pack p : PACKS) {
            if (p.repo == null) continue;
            try {
                JSONObject o = new JSONObject(httpGet("https://api.github.com/repos/" + p.repo + "/releases/latest"));
                String tag = o.optString("tag_name", "");
                if (tag.startsWith(p.tagPrefix)) tag = tag.substring(p.tagPrefix.length());
                String version = tag.replace('-', '.');
                JSONArray assets = o.optJSONArray("assets");
                for (int i = 0; assets != null && i < assets.length(); i++) {
                    JSONObject a = assets.getJSONObject(i);
                    String name = a.optString("name", "");
                    long size = a.optLong("size", -1);
                    String url = a.optString("browser_download_url", null);
                    if (name.startsWith(p.assetPrefix) && name.endsWith(p.assetSuffix) && size > 0 && url != null) {
                        p.latest = new Build(version, url, size);
                        break;
                    }
                }
                Log.i(TAG, p.id + ": latest release " + (p.latest != null ? p.latest.version : "has no pack file"));
            } catch (Exception e) {
                Log.w(TAG, p.id + ": could not look for a newer release: " + reason(e));
            }
        }
    }

    private String httpGet(String url) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(20000);
        c.setRequestProperty("Accept", "application/vnd.github+json");
        c.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
        c.setRequestProperty("User-Agent", userAgent);
        try {
            int code = c.getResponseCode();
            if (code != 200) throw new IOException("GitHub answered HTTP " + code);
            try (InputStream in = c.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[1 << 14];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                return out.toString("UTF-8");
            }
        } finally {
            c.disconnect();
        }
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
