#ifndef EGG_GFXE_LIGHT_MANAGER_H
#define EGG_GFXE_LIGHT_MANAGER_H
#include <egg/types_egg.h>

#include <egg/core/eggIBinary.h>
#include <egg/prim.h>
#include <egg/gfxe/eggLightObject.h>

#include <nw4r/g3d.h>

namespace EGG {

// Wii Play: public, game code calls SetBinary (RPPnpGame::Load)
class LightManager : public IBinary<LightManager> {
public:
    struct AmbientObject {
        GXColor color;         // at 0x0
        const char* pName;     // at 0x4
    };

    struct BinData {
        be_u16 lightNum;        // at 0x10
        be_u16 ambientNum;      // at 0x12
        GXColor lightTexColor;  // at 0x14
        u8 unk18[0x28 - 0x18];  // at 0x18
        IBinary<LightObject>::Bin lights[1]; // at 0x28
    };

public:
    LightManager(u32 lightNum, u32 ambientNum, u8 viewNum);
    // Wii Play: destructor inline (the derived RPGrpLightManager destructor calls no base destructor)
    virtual ~LightManager() {}                        // at 0x1C
    virtual void SetBinaryInner(const IBinary::Bin&); // at 0x8
    virtual void GetBinaryInner(IBinary::Bin*) const; // at 0xC
    virtual const char* GetBinaryType() const;        // at 0x10
    // Wii Play: inline, emitted after the destructor
    virtual u32 GetBinarySize() const {               // at 0x14
        return sizeof(Bin) +
               (m_binLightNum - 1) * sizeof(IBinary<LightObject>::Bin) +
               m_binAmbientNum * sizeof(AmbientObject);
    }
    virtual u8 GetVersion() const { // at 0x18
        return 2;
    }

    void Reset();
    void SetUseNum(u16 lightNum, u16 ambientNum);
    void Calc(nw4r::g3d::ScnRoot*);
    void CalcView(const nw4r::math::MTX34&, u8, nw4r::g3d::ScnRoot*);

    void LoadAmbientColorGX() const;
    // Wii Play: RPGrpModelScene::PreConditionForRenderingGX
    void LoadObjIndxGX() const;
    void DoneDraw();

    void LoadScnLightInner(nw4r::g3d::ResAnmScn, f32, u32);

    void CopyToG3D_World(nw4r::g3d::ScnRoot*) const;
    void CopyToG3D_View(nw4r::g3d::ScnRoot*, const nw4r::math::MTX34&) const;

    // Wii Play: separate accessor at line 157 (GetBinaryInner)
    const LightObject* GetLightObjConst(u16 index) const {
#line 157
        EGG_ASSERT(index < GetNum());
        return m_ppLightSet[index];
    }

    // Wii Play: the assertion is at line 166 in this EGG version
    LightObject* GetLightObj(u16 index) const {
#line 166
        EGG_ASSERT(index < GetNum());
        return m_ppLightSet[index];
    }

    GXLightObj* GetLightObjSet(u8 viewNo) const {
        return &m_pObjSet[viewNo * 8];
    }

    u16 GetNum() const {
        return m_numLight;
    }
    // Wii Play: ambient colours (RPHkyGame::Create)
    GXColor& GetAmbientColor(int idx) {
#line 174
        EGG_ASSERT(0 <= idx && idx < GetAmbientNum());
        return m_pAmbientSet[idx].color;
    }
    u16 GetAmbientNum() const {
        return m_numAmbient;
    }
    u8 GetCurrentView() const {
        return m_currentView;
    }
    LightTextureManager* GetLightTextureManager() const {
        return m_pLightTextureManager;
    }
    // Wii Play: LightTexture::draw
    const GXColor& GetLightTexColor() const {
        return m_lightTexColor;
    }

private:
    enum {
        cFlag_Calc = 1 << 2,
        cFlag_CalcView = 1 << 3,
        cFlag_ScnLight = 1 << 4,
    };

    u16 m_numLight;                // at 0x4
    u16 m_numAmbient;              // at 0x6
    u16 m_binLightNum;             // at 0x8
    u16 m_binAmbientNum;           // at 0xA
    LightObject** m_ppLightSet;    // at 0xC
    AmbientObject* m_pAmbientSet;  // at 0x10
    GXColor m_lightTexColor;       // at 0x14
    u8 m_viewNum;                  // at 0x18
    u8 m_currentView;              // at 0x19
    u16 m_flags;                   // at 0x1A
    GXLightObj* m_pObjSet;         // at 0x1C
    LightTextureManager* m_pLightTextureManager; // at 0x20
    nw4r::g3d::ResAnmScn m_resAnmScn; // at 0x24
    s16 m_lightSetRef;             // at 0x28
};

} // namespace EGG

#endif
