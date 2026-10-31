#include <egg/core.h>
#include <egg/gfxe.h>

#include <nw4r/math.h>

#include <revolution/GX.h>
#include <revolution/MTX.h>

#include <cstring>

namespace EGG {

u16 LightTexture::sTextureSize = 32;
template <> const char* IBinary<LightTexture>::spExtension = "bltex";

const GXTevColorArg scColorIn[] = {GX_CC_C0, GX_CC_C1, GX_CC_C2};
const GXTevKColorSel scKColorSel[] = {GX_TEV_KCSEL_K0, GX_TEV_KCSEL_K1,
                                      GX_TEV_KCSEL_K2};
const GXIndTexMtxID scIndMtx[] = {GX_ITM_0, GX_ITM_1, GX_ITM_2};

static void setupColor(const GXColor& rColor) {
    GXSetNumTexGens(0);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE,
                          GX_CH_ALPHA);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                  GX_COLOR_NULL);
    GXSetTevKColor(GX_KCOLOR0, rColor);
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_KONST, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO,
                    GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVPREV);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
}

static void setupNormalEnv() {
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumIndStages(1);
    GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP0);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
    GXSetNumTevStages(3);
    GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE,
                          GX_CH_ALPHA);

    for (int i = 0; i < 3; i++) {
        GXTevColorArg d = i == 0 ? GX_CC_ZERO : GX_CC_CPREV;
        GXTevStageID stage = static_cast<GXTevStageID>(i);

        GXSetTevSwapMode(stage, GX_TEV_SWAP0, GX_TEV_SWAP0);
        GXSetTevOrder(stage, GX_TEXCOORD_NULL,
                      static_cast<GXTexMapID>(GX_TEXMAP1 + i), GX_COLOR_NULL);
        GXSetTevKColorSel(stage, scKColorSel[i]);
        GXSetTevColorIn(stage, GX_CC_KONST, scColorIn[i], GX_CC_TEXC, d);
        GXSetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                        GX_TEVPREV);
        GXSetTevAlphaIn(stage, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO,
                        GX_CA_ZERO);
        GXSetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                        GX_TEVPREV);
        GXSetTevIndirect(stage, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_STU,
                         scIndMtx[i], GX_ITW_0, GX_ITW_0, GX_FALSE,
                         GX_FALSE, GX_ITBA_OFF);
    }

    GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
}

static void setupNormal() {
    GXSetNumTexGens(2);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(2);
    GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE,
                          GX_CH_ALPHA);

    for (int i = 0; i < 2; i++) {
        GXTevColorArg d = i == 0 ? GX_CC_ZERO : GX_CC_CPREV;
        GXTevStageID stage = static_cast<GXTevStageID>(i);

        GXSetTevOrder(stage, static_cast<GXTexCoordID>(i),
                      static_cast<GXTexMapID>(i), GX_COLOR_NULL);
        GXSetTevKColorSel(stage, scKColorSel[i]);
        GXSetTevColorIn(stage, GX_CC_KONST, scColorIn[i], GX_CC_TEXC, d);
        GXSetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                        GX_TEVPREV);
        GXSetTevAlphaIn(stage, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO,
                        GX_CA_ZERO);
        GXSetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                        GX_TEVPREV);
        GXSetTevDirect(stage);
    }

    GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
}

static inline void drawQuad(f32 x, f32 y) {
    f32 size = 0.99f;
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition1x8(0);
    GXTexCoord2f32(x, y);
    GXPosition1x8(1);
    GXTexCoord2f32(x, y - size);
    GXPosition1x8(2);
    GXTexCoord2f32(x + size, y - size);
    GXPosition1x8(3);
    GXTexCoord2f32(x + size, y);
    GXEnd();
}

static inline void setTevColor(int stage, GXColor color) {
    GXSetTevColor(static_cast<GXTevRegID>(stage + GX_TEVREG0), color);
}

const CpuTexture::EGradationKind scPatternKind[LightTexture::cPttrn_Max] = {
    CpuTexture::GRADATION_LINEAR,          CpuTexture::GRADATION_LINEAR,
    CpuTexture::GRADATION_LINEAR,          CpuTexture::GRADATION_QUADRATIC,
    CpuTexture::GRADATION_CUBIC,           CpuTexture::GRADATION_QUARTIC,
    CpuTexture::GRADATION_QUINTIC,         CpuTexture::GRADATION_LINEAR,
    CpuTexture::GRADATION_QUADRATIC_EASEOUT,
    CpuTexture::GRADATION_QUADRATIC_EASEOUT,
};

static const s16 scQuadPos[4 * 3] ALIGN(32) = {
    0, 0, 0, //
    0, 1, 0, //
    1, 1, 0, //
    1, 0, 0, //
};

CpuTexture* LightTexture::spPatternSet[LightTexture::cPttrn_Max];
u16 LightTexture::sDrawWidth;
u16 LightTexture::sDrawHeight;
u16 LightTexture::sDrawPosX;
u16 LightTexture::sDrawPosY;
u16 LightTexture::sTexWidth;
u16 LightTexture::sTexHeight;
u16 LightTexture::sDrawNumX;
u16 LightTexture::sDrawNumY;
CpuTexture* LightTexture::spNormalEnvironment;

void LightTexture::initialize(u16 textureSize, Heap* pHeap) {
    if (pHeap == NULL) {
        pHeap = Heap::getCurrentHeap();
    }

    sTextureSize = textureSize;

    for (int i = 0; i < cPttrn_Max; i++) {
        CpuTexture* pTex = spPatternSet[i] =
            new (pHeap) CpuTexture(64, 4, GX_TF_I8);
        pTex->configure();
        pTex->setWrapS(GX_REPEAT);
        pTex->setWrapT(GX_REPEAT);

        int start = 32;
        int goal = 64;

        switch (i) {
        case 1:
            start = 0;
            break;
        case 2:
            start = 16;
            break;
        case 7:
            start = 4;
            goal = 36;
            break;
        case 8:
            start = 10;
            goal = 42;
            break;
        case 9:
            start = 13;
            goal = 45;
            break;
        }

        pTex->alloc();
        pTex->fillGradationTable(scPatternKind[i], 's', start + 1, goal - 1,
                                 DrawGX::BLACK, DrawGX::WHITE, true, true);
    }

    spNormalEnvironment =
        new (pHeap) CpuTexture(sTextureSize, sTextureSize, GX_TF_RGBA8);
    spNormalEnvironment->configure();
    spNormalEnvironment->alloc();
    spNormalEnvironment->fillNormalMapSphere();
    spNormalEnvironment->setWrapS(GX_REPEAT);
    spNormalEnvironment->setWrapT(GX_REPEAT);
}

LightTexture::LightTexture(u16 index, const char* name,
                           const LightTextureManager* pManager)
    : CapTexture(sTextureSize, sTextureSize, GX_TF_RGBA8),
      mLightNum(pManager->getLightObjectMax()),
      unk34(0.0f, 0.0f, 0.0f),
      mLightMaskLo(0),
      mLightMaskHi(0),
      mDrawType(0),
      mpEffectScale(NULL),
      mpPttrn(NULL),
      mpManager(pManager) {

#line 338
    EGG_ASSERT(mLightNum > 0);
    EGG_ASSERT(name);

    u32 len = std::strlen(name);
    if (len >= MAX_NAME_LEN - 1) {
        len = MAX_NAME_LEN - 2;
    }

    for (int i = 0; i < len; i++) {
        mName[i] = name[i];
    }
    mName[len] = '\0';

    mIndex = index;

    if (getWidth() == 256) {
        mLodBias = 1;
    } else if (getWidth() == 128) {
        mLodBias = 0;
    } else if (getWidth() == 64) {
        mLodBias = -1;
    } else if (getWidth() == 32) {
        mLodBias = -2;
    } else if (getWidth() == 16) {
        mLodBias = -3;
    } else if (getWidth() == 8) {
        mLodBias = -4;
    } else if (getWidth() == 4) {
        mLodBias = -5;
    }

    mSetting.mStatus = cDrawStatus_None;

    mpEffectScale = new f32[mLightNum];
#line 388
    EGG_ASSERT(mpEffectScale != NULL);
    mpPttrn = new int[mLightNum];
#line 390
    EGG_ASSERT(mpPttrn != NULL);

    for (int i = 0; i < mLightNum; i++) {
        mpPttrn[i] = 0;
        mpEffectScale[i] = 1.0f;
    }
}

LightTexture::~LightTexture() {
    if (mpEffectScale != NULL) {
        delete mpEffectScale;
    }
    if (mpPttrn != NULL) {
        delete mpPttrn;
    }
}

void LightTexture::configure() {
    CapTexture::configure();
    allocWithHeaderDebug();
    setWrapS(GX_CLAMP);
    setWrapT(GX_CLAMP);
    disablePixModeSync();
}

void LightTexture::initDrawSetting(u16 x, u16 y, u16 width, u16 height) {
    Mtx44 proj;
    C_MTXOrtho(proj, height, 0.0f, 0.0f, width, 0.0f, 1.0f);
    StateGX::GXSetProjection_(proj, GX_ORTHOGRAPHIC);
    StateGX::GXSetViewport_(x, y, width, height, 0.0f, 1.0f);
    StateGX::GXSetScissor_(x, y, width, height);
    StateGX::GXSetScissorBoxOffset_(0, 0);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetArray(GX_VA_POS, PORT_GP_STATIC_BE16(scQuadPos, sizeof(scQuadPos)), sizeof(s16) * 3);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    sDrawWidth = width;
    sDrawHeight = height;
    sDrawPosX = x;
    sDrawPosY = y;
    sTexWidth = sTextureSize;
    sTexHeight = sTextureSize;
    sDrawNumX = width / sTextureSize;
    sDrawNumY = height / sTextureSize;

    GXSetNumChans(1);
    GXSetCullMode(GX_CULL_BACK);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL,
                  GX_DF_SIGN, GX_AF_NONE);
    GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL,
                  GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
}

void LightTexture::getDrawSetting(u16* posX, u16* posY, u16* width,
                                  u16* height, u16 index) {
#line 498
    EGG_ASSERT(posX);
    EGG_ASSERT(posY);
    EGG_ASSERT(width);
    EGG_ASSERT(height);

    *posX = sDrawPosX;
    *posY = sDrawPosY;

    u16 y = index / (sDrawNumX + 1);
    u16 x = index - y * sDrawNumX;
    *width = x * sTexWidth;
    *height = (y + 1) * sTexHeight;
}

void LightTexture::draw() {
    beginDraw(mpManager->getLightManager()->GetLightTexColor());

    for (int i = 0; i < mpManager->getLightObjectMax(); i++) {
        LightObject* pLight = mpManager->getLightObject(i);
        if (pLight == NULL) {
            break;
        }

        addLight(*pLight);
    }

    endDraw();
}

void LightTexture::beginDraw(const GXColor& rColor) {
#line 545
    EGG_ASSERT(mSetting.mStatus == cDrawStatus_None);
    EGG_ASSERT(getBuffer());
    // clang-format off
#line 547
    EGG_ASSERT_MSG(sTexWidth == getWidth() && sTexHeight == getHeight(), "This map can't write this setting!!");
    // clang-format on

    mSetting.mLightCnt = 0;
    mSetting.mStatus = cDrawStatus_Drawing;

    mSetting.mPosY = mIndex / sDrawNumX;
    mSetting.mPosX = sTexWidth * (mIndex - mSetting.mPosY * sDrawNumX);
    mSetting.mPosY = mSetting.mPosY * sTexHeight;

    nw4r::math::MTX34 mtx;
    PSMTXIdentity(mtx);
    GXLoadTexMtxImm(mtx, GX_TEXMTX0, GX_MTX_2x4);

    PSMTXScale(mtx, sTexWidth, sTexHeight, 1.0f);
    mtx._03 = mSetting.mPosX;
    mtx._13 = static_cast<int>(sDrawHeight - (mSetting.mPosY + sTexHeight));
    GXSetCurrentMtx(GX_PNMTX0);
    GXLoadPosMtxImm(mtx, GX_PNMTX0);

    setupColor(rColor);
    f32 ofs = 0.0f;
    drawQuad(ofs, ofs);

    switch (mDrawType) {
    case 0:
    case 2:
#line 579
        EGG_ASSERT(getNormalEnvironment());
        getNormalEnvironment()->load(GX_TEXMAP0);
        setupNormalEnv();
        break;
    case 1:
        setupNormal();
        break;
    }

    switch (mDrawType) {
    case 0:
    case 2:
        mSetting.mPassNum = 3;
        mSetting.mTexMapBase = 1;
        break;
    case 1:
        mSetting.mPassNum = 2;
        mSetting.mTexMapBase = 0;
        break;
    }
}

void LightTexture::addLight(const LightObject& rLight) {
#line 665
    EGG_ASSERT(mSetting.mStatus == cDrawStatus_Drawing);

    if (!rLight.IsEnable() || !rLight.IsFlag5() ||
        !(1 << rLight.GetIndex() & getLightMask()) ||
        (mSetting.mLightCnt >= 2 && static_cast<u8>(mDrawType) == 1)) {
        return;
    }

    nw4r::math::VEC3 dir;
    GXColor color;
    rLight.CalcFinalDirColor(*this, &dir, &color);

    int stage = mSetting.mLightCnt % mSetting.mPassNum;
    if (mSetting.mLightCnt == mSetting.mPassNum) {
        GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
    }

    getPatternTexture(mpPttrn[rLight.GetIndex()])
        ->load(static_cast<GXTexMapID>(mSetting.mTexMapBase + stage));

    const GXColor& rDark = rLight.GetDarknessColor();
    f32 intensity = rLight.GetIntensity();
    GXColor kcolor;
    kcolor.r = rDark.r * intensity;
    kcolor.g = rDark.g * intensity;
    kcolor.b = rDark.b * intensity;
    kcolor.a = rDark.a;

    setTevColor(stage, color);
    GXSetTevKColor(static_cast<GXTevKColorID>(stage), kcolor);

    switch (mDrawType) {
    case 0:
    case 2:
        f32 ind[2][3];
        ind[0][0] = -dir.x * 0.484375f;
        ind[0][1] = -dir.y * 0.484375f;
        ind[0][2] = -dir.z * 0.484375f;
        ind[1][0] = 0.0f;
        ind[1][1] = 0.0f;
        ind[1][2] = 0.0f;
        GXSetIndTexMtx(scIndMtx[stage], ind, -1);
        break;
    }

    if (stage == mSetting.mPassNum - 1) {
        f32 x = mDrawType == 2 ? 0.5f : 0.0f;
        f32 y = mDrawType == 2 ? 0.5f : 0.0f;
        drawQuad(x, y);
    }

    mSetting.mLightCnt++;
}

void LightTexture::endDraw() {
#line 734
    EGG_ASSERT(mSetting.mStatus == cDrawStatus_Drawing);

    int stage = mSetting.mLightCnt % mSetting.mPassNum;
    if (stage > 0) {
        GXColor black;
        black.r = black.g = black.b = 0;
        black.a = 255;

        for (; stage < mSetting.mPassNum; stage++) {
            setTevColor(stage, black);
            GXSetTevKColor(static_cast<GXTevKColorID>(stage), black);
        }

        f32 x = mDrawType == 2 ? 0.5f : 0.0f;
        f32 y = mDrawType == 2 ? 0.5f : 0.0f;
        drawQuad(x, y);
    }

    mSetting.mStatus = cDrawStatus_Drawn;
}

void LightTexture::capture(bool clear) {
    if (mSetting.mStatus != cDrawStatus_Drawn) {
        return;
    }

    if (clear) {
        enableColorUpdate();
        enableAlphaUpdate();
        setClearColor(StateGX::getEfbClearColor());
    } else {
        disableColorUpdate();
        disableAlphaUpdate();
    }

    CapTexture::capture(sDrawPosX + mSetting.mPosX, sDrawPosY + mSetting.mPosY);
    mSetting.mStatus = cDrawStatus_None;
}

const char* LightTexture::GetBinaryType() const {
    return "LTEX";
}

// The drawType argument comes before the mask: its value gets the register the original uses.
static inline void SetDrawTypeLightMask(u16& rMaskHi, int& rDrawType, u16& rMaskLo,
                                        u8 drawType, u32 lightMask) {
    rMaskHi = lightMask & 0xFFFF0000;
    rDrawType = drawType;
    rMaskLo = lightMask;
}

void LightTexture::SetBinaryInner(const Bin& rBin) {
    switch (rBin.header.version) {
    case 0:
        SetDrawTypeLightMask(mLightMaskHi, mDrawType, mLightMaskLo,
                             rBin.data.drawType, rBin.data.lightMask);

        u16 num = rBin.data.lightNum;
        if (num > mLightNum) {
            num = mLightNum;
        }

        const int n = num;

        const BinData::Light* pLights = rBin.data.lights;
        for (int i = 0; i < n; i++) {
            setEffectScale(i, pLights[i].effectScale);
            setPttrn(i, pLights[i].pttrn);
        }
        break;
    }
}

void LightTexture::GetBinaryInner(Bin* pBin) const {
    pBin->header.version = 0;
    pBin->data.drawType = mDrawType;
    pBin->data.lightNum = mLightNum;
    std::strcpy(pBin->data.name, mName);
    pBin->data.lightMask = getLightMask();

    BinData::Light* pLights = pBin->data.lights;
    for (int i = 0; i < mLightNum; i++) {
        pLights[i].effectScale = getEffectScale(i);
        pLights[i].pttrn = getPttrn(i);
    }
}

template <> u8 IBinary<LightTexture>::GetVersion() const {
    return 0;
}

u32 LightTexture::GetBinarySize() const {
    return sizeof(Bin) + (mLightNum - 1) * sizeof(BinData::Light);
}

} // namespace EGG
