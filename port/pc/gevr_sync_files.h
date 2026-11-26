/*
 * The cross-platform sync's ROMs and texture packs: gevr_sync_files.cpp,
 * docs/cloud-sync.md (step 3).
 */
#ifndef GEVR_SYNC_FILES_H
#define GEVR_SYNC_FILES_H

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gsync {

class Store;

struct LocalFile {
    std::string path;      // a ROM: its name in the ROM folder ("ge.z64"); a pack: "texture-packs/hd" in the save folder
    bool pack = false;
    long long bytes = 0;
    std::string stamp;     // changes when it does (size and time; a pack's count too)
};

struct FileEntry {         // a line of a device's index
    std::string path, blob;
    bool pack = false;
    long long bytes = 0;
    long long t = 0;       // when that device's copy changed (never older than what it had seen)
};
typedef std::map<std::string, FileEntry> FilesIndex;

struct FileSynced {        // this device's memory of a path
    std::string local;     // its stamp here then
    long long t = 0;       // the entry it was in step with
    std::string blob;
};

enum class FileMove { None, Adopt, Up, Down };
FileMove decideFileMove(const LocalFile *local, const FileEntry *winner, const FileSynced *synced);

std::string filesIndexName(const std::string &deviceId);   // "files-<id>.txt"
bool filesIndexNameId(const std::string &name, std::string *id);
std::string filesIndexText(const FilesIndex &ix);
bool filesIndexParse(const std::string &text, FilesIndex *out);
/* The ROMs sit where the launcher looks for them, the game's folder ($B: the
 * Quest's files/data, the PC's exe folder); the packs in the save folder's
 * texture-packs ($S). A ROM is known by its name alone, so the layouts meet. */
std::vector<LocalFile> localFiles(const std::string &root, const std::string &romDir, bool packs);
std::string localPathOf(const std::string &root, const std::string &romDir, const std::string &path, bool pack);

struct FilesPassArgs {
    std::string root, deviceId;
    std::string romDir;        // the ROMs' folder ($B)
    bool packs = false;
    long long now = 0;
    std::function<void(const std::string &line, float done)> progress;   // done < 0: unknown
    std::function<bool()> stop;
    std::function<bool()> hold;    // true: start no new item (a level is loaded); the one in flight finishes
    std::function<long long(const std::string &path)> freeBytes;         // -1 unknown
};

struct FilesResult {
    int moved = 0, failed = 0;
    std::string error;                     // the last failure's
    std::vector<std::string> arrived;      // put in place now
    std::vector<std::string> deferred;     // in place at the next launch
    std::vector<std::string> obsolete;     // blobs no index names any more (removed)
    bool held = false;                     // stopped early for a level (the rest at the next pass)
};

/* What a quit (or a crash, a battery) left mid-step, put right: at launch and at
 * the start of every pass. Partial downloads and zips in .gevr-sync/work/ go; a
 * half-unpacked pack in ready/ (no descriptor yet) goes; a ROM or pack left as
 * <target>.sync-old between the two renames of an install comes back when the
 * target is missing, and goes when the target is there. */
void filesRecover(const std::string &root, const std::string &romDir);

/* One pass: indexes read, uploads and downloads, this device's index written. */
FilesResult filesPass(Store *store, const FilesPassArgs &args);

/* At launch, before the game reads them: downloads that waited, put in place. */
int filesInstallReady(const std::string &root, const std::string &romDir, std::vector<std::string> *installed);

}   // namespace gsync

#endif
