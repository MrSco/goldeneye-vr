package com.gevr.port;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/** Android exit reason values; kept free of framework calls for regression checks. */
final class CrashExitPolicy {
    static boolean isCrash(int reason, int status) {
        // Java crash, native crash, ANR, or a fatal native signal. SIGKILL
        // (force stop, restart, memory pressure) is deliberately excluded.
        return reason == 4 || reason == 5 || reason == 6
                || (reason == 2 && (status == 4 || status == 6 || status == 7 || status == 8 || status == 11));
    }

    /**
     * Whether an exit record is worth offering: a crash of THIS install that
     * has not been offered yet. Records older than the APK's lastUpdateTime
     * belong to a previous build (report 5103ca8b was a five-day-old SIGSEGV
     * from v0.1.x surfaced on the first v0.3.7 launch); the log of that run is
     * gone and the report would carry the wrong version.
     */
    static boolean qualifies(int reason, int status, long exitAt, long installAt, long offeredAt) {
        return isCrash(reason, status) && exitAt > installAt && exitAt > offeredAt;
    }

    /**
     * Same install rule for the app's own foreground marker: a marker left from
     * before the install was the install killing the app. The marker only stands
     * in for missing exit records, so a record at or after it means the system
     * logged how that run ended and it was not a crash (report 8bb89f82 was a
     * user-requested force stop one second after launch).
     */
    static boolean unexpectedExit(long markerAt, long installAt, long offeredAt, long lastExitAt) {
        return markerAt > installAt && markerAt > offeredAt && lastExitAt < markerAt;
    }

    /** ApplicationExitInfo.REASON_* names (android-34). */
    static String reasonName(int reason) {
        switch (reason) {
            case 0: return "unknown";
            case 1: return "exit-self";
            case 2: return "signaled";
            case 3: return "low-memory";
            case 4: return "crash";
            case 5: return "crash-native";
            case 6: return "anr";
            case 7: return "initialization-failure";
            case 8: return "permission-change";
            case 9: return "excessive-resource-usage";
            case 10: return "user-requested";
            case 11: return "user-stopped";
            case 12: return "dependency-died";
            case 13: return "other";
            case 14: return "freezer";
            case 15: return "package-state-change";
            case 16: return "package-updated";
            default: return "reason" + reason;
        }
    }

    static String isoUtc(long millis) {
        SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US);
        format.setTimeZone(TimeZone.getTimeZone("UTC"));
        return format.format(new Date(millis));
    }

    /** One exit record as it appears in the log and the report's exit history. */
    static String describe(long exitAt, int reason, int status, String description, long installAt) {
        return isoUtc(exitAt) + " " + reasonName(reason) + "(" + reason + ") status=" + status
                + (exitAt <= installAt ? " [before install]" : "")
                + (description != null && !description.isEmpty() ? " " + description : "");
    }
}
