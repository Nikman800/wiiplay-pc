#ifndef EGG_GFXE_LIGHT_OBJECT_H
#define EGG_GFXE_LIGHT_OBJECT_H
#include <egg/types_egg.h>

#include <egg/core.h>
#include <egg/math.h>

#include <nw4r/g3d.h>

#include <revolution/GX.h>

#include <port/bemath.h>

namespace EGG {

class LightObject : public IBinary<LightObject> {
public:
    struct BinData {
        u8 spotFn;     // at 0x10
        u8 distAttnFn; // at 0x11
        u8 BYTE_0x12;
        u8 BYTE_0x13;
        be_u16 ambLightIdx;       // at 0x14
        be_u16 flags;             // at 0x16
        BE_MATH(_VEC3) pos;       // at 0x18
        BE_MATH(_VEC3) target;    // at 0x24
        be_f32 intensity;         // at 0x30
        GXColor color;            // at 0x34
        GXColor COLOR_0x38;
        be_f32 spotArg;      // at 0x3C
        be_f32 distAttnArg0; // at 0x40
        be_f32 distAttnArg1; // at 0x44
        char UNK_0x48[0x8];
    };

public:
    LightObject();

    virtual void SetBinaryInner(const IBinary::Bin&); // at 0x8
    virtual void GetBinaryInner(IBinary::Bin*) const; // at 0xC

    virtual const char* GetBinaryType() const; // 0x10

    virtual u8 GetVersion() const { // 0x18
        return VERSION;
    }

    void Reset();
    void Calc();
    void CalcView(const nw4r::math::MTX34& rCameraMtx);

    void CalcFinalDirColor(const EGG::LightTexture& rTexture,
                           nw4r::math::VEC3* pDir, GXColor* pColor) const;

    // Wii Play: LightTexture::addLight
    u16 GetIndex() const {
        return mAmbIndex;
    }
    bool IsEnable() const {
        return mFlags & cFlag_0;
    }
    bool IsFlag5() const {
        return mFlags & cFlag_5;
    }
    f32 GetIntensity() const {
        return mIntensity;
    }
    f32 GetDistance() const {
        return mDistance;
    }
    const GXColor& GetDarknessColor() const {
        return mDarknessColor;
    }

    void InitGX(GXLightObj* pLightObj) const;
    void CopyToG3D_World(nw4r::g3d::LightObj& rLightObj) const;
    void CopyToG3D_View(nw4r::g3d::LightObj& rLightObj,
                        const nw4r::math::MTX34& rViewMtx) const;
    void CopyFromG3D(const nw4r::g3d::ResAnmLight& rAnmLight, f32 frame);

    // Wii Play: LightManager
    void SetIndex(u16 index) {
        mAmbIndex = index;
    }
    void Disable() {
        mFlags &= ~cFlag_0;
    }
    // Wii Play: RPBilScene::Create
    void Enable() {
        mFlags |= cFlag_0;
    }

    void CalcDirDist();
    // Wii Play: used by RPHkyGame::Create
    void SetPos(const nw4r::math::VEC3& rPos) {
        mPos.x = rPos.x;
        mPos.y = rPos.y;
        mPos.z = rPos.z;
    }

    // Wii Play: RPBilCamera
    const nw4r::math::VEC3& GetAt() const {
        return mAt;
    }

    void SetAt(const nw4r::math::VEC3& rAt) {
        mAt.x = rAt.x;
        mAt.y = rAt.y;
        mAt.z = rAt.z;
    }

    void SetPosAt(const nw4r::math::VEC3& rPos, const nw4r::math::VEC3& rAt) {
        SetPos(rPos);
        SetAt(rAt);
        CalcDirDist();
    }

    void CalcAt();

    void SetDir(const nw4r::math::VEC3& dir) {
#line 559
        EGG_ASSERT(VEC3Len( &dir ) > 0.f);
        mDir.x = dir.x;
        mDir.y = dir.y;
        mDir.z = dir.z;
        CalcAt();
    }

private:
    enum {
        cFlag_0 = 1 << 0,
        cFlag_5 = 1 << 5,
        cFlag_6 = 1 << 6,
        cFlag_9 = 1 << 9,
        cFlag_10 = 1 << 10,
    };

    enum LightType {
        cLightType_Pos,
        cLightType_Dir,
        cLightType_Spot,

        cLightType_Max
    };

    static const u8 VERSION = 1;

private:
    u16 mAmbIndex;            // at 0x4
    nw4r::math::VEC3 mAt;             // at 0x8
    nw4r::math::VEC3 mPos;            // at 0x14
    nw4r::math::VEC3 mDir;            // at 0x20
    GXColor mBrightnessColor; // at 0x2C
    f32 mIntensity;           // at 0x30
    u32 mViewMode;            // at 0x34
    f32 mDistance;            // at 0x38
    GXColor mDarknessColor;   // at 0x3C
    LightType mLightType;     // at 0x40
    f32 mCutoff;              // at 0x44
    f32 mRefDistance;         // at 0x48
    f32 mRefBrightness;       // at 0x4C
    f32 mAttnAA;              // at 0x50
    f32 mAttnAB;              // at 0x54
    f32 mAttnAC;              // at 0x58
    f32 mAttnKA;              // at 0x5C
    f32 mAttnKB;              // at 0x60
    f32 mAttnKC;              // at 0x64
    GXSpotFn mSpotFn;         // at 0x68
    GXDistAttnFn mDistAttnFn; // at 0x6C
    f32 mShininess;           // at 0x70
    u16 unk74;
    u16 mFlags; // at 0x76
    u32 unk78;
    nw4r::math::VEC3 mLightVector; // at 0x7C
    GXColor mLightColor;           // at 0x88
    nw4r::math::VEC3 mViewPos;     // at 0x8C
    nw4r::math::VEC3 mViewAt;      // at 0x98
    nw4r::math::VEC3 mViewDir;     // at 0xA4

    // /* 0x6C */ nw4r::math::VEC3 field_0x6C;
    // /* 0x78 */ GXColor mLightColor;
    // /* 0x7C */ nw4r::math::VEC3 mViewPos;
    // /* 0x88 */ nw4r::math::VEC3 mViewAt;
    // /* 0x94 */ nw4r::math::VEC3 field_0x94;
    // /* 0xA0 */ u16 field_0xA0;
};

} // namespace EGG

#endif
