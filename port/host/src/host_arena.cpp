/**
 * M0 arena: the emulated MEM1/MEM2 block and a small allocator over it
 * (port/host/include/port/arena.h, port/docs/POINTERS.md §2).
 *
 * The allocator is a first-fit free list kept in address order, with
 * coalescing on free. Each allocation has a 16-byte header just before the
 * returned pointer that records where the carved block starts and how large
 * it is, so any alignment can be honoured. It is single-threaded, like the
 * rest of the host stand-ins; the Phase 7 OS layer replaces it with the
 * decompiled MEM/EGG heaps carved from the same block.
 */

#include <port/arena.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

u8* port_arena_base = NULL;

namespace {

const size_t kHeader = 16;
const size_t kGrain = 16;
const size_t kMinFree = 32;

struct FreeBlock {
    size_t size;
    FreeBlock* next;
};

struct AllocHeader {
    u8* start;   // start of the carved block (<= header)
    size_t size; // size of the carved block
};

static_assert(sizeof(AllocHeader) <= kHeader, "allocation header too large");

struct Region {
    u8* lo;
    u8* hi;
    FreeBlock* free; // address-ordered
};

Region sRegions[2]; // [0] MEM2, [1] MEM1 (allocation order)
size_t sUsed = 0;

void Fail(const char* what) {
    std::fprintf(stderr, "port arena: %s\n", what);
    std::abort();
}

void InitRegion(Region* r, u8* lo, u8* hi) {
    r->lo = lo;
    r->hi = hi;
    r->free = reinterpret_cast<FreeBlock*>(lo);
    r->free->size = static_cast<size_t>(hi - lo);
    r->free->next = NULL;
}

void* AllocFrom(Region* r, size_t size, size_t align) {
    size_t need = (size + kGrain - 1) & ~(kGrain - 1);
    if (need == 0) {
        need = kGrain;
    }

    FreeBlock** link = &r->free;
    for (FreeBlock* b = r->free; b != NULL; link = &b->next, b = b->next) {
        u8* start = reinterpret_cast<u8*>(b);
        u8* end = start + b->size;
        uintptr_t user = reinterpret_cast<uintptr_t>(start) + kHeader;
        user = (user + align - 1) & ~static_cast<uintptr_t>(align - 1);
        u8* userEnd = reinterpret_cast<u8*>(user) + need;
        if (userEnd > end) {
            continue;
        }

        FreeBlock* next = b->next;
        size_t rest = static_cast<size_t>(end - userEnd);
        if (rest >= kMinFree) {
            FreeBlock* tail = reinterpret_cast<FreeBlock*>(userEnd);
            tail->size = rest;
            tail->next = next;
            *link = tail;
        } else {
            userEnd = end;
            *link = next;
        }

        AllocHeader* h = reinterpret_cast<AllocHeader*>(user - kHeader);
        h->start = start;
        h->size = static_cast<size_t>(userEnd - start);
        sUsed += h->size;
        return reinterpret_cast<void*>(user);
    }
    return NULL;
}

void FreeTo(Region* r, u8* start, size_t size) {
    FreeBlock* prev = NULL;
    FreeBlock* cur = r->free;
    while (cur != NULL && reinterpret_cast<u8*>(cur) < start) {
        prev = cur;
        cur = cur->next;
    }

    FreeBlock* b = reinterpret_cast<FreeBlock*>(start);
    b->size = size;
    b->next = cur;

    if (cur != NULL && start + size == reinterpret_cast<u8*>(cur)) {
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev != NULL && reinterpret_cast<u8*>(prev) + prev->size == start) {
        prev->size += b->size;
        prev->next = b->next;
    } else if (prev != NULL) {
        prev->next = b;
    } else {
        r->free = b;
    }
}

} // namespace

extern "C" {

void PortArenaInit(void) {
    if (port_arena_base != NULL) {
        return;
    }

    void* p = mmap(NULL, PORT_ARENA_SIZE, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        Fail("cannot reserve the MEM1/MEM2 block");
    }
    u8* base = static_cast<u8*>(p);
    if (mprotect(base + PORT_MEM1_OFFSET, PORT_MEM1_SIZE,
                 PROT_READ | PROT_WRITE) != 0 ||
        mprotect(base + PORT_MEM2_OFFSET, PORT_MEM2_SIZE,
                 PROT_READ | PROT_WRITE) != 0) {
        Fail("cannot commit MEM1/MEM2");
    }

    port_arena_base = base;
    InitRegion(&sRegions[0], base + PORT_MEM2_OFFSET,
               base + PORT_MEM2_OFFSET + PORT_MEM2_SIZE);
    InitRegion(&sRegions[1], base + PORT_MEM1_OFFSET + PORT_MEM1_RESERVED,
               base + PORT_MEM1_OFFSET + PORT_MEM1_SIZE);
}

void* PortArenaAlloc(size_t size, int align) {
    PortArenaInit();

    size_t a = static_cast<size_t>(align < 0 ? -align : align);
    if (a < kGrain) {
        a = kGrain;
    }
    if ((a & (a - 1)) != 0) {
        Fail("alignment is not a power of two");
    }

    for (Region& r : sRegions) {
        if (void* p = AllocFrom(&r, size, a)) {
            return p;
        }
    }
    return NULL;
}

void PortArenaFree(void* p) {
    if (p == NULL) {
        return;
    }
    if (!port_in_arena(p)) {
        Fail("free of a pointer outside the arena");
    }

    AllocHeader* h =
        reinterpret_cast<AllocHeader*>(static_cast<u8*>(p) - kHeader);
    u8* start = h->start;
    size_t size = h->size;
    sUsed -= size;

    for (Region& r : sRegions) {
        if (start >= r.lo && start < r.hi) {
            FreeTo(&r, start, size);
            return;
        }
    }
    Fail("free of a corrupt block");
}

int PortArenaReserve(void* lo, void* hi) {
    PortArenaInit();
    u8* l = static_cast<u8*>(lo);
    u8* h = static_cast<u8*>(hi);
    if (l >= h) {
        return 0;
    }
    for (Region& r : sRegions) {
        if (l < r.lo || h > r.hi) {
            continue;
        }
        FreeBlock** link = &r.free;
        for (FreeBlock* b = r.free; b != NULL; link = &b->next, b = b->next) {
            u8* start = reinterpret_cast<u8*>(b);
            u8* end = start + b->size;
            if (l < start || h > end) {
                continue;
            }
            // Keep the pieces before and after the range as free blocks
            // (a piece too small for a block header is dropped).
            FreeBlock* next = b->next;
            if (static_cast<size_t>(end - h) >= kMinFree) {
                FreeBlock* tail = reinterpret_cast<FreeBlock*>(h);
                tail->size = static_cast<size_t>(end - h);
                tail->next = next;
                next = tail;
            }
            if (static_cast<size_t>(l - start) >= kMinFree) {
                b->size = static_cast<size_t>(l - start);
                b->next = next;
            } else {
                *link = next;
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

size_t PortArenaUsed(void) {
    return sUsed;
}

void* PortArenaMEM1Lo(void) {
    PortArenaInit();
    return port_arena_base + PORT_MEM1_OFFSET + PORT_MEM1_RESERVED;
}
void* PortArenaMEM1Hi(void) {
    PortArenaInit();
    return port_arena_base + PORT_MEM1_OFFSET + PORT_MEM1_SIZE;
}
void* PortArenaMEM2Lo(void) {
    PortArenaInit();
    return port_arena_base + PORT_MEM2_OFFSET;
}
void* PortArenaMEM2Hi(void) {
    PortArenaInit();
    return port_arena_base + PORT_MEM2_OFFSET + PORT_MEM2_SIZE;
}

void PortArenaBadPointer(const void* p) {
    std::fprintf(stderr, "port arena: host pointer %p is not in MEM1/MEM2\n",
                 p);
    std::abort();
}

void PortArenaBadAddress(u32 v) {
    std::fprintf(stderr, "port arena: address 0x%08X is not in MEM1/MEM2\n",
                 static_cast<unsigned>(v));
    std::abort();
}

} // extern "C"
