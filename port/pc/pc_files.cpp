/*
 * The PC services' files (pc_http.h): plain file work on UTF-8 paths, the
 * SharedPreferences stand-in (gevr_prefs.txt) and ModArchive's bounded zip
 * extraction, with miniz (port/vr/miniz) in place of java.util.zip.
 */

#include "pc_http.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>   // fsync
#endif

#include "../vr/miniz/miniz.h"

namespace fs = std::filesystem;

namespace pcnet {

static fs::path P(const std::string &s)
{
#ifdef _WIN32
    return fs::path(wide(s));
#else
    return fs::path(s);
#endif
}

FILE *openFile(const std::string &path, const char *mode)
{
#ifdef _WIN32
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

bool isFile(const std::string &path) { std::error_code ec; return fs::is_regular_file(P(path), ec); }
bool isDir(const std::string &path) { std::error_code ec; return fs::is_directory(P(path), ec); }

long long fileSize(const std::string &path)
{
    std::error_code ec;
    const auto n = fs::file_size(P(path), ec);
    return ec ? -1 : (long long)n;
}

long long fileTimeMs(const std::string &path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(wide(path).c_str(), GetFileExInfoStandard, &a)) return 0;
    const unsigned long long t = ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    return (long long)(t / 10000ULL) - 11644473600000LL;   // 100 ns since 1601 to ms since 1970
#else
    std::error_code ec;
    const auto t = fs::last_write_time(P(path), ec);
    if (ec) return 0;
#ifdef __APPLE__
    // Apple's libc++ has no clock_cast: the same instant through both clocks' now
    const auto sys = std::chrono::system_clock::now() +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
#else
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch()).count();
#endif
#endif
}

bool makeDirs(const std::string &path)
{
    std::error_code ec;
    fs::create_directories(P(path), ec);
    return fs::is_directory(P(path), ec);
}

bool removeFile(const std::string &path) { std::error_code ec; return fs::remove(P(path), ec) && !ec; }

bool deleteTree(const std::string &path)
{
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(P(path), ec))) return true;
    fs::remove_all(P(path), ec);
    return !ec;
}

bool movePath(const std::string &from, const std::string &to, bool replace)
{
#ifdef _WIN32
    // a running exe or a loaded DLL can be renamed, though not deleted or overwritten
    return MoveFileExW(wide(from).c_str(), wide(to).c_str(), replace ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
#else
    std::error_code ec;
    if (!replace && fs::exists(P(to), ec)) return false;
    fs::rename(P(from), P(to), ec);
    return !ec;
#endif
}

std::vector<std::string> listDir(const std::string &path)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(P(path), ec), end; !ec && it != end; it.increment(ec)) {
#ifdef _WIN32
        out.push_back(narrow(it->path().filename().wstring()));
#else
        out.push_back(it->path().filename().string());
#endif
    }
    return out;
}

bool readFile(const std::string &path, std::string *out, size_t max)
{
    out->clear();
    FILE *f = openFile(path, "rb");
    if (!f) return false;
    char buf[1 << 14];
    size_t n;
    while (out->size() < max && (n = fread(buf, 1, std::min(sizeof(buf), max - out->size()), f)) > 0) out->append(buf, n);
    fclose(f);
    return true;
}

std::string tailFile(const std::string &path, size_t max)
{
    std::string out;
    FILE *f = openFile(path, "rb");
    if (!f) return out;
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    const long long skip = size > (long long)max ? size - (long long)max : 0;
    _fseeki64(f, skip, SEEK_SET);
#else
    fseeko(f, 0, SEEK_END);
    const long long size = (long long)ftello(f);
    const long long skip = size > (long long)max ? size - (long long)max : 0;
    fseeko(f, (off_t)skip, SEEK_SET);
#endif
    char buf[1 << 14];
    size_t n;
    while (out.size() < max && (n = fread(buf, 1, std::min(sizeof(buf), max - out.size()), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

bool writeFileAtomic(const std::string &path, const std::string &data)
{
    const std::string tmp = path + ".tmp";
    FILE *f = openFile(tmp, "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size() && fflush(f) == 0;
#ifdef _WIN32
    if (ok) FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(f)));   // out.getFD().sync()
#else
    if (ok) fsync(fileno(f));
#endif
    fclose(f);
    if (!ok || !movePath(tmp, path, true)) {
        removeFile(tmp);
        return false;
    }
    return true;
}

long long freeSpace(const std::string &path)
{
    std::error_code ec;
    const fs::space_info s = fs::space(P(path), ec);
    return ec ? -1 : (long long)s.available;
}

std::string exeDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (n == 0 || n >= MAX_PATH * 4) return ".";
    return narrow(fs::path(std::wstring(buf, n)).parent_path().wstring());
#else
    // Linux: the folder of /proc/self/exe
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string(".") : self.parent_path().string();
#endif
}

/* ---- SharedPreferences: gevr_prefs.txt ---- */

static std::mutex s_prefsLock;
static const char *kPrefsFile = "gevr_prefs.txt";

static std::map<std::string, std::string> prefsRead()
{
    std::map<std::string, std::string> m;
    std::string text;
    if (!readFile(kPrefsFile, &text, 1 << 16)) return m;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#') m[line.substr(0, eq)] = line.substr(eq + 1);
        start = end + 1;
    }
    return m;
}

std::string prefGet(const std::string &key, const std::string &def)
{
    std::lock_guard<std::mutex> lock(s_prefsLock);
    const auto m = prefsRead();
    const auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

long long prefGetInt(const std::string &key, long long def)
{
    const std::string v = prefGet(key);
    return v.empty() ? def : strtoll(v.c_str(), nullptr, 10);
}

void prefSet(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(s_prefsLock);
    auto m = prefsRead();
    if (value.empty()) m.erase(key);
    else m[key] = clean(value);
    std::string text = "# GoldenEye VR's own state (the Quest keeps it in Android's SharedPreferences)\n";
    for (const auto &kv : m) text += kv.first + "=" + kv.second + "\n";
    writeFileAtomic(kPrefsFile, text);
}

void prefSetInt(const std::string &key, long long value) { prefSet(key, std::to_string(value)); }

/* ---- bounded zip extraction: ModArchive.unpack ---- */

/* A zip entry's name as a path inside the folder, or "" when it would leave it
 * (ModArchive's canonical-path check: "..", absolute paths, drive letters). */
static std::string safeRelative(const std::string &name)
{
    std::string out;
    size_t start = 0;
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return std::string();
    while (start <= name.size()) {
        size_t end = name.find_first_of("/\\", start);
        if (end == std::string::npos) end = name.size();
        const std::string part = name.substr(start, end - start);
        if (part == "..") return std::string();
        if (!part.empty() && part != ".") {
            // Windows drops trailing dots and spaces: "a." would be "a"
            if (part.back() == '.' || part.back() == ' ') return std::string();
            if (!out.empty()) out += '/';
            out += part;
        }
        start = end + 1;
    }
    return out;
}

struct UnzipSink {
    FILE *f;
    long long bytes, *total, maxEntry, maxTotal;
    const std::function<bool(int)> *progress;
    int percent;
    bool cancelled, tooBig;
};

static size_t unzipWrite(void *opaque, mz_uint64 ofs, const void *buf, size_t n)
{
    UnzipSink *s = (UnzipSink *)opaque;
    (void)ofs;
    if (s->progress && *s->progress && !(*s->progress)(s->percent)) { s->cancelled = true; return 0; }
    s->bytes += (long long)n;
    *s->total += (long long)n;
    if (s->bytes > s->maxEntry || *s->total > s->maxTotal) { s->tooBig = true; return 0; }
    return fwrite(buf, 1, n, s->f);
}

int unzipBounded(const std::string &zip, const std::string &dir, const UnzipRules &rules,
                 const std::function<bool(int percent)> &progress, std::vector<std::string> *written)
{
    FILE *zf = openFile(zip, "rb");
    if (!zf) throw HttpError{0, "could not open the download"};
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_cfile(&z, zf, (mz_uint64)fileSize(zip), 0)) {
        fclose(zf);
        throw HttpError{0, "the download is not a zip"};
    }
    std::set<std::string> paths;
    long long extracted = 0;
    int kept = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&z);
    std::string failure;
    bool cancelled = false;
    for (mz_uint i = 0; i < count && failure.empty() && !cancelled; i++) {
        const int percent = (int)std::min<long long>(99, (long long)(i + 1) * 100 / std::max<mz_uint>(1, count));
        if (progress && !progress(percent)) { cancelled = true; break; }
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) { failure = "damaged archive"; break; }
        std::string name = st.m_filename;
        if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
        std::string lower = name;
        for (char &c : lower) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        bool wanted = rules.kinds.empty();
        for (const std::string &k : rules.kinds) wanted = wanted || k.empty() || (lower.size() >= k.size() && lower.compare(lower.size() - k.size(), k.size(), k) == 0);
        if (!wanted) continue;
        if (!rules.stripPrefix.empty()) {
            if (name.compare(0, rules.stripPrefix.size(), rules.stripPrefix) != 0) { failure = "bad or duplicate path: " + name; break; }
            name = name.substr(rules.stripPrefix.size());
        }
        const std::string rel = safeRelative(name);
        std::string key = rel;   // NTFS ignores case: two names differing only so are one file
        for (char &c : key) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (rel.empty() || !paths.insert(key).second) { failure = "bad or duplicate path: " + name; break; }
        if ((long long)st.m_uncomp_size < rules.minEntry || (long long)st.m_uncomp_size > rules.maxEntry) {
            failure = "invalid entry size: " + name;
            break;
        }
        const std::string out = dir + "/" + rel;
        const size_t slash = out.find_last_of('/');
        if (!makeDirs(out.substr(0, slash))) { failure = "could not create pack folder"; break; }
        UnzipSink sink = { openFile(out, "wb"), 0, &extracted, rules.maxEntry, rules.maxTotal, &progress, percent, false, false };
        if (!sink.f) { failure = "could not write " + rel; break; }
        // miniz checks each entry's CRC-32 and size as java.util.zip did
        const bool ok = mz_zip_reader_extract_to_callback(&z, i, unzipWrite, &sink, 0) != 0;
        const bool closed = fclose(sink.f) == 0;
        if (sink.cancelled) { cancelled = true; break; }
        if (sink.tooBig) { failure = "pack exceeds extraction limit"; break; }
        if (!ok || !closed || sink.bytes != (long long)st.m_uncomp_size) { failure = "damaged archive entry: " + name; break; }
        if (written) written->push_back(rel);
        kept++;
    }
    mz_zip_reader_end(&z);
    fclose(zf);
    if (cancelled) throw HttpError{-1, "cancelled"};
    if (!failure.empty()) throw HttpError{0, failure};
    if (kept < rules.least) throw HttpError{0, "download is missing pack files"};
    return kept;
}

}   // namespace pcnet
