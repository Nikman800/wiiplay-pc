#ifndef RP_PARTY_DUC_UFO_H
#define RP_PARTY_DUC_UFO_H
#include <Pack/types_pack.h>

#include <port/be.h>

#include <Pack/RPGraphics/IRPGrpModelCallback.h>
#include <Pack/RPParty/RPDucScene/RPDucTarget.h>

#include <nw4r/math.h>

//! @addtogroup rp_duc
//! @{

// Forward declarations
class RPDucAvatar;
class RPGrpModel;
class RPSysEffect;

/**
 * @brief One pattern of RPDucUfoParam (RPDucUfo::Launch, pattern & 7)
 * @details Size 0x8C [H]. The frame counts are tripled by LoadParams (three sub-steps per
 * frame) [M].
 */
struct RPDucUfoPattern {
    be_f32 mHitScale;      // at 0x0
    be_f32 mLeaveAccel;    // at 0x4 vertical acceleration while leaving
    be_f32 mSteer;         // at 0x8 approach steering factor
    be_f32 mApproachSpeed; // at 0xC
    be_s32 mHoldFrames;    // at 0x10
    be_s32 mAbductFrames;  // at 0x14 circling before the abduction
    be_s32 mLiftFrames[2]; // at 0x18 [1] in the two-player versus mode
    be_f32 mFlyAccel[2];   // at 0x20 [1] in the two-player versus mode
    be_f32 mFleeRangeX;    // at 0x28
    be_f32 mFleeRangeY;    // at 0x2C
    be_f32 mFleeAimY;      // at 0x30
    be_f32 mFleeAimZ;      // at 0x34
    be_f32 mFleeSpeed[2];  // at 0x38 [1] in the two-player versus mode
    be_f32 mHitRadius;     // at 0x40
    be_s32 mWaitFrames;    // at 0x44
    be_s32 mMoveCount;     // at 0x48 wander moves before a long wait
    be_f32 mWanderSpeed;   // at 0x4C
    be_f32 mWanderVelZ;    // at 0x50
    be_s32 mWanderFrames;  // at 0x54
    be_s32 mWait2Frames;   // at 0x58
    be_f32 mCircleSpeed;   // at 0x5C
    be_f32 mCircleVelZ;    // at 0x60
    be_s32 mCircleTimeMax; // at 0x64
    be_s32 mCircleTimeMin; // at 0x68
    be_s32 mCircleStep[2]; // at 0x6C [0] model b, [1] model a
    be_s32 mCircleFrames;  // at 0x74
    be_f32 mAbductSpeed;   // at 0x78
    be_s32 mAbductTimeMax; // at 0x7C
    be_s32 mAbductTimeMin; // at 0x80
    be_s32 mAbductStep[2]; // at 0x84 [0] model b, [1] model a
};

/**
 * @brief Contents of param/DucUfoParam.bin, copied into RPDucUfo by LoadParams
 * @details Size 0x4A8 [H]: a 0x48-byte header and eight patterns; copied member by member.
 */
struct RPDucUfoParam {
    be_f32 mFarDist;              // at 0x0 [M] hit scale grows from mNearDist to here
    be_f32 mGrowZ;                // at 0x4 [M]
    be_f32 mNearDist;             // at 0x8 [M]
    be_f32 mNearZ;                // at 0xC [M]
    be_s32 mPointsB;              // at 0x10
    be_s32 mPointsVersusB;        // at 0x14
    be_s32 mPointsA;              // at 0x18
    be_s32 mPointsVersusA;        // at 0x1C
    be_f32 mLiftHeight;           // at 0x20
    be_f32 mAbductY;              // at 0x24
    be_f32 mBaseRadius;           // at 0x28 hit radius of scale 1
    be_f32 unk2C;                 // at 0x2C [L]
    be_f32 mWanderDistMax;        // at 0x30
    be_f32 mWanderDistMin;        // at 0x34
    be_f32 unk38;                 // at 0x38 [L]
    be_f32 mCircleRadius;         // at 0x3C
    be_f32 mAbductBottom;         // at 0x40
    be_f32 mAbductRadius;         // at 0x44
    RPDucUfoPattern mPatterns[8]; // at 0x48
};

/**
 * @brief UFO target that abducts the Mii avatars
 * @details vtable 8035A2F8 (IRPGrpModelCallback part at 0xFC, vtable 8035A3A0). Size 0x6C8,
 * verified. Thirteen states: check hit, leave, wait 2, wander move, wait, move to circle start,
 * circle, move to circle, circle before abduct, approach avatar, lift, hold, fly away.
 * Model a is mpModel, model b mpModelB.
 * @note Name is a project convention (ledger Duc_Ufo).
 */
class RPDucUfo : public RPDucTarget, public IRPGrpModelCallback {
public:
    typedef void (RPDucUfo::*StateFunc)();

public:
    RPDucUfo();
    virtual ~RPDucUfo(); // at 0x8

    virtual void Create();    // at 0xC
    virtual void Load();      // at 0x10
    virtual void Calculate(); // at 0x18

    virtual void CalcModelMtx(); // at 0x24

    virtual void ResetParams();       // at 0x60
    virtual void Launch(u32 pattern); // at 0x64
    virtual void SpawnHitEffect();    // at 0x68
    virtual void PlaySpawnSound();    // at 0x6C
    virtual void PlayHitSound(s32 player); // at 0x70
    virtual void ApplyVelocity();     // at 0x78
    /**
     * @brief RPDucTarget::VF_0x9C override: picks the avatar to abduct [M]
     */
    virtual void VF_0x9C(RPDucAvatar* pAvatar); // at 0x9C

    /**
     * @brief IRPGrpModelCallback::EditWorldMtx override (thunk in the
     * IRPGrpModelCallback part) [M]
     */
    virtual void EditWorldMtx(RPGrpModel* pModel); // at 0xC4

    void LoadParams();
    void SetupCircle();
    void SetupAbduct();
    void PickWanderTarget();
    void SetupFlee();
    bool CheckShotHit();
    void UpdateBeamEffects();
    void StateCheckHit();
    void StateLeave();
    void StateWait2();
    void StateWanderMove();
    void StateWait();
    void StateMoveToCircleStart();
    void StateCircle();
    void StateMoveToCircle();
    void StateCircleBeforeAbduct();
    void StateApproachAvatar();
    void StateLift();
    void StateHold();
    void StateFlyAway();

    RPGrpModel* GetCurrentModel() const {
        return mIsTypeB ? mpModelB : mpModel;
    }

    void SetVelocity2D(f32 y, f32 x) {
        mVelocity.z = 0.0f;
        mVelocity.x = x;
        mVelocity.y = y;
    }

    void SetAbductGoal(nw4r::math::VEC3 pos) {
        mGoal = pos;
        mGoal.y = mParam.mAbductY;
        mGoal.z += 3.0f;
    }

    static const s32 sPatternNum;

private:
    RPDucUfoParam mParam;            // at 0x100
    f32 mBoundLeft;                  // at 0x5A8 screen edges at the UFO depth
    f32 mBoundRight;                 // at 0x5AC
    f32 mBoundTop;                   // at 0x5B0
    f32 mBoundBottom;                // at 0x5B4
    nw4r::math::VEC3 mDir;           // at 0x5B8
    f32 mSpeed;                      // at 0x5C4
    f32 mCircleRadius;               // at 0x5C8
    nw4r::math::VEC3 mCircleCenter;  // at 0x5CC
    u16 mCircleAngle;                // at 0x5D8
    s16 mCircleStep;                 // at 0x5DA
    s16 mWaitTimer;                  // at 0x5DC
    s16 mMoveCount;                  // at 0x5DE also the circle timer
    s16 mTotalTimer;                 // at 0x5E0
    s16 mHoldTimer;                  // at 0x5E2
    nw4r::math::VEC3 mGoal;          // at 0x5E4
    f32 mCarryOffset;                // at 0x5F0 height of the UFO above the carried avatar
    s32 mMode;                       // at 0x5F4 1 wander, 2 circle, 3 abduct
    s32 mState;                      // at 0x5F8
    bool mIsTypeB;                   // at 0x5FC model b (pattern bit 3)
    RPGrpModel* mpModelB;            // at 0x600 sht_ufo_b
    bool mCarrying;                  // at 0x604
    bool mLost;                      // at 0x605 left the screen carrying the avatar
    s32 unk608;                      // at 0x608 [L] reset to -1
    RPDucAvatar* mpTargetAvatar;     // at 0x60C
    nw4r::math::VEC3 mBeamPos;       // at 0x610 [M]
    f32 mBeamScale;                  // at 0x61C
    RPSysEffect* mpEffects[3];       // at 0x620 Sht_ufo_abduct, Sht_ufo_ptc, Sht_ufo_glow
    StateFunc mStateFuncs[13];       // at 0x62C
};

//! @}

#endif
