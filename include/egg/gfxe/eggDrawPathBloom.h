#ifndef EGG_GFXE_DRAW_PATH_BLOOM_H
#define EGG_GFXE_DRAW_PATH_BLOOM_H
#include <egg/types_egg.h>

#include <egg/core.h>
#include <egg/gfxe/eggDrawPathBase.h>

#include <revolution/GX.h>


namespace EGG {

// Forward declarations
class PostEffectBlur;
class PostEffectBlurGather;
class PostEffectBlurSimple;
class PostEffectMask;
class PostEffectSimple;
class TextureBuffer;

class DrawPathBloom : public DrawPathBase, public IBinary<DrawPathBloom> {
    friend class ScnRenderer;

public:
    struct BlurParam {
        be_f32 mRadius;    // at 0x0
        be_f32 mIntensity; // at 0x4
        u8 unk8[0x20 - 0x8];
    };

    struct BinData {
        be_f32 mThreshold;       // at 0x0
        GXColor mMaskColor;      // at 0x4
        GXColor mColor;          // at 0x8
        be_u16 mFlags;           // at 0xC
        be_u16 unkE;             // at 0xE
        BlurParam mBlur0;        // at 0x10
        BlurParam mBlur1;        // at 0x30
        BlurParam mBlur2;        // at 0x50
        u8 mBlendModeIdx;        // at 0x70
        u8 mNumBlur;             // at 0x71
        u8 unk72[0x8C - 0x72];   // at 0x72
        be_f32 mGatherScale0;    // at 0x8C
        be_f32 mGatherScale1;    // at 0x90
    };

public:
    DrawPathBloom();
    virtual ~DrawPathBloom();       // at 0x8
    virtual u16 getNumStep() const { // at 0x1C
        return cStep_Max;
    }
    virtual void internalCalc();    // at 0x24
    virtual void internalDraw(u16); // at 0x28

    virtual void SetBinaryInner(const Bin&);   // at 0x8
    virtual void GetBinaryInner(Bin*) const;   // at 0xC
    virtual const char* GetBinaryType() const; // at 0x10
    virtual u8 GetVersion() const { // at 0x18
        return 1;
    }

    void setFlag8() {
        mFlags |= 0x8;
    }
    void clearFlag8() {
        mFlags &= ~0x8;
    }
    bool isFlag8() const {
        return mFlags & 0x8;
    }

private:
    enum Step {
        cStep_0,
        cStep_1,
        cStep_2,

        cStep_Max
    };

private:
    u8 mFlags;                         // at 0x80
    PostEffectMask* mpMask;            // at 0x84
    PostEffectBlur* mpBlur1;           // at 0x88
    PostEffectBlur* mpBlur2;           // at 0x8C
    PostEffectBlurSimple* mpBlurSimple; // at 0x90
    PostEffectBlurGather* mpBlurGather; // at 0x94
    PostEffectSimple* mpSimple;        // at 0x98
    TextureBuffer* mpTexBuffer;        // at 0x9C
    BinData mData;                     // at 0xA0
};

} // namespace EGG

#endif
