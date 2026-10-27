/*
 * isosession.cpp
 * See isosession.h.
 */
#include "isosession.h"

#include <atomic>
#include <cstring>
#include <string>

namespace
{
/* bkisofs' warning callback has no VolInfo argument, so the session that owns
 * the callbacks is tracked globally. The application uses one session. */
std::atomic<IsoSession*> g_session{nullptr};

const char* const kUnknownErrorText = "Double oops, unusable error number used";
}

IsoSession::IsoSession()
{
    g_session.store(this);
}

IsoSession::~IsoSession()
{
    close();
    IsoSession* self = this;
    g_session.compare_exchange_strong(self, nullptr);
}

/* ------------------------------------------------------------------ errors */

std::string IsoSession::errorString(int rc)
{
    std::string text;
    if (rc > 0)
        return "Success";

    /* some codes are missing from bkError.c's table; bk_get_error_string()
     * prints "unknown error" to stdout for those, so handle them first */
    if (rc == BKERROR_NOT_REG_FILE_FOR_BR)
        text = BKERROR_NOT_REG_FILE_FOR_BR_TEXT;
    else if (rc == BKERROR_NOT_DIR_IN_PATH)
        text = BKERROR_NOT_DIR_IN_PATH_TEXT;
    else
        text = bk_get_error_string(rc);

    if (text == kUnknownErrorText)
        text = "Unknown bkisofs error";

    return text + " (code " + std::to_string(rc) + ")";
}

/* ------------------------------------------------------------------- paths */

std::string IsoSession::joinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty() || dir == "/")
        return "/" + name;
    if (dir.back() == '/')
        return dir + name;
    return dir + "/" + name;
}

std::string IsoSession::parentPath(const std::string& path)
{
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    const std::string::size_type slash = p.rfind('/');
    if (slash == std::string::npos || slash == 0)
        return "/";
    return p.substr(0, slash);
}

std::string IsoSession::baseName(const std::string& path)
{
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    const std::string::size_type slash = p.rfind('/');
    if (slash == std::string::npos)
        return p;
    return p.substr(slash + 1);
}

std::string IsoSession::dirArg(const std::string& dir)
{
    if (dir.empty())
        return "/";
    if (dir.back() == '/')
        return dir;
    return dir + "/";
}

/* --------------------------------------------------------------- callbacks */

void IsoSession::setCallbacks(PulseFn pulse, PercentFn percent, WarningFn warning)
{
    pulse_ = std::move(pulse);
    percent_ = std::move(percent);
    warning_ = std::move(warning);
}

void IsoSession::cbPulse(VolInfo*)
{
    IsoSession* s = g_session.load();
    if (s && s->pulse_)
        s->pulse_();
}

void IsoSession::cbPercent(VolInfo*, double percent)
{
    IsoSession* s = g_session.load();
    if (s && s->percent_)
        s->percent_(percent);
}

bool IsoSession::cbWarning(const char* message)
{
    IsoSession* s = g_session.load();
    if (s && s->warning_)
        return s->warning_(message ? message : "");
    return false; /* no handler: stop the operation */
}

void IsoSession::cancel()
{
    cancelRequested_.store(true);
    std::lock_guard<std::mutex> lock(volMutex_);
    if (vol_)
        bk_cancel_operation(vol_);
}

void IsoSession::setFollowSymlinks(bool on)
{
    followSymlinks_ = on;
    if (vol_)
        bk_set_follow_symlinks(vol_, on);
}

/* --------------------------------------------------------------- lifecycle */

int IsoSession::initVol()
{
    destroyVol();

    /* VolInfo embeds two 100 KB buffers, keep it off the stack */
    VolInfo* vol = new VolInfo;
    const int rc = bk_init_vol_info(vol, scanDuplicates_);
    if (failed(rc)) {
        delete vol;
        return rc;
    }
    vol->warningCbk = &IsoSession::cbWarning;
    bk_set_follow_symlinks(vol, followSymlinks_);

    std::lock_guard<std::mutex> lock(volMutex_);
    vol_ = vol;
    return 1;
}

void IsoSession::destroyVol()
{
    VolInfo* vol;
    {
        std::lock_guard<std::mutex> lock(volMutex_);
        vol = vol_;
        vol_ = nullptr;
    }
    if (!vol)
        return;
    bk_destroy_vol_info(vol);
    delete vol;
}

void IsoSession::close()
{
    destroyVol();
    sourcePath_.clear();
    sourceNamespaces_ = 0;
    loadedNamespace_ = 0;
}

int IsoSession::newImage()
{
    close();
    const int rc = initVol();
    if (failed(rc))
        return rc;
    bk_set_vol_name(vol_, "CDROM");
    return 1;
}

int IsoSession::readTree(int filenameType)
{
    /* posix permissions only exist in Rock Ridge records */
    const bool keepPosix = (filenameType == FNTYPE_ROCKRIDGE);
    return bk_read_dir_tree(vol_, filenameType, keepPosix, &IsoSession::cbPulse);
}

int IsoSession::open(const std::string& isoPath)
{
    static const int order[] = { FNTYPE_ROCKRIDGE, FNTYPE_JOLIET, FNTYPE_9660 };
    int rc = BKERROR_UNKNOWN_FILENAME_TYPE;

    close();
    cancelRequested_.store(false);

    for (int type : order) {
        if (cancelRequested_.load()) {
            rc = BKERROR_OPER_CANCELED_BY_USER;
            break;
        }

        rc = initVol();
        if (failed(rc))
            break;

        rc = bk_open_image(vol_, isoPath.c_str());
        if (failed(rc))
            break;

        rc = bk_read_vol_info(vol_);
        if (failed(rc))
            break;

        sourceNamespaces_ = vol_->filenameTypes;
        if (type != FNTYPE_9660 && !(vol_->filenameTypes & type))
            continue;

        rc = readTree(type);
        if (!failed(rc)) {
            sourcePath_ = isoPath;
            loadedNamespace_ = type;
            return 1;
        }

        if (rc == BKERROR_OPER_CANCELED_BY_USER)
            break;

        /* a partly read tree cannot be reused, start over with the next
         * namespace (e.g. Rock Ridge without PX entries) */
    }

    close();
    return rc;
}

/* ---------------------------------------------------------------- browsing */

const BkDir* IsoSession::findDir(const std::string& dirPath) const
{
    if (!vol_)
        return nullptr;
    BkDir* dir = nullptr;
    const std::string arg = dirArg(dirPath);
    if (failed(bk_get_dir_from_string(vol_, arg.c_str(), &dir)))
        return nullptr;
    return dir;
}

int IsoSession::list(const std::string& dirPath, std::vector<IsoEntry>& out) const
{
    out.clear();
    const BkDir* dir = findDir(dirPath);
    if (!dir)
        return BKERROR_DIR_NOT_FOUND_ON_IMAGE;

    for (const BkFileBase* child = dir->children; child; child = child->next) {
        IsoEntry e;
        e.name = child->name;
        e.mode = child->posixFileMode;
        if (IS_DIR(child->posixFileMode)) {
            e.kind = IsoEntry::Directory;
        } else if (IS_REG_FILE(child->posixFileMode)) {
            const BkFile* f = BK_FILE_PTR(child);
            e.kind = IsoEntry::File;
            e.size = f->size;
            e.onImage = f->onImage;
        } else if (IS_SYMLINK(child->posixFileMode)) {
            e.kind = IsoEntry::Symlink;
            e.symlinkTarget = BK_SYMLINK_PTR(child)->target;
        }
        out.push_back(std::move(e));
    }
    return 1;
}

bool IsoSession::exists(const std::string& dirPath, const std::string& name) const
{
    const BkDir* dir = findDir(dirPath);
    if (!dir)
        return false;
    for (const BkFileBase* c = dir->children; c; c = c->next)
        if (name == c->name)
            return true;
    return false;
}

bool IsoSession::isDirectory(const std::string& path) const
{
    return findDir(path) != nullptr;
}

unsigned long long IsoSession::countRecursive(const BkDir* dir)
{
    unsigned long long n = 0;
    for (const BkFileBase* c = dir->children; c; c = c->next) {
        ++n;
        if (IS_DIR(c->posixFileMode))
            n += countRecursive(BK_DIR_PTR(c));
    }
    return n;
}

unsigned long long IsoSession::countItems() const
{
    return vol_ ? countRecursive(&vol_->dirTree) : 0;
}

/* ----------------------------------------------------------------- editing */

int IsoSession::add(const std::string& fsPath, const std::string& destDir)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_add(vol_, fsPath.c_str(), dirArg(destDir).c_str(), &IsoSession::cbPulse);
}

int IsoSession::addAs(const std::string& fsPath, const std::string& destDir,
                      const std::string& newName)
{
    if (!vol_)
        return BKERROR_SANITY;

    /* bk_add_as() checks the destination for the *source* name rather than
     * newName, so it refuses "add foo as bar" when foo already exists and
     * would silently create a second "bar". Check newName here and move a
     * clashing source-named item out of the way for the duration of the call. */
    if (exists(destDir, newName))
        return BKERROR_DUPLICATE_ADD;

    const std::string srcName = baseName(fsPath);
    std::string parked;
    if (srcName != newName && exists(destDir, srcName)) {
        for (unsigned n = 0; parked.empty() || exists(destDir, parked); ++n)
            parked = ".bkiso-qt-parked-" + std::to_string(n);
        const int rc = bk_rename(vol_, joinPath(destDir, srcName).c_str(),
                                 parked.c_str());
        if (failed(rc))
            return rc;
    }

    const int rc = bk_add_as(vol_, fsPath.c_str(), dirArg(destDir).c_str(),
                             newName.c_str(), &IsoSession::cbPulse);

    if (!parked.empty())
        bk_rename(vol_, joinPath(destDir, parked).c_str(), srcName.c_str());

    return rc;
}

int IsoSession::createDir(const std::string& parentDir, const std::string& name)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_create_dir(vol_, dirArg(parentDir).c_str(), name.c_str());
}

int IsoSession::remove(const std::string& path)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_delete(vol_, path.c_str());
}

int IsoSession::rename(const std::string& path, const std::string& newName)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_rename(vol_, path.c_str(), newName.c_str());
}

int IsoSession::extract(const std::string& isoPath, const std::string& fsDestDir,
                        bool keepPermissions)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_extract(vol_, isoPath.c_str(), fsDestDir.c_str(), keepPermissions,
                      &IsoSession::cbPulse);
}

int IsoSession::permissions(const std::string& path, mode_t& perms) const
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_get_permissions(vol_, path.c_str(), &perms);
}

int IsoSession::setPermissions(const std::string& path, mode_t perms)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_set_permissions(vol_, path.c_str(), perms);
}

/* ------------------------------------------------------- volume descriptor */

std::string IsoSession::volumeName() const
{
    return vol_ ? bk_get_volume_name(vol_) : std::string();
}

int IsoSession::setVolumeName(const std::string& name)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_set_vol_name(vol_, name.c_str());
}

std::string IsoSession::publisher() const
{
    return vol_ ? bk_get_publisher(vol_) : std::string();
}

int IsoSession::setPublisher(const std::string& publisher)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_set_publisher(vol_, publisher.c_str());
}

std::string IsoSession::dataPreparer() const
{
    return vol_ ? std::string(vol_->dataPreparer) : std::string();
}

time_t IsoSession::creationTime() const
{
    return vol_ ? bk_get_creation_time(vol_) : 0;
}

long long IsoSession::estimatedSize(int filenameTypes) const
{
    return vol_ ? (long long)bk_estimate_iso_size(vol_, filenameTypes) : 0;
}

/* ------------------------------------------------------------- boot record */

int IsoSession::bootMediaType() const
{
    return vol_ ? vol_->bootMediaType : BOOT_MEDIA_NONE;
}

unsigned IsoSession::bootRecordSize() const
{
    return vol_ ? vol_->bootRecordSize : 0;
}

bool IsoSession::bootRecordIsVisible() const
{
    return vol_ && vol_->bootMediaType != BOOT_MEDIA_NONE && vol_->bootRecordIsVisible;
}

bool IsoSession::findBootFile(const BkDir* dir, const std::string& dirPath,
                              std::string& out) const
{
    for (const BkFileBase* c = dir->children; c; c = c->next) {
        const std::string path = joinPath(dirPath, c->name);
        if (IS_REG_FILE(c->posixFileMode) &&
            BK_FILE_PTR(c) == vol_->bootRecordOnImage) {
            out = path;
            return true;
        }
        if (IS_DIR(c->posixFileMode) && findBootFile(BK_DIR_PTR(c), path, out))
            return true;
    }
    return false;
}

std::string IsoSession::bootFilePath() const
{
    std::string path;
    if (bootRecordIsVisible())
        findBootFile(&vol_->dirTree, "/", path);
    return path;
}

std::string IsoSession::bootMediaName(int mediaType)
{
    switch (mediaType) {
    case BOOT_MEDIA_NONE:         return "None";
    case BOOT_MEDIA_NO_EMULATION: return "No emulation";
    case BOOT_MEDIA_1_2_FLOPPY:   return "1.2 MB floppy emulation";
    case BOOT_MEDIA_1_44_FLOPPY:  return "1.44 MB floppy emulation";
    case BOOT_MEDIA_2_88_FLOPPY:  return "2.88 MB floppy emulation";
    case BOOT_MEDIA_HARD_DISK:    return "Hard disk emulation";
    default:                      return "Unknown";
    }
}

int IsoSession::addBootRecord(const std::string& fsPath, int mediaType)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_add_boot_record(vol_, fsPath.c_str(), mediaType);
}

int IsoSession::setBootFile(const std::string& isoPath)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_set_boot_file(vol_, isoPath.c_str());
}

void IsoSession::removeBootRecord()
{
    if (vol_)
        bk_delete_boot_record(vol_);
}

int IsoSession::extractBootRecord(const std::string& fsPath, unsigned perms)
{
    if (!vol_)
        return BKERROR_SANITY;
    return bk_extract_boot_record(vol_, fsPath.c_str(), perms);
}

/* ----------------------------------------------------------------- writing */

int IsoSession::write(const std::string& outPath, int filenameTypes, time_t created)
{
    if (!vol_)
        return BKERROR_SANITY;
    /* bk_write_image() calls the progress function unconditionally, so a
     * non-NULL callback is mandatory */
    return bk_write_image(outPath.c_str(), vol_, created,
                          filenameTypes | FNTYPE_9660, &IsoSession::cbPercent);
}
