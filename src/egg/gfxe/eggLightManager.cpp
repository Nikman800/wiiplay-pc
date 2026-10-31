#include <egg/core.h>
#include <egg/gfxe.h>

#include <nw4r/g3d.h>
#include <nw4r/math.h>

#include <revolution/GX.h>
#include <revolution/OS.h>

namespace EGG {

template <> const char* IBinary<LightManager>::spExtension = "blight";

namespace {

const GXLightID scLightID[] = {GX_LIGHT0, GX_LIGHT1, GX_LIGHT2, GX_LIGHT3,
                               GX_LIGHT4, GX_LIGHT5, GX_LIGHT6, GX_LIGHT7};

#pragma pack(push, 1)
struct BinAmbient {
    GXColor color; // at 0x0
    u8 unk4[4];    // at 0x4
};

// Version 0
struct BinData0 {
    be_u16 lightNum;     // at 0x10
    u8 unk12[2];         // at 0x12
    GXColor ambient[2];  // at 0x14
    u8 unk1C[0x30 - 0x1C];
    IBinary<LightObject>::Bin lights[1]; // at 0x30
};

// Version 1
struct BinData1 {
    be_u16 lightNum;     // at 0x10
    u8 unk12[2];         // at 0x12
    GXColor ambient[8];  // at 0x14
    GXColor lightTexColor; // at 0x34
    u8 unk38[0x48 - 0x38];
    IBinary<LightObject>::Bin lights[1]; // at 0x48
};
#pragma pack(pop)

} // namespace

LightManager::LightManager(u32 lightNum, u32 ambientNum, u8 viewNum)
    : m_numLight(lightNum),
      m_numAmbient(ambientNum),
      m_binLightNum(lightNum),
      m_binAmbientNum(ambientNum),
      m_ppLightSet(NULL),
      m_pAmbientSet(NULL),
      m_viewNum(viewNum),
      m_flags(0),
      m_pLightTextureManager(NULL),
      m_resAnmScn(NULL),
      m_lightSetRef(-1) {

#line 43
    EGG_ASSERT(lightNum > 0);
    EGG_ASSERT(ambientNum > 0);

    m_pLightTextureManager = new LightTextureManager(this);
#line 47
    EGG_ASSERT(m_pLightTextureManager != NULL);

    m_pObjSet = new GXLightObj[m_viewNum * 8];
#line 50
    EGG_ASSERT(m_pObjSet != NULL);

    m_ppLightSet = new LightObject*[m_numLight];
#line 53
    EGG_ASSERT(m_ppLightSet != NULL);

    m_pAmbientSet = new AmbientObject[m_numAmbient];
#line 56
    EGG_ASSERT(m_pAmbientSet != NULL);

    for (u16 i_light = 0; i_light < GetNum(); i_light++) {
        m_ppLightSet[i_light] = new LightObject();
#line 61
        EGG_ASSERT(m_ppLightSet[i_light] != NULL);
        GetLightObj(i_light)->SetIndex(i_light);
    }

    Reset();
}

void LightManager::Reset() {
    for (u16 i = 0; i < GetNum(); i++) {
        LightObject* pLight = GetLightObj(i);
        pLight->Reset();
        if (i != 0) {
            pLight->Disable();
        }
    }

    for (u16 i = 0; i < GetAmbientNum(); i++) {
        GXColor color = {100, 100, 100, 255};
        m_pAmbientSet[i].color = color;
        m_pAmbientSet[i].pName = NULL;
    }

    m_lightTexColor = DrawGX::BLACK;
    m_currentView = 0;
    m_flags = 0;
}

// Wii Play: not referenced (dead-stripped); its assertion strings stay in the pool
void LightManager::SetUseNum(u16 lightNum, u16 ambientNum) {
    EGG_ASSERT(lightNum <= GetNum());
    EGG_ASSERT(ambientNum <= GetAmbientNum());

    m_binLightNum = lightNum;
    m_binAmbientNum = ambientNum;
}

void LightManager::Calc(nw4r::g3d::ScnRoot* pScnRoot) {
    if (!(m_flags & cFlag_Calc)) {
        for (int i = 0; i < GetNum(); i++) {
            GetLightObj(i)->Calc();
        }

        m_flags |= cFlag_Calc;
        m_pLightTextureManager->frameReset();
    }

    if (pScnRoot != NULL) {
        CopyToG3D_World(pScnRoot);
    }
}

void LightManager::CalcView(const nw4r::math::MTX34& rCamMtx, u8 viewNo,
                            nw4r::g3d::ScnRoot* pScnRoot) {
#line 169
    EGG_ASSERT(viewNo < m_viewNum);

    m_currentView = viewNo;
    m_flags |= cFlag_CalcView;

    GXLightObj* pObj = GetLightObjSet(viewNo);

    for (int i = 0; i < GetNum(); i++) {
        GetLightObj(i)->CalcView(rCamMtx);
        if (i < 8) {
            GetLightObj(i)->InitGX(&pObj[i]);
        }
    }

    DCFlushRange(pObj, sizeof(GXLightObj) * 8);

    if (pScnRoot != NULL) {
        nw4r::math::MTX34 invMtx;
        PSMTXInverse(rCamMtx, invMtx);
        CopyToG3D_View(pScnRoot, invMtx);
    }
}

void LightManager::LoadAmbientColorGX() const {
    GXSetChanAmbColor(GX_COLOR0A0, m_pAmbientSet[0].color);
    GXSetChanAmbColor(GX_COLOR1A1, DrawGX::BLACK);
}

void LightManager::LoadObjIndxGX() const {
    GXSetArray(GX_LIGHT_ARRAY, PORT_GP_LIGHT_OBJS(GetLightObjSet(m_currentView), 8),
               sizeof(GXLightObj));

    for (int i = 0; i < 8; i++) {
        GXLoadLightObjIndx(i, scLightID[i]);
    }
}

void LightManager::DoneDraw() {
    if ((m_flags & cFlag_CalcView) && (m_flags & cFlag_Calc)) {
        m_flags &= ~cFlag_ScnLight;
    }

    m_flags &= ~(cFlag_Calc | cFlag_CalcView);
}

void LightManager::LoadScnLightInner(nw4r::g3d::ResAnmScn scn, f32 frame,
                                     u32 refNumber) {
    if (!scn.IsValid()) {
        return;
    }

    m_resAnmScn = scn;
    m_lightSetRef = refNumber;
    m_flags |= cFlag_ScnLight;

    u32 ambNum;
    nw4r::g3d::ResLightSet lightSet(NULL);
    if (static_cast<int>(refNumber) != -1) {
        lightSet = scn.GetResLightSetByRefNumber(refNumber);
    }

    int ambId = -1;
    if (lightSet.IsValid()) {
        ambId = lightSet.GetAmbLightID();
    }

    ambNum = GetAmbientNum();
    if (scn.GetResAnmAmbLightMaxRefNumber() < ambNum) {
        ambNum = scn.GetResAnmAmbLightMaxRefNumber();
    }

    for (u32 i = 0; i < ambNum; i++) {
        if (ambId != -1 && i != ambId) {
            continue;
        }

        nw4r::g3d::AmbLightAnmResult result;
        nw4r::g3d::ResAnmAmbLight amb =
            m_resAnmScn.GetResAnmAmbLightByRefNumber(i);

        if (!amb.IsValid()) {
            result.flags = 0;
            result.color = 0xFFFFFFFF;
        } else {
            amb.GetAnmResult(&result, frame);
            const nw4r::g3d::ResAnmAmbLightData& r = amb.ref();
            m_pAmbientSet[i].pName =
                r.name != 0 ? reinterpret_cast<const char*>(&r) + r.name
                            : NULL;
        }

        if (!(result.flags & nw4r::g3d::AmbLightAnmResult::FLAG_COLOR_ENABLE)) {
            result.color |= 0xFFFFFF00;
        }
        if (!(result.flags & nw4r::g3d::AmbLightAnmResult::FLAG_ALPHA_ENABLE)) {
            result.color |= 0x000000FF;
        }

        GXColor& rDst = m_pAmbientSet[i].color;
        u32 color = result.color;
        rDst.r = color >> 24;
        rDst.g = color >> 16;
        rDst.b = color >> 8;
        rDst.a = color;
    }

    u32 lightNum = GetNum();
    if (scn.GetResAnmLightMaxRefNumber() <= lightNum) {
        lightNum = scn.GetResAnmLightMaxRefNumber();
    }
    if (lightSet.IsValid()) {
        lightNum = lightSet.GetNumLight();
    }

    // Light IDs are read at a signed byte offset (i * 2) into lightId; an
    // array index (GetLightID) strength-reduces differently.
    for (int i = 0; i < lightNum; i++) {
        m_ppLightSet[lightSet.IsValid()
                         ? *reinterpret_cast<const u16*>(
                               reinterpret_cast<const u8*>(lightSet.ref().lightId) + i * 2)
                         : i]
            ->Disable();
    }

    for (int i = 0; i < lightNum; i++) {
        nw4r::g3d::ResAnmLight light = m_resAnmScn.GetResAnmLightByRefNumber(i);
        if (light.IsValid()) {
            LightObject* pLight = m_ppLightSet[
                lightSet.IsValid()
                    ? *reinterpret_cast<const u16*>(
                          reinterpret_cast<const u8*>(lightSet.ref().lightId) + i * 2)
                    : i];
            if (!pLight->IsEnable()) {
                pLight->CopyFromG3D(light, frame);
            }
        }
    }
}

void LightManager::CopyToG3D_World(nw4r::g3d::ScnRoot* pScnRoot) const {
#line 424
    EGG_ASSERT(pScnRoot != NULL);

    nw4r::g3d::LightSetting& l = pScnRoot->GetLightSetting();
#line 428
    EGG_ASSERT(l.GetNumLightObj() >= GetNum());
    EGG_ASSERT(l.GetNumLightSet() >= GetAmbientNum());

    if (m_flags & cFlag_ScnLight) {
        bool is_success =
            G3DUtility::setUpLightSet(l, m_resAnmScn, m_lightSetRef);
#line 435
        EGG_ASSERT(is_success);
    }

    for (int i = 0; i < GetAmbientNum(); i++) {
        reinterpret_cast<nw4r::ut::Color*>(l.GetAmbLightObjArray())[i] =
            m_pAmbientSet[i].color;
    }

    for (int i = 0; i < GetNum(); i++) {
        m_ppLightSet[i]->CopyToG3D_World(l.GetLightObjArray()[i]);
    }
}

void LightManager::CopyToG3D_View(nw4r::g3d::ScnRoot* pScnRoot,
                                  const nw4r::math::MTX34& rViewMtx) const {
#line 459
    EGG_ASSERT(pScnRoot != NULL);

    nw4r::g3d::LightSetting& l = pScnRoot->GetLightSetting();
    for (int i = 0; i < GetNum(); i++) {
        m_ppLightSet[i]->CopyToG3D_View(l.GetLightObjArray()[i], rViewMtx);
    }
}

const char* LightManager::GetBinaryType() const {
    return "LGHT";
}

void LightManager::SetBinaryInner(const Bin& rBin) {
    switch (rBin.header.version) {
    case 0: {
        const BinData0& r = reinterpret_cast<const BinData0&>(rBin.data);

        u16 lightNum = r.lightNum;
        if (lightNum >= m_numLight) {
            lightNum = m_numLight;
        }

        m_pAmbientSet[0].color = r.ambient[0];
        m_pAmbientSet[1].color = r.ambient[1];
        m_lightTexColor = DrawGX::BLACK;

        const IBinary<LightObject>::Bin* pLights = r.lights;
        u32 n = lightNum;
        for (u16 i = 0; i < n; i++) {
            GetLightObj(i)->SetBinary(&pLights[i]);
        }
        break;
    }
    case 1: {
        u32 lightNumTmp;
        u16 ambNum;
        const BinData1* p;
        const IBinary<LightObject>::Bin* pLights;
        u16 i;
        u16 lightNum;

        p = reinterpret_cast<const BinData1*>(&rBin.data);
        lightNumTmp = p->lightNum >= m_numLight ? m_numLight : p->lightNum;
        lightNum = lightNumTmp;
        ambNum = m_numAmbient > 8 ? 8 : m_numAmbient;
        for (int j = 0; j < ambNum; j++) {
            m_pAmbientSet[j].color = p->ambient[j];
        }

        m_lightTexColor = p->lightTexColor;

        pLights = p->lights;
        for (i = 0; i < lightNum; i++) {
            GetLightObj(i)->SetBinary(&pLights[i]);
        }
        break;
    }
    case 2: {
        u32 lightNumTmp;
        u32 ambNumTmp;
        const IBinary<LightObject>::Bin* pLights;
        u16 ambNum;
        u16 lightNum;
        const BinAmbient* pAmb;

        lightNumTmp = rBin.data.lightNum;
        if (lightNumTmp >= m_binLightNum) {
            lightNumTmp = m_binLightNum;
        }
        lightNum = lightNumTmp;

        ambNumTmp = rBin.data.ambientNum;
        if (ambNumTmp >= m_binAmbientNum) {
            ambNumTmp = m_binAmbientNum;
        }
        ambNum = ambNumTmp;

        m_lightTexColor = rBin.data.lightTexColor;

        pLights = rBin.data.lights;
        for (u16 i = 0; i < lightNum; i++) {
            GetLightObj(i)->SetBinary(&pLights[i]);
        }

        pAmb = reinterpret_cast<const BinAmbient*>(&pLights[rBin.data.lightNum]);
        for (u16 i = 0; i < ambNum; i++) {
            m_pAmbientSet[i].color = pAmb[i].color;
        }
        break;
    }
    }
}

void LightManager::GetBinaryInner(Bin* pBin) const {
    u16 lightNum = m_binLightNum;
    u16 ambNum = m_binAmbientNum;
    pBin->data.lightNum = lightNum;
    pBin->data.ambientNum = ambNum;

    IBinary<LightObject>::Bin* pLights = pBin->data.lights;
    for (u16 i = 0; i < lightNum; i++) {
        GetLightObjConst(i)->GetBinary(&pLights[i]);
    }

    BinAmbient* pAmb = reinterpret_cast<BinAmbient*>(&pLights[lightNum]);
    for (u16 i = 0; i < ambNum; i++) {
        pAmb[i].color = m_pAmbientSet[i].color;
        pAmb[i].unk4[3] = 0;
        pAmb[i].unk4[2] = 0;
        pAmb[i].unk4[1] = 0;
        pAmb[i].unk4[0] = 0;
    }

    pBin->data.lightTexColor = m_lightTexColor;
}

} // namespace EGG
