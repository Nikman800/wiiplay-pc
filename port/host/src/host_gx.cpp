/**
 * Host stand-ins for the GX, MTX and MEM allocator calls that nw4r::lyt links
 * (port/docs/HOST_BUILD.md §1).
 *
 * The layout harness builds layouts and binds animations without drawing, so
 * every GX call here is a no-op; the Phase 7 GX layer replaces this file. The
 * matrix functions are plain C++ versions of the paired-single assembly in
 * src/revolution/MTX/mtx.c. MEMAllocFromAllocator / MEMFreeToAllocator are
 * the two dispatch functions of src/revolution/MEM/mem_allocator.c (the rest
 * of that file needs the MEM heaps).
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/MEM.h>
#include <revolution/MTX.h>

#include <cmath>
#include <cstring>

extern "C" {

/* MEM */
void* MEMAllocFromAllocator(MEMAllocator* allocator, u32 size) {
    return allocator->funcs->allocFunc(allocator, size);
}

void MEMFreeToAllocator(MEMAllocator* allocator, void* block) {
    allocator->funcs->freeFunc(allocator, block);
}

/* MTX */
void PSMTXCopy(const Mtx src, Mtx dst) {
    if (src != dst) {
        std::memmove(dst, src, sizeof(Mtx));
    }
}

void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    Mtx t;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] +
                      (j == 3 ? a[i][3] : 0.0f);
        }
    }
    std::memcpy(ab, t, sizeof(Mtx));
}

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

/* GX: no-ops until the Phase 7 GX layer */
void GXBegin(GXPrimitive, GXVtxFmt, u16) {}
void GXClearVtxDesc(void) {}
u32 GXGetTexObjTlut(GXTexObj*) {
    return 0;
}
void GXInitTexObj(GXTexObj*, void*, u16, u16, GXTexFmt, GXTexWrapMode, GXTexWrapMode,
                  GXBool) {}
void GXInitTexObjCI(GXTexObj*, void*, u16, u16, GXTexFmt, GXTexWrapMode, GXTexWrapMode,
                    GXBool, u32) {}
void GXInitTexObjLOD(GXTexObj*, GXTexFilter, GXTexFilter, f32, f32, f32, GXBool, GXBool,
                     GXAnisotropy) {}
void GXInitTexObjTlut(GXTexObj*, u32) {}
void GXInitTlutObj(GXTlutObj*, void*, GXTlutFmt, u16) {}
void GXLoadPosMtxImm(const Mtx, u32) {}
void GXLoadTexMtxImm(const Mtx, u32, GXMtxType) {}
void GXLoadTexObj(const GXTexObj*, GXTexMapID) {}
void GXLoadTlut(const GXTlutObj*, u32) {}
void GXSetAlphaCompare(GXCompare, u8, GXAlphaOp, GXCompare, u8) {}
void GXSetBlendMode(GXBlendMode, GXBlendFactor, GXBlendFactor, GXLogicOp) {}
void GXSetChanCtrl(GXChannelID, GXBool, GXColorSrc, GXColorSrc, GXLightID, GXDiffuseFn,
                   GXAttnFn) {}
void GXSetChanMatColor(GXChannelID, GXColor) {}
void GXSetCurrentMtx(u32) {}
void GXSetFog(GXFogType, f32, f32, f32, f32, GXColor) {}
void GXSetIndTexCoordScale(GXIndTexStageID, GXIndTexScale, GXIndTexScale) {}
void GXSetIndTexMtx(GXIndTexMtxID, const f32[2][3], s8) {}
void GXSetIndTexOrder(GXIndTexStageID, GXTexCoordID, GXTexMapID) {}
void GXSetNumChans(u8) {}
void GXSetNumIndStages(u8) {}
void GXSetNumTevStages(u8) {}
void GXSetNumTexGens(u8) {}
void GXSetTevAlphaIn(GXTevStageID, GXTevAlphaArg, GXTevAlphaArg, GXTevAlphaArg,
                     GXTevAlphaArg) {}
void GXSetTevAlphaOp(GXTevStageID, GXTevOp, GXTevBias, GXTevScale, GXBool, GXTevRegID) {}
void GXSetTevColor(GXTevRegID, GXColor) {}
void GXSetTevColorIn(GXTevStageID, GXTevColorArg, GXTevColorArg, GXTevColorArg,
                     GXTevColorArg) {}
void GXSetTevColorOp(GXTevStageID, GXTevOp, GXTevBias, GXTevScale, GXBool, GXTevRegID) {}
void GXSetTevColorS10(GXTevRegID, GXColorS10) {}
void GXSetTevDirect(GXTevStageID) {}
void GXSetTevIndirect(GXTevStageID, GXIndTexStageID, GXIndTexFormat, GXIndTexBiasSel,
                      GXIndTexMtxID, GXIndTexWrap, GXIndTexWrap, GXBool, GXBool,
                      GXIndTexAlphaSel) {}
void GXSetTevKAlphaSel(GXTevStageID, GXTevKAlphaSel) {}
void GXSetTevKColor(GXTevKColorID, GXColor) {}
void GXSetTevKColorSel(GXTevStageID, GXTevKColorSel) {}
void GXSetTevOp(GXTevStageID, GXTevMode) {}
void GXSetTevOrder(GXTevStageID, GXTexCoordID, GXTexMapID, GXChannelID) {}
void GXSetTevSwapMode(GXTevStageID, GXTevSwapSel, GXTevSwapSel) {}
void GXSetTevSwapModeTable(GXTevSwapSel, GXTevColorChan, GXTevColorChan, GXTevColorChan,
                           GXTevColorChan) {}
void GXSetTexCoordGen2(GXTexCoordID, GXTexGenType, GXTexGenSrc, u32, GXBool, u32) {}
void GXSetVtxAttrFmt(GXVtxFmt, GXAttr, GXCompCnt, GXCompType, u8) {}
void GXSetVtxDesc(GXAttr, GXAttrType) {}
void GXSetZTexture(GXZTexOp, GXTexFmt, u32) {}

} // extern "C"
