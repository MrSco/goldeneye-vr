/*
 * iCloud sync's local parts (port/ios/ios_icloud.mm): the ini rewrite, the
 * archive round trip, unsafe names, the item scan and putting a download in
 * place. No iCloud account needed. Built for the iOS simulator and run there
 * (port/tests/test_icloud.sh, on the Mac).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" {
const char *fsFullPath(const char *relPath) { return relPath; }
void sysLogPrintf(int level, const char *fmt, ...)
{
    (void)level;
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}
}

#include "../ios/ios_icloud.mm"

static int s_fail = 0;
static void check(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        s_fail++;
    }
}

static void put(const std::string &path, const std::string &data)
{
    makeDirs(path.substr(0, path.rfind('/')));
    FILE *f = fopen(path.c_str(), "wb");
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static std::string get(const std::string &path)
{
    std::string s;
    readFile(path, &s);
    return s;
}

int main()
{
    // goldeneye-vr.ini: lines changed in place, new keys appended
    {
        const std::string text = "[VR]\n; a comment\nA=1\nB=2\nTouchLayout=x=1;y=2\n";
        check(iniMap(kIni, text)["TouchLayout"] == "x=1;y=2", "a value keeps its own '='");
        const std::string out = iniApply(kIni, text, { { "B", "3" }, { "C", "4" } });
        check(out == "[VR]\n; a comment\nA=1\nB=3\nTouchLayout=x=1;y=2\nC=4\n", "ini: B changed, C appended");
        check(iniApply(kIni, "", { { "A", "1" } }) == "[VR]\nA=1\n", "ini from nothing has its section");
        check(iniMap(kIni, "[VR]\r\nA=1\r\n")["A"] == "1", "CRLF lines read");
    }
    // goldeneye.ini: Section.key, missing keys under their section
    {
        const std::string text = "[Game]\nCenterHUD=0\n\n[Input]\nMouseEnabled=1\n";
        std::map<std::string, std::string> m = iniMap(kCfg, text);
        check(m["Game.CenterHUD"] == "0" && m["Input.MouseEnabled"] == "1", "cfg keys carry their section");
        const std::string out = iniApply(kCfg, text, { { "Game.CenterHUD", "1" }, { "Video.DefaultFullscreen", "1" } });
        check(out.find("CenterHUD=1") != std::string::npos && out.find("[Video]\nDefaultFullscreen=1\n") != std::string::npos,
              "cfg: changed in place, a new section appended");
        check(iniMap(kCfg, out)["Video.DefaultFullscreen"] == "1", "cfg: the new section reads back");
    }
    // which keys stay on one kind of device
    {
        check(isLocalKey(kIni, "FrameRate") && isLocalKey(kIni, "TouchLayout") && isLocalKey(kIni, "ICloudPacks"),
              "display, touch and the iCloud rows are the device's");
        check(!isLocalKey(kIni, "SnapTurn") && !isLocalKey(kIni, "PadLookSpeed") && !isLocalKey(kIni, "PlayerName"),
              "controls and the name are shared");
        check(isLocalKey(kCfg, "Video.DefaultFullscreen") && isLocalKey(kCfg, "Audio.BufferSize")
                  && !isLocalKey(kCfg, "Game.Player1.CrosshairSize"),
              "cfg: video/audio the device's, crosshair shared");
        // never synced at all: the room's (split-screen guest seats, pads) and the iCloud rows
        check(isDeviceKey(kIni, "Guest2Name") && isDeviceKey(kIni, "Guest4Loadout3") && isDeviceKey(kIni, "Guest3Chr")
                  && isDeviceKey(kIni, "ICloudSync") && isDeviceKey(kCfg, "Input.Player2.ControllerIndex")
                  && isDeviceKey(kCfg, "Audio.BufferSize"),
              "guest seats, pad assignments, audio and the iCloud rows stay on the device");
        check(isLocalKey(kIni, "MasterVolume") && !isDeviceKey(kIni, "MasterVolume") && !isDeviceKey(kIni, "FrameRate")
                  && !isLocalKey(kIni, "AdaptiveTriggers") && !isLocalKey(kIni, "MpLobby"),
              "master volume per family; triggers and multiplayer choices shared");
    }

    char tmpl[] = "/tmp/gevr-icloud-test-XXXXXX";
    const char *made = mkdtemp(tmpl);
    if (!made) {
        printf("FAIL: no temp folder\n");
        return 1;
    }
    g_root = made;

    // the archive: a pack folder in, fed back in odd pieces
    {
        put(g_root + "/src/a.txt", "alpha");
        put(g_root + "/src/sub/b.bin", std::string(100000, 'b'));
        put(g_root + "/src/sub/deep/empty", "");
        ChunkWriter w;
        w.label = "src";
        const uint32_t end = 0xffffffffu;
        bool ok = w.put(kMagic, sizeof(kMagic));
        std::vector<std::pair<std::string, long long>> files;
        long long maxMtime = 0;
        walkDir(g_root + "/src", "", &files, &maxMtime);
        check(files.size() == 3, "walk finds the three files");
        for (auto &f : files) ok = ok && archiveFile(&w, f.first, g_root + "/src/" + f.first);
        ok = ok && w.put(&end, 4);
        check(ok && w.chunks == 0, "archive built in memory");
        Unpacker u;
        u.dest = g_root + "/out";
        makeDirs(u.dest);
        for (size_t i = 0; i < w.buf.size(); i += 7) u.feed(w.buf.data() + i, std::min<size_t>(7, w.buf.size() - i));
        check(u.err.empty() && u.state == 5, "archive read to its end");
        check(get(g_root + "/out/a.txt") == "alpha" && get(g_root + "/out/sub/b.bin") == std::string(100000, 'b'),
              "files come back the same");
        struct stat st;
        check(stat((g_root + "/out/sub/deep/empty").c_str(), &st) == 0 && st.st_size == 0, "an empty file comes back");
    }
    // a ROM is one entry named ""
    {
        put(g_root + "/rom.z64", "ROMDATA");
        ChunkWriter w;
        const uint32_t end = 0xffffffffu;
        bool ok = w.put(kMagic, sizeof(kMagic)) && archiveFile(&w, "", g_root + "/rom.z64") && w.put(&end, 4);
        Unpacker u;
        u.dest = g_root + "/romout";
        makeDirs(u.dest);
        u.feed(w.buf.data(), w.buf.size());
        check(ok && u.err.empty() && u.state == 5 && get(g_root + "/romout/__file") == "ROMDATA", "ROM archive round trip");
        Unpacker cut;
        cut.dest = g_root + "/cut";
        makeDirs(cut.dest);
        cut.feed(w.buf.data(), w.buf.size() - 4);
        check(cut.state != 5, "a cut archive is not finished");
    }
    // names that would leave the folder
    {
        Unpacker u;
        check(!u.safeName("../x") && !u.safeName("/etc/x") && !u.safeName("a//b") && !u.safeName("a/./b")
                  && !u.safeName("a\\b") && u.safeName("a/b.png") && u.safeName(""),
              "unsafe names refused");
        u.dest = g_root + "/bad";
        makeDirs(u.dest);
        std::string a(kMagic, 8);
        const std::string name = "../escape";
        uint32_t nl = (uint32_t)name.size();
        unsigned long long sz = 1;
        a.append((const char *)&nl, 4);
        a += name;
        a.append((const char *)&sz, 8);
        a += "x";
        u.feed(a.data(), a.size());
        struct stat st;
        check(!u.err.empty() && stat((g_root + "/escape").c_str(), &st) != 0, "an escaping name writes nothing");
    }
    // the items: ROMs in the folder and data/, packs only when asked; picked*.z64 skipped
    {
        put(g_root + "/data/ge.z64", std::string(64, 'g'));
        put(g_root + "/data/picked.z64", "p");
        put(g_root + "/ge.z64.part", "p");
        put(g_root + "/texture-packs/hd/a.png", "png");
        put(g_root + "/texture-packs/hd.old-icloud/a.png", "old");
        std::vector<LocalItem> roms = localItems(false), all = localItems(true);
        std::set<std::string> names;
        for (auto &it : all) names.insert(it.path);
        if (all.size() != 3) {
            for (auto &it : all) printf("  item %s\n", it.path.c_str());
        }
        check(roms.size() == 2, "two ROMs without packs (rom.z64, data/ge.z64)");
        check(names.count("data/ge.z64") && names.count("rom.z64") && names.count("texture-packs/hd")
                  && !names.count("data/picked.z64") && !names.count("texture-packs/hd.old-icloud") && all.size() == 3,
              "packs with them, never picked or leftovers");
    }
    // a pack put in place over an older one
    {
        put(g_root + "/stage/files/new.png", "new");
        check(installNow(g_root + "/stage/files", "texture-packs/hd", true), "pack installed");
        struct stat st;
        check(get(g_root + "/texture-packs/hd/new.png") == "new" && stat((g_root + "/texture-packs/hd/a.png").c_str(), &st) != 0,
              "the old pack is gone, the new one in place");
        put(g_root + "/stage2/__file", "NEWROM");
        check(installNow(g_root + "/stage2", "data/ge.z64", false) && get(g_root + "/data/ge.z64") == "NEWROM", "ROM replaced");
    }
    // saves: blank vs progress (GoldenEye's EEPROM: 32-byte header, five 0x60 slots)
    {
        std::string blank(2048, '\0');
        for (int s = 0; s < 5; s++) blank[32 + s * 0x60 + 8] = (char)0x80;   // every slot free
        check(!saveHasProgress(blank), "all slots free: no progress");
        std::string folders = blank;
        for (int s = 0; s < 4; s++) {
            folders[32 + s * 0x60 + 8] = (char)s;                 // folder s in use, nothing done
            folders[32 + s * 0x60 + 10] = (char)0xff;             // volumes and options don't count
            folders[32 + s * 0x60 + 11] = (char)0xff;
            folders[32 + s * 0x60 + 0] = 0x12;                    // nor the checksum
        }
        check(!saveHasProgress(folders), "four empty folders: no progress");
        std::string dam = folders;
        dam[32 + 1 * 0x60 + 18] = 0x20;                           // a time in folder 2
        check(saveHasProgress(dam), "a finished level is progress");
        std::string cheat = folders;
        cheat[32 + 3 * 0x60 + 15] = 0x04;
        check(saveHasProgress(cheat), "an unlocked cheat is progress");
        std::string freeSlot = blank;
        freeSlot[32 + 4 * 0x60 + 18] = 0x20;                      // a time in a free slot is old data
        check(!saveHasProgress(freeSlot), "a free slot's leftovers are not progress");
        check(saveHasProgress(std::string(2048, 'x')), "another layout gets no guard");
        check(saveHasProgress(std::string(100, 'x')), "a short file gets no guard");
        check(saveWhole(blank) && !saveWhole(std::string()) && !saveWhole(std::string(100, 'x')),
              "only a whole 2048-byte EEPROM syncs; one cut short never wins");
    }
    // backups: dated, the last five
    {
        for (int i = 0; i < 7; i++) put(g_root + "/eeprom.bin.before-icloud-2026010" + std::to_string(i) + "-000000", "old");
        saveBackup("eeprom.bin", "now");
        int n = 0;
        if (DIR *d = opendir(g_root.c_str())) {
            while (struct dirent *e = readdir(d)) n += strncmp(e->d_name, "eeprom.bin.before-icloud-", 25) == 0;
            closedir(d);
        }
        struct stat st;
        check(n == 5 && stat((g_root + "/eeprom.bin.before-icloud-20260100-000000").c_str(), &st) != 0,
              "backups: five kept, the oldest gone");
        check(!isSaveName("eeprom.bin.before-icloud-20260100-000000") && isSaveName("eeprom-gex.bin"),
              "a backup is not a save");
    }
    // files: what an item does in a pass (decideMove)
    {
        int asked = 0;
        auto same = [&]() { asked++; return true; };
        auto differs = [&]() { asked++; return false; };
        const Synced was{ "L1", "R1" };
        check(decideMove("L1", "", nullptr, differs) == Move::Up, "only here: up");
        check(decideMove("", "R1", nullptr, differs) == Move::Down, "only in iCloud: down");
        check(decideMove("L1", "R1", &was, differs) == Move::None, "in step: nothing");
        check(decideMove("L1", "R2", &was, differs) == Move::Down, "changed in iCloud only: down");
        check(decideMove("L1", "R2", &was, same) == Move::Adopt, "another device's stamp, same content: synced");
        check(decideMove("L2", "R1", &was, differs) == Move::Up, "changed here only: up");
        check(decideMove("L2", "R2", &was, differs) == Move::Down, "both changed: iCloud's wins");
        asked = 0;
        check(decideMove("L9", "R9", nullptr, differs) == Move::Adopt && asked == 0,
              "never synced with this iCloud: the copy here stays, unread");
    }
    // files.txt: one CloudKit database's stamps (a test build's are not TestFlight's)
    {
        makeDirs(statePath(""));
        writeSynced({ { "ge.z64", { "L1", "R1" } } }, "dev-db");
        check(readSynced("dev-db").count("ge.z64") == 1, "files.txt: read back by its own database");
        check(readSynced("prod-db").empty(), "files.txt: another database's stamps count as never synced");
        put(statePath("files.txt"), "ge.z64\tL1\tR1\n");
        check(readSynced("prod-db").empty(), "files.txt from before the database line: never synced");
    }
    removeTree(g_root);
    if (s_fail) {
        printf("%d failed\n", s_fail);
        return 1;
    }
    printf("PASS: icloud: ini rewrite, device keys, archive round trip, unsafe names, item scan, install, save progress guard, backups, file moves, files.txt per database\n");
    return 0;
}
