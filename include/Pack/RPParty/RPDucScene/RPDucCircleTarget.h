#ifndef RP_PARTY_DUC_CIRCLE_TARGET_H
#define RP_PARTY_DUC_CIRCLE_TARGET_H
#include <Pack/types_pack.h>

#include <port/be.h>

#include <Pack/RPParty/RPDucScene/RPDucTarget.h>

#include <nw4r/math.h>

//! @addtogroup rp_duc
//! @{

// Forward declarations
class RPGrpModel;

/**
 * @brief One pattern of RPDucCircleTargetParam (RPDucCircleTarget::Launch, pattern & 7)
 * @details Size 0x1C [H]; the frame counts are tripled by LoadParams. [0] 16:9, [1] 4:3.
 */
struct RPDucCircleTargetPattern {
    be_f32 mHitScale[2];    // at 0x0
    be_f32 mRadius[2];      // at 0x8
    be_s32 mGrowFrames;     // at 0x10
    be_s32 mStandFrames[2]; // at 0x14 [1] in the two-player versus mode
};

/**
 * @brief Contents of param/DucCircleMatoParam.bin, copied into RPDucCircleTarget by LoadParams
 * @details Size 0x140 [H]: a 0x38-byte header copied member by member, then the grid and eight
 * patterns copied as one Layout.
 */
struct RPDucCircleTargetParam {
    be_f32 mToppleMax;                         // at 0x0 degrees
    be_f32 mToppleStep;                        // at 0x4
    be_f32 mFallDrag;                          // at 0x8
    be_f32 mFallGravity;                       // at 0xC
    be_f32 unk10;                              // at 0x10 [L]
    be_s32 mPoints[8];                         // at 0x14 per type, [odd] in the versus mode:
                                               // b, a, other face, own face
    be_f32 mBaseRadius;                        // at 0x34 hit radius of scale 1
    /**
     * @brief Grid and patterns, copied by LoadParams as one block (size 0x108) [H]
     */
    struct Layout {
        be_f32 mGrid[2][5];                    // at 0x0 16:9 and 4:3: step x, step y, z,
                                               // origin x, origin y
        RPDucCircleTargetPattern mPatterns[8]; // at 0x28
    };

    Layout mLayout;                            // at 0x38
};

/**
 * @brief Round bull's-eye target (four models, Mii face on model c)
 * @details vtable 8035AE80. Size 0x2CC, verified. Six states: check hit, grow in, stand,
 * shrink out, topple, fall. Placed on a grid whose used slots RPDucCircleTargetMgr tracks.
 * @note Name is a project convention (ledger Duc_CircleTarget; "mato" is Japanese for target).
 */
class RPDucCircleTarget : public RPDucTarget {
public:
    typedef void (RPDucCircleTarget::*StateFunc)();

public:
    RPDucCircleTarget();
    virtual ~RPDucCircleTarget(); // at 0x8

    virtual void Create();    // at 0xC
    virtual void Load();      // at 0x10
    virtual void Calculate(); // at 0x18

    virtual void CalcModelMtx(); // at 0x24

    virtual void ResetParams();       // at 0x60
    virtual void Launch(u32 pattern); // at 0x64
    virtual void PlaySpawnSound();    // at 0x6C
    virtual void PlayHitSound(s32 player); // at 0x70
    virtual void ApplyVelocity();     // at 0x78
    /**
     * @brief Also frees the target's grid slot [M]
     */
    virtual void Remove(); // at 0x94

    void LoadParams();
    bool CheckShotHit();
    void SpawnHitEffects(s32 player);
    void StateCheckHit();
    void StateGrowIn();
    void StateStand();
    void StateShrinkOut();
    void StateTopple();
    void StateFall();

    static const s32 sPatternNum;

private:
    RPDucCircleTargetParam mParam; // at 0xFC
    nw4r::math::VEC3 mBase;        // at 0x23C foot of the target (topple pivot)
    f32 mTopple;                   // at 0x248 degrees
    f32 mRadius;                   // at 0x24C full-size hit radius
    f32 mScaleBase;                // at 0x250 full-size hit scale
    s32 mTimer;                    // at 0x254
    s32 mGrowFrames;               // at 0x258
    s32 mStandFrames;              // at 0x25C
    s32 mGridSlot;                 // at 0x260 [M]
    s32 mState;                    // at 0x264
    s32 mType;                     // at 0x268 0 a, 1 b, 2 face of player 1, 3 face of player 2
    RPGrpModel* mpModels[4];       // at 0x26C sht_target_a .. sht_target_d
    char unk27C[0x284 - 0x27C];
    StateFunc mStateFuncs[6];      // at 0x284
};

//! @}

#endif
