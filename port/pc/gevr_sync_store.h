/*
 * Where the device records live (docs/cloud-sync.md): flat names
 * ("device-<id>.txt") in one place the player owns. Google Drive is one;
 * a folder is the other (tests, development, GEVR_SYNC_DIR).
 */
#ifndef GEVR_SYNC_STORE_H
#define GEVR_SYNC_STORE_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gsync {

class Store {
public:
    virtual ~Store() {}
    virtual std::string label() const = 0;   // for the log and the status line
    /* every name there; false (and why) when the store can't be read */
    virtual bool list(std::vector<std::string> *names, std::string *why) = 0;
    virtual bool read(const std::string &name, std::string *data, std::string *why) = 0;
    /* replaces the whole file, never half of it */
    virtual bool write(const std::string &name, const std::string &data, std::string *why) = 0;
    /* a short wait for each answer (the save check at START) */
    virtual void setQuick(bool on) { (void)on; }

    /* ---- blobs: ROMs and packs by content (gevr_sync_files.cpp), any size ---- */
    typedef std::function<bool(long long done, long long total)> Progress;   // false: stop
    /* every blob there: id -> bytes */
    virtual bool listBlobs(std::map<std::string, long long> *blobs, std::string *why)
    {
        (void)blobs;
        *why = "no files in this store";
        return false;
    }
    /* the local file up as blob `id`; `label` names it for a person looking ("ge.z64") */
    virtual bool putBlob(const std::string &id, const std::string &label, const std::string &localPath,
                         const Progress &progress, std::string *why)
    {
        (void)id, (void)label, (void)localPath, (void)progress;
        *why = "no files in this store";
        return false;
    }
    /* blob `id` down into localPath (made whole or not at all) */
    virtual bool getBlob(const std::string &id, const std::string &localPath, const Progress &progress,
                         std::string *why)
    {
        (void)id, (void)localPath, (void)progress;
        *why = "no files in this store";
        return false;
    }
    virtual bool removeBlob(const std::string &id, std::string *why)
    {
        (void)id;
        *why = "no files in this store";
        return false;
    }
};

std::unique_ptr<Store> folderStore(const std::string &dir);

/* ---- Google Drive (gevr_sync_drive.cpp; not in the offline test) ---- */

bool driveAvailable();                                    // the build has a Google client
bool driveLinked(const std::string &stateDir);            // this device signed in
std::string driveEmail(const std::string &stateDir);      // which account, "" unknown
std::unique_ptr<Store> driveStore(const std::string &stateDir);   // null when not linked
void driveUnlink(const std::string &stateDir);
std::string driveSignedOut(const std::string &stateDir);   // why Google signed it out, "" if not

struct DriveSignIn {
    enum State { IDLE, STARTING, WAITING, DONE, FAILED } state = IDLE;
    std::string code, url;    // WAITING: what the player enters, and where
    std::string message;      // DONE: the account; FAILED: why
};
void driveSignInStart(const std::string &stateDir);   // in the background
void driveSignInCancel();
DriveSignIn driveSignInState();

}   // namespace gsync

#endif
