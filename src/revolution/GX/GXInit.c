#include <revolution/BASE.h>
#include <revolution/GX.h>
#include <revolution/OS.h>
#include <revolution/VI.h>

// Internal SDK functions without a public prototype
void __GXFifoInit(void);
void __GXPEInit(void);
void __GXAbort(void);
void __GXSetTmemConfig(u32 config);
void GXSetBreakPtCallback(void* callback);
void GXSetDispCopyFrame2Field(u32 mode);
void GXClearBoundingBox(void);
void GXPokeColorUpdate(GXBool enable);
void GXPokeAlphaUpdate(GXBool enable);
void GXPokeDither(GXBool enable);
void GXPokeBlendMode(u32 mode, u32 src, u32 dst, u32 op);
void GXPokeAlphaMode(u32 func, u8 threshold);
void GXPokeAlphaRead(u32 mode);
void GXPokeDstAlpha(GXBool enable, u8 alpha);
void GXPokeZMode(GXBool compare, u32 func, GXBool update);
void GXSetGPMetric(u32 perf0, u32 perf1);
void GXClearGPMetric(void);
#if defined(PORT_HOST)
// Implicitly declared in the original; GCC requires prototypes
void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size);
void GXSetGPFifo(const GXFifoObj* fifo);
#endif

static GXFifoObj FifoObj;
static GXData gxData;
GXData* const __GXData = &gxData;

volatile void* __piReg = NULL;
volatile void* __cpReg = NULL;
volatile void* __peReg = NULL;
volatile void* __memReg = NULL;

const char* __GXVersion =
    "<< RVL_SDK - GX \trelease build: Nov 30 2006 03:30:39 (0x4199_60831) >>";

static BOOL __GXShutdown(BOOL final, u32 event);

static GXTexRegion* __GXDefaultTexRegionCallback(const GXTexObj* obj,
                                                 GXTexMapID id);
static GXTlutRegion* __GXDefaultTlutRegionCallback(u32 idx);

#define GX_TMEM_LO 0x00000
#define GX_TMEM_HI 0x80000
#define GX_32K 0x08000
#define GX_8K 0x02000

static u16 DefaultTexData[] ALIGN(32) = {
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

static GXVtxAttrFmtList GXDefaultVATList[] = {
    {GX_VA_POS, GX_POS_XYZ, GX_F32, 0},
    {GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0},
    {GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0},
    {GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0},
    {GX_VA_TEX0, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX1, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX2, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX3, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX4, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX5, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX6, GX_TEX_ST, GX_F32, 0},
    {GX_VA_TEX7, GX_TEX_ST, GX_F32, 0},
    {GX_VA_NULL, (GXCompCnt)0, (GXCompType)0, 0}};

static f32 GXDefaultProjData[] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f, 0.0f};

static u32 GXTexRegionAddrTable[] = {
    GX_TMEM_LO + 0x00000, GX_TMEM_LO + 0x10000, GX_TMEM_LO + 0x20000,
    GX_TMEM_LO + 0x30000, GX_TMEM_LO + 0x40000, GX_TMEM_LO + 0x50000,
    GX_TMEM_LO + 0x60000, GX_TMEM_LO + 0x70000, GX_TMEM_LO + 0x08000,
    GX_TMEM_LO + 0x18000, GX_TMEM_LO + 0x28000, GX_TMEM_LO + 0x38000,
    GX_TMEM_LO + 0x48000, GX_TMEM_LO + 0x58000, GX_TMEM_LO + 0x68000,
    GX_TMEM_LO + 0x78000, GX_TMEM_LO + 0x00000, GX_TMEM_HI + 0x10000,
    GX_TMEM_LO + 0x20000, GX_TMEM_HI + 0x30000, GX_TMEM_LO + 0x40000,
    GX_TMEM_HI + 0x18000, GX_TMEM_LO + 0x60000, GX_TMEM_HI + 0x38000,
    GX_TMEM_HI + 0x00000, GX_TMEM_LO + 0x10000, GX_TMEM_HI + 0x20000,
    GX_TMEM_LO + 0x30000, GX_TMEM_HI + 0x08000, GX_TMEM_LO + 0x50000,
    GX_TMEM_HI + 0x28000, GX_TMEM_LO + 0x70000, GX_TMEM_LO + 0x00000,
    GX_TMEM_HI + 0x10000, GX_TMEM_LO + 0x20000, GX_TMEM_HI + 0x30000,
    GX_TMEM_LO + 0x40000, GX_TMEM_HI + 0x10000, GX_TMEM_LO + 0x60000,
    GX_TMEM_HI + 0x30000, GX_TMEM_HI + 0x00000, GX_TMEM_LO + 0x10000,
    GX_TMEM_HI + 0x20000, GX_TMEM_LO + 0x30000, GX_TMEM_HI + 0x00000,
    GX_TMEM_LO + 0x50000, GX_TMEM_HI + 0x20000, GX_TMEM_LO + 0x70000,
};

static OSShutdownFunctionInfo GXShutdownFuncInfo = {__GXShutdown, 127};

static void EnableWriteGatherPipe(void) {
    u32 hid2 = PPCMfhid2();
    PPCMtwpar(0x0C008000);
    hid2 |= 0x40000000;
    PPCMthid2(hid2);
}

static GXTexRegion* __GXDefaultTexRegionCallback(const GXTexObj* obj,
                                                 GXTexMapID id) {
    GXTexFmt fmt;
    GXBool mipmap;

    fmt = GXGetTexObjFmt(obj);
    mipmap = GXGetTexObjMipMap(obj);
    id = (GXTexMapID)(id % 8);

    switch (fmt) {
    case GX_TF_RGBA8:
        if (mipmap) {
            return &gxdt->TexRegions2[id];
        }
        return &gxdt->TexRegions1[id];
    case GX_TF_C4:
    case GX_TF_C8:
    case GX_TF_C14X2:
        return &gxdt->TexRegions0[id];
    default:
        if (mipmap) {
            return &gxdt->TexRegions1[id];
        }
        return &gxdt->TexRegions0[id];
    }
}

static GXTlutRegion* __GXDefaultTlutRegionCallback(u32 idx) {
    if (idx >= 20) {
        return NULL;
    }

    return &gxdt->TlutRegions[idx];
}

static inline u32 __GXReadMEMCounterU32(u32 regAddrL, u32 regAddrH) {
    u32 ctrH0, ctrH1, ctrL;

    ctrH0 = ((volatile u16*)__memReg)[regAddrH];
    do {
        ctrH1 = ctrH0;
        ctrL = ((volatile u16*)__memReg)[regAddrL];
        ctrH0 = ((volatile u16*)__memReg)[regAddrH];
    } while (ctrH0 != ctrH1);

    return (ctrH0 << 16) | ctrL;
}

static BOOL __GXShutdown(BOOL final, u32 event) {
    static u32 peCount;
    static s64 time;
    static u32 calledOnce = 0;
    u32 peCountNew;
    s64 timeNew;

    if (!final) {
        if (!calledOnce) {
            peCount = __GXReadMEMCounterU32(0x28, 0x27);
            time = OSGetTime();
            calledOnce = 1;
            return FALSE;
        }

        timeNew = OSGetTime();
        peCountNew = __GXReadMEMCounterU32(0x28, 0x27);

        if (timeNew - time < 10) {
            return FALSE;
        }

        if (peCountNew != peCount) {
            peCount = peCountNew;
            time = timeNew;
            return FALSE;
        }
    } else {
        GXSetBreakPtCallback(NULL);
        GXSetDrawSyncCallback(NULL);
        GXSetDrawDoneCallback(NULL);

        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        WGPIPE_WRITE(i, 0);
        PPCSync();

        ((volatile u16*)__cpReg)[1] = 0;
        ((volatile u16*)__cpReg)[2] = 3;
        gxdt->BYTE_0x5FA = TRUE;
        __GXAbort();
    }

    return TRUE;
}

void __GXInitRevisionBits(void) {
    u32 i;

    for (i = 0; i < GX_MAX_VTXFMT; i++) {
        gxdt->vatA[i] = GX_BITSET(gxdt->vatA[i], 1, 1, 1);
        gxdt->vatB[i] = GX_BITSET(gxdt->vatB[i], 0, 1, 1);
        GX_CP_LOAD_REG(0x80 | i, gxdt->vatB[i]);
    }

    {
        u32 reg1 = 0;
        u32 reg2 = 0;

        reg1 = GX_BITSET(reg1, 31, 1, 1);
        reg1 = GX_BITSET(reg1, 30, 1, 1);
        reg1 = GX_BITSET(reg1, 29, 1, 1);
        reg1 = GX_BITSET(reg1, 28, 1, 1);
        reg1 = GX_BITSET(reg1, 27, 1, 1);
        reg1 = GX_BITSET(reg1, 26, 1, 1);
        GX_XF_LOAD_REG(0x1000, reg1);

        reg2 = GX_BITSET(reg2, 31, 1, 1);
        GX_XF_LOAD_REG(0x1012, reg2);
    }

    {
        u32 reg = 0;

        reg = GX_BITSET(reg, 31, 1, 1);
        reg = GX_BITSET(reg, 30, 1, 1);
        reg = GX_BITSET(reg, 29, 1, 1);
        reg = GX_BITSET(reg, 28, 1, 1);
        reg = GX_BITSET(reg, 0, 8, 0x58);
        GX_BP_LOAD_REG(reg);
    }
}

void __GXInitGX(void);

GXFifoObj* GXInit(void* base, u32 size) {
    static u32 shutdownFuncRegistered = 0;
    u32 i;

    OSRegisterVersion(__GXVersion);

    gxdt->dlistActive = FALSE;
    gxdt->dlistSave = TRUE;
    gxdt->BYTE_0x5FA = TRUE;
    gxdt->tcsManEnab = 0;
    gxdt->tevTcEnab = 0;

    GXSetMisc(1, 0);

    __piReg = OSPhysicalToUncached(0x0C003000);
    __cpReg = OSPhysicalToUncached(0x0C000000);
    __peReg = OSPhysicalToUncached(0x0C001000);
    __memReg = OSPhysicalToUncached(0x0C004000);

    __GXFifoInit();
    GXInitFifoBase(&FifoObj, base, size);
    GXSetCPUFifo(&FifoObj);
    GXSetGPFifo(&FifoObj);

    if (!shutdownFuncRegistered) {
        OSRegisterShutdownFunction(&GXShutdownFuncInfo);
        shutdownFuncRegistered = 1;
    }

    __GXPEInit();
    EnableWriteGatherPipe();

    gxdt->genMode = 0;
    gxdt->genMode = GX_BITSET(gxdt->genMode, 0, 8, 0);
    gxdt->ind_imask = 0xFF;
    gxdt->ind_imask = GX_BITSET(gxdt->ind_imask, 0, 8, 0xF);
    gxdt->linePtWidth = 0;
    gxdt->linePtWidth = GX_BITSET(gxdt->linePtWidth, 0, 8, 0x22);

    for (i = 0; i < 16; i++) {
        gxdt->tevc[i] = 0;
        gxdt->teva[i] = 0;
        gxdt->tref[i / 2] = 0;
        gxdt->texmapId[i] = GX_TEXMAP_NULL;
        gxdt->tevc[i] = GX_BITSET(gxdt->tevc[i], 0, 8, 0xC0 + i * 2);
        gxdt->teva[i] = GX_BITSET(gxdt->teva[i], 0, 8, 0xC1 + i * 2);
        gxdt->tevKsel[i / 2] =
            GX_BITSET(gxdt->tevKsel[i / 2], 0, 8, 0xF6 + i / 2);
        gxdt->tref[i / 2] = GX_BITSET(gxdt->tref[i / 2], 0, 8, 0x28 + i / 2);
    }

    gxdt->ras1_iref = 0;
    gxdt->ras1_iref = GX_BITSET(gxdt->ras1_iref, 0, 8, 0x27);

    for (i = 0; i < 8; i++) {
        gxdt->txcRegs[i] = 0;
        gxdt->suTs1[i] = 0;
        gxdt->txcRegs[i] = GX_BITSET(gxdt->txcRegs[i], 0, 8, 0x30 + i * 2);
        gxdt->suTs1[i] = GX_BITSET(gxdt->suTs1[i], 0, 8, 0x31 + i * 2);
    }

    gxdt->scissorTL = GX_BITSET(gxdt->scissorTL, 0, 8, 0x20);
    gxdt->scissorBR = GX_BITSET(gxdt->scissorBR, 0, 8, 0x21);
    gxdt->blendMode = GX_BITSET(gxdt->blendMode, 0, 8, 0x41);
    gxdt->dstAlpha = GX_BITSET(gxdt->dstAlpha, 0, 8, 0x42);
    gxdt->zMode = GX_BITSET(gxdt->zMode, 0, 8, 0x40);
    gxdt->zControl = GX_BITSET(gxdt->zControl, 0, 8, 0x43);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 23, 2, 0);

    gxdt->scaleZ = (f32)(0xFFFFFF + 1);
    gxdt->offsetZ = 0.0f;
    gxdt->gxDirtyFlags = 0;
    gxdt->vatDirtyFlags = 0;

    {
        u32 reg;
        u32 freqBase = OS_BUS_CLOCK_SPEED / 500;

        __GXFlushTextureState();
        reg = (freqBase / 2048) | 1 << 10 | 0x69 << 24;
        GX_BP_LOAD_REG(reg);
        __GXFlushTextureState();

        reg = (freqBase / 4224) | 1 << 9 | 0x46 << 24;
        GX_BP_LOAD_REG(reg);
    }

    __GXInitRevisionBits();

    for (i = 0; i < 8; i++) {
        GXInitTexCacheRegion(&gxdt->TexRegions0[i], GX_FALSE,
                             GXTexRegionAddrTable[i], GX_TEXCACHE_32K,
                             GXTexRegionAddrTable[i + 8], GX_TEXCACHE_32K);
        GXInitTexCacheRegion(&gxdt->TexRegions1[i], GX_FALSE,
                             GXTexRegionAddrTable[i + 16], GX_TEXCACHE_32K,
                             GXTexRegionAddrTable[i + 24], GX_TEXCACHE_32K);
        GXInitTexCacheRegion(&gxdt->TexRegions2[i], GX_TRUE,
                             GXTexRegionAddrTable[i + 32], GX_TEXCACHE_32K,
                             GXTexRegionAddrTable[i + 40], GX_TEXCACHE_32K);
    }

    for (i = 0; i < 16; i++) {
        GXInitTlutRegion(&gxdt->TlutRegions[i],
                         GX_TMEM_HI + GX_32K * 8 + GX_8K * i, GX_TLUT_256);
    }

    for (i = 0; i < 4; i++) {
        GXInitTlutRegion(&gxdt->TlutRegions[i + 16],
                         GX_TMEM_HI + GX_32K * 8 + GX_8K * 16 + GX_32K * i,
                         GX_TLUT_1K);
    }

    {
        ((volatile u16*)__cpReg)[3] = 0;

        gxdt->perfSel = GX_BITSET(gxdt->perfSel, 24, 4, 0);
        GX_CP_LOAD_REG(0x20, gxdt->perfSel);
        GX_XF_LOAD_REG(0x1006, 0);
        GX_BP_LOAD_REG(0x23000000);
        GX_BP_LOAD_REG(0x24000000);
        GX_BP_LOAD_REG(0x67000000);
    }

    __GXSetIndirectMask(0);
    __GXSetTmemConfig(2);
    __GXInitGX();

    return &FifoObj;
}

void __GXInitGX(void) {
    GXRenderModeObj* rmode;
    GXTexObj tex_obj;
    Mtx identity_mtx;
    GXColor clear = {64, 64, 64, 255};
    GXColor black = {0, 0, 0, 0};
    GXColor white = {255, 255, 255, 255};
    u32 i;

    switch (VIGetTvFormat()) {
    case VI_TVFORMAT_NTSC:
        rmode = &GXNtsc480IntDf;
        break;
    case VI_TVFORMAT_PAL:
        rmode = &GXPal528IntDf;
        break;
    case VI_TVFORMAT_EURGB60:
        rmode = &GXEurgb60Hz480IntDf;
        break;
    case VI_TVFORMAT_MPAL:
        rmode = &GXMpal480IntDf;
        break;
    default:
        rmode = &GXNtsc480IntDf;
        break;
    }

    GXSetCopyClear(clear, 0xFFFFFF);

    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX3, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_TEX4, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD5, GX_TG_MTX2x4, GX_TG_TEX5, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD6, GX_TG_MTX2x4, GX_TG_TEX6, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD7, GX_TG_MTX2x4, GX_TG_TEX7, GX_IDENTITY, FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(1);
    GXClearVtxDesc();
    GXInvalidateVtxCache();

    for (i = GX_VA_POS; i <= GX_LIGHT_ARRAY; i++) {
        GXSetArray((GXAttr)i, PORT_GP_STATIC(gxdt, sizeof(GXData)), 0);
    }

    for (i = 0; i < GX_MAX_VTXFMT; i++) {
        GXSetVtxAttrFmtv((GXVtxFmt)i, GXDefaultVATList);
    }

    GXSetLineWidth(6, 0);
    GXSetPointSize(6, 0);
    GXEnableTexOffsets(GX_TEXCOORD0, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD1, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD2, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD3, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD4, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD5, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD6, GX_DISABLE, GX_DISABLE);
    GXEnableTexOffsets(GX_TEXCOORD7, GX_DISABLE, GX_DISABLE);

    identity_mtx[0][0] = 1.0f;
    identity_mtx[0][1] = 0.0f;
    identity_mtx[0][2] = 0.0f;
    identity_mtx[0][3] = 0.0f;
    identity_mtx[1][0] = 0.0f;
    identity_mtx[1][1] = 1.0f;
    identity_mtx[1][2] = 0.0f;
    identity_mtx[1][3] = 0.0f;
    identity_mtx[2][0] = 0.0f;
    identity_mtx[2][1] = 0.0f;
    identity_mtx[2][2] = 1.0f;
    identity_mtx[2][3] = 0.0f;

    GXLoadPosMtxImm(identity_mtx, GX_PNMTX0);
    GXLoadNrmMtxImm(identity_mtx, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXLoadTexMtxImm(identity_mtx, GX_IDENTITY, GX_MTX_3x4);
    GXLoadTexMtxImm(identity_mtx, GX_PTIDENTITY, GX_MTX_3x4);
    GXSetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->xfbHeight, 0.0f, 1.0f);
    GXSetProjectionv(GXDefaultProjData);
    GXSetCoPlanar(GX_DISABLE);
    GXSetCullMode(GX_CULL_BACK);
    GXSetClipMode(GX_CLIP_ENABLE);
    GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GXSetScissorBoxOffset(0, 0);
    GXSetNumChans(0);
    GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanAmbColor(GX_COLOR0A0, black);
    GXSetChanMatColor(GX_COLOR0A0, white);
    GXSetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanAmbColor(GX_COLOR1A1, black);
    GXSetChanMatColor(GX_COLOR1A1, white);
    GXInvalidateTexAll();
    GXSetTexRegionCallback(__GXDefaultTexRegionCallback);
    GXSetTlutRegionCallback(__GXDefaultTlutRegionCallback);

    GXInitTexObj(&tex_obj, PORT_GP_STATIC(DefaultTexData, sizeof(DefaultTexData)), 4, 4,
                 GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&tex_obj, GX_TEXMAP0);
    GXLoadTexObj(&tex_obj, GX_TEXMAP1);
    GXLoadTexObj(&tex_obj, GX_TEXMAP2);
    GXLoadTexObj(&tex_obj, GX_TEXMAP3);
    GXLoadTexObj(&tex_obj, GX_TEXMAP4);
    GXLoadTexObj(&tex_obj, GX_TEXMAP5);
    GXLoadTexObj(&tex_obj, GX_TEXMAP6);
    GXLoadTexObj(&tex_obj, GX_TEXMAP7);

    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD2, GX_TEXMAP2, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP3, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD4, GX_TEXMAP4, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD5, GX_TEXMAP5, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE6, GX_TEXCOORD6, GX_TEXMAP6, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE7, GX_TEXCOORD7, GX_TEXMAP7, GX_COLOR0A0);
    GXSetTevOrder(GX_TEVSTAGE8, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE9, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE10, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE11, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE12, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE13, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE14, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOrder(GX_TEVSTAGE15, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetNumTevStages(1);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);

    for (i = 0; i < GX_MAX_TEVSTAGE; i++) {
        GXSetTevKColorSel((GXTevStageID)i, GX_TEV_KCSEL_1_4);
        GXSetTevKAlphaSel((GXTevStageID)i, GX_TEV_KASEL_1);
        GXSetTevSwapMode((GXTevStageID)i, GX_TEV_SWAP0, GX_TEV_SWAP0);
    }

    GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
    GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
    GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);

    for (i = 0; i < GX_MAX_TEVSTAGE; i++) {
        GXSetTevDirect((GXTevStageID)i);
    }

    GXSetNumIndStages(0);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE1, GX_ITS_1, GX_ITS_1);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE2, GX_ITS_1, GX_ITS_1);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE3, GX_ITS_1, GX_ITS_1);

    GXSetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, black);
    GXSetFogRangeAdj(GX_DISABLE, 0, NULL);
    GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetColorUpdate(GX_ENABLE);
    GXSetAlphaUpdate(GX_ENABLE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetZCompLoc(GX_TRUE);
    GXSetDither(GX_ENABLE);
    GXSetDstAlpha(GX_DISABLE, 0);
    GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GXSetFieldMask(GX_ENABLE, GX_ENABLE);
    GXSetFieldMode(rmode->field_rendering,
                   rmode->viHeight == 2 * rmode->xfbHeight ? GX_ENABLE : GX_DISABLE);
    GXSetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GXSetDispCopyDst(rmode->fbWidth, rmode->efbHeight);
    GXSetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight);
    GXSetCopyClamp((GXCopyClamp)(GX_CLAMP_TOP | GX_CLAMP_BOTTOM));
    GXSetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GXSetDispCopyGamma(0);
    GXSetDispCopyFrame2Field(0);
    GXClearBoundingBox();

    GXPokeColorUpdate(GX_TRUE);
    GXPokeAlphaUpdate(GX_TRUE);
    GXPokeDither(GX_FALSE);
    GXPokeBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ONE, GX_LO_SET);
    GXPokeAlphaMode(GX_ALWAYS, 0);
    GXPokeAlphaRead(1);
    GXPokeDstAlpha(GX_DISABLE, 0);
    GXPokeZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);

    GXSetGPMetric(0x23, 0x16);
    GXClearGPMetric();
}
