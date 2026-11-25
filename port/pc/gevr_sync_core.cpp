/*
 * Cross-platform sync's rules: gevr_sync_core.h, docs/cloud-sync.md.
 */
#include "gevr_sync_core.h"
#include "gevr_sync_keys.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace gsync {

const char *const kFileNames[FILE_COUNT] = { "goldeneye-vr.ini", "goldeneye.ini" };
const char *const kFileTags[FILE_COUNT] = { "ini", "cfg" };

/* ------------------------------------------------------------------ scopes */

static bool startsWith(const std::string &s, const char *p)
{
    return s.compare(0, strlen(p), p) == 0;
}

/* the table iCloud uses too (port/include/gevr_sync_keys.h) */
Scope keyScope(FileId f, const std::string &key)
{
    const gevrsync::KeyScope s = f == FILE_INI ? gevrsync::iniKeyScope(key) : gevrsync::cfgKeyScope(key);
    return s == gevrsync::KEY_DEVICE ? SCOPE_DEVICE : s == gevrsync::KEY_KIND ? SCOPE_KIND : SCOPE_SHARED;
}

bool isSaveName(const std::string &n)
{
    return n.size() >= 10 && startsWith(n, "eeprom") && n.compare(n.size() - 4, 4, ".bin") == 0 &&
           n.find('/') == std::string::npos && n.find('\\') == std::string::npos;
}

/* ------------------------------------------------------------------- ini */

static std::string trimEol(const std::string &s)
{
    size_t e = s.size();
    while (e && (s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(0, e);
}

struct IniLine {
    std::string text;      // as read, without the line end
    std::string key;       // "" for headers, comments, blanks
    std::string section;   // goldeneye.ini's section it is in
};

static std::vector<IniLine> iniLines(FileId f, const std::string &text)
{
    std::vector<IniLine> out;
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        IniLine line;
        line.text = trimEol(text.substr(pos, nl - pos));
        pos = nl + 1;
        const std::string &t = line.text;
        if (!t.empty() && t[0] == '[') {
            const size_t close = t.find(']');
            section = t.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        } else if (!t.empty() && t[0] != ';' && t[0] != '#') {
            const size_t eq = t.find('=');
            if (eq != std::string::npos && eq > 0) {
                std::string k = t.substr(0, eq);
                while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
                line.key = f == FILE_CFG ? section + "." + k : k;
            }
        }
        line.section = section;
        out.push_back(line);
    }
    return out;
}

static std::string iniValue(const IniLine &l)
{
    const size_t eq = l.text.find('=');
    std::string v = l.text.substr(eq + 1);
    size_t s = 0;
    while (s < v.size() && (v[s] == ' ' || v[s] == '\t')) s++;
    return v.substr(s);
}

std::map<std::string, std::string> iniMap(FileId f, const std::string &text)
{
    std::map<std::string, std::string> m;
    for (const IniLine &l : iniLines(f, text)) {
        if (!l.key.empty()) m[l.key] = iniValue(l);
    }
    return m;
}

std::string iniApply(FileId f, const std::string &text, const std::map<std::string, std::string> &set)
{
    std::vector<IniLine> lines = iniLines(f, text);
    std::set<std::string> done;
    for (IniLine &l : lines) {
        if (l.key.empty()) continue;
        auto it = set.find(l.key);
        if (it == set.end()) continue;
        const std::string bare = f == FILE_CFG ? l.key.substr(l.section.size() + 1) : l.key;
        l.text = bare + "=" + it->second;
        done.insert(l.key);
    }
    std::string out;
    for (const IniLine &l : lines) out += l.text + "\n";
    if (f == FILE_INI) {
        if (lines.empty()) out += "[VR]\n";
        for (const auto &kv : set) {
            if (!done.count(kv.first)) out += kv.first + "=" + kv.second + "\n";
        }
        return out;
    }
    // goldeneye.ini: the missing keys under their own section (the last dot
    // splits it: "Input.Player1.Binds.Fire" is Fire in [Input.Player1.Binds])
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> bySection;
    for (const auto &kv : set) {
        if (done.count(kv.first)) continue;
        const size_t dot = kv.first.rfind('.');
        if (dot == std::string::npos || dot == 0) continue;
        bySection[kv.first.substr(0, dot)].push_back({ kv.first.substr(dot + 1), kv.second });
    }
    for (const auto &sec : bySection) {
        out += "\n[" + sec.first + "]\n";
        for (const auto &kv : sec.second) out += kv.first + "=" + kv.second + "\n";
    }
    return out;
}

/* ------------------------------------------------------------- encodings */

std::string hashText(const std::string &data)
{
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx%08zx", h, data.size());
    return buf;
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string &in)
{
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out += kB64[v >> 18 & 63];
        out += kB64[v >> 12 & 63];
        out += kB64[v >> 6 & 63];
        out += kB64[v & 63];
    }
    if (i < in.size()) {
        const unsigned v = (unsigned char)in[i] << 16 | (i + 1 < in.size() ? (unsigned char)in[i + 1] << 8 : 0);
        out += kB64[v >> 18 & 63];
        out += kB64[v >> 12 & 63];
        out += i + 1 < in.size() ? kB64[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64Decode(const std::string &in, std::string *out)
{
    out->clear();
    unsigned acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') break;
        const char *p = strchr(kB64, c);
        if (!p || !c) return false;
        acc = acc << 6 | (unsigned)(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            *out += (char)(acc >> bits & 0xff);
        }
    }
    return true;
}

/* Record fields: tabs separate them; a value's \, tab and line ends escaped. */
static std::string esc(const std::string &s)
{
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

static std::string unesc(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            o += s[i];
        }
    }
    return o;
}

static std::vector<std::string> splitTabs(const std::string &line)
{
    std::vector<std::string> f;
    size_t pos = 0;
    for (;;) {
        const size_t tab = line.find('\t', pos);
        f.push_back(line.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos));
        if (tab == std::string::npos) break;
        pos = tab + 1;
    }
    return f;
}

static std::vector<std::string> textLines(const std::string &text)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        out.push_back(trimEol(text.substr(pos, nl - pos)));
        pos = nl + 1;
    }
    return out;
}

static long long toLL(const std::string &s) { return strtoll(s.c_str(), nullptr, 10); }

/* --------------------------------------------------------------- records */

static const char kRecordMagic[] = "gevr-sync-record\t1";

std::string recordName(const std::string &id) { return "device-" + id + ".txt"; }

bool recordNameId(const std::string &name, std::string *id)
{
    if (name.size() <= 11 || !startsWith(name, "device-") || name.compare(name.size() - 4, 4, ".txt") != 0) {
        return false;
    }
    *id = name.substr(7, name.size() - 11);
    return true;
}

std::string recordText(const DeviceRecord &r)
{
    std::string o = std::string(kRecordMagic) + "\n";
    o += "id\t" + esc(r.id) + "\n";
    o += "name\t" + esc(r.name) + "\n";
    o += "kind\t" + esc(r.kind) + "\n";
    o += "version\t" + esc(r.version) + "\n";
    o += "seen\t" + std::to_string(r.seen) + "\n";
    for (int f = 0; f < FILE_COUNT; f++) {
        for (const auto &kv : r.files[f]) {
            o += std::string(kFileTags[f]) + "\t" + std::to_string(kv.second.t) + "\t" + esc(kv.first) + "\t" +
                 esc(kv.second.value) + "\n";
        }
    }
    for (const auto &kv : r.saves) {
        o += "save\t" + esc(kv.first) + "\t" + std::to_string(kv.second.t) + "\t" + (kv.second.progress ? "1" : "0") +
             "\t" + base64Encode(kv.second.data) + "\n";
    }
    return o;
}

bool recordParse(const std::string &text, DeviceRecord *out)
{
    *out = DeviceRecord();
    const std::vector<std::string> lines = textLines(text);
    if (lines.empty() || !startsWith(lines[0], "gevr-sync-record\t")) return false;
    for (size_t i = 1; i < lines.size(); i++) {
        const std::vector<std::string> f = splitTabs(lines[i]);
        const std::string &tag = f[0];
        if (f.size() == 2 && tag == "id") out->id = unesc(f[1]);
        else if (f.size() == 2 && tag == "name") out->name = unesc(f[1]);
        else if (f.size() == 2 && tag == "kind") out->kind = unesc(f[1]);
        else if (f.size() == 2 && tag == "version") out->version = unesc(f[1]);
        else if (f.size() == 2 && tag == "seen") out->seen = toLL(f[1]);
        else if (f.size() == 5 && tag == "save") {
            const std::string name = unesc(f[1]);
            SaveRec s;
            s.t = toLL(f[2]);
            s.progress = f[3] == "1";
            if (isSaveName(name) && base64Decode(f[4], &s.data)) out->saves[name] = s;
        } else if (f.size() == 4) {
            for (int fi = 0; fi < FILE_COUNT; fi++) {
                if (tag == kFileTags[fi]) {
                    Entry e;
                    e.t = toLL(f[1]);
                    e.value = unesc(f[3]);
                    out->files[fi][unesc(f[2])] = e;
                }
            }
        }
        // anything else: a later version's line, left alone
    }
    return !out->id.empty();
}

/* -------------------------------------------------------------- baseline */

std::string baselineText(const Baseline &b)
{
    std::string o = "gevr-sync-state\t1\n";
    o += std::string("joined\t") + (b.joined ? "1" : "0") + "\n";
    o += "clock\t" + std::to_string(b.clock) + "\n";
    for (int f = 0; f < FILE_COUNT; f++) {
        for (const auto &kv : b.files[f]) o += std::string(kFileTags[f]) + "\t" + esc(kv.first) + "\t" + esc(kv.second) + "\n";
    }
    for (const auto &kv : b.saveHash) o += "save\t" + esc(kv.first) + "\t" + kv.second + "\n";
    return o;
}

bool baselineParse(const std::string &text, Baseline *out)
{
    *out = Baseline();
    const std::vector<std::string> lines = textLines(text);
    if (lines.empty() || !startsWith(lines[0], "gevr-sync-state\t")) return false;
    for (size_t i = 1; i < lines.size(); i++) {
        const std::vector<std::string> f = splitTabs(lines[i]);
        if (f.size() == 2 && f[0] == "joined") out->joined = f[1] == "1";
        else if (f.size() == 2 && f[0] == "clock") out->clock = toLL(f[1]);
        else if (f.size() == 2 && f[0] == "pulled") out->pulled = toLL(f[1]);
        else if (f.size() == 3 && f[0] == "save") out->saveHash[unesc(f[1])] = f[2];
        else if (f.size() == 3) {
            for (int fi = 0; fi < FILE_COUNT; fi++) {
                if (f[0] == kFileTags[fi]) out->files[fi][unesc(f[1])] = unesc(f[2]);
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ saves */

bool saveHasProgress(const std::string &data)
{
    if (data.size() < 32 + 5 * 0x60) return true;
    for (int slot = 0; slot < 5; slot++) {
        const unsigned char *s = (const unsigned char *)data.data() + 32 + slot * 0x60;
        if (s[8] & 0x80) continue;   // completion_bitflags: SAVEFLAG_DORESET, a free slot
        if (s[9] & 1) return true;   // flag_007
        for (int i = 14; i < 17; i++) {
            if (s[i]) return true;   // unlocked_cheats_1..3
        }
        for (int i = 18; i < 0x60; i++) {
            if (s[i]) return true;   // times
        }
    }
    return false;   // every slot free or empty: a blank save
}

/* ------------------------------------------------------------------- sync */

namespace {

struct Candidate {
    const SaveRec *save;
    const DeviceRecord *rec;
};

/* progress first, then the newest, then the device id (every device picks the same) */
bool saveBetter(const Candidate &a, const Candidate &b)
{
    if (a.save->progress != b.save->progress) return a.save->progress;
    if (a.save->t != b.save->t) return a.save->t > b.save->t;
    return a.rec->id > b.rec->id;
}

std::map<std::string, std::string> syncedKeys(FileId f, const std::map<std::string, std::string> &all)
{
    std::map<std::string, std::string> out;
    for (const auto &kv : all) {
        if (keyScope(f, kv.first) != SCOPE_DEVICE) out.insert(kv);
    }
    return out;
}

std::string deviceLabel(const DeviceRecord &r)
{
    return r.name.empty() ? (r.kind.empty() ? std::string("another device") : r.kind) : r.name;
}

}   // namespace

Outcome sync(const DeviceRecord &mineIn, const std::vector<DeviceRecord> &othersIn, const LocalState &local,
             const Baseline &base, long long now, bool apply)
{
    Outcome out;
    out.mine = mineIn;
    DeviceRecord &mine = out.mine;
    std::vector<const DeviceRecord *> others;
    for (const DeviceRecord &r : othersIn) {
        if (r.id != mine.id) others.push_back(&r);   // never our own twice
    }

    // the clock: no stamp of ours is older than anything seen
    long long seen = base.clock;
    auto see = [&seen](const DeviceRecord &r) {
        for (int f = 0; f < FILE_COUNT; f++) {
            for (const auto &kv : r.files[f]) seen = std::max(seen, kv.second.t);
        }
        for (const auto &kv : r.saves) seen = std::max(seen, kv.second.t);
    };
    see(mine);
    if (apply) {
        for (const DeviceRecord *r : others) see(*r);
    }
    const long long stamp = std::max(now, seen + 1);
    long long newest = seen;

    const bool joining = !base.joined;
    out.base = base;
    out.base.joined = base.joined || apply;

    // ---- settings
    for (int fi = 0; fi < FILE_COUNT; fi++) {
        const FileId f = (FileId)fi;
        const std::map<std::string, std::string> localMap = local.have[fi] ? iniMap(f, local.text[fi])
                                                                           : std::map<std::string, std::string>();
        // a record's key: the setting's, with "@kind" for a kind's own
        // ("FrameRate@quest"), so a PC that plays flat and in VR keeps both
        auto recKey = [&](const std::string &key) {
            return keyScope(f, key) == SCOPE_KIND ? key + "@" + mine.kind : key;
        };
        // the setting a record's key stands for here, "" when it is another kind's
        auto localKey = [&](const std::string &rk) -> std::string {
            const size_t at = rk.find('@');
            if (at == std::string::npos) return keyScope(f, rk) == SCOPE_SHARED ? rk : std::string();
            const std::string key = rk.substr(0, at);
            return keyScope(f, key) == SCOPE_KIND && rk.compare(at + 1, std::string::npos, mine.kind) == 0
                       ? key : std::string();
        };

        // this device's changes into its record
        for (const auto &kv : localMap) {
            if (keyScope(f, kv.first) == SCOPE_DEVICE) continue;
            const std::string rk = recKey(kv.first);
            bool changedHere;
            if (!joining) {
                auto b = base.files[fi].find(kv.first);
                changedHere = b == base.files[fi].end() || b->second != kv.second;
            } else {
                // joining: only what no other device has goes up (theirs win)
                changedHere = true;
                for (const DeviceRecord *r : others) {
                    if (r->files[fi].count(rk)) changedHere = false;
                }
            }
            if (!changedHere) continue;
            // set back here to a value this device had set before: stamped again,
            // or another device's newer one would put it back
            auto m = mine.files[fi].find(rk);
            if (joining && m != mine.files[fi].end() && m->second.value == kv.second) continue;
            mine.files[fi][rk] = Entry{ kv.second, stamp };
            newest = std::max(newest, stamp);
            out.mineChanged = true;
        }

        std::string text = local.have[fi] ? local.text[fi] : std::string();
        if (apply) {
            // the newest value of each setting, among the records that hold it for us
            std::map<std::string, std::pair<Entry, const DeviceRecord *>> win;
            auto offer = [&](const DeviceRecord &r) {
                for (const auto &kv : r.files[fi]) {
                    const std::string key = localKey(kv.first);
                    if (key.empty()) continue;
                    auto w = win.find(key);
                    if (w == win.end() || kv.second.t > w->second.first.t ||
                        (kv.second.t == w->second.first.t && r.id > w->second.second->id)) {
                        win[key] = { kv.second, &r };
                    }
                }
            };
            offer(mine);
            for (const DeviceRecord *r : others) offer(*r);
            std::map<std::string, std::string> take;
            for (const auto &kv : win) {
                auto l = localMap.find(kv.first);
                if (l != localMap.end() && l->second == kv.second.first.value) continue;
                take[kv.first] = kv.second.first.value;
            }
            if (!take.empty()) {
                text = iniApply(f, text, take);
                out.fileChanged[fi] = true;
                out.settingsTaken += (int)take.size();
            }
        }
        out.text[fi] = text;
        out.base.files[fi] = syncedKeys(f, iniMap(f, text));
    }

    // ---- saves
    std::set<std::string> names;
    for (const auto &kv : local.saves) {
        if (isSaveName(kv.first)) names.insert(kv.first);
    }
    for (const auto &kv : mine.saves) names.insert(kv.first);
    if (apply) {
        for (const DeviceRecord *r : others) {
            for (const auto &kv : r->saves) names.insert(kv.first);
        }
    }
    for (const std::string &n : names) {
        auto l = local.saves.find(n);
        const bool haveLocal = l != local.saves.end();
        bool editedHere = false;
        if (haveLocal) {
            const std::string h = hashText(l->second);
            auto b = base.saveHash.find(n);
            auto m = mine.saves.find(n);
            const bool sameAsMine = m != mine.saves.end() && m->second.data == l->second;
            editedHere = (b == base.saveHash.end() || b->second != h) && !sameAsMine;
            if (editedHere) {
                auto tm = local.saveTimes.find(n);
                const long long written = tm != local.saveTimes.end() ? tm->second : now;
                SaveRec s;
                s.t = std::max(written, base.clock + 1);   // written after what the last sync saw
                s.progress = saveHasProgress(l->second);
                s.data = l->second;
                mine.saves[n] = s;
                newest = std::max(newest, s.t);
                out.mineChanged = true;
            }
        }

        std::string finalData = haveLocal ? l->second : std::string();
        bool haveFinal = haveLocal;
        if (apply) {
            std::vector<Candidate> cands;
            auto m = mine.saves.find(n);
            if (m != mine.saves.end()) cands.push_back({ &m->second, &mine });
            for (const DeviceRecord *r : others) {
                auto s = r->saves.find(n);
                if (s != r->saves.end()) cands.push_back({ &s->second, r });
            }
            if (!cands.empty()) {
                Candidate best = cands[0];
                for (const Candidate &c : cands) {
                    if (saveBetter(c, best)) best = c;
                }
                if (!haveLocal || best.save->data != l->second) {
                    SaveWrite w;
                    w.name = n;
                    w.data = best.save->data;
                    w.t = best.save->t;
                    w.backup = haveLocal;
                    w.fromDevice = deviceLabel(*best.rec);
                    out.saves.push_back(w);
                    finalData = best.save->data;
                    haveFinal = true;
                    // a conflict: this device's own save and the winner were both
                    // written since this device last read the others' (a new
                    // device's first save, or a blank one, is only kept aside)
                    const bool mineHere = m != mine.saves.end() && m->second.data == l->second;
                    if (haveLocal && base.joined && best.rec != &mine && mineHere && m->second.t > base.pulled &&
                        best.save->t > base.pulled && saveHasProgress(l->second)) {
                        out.messages.push_back("Your save changed on this device and on " + deviceLabel(*best.rec) +
                                               ". The game kept " + deviceLabel(*best.rec) +
                                               "'s, the newer one; this device's is kept as " + n +
                                               ".before-sync-<date> in the save folder.");
                    } else if (haveLocal && !base.joined && best.rec != &mine && saveHasProgress(l->second)) {
                        // joining with a save of its own: the others' newer one is used
                        out.messages.push_back("This device now uses your save from " + deviceLabel(*best.rec) +
                                               ", the newer one. Its own is kept as " + n +
                                               ".before-sync-<date> in the save folder.");
                    }
                }
            }
        }
        if (haveFinal) out.base.saveHash[n] = hashText(finalData);
    }

    out.base.clock = std::max(newest, apply ? seen : base.clock);
    if (apply) out.base.pulled = out.base.clock;
    if (out.mineChanged || apply) {
        mine.seen = now;
    }
    return out;
}

}   // namespace gsync
