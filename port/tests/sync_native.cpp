/*
 * Cross-platform sync (docs/cloud-sync.md), offline. Two parts:
 *
 *  - core: several simulated devices run gsync::sync (port/pc/gevr_sync_core.cpp)
 *    against one store, in this process, with their own clocks.
 *  - launch/push <saveFolder> <kind>: the real file glue (port/pc/gevr_sync.cpp)
 *    on one save folder, the store a folder named by GEVR_SYNC_DIR.
 *    port/tests/test_sync.py runs these as separate launches.
 */
#include "gevr_sync_core.h"
#include "gevr_sync.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gsync {
void launchAt(const std::string &root, const std::string &kind);
void pushForTest();
}

using namespace gsync;

static int s_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* ------------------------------------------------------------ simulation */

typedef std::map<std::string, std::string> StoreMap;

struct Dev {
    std::string id, kind;
    long long offset = 0;   // its clock's error
    LocalState local;
    Baseline base;
    DeviceRecord mine;
    std::vector<std::string> messages;
    std::map<std::string, int> backups;
    Dev(const std::string &i, const std::string &k) : id(i), kind(k)
    {
        mine.id = id;
        mine.kind = kind;
        mine.name = "dev " + id;
    }
};

static std::string iniGet(const Dev &d, FileId f, const std::string &key)
{
    auto m = iniMap(f, d.local.text[f]);
    auto it = m.find(key);
    return it == m.end() ? std::string("<none>") : it->second;
}

static void iniSet(Dev &d, FileId f, const std::string &key, const std::string &value)
{
    d.local.text[f] = iniApply(f, d.local.text[f], { { key, value } });
    d.local.have[f] = true;
}

static Outcome launch(Dev &d, StoreMap &store, long long now)
{
    std::vector<DeviceRecord> others;
    for (const auto &kv : store) {
        DeviceRecord r;
        if (recordParse(kv.second, &r) && r.id != d.id) others.push_back(r);
    }
    Outcome o = sync(d.mine, others, d.local, d.base, now + d.offset, true);
    for (int f = 0; f < FILE_COUNT; f++) {
        if (o.fileChanged[f]) {
            d.local.text[f] = o.text[f];
            d.local.have[f] = true;
        }
    }
    for (const SaveWrite &w : o.saves) {
        if (w.backup) d.backups[w.name]++;
        d.local.saves[w.name] = w.data;
        d.local.saveTimes[w.name] = w.t;
    }
    for (const std::string &m : o.messages) d.messages.push_back(m);
    d.base = o.base;
    d.mine = o.mine;
    store[recordName(d.id)] = recordText(d.mine);
    return o;
}

static void push(Dev &d, StoreMap &store, long long now)
{
    Outcome o = sync(d.mine, {}, d.local, d.base, now + d.offset, false);
    CHECK(o.saves.empty() && !o.fileChanged[0] && !o.fileChanged[1]);
    d.base = o.base;
    if (o.mineChanged) {
        d.mine = o.mine;
        store[recordName(d.id)] = recordText(d.mine);
    }
}

static std::string blankSave() { return std::string(512, '\0'); }

static std::string progressSave(unsigned char level)
{
    std::string s(512, '\0');
    s[32 + 20] = (char)level;   // slot 0: a finished level's time
    return s;
}

static void writeSave(Dev &d, const std::string &name, const std::string &data, long long t)
{
    d.local.saves[name] = data;
    d.local.saveTimes[name] = t + d.offset;
}

/* ------------------------------------------------------------- the cases */

static void testScopesAndJoining()
{
    StoreMap store;
    const long long T = 1764000000000LL;
    Dev a("aaaa", "quest"), b("bbbb", "quest"), c("cccc", "pc");
    iniSet(a, FILE_INI, "LookSpeed", "5");
    iniSet(a, FILE_INI, "FrameRate", "90");
    iniSet(a, FILE_INI, "ICloudSync", "1");
    iniSet(a, FILE_CFG, "Input.Player1.ControllerIndex", "2");
    iniSet(a, FILE_CFG, "Input.Player1.Binds.Fire", "MOUSE_LEFT");
    launch(a, store, T);
    DeviceRecord ra;
    CHECK(recordParse(store[recordName("aaaa")], &ra));
    CHECK(ra.files[FILE_INI].count("LookSpeed"));
    CHECK(ra.files[FILE_INI].count("FrameRate@quest"));
    CHECK(!ra.files[FILE_INI].count("FrameRate"));
    CHECK(!ra.files[FILE_INI].count("ICloudSync"));                       // this device's own
    CHECK(!ra.files[FILE_CFG].count("Input.Player1.ControllerIndex"));    // the pads in its room
    CHECK(ra.files[FILE_CFG].count("Input.Player1.Binds.Fire"));

    // a second Quest joins: the first one's values win, only what it lacks goes up
    iniSet(b, FILE_INI, "LookSpeed", "3");
    iniSet(b, FILE_INI, "FrameRate", "72");
    iniSet(b, FILE_INI, "Extra", "1");
    iniSet(b, FILE_CFG, "Input.Player1.ControllerIndex", "0");
    Outcome ob = launch(b, store, T + 1000);
    CHECK(iniGet(b, FILE_INI, "LookSpeed") == "5");
    CHECK(iniGet(b, FILE_INI, "FrameRate") == "90");
    CHECK(iniGet(b, FILE_INI, "Extra") == "1");
    CHECK(iniGet(b, FILE_CFG, "Input.Player1.ControllerIndex") == "0");   // its own stays
    CHECK(iniGet(b, FILE_CFG, "Input.Player1.Binds.Fire") == "MOUSE_LEFT");   // new section made
    CHECK(ob.settingsTaken == 3);
    DeviceRecord rb;
    CHECK(recordParse(store[recordName("bbbb")], &rb));
    CHECK(!rb.files[FILE_INI].count("LookSpeed"));
    CHECK(rb.files[FILE_INI].count("Extra"));

    // a PC joins: the Quests' frame rate is theirs, the look speed everyone's
    iniSet(c, FILE_INI, "FrameRate", "144");
    launch(c, store, T + 2000);
    CHECK(iniGet(c, FILE_INI, "FrameRate") == "144");
    CHECK(iniGet(c, FILE_INI, "LookSpeed") == "5");
    DeviceRecord rc;
    CHECK(recordParse(store[recordName("cccc")], &rc));
    CHECK(rc.files[FILE_INI].count("FrameRate@pc"));

    // A sees B's Extra and C nothing of its kind
    launch(a, store, T + 3000);
    CHECK(iniGet(a, FILE_INI, "Extra") == "1");
    CHECK(iniGet(a, FILE_INI, "FrameRate") == "90");
}

static void testChangesAndClocks()
{
    StoreMap store;
    const long long T = 1764000000000LL;
    Dev a("aaaa", "quest"), b("bbbb", "tv");
    iniSet(a, FILE_INI, "LookSpeed", "5");
    launch(a, store, T);
    launch(b, store, T + 10);
    CHECK(iniGet(b, FILE_INI, "LookSpeed") == "5");

    // B changes it while playing (pushed); A's next launch takes it
    iniSet(b, FILE_INI, "LookSpeed", "7");
    push(b, store, T + 20000);
    launch(a, store, T + 30000);
    CHECK(iniGet(a, FILE_INI, "LookSpeed") == "7");

    // both change it between syncs: the later one wins everywhere
    iniSet(a, FILE_INI, "LookSpeed", "1");
    push(a, store, T + 40000);
    iniSet(b, FILE_INI, "LookSpeed", "2");
    push(b, store, T + 50000);
    launch(a, store, T + 60000);
    launch(b, store, T + 61000);
    CHECK(iniGet(a, FILE_INI, "LookSpeed") == "2");
    CHECK(iniGet(b, FILE_INI, "LookSpeed") == "2");

    // a clock an hour slow: a change made after seeing B's still wins
    a.offset = -3600LL * 1000;
    iniSet(a, FILE_INI, "LookSpeed", "9");
    push(a, store, T + 70000);
    launch(b, store, T + 80000);
    CHECK(iniGet(b, FILE_INI, "LookSpeed") == "9");

    // set back to a value this device had set before: it still wins
    a.offset = 0;
    iniSet(b, FILE_INI, "Crosshair", "1");
    push(b, store, T + 90000);
    launch(a, store, T + 100000);
    CHECK(iniGet(a, FILE_INI, "Crosshair") == "1");
    iniSet(a, FILE_INI, "Crosshair", "0");
    push(a, store, T + 110000);
    launch(b, store, T + 115000);
    CHECK(iniGet(b, FILE_INI, "Crosshair") == "0");
    iniSet(b, FILE_INI, "Crosshair", "1");      // B sets it again
    push(b, store, T + 120000);
    launch(a, store, T + 130000);
    CHECK(iniGet(a, FILE_INI, "Crosshair") == "1");
    iniSet(a, FILE_INI, "Crosshair", "0");      // back to A's earlier value
    push(a, store, T + 140000);
    launch(b, store, T + 150000);
    CHECK(iniGet(b, FILE_INI, "Crosshair") == "0");

    // nothing changed: nothing to upload
    Outcome o = sync(a.mine, {}, a.local, a.base, T + 160000, false);
    CHECK(!o.mineChanged);
}

static void testKindSwitch()
{
    // a PC that plays flat and in VR keeps one frame rate for each
    StoreMap store;
    const long long T = 1764000000000LL;
    Dev pc("pc01", "pc"), other("vr02", "pcvr");
    iniSet(other, FILE_INI, "FrameRate", "90");
    launch(other, store, T);
    iniSet(pc, FILE_INI, "FrameRate", "144");
    launch(pc, store, T + 1000);
    CHECK(iniGet(pc, FILE_INI, "FrameRate") == "144");
    pc.kind = pc.mine.kind = "pcvr";
    launch(pc, store, T + 2000);
    CHECK(iniGet(pc, FILE_INI, "FrameRate") == "90");
    pc.kind = pc.mine.kind = "pc";
    launch(pc, store, T + 3000);
    CHECK(iniGet(pc, FILE_INI, "FrameRate") == "144");
}

static void testSaves()
{
    StoreMap store;
    const long long T = 1764000000000LL;
    Dev a("aaaa", "quest"), b("bbbb", "tv");
    writeSave(a, "eeprom.bin", progressSave(1), T);
    launch(a, store, T + 1000);

    // a new device's blank save never replaces progress, even written later
    writeSave(b, "eeprom.bin", blankSave(), T + 5000);
    Outcome ob = launch(b, store, T + 6000);
    CHECK(b.local.saves["eeprom.bin"] == progressSave(1));
    CHECK(b.backups["eeprom.bin"] == 1);
    CHECK(ob.messages.empty());
    CHECK(b.local.saveTimes["eeprom.bin"] >= T);

    // a later save with progress wins on the other device
    writeSave(b, "eeprom.bin", progressSave(2), T + 20000);
    push(b, store, T + 20000);
    launch(a, store, T + 30000);
    CHECK(a.local.saves["eeprom.bin"] == progressSave(2));
    CHECK(a.messages.empty());

    // unchanged since: nothing goes up, nothing comes down
    Outcome o = launch(a, store, T + 40000);
    CHECK(!o.mineChanged && o.saves.empty());

    // both play between syncs: the newer is kept, the other told and backed up
    writeSave(a, "eeprom.bin", progressSave(3), T + 50000);
    push(a, store, T + 50000);
    writeSave(b, "eeprom.bin", progressSave(4), T + 60000);
    push(b, store, T + 60000);
    launch(b, store, T + 70000);
    CHECK(b.local.saves["eeprom.bin"] == progressSave(4));
    CHECK(b.messages.empty());
    const int before = a.backups["eeprom.bin"];
    launch(a, store, T + 80000);
    CHECK(a.local.saves["eeprom.bin"] == progressSave(4));
    CHECK(a.backups["eeprom.bin"] == before + 1);
    CHECK(a.messages.size() == 1);

    // a device joining with progress of its own: the newer save wins, and it is told
    Dev c("cccc", "pc");
    writeSave(c, "eeprom.bin", progressSave(9), T + 1000);
    launch(c, store, T + 85000);
    CHECK(c.local.saves["eeprom.bin"] == progressSave(4));
    CHECK(c.backups["eeprom.bin"] == 1 && c.messages.size() == 1);

    // a second save file (another EEPROM) travels too
    writeSave(b, "eeprom-hack1.bin", progressSave(5), T + 90000);
    push(b, store, T + 90000);
    launch(a, store, T + 100000);
    CHECK(a.local.saves["eeprom-hack1.bin"] == progressSave(5));
    CHECK(!a.backups.count("eeprom-hack1.bin"));
}

static void testRecordText()
{
    DeviceRecord r;
    r.id = "0123456789abcdef";
    r.name = "Den\tTV";
    r.kind = "tv";
    r.version = "0.5.27";
    r.seen = 42;
    r.files[FILE_INI]["Name"] = Entry{ "Bond\\James\nBond", 7 };
    r.files[FILE_CFG]["Input.Player1.Binds.Fire"] = Entry{ "A, B", 8 };
    SaveRec s;
    s.t = 9;
    s.progress = true;
    s.data = std::string("\0\1\2\377x", 5);
    r.saves["eeprom.bin"] = s;
    DeviceRecord p;
    CHECK(recordParse(recordText(r) + "future\tline\n", &p));
    CHECK(p.id == r.id && p.name == r.name && p.kind == "tv" && p.version == "0.5.27" && p.seen == 42);
    CHECK(p.files[FILE_INI]["Name"].value == "Bond\\James\nBond" && p.files[FILE_INI]["Name"].t == 7);
    CHECK(p.files[FILE_CFG]["Input.Player1.Binds.Fire"].value == "A, B");
    CHECK(p.saves["eeprom.bin"].data == s.data && p.saves["eeprom.bin"].progress && p.saves["eeprom.bin"].t == 9);
    CHECK(!recordParse("hello\n", &p));
    std::string id;
    CHECK(recordNameId(recordName("abc"), &id) && id == "abc");
    CHECK(!recordNameId("README.txt", &id));

    Baseline b;
    b.joined = true;
    b.clock = 99;
    b.files[FILE_CFG]["Input.Player1.Binds.Fire"] = "A";
    b.saveHash["eeprom.bin"] = hashText("x");
    Baseline q;
    CHECK(baselineParse(baselineText(b), &q));
    CHECK(q.joined && q.clock == 99 && q.files[FILE_CFG]["Input.Player1.Binds.Fire"] == "A" &&
          q.saveHash["eeprom.bin"] == hashText("x"));

    std::string dec;
    for (const char *t : { "", "a", "ab", "abc", "abcd" }) {
        CHECK(base64Decode(base64Encode(t), &dec) && dec == t);
    }
    CHECK(saveHasProgress(progressSave(1)) && !saveHasProgress(blankSave()));
    CHECK(isSaveName("eeprom.bin") && isSaveName("eeprom-x.bin") && !isSaveName("eeprom.bin.before-sync-1") &&
          !isSaveName("goldeneye.ini"));
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "core")) {
        testRecordText();
        testScopesAndJoining();
        testChangesAndClocks();
        testKindSwitch();
        testSaves();
        if (s_fail) return 1;
        printf("sync core: ok\n");
        return 0;
    }
    if (argc >= 4 && (!strcmp(argv[1], "launch") || !strcmp(argv[1], "push"))) {
        gsync::launchAt(argv[2], argv[3]);
        if (!strcmp(argv[1], "push")) gsync::pushForTest();
        char line[256], msg[512];
        gevrSyncStatus(line, sizeof(line));
        printf("status=%s\n", line);
        if (gevrSyncTakeMessage(msg, sizeof(msg))) printf("message=%s\n", msg);
        printf("active=%d\n", gevrSyncActive());
        return 0;
    }
    fprintf(stderr, "usage: sync_test core | launch <folder> <kind> | push <folder> <kind>\n");
    return 2;
}
