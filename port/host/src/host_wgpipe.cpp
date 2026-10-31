/**
 * The host write-gather pipe (port/wgpipe.h, port/docs/HOST_BUILD.md §26).
 *
 * The decomp's GX SDK writes every command through WGPIPE_WRITE. The bytes go
 * to the current CPU FIFO, chosen by the host GXSetCPUFifo/GXSetGPFifo
 * (src/host_gxfifo.cpp):
 *
 *   stream  the CPU FIFO is the GP FIFO (GXInit's): the bytes are gathered
 *           here and handed, in order, to the sink (the GX backend); the sink
 *           sees whole commands at every sync point (GXFlush, GXDrawDone,
 *           GXSetDrawSync, GXCopyDisp), because the SDK only syncs between
 *           commands;
 *   memory  any other CPU FIFO (a display list between GXBeginDisplayList
 *           and GXEndDisplayList): the bytes land in that Wii buffer, as the
 *           Wii's gather pipe puts them; a write past its end sets the wrap
 *           flag (GXEndDisplayList then returns 0, as on the Wii).
 *
 * Without a sink (every harness except the GX ones) the stream is counted and
 * dropped.
 */
#include <types.h>

#include <port/arena.h>
#include <port/gxsink.h>
#include <port/wgpipe.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const u32 kGatherSize = 64 * 1024;
u8 sGather[kGatherSize];

const PortGXSink* sSink = nullptr;
u64 sStreamBytes = 0;

bool sMemoryMode = false;
u8* sMemBase = nullptr;
u8* sMemLimit = nullptr;
bool sMemWrapped = false;

// PortGPScratch: a bump allocator over one M0 block, emptied at every drain.
const u32 kScratchSize = 4 * 1024 * 1024;
u8* sScratch = nullptr;
u32 sScratchUsed = 0;

void SinkWrite(const u8* data, u32 size) {
    sStreamBytes += size;
    if (sSink != nullptr && sSink->write != nullptr && size != 0) {
        sSink->write(data, size, sSink->user);
    }
}

// Hands the gathered stream bytes to the sink (no sync).
void SpillGather() {
    if (!sMemoryMode) {
        SinkWrite(sGather, static_cast<u32>(port_wgpipe_cur - sGather));
        port_wgpipe_cur = sGather;
        port_wgpipe_end = sGather + kGatherSize;
    }
}

} // namespace

extern "C" {

// Constant-initialised, so GX writes from static constructors work too.
u8* port_wgpipe_cur = sGather;
u8* port_wgpipe_end = sGather + kGatherSize;

void PortWGPipeOverflow(const u8* bytes, u32 size) {
    if (sMemoryMode) {
        // Past the end of the display list buffer: the Wii's FIFO wraps;
        // the SDK reports it (GXGetFifoWrap) and the list is unusable.
        u32 room = static_cast<u32>(port_wgpipe_end - port_wgpipe_cur);
        std::memcpy(port_wgpipe_cur, bytes, room);
        port_wgpipe_cur += room;
        sMemWrapped = true;
        return;
    }
    SpillGather();
    if (size > kGatherSize) {
        SinkWrite(bytes, size);
        return;
    }
    std::memcpy(port_wgpipe_cur, bytes, size);
    port_wgpipe_cur += size;
}

u32 PortWGPipeAddress(const void* p) {
    return port_vaddr(p);
}

const PortGXSink* PortGXSetSink(const PortGXSink* sink) {
    SpillGather();
    const PortGXSink* old = sSink;
    sSink = sink;
    return old;
}

u64 PortGXStreamBytes(void) {
    return sStreamBytes + (sMemoryMode ? 0 : static_cast<u64>(port_wgpipe_cur - sGather));
}

void PortWGPipeSetStream(void) {
    if (!sMemoryMode) {
        return;
    }
    sMemoryMode = false;
    port_wgpipe_cur = sGather;
    port_wgpipe_end = sGather + kGatherSize;
}

void PortWGPipeSetMemory(void* base, void* writePtr, void* limit) {
    SpillGather();
    sMemoryMode = true;
    sMemBase = static_cast<u8*>(base);
    sMemLimit = static_cast<u8*>(limit);
    sMemWrapped = false;
    port_wgpipe_cur = static_cast<u8*>(writePtr);
    port_wgpipe_end = sMemLimit;
}

int PortWGPipeIsMemory(void) {
    return sMemoryMode ? 1 : 0;
}

void* PortWGPipeMemoryFinish(int* wrapped) {
    // The Wii's gather pipe writes 32-byte bursts: what is still in it when
    // the FIFO changes is padded with zeros (GX_NOP).
    while (((port_wgpipe_cur - sMemBase) & 31) != 0) {
        if (port_wgpipe_cur >= port_wgpipe_end) {
            sMemWrapped = true;
            break;
        }
        *port_wgpipe_cur++ = 0;
    }
    *wrapped = sMemWrapped ? 1 : 0;
    return port_wgpipe_cur;
}

void PortWGPipeSync(u32 kind) {
    if (sMemoryMode) {
        return;
    }
    SpillGather();
    if (sSink != nullptr && sSink->sync != nullptr) {
        sSink->sync(kind, sSink->user);
    }
    if (kind == PORT_GX_SYNC_DRAWDONE || kind == PORT_GX_SYNC_DISPLAY) {
        // Everything sent so far has run: nothing reads the scratch any more.
        sScratchUsed = 0;
    }
}

void PortGPScratchRange(u32* base, u32* size) {
    *base = sScratch != nullptr ? port_vaddr(sScratch) : 0;
    *size = sScratch != nullptr ? kScratchSize : 0;
}

void* PortGPScratch(u32 size) {
    if (sScratch == nullptr) {
        sScratch = static_cast<u8*>(PortArenaAlloc(kScratchSize, 32));
    }
    size = (size + 31) & ~31u;
    if (size > kScratchSize - sScratchUsed) {
        std::fprintf(stderr, "PortGPScratch: %u bytes in use before a GXDrawDone\n", sScratchUsed);
        std::abort();
    }
    void* p = sScratch + sScratchUsed;
    sScratchUsed += size;
    return p;
}

void PortGXDisplayCopied(void* dest, int clear) {
    (void)dest;
    (void)clear;
    PortWGPipeSync(PORT_GX_SYNC_DISPLAY);
}

} // extern "C"
