#include <egg/gfxe/eggLightTextureManager.h>

#include <egg/gfxe.h>
#include <egg/prim.h>
#include <nw4r/g3d.h>
#include <nw4r/math.h>
#include <revolution/GX.h>

#include <cstring>

namespace EGG {

template <> const char* IBinary<LightTextureManager>::spExtension = "blmap";

LightTextureManager::LightTextureManager(const LightManager* pManager)
    : mFlags(cFlag_ClearWorkSpace | cFlag_4 | cFlag_5 | cFlag_6),
      mTexNum(0),
      mppLightTextures(NULL),
      mpLightManager(pManager),
      mDrawFlags(0),
      mLightObjectMax(LIGHT_OBJ_MAX),
      mppLightObjs(NULL) {

    EGG_ASSERT_LINE(36, pManager);

    mppLightTextures = new LightTexture*[LIGHT_TEX_MAX];
    for (int i = 0; i < LIGHT_TEX_MAX; i++) {
        mppLightTextures[i] = NULL;
    }

    mppLightObjs = new LightObject*[mLightObjectMax];
    for (int i = 0; i < mLightObjectMax; i++) {
        mppLightObjs[i] = NULL;
    }
}

u16 LightTextureManager::createTexture(const char* pName) {
    EGG_ASSERT_LINE(78, mTexNum < getMax());

    for (int i = 0; i < mTexNum; i++) {
        if (std::strcmp(pName, mppLightTextures[i]->getName()) == 0) {
            return i;
        }
    }

    mppLightTextures[mTexNum] = new LightTexture(mTexNum, pName, this);
    mppLightTextures[mTexNum]->configure();

    return mTexNum++;
}

u16 LightTextureManager::replaceModelTexture(int index, ModelEx* pMdl) const {
    EGG_ASSERT_LINE(121, pMdl);
    EGG_ASSERT_LINE(122, 0 <= index && index < mTexNum);

    GXTexObj tobj;
    mppLightTextures[index]->getTexObj(&tobj);

    TextureReplaceResult resultSet[255];
    u16 resultNum =
        pMdl->replaceTexture(mppLightTextures[index]->getName(), tobj, false,
                             resultSet, ARRAY_SIZE(resultSet), false);

    pMdl->replaceTexture(mppLightTextures[index]->getName(), tobj, false, NULL,
                         0, true);

    for (int i = 0; i < resultNum; i++) {
        u16 matIdx = resultSet[i].materialID;
        GXTexCoordID coord = static_cast<GXTexCoordID>(resultSet[i].texCoordID);

        nw4r::g3d::ResTexSrt srt = pMdl->getResMat(matIdx).GetResTexSrt();
        EGG_ASSERT_LINE(138, srt.IsValid());

        srt.SetMapMode(
            coord,                                                     //
            nw4r::g3d::G3DState::SCNDEPENDENT_TEXMTX_FUNCTYPE_SRC_NRM, //
            -1, -1);

        nw4r::g3d::ResMatTexCoordGen gen =
            pMdl->getResMat(matIdx).GetResMatTexCoordGen();
        EGG_ASSERT_LINE(144, gen.IsValid());

        GXTexGenType func;
        GXTexGenSrc param;
        GXBool normalize;
        u32 postMtx;
        gen.GXGetTexCoordGen2(coord, &func, &param, &normalize, &postMtx);

        gen.GXSetTexCoordGen2(coord, GX_TG_MTX3x4, GX_TG_NRM, GX_TRUE, postMtx);
        gen.EndEdit();
    }

    return resultNum;
}

int LightTextureManager::getTextureIndex(const char* pName) const {
    for (int i = 0; i < mTexNum; i++) {
        if (std::strcmp(pName, mppLightTextures[i]->getName()) == 0) {
            return i;
        }
    }

    return -1;
}

u16 LightTextureManager::replaceModelTexture(const char* pName,
                                             ModelEx* pMdl) const {
    int tex_index = getTextureIndex(pName);
    EGG_ASSERT(tex_index != -1);

    return replaceModelTexture(tex_index, pMdl);
}

void LightTextureManager::correctLightObject() {
    int i;
    int numLight = 0;

    for (i = 0; i < mpLightManager->GetNum(); i++) {
        const LightObject* p_obj = mpLightManager->GetLightObjConst(i);
#line 234
        EGG_ASSERT(p_obj);

        if (p_obj->IsEnable() && p_obj->IsFlag5()) {
            mppLightObjs[numLight] = const_cast<LightObject*>(p_obj);
            numLight++;

            if (numLight >= mLightObjectMax) {
                break;
            }
        }
    }

    for (; numLight < mLightObjectMax; numLight++) {
        mppLightObjs[numLight] = NULL;
    }
}

void LightTextureManager::frameReset() {
    enum {
        cFrameFlag2 = 1 << 1,
    };

    correctLightObject();
    mFlags &= ~cFrameFlag2;
    mDrawFlags = 0;
}

void LightTextureManager::drawAndCaptureTexture(
    LightManager* pManager, const Screen::DataEfb& rEfb, f32 x1, f32 y1,
    f32 x2, f32 y2) {
    bool setPixelFmt = isEnableSetPixelFormat();
    u32 view = 1 << mpLightManager->GetCurrentView();

    if ((mFlags & cFlag_4) == 0 || (mDrawFlags & view) != 0 || mTexNum == 0) {
        return;
    }

    if ((mFlags & cFlag_6) != 0) {
        x1 = 0.0f;
        y1 = StateGX::getEfbHeight();
        x2 = StateGX::getEfbWidth();
        y2 = 528 - StateGX::getEfbHeight();
    }

    mDrawFlags |= view;

    StateGX::AutoColorUpdate colorUpdate(true);
    StateGX::AutoAlphaUpdate alphaUpdate(false);

    LightTexture::initDrawSetting(x1 + (static_cast<u16>(x1) & 1),
                                  y1 + (static_cast<u16>(y1) & 1), x2, y2);

    TextureBuffer* pTextureBuffer = NULL;

    u16 drawX;
    u16 drawY;
    u16 drawWidth;
    u16 drawHeight;
    LightTexture::getDrawSetting(&drawX, &drawY, &drawWidth, &drawHeight,
                                 mTexNum);
    drawWidth += drawWidth % 4;
    drawHeight += drawHeight % 4;

    if ((mFlags & cFlag_6) == 0 && (mFlags & cFlag_2) != 0) {
        pTextureBuffer =
            TextureBuffer::alloc(drawWidth, drawHeight, GX_TF_RGBA8);
        pTextureBuffer->capture(drawX, drawY, false, -1);
    }

    if (setPixelFmt) {
        StateGX::setPixelFormatGX(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    }

    for (int i = 0; i < mTexNum; i++) {
        mppLightTextures[i]->draw();
    }

    for (int i = 0; i < mTexNum; i++) {
        mppLightTextures[i]->capture(setPixelFmt && isEnableClearWorkSpace());
    }

    if (setPixelFmt) {
        StateGX::setPixelFormatGX(StateGX::getDefaultPixelFormat(),
                                  StateGX::getDefaultZFmt16());
    }

    if ((mFlags & cFlag_6) == 0) {
        StateGX::AutoColorUpdate restoreColorUpdate(true);
        StateGX::AutoAlphaUpdate restoreAlphaUpdate(true);

        if (isEnableClearWorkSpace() || pTextureBuffer != NULL) {
            Mtx44 proj;
            C_MTXOrtho(proj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
            StateGX::GXSetProjection_(proj, GX_ORTHOGRAPHIC);
            StateGX::GXSetViewport_(drawX, drawY, drawWidth, drawHeight, 0.0f,
                                    1.0f);
            StateGX::GXSetScissor_(drawX, drawY, drawWidth, drawHeight);
            StateGX::GXSetScissorBoxOffset_(0, 0);

            nw4r::math::MTX34 drawMtx;
            PSMTXIdentity(drawMtx);

            if (pTextureBuffer != NULL) {
                DrawGX::BeginDrawScreen(true, true, false);
                DrawGX::SetBlendMode(DrawGX::BLEND_NONE);
                GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                               GX_LO_CLEAR);
                pTextureBuffer->load(GX_TEXMAP0);
                DrawGX::DrawDL(DrawGX::DL_SCREEN_TEXTURE, drawMtx,
                               DrawGX::WHITE);
                pTextureBuffer->free();
            } else {
#line 382
                EGG_ASSERT(isEnableClearWorkSpace());

                if (setPixelFmt) {
                    DrawGX::BeginDrawScreen(true, false, true);
                    DrawGX::SetBlendMode(DrawGX::BLEND_NONE);
                    DrawGX::DrawDL(DrawGX::DL_SCREEN, drawMtx,
                                   StateGX::getEfbClearColor());
                }
            }
        }
    }

    GXInvalidateTexAll();
}

const char* LightTextureManager::GetBinaryType() const {
    return "LMAP";
}

static inline void setTextureBinary(LightTexture** const& ppTextures, const u16& num,
                                        const IBinary<LightTexture>::Bin* pBin) {
    for (int index = 0; index < num; index++) {
        if (std::strcmp(pBin->data.name, ppTextures[index]->getName()) == 0) {
            ppTextures[index]->IBinary<LightTexture>::SetBinary(pBin);
            break;
        }
    }
}

void LightTextureManager::SetBinaryInner(const IBinary::Bin& rBin) {
    switch (rBin.header.version) {
    case VERSION: {
        const IBinary<LightTexture>::Bin* pTextures =
            reinterpret_cast<const IBinary<LightTexture>::Bin*>(
                &rBin.data.unk10[0x10]);

        for (int i = 0; i < *reinterpret_cast<const be_u16*>(rBin.data.unk10);
             i++) {
            setTextureBinary(mppLightTextures, mTexNum, pTextures);

            pTextures = reinterpret_cast<const IBinary<LightTexture>::Bin*>(
                reinterpret_cast<const u8*>(pTextures) +
                pTextures->header.size);
        }
        break;
    }
    }
}

void LightTextureManager::GetBinaryInner(IBinary::Bin* pBin) const {
    *reinterpret_cast<be_u16*>(pBin->data.unk10) = mTexNum;

    IBinary<LightTexture>::Bin* pTextures =
        reinterpret_cast<IBinary<LightTexture>::Bin*>(&pBin->data.unk10[0x10]);

    for (int index = 0; index < mTexNum; index++) {
        mppLightTextures[index]->IBinary<LightTexture>::GetBinary(pTextures);

        pBin->header.size += pTextures->header.size;
        pTextures = reinterpret_cast<IBinary<LightTexture>::Bin*>(
            reinterpret_cast<u8*>(pTextures) + pTextures->header.size);
    }
}

u8 LightTextureManager::GetVersion() const {
    return VERSION;
}

} // namespace EGG
