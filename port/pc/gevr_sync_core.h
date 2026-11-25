/*
 * Cross-platform sync's rules (docs/cloud-sync.md), with no I/O: what a
 * device's record holds, how settings and saves are chosen, what a launch
 * writes. gevr_sync.cpp reads and writes the files and the store around it;
 * port/tests/sync_native.cpp runs several devices through it.
 *
 * One writer per record: every device writes only its own record
 * (device-<id>.txt) and reads everyone's. A store without transactions
 * (Google Drive) can't lose a write that way, however many devices run.
 *
 *  - Settings: each key this device set carries a stamp (a time in ms; the
 *    record's device breaks a tie). The newest stamp wins, among the records
 *    the key's scope reaches: every device (shared), devices of the same kind
 *    (frame rate, screen...) or none (this device only: never uploaded).
 *    A stamp is never older than anything the device has seen (a slow clock
 *    still orders a change after what it saw).
 *  - Saves: never merged (the iCloud rule). A save with progress beats one
 *    without; then the one written last wins. The one replaced is kept beside
 *    it (<name>.before-sync-<date>).
 *  - Joining: a device's first sync takes the others' values; only keys
 *    nobody has go up.
 */
#ifndef GEVR_SYNC_CORE_H
#define GEVR_SYNC_CORE_H

#include <map>
#include <string>
#include <vector>

namespace gsync {

enum Scope { SCOPE_SHARED, SCOPE_KIND, SCOPE_DEVICE };
enum FileId { FILE_INI = 0, FILE_CFG = 1, FILE_COUNT = 2 };

/* "goldeneye-vr.ini", "goldeneye.ini" (in the save folder) */
extern const char *const kFileNames[FILE_COUNT];
/* "ini", "cfg": their lines in a record */
extern const char *const kFileTags[FILE_COUNT];

Scope keyScope(FileId f, const std::string &key);
bool isSaveName(const std::string &name);   // eeprom*.bin, no folders

/* ---- settings files (goldeneye.ini's keys are "Section.key") ---- */

std::map<std::string, std::string> iniMap(FileId f, const std::string &text);
/* The text with these keys set: lines changed in place, new keys appended
 * (under their section, for goldeneye.ini). */
std::string iniApply(FileId f, const std::string &text, const std::map<std::string, std::string> &set);

/* ---- records ---- */

struct Entry {
    std::string value;
    long long t = 0;
};
using Stamped = std::map<std::string, Entry>;

struct SaveRec {
    long long t = 0;        // when it was written (ms), never older than what its device had seen
    bool progress = false;
    std::string data;
};

struct DeviceRecord {
    std::string id, name, kind, version;
    long long seen = 0;     // its last sync (ms)
    Stamped files[FILE_COUNT];
    std::map<std::string, SaveRec> saves;
};

std::string recordName(const std::string &id);                  // "device-<id>.txt"
bool recordNameId(const std::string &name, std::string *id);
std::string recordText(const DeviceRecord &r);
bool recordParse(const std::string &text, DeviceRecord *out);   // false: not a record

/* ---- this device's memory of the last sync (never uploaded) ---- */

struct Baseline {
    bool joined = false;
    long long clock = 0;    // the newest stamp seen at the last sync
    long long pulled = 0;   // the clock at the last launch sync (the others' records read)
    std::map<std::string, std::string> files[FILE_COUNT];   // synced keys as they were then
    std::map<std::string, std::string> saveHash;            // the saves as they were then
};

std::string baselineText(const Baseline &b);
bool baselineParse(const std::string &text, Baseline *out);

/* ---- one sync ---- */

struct LocalState {
    bool have[FILE_COUNT] = { false, false };
    std::string text[FILE_COUNT];
    std::map<std::string, std::string> saves;      // name -> bytes
    std::map<std::string, long long> saveTimes;    // name -> written (ms)
};

struct SaveWrite {
    std::string name, data;
    long long t = 0;        // the file's time to set (ms)
    bool backup = false;    // keep the file it replaces
    std::string fromDevice; // whose it is (its name, for the log)
};

struct Outcome {
    bool fileChanged[FILE_COUNT] = { false, false };
    std::string text[FILE_COUNT];
    int settingsTaken = 0;              // values that came from other devices
    std::vector<SaveWrite> saves;
    Baseline base;                      // to keep for the next sync
    DeviceRecord mine;                  // this device's record as it is now
    bool mineChanged = false;           // to upload
    std::vector<std::string> messages;  // for the player (a save conflict)
};

/* At launch, before the game reads its files: this device's changes into its
 * record, the others' winners into the files. `apply` false (after the game
 * wrote a file): only this device's changes go into its record. */
Outcome sync(const DeviceRecord &mine, const std::vector<DeviceRecord> &others, const LocalState &local,
             const Baseline &base, long long now, bool apply);

/* GoldenEye's EEPROM: a slot in use with a finished level, a cheat or 007 mode
 * (ios_icloud.mm's rule; another layout counts as progress) */
bool saveHasProgress(const std::string &data);

std::string hashText(const std::string &data);   // change detection only (FNV-1a 64, hex)
std::string base64Encode(const std::string &in);
bool base64Decode(const std::string &in, std::string *out);

}   // namespace gsync

#endif
