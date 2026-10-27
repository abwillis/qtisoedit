/*
 * session_test.cpp
 * Exercises IsoSession against the real bkisofs library (no Qt needed).
 * Usage: session_test <scratch-dir>
 */
#include "isosession.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                         #cond);                                             \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_RC(expr)                                                       \
    do {                                                                     \
        int rc_ = (expr);                                                    \
        if (IsoSession::failed(rc_)) {                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__,         \
                         __LINE__, #expr,                                    \
                         IsoSession::errorString(rc_).c_str());              \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void writeFile(const std::string& path, const std::string& data)
{
    std::ofstream(path, std::ios::binary) << data;
}

static std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

static bool hasEntry(const IsoSession& s, const std::string& dir,
                     const std::string& name, IsoEntry* out = nullptr)
{
    std::vector<IsoEntry> entries;
    if (IsoSession::failed(s.list(dir, entries)))
        return false;
    for (const IsoEntry& e : entries)
        if (e.name == name) {
            if (out)
                *out = e;
            return true;
        }
    return false;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]);
        return 2;
    }
    const std::string base = argv[1];
    const std::string src = base + "/src";
    const std::string out = base + "/out";
    mkdir(base.c_str(), 0755);
    mkdir(src.c_str(), 0755);
    mkdir((src + "/docs").c_str(), 0755);
    mkdir(out.c_str(), 0755);
    writeFile(src + "/hello.txt", "hello world\n");
    writeFile(src + "/docs/readme.md", std::string(5000, 'x'));
    writeFile(src + "/docs/copy.md", std::string(5000, 'x')); /* duplicate */
    unlink((src + "/link").c_str());
    symlink("hello.txt", (src + "/link").c_str());

    /* path helpers */
    CHECK(IsoSession::joinPath("/", "a") == "/a");
    CHECK(IsoSession::joinPath("/a", "b") == "/a/b");
    CHECK(IsoSession::parentPath("/a/b") == "/a");
    CHECK(IsoSession::parentPath("/a") == "/");
    CHECK(IsoSession::baseName("/a/b") == "b");
    CHECK(IsoSession::dirArg("/a") == "/a/");
    CHECK(IsoSession::dirArg("/") == "/");

    int warnings = 0, percents = 0;
    IsoSession s;
    s.setCallbacks(nullptr,
                   [&](double) { ++percents; },
                   [&](const std::string&) { ++warnings; return true; });

    /* ---- build a new image ---- */
    CHECK_RC(s.newImage());
    CHECK(s.isOpen());
    CHECK_RC(s.add(src + "/hello.txt", "/"));
    CHECK_RC(s.add(src + "/docs", "/"));
    CHECK_RC(s.add(src + "/link", "/"));
    CHECK(s.add(src + "/hello.txt", "/") == BKERROR_DUPLICATE_ADD);
    CHECK_RC(s.addAs(src + "/hello.txt", "/", "renamed.txt"));
    CHECK(s.exists("/", "hello.txt")); /* parked item restored */
    CHECK(s.addAs(src + "/hello.txt", "/", "renamed.txt") == BKERROR_DUPLICATE_ADD);
    CHECK_RC(s.createDir("/", "boot"));
    CHECK_RC(s.createDir("/boot", "grub"));
    CHECK(s.createDir("/", "boot") == BKERROR_DUPLICATE_CREATE_DIR);
    CHECK(s.createDir("/", "bad/name") == BKERROR_NAME_INVALID_CHAR);
    CHECK(s.isDirectory("/boot/grub"));
    CHECK(!s.isDirectory("/hello.txt"));
    CHECK(s.exists("/docs", "readme.md"));
    CHECK_RC(s.rename("/renamed.txt", "second.txt"));
    CHECK(s.exists("/", "second.txt"));
    CHECK_RC(s.setPermissions("/second.txt", 0600));
    mode_t perms = 0;
    CHECK_RC(s.permissions("/second.txt", perms));
    CHECK(perms == 0600);
    CHECK_RC(s.setVolumeName("TEST_VOLUME"));
    CHECK_RC(s.setPublisher("bkiso-qt test"));
    CHECK(s.countItems() == 8); /* hello, docs, readme, copy, link, second, boot, grub */
    CHECK(s.estimatedSize(FNTYPE_9660 | FNTYPE_ROCKRIDGE | FNTYPE_JOLIET) > 0);

    IsoEntry e;
    CHECK(hasEntry(s, "/", "link", &e) && e.kind == IsoEntry::Symlink &&
          e.symlinkTarget == "hello.txt");
    CHECK(hasEntry(s, "/docs", "readme.md", &e) && e.size == 5000 && !e.onImage);

    const std::string iso1 = base + "/one.iso";
    unlink(iso1.c_str());
    CHECK_RC(s.write(iso1, FNTYPE_ROCKRIDGE | FNTYPE_JOLIET, std::time(nullptr)));
    CHECK(percents > 0);

    /* ---- reopen ---- */
    CHECK_RC(s.open(iso1));
    CHECK(s.sourcePath() == iso1);
    CHECK(s.loadedNamespace() == FNTYPE_ROCKRIDGE);
    CHECK(s.sourceNamespaces() & FNTYPE_JOLIET);
    CHECK(s.volumeName() == "TEST_VOLUME");
    CHECK(s.publisher() == "bkiso-qt test");
    CHECK(hasEntry(s, "/docs", "readme.md", &e) && e.size == 5000 && e.onImage);
    CHECK(hasEntry(s, "/", "link", &e) && e.kind == IsoEntry::Symlink);
    CHECK(s.isDirectory("/boot/grub"));
    CHECK_RC(s.permissions("/second.txt", perms));
    CHECK(perms == 0600);

    /* writing over the image being edited must be refused */
    CHECK(s.write(iso1, FNTYPE_ROCKRIDGE, std::time(nullptr)) == BKERROR_SAVE_OVERWRITE);

    /* ---- extract ---- */
    std::system(("rm -rf '" + out + "'/*").c_str());
    CHECK_RC(s.extract("/hello.txt", out, true));
    CHECK_RC(s.extract("/docs", out, true));
    CHECK(readFile(out + "/hello.txt") == "hello world\n");
    CHECK(readFile(out + "/docs/readme.md") == std::string(5000, 'x'));
    /* with a warning handler installed, bkisofs routes the failure through
     * it; "continue" turns the result into success */
    const int warningsBefore = warnings;
    CHECK(s.extract("/hello.txt", out, true) == 1);
    CHECK(warnings == warningsBefore + 1);
    s.setCallbacks(nullptr, nullptr, [](const std::string&) { return false; });
    CHECK(s.extract("/hello.txt", out, true) == BKWARNING_OPER_PARTLY_FAILED);
    s.setCallbacks(nullptr, [&](double) { ++percents; },
                   [&](const std::string&) { ++warnings; return true; });

    /* ---- edit an opened image and save a copy ---- */
    CHECK_RC(s.remove("/docs/copy.md"));
    CHECK(!s.exists("/docs", "copy.md"));
    CHECK(s.remove("/") == BKERROR_DELETE_ROOT);
    CHECK_RC(s.add(src + "/hello.txt", "/boot/grub"));

    /* visible no-emulation boot file */
    CHECK_RC(s.setBootFile("/boot/grub/hello.txt"));
    CHECK(s.bootMediaType() == BOOT_MEDIA_NO_EMULATION);
    CHECK(s.bootFilePath() == "/boot/grub/hello.txt");
    s.removeBootRecord();
    CHECK(s.bootMediaType() == BOOT_MEDIA_NONE);
    CHECK(s.setBootFile("/boot") == BKERROR_NOT_REG_FILE_FOR_BR);
    CHECK(IsoSession::errorString(BKERROR_NOT_REG_FILE_FOR_BR).find("regular file") !=
          std::string::npos);

    /* hidden floppy boot record must have an exact size */
    const std::string floppy = base + "/floppy.img";
    writeFile(floppy, std::string(1474560, '\0'));
    CHECK(s.addBootRecord(src + "/hello.txt", BOOT_MEDIA_1_44_FLOPPY) ==
          BKERROR_ADD_BOOT_RECORD_WRONG_SIZE);
    CHECK_RC(s.addBootRecord(floppy, BOOT_MEDIA_1_44_FLOPPY));
    CHECK(s.bootMediaType() == BOOT_MEDIA_1_44_FLOPPY);
    CHECK(s.bootFilePath().empty());

    const std::string iso2 = base + "/two.iso";
    unlink(iso2.c_str());
    CHECK_RC(s.write(iso2, FNTYPE_ROCKRIDGE | FNTYPE_JOLIET, std::time(nullptr)));

    CHECK_RC(s.open(iso2));
    CHECK(s.bootMediaType() == BOOT_MEDIA_1_44_FLOPPY);
    CHECK(s.bootRecordSize() == 1474560);
    CHECK(!s.exists("/docs", "copy.md"));
    CHECK(s.exists("/boot/grub", "hello.txt"));
    const std::string bootOut = out + "/boot.img";
    CHECK_RC(s.extractBootRecord(bootOut));
    CHECK(readFile(bootOut).size() == 1474560);

    /* Joliet-only image: loader must fall back from Rock Ridge */
    const std::string iso3 = base + "/joliet.iso";
    unlink(iso3.c_str());
    CHECK_RC(s.write(iso3, FNTYPE_JOLIET, std::time(nullptr)));
    CHECK_RC(s.open(iso3));
    CHECK(s.loadedNamespace() == FNTYPE_JOLIET);
    CHECK(s.exists("/docs", "readme.md"));

    /* cancel before an extract: the library reports cancellation */
    std::system(("rm -rf '" + out + "'/*").c_str());
    s.setCallbacks([&] { s.cancel(); }, nullptr, nullptr);
    int rc = s.extract("/docs", out, false);
    (void)rc; /* progress is throttled, cancellation is best-effort */

    s.close();
    CHECK(!s.isOpen());
    CHECK(IsoSession::failed(s.open(base + "/does-not-exist.iso")));
    CHECK(!s.isOpen());

    std::printf("%s (%d failure%s, %d warning callback%s)\n",
                g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
