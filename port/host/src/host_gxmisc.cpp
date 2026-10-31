/**
 * Host GXMisc (replaces src/revolution/GX/GXMisc.c; port/docs/HOST_BUILD.md §26).
 *
 * The register writes are the SDK's (BP 0x45 draw done, 0x47/0x48 tokens,
 * GXFlush's 32 bytes of padding); the waiting is not: the host has no PE
 * interrupts, so each sync point hands the stream to the sink and lets it
 * finish (src/host_wgpipe.cpp), then sets the PE token register and calls
 * the game's callback before returning, as the interrupt handlers would.
 */
#include <types.h>

#include <cstring>

#include <revolution/GX.h>
#include <revolution/OS.h>
#include <revolution/VI.h>

#include <port/gxsink.h>
#include <port/wgpipe.h>

extern "C" {

extern volatile void* __peReg;
void __GXInitRevisionBits(void);

}

namespace {

GXDrawSyncCallback sTokenCB = nullptr;
GXDrawDoneCallback sDrawDoneCB = nullptr;

void WritePE16(u32 index, u16 value) {
    if (__peReg != nullptr) {
        reinterpret_cast<volatile u16*>(__peReg)[index] = value;
    }
}

} // namespace

extern "C" {

void GXSetMisc(UNKWORD token, UNKWORD val) {
    switch (token) {
    case 0:
        break;
    case 1:
        gxdt->SHORT_0x4 = val;
        gxdt->SHORT_0x0 = !gxdt->SHORT_0x4;
        gxdt->lastWriteWasXF = TRUE;
        if (gxdt->SHORT_0x4 > 0) {
            gxdt->gxDirtyFlags |= 8;
        }
        break;
    case 2:
        gxdt->dlistSave = (val != 0);
        break;
    case 3:
        gxdt->BYTE_0x5FA = (val != 0);
        break;
    }
}

void GXFlush(void) {
    if (gxdt->gxDirtyFlags) {
        __GXSetDirtyState();
    }
    for (int n = 0; n < 8; n++) {
        WGPIPE_WRITE(i, 0); // WGPIPE.i = 0
    }
    PortWGPipeSync(PORT_GX_SYNC_FLUSH);
}

void GXResetWriteGatherPipe(void) {}

void __GXAbort(void) {
    PortWGPipeSync(PORT_GX_SYNC_DRAWDONE);
}

void GXAbortFrame(void) {
    __GXAbort();
    __GXInitRevisionBits();
    gxdt->gxDirtyFlags = 0;
    GXFlush();
}

void GXSetDrawSync(u16 token) {
    u32 reg = token | 0x48 << 24;
    GX_BP_LOAD_REG(reg);
    reg = GX_BITSET(reg, 16, 16, token);
    reg = GX_BITSET(reg, 0, 8, 0x47);
    GX_BP_LOAD_REG(reg);
    GXFlush();
    gxdt->lastWriteWasXF = FALSE;

    // GXTokenInterruptHandler, once the GP has reached the token
    PortWGPipeSync(PORT_GX_SYNC_DRAWDONE);
    WritePE16(7, token);
    if (sTokenCB != nullptr) {
        sTokenCB(token);
    }
}

void GXDrawDone(void) {
    u32 reg = 2 | 0x45 << 24;
    GX_BP_LOAD_REG(reg);
    GXFlush();

    // GXFinishInterruptHandler
    PortWGPipeSync(PORT_GX_SYNC_DRAWDONE);
    if (sDrawDoneCB != nullptr) {
        sDrawDoneCB();
    }
}

void GXPixModeSync(void) {
    GX_BP_LOAD_REG(gxdt->zControl);
    gxdt->lastWriteWasXF = FALSE;
    // The GP pipeline flush (used after EFB copies): the host also runs
    // everything before it now. On the Wii the GP keeps pace with the CPU, so
    // memory that earlier commands read (textures, vertex arrays) is read long
    // before the CPU reuses it; the host's GP runs only at sync points.
    // RVLFaceLib depends on it: RFLiMakeTexture draws each Mii's face parts
    // from buffers it frees and refills for the next Mii, with only a
    // GXCopyTex and this call in between.
    PortWGPipeSync(PORT_GX_SYNC_DRAWDONE);
}

// CPU access to the EFB (peek/poke) is not emulated: the poke modes only
// matter for the GXPoke/GXPeek accessors themselves.
void GXPokeAlphaMode(GXCompare, u8) {}
void GXPokeAlphaRead(u32) {}
void GXPokeAlphaUpdate(GXBool) {}
void GXPokeBlendMode(GXBlendMode, GXBlendFactor, GXBlendFactor, GXLogicOp) {}
void GXPokeColorUpdate(GXBool) {}
void GXPokeDstAlpha(GXBool, u8) {}
void GXPokeDither(GXBool) {}
void GXPokeZMode(GXBool, GXCompare, GXBool) {}

GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback callback) {
    GXDrawSyncCallback old = sTokenCB;
    sTokenCB = callback;
    return old;
}

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback callback) {
    GXDrawDoneCallback old = sDrawDoneCB;
    sDrawDoneCB = callback;
    return old;
}

void __GXPEInit(void) {}

} // extern "C"

// --- SDK data and platform calls GXInit needs --------------------------------

extern "C" {

// The SDK's render modes (GXFrameBuf data, not decompiled yet), as in the RVL
// SDK: 640x480 (PAL: 640x528) interlaced, double-strike off, no AA, sample
// pattern centred, the default vertical filter. GXInit uses the NTSC one on
// the host (VIGetTvFormat).
#define PORT_RENDER_MODE(name, mode, efbH, xfbH, viY, viH)                         \
    GXRenderModeObj name = {(mode), 640, (efbH), (xfbH), 40, (viY), 640, (viH), VI_XFBMODE_DF, \
                            GX_FALSE, GX_FALSE,                                    \
                            {{6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6},       \
                             {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}, {6, 6}},      \
                            {8, 8, 10, 12, 10, 8, 8}};
PORT_RENDER_MODE(GXNtsc480IntDf, VI_TVMODE_NTSC_INT, 480, 480, 0, 480)
PORT_RENDER_MODE(GXMpal480IntDf, VI_TVMODE_MPAL_INT, 480, 480, 0, 480)
PORT_RENDER_MODE(GXEurgb60Hz480IntDf, VI_TVMODE_EURGB60_INT, 480, 480, 0, 480)
PORT_RENDER_MODE(GXPal528IntDf, VI_TVMODE_PAL_INT, 528, 528, 23, 528)

// Weak: a harness with the platform layer (§22) or the sound host (§21) has its own.
__attribute__((weak)) BOOL OSDisableInterrupts(void) {
    return TRUE;
}
__attribute__((weak)) BOOL OSRestoreInterrupts(BOOL status) {
    return status;
}
__attribute__((weak)) s64 OSGetTime(void) {
    return 0;
}
__attribute__((weak)) void OSRegisterVersion(const char*) {}
__attribute__((weak)) void OSRegisterShutdownFunction(OSShutdownFunctionInfo*) {}
__attribute__((weak)) VITVFormat VIGetTvFormat(void) {
    return VI_TVFORMAT_NTSC;
}

// The write-gather pipe's HID2/WPAR set-up and sync: nothing to do on the host.
__attribute__((weak)) u32 PPCMfhid2(void) {
    return 0;
}
__attribute__((weak)) void PPCMthid2(u32) {}
__attribute__((weak)) void PPCMtwpar(u32) {}
__attribute__((weak)) void PPCSync(void) {}

namespace {
const void* GPCopyBE32(const void* src, u32 bytes) {
    u8* dst = static_cast<u8*>(PortGPScratch(bytes));
    const u8* p = static_cast<const u8*>(src);
    for (u32 i = 0; i < bytes; i += 4) {
        u32 v;
        std::memcpy(&v, p + i, 4);
        dst[i] = static_cast<u8>(v >> 24);
        dst[i + 1] = static_cast<u8>(v >> 16);
        dst[i + 2] = static_cast<u8>(v >> 8);
        dst[i + 3] = static_cast<u8>(v);
    }
    return dst;
}
} // namespace

void PortGXSetViewMtxArrays(u32 num, const void* pos, const void* nrm, const void* env) {
    if (pos != NULL) {
        GXSetArray(GX_POS_MTX_ARRAY, GPCopyBE32(pos, num * 48), 48);
    }
    if (nrm != NULL) {
        GXSetArray(GX_NRM_MTX_ARRAY, GPCopyBE32(nrm, num * 36), 36);
    }
    if (env != NULL) {
        GXSetArray(GX_TEX_MTX_ARRAY, GPCopyBE32(env, num * 48), 48);
    }
}

void* PortGPLightObjs(const void* objs, u32 n) {
    // XF light memory: 3 unused words, the colour (GXColor bytes: already in order), then
    // 12 floats (attenuation, position, direction).
    u8* dst = static_cast<u8*>(PortGPScratch(n * 64));
    std::memcpy(dst, objs, n * 64);
    for (u32 i = 0; i < n; i++) {
        const u8* src = static_cast<const u8*>(objs) + i * 64 + 16;
        std::memcpy(dst + i * 64 + 16, GPCopyBE32(src, 48), 48);
    }
    return dst;
}

// The locked cache's DMA queue (g3d's DrawResMdlDirectly waits on it before drawing). The
// game runs g3d with the locked cache off (G3dInit(false)), so nothing is ever queued.
void LCQueueWait(u32 len) {
    (void)len;
}

} // extern "C"
