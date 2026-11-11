/* The launcher's Ghosts tab (docs/speedrun.md); gevr_ghosts_ui.h draws it.
 *
 * Dab's Mod's Ghost Share (port/src/ghostnet.c: upload, leaderboards,
 * download) against the lobby Worker's ghost boards (ghosts.ts), signed in with
 * the Friends identity: FriendsClient.java (pc_friends.cpp on PC) does the HTTP
 * on its own thread, as with every Friends request, and answers with GHOST*
 * events, which vr_friends.cpp hands here. The files are the ghost race's
 * (src/game/gevr_ghost.c through gevr_ghost.h): the best run on file for a
 * mission, the run a mission races (its "-race.txt"), and a run downloaded
 * from a board, kept as "<mission>-<friend code>.gvg".
 *
 * Boards are GoldenEye's: a hack's levels are other missions under the same
 * numbers, and the board would compare different games. A hack still races
 * its own best run on file. */
#if defined(ANDROID) || defined(GEVR_PC)
#include "vr_ghosts.h"
#include "gevr_ghosts_ui.h"
#include "gevr_ghost.h"
#include "gevr_romlayout.h"
#include "vr_friends.h"
#include "vr_settings.h"
#include "vr_log.h"
#include "net_match.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

extern "C" const char *fsFullPath(const char *relPath);   // port/src/fs.c
extern "C" void vrSettingsSave(void);

namespace {
const char *const kDifficulty[] = { "Agent", "Secret Agent", "00 Agent" };
const char *const kPlays[] = { "", "vr", "flat" };
const char *kUploadedFile = "$S/ghosts/uploaded.txt";   // "<file> <time60>": what the boards have of ours

GevrGhostsView s_view;
bool s_started = false;
const char *s_missionNames[32];
int s_missionStage[32];
std::string s_boardKey;                  // the board asked for, and shown
std::vector<GevrGhostRow> s_staging;
bool s_stagingOpen = false;
std::string s_racingCode;                // the friend code of the downloaded run this mission races
struct Upload { int mission, difficulty, time60; std::string file; };
std::map<std::string, Upload> s_uploads;   // by the path sent
std::vector<std::pair<int, int>> s_uploadQueue;   // Upload all's missions still to send
int s_uploadStored = 0, s_uploadSent = 0;
struct Download { int mission, difficulty, time60; std::string file, rel, name, code; };
std::map<long long, Download> s_downloads;
std::map<std::string, int> s_uploaded;   // file -> the time60 the boards have

std::string fullPath(const std::string &rel) { return fsFullPath(rel.c_str()); }
std::string fileOf(const std::string &rel) {
    const size_t slash = rel.find_last_of('/');
    return slash == std::string::npos ? rel : rel.substr(slash + 1);
}
std::string relPath(int mission, int difficulty, const char *what) {
    char rel[160];
    gevrGhostFilePath(rel, sizeof(rel), GEVR_CAMPAIGN_GE, s_missionStage[mission], difficulty, what);
    return rel;
}
std::string missionText(int mission, int difficulty) {
    return std::string(s_missionNames[mission]) + ", " + kDifficulty[difficulty];
}
std::string boardKey() {
    return "0|" + std::to_string(s_missionStage[s_view.mission]) + "|" + std::to_string(s_view.difficulty) + "|" + kPlays[s_view.plays];
}
void say(const std::string &text, bool bad) {
    s_view.message = text;
    s_view.messageBad = bad;
    vr_log("ghosts: %s", text.c_str());
}

void loadUploaded() {
    s_uploaded.clear();
    if (FILE *f = fopen(fullPath(kUploadedFile).c_str(), "r")) {
        char name[96];
        int time60;
        while (fscanf(f, "%95s %d", name, &time60) == 2) s_uploaded[name] = time60;
        fclose(f);
    }
}
void saveUploaded() {
    gevrGhostEnsureDir();
    if (FILE *f = fopen(fullPath(kUploadedFile).c_str(), "w")) {
        for (const auto &x : s_uploaded) fprintf(f, "%s %d\n", x.first.c_str(), x.second);
        fclose(f);
    }
}

void requestBoard() {
    s_boardKey = boardKey();
    s_stagingOpen = false;
    s_view.state = GEVR_GHOSTS_LOADING;
    s_view.page = 0;
    gevrFriendsSend("ghost-board|" + s_boardKey);
}

// the mission's own run, which run it races, and the board's marks for both
void readLocal() {
    const int mission = s_view.mission, difficulty = s_view.difficulty;
    GevrGhostInfo best, chosen;
    s_view.haveBest = gevrGhostFileInfo(relPath(mission, difficulty, "best").c_str(), &best) != 0;
    s_view.bestTime60 = best.time60;
    s_view.bestFlags = best.flags;
    s_view.bestUnranked = s_view.haveBest && (best.flags & GEVR_GHF_UNRANKED) != 0;
    char file[96];
    gevrGhostGetRace(GEVR_CAMPAIGN_GE, s_missionStage[mission], difficulty, file, sizeof(file));
    s_racingCode.clear();
    s_view.racingOther = false;
    if (file[0] && gevrGhostFileInfo(("$S/ghosts/" + std::string(file)).c_str(), &chosen)
        && chosen.stage == s_missionStage[mission] && chosen.difficulty == difficulty && chosen.campaign == GEVR_CAMPAIGN_GE) {
        s_view.racingOther = true;
        s_racingCode = chosen.owner;
        std::string name = "GE-" + s_racingCode;
        for (const GevrGhostRow &r : s_view.rows)
            if (r.code == s_racingCode) name = r.name + " (GE-" + r.code + ")";
        s_view.racing = name + ", " + gevrGhostTimeText(chosen.time60);
    } else if (s_view.haveBest) {
        s_view.racing = "your best run, " + gevrGhostTimeText(best.time60);
    } else {
        s_view.racing = "nothing yet (your first finish becomes the ghost)";
    }
    const std::string &me = gevrFriends().friendCode;
    s_view.bestRank = 0;
    for (size_t i = 0; i < s_view.rows.size(); i++) {
        GevrGhostRow &r = s_view.rows[i];
        r.mine = !me.empty() && r.code == me;
        r.racing = s_view.racingOther ? r.code == s_racingCode : r.mine && s_view.haveBest;
        if (r.mine && !s_view.bestRank) s_view.bestRank = (int)i + 1;
    }
}

// one mission's best run to the boards: false (and why) if it cannot go
bool startUpload(int mission, int difficulty, std::string *why) {
    const std::string rel = relPath(mission, difficulty, "best");
    GevrGhostInfo info;
    if (!gevrGhostFileInfo(rel.c_str(), &info)) { *why = "No run of yours on " + missionText(mission, difficulty) + "."; return false; }
    if (info.flags & GEVR_GHF_UNRANKED) {
        *why = missionText(mission, difficulty) + " was set with force pull, the AI ally or a changed movement speed: "
               "the boards rank the original rules.";
        return false;
    }
    const GevrFriendsModel &friends = gevrFriends();
    if (friends.status != "ready" || friends.friendCode.empty()) { *why = "Friends is still signing in; try again in a moment."; return false; }
    if (!gevrGhostStampOwner(rel.c_str(), friends.friendCode.c_str())) {
        *why = missionText(mission, difficulty) + " was set by another player, or could not be read.";
        return false;
    }
    const std::string path = fullPath(rel);
    s_uploads[path] = { mission, difficulty, info.time60, fileOf(rel) };
    gevrFriendsSend("ghost-upload|" + path);
    return true;
}

// Upload all: one at a time, the next when the last is answered
void pumpUploads() {
    while (!s_uploadQueue.empty() && s_uploads.empty()) {
        const std::pair<int, int> next = s_uploadQueue.front();
        s_uploadQueue.erase(s_uploadQueue.begin());
        std::string why;
        if (startUpload(next.first, next.second, &why)) s_uploadSent++;
        else vr_log("ghosts: skipped: %s", why.c_str());
    }
}

void uploadAll() {
    s_uploadQueue.clear();
    s_uploadStored = s_uploadSent = 0;
    int skipped = 0;
    for (int m = 0; m < netCoopMissionCount() && m < 32; m++)
        for (int d = 0; d < 3; d++) {
            GevrGhostInfo info;
            const std::string rel = relPath(m, d, "best");
            if (!gevrGhostFileInfo(rel.c_str(), &info)) continue;
            if (info.flags & GEVR_GHF_UNRANKED) { skipped++; continue; }
            const auto had = s_uploaded.find(fileOf(rel));
            if (had != s_uploaded.end() && had->second == info.time60) continue;   // the boards have this one
            s_uploadQueue.push_back({ m, d });
        }
    if (s_uploadQueue.empty()) {
        say(skipped ? "Nothing new to upload (runs with force pull, the AI ally or a changed speed are not ranked)."
                    : "Nothing new to upload.", false);
        return;
    }
    say("Uploading " + std::to_string(s_uploadQueue.size()) + " runs...", false);
    pumpUploads();
}

void race(long long id) {
    for (const GevrGhostRow &r : s_view.rows) {
        if (r.id != id) continue;
        if (r.mine) {   // your own run is your best on file
            gevrGhostSetRace(GEVR_CAMPAIGN_GE, s_missionStage[s_view.mission], s_view.difficulty, "");
            readLocal();
            return;
        }
        const std::string rel = relPath(s_view.mission, s_view.difficulty, r.code.c_str());
        gevrGhostEnsureDir();
        s_downloads[id] = { s_view.mission, s_view.difficulty, r.time60, fileOf(rel), rel, r.name, r.code };
        gevrFriendsSend("ghost-get|" + std::to_string(id) + "|" + fullPath(rel));
        say("Downloading " + r.name + "'s run...", false);
        return;
    }
}

void start() {
    s_started = true;
    s_view.missionCount = 0;
    for (int i = 0; i < netCoopMissionCount() && i < 32; i++) {
        s_missionNames[i] = netCoopMission(i)->name;
        s_missionStage[i] = netCoopMission(i)->level_id;
        s_view.missionCount++;
    }
    s_view.missions = s_missionNames;
    loadUploaded();
}
}

void gevrGhostsEvent(const std::vector<std::string> &f) {
    const std::string &kind = f[0];
    auto key = [&f](size_t at) {
        return f.size() >= at + 4 ? f[at] + "|" + f[at + 1] + "|" + f[at + 2] + "|" + f[at + 3] : std::string();
    };
    if (kind == "GHOSTS_BEGIN" && key(1) == s_boardKey) {
        s_staging.clear();
        s_stagingOpen = true;
    } else if (kind == "GHOST" && f.size() >= 6 && s_stagingOpen) {
        GevrGhostRow r;
        r.id = atoll(f[1].c_str());
        r.time60 = atoi(f[2].c_str());
        r.flags = atoi(f[3].c_str());
        r.code = f[4];
        r.name = f[5];
        s_staging.push_back(r);
    } else if (kind == "GHOSTS_END" && s_stagingOpen && key(1) == s_boardKey) {
        s_stagingOpen = false;
        s_view.rows = s_staging;
        s_view.state = GEVR_GHOSTS_LOADED;
        readLocal();
    } else if (kind == "GHOST_FAIL" && f.size() >= 7 && f[1] == "board" && key(2) == s_boardKey) {
        s_stagingOpen = false;
        s_view.state = GEVR_GHOSTS_FAILED;
        say("The board did not load: " + f[6], true);
    } else if (kind == "GHOST_UPLOADED" && f.size() >= 5) {
        const auto it = s_uploads.find(f[1]);
        if (it == s_uploads.end()) return;
        const Upload up = it->second;
        s_uploads.erase(it);
        const bool stored = f[2] == "1";
        s_uploaded[up.file] = up.time60;   // either way, the boards have this run or a faster one
        saveUploaded();
        if (stored) s_uploadStored++;
        if (s_uploadSent > 0) {
            if (s_uploadQueue.empty() && s_uploads.empty())
                say("Uploaded " + std::to_string(s_uploadSent) + " runs: " + std::to_string(s_uploadStored) + " on the boards.", false);
        } else {
            say(missionText(up.mission, up.difficulty) + (stored ? ": on the board at #" + f[3] + "." : ": " + f[4] + "."), false);
        }
        pumpUploads();
        if (up.mission == s_view.mission && up.difficulty == s_view.difficulty) requestBoard();
    } else if (kind == "GHOST_FAIL" && f.size() >= 4 && f[1] == "upload") {
        s_uploads.erase(f[2]);
        s_uploadQueue.clear();   // the rest would fail the same way (offline, signed out, too many)
        s_uploadSent = 0;
        say("Upload failed: " + f[3], true);
    } else if (kind == "GHOST_SAVED" && f.size() >= 3) {
        const long long id = atoll(f[1].c_str());
        const auto it = s_downloads.find(id);
        if (it == s_downloads.end()) return;
        const Download d = it->second;
        s_downloads.erase(it);
        GevrGhostInfo info;
        if (!gevrGhostFileInfo(d.rel.c_str(), &info) || info.stage != s_missionStage[d.mission] || info.difficulty != d.difficulty
            || info.campaign != GEVR_CAMPAIGN_GE) {
            say(d.name + "'s run did not check out; it is not raced.", true);
            return;
        }
        gevrGhostSetRace(GEVR_CAMPAIGN_GE, s_missionStage[d.mission], d.difficulty, d.file.c_str());
        if (!VrGhostRace) {
            VrGhostRace = 1;
            vrSettingsSave();
        }
        say("Racing " + d.name + "'s " + gevrGhostTimeText(info.time60) + " on " + missionText(d.mission, d.difficulty)
            + ": choose the mission in the game.", false);
        if (d.mission == s_view.mission && d.difficulty == s_view.difficulty) readLocal();
    } else if (kind == "GHOST_FAIL" && f.size() >= 4 && f[1] == "get") {
        s_downloads.erase(atoll(f[2].c_str()));
        say("Download failed: " + f[3], true);
    }
}

void gevrGhostsPage(const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    if (!s_started) start();
    s_view.note = VrCampaign != GEVR_CAMPAIGN_GE
        ? "The ghost boards are GoldenEye's: choose GoldenEye beside START. A hack's missions still race your own best run."
        : nullptr;
    if (s_view.note) {
        gevrGhostsPageDraw(s_view, gold, good, bad);
        return;
    }
    if (s_view.state == GEVR_GHOSTS_IDLE) {
        readLocal();
        requestBoard();
    }
    s_view.raceOn = VrGhostRace != 0;
    s_view.busy = !s_uploads.empty() || !s_uploadQueue.empty() || !s_downloads.empty();
    const GevrGhostsActions act = gevrGhostsPageDraw(s_view, gold, good, bad);
    if (act.selection || act.refresh) {
        readLocal();
        requestBoard();
    }
    if (act.upload) {
        std::string why;
        s_uploadSent = 0;
        if (startUpload(s_view.mission, s_view.difficulty, &why)) say("Uploading " + missionText(s_view.mission, s_view.difficulty) + "...", false);
        else say(why, true);
    }
    if (act.uploadAll) uploadAll();
    if (act.raceMine) {
        gevrGhostSetRace(GEVR_CAMPAIGN_GE, s_missionStage[s_view.mission], s_view.difficulty, "");
        readLocal();
    }
    if (act.raceOn) {
        VrGhostRace = 1;
        vrSettingsSave();
    }
    if (act.race >= 0) race(act.race);
}
#endif
