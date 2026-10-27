/*
 * isosession.h
 * Qt-free C++ wrapper around one bkisofs VolInfo.
 *
 * bkisofs keeps all state in a single VolInfo and reports progress through
 * plain C function pointers that carry no user-data argument. This class owns
 * the VolInfo, routes those callbacks to std::function objects, and normalises
 * the path conventions bkisofs expects (directory arguments for
 * bk_create_dir()/bk_get_dir_from_string() must end with '/').
 *
 * Thread model: one operation at a time. Long operations (open, add, extract,
 * write) may run on a worker thread as long as nothing else touches the
 * session until they return. cancel() may be called from any thread.
 */
#ifndef ISOSESSION_H
#define ISOSESSION_H

#include <ctime>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <sys/types.h>

#include "bk.h"

struct IsoEntry
{
    enum Kind { Directory, File, Symlink, Other };

    std::string name;            /* raw bytes as stored by bkisofs */
    Kind kind = Other;
    unsigned mode = 0;           /* full posix mode (type + permissions) */
    unsigned long long size = 0; /* regular files only */
    std::string symlinkTarget;   /* symlinks only */
    bool onImage = false;        /* false = added from the filesystem */
};

class IsoSession
{
public:
    using PulseFn = std::function<void()>;
    using PercentFn = std::function<void(double)>;
    using WarningFn = std::function<bool(const std::string&)>;

    IsoSession();
    ~IsoSession();
    IsoSession(const IsoSession&) = delete;
    IsoSession& operator=(const IsoSession&) = delete;

    /* error helpers */
    static bool failed(int rc) { return rc <= 0; }
    static std::string errorString(int rc);

    /* path helpers ("/" is the root, directories are stored without a
     * trailing slash except the root itself) */
    static std::string joinPath(const std::string& dir, const std::string& name);
    static std::string parentPath(const std::string& path);
    static std::string baseName(const std::string& path);
    static std::string dirArg(const std::string& dir); /* adds trailing '/' */

    /* callbacks used by long-running operations */
    void setCallbacks(PulseFn pulse, PercentFn percent, WarningFn warning);

    /* options */
    void setScanForDuplicates(bool on) { scanDuplicates_ = on; } /* next open/new */
    bool scanForDuplicates() const { return scanDuplicates_; }
    void setFollowSymlinks(bool on);
    bool followSymlinks() const { return followSymlinks_; }

    /* lifecycle */
    int newImage();
    int open(const std::string& isoPath);
    void close();
    bool isOpen() const { return vol_ != nullptr; }
    const std::string& sourcePath() const { return sourcePath_; }
    unsigned sourceNamespaces() const { return sourceNamespaces_; } /* FNTYPE_* mask */
    int loadedNamespace() const { return loadedNamespace_; }        /* one FNTYPE_* */

    /* browsing */
    int list(const std::string& dirPath, std::vector<IsoEntry>& out) const;
    bool exists(const std::string& dirPath, const std::string& name) const;
    bool isDirectory(const std::string& path) const;
    unsigned long long countItems() const;

    /* editing */
    int add(const std::string& fsPath, const std::string& destDir);
    int addAs(const std::string& fsPath, const std::string& destDir,
              const std::string& newName);
    int createDir(const std::string& parentDir, const std::string& name);
    int remove(const std::string& path);
    int rename(const std::string& path, const std::string& newName);
    int extract(const std::string& isoPath, const std::string& fsDestDir,
                bool keepPermissions);
    int permissions(const std::string& path, mode_t& perms) const;
    int setPermissions(const std::string& path, mode_t perms);

    /* volume descriptor */
    std::string volumeName() const;
    int setVolumeName(const std::string& name);
    std::string publisher() const;
    int setPublisher(const std::string& publisher);
    std::string dataPreparer() const;
    time_t creationTime() const;
    long long estimatedSize(int filenameTypes) const;

    /* El Torito boot record */
    int bootMediaType() const;                 /* BOOT_MEDIA_* */
    unsigned bootRecordSize() const;
    bool bootRecordIsVisible() const;
    std::string bootFilePath() const;          /* "" if hidden or none */
    static std::string bootMediaName(int mediaType);
    int addBootRecord(const std::string& fsPath, int mediaType);
    int setBootFile(const std::string& isoPath);
    void removeBootRecord();
    int extractBootRecord(const std::string& fsPath, unsigned perms = 0644);

    /* writing */
    int write(const std::string& outPath, int filenameTypes, time_t created);

    /* cancel the running add/extract/read/write */
    void cancel();

private:
    int initVol();
    void destroyVol();
    int readTree(int filenameType);
    const BkDir* findDir(const std::string& dirPath) const;
    bool findBootFile(const BkDir* dir, const std::string& dirPath,
                      std::string& out) const;
    static unsigned long long countRecursive(const BkDir* dir);

    /* C callbacks handed to bkisofs */
    static void cbPulse(VolInfo* vol);
    static void cbPercent(VolInfo* vol, double percent);
    static bool cbWarning(const char* message);

    VolInfo* vol_ = nullptr;
    std::mutex volMutex_;                 /* guards vol_ swaps vs. cancel() */
    std::atomic<bool> cancelRequested_{false};
    std::string sourcePath_;
    unsigned sourceNamespaces_ = 0;
    int loadedNamespace_ = 0;
    bool scanDuplicates_ = true;
    bool followSymlinks_ = false;

    PulseFn pulse_;
    PercentFn percent_;
    WarningFn warning_;
};

#endif
