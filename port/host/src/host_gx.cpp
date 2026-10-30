/**
 * Host stand-ins for the GX, MTX and MEM allocator calls that nw4r::lyt and the
 * nw4r::g3d resource code link (GX texture/TLUT objects: host_gxtexobj.cpp)
 * (port/docs/HOST_BUILD.md §1).
 *
 * The layout harness builds layouts and binds animations without drawing, so
 * every GX call here is a no-op; the Phase 7 GX layer replaces this file. The
 * matrix functions are plain C++ versions of the paired-single assembly in
 * src/revolution/MTX/mtx.c. MEMAllocFromAllocator / MEMFreeToAllocator are
 * the two dispatch functions of src/revolution/MEM/mem_allocator.c (the rest
 * of that file needs the MEM heaps); they are weak, so a harness that links
 * mem_allocator.c itself (mdl_harness, docs/HOST_BUILD.md §20) gets the real ones.
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/MEM.h>
#include <revolution/MTX.h>

#include <cmath>
#include <cstring>

extern "C" {

/* MEM */
__attribute__((weak)) void* MEMAllocFromAllocator(MEMAllocator* allocator, u32 size) {
    return allocator->funcs->allocFunc(allocator, size);
}

__attribute__((weak)) void MEMFreeToAllocator(MEMAllocator* allocator, void* block) {
    allocator->funcs->freeFunc(allocator, block);
}

/* MTX (PSMTXIdentity, PSMTXCopy and PSMTXConcat come from src/revolution/MTX,
 * built with their host forms: docs/HOST_BUILD.md §18) */


void PSMTXScale(Mtx m, f32 x, f32 y, f32 z) {
    std::memset(m, 0, sizeof(Mtx));
    m[0][0] = x;
    m[1][1] = y;
    m[2][2] = z;
}

void PSMTXTransApply(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
    if (src != dst) {
        std::memcpy(dst, src, sizeof(Mtx));
    }
    dst[0][3] += x;
    dst[1][3] += y;
    dst[2][3] += z;
}

void PSMTXRotRad(Mtx m, char axis, f32 rad) {
    f32 s = std::sin(rad);
    f32 c = std::cos(rad);
    std::memset(m, 0, sizeof(Mtx));
    switch (axis) {
    case 'x':
    case 'X':
        m[0][0] = 1.0f;
        m[1][1] = c;
        m[1][2] = -s;
        m[2][1] = s;
        m[2][2] = c;
        break;
    case 'y':
    case 'Y':
        m[0][0] = c;
        m[0][2] = s;
        m[1][1] = 1.0f;
        m[2][0] = -s;
        m[2][2] = c;
        break;
    default:
        m[0][0] = c;
        m[0][1] = -s;
        m[1][0] = s;
        m[1][1] = c;
        m[2][2] = 1.0f;
        break;
    }
}

/* GX: no-ops for the harnesses that do not draw. Weak: the GX harnesses link
 * the real SDK (src/revolution/GX, docs/HOST_BUILD.md §26). */
__attribute__((weak)) void GXBegin(GXPrimitive, GXVtxFmt, u16) {}
/* ScnRoot::CalcView invalidates the GPU's vertex cache before the view
 * matrices are computed: nothing to invalidate on the host. */
__attribute__((weak)) void GXInvalidateVtxCache(void) {}
__attribute__((weak)) void GXClearVtxDesc(void) {}
__attribute__((weak)) void GXLoadPosMtxImm(const Mtx, u32) {}
__attribute__((weak)) void GXLoadTexMtxImm(const Mtx, u32, GXMtxType) {}
__attribute__((weak)) void GXLoadTexObj(const GXTexObj*, GXTexMapID) {}
__attribute__((weak)) void GXLoadTlut(const GXTlutObj*, u32) {}
__attribute__((weak)) void GXSetAlphaCompare(GXCompare, u8, GXAlphaOp, GXCompare, u8) {}
__attribute__((weak)) void GXSetBlendMode(GXBlendMode, GXBlendFactor, GXBlendFactor, GXLogicOp) {}
__attribute__((weak)) void GXSetChanCtrl(GXChannelID, GXBool, GXColorSrc, GXColorSrc, GXLightID, GXDiffuseFn,
                   GXAttnFn) {}
__attribute__((weak)) void GXSetChanMatColor(GXChannelID, GXColor) {}
__attribute__((weak)) void GXSetCurrentMtx(u32) {}
__attribute__((weak)) void GXSetFog(GXFogType, f32, f32, f32, f32, GXColor) {}
__attribute__((weak)) void GXSetIndTexCoordScale(GXIndTexStageID, GXIndTexScale, GXIndTexScale) {}
__attribute__((weak)) void GXSetIndTexMtx(GXIndTexMtxID, const f32[2][3], s8) {}
__attribute__((weak)) void GXSetIndTexOrder(GXIndTexStageID, GXTexCoordID, GXTexMapID) {}
__attribute__((weak)) void GXSetNumChans(u8) {}
__attribute__((weak)) void GXSetNumIndStages(u8) {}
__attribute__((weak)) void GXSetNumTevStages(u8) {}
__attribute__((weak)) void GXSetNumTexGens(u8) {}
__attribute__((weak)) void GXSetTevAlphaIn(GXTevStageID, GXTevAlphaArg, GXTevAlphaArg, GXTevAlphaArg,
                     GXTevAlphaArg) {}
__attribute__((weak)) void GXSetTevAlphaOp(GXTevStageID, GXTevOp, GXTevBias, GXTevScale, GXBool, GXTevRegID) {}
__attribute__((weak)) void GXSetTevColor(GXTevRegID, GXColor) {}
__attribute__((weak)) void GXSetTevColorIn(GXTevStageID, GXTevColorArg, GXTevColorArg, GXTevColorArg,
                     GXTevColorArg) {}
__attribute__((weak)) void GXSetTevColorOp(GXTevStageID, GXTevOp, GXTevBias, GXTevScale, GXBool, GXTevRegID) {}
__attribute__((weak)) void GXSetTevColorS10(GXTevRegID, GXColorS10) {}
__attribute__((weak)) void GXSetTevDirect(GXTevStageID) {}
__attribute__((weak)) void GXSetTevIndirect(GXTevStageID, GXIndTexStageID, GXIndTexFormat, GXIndTexBiasSel,
                      GXIndTexMtxID, GXIndTexWrap, GXIndTexWrap, GXBool, GXBool,
                      GXIndTexAlphaSel) {}
__attribute__((weak)) void GXSetTevKAlphaSel(GXTevStageID, GXTevKAlphaSel) {}
__attribute__((weak)) void GXSetTevKColor(GXTevKColorID, GXColor) {}
__attribute__((weak)) void GXSetTevKColorSel(GXTevStageID, GXTevKColorSel) {}
__attribute__((weak)) void GXSetTevOp(GXTevStageID, GXTevMode) {}
__attribute__((weak)) void GXSetTevOrder(GXTevStageID, GXTexCoordID, GXTexMapID, GXChannelID) {}
__attribute__((weak)) void GXSetTevSwapMode(GXTevStageID, GXTevSwapSel, GXTevSwapSel) {}
__attribute__((weak)) void GXSetTevSwapModeTable(GXTevSwapSel, GXTevColorChan, GXTevColorChan, GXTevColorChan,
                           GXTevColorChan) {}
__attribute__((weak)) void GXSetTexCoordGen2(GXTexCoordID, GXTexGenType, GXTexGenSrc, u32, GXBool, u32) {}
__attribute__((weak)) void GXSetVtxAttrFmt(GXVtxFmt, GXAttr, GXCompCnt, GXCompType, u8) {}
__attribute__((weak)) void GXSetVtxDesc(GXAttr, GXAttrType) {}
__attribute__((weak)) void GXSetZTexture(GXZTexOp, GXTexFmt, u32) {}
/* linked by nw4r::g3d resource code (drawing and set-up paths) */
__attribute__((weak)) void GXCallDisplayList(void*, u32) {}
__attribute__((weak)) void GXSetArray(GXAttr, const void*, u8) {}


} // extern "C"

