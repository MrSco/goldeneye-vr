package com.gevr.port;

import java.util.List;

/**
 * Release tags and which one to offer (UpdateChecker). Plain Java, no Android
 * classes, so it can be tested on a desktop JVM (tools/updater_test).
 *
 * Tags look like the ones this repo publishes: "v0.1.12", or for test builds
 * published as GitHub pre-releases, "v0.1.13-test.1". Ordering is semver's:
 * numbers first, then a version with a suffix sorts before the same version
 * without one (0.1.13-test.2 < 0.1.13), suffixes compared piece by piece.
 */
final class UpdateVersion {

    private UpdateVersion() {
    }

    /** One release as the GitHub API lists it, reduced to what the updater needs. */
    static final class Release {
        String tag;
        boolean prerelease;
        boolean draft;
        String apkName;
        String apkUrl;
        long apkSize;
        /** Hex SHA-256 of the APK when GitHub reports one ("digest": "sha256:..."), else null. */
        String apkSha256;
        String notes;
    }

    /** "v0.1.12" -> "0.1.12"; trims whitespace. */
    static String normalize(String tag) {
        if (tag == null) return "";
        String t = tag.trim();
        if (t.startsWith("v") || t.startsWith("V")) t = t.substring(1);
        return t;
    }

    /** Negative if a is older than b, zero if the same, positive if newer. */
    static int compare(String a, String b) {
        String na = normalize(a), nb = normalize(b);
        String[] sa = splitSuffix(na), sb = splitSuffix(nb);
        String[] ca = sa[0].split("\\."), cb = sb[0].split("\\.");
        int n = Math.max(ca.length, cb.length);
        for (int i = 0; i < n; i++) {
            long x = i < ca.length ? leadingNumber(ca[i]) : 0;
            long y = i < cb.length ? leadingNumber(cb[i]) : 0;
            if (x != y) return x < y ? -1 : 1;
        }
        // Same numbers: a release beats any pre-release of it.
        boolean pa = !sa[1].isEmpty(), pb = !sb[1].isEmpty();
        if (!pa && !pb) return 0;
        if (!pa) return 1;
        if (!pb) return -1;
        String[] ia = sa[1].split("\\."), ib = sb[1].split("\\.");
        int m = Math.max(ia.length, ib.length);
        for (int i = 0; i < m; i++) {
            if (i >= ia.length) return -1;   // fewer pieces sorts first
            if (i >= ib.length) return 1;
            boolean da = isDigits(ia[i]), db = isDigits(ib[i]);
            int c;
            if (da && db) {
                c = Long.compare(Long.parseLong(ia[i]), Long.parseLong(ib[i]));
            } else if (da != db) {
                c = da ? -1 : 1;             // numeric pieces sort before words
            } else {
                c = ia[i].compareTo(ib[i]);
            }
            if (c != 0) return c < 0 ? -1 : 1;
        }
        return 0;
    }

    /**
     * The newest release with an APK that is newer than the installed version,
     * or null. Drafts are never offered; pre-releases only when asked for.
     */
    static Release pick(List<Release> releases, String installed, boolean includePrereleases) {
        Release best = null;
        for (Release r : releases) {
            if (r == null || r.draft || r.tag == null || r.apkUrl == null) continue;
            if (r.prerelease && !includePrereleases) continue;
            if (best == null || compare(r.tag, best.tag) > 0) best = r;
        }
        if (best != null && compare(best.tag, installed) > 0) return best;
        return null;
    }

    /** Whether the name looks like this project's APK asset. */
    static boolean isApkAsset(String name) {
        return name != null && name.toLowerCase(java.util.Locale.ROOT).endsWith(".apk");
    }

    /** Prefer "GoldenEye-VR-*.apk" over any other APK attached to the same release. */
    static boolean isPreferredApk(String name) {
        return isApkAsset(name) && name.startsWith("GoldenEye-VR");
    }

    // "0.1.13-test.1+abc" -> ["0.1.13", "test.1"] (build metadata after '+' ignored)
    private static String[] splitSuffix(String v) {
        int plus = v.indexOf('+');
        if (plus >= 0) v = v.substring(0, plus);
        int dash = v.indexOf('-');
        if (dash < 0) return new String[] {v, ""};
        return new String[] {v.substring(0, dash), v.substring(dash + 1)};
    }

    private static long leadingNumber(String s) {
        int i = 0;
        while (i < s.length() && Character.isDigit(s.charAt(i))) i++;
        if (i == 0) return 0;
        try {
            return Long.parseLong(s.substring(0, i));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static boolean isDigits(String s) {
        if (s.isEmpty()) return false;
        for (int i = 0; i < s.length(); i++) {
            if (!Character.isDigit(s.charAt(i))) return false;
        }
        return true;
    }
}
