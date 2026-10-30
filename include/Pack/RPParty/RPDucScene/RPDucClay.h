#ifndef RP_PARTY_DUC_CLAY_H
#define RP_PARTY_DUC_CLAY_H
#include <Pack/types_pack.h>

#include <port/be.h>

#include <Pack/RPParty/RPDucScene/RPDucTarget.h>

#include <nw4r/math.h>

// Preserve the clay launcher's original seed updates, precision, and FMA operand order.
inline f32 RPDucClayRandRange(f32 lo, f32 hi) {
    if (lo == hi) {
        return lo;
    }
    RP_randSeedX = RP_randSeedX * 0x41C64E6D + 12345;
    if (RP_randSeedY & 1) {
        RP_randSeedY ^= 0x11020;
    }
    RP_randSeedY >>= 1;
    f32 r = (f32)((RP_randSeedX ^ RP_randSeedY) & 0x7FFFFF);
    r = 0.0000001192093f * r;
    return __fmadds(hi - lo, r, lo);
}

//! @addtogroup rp_duc
//! @{

/**
 * @brief One launch pattern of param/DucClayParam.bin (RPDucClay::Launch, pattern & 7)
 * @details Size 0x38 [H]; copied whole by SetupLaunch [M].
 */
struct RPDucClayPattern {
    be_f32 mAimRangeX[2]; // at 0x0 random aim offset: [0] 16:9, [1] 4:3 (EGG::Screen::sTVMode 0)
    be_f32 mAimRangeY;    // at 0x8 random aim offset
    be_f32 mAimY;         // at 0xC
    be_f32 mAimZ;         // at 0x10
    be_f32 mHitScale;     // at 0x14 grows while the clay comes near
    be_f32 mThrustDecay;  // at 0x18 [M] unk2F0 factor per step (slow and fall states)
    be_f32 mThrust;       // at 0x1C [M] initial unk2F0; also the gravity of the rise state
    be_f32 mTiltRange[2]; // at 0x20 random sideways tilt of the up axis
    be_f32 mFallScaleMax; // at 0x28 [M]
    be_f32 mFallScaleAdd; // at 0x2C [M]
    be_f32 mHitRadius;    // at 0x30
    be_f32 mSpeed;        // at 0x34
};

/**
 * @brief Contents of param/DucClayParam.bin, copied into RPDucClay by Load
 * @details Size 0x1E4 [H] (121 words); copied member by member (the Load copy).
 */
struct RPDucClayParam {
    be_f32 mDrag;                  // at 0x0 velocity factor per step
    be_f32 mNearZ;                 // at 0x4 [M] hit scale reaches double here
    be_f32 mFarZ;                  // at 0x8 [M] hit scale starts growing here
    be_f32 mBonusZ;                // at 0xC farther hits score mBonusPoints
    be_s32 mBonusPoints;           // at 0x10
    be_s32 mPoints;                // at 0x14
    be_f32 mBaseRadius;            // at 0x18 hit radius of scale 1
    be_f32 mSpinDecay;             // at 0x1C [M] unk2EC factor per step
    be_f32 mSpin;                  // at 0x20 [M] initial unk2EC
    RPDucClayPattern mPatterns[8]; // at 0x24
};

/**
 * @brief Clay pigeon target
 * @details vtable 80359E30. Size 0x32C, verified. States: 0 check hit, 1 rise, 2 slow, 3 fall.
 * @note Name is a project convention (ledger Duc_Clay).
 */
class RPDucClay : public RPDucTarget {
public:
    typedef void (RPDucClay::*StateFunc)();

public:
    RPDucClay();
    virtual ~RPDucClay(); // at 0x8

    virtual void Load();      // at 0x10
    virtual void Calculate(); // at 0x18

    virtual void ResetParams();       // at 0x60
    virtual void Launch(u32 pattern); // at 0x64
    virtual void SpawnHitEffect();    // at 0x68
    virtual void PlaySpawnSound();    // at 0x6C
    virtual void PlayHitSound(s32 player); // at 0x70
    virtual void ApplyVelocity();     // at 0x78
    virtual bool CanShotPassThrough() { // at 0xA4
        return true;
    }

    void SetupLaunch();
    bool CheckShotHit();
    void StateCheckHit();
    void StateRise();
    void StateSlow();
    void StateFall();

private:
    RPDucClayParam mParam;    // at 0xFC
    nw4r::math::VEC3 mAimPos; // at 0x2E0 [M]
    f32 mSpin;                // at 0x2EC [M] turns the forward axis about the side axis
    f32 mThrust;              // at 0x2F0 [M] along the up axis
    f32 mSpeedScale;          // at 0x2F4 [L] reset to 1.0
    s32 mState;               // at 0x2F8
    StateFunc mStateFuncs[4]; // at 0x2FC
};

//! @}

#endif
