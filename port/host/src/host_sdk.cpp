/**
 * Minimal host stand-ins for the SDK and EGG pieces the loader path calls.
 *
 * These are deliberately small and synchronous: they let the real decompiled
 * loader code (EGG::DvdFile, EGG::DvdRipper, EGG::Decomp, EGG::Archive, ARC)
 * run on a PC. They are not the Phase 7 platform layer; that will replace
 * this file.
 *
 *   OS     mutexes and message queues (single-threaded), OSReport/OSPanic
 *   DVD    paths resolve against a host directory (the extracted disc's
 *          files/ folder); entry numbers index a table of opened paths
 *   EGG    Heap::alloc/free and the global/heap operator new/delete map to
 *          aligned host allocations; Heap::findContainHeap returns NULL
 *   VI/DC  no-ops
 */

#include <egg/core.h>
#include <revolution/DVD.h>
#include <revolution/OS.h>
#include <revolution/VI.h>

#include <port/host.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/******************************************************************************
 * Disc root
 ******************************************************************************/

namespace {

std::string sDiscRoot = ".";
std::vector<std::string> sEntries; // entrynum -> host path

} // namespace

extern "C" void PortHostSetDiscRoot(const char* pRoot) {
    sDiscRoot = pRoot;
}

/******************************************************************************
 * OS
 ******************************************************************************/

extern "C" {

void OSReport(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    std::vfprintf(stderr, msg, args);
    va_end(args);
}

void OSPanic(const char* file, int line, const char* msg, ...) {
    va_list args;
    std::fprintf(stderr, "OSPanic %s:%d: ", file, line);
    va_start(args, msg);
    std::vfprintf(stderr, msg, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::abort();
}

void OSInitMutex(OSMutex* mutex) {
    std::memset(mutex, 0, sizeof(*mutex));
}
void OSLockMutex(OSMutex* mutex) {
    (void)mutex;
}
void OSUnlockMutex(OSMutex* mutex) {
    (void)mutex;
}

static OSThread sMainThread;
OSThread* OSGetCurrentThread(void) {
    return &sMainThread;
}

void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* buffer,
                        s32 capacity) {
    std::memset(queue, 0, sizeof(*queue));
    queue->buffer = buffer;
    queue->capacity = capacity;
}

BOOL OSSendMessage(OSMessageQueue* queue, OSMessage mesg, u32 flags) {
    (void)flags;
    if (queue->size >= queue->capacity) {
        return FALSE;
    }
    queue->buffer[(queue->front + queue->size) % queue->capacity] = mesg;
    queue->size++;
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* mesg, u32 flags) {
    if (queue->size == 0) {
        // Single-threaded host: a blocking receive on an empty queue would
        // never return.
        if (flags & OS_MSG_BLOCKING) {
            OSPanic(__FILE__, __LINE__, "OSReceiveMessage: empty queue");
        }
        return FALSE;
    }
    if (mesg != NULL) {
        *mesg = queue->buffer[queue->front];
    }
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    return TRUE;
}

void DCInvalidateRange(const void* buf, u32 len) {
    (void)buf;
    (void)len;
}
void DCFlushRange(const void* buf, u32 len) {
    (void)buf;
    (void)len;
}
void VIWaitForRetrace(void) {}

/******************************************************************************
 * DVD
 ******************************************************************************/

s32 DVDConvertPathToEntrynum(const char* path) {
    std::string rel = path;
    while (!rel.empty() && rel[0] == '/') {
        rel.erase(0, 1);
    }
    std::string full = sDiscRoot + "/" + rel;

    FILE* f = std::fopen(full.c_str(), "rb");
    if (f == NULL) {
        return -1;
    }
    std::fclose(f);

    for (size_t i = 0; i < sEntries.size(); i++) {
        if (sEntries[i] == full) {
            return static_cast<s32>(i);
        }
    }
    sEntries.push_back(full);
    return static_cast<s32>(sEntries.size() - 1);
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* info) {
    if (entrynum < 0 || static_cast<size_t>(entrynum) >= sEntries.size()) {
        return FALSE;
    }
    FILE* f = std::fopen(sEntries[entrynum].c_str(), "rb");
    if (f == NULL) {
        return FALSE;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);

    std::memset(info, 0, sizeof(*info));
    info->size = static_cast<u32>(size);
    info->block.userData = f; // host handle lives in the runtime struct
    info->block.state = 0;
    return TRUE;
}

BOOL DVDOpen(const char* path, DVDFileInfo* info) {
    return DVDFastOpen(DVDConvertPathToEntrynum(path), info);
}

BOOL DVDClose(DVDFileInfo* info) {
    if (info->block.userData != NULL) {
        std::fclose(static_cast<FILE*>(info->block.userData));
        info->block.userData = NULL;
    }
    return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* info, void* dst, s32 size, s32 offset,
                s32 prio) {
    (void)prio;
    FILE* f = static_cast<FILE*>(info->block.userData);
    if (f == NULL || size < 0 || offset < 0) {
        return DVD_RESULT_FATAL_ERROR;
    }
    // Reads may run past the end of the file (callers round sizes up to 32
    // bytes); the disc returns whatever follows, the host returns zeros.
    std::memset(dst, 0, size);
    if (std::fseek(f, offset, SEEK_SET) != 0) {
        return DVD_RESULT_FATAL_ERROR;
    }
    size_t got = std::fread(dst, 1, size, f);
    (void)got;
    return size;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* info, void* dst, s32 size, s32 offset,
                      DVDAsyncCallback callback, s32 prio) {
    s32 result = DVDReadPrio(info, dst, size, offset, prio);
    if (callback != NULL) {
        callback(result, info);
    }
    return TRUE;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    (void)block;
    return 0; // DVD_STATE_END
}

} // extern "C"

/******************************************************************************
 * EGG heap
 ******************************************************************************/

namespace {

void* HostAlloc(size_t size, int align) {
    // Negative alignment means "allocate from the tail" on the Wii; the host
    // only keeps the alignment.
    size_t a = static_cast<size_t>(align < 0 ? -align : align);
    if (a < sizeof(void*)) {
        a = sizeof(void*);
    }
    void* p = NULL;
    if (posix_memalign(&p, a, size == 0 ? 1 : size) != 0) {
        return NULL;
    }
    return p;
}

} // namespace

namespace EGG {

void* Heap::alloc(u32 size, int align, Heap* pHeap) {
    (void)pHeap;
    return HostAlloc(size, align);
}

void Heap::free(void* pBlock, Heap* pHeap) {
    (void)pHeap;
    std::free(pBlock);
}

Heap* Heap::findContainHeap(const void* pBlock) {
    (void)pBlock;
    return NULL;
}

} // namespace EGG

void* operator new(size_t size) {
    return HostAlloc(size, 4);
}
void* operator new(size_t size, EGG::Heap* pHeap, int align) {
    return EGG::Heap::alloc(static_cast<u32>(size), align, pHeap);
}
void* operator new[](size_t size) {
    return HostAlloc(size, 4);
}
void* operator new[](size_t size, int align) {
    return HostAlloc(size, align);
}
void* operator new[](size_t size, EGG::Heap* pHeap, int align) {
    return EGG::Heap::alloc(static_cast<u32>(size), align, pHeap);
}
void operator delete(void* pBlock) noexcept {
    std::free(pBlock);
}
void operator delete[](void* pBlock) noexcept {
    std::free(pBlock);
}
void operator delete(void* pBlock, size_t) noexcept {
    std::free(pBlock);
}
void operator delete[](void* pBlock, size_t) noexcept {
    std::free(pBlock);
}
