/**
 * The NAND on the host (port/docs/HOST_BUILD.md §22): a directory stands for
 * the Wii's file system.
 *
 * A NAND path maps to <root><path>: /title/00010000/52484145/data/RPParty.dat
 * is <root>/title/00010000/52484145/data/RPParty.dat. Relative paths are
 * relative to the current directory, which is the title's home directory
 * (/title/00010000/<disc ID in hex>/data) as on the Wii. PortNANDSetRoot
 * creates the home directory and /tmp. Paths with "." or ".." components are
 * refused (NAND_RESULT_INVALID), so nothing outside the root is reachable.
 *
 * The calls follow the SDK's results: NANDOpen does not truncate (a write
 * overwrites in place, as on the Wii), NANDCreate of an existing file returns
 * NAND_RESULT_EXISTS, a missing file is NAND_RESULT_NOEXISTS, NANDRead /
 * NANDWrite return the byte count, NANDSeek the new position, NANDMove moves a
 * file into a directory. NANDCheck always answers "enough space".
 * NANDPrivateSafeOpen / NANDSafeClose (the SDK copies the file to /tmp and
 * back) are a plain open and close.
 *
 * Asynchronous calls do the work at once and deliver the callback later: the
 * NAND is a device of the cooperative thread system (PortOSAddDevice), so the
 * callbacks run when every thread is blocked or the main thread idles, in
 * interrupt context, as IPC replies do on the Wii.
 *
 * NANDInitBanner fills the banner as the SDK does; on the host the title
 * strings are stored as UTF-16BE (the file format; host wide strings are
 * little-endian, §6). The banner's numeric fields are big-endian through the
 * wrapper (include/revolution/NAND/nand.h).
 */

#include <revolution/NAND.h>
#include <revolution/OS.h>

#include <port/host.h>
#include <port/platform.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const int MAX_FD = 32;

char sRoot[512];
char sHomeDir[FS_MAX_PATH];
FILE* sFiles[MAX_FD];
bool sDeviceAdded = false;

struct Pending {
    NANDAsyncCallback callback;
    NANDCommandBlock* block;
    s32 result;
};
Pending* sPending = NULL;
u32 sPendingCount = 0;
u32 sPendingCap = 0;

[[noreturn]] void Fail(const char* what) {
    std::fprintf(stderr, "host_nand: %s\n", what);
    std::abort();
}

// Device function: deliver the callbacks of the async calls made so far.
int NandRun(void) {
    if (sPendingCount == 0) {
        return 0;
    }
    // Callbacks may queue more calls; take the list first.
    Pending* list = sPending;
    u32 count = sPendingCount;
    sPending = NULL;
    sPendingCount = 0;
    sPendingCap = 0;
    for (u32 i = 0; i < count; i++) {
        list[i].callback(list[i].result, list[i].block);
    }
    std::free(list);
    return 1;
}

s32 Complete(s32 result, NANDAsyncCallback callback, NANDCommandBlock* block) {
    if (callback == NULL) {
        return result < 0 ? result : NAND_RESULT_OK;
    }
    if (sPendingCount == sPendingCap) {
        sPendingCap = sPendingCap ? sPendingCap * 2 : 8;
        sPending = static_cast<Pending*>(std::realloc(sPending, sPendingCap * sizeof(Pending)));
        if (sPending == NULL) {
            Fail("out of memory");
        }
    }
    Pending& p = sPending[sPendingCount++];
    p.callback = callback;
    p.block = block;
    p.result = result;
    return NAND_RESULT_OK;
}

// NAND path -> absolute NAND path -> host path.
s32 HostPath(const char* path, char* out, size_t outSize) {
    if (sRoot[0] == '\0') {
        Fail("NAND call before PortNANDSetRoot");
    }
    char abs[2 * FS_MAX_PATH];
    if (path == NULL || path[0] == '\0') {
        return NAND_RESULT_INVALID;
    }
    if (path[0] == '/') {
        std::snprintf(abs, sizeof(abs), "%s", path);
    } else {
        std::snprintf(abs, sizeof(abs), "%s/%s", sHomeDir, path);
    }
    if (std::strlen(abs) >= FS_MAX_PATH) {
        return NAND_RESULT_INVALID;
    }
    // No "." / ".." components and no empty ones (other than a trailing '/').
    for (const char* p = abs; *p;) {
        if (*p == '/') {
            p++;
            continue;
        }
        const char* e = std::strchr(p, '/');
        size_t n = e ? static_cast<size_t>(e - p) : std::strlen(p);
        if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
            return NAND_RESULT_INVALID;
        }
        p += n;
    }
    if (std::snprintf(out, outSize, "%s%s", sRoot, abs) >= static_cast<int>(outSize)) {
        return NAND_RESULT_INVALID;
    }
    return NAND_RESULT_OK;
}

s32 ErrnoResult() {
    switch (errno) {
    case ENOENT:
    case ENOTDIR:
        return NAND_RESULT_NOEXISTS;
    case EEXIST:
        return NAND_RESULT_EXISTS;
    case EACCES:
    case EPERM:
        return NAND_RESULT_ACCESS;
    case ENOSPC:
        return NAND_RESULT_MAXBLOCKS;
    case ENOTEMPTY:
        return NAND_RESULT_NOTEMPTY;
    default:
        return NAND_RESULT_UNKNOWN;
    }
}

bool MakeDirs(const char* hostPath) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", hostPath);
    for (char* p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (PortMakeDir(buf) != 0) {
                return false;
            }
            *p = '/';
        }
    }
    return PortMakeDir(buf) == 0;
}

FILE* FileOf(NANDFileInfo* info) {
    if (info == NULL || info->fd < 0 || info->fd >= MAX_FD || sFiles[info->fd] == NULL) {
        return NULL;
    }
    return sFiles[info->fd];
}

s32 DoCreate(const char* path, bool dir) {
    char host[1024];
    s32 r = HostPath(path, host, sizeof(host));
    if (r != NAND_RESULT_OK) {
        return r;
    }
    struct stat st;
    if (stat(host, &st) == 0) {
        return NAND_RESULT_EXISTS;
    }
    if (dir) {
        return PortMakeDir(host) == 0 ? NAND_RESULT_OK : ErrnoResult();
    }
    FILE* f = std::fopen(host, "wb");
    if (f == NULL) {
        return ErrnoResult();
    }
    std::fclose(f);
    return NAND_RESULT_OK;
}

s32 DoDelete(const char* path) {
    char host[1024];
    s32 r = HostPath(path, host, sizeof(host));
    if (r != NAND_RESULT_OK) {
        return r;
    }
    struct stat st;
    if (stat(host, &st) != 0) {
        return NAND_RESULT_NOEXISTS;
    }
    if (S_ISDIR(st.st_mode)) {
        // The SDK deletes a directory with its contents.
        DIR* d = opendir(host);
        if (d != NULL) {
            while (dirent* e = readdir(d)) {
                if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                char sub[FS_MAX_PATH * 2];
                if (path[0] != '/') {
                    std::snprintf(sub, sizeof(sub), "%s/%s/%s", sHomeDir, path, e->d_name);
                } else {
                    std::snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
                }
                DoDelete(sub);
            }
            closedir(d);
        }
        return rmdir(host) == 0 ? NAND_RESULT_OK : ErrnoResult();
    }
    return unlink(host) == 0 ? NAND_RESULT_OK : ErrnoResult();
}

s32 DoOpen(const char* path, NANDFileInfo* info, u8 mode) {
    char host[1024];
    s32 r = HostPath(path, host, sizeof(host));
    if (r != NAND_RESULT_OK) {
        return r;
    }
    struct stat st;
    if (stat(host, &st) != 0) {
        return NAND_RESULT_NOEXISTS;
    }
    if (S_ISDIR(st.st_mode)) {
        return NAND_RESULT_INVALID;
    }
    const char* how;
    switch (mode) {
    case NAND_ACCESS_READ:
        how = "rb";
        break;
    case NAND_ACCESS_WRITE:
    case NAND_ACCESS_RW:
        how = "r+b"; // no truncation, as on the Wii
        break;
    default:
        return NAND_RESULT_INVALID;
    }
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (sFiles[i] == NULL) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        return NAND_RESULT_MAXFD;
    }
    FILE* f = std::fopen(host, how);
    if (f == NULL) {
        return ErrnoResult();
    }
    sFiles[fd] = f;
    std::memset(info, 0, sizeof(*info));
    info->fd = fd;
    info->tempFd = -1;
    std::snprintf(info->openPath, sizeof(info->openPath), "%s", path);
    info->access = mode;
    return NAND_RESULT_OK;
}

s32 DoClose(NANDFileInfo* info) {
    FILE* f = FileOf(info);
    if (f == NULL) {
        return NAND_RESULT_INVALID;
    }
    std::fclose(f);
    sFiles[info->fd] = NULL;
    info->fd = -1;
    return NAND_RESULT_OK;
}

s32 DoRead(NANDFileInfo* info, void* buf, u32 len) {
    FILE* f = FileOf(info);
    if (f == NULL || info->access == NAND_ACCESS_WRITE) {
        return NAND_RESULT_ACCESS;
    }
    return static_cast<s32>(std::fread(buf, 1, len, f));
}

s32 DoWrite(NANDFileInfo* info, const void* buf, u32 len) {
    FILE* f = FileOf(info);
    if (f == NULL || info->access == NAND_ACCESS_READ) {
        return NAND_RESULT_ACCESS;
    }
    size_t n = std::fwrite(buf, 1, len, f);
    std::fflush(f);
    return n == len ? static_cast<s32>(len) : NAND_RESULT_MAXBLOCKS;
}

s32 DoSeek(NANDFileInfo* info, s32 offset, NANDSeekMode whence) {
    FILE* f = FileOf(info);
    if (f == NULL) {
        return NAND_RESULT_INVALID;
    }
    int w = whence == NAND_SEEK_BEG ? SEEK_SET : whence == NAND_SEEK_CUR ? SEEK_CUR : SEEK_END;
    if (std::fseek(f, offset, w) != 0) {
        return NAND_RESULT_INVALID;
    }
    return static_cast<s32>(std::ftell(f));
}

s32 DoGetLength(NANDFileInfo* info, u32* length) {
    FILE* f = FileOf(info);
    if (f == NULL) {
        return NAND_RESULT_INVALID;
    }
    long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    *length = static_cast<u32>(std::ftell(f));
    std::fseek(f, pos, SEEK_SET);
    return NAND_RESULT_OK;
}

s32 DoGetType(const char* path, u8* type) {
    char host[1024];
    s32 r = HostPath(path, host, sizeof(host));
    if (r != NAND_RESULT_OK) {
        return r;
    }
    struct stat st;
    if (stat(host, &st) != 0) {
        return NAND_RESULT_NOEXISTS;
    }
    *type = S_ISDIR(st.st_mode) ? NAND_FILE_TYPE_DIR : NAND_FILE_TYPE_FILE;
    return NAND_RESULT_OK;
}

} // namespace

extern "C" {

/* The NAND's directory on the host; created if needed, with the title's home
 * directory (from the disc ID in the boot information, RHAE if there is none)
 * and /tmp. Returns 0 on success. */
int PortNANDSetRoot(const char* root) {
    std::snprintf(sRoot, sizeof(sRoot), "%s", root);
    size_t n = std::strlen(sRoot);
    while (n > 1 && sRoot[n - 1] == '/') {
        sRoot[--n] = '\0';
    }

    const char* game = "RHAE";
    char id[5] = {0};
    PortOSInit();
    const OSBootInfo* bi = static_cast<OSBootInfo*>(OSPhysicalToCached(OS_PHYS_BOOT_INFO));
    if (bi->diskID.game[0] != '\0') {
        std::memcpy(id, bi->diskID.game, 4);
        game = id;
    }
    std::snprintf(sHomeDir, sizeof(sHomeDir), "/title/00010000/%02x%02x%02x%02x",
                  static_cast<u8>(game[0]), static_cast<u8>(game[1]), static_cast<u8>(game[2]),
                  static_cast<u8>(game[3]));
    std::strcat(sHomeDir, "/data");

    char host[1024];
    std::snprintf(host, sizeof(host), "%s%s", sRoot, sHomeDir);
    if (!MakeDirs(host)) {
        return -1;
    }
    std::snprintf(host, sizeof(host), "%s/tmp", sRoot);
    if (!MakeDirs(host)) {
        return -1;
    }
    return 0;
}

/* The Mii database RFL reads at boot (RFLiBootLoadDatabaseAsync): RFLiDatabase,
 * 0x1F1E0 bytes ("RNOD", 100 Miis, the hidden database's header and a CRC over the
 * whole), then the hidden database's records. The copy goes to a temporary file in
 * the NAND and is renamed over the old one, so a failed copy leaves it as it was. */
int PortNANDImportMiiDB(const char* src, char* err, size_t errSize) {
    const char* const path = "/shared2/menu/FaceLib/RFL_DB.dat";
    const size_t headerSize = 0x1F1E0; // sizeof(RFLiDatabase)
    FILE* in = std::fopen(src, "rb");
    if (in == NULL) {
        std::snprintf(err, errSize, "cannot open %s: %s", src, std::strerror(errno));
        return -1;
    }
    std::fseek(in, 0, SEEK_END);
    long size = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);
    unsigned char* data = size > 0 ? static_cast<unsigned char*>(std::malloc(size)) : NULL;
    bool ok = data != NULL && std::fread(data, 1, size, in) == static_cast<size_t>(size);
    std::fclose(in);
    if (!ok || static_cast<size_t>(size) < headerSize || std::memcmp(data, "RNOD", 4) != 0) {
        std::free(data);
        std::snprintf(err, errSize, "%s is not a Mii database (RFL_DB.dat)", src);
        return -1;
    }
    // RFLiCalculateCRC over the header, its CRC included, is 0 for a good file
    u16 crc = 0;
    for (size_t i = 0; i < headerSize; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            crc = (crc & 0x8000) != 0 ? static_cast<u16>((crc << 1) ^ 0x1021) : static_cast<u16>(crc << 1);
            crc ^= (data[i] >> bit) & 1;
        }
    }
    if (crc != 0) {
        std::free(data);
        std::snprintf(err, errSize, "%s: the Mii database's checksum is wrong (a damaged file?)", src);
        return -1;
    }

    char host[1024], tmp[1100];
    if (HostPath(path, host, sizeof(host)) != NAND_RESULT_OK) {
        std::free(data);
        std::snprintf(err, errSize, "the NAND path is too long");
        return -1;
    }
    std::snprintf(tmp, sizeof(tmp), "%s.import", host);
    char dir[1024];
    std::snprintf(dir, sizeof(dir), "%s", host);
    *std::strrchr(dir, '/') = '\0';
    FILE* out = MakeDirs(dir) ? std::fopen(tmp, "wb") : NULL;
    ok = out != NULL && std::fwrite(data, 1, size, out) == static_cast<size_t>(size);
    ok = out != NULL && std::fclose(out) == 0 && ok;
    std::free(data);
    if (!ok || PortRenameReplace(tmp, host) != 0) {
        std::snprintf(err, errSize, "cannot write %s: %s", host, std::strerror(errno));
        std::remove(tmp);
        return -1;
    }
    return 0;
}

s32 NANDInit(void) {
    if (!sDeviceAdded) {
        sDeviceAdded = true;
        PortOSAddDevice(NandRun);
    }
    return NAND_RESULT_OK;
}

s32 NANDGetHomeDir(char* out) {
    std::strcpy(out, sHomeDir);
    return NAND_RESULT_OK;
}
s32 NANDGetCurrentDir(char* out) {
    std::strcpy(out, sHomeDir);
    return NAND_RESULT_OK;
}

s32 NANDCheck(u32 neededBlocks, u32 neededFiles, u32* answer) {
    (void)neededBlocks;
    (void)neededFiles;
    *answer = 0;
    return NAND_RESULT_OK;
}

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    return DoCreate(path, false);
}
s32 NANDPrivateCreate(const char* path, u8 perm, u8 attr) {
    return NANDCreate(path, perm, attr);
}
s32 NANDPrivateCreateAsync(const char* path, u8 perm, u8 attr, NANDAsyncCallback callback,
                           NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    return Complete(DoCreate(path, false), callback, block);
}
s32 NANDPrivateCreateDir(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    return DoCreate(path, true);
}
s32 NANDPrivateCreateDirAsync(const char* path, u8 perm, u8 attr, NANDAsyncCallback callback,
                              NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    return Complete(DoCreate(path, true), callback, block);
}

s32 NANDDelete(const char* path) {
    return DoDelete(path);
}
s32 NANDPrivateDelete(const char* path) {
    return DoDelete(path);
}
s32 NANDPrivateDeleteAsync(const char* path, NANDAsyncCallback callback, NANDCommandBlock* block) {
    return Complete(DoDelete(path), callback, block);
}

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 mode) {
    return DoOpen(path, info, mode);
}
s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 mode) {
    return DoOpen(path, info, mode);
}
s32 NANDOpenAsync(const char* path, NANDFileInfo* info, u8 mode, NANDAsyncCallback callback,
                  NANDCommandBlock* block) {
    return Complete(DoOpen(path, info, mode), callback, block);
}
s32 NANDPrivateOpenAsync(const char* path, NANDFileInfo* info, u8 mode,
                         NANDAsyncCallback callback, NANDCommandBlock* block) {
    return Complete(DoOpen(path, info, mode), callback, block);
}
s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, u8 access, void* buf,
                             u32 len, NANDAsyncCallback callback, NANDCommandBlock* block) {
    (void)buf;
    (void)len;
    return Complete(DoOpen(path, info, access), callback, block);
}

s32 NANDClose(NANDFileInfo* info) {
    return DoClose(info);
}
s32 NANDCloseAsync(NANDFileInfo* info, NANDAsyncCallback callback, NANDCommandBlock* block) {
    return Complete(DoClose(info), callback, block);
}
s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDAsyncCallback callback, NANDCommandBlock* block) {
    return Complete(DoClose(info), callback, block);
}

s32 NANDRead(NANDFileInfo* info, void* buf, u32 len) {
    return DoRead(info, buf, len);
}
s32 NANDReadAsync(NANDFileInfo* info, void* buf, u32 len, NANDAsyncCallback callback,
                  NANDCommandBlock* block) {
    return Complete(DoRead(info, buf, len), callback, block);
}
s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 len) {
    return DoWrite(info, buf, len);
}
s32 NANDWriteAsync(NANDFileInfo* info, const void* buf, u32 len, NANDAsyncCallback callback,
                   NANDCommandBlock* block) {
    return Complete(DoWrite(info, buf, len), callback, block);
}
s32 NANDSeek(NANDFileInfo* info, s32 offset, NANDSeekMode whence) {
    return DoSeek(info, offset, whence);
}
s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, NANDSeekMode whence,
                  NANDAsyncCallback callback, NANDCommandBlock* block) {
    return Complete(DoSeek(info, offset, whence), callback, block);
}
s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    return DoGetLength(info, length);
}
s32 NANDGetLengthAsync(NANDFileInfo* info, u32* lengthOut, NANDAsyncCallback callback,
                       NANDCommandBlock* block) {
    return Complete(DoGetLength(info, lengthOut), callback, block);
}

s32 NANDGetType(const char* path, u8* type) {
    return DoGetType(path, type);
}
s32 NANDPrivateGetTypeAsync(const char* path, u8* type, NANDAsyncCallback callback,
                            NANDCommandBlock* block) {
    return Complete(DoGetType(path, type), callback, block);
}

s32 NANDGetStatus(const char* path, NANDStatus* status) {
    u8 type;
    s32 r = DoGetType(path, &type);
    if (r == NAND_RESULT_OK) {
        std::memset(status, 0, sizeof(*status));
        status->perm = NAND_PERM_RUSR | NAND_PERM_WUSR;
    }
    return r;
}
s32 NANDPrivateGetStatusAsync(const char* path, NANDStatus* status, NANDAsyncCallback callback,
                              NANDCommandBlock* block) {
    return Complete(NANDGetStatus(path, status), callback, block);
}

/* Moves the file `from` into the directory `to` (keeping its name). */
s32 NANDMove(const char* from, const char* to) {
    char hostFrom[1024], hostDir[1024], hostTo[1100];
    s32 r = HostPath(from, hostFrom, sizeof(hostFrom));
    if (r != NAND_RESULT_OK) {
        return r;
    }
    r = HostPath(to, hostDir, sizeof(hostDir));
    if (r != NAND_RESULT_OK) {
        return r;
    }
    struct stat st;
    if (stat(hostFrom, &st) != 0) {
        return NAND_RESULT_NOEXISTS;
    }
    if (stat(hostDir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return NAND_RESULT_NOEXISTS;
    }
    const char* name = std::strrchr(hostFrom, '/');
    std::snprintf(hostTo, sizeof(hostTo), "%s%s", hostDir, name);
    // POSIX rename: an existing file of that name is replaced (MoveFileEx on Windows)
    return PortRenameReplace(hostFrom, hostTo) == 0 ? NAND_RESULT_OK : ErrnoResult();
}

void NANDSetUserData(NANDCommandBlock* block, void* data) {
    block->userData = data;
}
void* NANDGetUserData(NANDCommandBlock* block) {
    return block->userData;
}

void NANDInitBanner(NANDBanner* banner, u32 flags, const wchar_t* title, const wchar_t* subtitle) {
    std::memset(banner, 0, sizeof(NANDBanner));
    banner->flags = flags;
    banner->magic = 'WIBN'; // NAND_BANNER_MAGIC (NANDCore.c)

    // As the SDK: an empty string becomes " "; at most 32 code units. Stored
    // big-endian (the file format).
    const wchar_t* text[2] = {title, subtitle};
    wchar_t* dst[2] = {banner->title, banner->subtitle};
    for (int k = 0; k < 2; k++) {
        const wchar_t* s = text[k][0] == 0 ? L" " : text[k];
        u8* out = reinterpret_cast<u8*>(dst[k]);
        for (int i = 0; i < NAND_BANNER_TITLE_MAX && s[i] != 0; i++) {
            u16 c = static_cast<u16>(s[i]);
            out[2 * i] = static_cast<u8>(c >> 8);
            out[2 * i + 1] = static_cast<u8>(c);
        }
    }
}

} // extern "C"
