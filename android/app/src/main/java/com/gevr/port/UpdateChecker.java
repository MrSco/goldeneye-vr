package com.gevr.port;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import androidx.core.content.ContextCompat;
import androidx.core.content.FileProvider;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Update check against this repo's GitHub releases, for the in-VR launcher
 * (port/vr/vr_launcher.cpp talks to it through MainActivity.updaterStatus and
 * MainActivity.updaterCommand).
 *
 * Check: GET /repos/MrSco/goldeneye-vr/releases, pick the newest release (and,
 * with "test builds" on, pre-release) whose tag is newer than this app's
 * versionName and which has an APK attached (UpdateVersion.pick).
 *
 * Update: download the APK into files/updates, check its size and GitHub's
 * SHA-256 digest, check that it is this package signed with this app's key
 * (Android refuses anything else, and the only way round that - uninstalling -
 * deletes the player's ROM), then hand it to the system installer. The
 * installer's confirmation is a 2D panel; MainActivity brings the app back to
 * VR after it, as it does after the ROM picker. A successful update ends this
 * process; the next launch notices the new version and says so.
 *
 * Nothing here blocks the caller: network and file work run on one worker
 * thread and the launcher polls the status every frame.
 */
final class UpdateChecker {
    private static final String TAG = "GEVR-Update";
    private static final String REPO = "MrSco/goldeneye-vr";
    private static final String API = "https://api.github.com/repos/" + REPO + "/releases?per_page=20";
    private static final String ACTION_INSTALL_STATUS = "com.gevr.port.UPDATE_INSTALL_STATUS";
    private static final String PREFS = "updater";
    private static final String PREF_TEST_BUILDS = "testBuilds";
    private static final String PREF_PENDING = "pendingVersion";
    private static final long MAX_APK = 512L << 20;

    // States the launcher shows (vr_launcher.cpp keeps the same names).
    static final String IDLE = "idle";
    static final String CHECKING = "checking";
    static final String UP_TO_DATE = "uptodate";
    static final String AVAILABLE = "available";
    static final String DOWNLOADING = "downloading";
    static final String NEEDS_PERMISSION = "permission";
    static final String INSTALLING = "installing";
    static final String ERROR = "error";

    private final MainActivity activity;
    private final Context app;
    private final SharedPreferences prefs;
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "updater");
        t.setDaemon(true);
        return t;
    });

    private volatile String state = IDLE;
    private volatile String message = "";
    private volatile int progress = -1;           // percent while downloading, else -1
    private volatile UpdateVersion.Release offer;   // what "Update" installs
    private volatile File downloaded;              // verified APK for `offer`, if any
    private volatile String notice = "";           // "Updated to v0.1.13.", shown while nothing else is
    private volatile int session = -1;             // PackageInstaller session awaiting its result
    private volatile boolean cancelled;            // the launcher's Cancel, for a running download
    private volatile boolean triedInstallerActivity; // fallback route used for this install
    private volatile File installingApk;            // the APK the current install is for
    private final String installed;

    UpdateChecker(MainActivity activity) {
        this.activity = activity;
        this.app = activity.getApplicationContext();
        this.prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        this.installed = installedVersion();

        // Did the last run install an update? Say so once, and tidy up.
        String pending = prefs.getString(PREF_PENDING, null);
        if (pending != null) {
            if (UpdateVersion.compare(installed, pending) >= 0) {
                notice = "Updated to v" + UpdateVersion.normalize(installed) + ".";
            }
            prefs.edit().remove(PREF_PENDING).apply();
        }
        worker.execute(this::deleteDownloads);

        // Not exported on every API level (ContextCompat guards older ones
        // with a signature permission): only the installer's PendingIntent,
        // sent as this app, may reach it. Otherwise any app could hand us an
        // Intent to start as ourselves.
        ContextCompat.registerReceiver(app, installStatus, new IntentFilter(ACTION_INSTALL_STATUS),
                ContextCompat.RECEIVER_NOT_EXPORTED);
    }

    // ---- what the launcher reads and does -----------------------------------

    /** state \t offered version \t progress \t message \t test builds (0/1) \t installed version */
    String status() {
        UpdateVersion.Release o = offer;
        String s = state, m = message;
        if (m.isEmpty() && (IDLE.equals(s) || CHECKING.equals(s) || UP_TO_DATE.equals(s))) m = notice;
        return s + "\t" + (o != null ? UpdateVersion.normalize(o.tag) : "") + "\t" + progress + "\t"
                + clean(m) + "\t" + (testBuilds() ? 1 : 0) + "\t" + UpdateVersion.normalize(installed);
    }

    // Serialised: the launcher's thread and the UI thread (activity results,
    // install status) both drive the updater.
    synchronized void command(String cmd) {
        if (cmd == null) return;
        switch (cmd) {
            case "check":
                check();
                break;
            case "update":
                update();
                break;
            case "retry":
                if (offer != null) update();
                else check();
                break;
            case "cancel":
                cancel();
                break;
            case "testbuilds:1":
            case "testbuilds:0":
                prefs.edit().putBoolean(PREF_TEST_BUILDS, cmd.endsWith("1")).apply();
                check();
                break;
            default:
                Log.w(TAG, "unknown command " + cmd);
        }
    }

    /** MainActivity: back from the "install unknown apps" settings page. */
    synchronized void onPermissionPageClosed() {
        if (!NEEDS_PERMISSION.equals(state)) return;
        if (canInstall()) {
            message = "";
            update();
        } else {
            setState(AVAILABLE, "Allow \"Install unknown apps\" first.");
        }
    }

    // ---- check -----------------------------------------------------------------

    private boolean testBuilds() {
        return prefs.getBoolean(PREF_TEST_BUILDS, false);
    }

    private void check() {
        if (busy()) return;
        setState(CHECKING, "");
        worker.execute(() -> {
            try {
                List<UpdateVersion.Release> releases = parseReleases(httpGet(API));
                UpdateVersion.Release r = UpdateVersion.pick(releases, installed, testBuilds());
                if (r == null) {
                    offer = null;
                    setState(UP_TO_DATE, "");
                } else {
                    if (offer == null || !r.tag.equals(offer.tag)) downloaded = null;
                    offer = r;
                    setState(AVAILABLE, r.prerelease ? "Test build." : "");
                }
                Log.i(TAG, "check: installed " + installed + ", offer " + (r == null ? "none" : r.tag));
            } catch (Exception e) {
                Log.w(TAG, "check failed", e);
                setState(ERROR, "Could not check for updates: " + reason(e));
            }
        });
    }

    private static List<UpdateVersion.Release> parseReleases(String json) throws Exception {
        JSONArray arr = new JSONArray(json);
        List<UpdateVersion.Release> out = new ArrayList<>();
        for (int i = 0; i < arr.length(); i++) {
            JSONObject o = arr.getJSONObject(i);
            UpdateVersion.Release r = new UpdateVersion.Release();
            r.tag = o.optString("tag_name", null);
            r.prerelease = o.optBoolean("prerelease", false);
            r.draft = o.optBoolean("draft", false);
            r.notes = o.optString("body", "");
            JSONArray assets = o.optJSONArray("assets");
            for (int j = 0; assets != null && j < assets.length(); j++) {
                JSONObject a = assets.getJSONObject(j);
                String name = a.optString("name", "");
                if (!UpdateVersion.isApkAsset(name)) continue;
                // the project's own APK wins over anything else attached
                if (r.apkUrl != null && !UpdateVersion.isPreferredApk(name)) continue;
                r.apkName = name;
                r.apkUrl = a.optString("browser_download_url", null);
                r.apkSize = a.optLong("size", -1);
                String digest = a.optString("digest", "");
                r.apkSha256 = digest.startsWith("sha256:") ? digest.substring(7).toLowerCase() : null;
                if (UpdateVersion.isPreferredApk(name)) break;
            }
            out.add(r);
        }
        return out;
    }

    // ---- update ----------------------------------------------------------------

    private void update() {
        final UpdateVersion.Release r = offer;
        if (r == null || busy()) return;
        if (!canInstall()) {
            askForInstallPermission();
            return;
        }
        notice = "";
        final File ready = downloaded;
        if (ready != null && ready.isFile()) {
            // busy from this moment, so a second press cannot start a second session
            setState(INSTALLING, "Confirm the update in the window that opened.");
            worker.execute(() -> install(ready, r));
            return;
        }
        cancelled = false;
        setState(DOWNLOADING, "");
        progress = 0;
        worker.execute(() -> {
            try {
                File apk = download(r);
                verify(apk, r);
                downloaded = apk;
                install(apk, r);
            } catch (Exception e) {
                progress = -1;
                if (cancelled) {
                    setState(AVAILABLE, "Update cancelled.");
                } else {
                    Log.w(TAG, "update failed", e);
                    setState(ERROR, reason(e));
                }
            }
        });
    }

    // The launcher's Cancel: a download stops, a prompt that never answered
    // (closed from the shell, no result delivered) stops being waited for.
    private void cancel() {
        if (DOWNLOADING.equals(state)) {
            cancelled = true;
            return;
        }
        if (INSTALLING.equals(state) || NEEDS_PERMISSION.equals(state)) {
            int id = session;
            session = -1;
            if (id >= 0) {
                try {
                    app.getPackageManager().getPackageInstaller().abandonSession(id);
                } catch (Exception e) {
                    Log.w(TAG, "abandon session " + id, e);
                }
            }
            prefs.edit().remove(PREF_PENDING).apply();
            setState(offer != null ? AVAILABLE : IDLE, "Update cancelled.");
        }
    }

    private File download(UpdateVersion.Release r) throws IOException {
        File dir = updatesDir();
        String name = r.apkName != null ? r.apkName.replaceAll("[^A-Za-z0-9._-]", "_") : "update.apk";
        File part = new File(dir, name + ".part");
        File apk = new File(dir, name);
        HttpURLConnection c = open(r.apkUrl, "application/octet-stream");
        try {
            int code = c.getResponseCode();
            if (code != 200) throw new IOException("download failed (HTTP " + code + ")");
            long total = r.apkSize > 0 ? r.apkSize : c.getContentLengthLong();
            long got = 0;
            try (InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(part)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) {
                    if (cancelled) throw new IOException("cancelled");
                    out.write(buf, 0, n);
                    got += n;
                    if (got > MAX_APK) throw new IOException("download is too large");
                    if (total > 0) progress = (int) Math.min(99, got * 100 / total);
                }
            }
            if (r.apkSize > 0 && got != r.apkSize) {
                throw new IOException("download incomplete (" + got + " of " + r.apkSize + " bytes)");
            }
        } catch (IOException e) {
            part.delete();
            throw e;
        } finally {
            c.disconnect();
        }
        apk.delete();
        if (!part.renameTo(apk)) {
            part.delete();
            throw new IOException("could not save the download");
        }
        progress = 100;
        return apk;
    }

    /** Same bytes GitHub has, this package, a version Android will accept, our signing key. */
    private void verify(File apk, UpdateVersion.Release r) throws Exception {
        if (r.apkSha256 != null) {
            String got = sha256(apk);
            if (!got.equalsIgnoreCase(r.apkSha256)) {
                apk.delete();
                throw new IOException("the download is damaged (checksum mismatch)");
            }
        }
        PackageManager pm = app.getPackageManager();
        String pkg = app.getPackageName();
        PackageInfo mine = pm.getPackageInfo(pkg, signingFlags());
        PackageInfo theirs = pm.getPackageArchiveInfo(apk.getAbsolutePath(), signingFlags());
        if (theirs == null) {
            apk.delete();
            throw new IOException("the download is not a valid APK");
        }
        if (!pkg.equals(theirs.packageName)) {
            apk.delete();
            throw new IOException("the download is a different app (" + theirs.packageName + ")");
        }
        if (versionCode(theirs) < versionCode(mine)) {
            apk.delete();
            throw new IOException("that build is older than the one installed (versionCode "
                    + versionCode(theirs) + " < " + versionCode(mine) + ")");
        }
        if (!sameSigner(mine, theirs)) {
            apk.delete();
            throw new IOException("v" + UpdateVersion.normalize(r.tag) + " is signed with a different key than "
                    + "this copy, so Android would refuse it. This happens with debug builds; "
                    + "install the release with SideQuest instead (don't uninstall first: that deletes your ROM).");
        }
    }

    private void install(File apk, UpdateVersion.Release r) {
        setState(INSTALLING, "Confirm the update in the window that opened.");
        progress = -1;
        installingApk = apk;
        triedInstallerActivity = false;
        prefs.edit().putString(PREF_PENDING, UpdateVersion.normalize(r.tag)).apply();
        try {
            installWithSession(apk);
        } catch (Exception e) {
            // Some shells refuse sessions from ordinary apps; the installer
            // activity is the older route to the same confirmation.
            Log.w(TAG, "session install failed, trying the installer activity", e);
            try {
                installWithIntent(apk);
            } catch (Exception e2) {
                Log.e(TAG, "installer activity failed too", e2);
                prefs.edit().remove(PREF_PENDING).apply();
                setState(ERROR, "Could not start the installer: " + reason(e2));
            }
        }
    }

    private void installWithSession(File apk) throws IOException {
        PackageInstaller pi = app.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(app.getPackageName());
        params.setSize(apk.length());
        int id = pi.createSession(params);
        PackageInstaller.Session s = pi.openSession(id);
        try {
            try (InputStream in = new FileInputStream(apk);
                 OutputStream out = s.openWrite("base.apk", 0, apk.length())) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                s.fsync(out);
            }
            Intent cb = new Intent(ACTION_INSTALL_STATUS).setPackage(app.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;  // the installer adds its extras
            PendingIntent p = PendingIntent.getBroadcast(app, id, cb, flags);
            session = id;
            s.commit(p.getIntentSender());
            Log.i(TAG, "install session " + id + " committed");
        } catch (IOException | RuntimeException e) {
            session = -1;
            s.abandon();
            throw e;
        } finally {
            s.close();
        }
    }

    // For a result, so a cancelled prompt comes back through MainActivity and
    // onInstallerClosed (no FLAG_ACTIVITY_NEW_TASK: that cancels the result).
    private void installWithIntent(File apk) {
        Uri uri = FileProvider.getUriForFile(app, app.getPackageName() + ".fileprovider", apk);
        @SuppressWarnings("deprecation")
        Intent i = new Intent(Intent.ACTION_VIEW)
                .setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                .putExtra(Intent.EXTRA_RETURN_RESULT, true);
        activity.runOnUiThread(() -> {
            try {
                activity.cancelComeBackToVr();
                activity.startActivityForResult(i, MainActivity.REQUEST_INSTALLER);
            } catch (Exception e) {
                Log.e(TAG, "no installer activity", e);
                prefs.edit().remove(PREF_PENDING).apply();
                setState(ERROR, "This headset would not open the installer. Update with SideQuest instead.");
            }
        });
    }

    /** MainActivity: the installer activity (fallback route) closed without replacing us. */
    synchronized void onInstallerClosed(boolean ok) {
        if (!INSTALLING.equals(state)) return;
        prefs.edit().remove(PREF_PENDING).apply();
        setState(AVAILABLE, ok ? "" : "Update cancelled.");
    }

    // Results of the session commit; a success ends this process before long.
    private final BroadcastReceiver installStatus = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            synchronized (UpdateChecker.this) {
                onInstallStatus(intent);
            }
        }
    };

    private void onInstallStatus(Intent intent) {
        int id = intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1);
        if (id < 0 || id != session) {
            Log.w(TAG, "install status for session " + id + " ignored (waiting for " + session + ")");
            return;
        }
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
        Log.i(TAG, "install status " + status + (msg != null ? " (" + msg + ")" : ""));
        if (status != PackageInstaller.STATUS_PENDING_USER_ACTION) session = -1;
        switch (status) {
            case PackageInstaller.STATUS_PENDING_USER_ACTION: {
                @SuppressWarnings("deprecation")
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm == null) {
                    setState(ERROR, "The installer did not ask for confirmation.");
                    return;
                }
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                activity.runOnUiThread(() -> {
                    try {
                        // a return-to-VR retry still running would cover the prompt
                        activity.cancelComeBackToVr();
                        activity.startActivity(confirm);
                    } catch (Exception e) {
                        Log.e(TAG, "could not show the install confirmation", e);
                        setState(ERROR, "Could not show the install confirmation.");
                    }
                });
                break;
            }
            case PackageInstaller.STATUS_SUCCESS:
                setState(INSTALLING, "Updated. Start GoldenEye VR again from the Library.");
                break;
            case PackageInstaller.STATUS_FAILURE_ABORTED:
                prefs.edit().remove(PREF_PENDING).apply();
                setState(AVAILABLE, "Update cancelled.");
                activity.comeBackToVr();
                break;
            case PackageInstaller.STATUS_FAILURE:
            case PackageInstaller.STATUS_FAILURE_BLOCKED:
                // Refused as a session (a shell that only lets its own
                // installer run them): the installer activity, once.
                if (!triedInstallerActivity && installingApk != null && installingApk.isFile()) {
                    triedInstallerActivity = true;
                    Log.w(TAG, "session refused, trying the installer activity");
                    installWithIntent(installingApk);
                    break;
                }
                // fall through
            default:
                prefs.edit().remove(PREF_PENDING).apply();
                setState(ERROR, "Update failed" + (msg != null ? ": " + msg : "."));
                activity.comeBackToVr();
        }
    }

    // ---- permission ------------------------------------------------------------

    private boolean canInstall() {
        return Build.VERSION.SDK_INT < 26 || app.getPackageManager().canRequestPackageInstalls();
    }

    private void askForInstallPermission() {
        setState(NEEDS_PERMISSION, "Allow \"Install unknown apps\" in the window that opened.");
        activity.runOnUiThread(() -> {
            try {
                activity.cancelComeBackToVr();
                Intent i = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + app.getPackageName()));
                activity.startActivityForResult(i, MainActivity.REQUEST_INSTALL_PERMISSION);
            } catch (Exception e) {
                Log.e(TAG, "no settings page for unknown sources", e);
                setState(ERROR, "This headset has no \"Install unknown apps\" setting to open. "
                        + "Update with SideQuest instead.");
            }
        });
    }

    // ---- helpers ---------------------------------------------------------------

    private boolean busy() {
        String s = state;
        return CHECKING.equals(s) || DOWNLOADING.equals(s) || INSTALLING.equals(s);
    }

    private void setState(String s, String msg) {
        state = s;
        message = msg == null ? "" : msg;
    }

    private String installedVersion() {
        try {
            String v = app.getPackageManager().getPackageInfo(app.getPackageName(), 0).versionName;
            return v != null ? v : "0";
        } catch (PackageManager.NameNotFoundException e) {
            return "0";
        }
    }

    private File updatesDir() throws IOException {
        File dir = app.getExternalFilesDir("updates");
        if (dir == null) throw new IOException("no storage for the download");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("could not create " + dir);
        return dir;
    }

    private void deleteDownloads() {
        File dir = app.getExternalFilesDir("updates");
        File[] files = dir != null ? dir.listFiles() : null;
        if (files == null) return;
        for (File f : files) {
            if (f.isFile()) f.delete();
        }
    }

    private HttpURLConnection open(String url, String accept) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(20000);
        c.setInstanceFollowRedirects(true);   // release assets redirect to GitHub's file host
        c.setRequestProperty("Accept", accept);
        c.setRequestProperty("User-Agent", "GoldenEye-VR/" + UpdateVersion.normalize(installed));
        return c;
    }

    private String httpGet(String url) throws IOException {
        HttpURLConnection c = open(url, "application/vnd.github+json");
        c.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
        try {
            int code = c.getResponseCode();
            if (code == 403 || code == 429) {
                throw new IOException("GitHub's rate limit was reached; try again in an hour");
            }
            if (code != 200) throw new IOException("GitHub answered HTTP " + code);
            try (InputStream in = c.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[1 << 14];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    if (out.size() > (8 << 20)) throw new IOException("GitHub's answer is too large");
                }
                return new String(out.toByteArray(), StandardCharsets.UTF_8);
            }
        } finally {
            c.disconnect();
        }
    }

    private static String sha256(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
        }
        StringBuilder sb = new StringBuilder();
        for (byte b : md.digest()) sb.append(String.format("%02x", b & 0xff));
        return sb.toString();
    }

    @SuppressWarnings("deprecation")
    private static int signingFlags() {
        return Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
    }

    @SuppressWarnings("deprecation")
    private static Signature[] signers(PackageInfo p) {
        if (Build.VERSION.SDK_INT >= 28) {
            if (p.signingInfo == null) return null;
            return p.signingInfo.hasMultipleSigners()
                    ? p.signingInfo.getApkContentsSigners()
                    : p.signingInfo.getSigningCertificateHistory();
        }
        return p.signatures;
    }

    private static boolean sameSigner(PackageInfo a, PackageInfo b) {
        Signature[] sa = signers(a), sb = signers(b);
        if (sa == null || sb == null || sa.length == 0 || sb.length == 0) return false;
        // Single signer: the update's current key must be the installed app's
        // current key (a rotated key would list the old one in its history).
        if (sa.length == 1 || sb.length == 1) {
            for (Signature s : sb) {
                if (s.equals(sa[sa.length - 1])) return true;
            }
            return false;
        }
        Signature[] x = sa.clone(), y = sb.clone();
        Arrays.sort(x, (p, q) -> p.toCharsString().compareTo(q.toCharsString()));
        Arrays.sort(y, (p, q) -> p.toCharsString().compareTo(q.toCharsString()));
        return Arrays.equals(x, y);
    }

    @SuppressWarnings("deprecation")
    private static long versionCode(PackageInfo p) {
        return Build.VERSION.SDK_INT >= 28 ? p.getLongVersionCode() : p.versionCode;
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
