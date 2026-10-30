#ifndef RP_PARTY_DUC_BALLOON_H
#define RP_PARTY_DUC_BALLOON_H
#include <Pack/types_pack.h>

#include <port/be.h>

#include <Pack/RPGraphics/IRPGrpModelCallback.h>
#include <Pack/RPParty/RPDucScene/RPDucTarget.h>

#include <nw4r/math.h>

//! @addtogroup rp_duc
//! @{

// Forward declarations
class RPCurve;
class RPGrpModel;

/**
 * @brief One launch pattern of param/DucBalloonParam.bin (RPDucBalloon::Launch, pattern & 7)
 */
struct RPDucBalloonPattern {
    be_f32 mHitScale;  // at 0x0 debug sphere scale
    be_f32 mRise;      // at 0x4 [M] acceleration along unk1BC
    be_f32 mDriftX;    // at 0x8 [M] random sideways drift range
    be_f32 mHitRadius; // at 0xC
};

/**
 * @brief Contents of param/DucBalloonParam.bin, copied into RPDucBalloon by Load
 * @details Size 0xBC [H] (47 words); copied member by member (the Load copy).
 */
struct RPDucBalloonParam {
    be_f32 mDrag;                     // at 0x0 velocity factor per step
    be_s32 mPoints;                   // at 0x4 doubled for the same colour twice
    be_f32 mBaseRadius;               // at 0x8 hit radius of scale 1
    be_f32 unk0C;                     // at 0xC [L] string curve parameter
    be_f32 unk10;                     // at 0x10 [L] string curve parameter
    be_f32 unk14;                     // at 0x14 [M] screen-exit offset below the string top
    be_f32 unk18;                     // at 0x18 [L] string curve parameter
    be_f32 mStringLength;             // at 0x1C balloon centre to the string top
    be_s32 mSwayStep;                 // at 0x20 sway angle step (16-bit index)
    be_f32 mSwayAmp;                  // at 0x24 sway amplitude in degrees
    RPDucBalloonPattern mPatterns[8]; // at 0x28
    be_f32 mStringSegs[5];            // at 0xA8 curve segment lengths of the string [M]
};

/**
 * @brief Balloon target (three colours) with a string model
 * @details vtable 80359CD8 (IRPGrpModelCallback part at 0xFC, vtable 80359D80). Size 0x208,
 * verified. Two states: 0 check hit, 1 float. The string bends through EditWorldMtx.
 * @note Name is a project convention (ledger Duc_Balloon).
 */
class RPDucBalloon : public RPDucTarget, public IRPGrpModelCallback {
public:
    typedef void (RPDucBalloon::*StateFunc)();

public:
    RPDucBalloon();
    virtual ~RPDucBalloon(); // at 0x8

    virtual void Create();    // at 0xC
    virtual void Load();      // at 0x10
    virtual void Calculate(); // at 0x18

    virtual void CalcModelMtx(); // at 0x24

    virtual void ResetParams();    // at 0x60
    virtual void Launch(u32 pattern); // at 0x64
    virtual void SpawnHitEffect(); // at 0x68
    virtual void PlaySpawnSound(); // at 0x6C
    virtual void PlayHitSound(s32 player); // at 0x70
    virtual void ApplyVelocity();  // at 0x78

    /**
     * @brief IRPGrpModelCallback::EditWorldMtx override: builds the string joint matrices
     * (thunk in the IRPGrpModelCallback part) [M]
     */
    virtual void EditWorldMtx(RPGrpModel* pModel); // at 0xC4

    bool CheckShotHit();
    void StateCheckHit();
    void StateFloat();

private:
    RPDucBalloonParam mParam;     // at 0x100
    nw4r::math::VEC3 mRiseDir;    // at 0x1BC [M] drift direction (x random, unit length)
    f32 mRise;                    // at 0x1C8 [M] from the pattern
    u32 mSwayIdx;                 // at 0x1CC random 16-bit start, advanced by mSwayStep
    f32 mSwayAngle;               // at 0x1D0 degrees
    s32 mColor;                   // at 0x1D4 0 red, 1 blue, 2 yellow (pattern bits 4-5)
    RPGrpModel* mpColorModels[3]; // at 0x1D8 red, blue, yellow
    RPGrpModel* mpStringModel;    // at 0x1E4 sht_balloon_himo
    RPCurve* mpStringCurve;       // at 0x1E8
    s32 mState;                   // at 0x1EC
    StateFunc mStateFuncs[2];     // at 0x1F0
};

//! @}

#endif
