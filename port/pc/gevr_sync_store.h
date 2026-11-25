/*
 * Where the device records live (docs/cloud-sync.md): flat names
 * ("device-<id>.txt") in one place the player owns. Google Drive is one;
 * a folder is the other (tests, development, GEVR_SYNC_DIR).
 */
#ifndef GEVR_SYNC_STORE_H
#define GEVR_SYNC_STORE_H

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
};

std::unique_ptr<Store> folderStore(const std::string &dir);

}   // namespace gsync

#endif
