/**
 * Host GXFifo (replaces src/revolution/GX/GXFifo.c; port/docs/HOST_BUILD.md §26).
 *
 * The Wii's FIFO code programs the PI/CP FIFO registers and handles the
 * overflow/underflow interrupts. On the host the CPU FIFO is either the
 * command stream (when it is the GP FIFO, as after GXInit) or a buffer in Wii
 * memory (any other FIFO: GXBeginDisplayList's), and the GP never waits:
 * src/host_wgpipe.cpp does the writing. The FIFO objects keep the SDK's
 * GXFifoObjImpl fields (runtime objects, native pointers).
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/GX/GXInternal.h>
#include <revolution/OS.h>

#include <port/gxsink.h>

#include <cstring>

namespace {

GXFifoObjImpl sCPUFifo;
GXFifoObjImpl sGPFifo;
bool sCPUFifoSet = false;
bool sGPFifoSet = false;
void* sBreakPtCB = nullptr;

GXFifoObjImpl* Impl(GXFifoObj* fifo) {
    return reinterpret_cast<GXFifoObjImpl*>(fifo);
}

const GXFifoObjImpl* Impl(const GXFifoObj* fifo) {
    return reinterpret_cast<const GXFifoObjImpl*>(fifo);
}

// The CPU FIFO is the stream when it is the GP FIFO, a memory buffer otherwise.
void Retarget() {
    if (!sCPUFifoSet) {
        return;
    }
    if (sGPFifoSet && sCPUFifo.base == sGPFifo.base) {
        PortWGPipeSetStream();
    } else {
        PortWGPipeSetMemory(sCPUFifo.base, sCPUFifo.writePtr,
                            static_cast<u8*>(sCPUFifo.base) + sCPUFifo.size);
    }
}

// Brings sCPUFifo's write pointer, count and wrap flag up to date.
void SaveCPUFifo() {
    if (sCPUFifoSet && PortWGPipeIsMemory()) {
        int wrapped = 0;
        u8* wp = static_cast<u8*>(PortWGPipeMemoryFinish(&wrapped));
        sCPUFifo.writePtr = wp;
        sCPUFifo.count = static_cast<u32>(wp - static_cast<u8*>(sCPUFifo.base));
        sCPUFifo.wrap = wrapped ? 1 : 0;
    }
}

} // namespace

extern "C" {

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
    GXFifoObjImpl* f = Impl(fifo);
    f->base = base;
    f->end = static_cast<u8*>(base) + size - 4;
    f->size = size;
    f->hiWatermark = static_cast<u8*>(base) + (size - 16 * 1024);
    f->loWatermark = static_cast<u8*>(base) + ((size / 2) & ~0x1F);
    f->readPtr = base;
    f->writePtr = base;
    f->count = 0;
    f->wrap = 0;
}

void GXSetCPUFifo(GXFifoObj* fifo) {
    SaveCPUFifo();
    std::memcpy(&sCPUFifo, Impl(fifo), sizeof(GXFifoObjImpl));
    sCPUFifoSet = true;
    Retarget();
}

void GXSetGPFifo(const GXFifoObj* fifo) {
    SaveCPUFifo();
    std::memcpy(&sGPFifo, Impl(fifo), sizeof(GXFifoObjImpl));
    sGPFifoSet = true;
    Retarget();
}

BOOL GXGetCPUFifo(GXFifoObj* fifo) {
    if (!sCPUFifoSet) {
        return FALSE;
    }
    SaveCPUFifo();
    std::memcpy(Impl(fifo), &sCPUFifo, sizeof(GXFifoObjImpl));
    return TRUE;
}

BOOL GXGetGPFifo(GXFifoObj* fifo) {
    if (!sGPFifoSet) {
        return FALSE;
    }
    std::memcpy(Impl(fifo), &sGPFifo, sizeof(GXFifoObjImpl));
    return TRUE;
}

void GXGetFifoPtrs(const GXFifoObj* fifo, void** readPtr, void** writePtr) {
    *readPtr = Impl(fifo)->readPtr;
    *writePtr = Impl(fifo)->writePtr;
}

u32 GXGetFifoCount(GXFifoObj* fifo) {
    return Impl(fifo)->count;
}

u8 GXGetFifoWrap(GXFifoObj* fifo) {
    return Impl(fifo)->wrap;
}

// The GP is idle whenever the CPU looks: commands run synchronously.
void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle,
                   GXBool* brkpt) {
    *overhi = GX_FALSE;
    *underlow = GX_TRUE;
    *readIdle = GX_TRUE;
    *cmdIdle = GX_TRUE;
    *brkpt = GX_FALSE;
}

// (GXInit.c declares it with a void* callback; no breakpoints on the host.)
void* GXSetBreakPtCallback(void* cb) {
    void* old = sBreakPtCB;
    sBreakPtCB = cb;
    return old;
}

void GXEnableBreakPt(void*) {}
void GXDisableBreakPt(void) {}

void __GXFifoInit(void) {
    sCPUFifoSet = false;
    sGPFifoSet = false;
}

GXBool __GXIsGPFifoReady(void) {
    return sGPFifoSet ? GX_TRUE : GX_FALSE;
}

void __GXCleanGPFifo(void) {}

void __GXSaveFifo(void) {
    SaveCPUFifo();
}

OSThread* GXGetCurrentGXThread(void) {
    return OSGetCurrentThread();
}

} // extern "C"
