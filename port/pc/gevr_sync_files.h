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
    std::string path;      // in the save folder: "ge.z64", "data/ge.z64", "texture-packs/hd"
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
std::vector<LocalFile> localFiles(const std::string &root, bool packs);

struct FilesPassArgs {
    std::string root, deviceId;
    bool packs = false;
    long long now = 0;
    std::function<void(const std::string &line, float done)> progress;   // done < 0: unknown
    std::function<bool()> stop;
    std::function<long long(const std::string &path)> freeBytes;         // -1 unknown
};

struct FilesResult {
    int moved = 0, failed = 0;
    std::string error;                     // the last failure's
    std::vector<std::string> arrived;      // put in place now
    std::vector<std::string> deferred;     // in place at the next launch
    std::vector<std::string> obsolete;     // blobs no index names any more (removed)
};

/* One pass: indexes read, uploads and downloads, this device's index written. */
FilesResult filesPass(Store *store, const FilesPassArgs &args);

/* At launch, before the game reads them: downloads that waited, put in place. */
int filesInstallReady(const std::string &root, std::vector<std::string> *installed);

}   // namespace gsync

#endif
