package com.gevr.port;
import java.util.*;
/**
 * Desktop test of UpdateVersion (the launcher updater's version order and
 * release choice). From the repo root, with any JDK 11+:
 *
 *   javac -d build/updater_test android/app/src/main/java/com/gevr/port/UpdateVersion.java tools/updater_test/UpdateVersionTest.java
 *   java -cp build/updater_test com.gevr.port.UpdateVersionTest
 */
public class UpdateVersionTest {
  static int fails = 0, runs = 0;
  static void eq(Object got, Object want, String what) { runs++; if (!Objects.equals(got, want)) { fails++; System.out.println("FAIL " + what + ": got " + got + ", want " + want); } }
  static int sgn(int x) { return Integer.signum(x); }
  static UpdateVersion.Release rel(String tag, boolean pre, boolean draft, boolean apk) {
    UpdateVersion.Release r = new UpdateVersion.Release(); r.tag = tag; r.prerelease = pre; r.draft = draft; r.apkUrl = apk ? "https://x/" + tag + ".apk" : null; return r; }
  public static void main(String[] a) throws Exception {
    // ordering
    String[][] lt = { {"v0.1.12","v0.1.13"}, {"0.1.9","0.1.10"}, {"v0.1.12","0.2"}, {"0.1.13-test.1","0.1.13"}, {"0.1.13-test.1","0.1.13-test.2"},
      {"0.1.13-test.2","0.1.13-test.10"}, {"0.1.13-rc.1","0.1.13-test.1"}, {"0.1.13-1","0.1.13-alpha"}, {"0.1.13-test","0.1.13-test.1"}, {"0.1.12","0.1.13-test.1"},
      {"0.9","0.10"}, {"1.0.0","1.0.1"} };
    for (String[] p : lt) { eq(sgn(UpdateVersion.compare(p[0], p[1])), -1, p[0]+" < "+p[1]); eq(sgn(UpdateVersion.compare(p[1], p[0])), 1, p[1]+" > "+p[0]); }
    String[][] same = { {"v0.1.12","0.1.12"}, {"0.1","0.1.0"}, {"V1.2.3","1.2.3+build5"}, {" v0.1.12 ","0.1.12"} };
    for (String[] p : same) eq(UpdateVersion.compare(p[0], p[1]), 0, p[0]+" == "+p[1]);
    // pick
    List<UpdateVersion.Release> rs = Arrays.asList(
      rel("v0.1.14-test.1", true, false, true), rel("v0.1.15", false, true, true), rel("v0.1.13", false, false, true),
      rel("v0.1.12", false, false, true), rel("v0.1.16", false, false, false), null);
    eq(tag(UpdateVersion.pick(rs, "0.1.12", false)), "v0.1.13", "stable offer, draft and APK-less skipped");
    eq(tag(UpdateVersion.pick(rs, "0.1.12", true)), "v0.1.14-test.1", "test builds on");
    eq(tag(UpdateVersion.pick(rs, "0.1.13", false)), null, "up to date");
    eq(tag(UpdateVersion.pick(rs, "0.1.14-test.1", true)), null, "on the newest test build");
    eq(tag(UpdateVersion.pick(rs, "0.1.14-test.1", false)), null, "test build installed, test builds off, older stable");
    eq(tag(UpdateVersion.pick(Arrays.asList(rel("v0.1.14", false, false, true)), "0.1.14-test.1", false)), "v0.1.14", "stable of the test build");
    eq(tag(UpdateVersion.pick(new ArrayList<>(), "0.1.12", true)), null, "no releases");
    System.out.println(runs + " checks, " + fails + " failed");
    System.exit(fails == 0 ? 0 : 1);
  }
  static String tag(UpdateVersion.Release r) { return r == null ? null : r.tag; }
}
