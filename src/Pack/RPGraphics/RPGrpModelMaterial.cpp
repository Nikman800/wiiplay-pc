#include <Pack/RPGraphics.h>

#include <nw4r/g3d.h>

// Channel ID pairs {COLOR0, ALPHA0}, {COLOR1, ALPHA1}; defined in RPGrpView.cpp (linked), whose
// literal pool they follow in the original
extern "C" const u8 lbl_8043E9B8[4];
extern "C" const u8 lbl_8043E9BC[4];

// Pixel engine defaults: alpha test always passes, z test/update on (LEQUAL), blending off with
// the usual source-alpha factors
static const RPGrpModelMaterial::PixMode sDefaultPixMode = {
    GX_ALWAYS, GX_ALWAYS, 0,     0, GX_AOP_OR, true, true, GX_LEQUAL, GX_BM_NONE, GX_BL_SRCALPHA,
    GX_BL_INVSRCALPHA, GX_LO_CLEAR, false, 0, {0, 0}};

inline nw4r::g3d::ResGenMode RPGrpModelMaterial::GetGenMode() {
    if (!mCopiedMatAccess.IsValid()) {
        return mResMat.GetResGenMode();
    }
    nw4r::g3d::ResGenMode res = mCopiedMatAccess.GetResGenMode(false);
    if (res.IsValid()) {
        return res;
    }
    return mResMat.GetResGenMode();
}

inline nw4r::g3d::ResMatChan RPGrpModelMaterial::GetMatChan() {
    if (!mCopiedMatAccess.IsValid()) {
        return mResMat.GetResMatChan();
    }
    nw4r::g3d::ResMatChan res = mCopiedMatAccess.GetResMatChan(false);
    if (res.IsValid()) {
        return res;
    }
    return mResMat.GetResMatChan();
}

inline nw4r::g3d::ResMatPix RPGrpModelMaterial::GetMatPix() {
    if (!mCopiedMatAccess.IsValid()) {
        return mResMat.GetResMatPix();
    }
    nw4r::g3d::ResMatPix res = mCopiedMatAccess.GetResMatPix(false);
    if (res.IsValid()) {
        return res;
    }
    return mResMat.GetResMatPix();
}

inline nw4r::g3d::ResMatTevColor RPGrpModelMaterial::GetMatTevColor() {
    if (!mCopiedMatAccess.IsValid()) {
        return mResMat.GetResMatTevColor();
    }
    nw4r::g3d::ResMatTevColor res = mCopiedMatAccess.GetResMatTevColor(false);
    if (res.IsValid()) {
        return res;
    }
    return mResMat.GetResMatTevColor();
}

inline nw4r::g3d::ResTev RPGrpModelMaterial::GetTev() {
    if (!mCopiedMatAccess.IsValid()) {
        return static_cast<const nw4r::g3d::ResMat&>(mResMat).GetResTev();
    }
    nw4r::g3d::ResTev res = mCopiedMatAccess.GetResTev(false);
    if (res.IsValid()) {
        return res;
    }
    return static_cast<const nw4r::g3d::ResMat&>(mResMat).GetResTev();
}

inline nw4r::g3d::ResTexSrt RPGrpModelMaterial::GetTexSrt() {
    if (!mCopiedMatAccess.IsValid()) {
        return mResMat.GetResTexSrt();
    }
    nw4r::g3d::ResTexSrt res = mCopiedMatAccess.GetResTexSrt(false);
    if (res.IsValid()) {
        return res;
    }
    return mResMat.GetResTexSrt();
}

inline void RPGrpModelMaterial::EndEditMatChan() {
    if (mCopiedMatAccess.IsValid()) {
        mCopiedMatAccess.GetResMatChan(false);
    }
}

inline bool RPGrpModelMaterial::GetChanCtrl(u8 chan, ChanCtrl* pCtrl) {
    GXBool enable;
    GXColorSrc ambSrc;
    GXColorSrc matSrc;
    GXLightID lightMask;
    GXDiffuseFn diffuseFn;
    GXAttnFn attnFn;

    if (GetMatChan().GXGetChanCtrl(static_cast<GXChannelID>(chan), &enable, &ambSrc, &matSrc,
                                   &lightMask, &diffuseFn, &attnFn)) {
        pCtrl->enable = enable != GX_FALSE;
        pCtrl->lightMask = lightMask;
        pCtrl->ambSrc = ambSrc;
        pCtrl->matSrc = matSrc;
        pCtrl->diffuseFn = diffuseFn;
        pCtrl->attnFn = attnFn;
        return true;
    }
    return false;
}

inline void RPGrpModelMaterial::ReadTevColors() {
    nw4r::g3d::ResMatTevColor tevColor = GetMatTevColor();
    for (int i = 0; i < 4; i++) {
        tevColor.GXGetTevKColor(static_cast<GXTevKColorID>(i), &mTevKColors[i]);
    }
    for (int i = 0; i < 3; i++) {
        tevColor.GXGetTevColorS10(static_cast<GXTevRegID>(i + 1), &mTevColors[i]);
    }
}

inline void RPGrpModelMaterial::ReadTevSwap() {
    nw4r::g3d::ResTev tev = GetTev();
    for (int i = 0; i < 4; i++) {
        GXTevColorChan red, green, blue, alpha;
        if (tev.GXGetTevSwapModeTable(static_cast<GXTevSwapSel>(i), &red, &green, &blue,
                                      &alpha)) {
            mSwapTable[i][0] = red;
            mSwapTable[i][1] = green;
            mSwapTable[i][2] = blue;
            mSwapTable[i][3] = alpha;
        }
    }
}

inline void RPGrpModelMaterial::ReadEffectMtx() {
    nw4r::g3d::ResTexSrt texSrt = GetTexSrt();
    for (u32 i = 0; i < 8; i++) {
        nw4r::math::MTX34 mtx;
        if (texSrt.GetEffectMtx(i, &mtx)) {
#if defined(PORT_HOST)
            // The resource's matrix is big-endian: point at a native copy instead (SyncEffectMtx
            // writes it back through ResTexSrt::SetEffectMtx)
            mpEffectMtx[i] = new nw4r::math::MTX34(mtx);
#else
            mpEffectMtx[i] =
                static_cast<nw4r::math::MTX34*>(&texSrt.ref().effect[i].effectMtx);
#endif
        }
    }
}

RPGrpModelMaterial::~RPGrpModelMaterial() {}

RPGrpModelMaterial::RPGrpModelMaterial(RPGrpModel* pModel, u16 idx)
    : mSyncFlags(0),
      unk4(0),
      unk6(3),
      mPixMode(sDefaultPixMode),
      mpTevStageBuf(NULL),
      mResMat(pModel->GetScnMdlSimple() != NULL
                  ? pModel->GetScnMdlSimple()->GetResMdl().GetResMat(idx)
                  : nw4r::g3d::ResMat(NULL)),
      mCopiedMatAccess(pModel->GetScnMdl() != NULL ? pModel->GetScnMdl() : NULL, idx) {
    for (int i = 0; i < 8; i++) {
        mpEffectMtx[i] = NULL;
    }
    for (int i = 0; i < 4; i++) {
        mpChanCtrls[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        mpChanColors[i] = NULL;
    }
    for (int i = 0; i < 4; i++) {
        mSwapTable[i][0] = GX_CH_RED;
        mSwapTable[i][1] = GX_CH_GREEN;
        mSwapTable[i][2] = GX_CH_BLUE;
        mSwapTable[i][3] = GX_CH_ALPHA;
    }

    mNumChans = GetGenMode().GXGetNumChans();
    mNumTevStages = GetGenMode().GXGetNumTevStages();
    mNumTexGens = GetGenMode().GXGetNumTexGens();
    mNumIndStages = GetGenMode().GXGetNumIndStages();
    mCullMode = GetGenMode().GXGetCullMode();

    ChanCtrl ctrl;
    for (int i = 0; i < 4; i++) {
        if (GetChanCtrl(i, &ctrl)) {
            if (mpChanCtrls[i] == NULL) {
                mpChanCtrls[i] = new ChanCtrl;
            }
            *mpChanCtrls[i] = ctrl;
        }
    }

    for (int i = 0; i < 2; i++) {
        ChanColor color;
        if (GetChanColor(i, &color)) {
            if (mpChanColors[i] == NULL) {
                mpChanColors[i] = new ChanColor;
            }
            *mpChanColors[i] = color;
        }
    }

    GetPixMode(&mPixMode);
    mpTevStageBuf = new u8[mNumTevStages];
    ReadTevColors();
    ReadTevSwap();
    ReadEffectMtx();

    for (int i = 0; i < 8; i++) {
        nw4r::g3d::ResTexSrt texSrt = GetTexSrt();
        u32 mode;
        int camRef;
        int lightRef;
        if (texSrt.GetMapMode((u8)i, &mode, &camRef, &lightRef) && camRef == 0) {
            camRef = -1;
            texSrt.SetMapMode((u8)i, mode, -1, lightRef);
        }
    }
}

inline void RPGrpModelMaterial::SyncGenMode() {
    nw4r::g3d::ResGenMode genMode = GetGenMode();
    genMode.GXSetNumChans(mNumChans);
    genMode.GXSetNumTexGens(mNumTexGens);
    genMode.GXSetNumTevStages(mNumTevStages);
    genMode.GXSetNumIndStages(mNumIndStages);
    genMode.GXSetCullMode((unk6 & 1) ? static_cast<GXCullMode>(mCullMode) : GX_CULL_ALL);

    if (unk6 & 1) {
        unk6 |= 2;
    } else {
        unk6 &= ~2;
    }
}

// SyncChanCtrl takes one channel; Calc runs the loop, so i and pCtrl are Calc's own locals
// (numbered below the loop walker, coloured after it as in the original) while the ResMatChan
// temporary stays inside the inline (its stack slot follows SyncGenMode's).
inline void RPGrpModelMaterial::SyncChanCtrl(u8 chan, const ChanCtrl& rCtrl) {
    GetMatChan().GXSetChanCtrl(static_cast<GXChannelID>(chan), rCtrl.enable,
                               static_cast<GXColorSrc>(rCtrl.ambSrc),
                               static_cast<GXColorSrc>(rCtrl.matSrc),
                               static_cast<GXLightID>(rCtrl.lightMask),
                               static_cast<GXDiffuseFn>(rCtrl.diffuseFn),
                               static_cast<GXAttnFn>(rCtrl.attnFn));
    EndEditMatChan();
}

// NONMATCHING-DEBT: SyncChanColor walks `this` by 4 bytes to reach mpChanColors[i]: the original
// keeps a walker starting at `this` (loads at +0x7C) coloured before pColor, id0, id1 and i
// (r30, r29, r28, r27, r26). sim.py replay (2026-10-29): the optimizer's walker for
// mpChanColors[i] is numbered below every inline temporary, so in this inline it is coloured last;
// with the loop written in Calc (pColor, id0, id1 declared in that order) the registers match, but
// the GXColor/ResMatChan temporaries become Calc-level and take the top stack slots (0x40-0x44,
// 36 words). A per-channel helper cannot keep the ids as Calc locals because they are loaded after
// GetMatChan() inside the call arguments. ChanColor** walk: 2 words (addi walker, this, 0x7C).
inline void RPGrpModelMaterial::SyncChanColor() {
    int i;
    u8 id1;
    u8 id0;
    ChanColor* pColor;
    RPGrpModelMaterial* pObj = this;
    for (i = 0; i < 2; i++) {
        pColor = *(&pObj->mpChanColors[0]);
        if (pColor != NULL) {
            GetMatChan().GXSetChanMatColor(static_cast<GXChannelID>(id0 = lbl_8043E9BC[(u8)i * 2 + 0]),
                                           pColor->mat);
            GetMatChan().GXSetChanMatColor(static_cast<GXChannelID>(id1 = lbl_8043E9BC[(u8)i * 2 + 1]),
                                           pColor->mat);
            GetMatChan().GXSetChanAmbColor(static_cast<GXChannelID>(id0),
                                           pColor->amb);
            GetMatChan().GXSetChanAmbColor(static_cast<GXChannelID>(id1),
                                           pColor->amb);
            EndEditMatChan();
        }
        pObj = reinterpret_cast<RPGrpModelMaterial*>(reinterpret_cast<char*>(pObj) + 4);
    }
}

inline void RPGrpModelMaterial::SyncPixMode() {
    nw4r::g3d::ResMatPix pix = GetMatPix();
    pix.GXSetAlphaCompare(static_cast<GXCompare>(mPixMode.comp0), mPixMode.ref0,
                          static_cast<GXAlphaOp>(mPixMode.alphaOp),
                          static_cast<GXCompare>(mPixMode.comp1), mPixMode.ref1);
    pix.GXSetZMode(mPixMode.zTest, static_cast<GXCompare>(mPixMode.zComp), mPixMode.zUpdate);
    pix.GXSetBlendMode(static_cast<GXBlendMode>(mPixMode.blendMode),
                       static_cast<GXBlendFactor>(mPixMode.srcFactor),
                       static_cast<GXBlendFactor>(mPixMode.dstFactor),
                       static_cast<GXLogicOp>(mPixMode.logicOp));
    pix.GXSetDstAlpha(mPixMode.dstAlphaTest, mPixMode.dstAlpha);
    pix.DCStore(false);
}

inline void RPGrpModelMaterial::SyncTevColor() {
    nw4r::g3d::ResMatTevColor tevColor = GetMatTevColor();
    for (int i = 0; i < 4; i++) {
        tevColor.GXSetTevKColor(static_cast<GXTevKColorID>(i), mTevKColors[i]);
    }
    for (int i = 0; i < 3; i++) {
        tevColor.GXSetTevColorS10(static_cast<GXTevRegID>(i + 1), mTevColors[i]);
    }
    tevColor.DCStore(false);
}

inline void RPGrpModelMaterial::SyncTevSwap() {
    for (int i = 0; i < 4; i++) {
        GetTev().GXSetTevSwapModeTable(static_cast<GXTevSwapSel>(i),
                                       static_cast<GXTevColorChan>(mSwapTable[i][0]),
                                       static_cast<GXTevColorChan>(mSwapTable[i][1]),
                                       static_cast<GXTevColorChan>(mSwapTable[i][2]),
                                       static_cast<GXTevColorChan>(mSwapTable[i][3]));
    }
    GetTev().DCStore(false);
}

inline void RPGrpModelMaterial::SyncEffectMtx() {
    nw4r::g3d::ResTexSrt texSrt = GetTexSrt();
    for (int i = 0; i < 8; i++) {
        if (mpEffectMtx[i] != NULL) {
            texSrt.SetEffectMtx((u8)i, mpEffectMtx[i]);
        }
    }
}

// SyncChanColor (inlined here): the id bytes are named locals assigned at their first use and
// declared after i (i, id1, id0), so i is coloured after them (mwcc_trace: inline locals are
// numbered in reverse declaration order).
void RPGrpModelMaterial::Calc() {
    bool cullSynced = (unk6 & 1) == ((unk6 >> 1) & 1);
    if ((mSyncFlags & ESyncFlag_ResGenMode) || !cullSynced) {
        SyncGenMode();
    }
    if (mSyncFlags & ESyncFlag_ChanCtrl) {
        ChanCtrl* pCtrl;
        for (int i = 0; i < 4; i++) {
            pCtrl = mpChanCtrls[i];
            if (pCtrl != NULL) {
                SyncChanCtrl((u8)i, *pCtrl);
            }
        }
    }
    if (mSyncFlags & ESyncFlag_ChanColor) {
        SyncChanColor();
    }
    if (mSyncFlags & ESyncFlag_ResMatPix) {
        SyncPixMode();
    }
    if (mSyncFlags & ESyncFlag_ResMatTev) {
        SyncTevColor();
    }
    if (mSyncFlags & ESyncFlag_TevSwap) {
        SyncTevSwap();
    }
    if (mSyncFlags & ESyncFlag_EffectMtx) {
        SyncEffectMtx();
    }

    mSyncFlags = 0;
    unk4 = 0;
}

void RPGrpModelMaterial::GetPixMode(PixMode* pMode) {
    nw4r::g3d::ResMatPix pix = GetMatPix();

    GXCompare comp0;
    u8 ref0;
    GXCompare comp1;
    GXAlphaOp alphaOp;
    u8 ref1;
    if (pix.GXGetAlphaCompare(&comp0, &ref0, &alphaOp, &comp1, &ref1)) {
        pMode->comp0 = comp0;
        pMode->comp1 = comp1;
        pMode->ref0 = ref0;
        pMode->ref1 = ref1;
        pMode->alphaOp = alphaOp;
    }

    GXBool zTest;
    GXCompare zComp;
    GXBool zUpdate;
    if (pix.GXGetZMode(&zTest, &zComp, &zUpdate)) {
        pMode->zTest = zTest;
        pMode->zUpdate = zUpdate;
        pMode->zComp = zComp;
    }

    GXBlendMode blendMode;
    GXBlendFactor srcFactor;
    GXBlendFactor dstFactor;
    GXLogicOp logicOp;
    if (pix.GXGetBlendMode(&blendMode, &srcFactor, &dstFactor, &logicOp)) {
        pMode->blendMode = blendMode;
        pMode->srcFactor = srcFactor;
        pMode->dstFactor = dstFactor;
        pMode->logicOp = logicOp;
    }

    GXBool dstAlphaTest;
    u8 dstAlpha;
    if (pix.GXGetDstAlpha(&dstAlphaTest, &dstAlpha)) {
        pMode->dstAlphaTest = dstAlphaTest;
        pMode->dstAlpha = dstAlpha;
    }
}

bool RPGrpModelMaterial::GetChanColor(u8 idx, ChanColor* pColor) {
    const u8* pChan = &lbl_8043E9B8[idx * 2];
    bool found = false;

    for (int i = 0; i < 2; i++, pChan++) {
        GXColor mat;
        GXColor amb;
        bool hasMat = GetMatChan().GXGetChanMatColor(static_cast<GXChannelID>(*pChan), &mat);
        bool hasAmb = GetMatChan().GXGetChanAmbColor(static_cast<GXChannelID>(*pChan), &amb);

        if (hasMat || hasAmb) {
            found = true;

            if (i == 0) {
                if (hasMat) {
                    pColor->mat.r = mat.r;
                    pColor->mat.g = mat.g;
                    pColor->mat.b = mat.b;
                } else {
                    pColor->mat.r = pColor->mat.g = pColor->mat.b = 255;
                }

                if (hasAmb) {
                    pColor->amb.r = amb.r;
                    pColor->amb.g = amb.g;
                    pColor->amb.b = amb.b;
                } else {
                    pColor->amb.r = pColor->amb.g = pColor->amb.b = 255;
                }
            } else {
                if (hasMat) {
                    pColor->mat.a = mat.a;
                } else {
                    pColor->mat.a = 255;
                }

                if (hasAmb) {
                    pColor->amb.a = amb.a;
                } else {
                    pColor->amb.a = 255;
                }
            }
        }
    }

    return found;
}
