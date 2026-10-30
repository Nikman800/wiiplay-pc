#ifndef EGG_GFXE_LIGHT_TEXTURE_H
#define EGG_GFXE_LIGHT_TEXTURE_H
#include "eggCapTexture.h"
#include "eggIBinary.h"
#include "eggScreen.h"
#include "types_egg.h"

#include <nw4r/math.h>

namespace EGG {

class LightObject;

// Wii Play: full layout (eggLightTexture.cpp)
class LightTexture : public CapTexture, public IBinary<LightTexture> {
public:
    struct BinData {
        be_u16 lightNum; // at 0x10
        u8 unk12[2];    // at 0x12
        char name[32];  // at 0x14
        u8 drawType;    // at 0x34
        u8 unk35[3];    // at 0x35
        be_u32 lightMask; // at 0x38
        struct Light {
            be_f32 effectScale; // at 0x0
            u8 pttrn;        // at 0x4
            u8 unk5[3];      // at 0x5
        } lights[1];    // at 0x3C
    };

    enum EDrawStatus {
        cDrawStatus_None,
        cDrawStatus_Drawing,
        cDrawStatus_Drawn,
    };

    enum EPattern {
        cPttrn_Max = 10
    };

public:
    static void initialize(u16 textureSize, Heap* pHeap);
    static void initDrawSetting(u16 x, u16 y, u16 width, u16 height);
    static void getDrawSetting(u16* posX, u16* posY, u16* width, u16* height,
                               u16 index);

    LightTexture(u16 index, const char* name,
                 const LightTextureManager* pManager);
    virtual ~LightTexture();                           // at 0x8
    virtual void configure();                          // at 0xC

    virtual void SetBinaryInner(const Bin& rBin);      // at 0x8
    virtual void GetBinaryInner(Bin* pBin) const;      // at 0xC
    virtual u32 GetBinarySize() const;                 // at 0x14
    virtual const char* GetBinaryType() const;         // at 0x10

    void draw();
    void beginDraw(const GXColor& rColor);
    void addLight(const LightObject& rLight);
    void endDraw();
    void capture(bool clear);

    u32 getLightMask() const {
        return mLightMaskLo | mLightMaskHi << 16;
    }

    const char* getName() const {
        return mName;
    }

    void setEffectScale(u16 lightID, f32 scale) {
#line 218
        EGG_ASSERT(lightID < mLightNum);
        mpEffectScale[lightID] = scale;
    }

    f32 getEffectScale(u16 lightID) const {
#line 226
        EGG_ASSERT(lightID < mLightNum);
        return mpEffectScale[lightID];
    }

    void setPttrn(u16 lightID, int pttrn) {
#line 234
        EGG_ASSERT(lightID < mLightNum);
        mpPttrn[lightID] = pttrn;
    }

    int getPttrn(u16 lightID) const {
#line 242
        EGG_ASSERT(lightID < mLightNum);
        return mpPttrn[lightID];
    }

    static CpuTexture* getPatternTexture(int pat) {
#line 282
        EGG_ASSERT(pat < cPttrn_Max);
        return spPatternSet[pat];
    }

    static CpuTexture* getNormalEnvironment() {
        return spNormalEnvironment;
    }

private:
    struct DrawSetting {
        u8 mStatus;     // at 0x0
        u8 mPassNum;    // at 0x1
        u8 mTexMapBase; // at 0x2
        u16 mLightCnt;  // at 0x4
        u16 mPosX;      // at 0x6
        u16 mPosY;      // at 0x8
    };

    static const int MAX_NAME_LEN = 32;

    s8 mLodBias;                  // at 0x30
    u16 mLightNum;                // at 0x32
    nw4r::math::VEC3 unk34;       // at 0x34
    u16 mLightMaskLo;             // at 0x40
    u16 mLightMaskHi;             // at 0x42
    u16 mIndex;                   // at 0x44
    int mDrawType;                // at 0x48
    f32* mpEffectScale;           // at 0x4C
    int* mpPttrn;                 // at 0x50
    char mName[MAX_NAME_LEN];     // at 0x54
    const LightTextureManager* mpManager; // at 0x74
    DrawSetting mSetting;         // at 0x78

    static u16 sTextureSize;
    static CpuTexture* spPatternSet[cPttrn_Max];
    static u16 sDrawWidth;
    static u16 sDrawHeight;
    static u16 sDrawPosX;
    static u16 sDrawPosY;
    static u16 sTexWidth;
    static u16 sTexHeight;
    static u16 sDrawNumX;
    static u16 sDrawNumY;
    static CpuTexture* spNormalEnvironment;
};
} // namespace EGG

#endif
