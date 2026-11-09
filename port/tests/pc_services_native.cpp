/*
 * The PC services' rules, offline (port/pc: pc_mods.cpp, pc_reports.cpp,
 * pc_friends.cpp, pc_updater.cpp, pc_files.cpp). The cases follow the Java
 * tests where there are some: tools/updater_test/UpdateVersionTest.java,
 * android/app/src/test/.../ModArchiveTest.java and CrashExitPolicyTest.java.
 * Nothing here touches the network.
 *
 *   clang -c port/vr/miniz/miniz.c -o miniz.o
 *   clang++ -std=c++20 -I port/pc -I port/include port/tests/pc_services_native.cpp port/pc/pc_http.cpp port/pc/pc_files.cpp miniz.o -lwinhttp -lbcrypt -o pc_services_test.exe
 *
 * Run it from an empty folder: it writes its zips and their unpacked files there.
 */
#include <cstdarg>
#include <cstdio>
#include <string>

#include "gevr_bean_import.h"
#include "gevr_bean_ce.h"

extern "C" void sysLogPrintf(int, const char *, ...) {}
void pcRestartSelf(void) {}

/* The Bean import itself (port/src/gevr_bean_import.c, gevr_bean_ce.c) is not
 * linked here: pc_mods.cpp only hands it the picked file. */
static int s_importCalls;
extern "C" int gevrBeanImportStream(FILE *src, const char *, char *msg, size_t msgLen)
{
    s_importCalls++;
    if (src) fclose(src);
    snprintf(msg, msgLen, "stub");
    return -1;
}
extern "C" int gevrBeanImportPercent(void) { return 42; }
extern "C" void gevrBeanImportCancel(void) {}
extern "C" int gevrBeanCeApply(FILE *zip, const char *, char *msg, size_t msgLen)
{
    if (zip) fclose(zip);
    snprintf(msg, msgLen, "stub");
    return -1;
}

#include "../pc/pc_mods.cpp"
#include "../pc/pc_reports.cpp"
#include "../pc/pc_friends.cpp"
#include "../pc/pc_updater.cpp"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* A zip of 100 GOLDENEYE/N.png and, optionally, one more entry (ModArchiveTest.pack). */
static std::string pack(const char *name, const char *extra = nullptr, const std::string &data = "")
{
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    mz_zip_writer_init_file(&z, name, 0);
    for (int i = 0; i < 100; i++) {
        const std::string entry = "GOLDENEYE/" + std::to_string(i) + ".png";
        mz_zip_writer_add_mem(&z, entry.c_str(), "\x01\x02\x03\x04", 4, MZ_DEFAULT_COMPRESSION);
    }
    if (extra) mz_zip_writer_add_mem(&z, extra, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
    return name;
}

static bool throws(const std::function<void()> &f, int status = 0)
{
    try { f(); } catch (const HttpError &e) { return e.status == status; }
    return false;
}

static void testArchive()
{
    UnzipRules png;
    png.kinds = { ".png" };
    png.least = 100;
    // extractsTexturesWithNamesIntact
    CHECK(unzipBounded(pack("t1.zip"), "t1", png, nullptr) == 100);
    CHECK(isFile("t1/GOLDENEYE/42.png"));
    // texturePacksCannotInstallMeshes
    CHECK(unzipBounded(pack("t2.zip", "files/new/char/boilerbond/default.bin", "CAFF"), "t2", png, nullptr) == 100);
    CHECK(!isDir("t2/files"));
    // rejectsTooFewTextures
    {
        mz_zip_archive z;
        memset(&z, 0, sizeof(z));
        mz_zip_writer_init_file(&z, "t3.zip", 0);
        mz_zip_writer_add_mem(&z, "GOLDENEYE/1.png", "\x01\x02\x03\x04", 4, MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_finalize_archive(&z);
        mz_zip_writer_end(&z);
        CHECK(throws([&] { unzipBounded("t3.zip", "t3", png, nullptr); }));
    }
    // preventsZipTraversal (and Windows' own escapes)
    CHECK(throws([&] { unzipBounded(pack("t4.zip", "GOLDENEYE/../../escape.png", "\x01\x02\x03\x04"), "t4", png, nullptr); }));
    CHECK(!isFile("escape.png"));
    CHECK(throws([&] { unzipBounded(pack("t5.zip", "C:/escape.png", "\x01\x02\x03\x04"), "t5", png, nullptr); }));
    CHECK(throws([&] { unzipBounded(pack("t6.zip", "GOLDENEYE\\..\\..\\escape.png", "\x01\x02\x03\x04"), "t6", png, nullptr); }));
    // two names NTFS would make one file
    CHECK(throws([&] { unzipBounded(pack("t7.zip", "goldeneye/42.PNG", "\x01\x02\x03\x04"), "t7", png, nullptr); }));
    // an entry outside the size limits
    CHECK(throws([&] { unzipBounded(pack("t8.zip", "GOLDENEYE/tiny.png", "\x01"), "t8", png, nullptr); }));
    // cancelsDuringExtraction
    CHECK(throws([&] { unzipBounded(pack("t9.zip"), "t9", png, [](int) { return false; }); }, -1));
    // rejectsCorruptCrc: the central directory's CRC of the first entry, one bit off
    {
        pack("t10.zip");
        std::string bytes;
        readFile("t10.zip", &bytes, 1 << 20);
        for (size_t i = 0; i + 20 < bytes.size(); i++) {
            if (bytes[i] == 'P' && bytes[i + 1] == 'K' && bytes[i + 2] == 1 && bytes[i + 3] == 2) {
                bytes[i + 16] ^= 1;
                break;
            }
        }
        writeFileAtomic("t10.zip", bytes);
        CHECK(throws([&] { unzipBounded("t10.zip", "t10", png, nullptr); }));
    }
    // the updater's zip: one top folder taken off, every kind of file
    {
        mz_zip_archive z;
        memset(&z, 0, sizeof(z));
        mz_zip_writer_init_file(&z, "t11.zip", 0);
        mz_zip_writer_add_mem(&z, "GoldenEyeVR-1.2.3-windows-x64/GoldenEyeVR.exe", "MZ..", 4, MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem(&z, "GoldenEyeVR-1.2.3-windows-x64/VERSION", "1.2.3\n", 6, MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_add_mem(&z, "GoldenEyeVR-1.2.3-windows-x64/licenses/a.txt", "", 0, MZ_DEFAULT_COMPRESSION);
        mz_zip_writer_finalize_archive(&z);
        mz_zip_writer_end(&z);
        UnzipRules all;
        all.least = 1;
        all.minEntry = 0;
        all.stripPrefix = "GoldenEyeVR-1.2.3-windows-x64/";
        std::vector<std::string> files;
        CHECK(unzipBounded("t11.zip", "t11", all, nullptr, &files) == 3);
        CHECK(files.size() == 3 && files[0] == "GoldenEyeVR.exe" && files[2] == "licenses/a.txt");
        CHECK(isFile("t11/VERSION"));
        all.stripPrefix = "GoldenEyeVR-9.9.9-windows-x64/";   // a zip for another version
        CHECK(throws([&] { unzipBounded("t11.zip", "t11b", all, nullptr); }));
    }
}

static void testMods()
{
    CHECK(sameVersion("2026-10-19", "2026.10.19"));
    CHECK(sameVersion("V2025.12.30", "v2025.12.30"));
    CHECK(!sameVersion("1.1", "1.2"));
    // the status line's fields, as vr_launcher.cpp splits them
    ModManager &m = mods();
    const std::string status = m.status();
    CHECK(status.find("oddjob-hat\tOddjob's hat\tEurocom / EA (007 Nightfire), GoldenEye VR\tgithub.com/MrSco/goldeneye-vr-mods\t1.1\t0\tnone\t-1\t\t") != std::string::npos);
    CHECK(status.find("ge007-hd\tGoldenEye 007 HD\tintermissionfb and GhostlyDark\tevilgames.eu\tv2025.12.30\t129\tnone\t-1\t\t") == 0);
    // the player's own copy (importOwn): the Bean row's line once the job ends
    auto beanLine = [&] {
        std::string line;
        for (int i = 0; i < 300; i++) {
            const std::string all = m.status();
            const size_t at = all.find("ge007-bean\t");
            line = all.substr(at, all.find('\n', at) - at);
            if (line.find("\timporting\t") == std::string::npos && line.find("\tpatching\t") == std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return line;
    };
    m.importOwn("ge007-bean", "no-such-copy.7z");
    CHECK(beanLine().find("\terror\t-1\tcould not open that file\t") != std::string::npos);
    CHECK(s_importCalls == 0 && !isDir("texture-packs/.ge007-bean.unpacking"));
    writeFileAtomic("copy.7z", "not a copy");
    m.importOwn("ge007-bean", "copy.7z");
    CHECK(beanLine().find("\terror\t-1\tstub\t") != std::string::npos);   // the import's own message
    CHECK(s_importCalls == 1 && !isDir("texture-packs/.ge007-bean.unpacking") && !isDir("texture-packs/ge007-bean"));
    m.importOwn("ge007-hd", "copy.7z");   // only an own-copy pack imports
    CHECK(s_importCalls == 1);
    m.applyCe("copy.7z");                 // the Community Edition needs the imported copy
    CHECK(beanLine().find("\tpatching\t") == std::string::npos && beanLine().find("\tstub\t") != std::string::npos);
}

static void testReports()
{
    // CrashExitPolicyTest's qualifies / unexpectedExit
    const long long INSTALL = 1700000000000LL;
    CHECK(!qualifies(INSTALL - 5LL * 86400000, INSTALL, 0));
    CHECK(!qualifies(INSTALL, INSTALL, 0));
    CHECK(qualifies(INSTALL + 60000, INSTALL, 0));
    CHECK(qualifies(INSTALL + 60000, INSTALL, INSTALL + 59999));
    CHECK(!qualifies(INSTALL + 60000, INSTALL, INSTALL + 60000));
    CHECK(!unexpectedExit(INSTALL - 1, INSTALL, 0, 0, 0));
    CHECK(!unexpectedExit(0, INSTALL, 0, 0, 0));
    CHECK(unexpectedExit(INSTALL + 1, INSTALL, 0, 0, 0));
    CHECK(!unexpectedExit(INSTALL + 1, INSTALL, INSTALL + 1, 0, 0));
    const long long markerAt = INSTALL + 3600000;
    CHECK(!unexpectedExit(markerAt, INSTALL, 0, markerAt + 1000, 0));
    CHECK(!unexpectedExit(markerAt, INSTALL, 0, markerAt, 0));
    CHECK(unexpectedExit(markerAt, INSTALL, 0, markerAt - 60000, 0));
    CHECK(!unexpectedExit(markerAt, INSTALL, 0, 0, markerAt + 1000));   // a restart of the PC since
    CHECK(unexpectedExit(markerAt, INSTALL, 0, 0, markerAt - 1000));

    // LogReporter.redact's two patterns
    CHECK(redact("peer 192.168.1.20:5000 and 10.0.0.5") == "peer [redacted address]:5000 and [redacted address]");
    CHECK(redact("loopback 127.0.0.1 stays") == "loopback 127.0.0.1 stays");
    CHECK(redact("version 1.2.3.4.5 and 1.2.3 and 1234.1.1.1") == "version 1.2.3.4.5 and 1.2.3 and 1234.1.1.1");
    CHECK(redact("a1.2.3.4") == "a[redacted address]");
    CHECK(redact("launcher: lobbyCommand offer|dj0wDQo=\nnext 8.8.8.8\r\n") ==
          "launcher: lobbyCommand offer|[redacted signaling]\nnext [redacted address]\r\n");
    CHECK(redact("lobbyCommand answer|j1|abc|offer|xyz") == "lobbyCommand answer|j1|abc|offer|[redacted signaling]");
    CHECK(redact("offer| before LOBBYCOMMAND") == "offer| before LOBBYCOMMAND");

    // GZIPOutputStream's member, read back with miniz's inflate
    const std::string plain = "=== exit history ===\n" + std::string(5000, 'x') + "\nend\n";
    const std::string gz = gzip(plain);
    CHECK(gz.size() > 18 && (unsigned char)gz[0] == 0x1f && (unsigned char)gz[1] == 0x8b && gz[2] == 8);
    size_t outLen = 0;
    void *back = tinfl_decompress_mem_to_heap(gz.data() + 10, gz.size() - 18, &outLen, 0);
    CHECK(back && std::string((const char *)back, outLen) == plain);
    mz_free(back);
    const unsigned crc = (unsigned char)gz[gz.size() - 8] | ((unsigned char)gz[gz.size() - 7] << 8)
                       | ((unsigned char)gz[gz.size() - 6] << 16) | ((unsigned)(unsigned char)gz[gz.size() - 5] << 24);
    CHECK(crc == (unsigned)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)plain.data(), plain.size()));

    // the crash record the handler writes, read back
    s_recorded = false;
    pcCrashRecord("EXCEPTION 0xc0000005 at 0000 (module 0000 + 0x1234)\n", "BACKTRACE:\n#00\n");
    CrashRecord c;
    CHECK(readCrashRecord(&c));
    CHECK(c.at > INSTALL && c.summary == "EXCEPTION 0xc0000005 at 0000 (module 0000 + 0x1234) " && c.trace == "BACKTRACE:\n#00\n");
    pcCrashRecord("second", "");   // the first record of a run wins
    CHECK(readCrashRecord(&c) && c.trace == "BACKTRACE:\n#00\n");
    removeFile("gevr_crash.txt");
}

static void testFriends()
{
    std::string text = R"({"id":"abc","name":"Natty|Bond","n":7,"x":null})";
    jspan o;
    CHECK(jsonParse(text, &o));
    CHECK(need(o, "id") == "abc");
    CHECK(clean(need(o, "name")) == "Natty Bond");
    CHECK(need(o, "n") == "7");   // getString reads a number as text
    CHECK(throws([&] { need(o, "missing"); }));
    CHECK(throws([&] { need(o, "x"); }));
    CHECK(opt(o, "x").empty() && opt(o, "missing").empty());
    CHECK(optInt(o, "n") == "7" && optInt(o, "missing") == "0");
}

static Release rel(const char *tag, bool pre, bool draft, bool zip)
{
    Release r;
    r.tag = tag;
    r.prerelease = pre;
    r.draft = draft;
    if (zip) r.zipUrl = std::string("https://x/") + tag + ".zip";
    return r;
}

static std::string pickTag(const std::vector<Release> &rs, const char *installed, bool tests)
{
    Release r;
    return pick(rs, installed, tests, &r) ? r.tag : std::string("none");
}

static int sgn(int x) { return x < 0 ? -1 : x > 0 ? 1 : 0; }

static void testUpdater()
{
    // UpdateVersionTest's ordering
    const char *lt[][2] = { {"v0.1.12","v0.1.13"}, {"0.1.9","0.1.10"}, {"v0.1.12","0.2"}, {"0.1.13-test.1","0.1.13"}, {"0.1.13-test.1","0.1.13-test.2"},
        {"0.1.13-test.2","0.1.13-test.10"}, {"0.1.13-rc.1","0.1.13-test.1"}, {"0.1.13-1","0.1.13-alpha"}, {"0.1.13-test","0.1.13-test.1"}, {"0.1.12","0.1.13-test.1"},
        {"0.9","0.10"}, {"1.0.0","1.0.1"} };
    for (auto &p : lt) {
        CHECK(sgn(compare(p[0], p[1])) == -1);
        CHECK(sgn(compare(p[1], p[0])) == 1);
    }
    const char *same[][2] = { {"v0.1.12","0.1.12"}, {"0.1","0.1.0"}, {"V1.2.3","1.2.3+build5"}, {" v0.1.12 ","0.1.12"} };
    for (auto &p : same) CHECK(compare(p[0], p[1]) == 0);
    // pick
    const std::vector<Release> rs = { rel("v0.1.14-test.1", true, false, true), rel("v0.1.15", false, true, true),
        rel("v0.1.13", false, false, true), rel("v0.1.12", false, false, true), rel("v0.1.16", false, false, false) };
    CHECK(pickTag(rs, "0.1.12", false) == "v0.1.13");          // stable offer, draft and zip-less skipped
    CHECK(pickTag(rs, "0.1.12", true) == "v0.1.14-test.1");    // test builds on
    CHECK(pickTag(rs, "0.1.13", false) == "none");             // up to date
    CHECK(pickTag(rs, "0.1.14-test.1", true) == "none");       // on the newest test build
    CHECK(pickTag(rs, "0.1.14-test.1", false) == "none");      // test builds off, older stable
    CHECK(pickTag({ rel("v0.1.14", false, false, true) }, "0.1.14-test.1", false) == "v0.1.14");
    CHECK(pickTag({}, "0.1.12", true) == "none");
    // the Windows zip among a release's files, and GitHub's digest
    CHECK(isWindowsAsset("GoldenEyeVR-0.5.16-windows-x64.zip"));
    CHECK(!isWindowsAsset("GoldenEye-VR-0.5.16.apk") && !isWindowsAsset("GoldenEyeVR-0.5.16-windows-x64.debug")
          && !isWindowsAsset("gevr-server-0.5.16-linux-x64.tar.gz"));
    const std::vector<Release> parsed = parseReleases(R"([
        {"tag_name":"v0.5.16","prerelease":false,"draft":false,"body":"notes","assets":[
            {"name":"GoldenEye-VR-0.5.16.apk","size":9,"browser_download_url":"https://x/a.apk"},
            {"name":"GoldenEyeVR-0.5.16-windows-x64.zip","size":22000000,"digest":"sha256:ABCDEF0123",
             "browser_download_url":"https://x/w.zip"}]},
        {"tag_name":"v0.5.15","prerelease":true,"assets":[]}])");
    CHECK(parsed.size() == 2);
    CHECK(parsed[0].tag == "v0.5.16" && parsed[0].zipName == "GoldenEyeVR-0.5.16-windows-x64.zip"
          && parsed[0].zipUrl == "https://x/w.zip" && parsed[0].zipSize == 22000000 && parsed[0].zipSha256 == "abcdef0123");
    CHECK(parsed[1].prerelease && parsed[1].zipUrl.empty());
    CHECK(pickTag(parsed, "0.5.15-test.32", false) == "v0.5.16");
    CHECK(throws([] { parseReleases(R"({"message":"Not Found"})"); }));
    // the unpacked zip (package_pc.py's layout): both exes, the DLLs, the launch scripts
    {
        mz_zip_archive z;
        memset(&z, 0, sizeof(z));
        mz_zip_writer_init_file(&z, "t12.zip", 0);
        const char *names[] = { "GoldenEyeVR.exe", "GoldenEyeVR-VR.exe", "phonon.dll", "openxr_loader.dll", "Play VR.cmd",
                                "Play SteamVR.cmd", "Start-SteamVR.ps1", "README.txt", "VERSION", "licenses/openxr-loader.txt" };
        for (const char *n : names) {
            const std::string entry = std::string("GoldenEyeVR-0.5.16-windows-x64/") + n;
            mz_zip_writer_add_mem(&z, entry.c_str(), "0.5.16\n", 7, MZ_DEFAULT_COMPRESSION);
        }
        mz_zip_writer_finalize_archive(&z);
        mz_zip_writer_end(&z);
        UnzipRules all;
        all.least = 1;
        all.minEntry = 0;
        all.stripPrefix = "GoldenEyeVR-0.5.16-windows-x64/";
        std::vector<std::string> files;
        CHECK(unzipBounded("t12.zip", "t12", all, nullptr, &files) == 10);
        CHECK(isFile("t12/Play VR.cmd") && isFile("t12/licenses/openxr-loader.txt"));
        CHECK(stagedProblem(files, "0.5.16\n", "v0.5.16", "0.5.15-test.32", "GoldenEyeVR.exe").empty());
        CHECK(stagedProblem(files, "0.5.16", "v0.5.16", "0.5.15", "GoldenEyeVR-VR.exe").empty());
        CHECK(stagedProblem(files, "0.5.16", "v0.5.16", "0.5.15", "goldeneyevr-vr.EXE").empty());   // NTFS case
        // a zip without the exe that is running (an older flat-only zip under PC VR)
        CHECK(stagedProblem({ "GoldenEyeVR.exe", "VERSION" }, "0.5.16", "v0.5.16", "0.5.15", "GoldenEyeVR-VR.exe")
              == "the download has no GoldenEyeVR-VR.exe");
        CHECK(stagedProblem({ "GoldenEyeVR-VR.exe", "VERSION" }, "0.5.16", "v0.5.16", "0.5.15", "GoldenEyeVR-VR.exe")
              == "the download is not GoldenEye VR for Windows");
        CHECK(stagedProblem(files, "0.5.17", "v0.5.16", "0.5.15", "GoldenEyeVR.exe").find("different version") != std::string::npos);
        CHECK(stagedProblem(files, "", "v0.5.16", "0.5.15", "GoldenEyeVR.exe") == "the download is a different version (none)");
        CHECK(stagedProblem(files, "0.5.16", "v0.5.16", "0.5.17", "GoldenEyeVR.exe") == "that build is older than the one installed");
    }
}

int main()
{
    testArchive();
    testMods();
    testReports();
    testFriends();
    testUpdater();
    printf(failures ? "pc_services: %d of %d failed\n" : "pc_services: all %d passed\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
