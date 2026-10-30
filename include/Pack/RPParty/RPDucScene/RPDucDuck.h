#ifndef RP_PARTY_DUC_DUCK_H
#define RP_PARTY_DUC_DUCK_H
#include <Pack/types_pack.h>

#include <port/be.h>

// Wii Play: the duck constructor must not emit an unused callback vtable/destructor.
class __declspec(novtable) IRPGrpModelCallback;
#include <Pack/RPGraphics/IRPGrpModelCallback.h>
#include <Pack/RPParty/RPDucScene/RPDucTarget.h>

#include <nw4r/math.h>

//! @addtogroup rp_duc
//! @{

// Forward declarations
class RPGrpModel;
class RPSysEffect;

/**
 * @brief One flight pattern of RPDucDuckParam (RPDucDuck::Launch, pattern & 7)
 * @details Size 0x34 [H]. LoadParams triples mBounceFrames and, in 4:3, scales mJumpRangeX
 * by 0.75 [M].
 */
struct RPDucDuckPattern {
    be_f32 mHitScale;      // at 0x0
    be_f32 mHitRadius;     // at 0x4
    be_f32 mJumpLift;      // at 0x8 added to the velocity each step while jumping out
    be_f32 mJumpRangeX;    // at 0xC random aim offset
    be_f32 mJumpRangeY;    // at 0x10 random aim offset
    be_f32 mJumpAimY;      // at 0x14
    be_f32 mJumpAimZ;      // at 0x18
    be_f32 mJumpSpeed;     // at 0x1C
    be_s32 mBounceFrames;  // at 0x20 tripled on load (three sub-steps per frame)
    be_f32 mApproachSpeed; // at 0x24
    be_f32 mArcGravity;    // at 0x28
    be_f32 mArcAngle;      // at 0x2C degrees
    be_f32 mArcSpeed;      // at 0x30
};

/**
 * @brief Velocity damping and gravity of one motion type (RPDucDuck::mMotion)
 */
struct RPDucDuckMotion {
    be_f32 mDrag;    // at 0x0
    be_f32 mGravity; // at 0x4
};

/**
 * @brief Contents of param/DucDuckParam.bin, copied into RPDucDuck by LoadParams
 * @details Size 0x21C [H] (135 words); copied member by member (the LoadParams copy).
 */
struct RPDucDuckParam {
    be_s32 mFarPoints;             // at 0x0 jumping duck hit high on the screen
    be_s32 mPoints;                // at 0x4 jumping duck
    be_s32 unk08;                  // at 0x8 [L]
    be_s32 mApproachPoints;        // at 0xC
    be_s32 unk10;                  // at 0x10 [L]
    be_s32 mArcPoints;             // at 0x14
    be_f32 mBaseRadius;            // at 0x18 hit radius of scale 1
    be_f32 mDamageDrag;            // at 0x1C
    be_f32 mDamageLift;            // at 0x20 vertical speed when hit
    be_s32 mDamageFrames;          // at 0x24 tripled on load
    be_f32 mJumpDrag;              // at 0x28
    be_f32 mFarScreenY;            // at 0x2C
    be_f32 mApproachRangeMax;      // at 0x30
    be_f32 mApproachRangeMin;      // at 0x34
    be_f32 mApproachRadius;        // at 0x38
    be_f32 mForwardY;              // at 0x3C
    be_f32 mForwardRangeX;         // at 0x40 scaled by 0.75 in 4:3
    be_f32 mForwardZMax;           // at 0x44
    be_f32 mForwardXMin;           // at 0x48
    be_f32 mForwardZMin;           // at 0x4C
    be_f32 mForwardAccel;          // at 0x50
    be_f32 mForwardSpeedMax;       // at 0x54
    be_f32 mForwardSpeed;          // at 0x58
    RPDucDuckMotion mMotions[4];   // at 0x5C
    RPDucDuckPattern mPatterns[8]; // at 0x7C
};

/**
 * @brief Duck target (three colour models a/b/c)
 * @details vtable 80359FA8 (IRPGrpModelCallback part at 0xFC, vtable 8035A050). Size 0x3E0,
 * verified. Nine states: fly, jump out, approach, bounce, bounce exit, arc, fly forward,
 * damaged, fall dead. The animation virtuals are overridden to drive all three models.
 * @note Name is a project convention (ledger Duc_Duck).
 */
class RPDucDuck : public RPDucTarget, public IRPGrpModelCallback {
public:
    typedef void (RPDucDuck::*StateFunc)();

public:
    RPDucDuck();
    virtual ~RPDucDuck(); // at 0x8

    virtual void Create();    // at 0xC
    virtual void Load();      // at 0x10
    virtual void Calculate(); // at 0x18

    virtual void CalcModelMtx(); // at 0x24
    virtual void CreateAnmBuffer(RPGrpModelAnm::Anm anm, u16 num); // at 0x30
    virtual void RegistAnm(RPGrpModelAnm::Anm anm, u16 idx,
                           RPGrpHandle handle); // at 0x38
    virtual bool StartAnm(RPGrpModelAnm::Anm anm, u16 idx,
                          f32 frame); // at 0x3C
    virtual bool CancelAnm(RPGrpModelAnm::Anm anm, u16 idx); // at 0x44
    virtual void SetAnmUpdateRate(RPGrpModelAnm::Anm anm, u16 idx,
                                  f32 rate);                        // at 0x48
    virtual f32 GetAnmFrame(RPGrpModelAnm::Anm anm, u16 idx);    // at 0x4C
    virtual f32 GetAnmNumFrame(RPGrpModelAnm::Anm anm, u16 idx); // at 0x50

    virtual void ResetParams();        // at 0x60
    virtual void Launch(u32 pattern);  // at 0x64
    virtual void SpawnHitEffect();     // at 0x68
    virtual void PlaySpawnSound();     // at 0x6C
    virtual void PlayHitSound(s32 player); // at 0x70
    virtual void ApplyVelocity();      // at 0x78
    virtual bool CanShotPassThrough() { // at 0xA4
        // Preserve the original 0x20 zero bytes after the vtable as a weak writable literal.
#pragma push
#pragma readonly_strings off
        (void)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
#pragma pop
        return true;
    }

    /**
     * @brief IRPGrpModelCallback::EditWorldMtx override (thunk in the
     * IRPGrpModelCallback part) [M]
     */
    virtual void EditWorldMtx(RPGrpModel* pModel); // at 0xC4

    void LoadParams();
    void UpdateMouthEffect();
    void SetupJumpOut();
    void SetupApproach();
    void SetupFlyForward();
    bool CheckShotHit();
    void UpdateOrientation();
    void StateFly();
    void StateJumpOut();
    void StateApproach();
    void StateBounce();
    void StateBounceExit();
    void StateArc();
    void StateFlyForward();
    void StateDamaged();
    void StateFallDead();

    RPGrpModel* GetColorModel() const {
        RPGrpModel* pModel;
        switch (mColor) {
        case 0:
            pModel = mpModel;
            break;
        case 1:
            pModel = mpModelB;
            break;
        case 2:
            pModel = mpModelC;
            break;
        }
        return pModel;
    }

    static const s32 sPatternNum;
    static u32 sApproachSide;

private:
    RPDucDuckParam mParam;          // at 0x100
    nw4r::math::VEC3 mAimPos;       // at 0x31C
    nw4r::math::VEC3 mDir;          // at 0x328 normalised flight direction
    f32 mSpeed;                     // at 0x334
    f32 mBoundLeft;                 // at 0x338 screen edges at the duck's depth
    f32 mBoundRight;                // at 0x33C
    f32 mBoundTop;                  // at 0x340
    f32 mBoundBottom;               // at 0x344
    s32 mBounceTimer;               // at 0x348
    s16 mDamageTimer;               // at 0x34C
    s16 mAnmPaused;                 // at 0x34E the model frame is not advanced
    u16 mMotion;                    // at 0x350 index of RPDucDuckParam::mMotions
    u16 mColor;                     // at 0x352 [M] 0..2 selects the model
    u16 mSplashed;                  // at 0x354 the water splash has been spawned
    s32 mState;                     // at 0x358
    RPGrpModel* mpModelB;           // at 0x35C sht_duck_b (sht_duck_a is mpModel)
    RPGrpModel* mpModelC;           // at 0x360 sht_duck_c
    nw4r::math::VEC3 mMouthPos;     // at 0x364 [M]
    RPSysEffect* mpFallEffect;      // at 0x370 Sht_duck_fall
    StateFunc mStateFuncs[9];       // at 0x374
};

//! @}

#endif
