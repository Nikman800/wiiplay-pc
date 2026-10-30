/**
 * Minimal host stand-ins for the SDK and EGG pieces the loader path calls.
 *
 * These are deliberately small and synchronous: they let the real decompiled
 * loader code (EGG::DvdFile, EGG::DvdRipper, EGG::Decomp, EGG::Archive, ARC)
 * run on a PC. They are not the Phase 7 platform layer; that will replace
 * this file.
 *
 *   OS     mutexes and message queues (single-threaded), OSReport/OSPanic;
 *          the mutex, message-queue and current-thread functions are weak:
 *          src/host_thread.cpp (the cooperative threads of the audio
 *          runtime, HOST_BUILD.md §21) replaces them where it is linked
 *   DVD    paths resolve against a host directory (the extracted disc's
 *          files/ folder); entry numbers index a table of opened paths
 *   EGG    Heap::alloc/free and the global/heap operator new/delete
 *          allocate from the M0 arena (host_arena.cpp, port/arena.h);
 *          Heap::findContainHeap returns NULL, Heap::sCurrentHeap is NULL
 *
 * The DVD and EGG heap definitions are weak: a program that links the
 * Phase 7 platform layer (HOST_BUILD.md §22: the decomp's dvdfs.c with
 * src/host_dvd.cpp, and the real eggHeap.cpp / eggExpHeap.cpp / MEM heaps)
 * gets those instead.
 *   VI/DC  no-ops
 */

#include <egg/core.h>
#include <revolution/DVD.h>
#include <revolution/OS.h>
#include <revolution/VI.h>

#include <port/arena.h>
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

__attribute__((weak)) void OSInitMutex(OSMutex* mutex) {
    std::memset(mutex, 0, sizeof(*mutex));
}
__attribute__((weak)) void OSLockMutex(OSMutex* mutex) {
    (void)mutex;
}
__attribute__((weak)) void OSUnlockMutex(OSMutex* mutex) {
    (void)mutex;
}

static OSThread sMainThread;
__attribute__((weak)) OSThread* OSGetCurrentThread(void) {
    return &sMainThread;
}

__attribute__((weak)) void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* buffer,
                        s32 capacity) {
    std::memset(queue, 0, sizeof(*queue));
    queue->buffer = buffer;
    queue->capacity = capacity;
}

__attribute__((weak)) BOOL OSSendMessage(OSMessageQueue* queue, OSMessage mesg, u32 flags) {
    (void)flags;
    if (queue->size >= queue->capacity) {
        return FALSE;
    }
    queue->buffer[(queue->front + queue->size) % queue->capacity] = mesg;
    queue->size++;
    return TRUE;
}

__attribute__((weak)) BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* mesg, u32 flags) {
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
void DCFlushRangeNoSync(const void* buf, u32 len) {
    (void)buf;
    (void)len;
}
void DCStoreRange(const void* buf, u32 len) {
    (void)buf;
    (void)len;
}
void DCStoreRangeNoSync(const void* buf, u32 len) {
    (void)buf;
    (void)len;
}
void VIWaitForRetrace(void) {}

/******************************************************************************
 * DVD
 ******************************************************************************/

__attribute__((weak)) s32 DVDConvertPathToEntrynum(const char* path) {
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

__attribute__((weak)) BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* info) {
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

__attribute__((weak)) BOOL DVDClose(DVDFileInfo* info) {
    if (info->block.userData != NULL) {
        std::fclose(static_cast<FILE*>(info->block.userData));
        info->block.userData = NULL;
    }
    return TRUE;
}

__attribute__((weak)) s32 DVDReadPrio(DVDFileInfo* info, void* dst, s32 size, s32 offset,
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

__attribute__((weak)) BOOL DVDReadAsyncPrio(DVDFileInfo* info, void* dst, s32 size, s32 offset,
                      DVDAsyncCallback callback, s32 prio) {
    s32 result = DVDReadPrio(info, dst, size, offset, prio);
    if (callback != NULL) {
        callback(result, info);
    }
    return TRUE;
}

__attribute__((weak)) s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    (void)block;
    return 0; // DVD_STATE_END
}

} // extern "C"

/******************************************************************************
 * EGG heap
 ******************************************************************************/

namespace {

void* HostAlloc(size_t size, int align) {
    // M0: every heap block and every operator new comes from the emulated
    // MEM1/MEM2 block (port/arena.h), so file buffers have 32-bit Wii
    // addresses. Negative alignment means "allocate from the tail" on the
    // Wii; the host only keeps the alignment.
    void* p = PortArenaAlloc(size, align);
    if (p == NULL) {
        std::fprintf(stderr, "host heap: out of arena memory (%zu bytes)\n",
                     size);
    }
    return p;
}

void HostFree(void* p) {
    PortArenaFree(p);
}

} // namespace

namespace EGG {

__attribute__((weak)) void* Heap::alloc(u32 size, int align, Heap* pHeap) {
    (void)pHeap;
    return HostAlloc(size, align);
}

__attribute__((weak)) void Heap::free(void* pBlock, Heap* pHeap) {
    (void)pHeap;
    HostFree(pBlock);
}

__attribute__((weak)) Heap* Heap::findContainHeap(const void* pBlock) {
    (void)pBlock;
    return NULL;
}

// No heaps on the host yet: Heap::getCurrentHeap() is NULL, and the heap
// operator new below ignores the heap.
__attribute__((weak)) Heap* Heap::sCurrentHeap = NULL;

} // namespace EGG

__attribute__((weak)) void* operator new(size_t size) {
    return HostAlloc(size, 4);
}
__attribute__((weak)) void* operator new(size_t size, EGG::Heap* pHeap, int align) {
    return EGG::Heap::alloc(static_cast<u32>(size), align, pHeap);
}
__attribute__((weak)) void* operator new[](size_t size) {
    return HostAlloc(size, 4);
}
__attribute__((weak)) void* operator new[](size_t size, int align) {
    return HostAlloc(size, align);
}
__attribute__((weak)) void* operator new[](size_t size, EGG::Heap* pHeap, int align) {
    return EGG::Heap::alloc(static_cast<u32>(size), align, pHeap);
}
__attribute__((weak)) void operator delete(void* pBlock) noexcept {
    HostFree(pBlock);
}
__attribute__((weak)) void operator delete[](void* pBlock) noexcept {
    HostFree(pBlock);
}
// Sized deletes forward to the unsized ones, which are the real EGG ones
// (Heap::free) when eggHeap.cpp is linked.
void operator delete(void* pBlock, size_t) noexcept {
    ::operator delete(pBlock);
}
void operator delete[](void* pBlock, size_t) noexcept {
    ::operator delete[](pBlock);
}
