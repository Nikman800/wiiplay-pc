/**
 * The DVD drive on the host (port/docs/HOST_BUILD.md §22).
 *
 * The file system half of the SDK's DVD library is the decomp's own
 * src/revolution/DVD/dvdfs.c (DVDConvertPathToEntrynum, DVDOpen, DVDFastOpen,
 * DVDClose, DVDReadAsyncPrio, DVDReadPrio, DVDGetCurrentDir) with its command
 * queue src/revolution/DVD/dvdqueue.c. They walk the FST in memory, exactly as
 * on the Wii, and turn a file read into an absolute disc read. This file is
 * the drive underneath (what dvd.c and the DI interface do on the Wii):
 *
 *   - PortDVDSetDisc loads the disc's FST the way the apploader does: from
 *     sys/fst.bin of the extracted disc when it is there, otherwise built
 *     from the extracted files/ tree in the mastering tools' order (each
 *     directory's entries sorted by name, compared case-insensitively as
 *     upper case, files and directories mixed). The FST goes to the top of
 *     the MEM1 arena (the arena is lowered below it) with its three words per
 *     entry in host byte order (dvdfs.c reads them as native u32 values; the
 *     string table is unchanged). BootInfo->fstStart / fstSize point at it.
 *   - Disc addresses come from the FST: a read of disc bytes [a, a+n) is
 *     served from the extracted file whose FST extent contains a. Bytes past
 *     the end of the file read as zero (callers round sizes up to 32).
 *   - Commands (DVDReadAbsAsyncPrio, DVDCheckDiskAsync) are queued with the
 *     decomp's priority queue. The drive is a device of the cooperative
 *     thread system (PortOSAddDevice, src/host_thread.cpp): it completes
 *     every queued command when all threads are blocked or the main thread
 *     idles, and calls the callbacks in interrupt context. A thread that
 *     waits for a read (DVDReadPrio's OSSleepThread, EGG::DvdFile's message
 *     queue) therefore resumes after the read, as on the Wii.
 *   - DVDCancel / DVDCancelAsync take a waiting command off the queue.
 *
 * The disc root is configurable (PortDVDSetDisc arguments); nothing here
 * writes to it.
 */

#include <revolution/DVD.h>
#include <revolution/OS.h>

#include <port/arena.h>
#include <port/host.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void __DVDFSInit(void);
void __DVDClearWaitingQueue(void);
BOOL __DVDPushWaitingQueue(s32 prio, DVDCommandBlock* block);
DVDCommandBlock* __DVDPopWaitingQueue(void);
BOOL __DVDDequeueWaitingQueue(const DVDCommandBlock* block);
extern OSThreadQueue __DVDThreadQueue;
// dvd.c's: disc offsets in the FST are in 4-byte units (0) on the Wii.
volatile u32 __DVDLayoutFormat = 0;
}

namespace {

// Host command numbers (dvd.c's enum: COMMAND_READ is 1; the check is ours).
enum { HOST_COMMAND_READ = 1, HOST_COMMAND_CHECK_DISK = 0x100 };

struct FstFile {
    u64 discOffset; // bytes
    u32 size;
    char* path; // host path
};

// Plain malloc storage throughout: the platform layer must not allocate from
// the game's heaps (operator new is EGG::Heap::alloc when eggHeap.cpp is
// linked).
u8* sFst = NULL; // big-endian, as on the disc
u32 sFstSize = 0;
FstFile* sFiles = NULL; // sorted by discOffset
u32 sFileCount = 0;
char* sFilesRoot = NULL;
bool sFstFromTree = false;
bool sInitialized = false;
DVDCommandBlock* sExecuting = NULL;

[[noreturn]] void Fail(const char* what, const char* arg) {
    std::fprintf(stderr, "host_dvd: %s%s%s\n", what, arg ? ": " : "", arg ? arg : "");
    std::abort();
}

void* XMalloc(size_t n) {
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == NULL) {
        Fail("out of memory", NULL);
    }
    return p;
}

char* XStrdup(const char* s) {
    size_t n = std::strlen(s) + 1;
    char* p = static_cast<char*>(XMalloc(n));
    std::memcpy(p, s, n);
    return p;
}

u32 LoadBE32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}
void StoreBE32(u8* p, u32 v) {
    p[0] = u8(v >> 24);
    p[1] = u8(v >> 16);
    p[2] = u8(v >> 8);
    p[3] = u8(v);
}

bool ReadWholeFile(const char* path, u8** out, u32* size) {
    FILE* f = std::fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    u8* buf = static_cast<u8*>(XMalloc(n));
    bool ok = n >= 0 && std::fread(buf, 1, n, f) == static_cast<size_t>(n);
    std::fclose(f);
    if (!ok) {
        std::free(buf);
        return false;
    }
    *out = buf;
    *size = static_cast<u32>(n);
    return true;
}

/******************************************************************************
 * FST built from the extracted tree
 ******************************************************************************/

struct Growable {
    u8* data;
    u32 size;
    u32 cap;
    void Append(const void* p, u32 n) {
        if (size + n > cap) {
            cap = (size + n) * 2 + 256;
            data = static_cast<u8*>(std::realloc(data, cap));
            if (data == NULL) {
                Fail("out of memory", NULL);
            }
        }
        std::memcpy(data + size, p, n);
        size += n;
    }
};

// The mastering tools sort each directory's names case-insensitively, as
// upper case (Dolphin's DirectoryBlob reproduces the same order).
int CompareNames(const void* a, const void* b) {
    const char* x = *static_cast<char* const*>(a);
    const char* y = *static_cast<char* const*>(b);
    for (;; x++, y++) {
        int cx = std::toupper(static_cast<unsigned char>(*x));
        int cy = std::toupper(static_cast<unsigned char>(*y));
        if (cx != cy || cx == 0) {
            return cx - cy;
        }
    }
}

struct TreeBuilder {
    Growable nodes;
    Growable names;
    u32 count;
    u64 nextOffset; // bytes; files are laid out in FST order, 32 KiB aligned

    u32 AddNode(bool dir, const char* name, u32 w1, u32 w2) {
        u8 n[12];
        StoreBE32(n, (dir ? 0x01000000u : 0) | names.size);
        StoreBE32(n + 4, w1);
        StoreBE32(n + 8, w2);
        nodes.Append(n, 12);
        names.Append(name, static_cast<u32>(std::strlen(name)) + 1);
        return count++;
    }

    void Walk(const char* dirPath, u32 parent) {
        DIR* d = opendir(dirPath);
        if (d == NULL) {
            Fail("cannot list", dirPath);
        }
        char** list = NULL;
        u32 n = 0, cap = 0;
        while (dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                list = static_cast<char**>(std::realloc(list, cap * sizeof(char*)));
            }
            list[n++] = XStrdup(e->d_name);
        }
        closedir(d);
        std::qsort(list, n, sizeof(char*), CompareNames);

        for (u32 i = 0; i < n; i++) {
            size_t len = std::strlen(dirPath) + std::strlen(list[i]) + 2;
            char* path = static_cast<char*>(XMalloc(len));
            std::snprintf(path, len, "%s/%s", dirPath, list[i]);
            struct stat st;
            if (stat(path, &st) != 0) {
                Fail("cannot stat", path);
            }
            if (S_ISDIR(st.st_mode)) {
                u32 index = AddNode(true, list[i], parent, 0);
                Walk(path, index);
                StoreBE32(nodes.data + index * 12 + 8, count); // next: past the subtree
            } else {
                AddNode(false, list[i], static_cast<u32>(nextOffset >> 2),
                        static_cast<u32>(st.st_size));
                nextOffset += (static_cast<u64>(st.st_size) + 0x7FFF) & ~u64(0x7FFF);
            }
            std::free(path);
            std::free(list[i]);
        }
        std::free(list);
    }
};

void BuildFstFromTree(const char* root) {
    TreeBuilder b;
    std::memset(&b, 0, sizeof(b));
    b.nextOffset = 0x00100000; // anywhere past the disc header: only the order matters
    b.AddNode(true, "", 0, 0); // root; its name is not in the table on the disc
    b.names.size = 0;
    b.Walk(root, 0);
    StoreBE32(b.nodes.data + 8, b.count);

    sFstSize = b.nodes.size + b.names.size;
    sFst = static_cast<u8*>(XMalloc(sFstSize));
    std::memcpy(sFst, b.nodes.data, b.nodes.size);
    std::memcpy(sFst + b.nodes.size, b.names.data, b.names.size);
    std::free(b.nodes.data);
    std::free(b.names.data);
}

/******************************************************************************
 * Disc address -> extracted file
 ******************************************************************************/

int CompareFiles(const void* a, const void* b) {
    const FstFile* x = static_cast<const FstFile*>(a);
    const FstFile* y = static_cast<const FstFile*>(b);
    return x->discOffset < y->discOffset ? -1 : x->discOffset > y->discOffset ? 1 : 0;
}

// Walk the FST (big-endian) once: every file's extent and host path.
void IndexFiles() {
    u32 count = LoadBE32(sFst + 8);
    if (count == 0 || count * 12 > sFstSize) {
        Fail("bad FST", NULL);
    }
    const char* names = reinterpret_cast<const char*>(sFst + count * 12);
    sFiles = static_cast<FstFile*>(XMalloc(sizeof(FstFile) * count));
    sFileCount = 0;

    // Directory stack: the entry where each open directory ends.
    u32 ends[64];
    const char* dirNames[64];
    int depth = 0;
    char path[1024];
    for (u32 i = 1; i < count; i++) {
        while (depth > 0 && i >= ends[depth - 1]) {
            depth--;
        }
        const u8* e = sFst + i * 12;
        u32 w0 = LoadBE32(e);
        const char* name = names + (w0 & 0x00FFFFFF);
        if (w0 & 0xFF000000) {
            if (depth == 64) {
                Fail("FST too deep", NULL);
            }
            dirNames[depth] = name;
            ends[depth++] = LoadBE32(e + 8);
            continue;
        }
        size_t len = static_cast<size_t>(std::snprintf(path, sizeof(path), "%s", sFilesRoot));
        for (int k = 0; k < depth; k++) {
            len += static_cast<size_t>(
                std::snprintf(path + len, sizeof(path) - len, "/%s", dirNames[k]));
        }
        std::snprintf(path + len, sizeof(path) - len, "/%s", name);

        FstFile& f = sFiles[sFileCount++];
        f.discOffset = static_cast<u64>(LoadBE32(e + 4)) << 2;
        f.size = LoadBE32(e + 8);
        f.path = XStrdup(path);

        struct stat st;
        if (stat(path, &st) != 0 || static_cast<u64>(st.st_size) != f.size) {
            std::fprintf(stderr,
                         "host_dvd: warning: %s is missing or not %u bytes (the FST and the "
                         "extracted files disagree)\n",
                         path, static_cast<unsigned>(f.size));
        }
    }
    std::qsort(sFiles, sFileCount, sizeof(FstFile), CompareFiles);
}

const FstFile* FindFile(u64 discOffset) {
    u32 lo = 0, hi = sFileCount;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (sFiles[mid].discOffset <= discOffset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return NULL;
    }
    const FstFile* f = &sFiles[lo - 1];
    // The start must be inside the file (or at its end for an empty read).
    if (discOffset > f->discOffset + f->size) {
        return NULL;
    }
    return f;
}

s32 ReadDisc(u64 discOffset, void* dst, u32 length) {
    const FstFile* f = FindFile(discOffset);
    if (f == NULL) {
        std::fprintf(stderr, "host_dvd: read of disc offset 0x%llX is in no file\n",
                     static_cast<unsigned long long>(discOffset));
        return DVD_RESULT_FATAL_ERROR;
    }
    std::memset(dst, 0, length);
    u64 inFile = discOffset - f->discOffset;
    u64 avail = f->size - inFile;
    u32 n = static_cast<u32>(avail < length ? avail : length);
    if (n > 0) {
        FILE* fp = std::fopen(f->path, "rb");
        if (fp == NULL || std::fseek(fp, static_cast<long>(inFile), SEEK_SET) != 0 ||
            std::fread(dst, 1, n, fp) != n) {
            if (fp != NULL) {
                std::fclose(fp);
            }
            std::fprintf(stderr, "host_dvd: cannot read %s\n", f->path);
            return DVD_RESULT_FATAL_ERROR;
        }
        std::fclose(fp);
    }
    return static_cast<s32>(length);
}

/******************************************************************************
 * The drive
 ******************************************************************************/

// Device function (src/host_thread.cpp): completes every queued command.
int DriveRun(void) {
    int did = 0;
    for (;;) {
        DVDCommandBlock* block = __DVDPopWaitingQueue();
        if (block == NULL) {
            return did;
        }
        did = 1;
        sExecuting = block;
        block->state = DVD_STATE_BUSY;

        s32 result;
        if (block->command == HOST_COMMAND_READ) {
            result = ReadDisc(static_cast<u64>(block->offset) << 2, block->addr, block->length);
            if (result >= 0) {
                block->transferTotal = block->length;
                block->state = DVD_STATE_IDLE;
            } else {
                block->state = DVD_STATE_FATAL;
            }
        } else {
            // DVDCheckDiskAsync: the disc is always there.
            result = TRUE;
            block->state = DVD_STATE_IDLE;
        }
        sExecuting = NULL;
        if (block->callback != NULL) {
            block->callback(result, block);
        }
    }
}

BOOL IssueCommand(s32 prio, DVDCommandBlock* block) {
    if (!sInitialized) {
        Fail("DVD command before DVDInit", NULL);
    }
    BOOL enabled = OSDisableInterrupts();
    block->state = DVD_STATE_WAITING;
    BOOL ok = __DVDPushWaitingQueue(prio, block);
    OSRestoreInterrupts(enabled);
    return ok;
}

} // namespace

extern "C" {

int PortDVDSetDisc(const char* filesRoot, const char* fstPath) {
    if (sFst != NULL) {
        Fail("PortDVDSetDisc called twice", NULL);
    }
    PortOSInit();
    sFilesRoot = XStrdup(filesRoot);

    // sys/fst.bin next to files/ (the layout Dolphin's "Extract Entire Disc"
    // and wit produce), unless a path is given.
    char guess[1024];
    if (fstPath == NULL) {
        std::snprintf(guess, sizeof(guess), "%s/../sys/fst.bin", filesRoot);
        fstPath = guess;
    }
    if (fstPath[0] != '\0' && ReadWholeFile(fstPath, &sFst, &sFstSize)) {
        sFstFromTree = false;
    } else {
        BuildFstFromTree(filesRoot);
        sFstFromTree = true;
    }
    IndexFiles();

    // The apploader puts the FST at the top of MEM1 and lowers the arena.
    OSBootInfo* bi = static_cast<OSBootInfo*>(OSPhysicalToCached(OS_PHYS_BOOT_INFO));
    u8* hi = static_cast<u8*>(OSGetMEM1ArenaHi());
    u8* fst = reinterpret_cast<u8*>(
        (reinterpret_cast<uintptr_t>(hi) - sFstSize) & ~static_cast<uintptr_t>(31));
    std::memcpy(fst, sFst, sFstSize);
    u32 count = LoadBE32(sFst + 8);
    for (u32 i = 0; i < count * 3; i++) {
        u32 v = LoadBE32(fst + i * 4);
        std::memcpy(fst + i * 4, &v, 4); // host order for dvdfs.c
    }
    OSSetMEM1ArenaHi(fst);
    bi->arenaHi = fst;
    bi->fstStart = fst;
    bi->fstSize = sFstSize;

    // The disc ID, from the disc header when the extraction has it.
    char hdr[1024];
    std::snprintf(hdr, sizeof(hdr), "%s/../sys/boot.bin", filesRoot);
    u8* boot = NULL;
    u32 bootSize = 0;
    if (ReadWholeFile(hdr, &boot, &bootSize) && bootSize >= sizeof(DVDDiskID)) {
        std::memcpy(&bi->diskID, boot, sizeof(DVDDiskID));
        bi->diskID.rvlMagic = LoadBE32(boot + 0x18);
        bi->diskID.gcMagic = LoadBE32(boot + 0x1C);
    }
    std::free(boot);
    return sFstFromTree ? 1 : 0;
}

const void* PortDVDGetFst(u32* size, int* fromTree) {
    if (size != NULL) {
        *size = sFstSize;
    }
    if (fromTree != NULL) {
        *fromTree = sFstFromTree ? 1 : 0;
    }
    return sFst;
}

void DVDInit(void) {
    if (sInitialized) {
        return;
    }
    if (sFst == NULL) {
        Fail("DVDInit before PortDVDSetDisc", NULL);
    }
    sInitialized = true;
    __DVDLayoutFormat = 0;
    __DVDFSInit();
    __DVDClearWaitingQueue();
    OSInitThreadQueue(&__DVDThreadQueue);
    PortOSAddDevice(DriveRun);
}

BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* dst, u32 size, u32 offset,
                         DVDCommandCallback callback, s32 prio) {
    block->command = HOST_COMMAND_READ;
    block->addr = dst;
    block->length = size;
    block->offset = offset;
    block->transferTotal = 0;
    block->callback = callback;
    return IssueCommand(prio, block);
}

BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCommandCallback callback) {
    block->command = HOST_COMMAND_CHECK_DISK;
    block->transferTotal = 0;
    block->callback = callback;
    return IssueCommand(2, block);
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    BOOL enabled = OSDisableInterrupts();
    s32 state = block->state;
    OSRestoreInterrupts(enabled);
    return state;
}

s32 DVDGetDriveStatus(void) {
    if (sExecuting != NULL) {
        return DVD_STATE_BUSY;
    }
    return DVD_STATE_IDLE;
}

BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCommandCallback callback) {
    BOOL enabled = OSDisableInterrupts();
    switch (block->state) {
    case DVD_STATE_WAITING:
        __DVDDequeueWaitingQueue(block);
        block->state = DVD_STATE_CANCELED;
        if (block->callback != NULL) {
            block->callback(DVD_RESULT_CANCELED, block);
        }
        if (callback != NULL) {
            callback(DVD_RESULT_GOOD, block);
        }
        break;
    default:
        // Idle, finished, cancelled or failed: nothing to stop. (The host
        // drive never leaves a command half done between devices runs.)
        if (callback != NULL) {
            callback(DVD_RESULT_GOOD, block);
        }
        break;
    }
    OSRestoreInterrupts(enabled);
    return TRUE;
}

s32 DVDCancel(DVDCommandBlock* block) {
    DVDCancelAsync(block, NULL);
    return DVD_RESULT_GOOD;
}

const DVDDiskID* DVDGetCurrentDiskID(void) {
    return &static_cast<OSBootInfo*>(OSPhysicalToCached(OS_PHYS_BOOT_INFO))->diskID;
}

} // extern "C"
