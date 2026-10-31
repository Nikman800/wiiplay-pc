// Laser Hockey paddle: shape, collision against the puck, the rink and the other paddle,
// movement, rumble, remote control and the CPU player.
// Unit 0x8021B844-0x8021FE14 (dtk/game_units.tsv). With -inline deferred CodeWarrior emits a
// file's functions in reverse order, so the source runs from the last address to the first.
#include <Pack/RPParty/RPHkyScene/RPHkyPaddle.h>

#include <Pack/RPGraphics.h>
#include <Pack/RPParty/RPHkyScene/RPHkyGame.h>
#include <Pack/RPParty/RPHkyScene/RPHkyHud.h>
#include <Pack/RPParty/RPHkyScene/RPHkyLyt.h>
#include <Pack/RPParty/RPHkyScene/RPHkyPuck.h>

#include <egg/core.h>

#include <cmath>

// The address of a temporary (CodeWarrior accepts `&T(...)`; the host form is in
// port/host/include/decomp/compat.h)
#if !defined(PORT_TEMP_PTR)
#define PORT_TEMP_PTR(x) (&(x))
#endif

#ifndef ASM_PS
// Host builds give this block a C form (port/host/include/decomp/compat.h).
#define ASM_PS(name) ASM
#endif

f32 RPHkyPaddle::sRoundRadius = 34.0f;
f32 RPHkyPaddle::sCpuGain = 0.03f;
f32 RPHkyPaddle::sMaxSpeed = 10.0f;
f32 RPHkyPaddle::sVelDamping = 0.5f;
f32 RPHkyPaddle::sRectLength = 105.0f;
f32 RPHkyPaddle::sRectWidth = 16.0f;
s32 RPHkyPaddle::sRumbleFrames = 10;
f32 RPHkyPaddle::sAngleScale = 3.0f;
f32 RPHkyPaddle::sCpuTargetLerp = 1.0f;
f32 RPHkyPaddle::sCpuMaxSpeed = 0.3f;
f32 RPHkyPaddle::sCpuSpeedBase[2] = {0.2f, 0.2f};
f32 RPHkyPaddle::sCpuSpeedMin[2] = {0.1f, 0.1f};
f32 RPHkyPaddle::sCpuSpeedLead[2] = {0.05f, 0.05f};
f32 RPHkyPaddle::sCpuSpeedTrail[2] = {0.05f, 0.05f};
f32 RPHkyPaddle::sCpuSpeedMax[2] = {0.9f, 0.9f};
s32 RPHkyPaddle::sCpuLookAhead[2] = {10, 10};
f32 RPHkyPaddle::sPointerDepthScale[2] = {1.2f};

static inline const f32& MaxRef(const f32& a, const f32& b) {
    return a < b ? b : a;
}

static inline f32 LengthXZ(const nw4r::math::VEC3& rVec) {
    return std::sqrtf(rVec.x * rVec.x + rVec.z * rVec.z);
}

// NONMATCHING-DEBT: three 12-byte row copies, as in RPHkyPuck.cpp (see the reason there).
static inline void CopyRows(nw4r::math::MTX33& rDst, const nw4r::math::VEC3* pSrc) {
    struct Row {
        f32 v[3];
    };

    for (int i = 0; i < 3; i++) {
        *reinterpret_cast<Row*>(rDst.m[i]) = *reinterpret_cast<const Row*>(&pSrc[i]);
    }
}

enum EHkyPlayMode { HkyPlay_Single, HkyPlay_Multi };
static inline BOOL IsCpuPlayer(s32 player) {
    EHkyPlayMode mode = (EHkyPlayMode)RPHkyGame::GetInstance()->IsMultiPlayer();
    return !mode && player == 1;
}

inline BOOL RPHkyPaddle::IsCpu() const {
    return !RPHkyGame::GetInstance()->IsMultiPlayer() && mPlayerIndex == 1;
}

inline void RPHkyPaddle::StartControllerRumble(s32 frames) {
    if (!IsCpuPlayer(mPlayerIndex)) {
        EGG::CoreControllerMgr::instance()->getNthController(mPlayerIndex)->startSimpleRumble(
            -1, frames, 32);
    }
}

inline void RPHkyPaddle::ResetPushes() {
    mPushMin.x = mPushMin.y = mPushMin.z = 0.0f;
    mPushMax.x = mPushMax.y = mPushMax.z = 0.0f;
    mVelPushMin.x = mVelPushMin.y = mVelPushMin.z = 0.0f;
    mVelPushMax.x = mVelPushMax.y = mVelPushMax.z = 0.0f;
}

inline void RPHkyPaddle::AddPush(const nw4r::math::VEC3& rPush) {
    if (rPush.x < mPushMin.x) {
        mPushMin.x = rPush.x;
    } else if (rPush.x > mPushMax.x) {
        mPushMax.x = rPush.x;
    }

    if (rPush.y < mPushMin.y) {
        mPushMin.y = rPush.y;
    } else if (rPush.y > mPushMax.y) {
        mPushMax.y = rPush.y;
    }

    if (rPush.z < mPushMin.z) {
        mPushMin.z = rPush.z;
    } else if (rPush.z > mPushMax.z) {
        mPushMax.z = rPush.z;
    }
}

inline void RPHkyPaddle::AddVelocityPush(const nw4r::math::VEC3& rPush) {
    if (rPush.x < mVelPushMin.x) {
        mVelPushMin.x = rPush.x;
    } else if (rPush.x > mVelPushMax.x) {
        mVelPushMax.x = rPush.x;
    }

    if (rPush.y < mVelPushMin.y) {
        mVelPushMin.y = rPush.y;
    } else if (rPush.y > mVelPushMax.y) {
        mVelPushMax.y = rPush.y;
    }

    if (rPush.z < mVelPushMin.z) {
        mVelPushMin.z = rPush.z;
    } else if (rPush.z > mVelPushMax.z) {
        mVelPushMax.z = rPush.z;
    }
}

inline void RPHkyPaddle::SetRectEdges() {
    f32 l = 0.5f * sRectLength;
    f32 w = 0.5f * sRectWidth;

    mEdges[0].mPos.x = -w;
    mEdges[0].mPos.y = 0.0f;
    mEdges[0].mPos.z = l;
    mEdges[0].mDir.x = 1.0f;
    mEdges[0].mDir.y = 0.0f;
    mEdges[0].mDir.z = 0.0f;
    mEdges[0].mLength = sRectWidth;

    mEdges[1].mPos.x = w;
    mEdges[1].mPos.y = 0.0f;
    mEdges[1].mPos.z = -l;
    mEdges[1].mDir.x = -1.0f;
    mEdges[1].mDir.y = 0.0f;
    mEdges[1].mDir.z = 0.0f;
    mEdges[1].mLength = sRectWidth;

    mEdges[2].mPos.x = w;
    mEdges[2].mPos.y = 0.0f;
    mEdges[2].mPos.z = l;
    mEdges[2].mDir.x = 0.0f;
    mEdges[2].mDir.y = 0.0f;
    mEdges[2].mDir.z = -1.0f;
    mEdges[2].mLength = sRectLength;

    mEdges[3].mPos.x = -w;
    mEdges[3].mPos.y = 0.0f;
    mEdges[3].mPos.z = -l;
    mEdges[3].mDir.x = 0.0f;
    mEdges[3].mDir.y = 0.0f;
    mEdges[3].mDir.z = 1.0f;
    mEdges[3].mLength = sRectLength;
}

RPHkyPaddle::~RPHkyPaddle() {}

RPHkyPaddleTrail::~RPHkyPaddleTrail() {}

RPHkyPaddle::RPHkyPaddle(s32 playerIndex) : mPlayerIndex(playerIndex) {
    mTarget = sVecZero;
    mPrevPos1 = sVecZero;
    mPrevPos2 = sVecZero;
    ResetPushes();
    mScore = 0;
    mpEdgeIter = NULL;

    f32 w = 0.5f * sRectWidth;
    f32 l = 0.5f * sRectLength;

    mEdges[0].mPos.x = -w;
    mEdges[0].mPos.y = 0.0f;
    mEdges[0].mPos.z = l;
    mEdges[0].mDir.x = 1.0f;
    mEdges[0].mDir.y = 0.0f;
    mEdges[0].mDir.z = 0.0f;
    mEdges[0].mLength = sRectWidth;

    mEdges[1].mPos.x = w;
    mEdges[1].mPos.y = 0.0f;
    mEdges[1].mPos.z = -l;
    mEdges[1].mDir.x = -1.0f;
    mEdges[1].mDir.y = 0.0f;
    mEdges[1].mDir.z = 0.0f;
    mEdges[1].mLength = sRectWidth;

    mEdges[2].mPos.x = w;
    mEdges[2].mPos.y = 0.0f;
    mEdges[2].mPos.z = l;
    mEdges[2].mDir.x = 0.0f;
    mEdges[2].mDir.y = 0.0f;
    mEdges[2].mDir.z = -1.0f;
    mEdges[2].mLength = sRectLength;

    mEdges[3].mPos.x = -w;
    mEdges[3].mPos.y = 0.0f;
    mEdges[3].mPos.z = -l;
    mEdges[3].mDir.x = 0.0f;
    mEdges[3].mDir.y = 0.0f;
    mEdges[3].mDir.z = 1.0f;
    mEdges[3].mLength = sRectLength;

    mpEffect = new RPSysEffect(mPlayerIndex == 0 ? "Hky_paddletail_b1" : "Hky_paddletail_r1",
                               0, 0);
}

void RPHkyPaddle::Create() {
    mpTrail = new RPHkyPaddleTrail();

    mShapeType = Shape_Rect;
    for (int i = 0; i < 4; i++) {
        mRumbleState[i] = 0;
        mRumbleFrames[i] = 0;
    }

    ResetPushes();
    unk74 = unk7C = unk78 = 0.0f;

    CopyRows(mRot, sMtxIdentity);
    mVelocity = sVecZero;
    mPos.x = mPos.z = 0.0f;
    mPos.y = 100.0f;
}

void RPHkyPaddle::Load() {
    RPGrpHandle file = RPSimpleModel::LoadResource(
        mPlayerIndex == 0 ? "/G3D/Hck_paddle_B1.brres" : "/G3D/Hck_paddle_R1.brres");
    RPSimpleModel::LoadModel(mPlayerIndex == 0 ? "Hck_paddle_B1" : "Hck_paddle_R1", file, 1, 0,
                             0x108);

    RPGrpModel* pModel = mpModel;
    pModel->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Clr, 1);

    RPGrpHandle anmFile = RPSimpleModel::LoadResource("/G3D/Hck_anim.brres");
    const char* pAnmName = mPlayerIndex == 0 ? "Hck_paddle_B_hit" : "Hck_paddle_R_hit";
    RPGrpModelResManager* pResMgr = RPGrpModelResManager::GetCurrent();
    RPGrpHandle anm = pResMgr->CreateData(
        RPGrpModelResManager::Type_ResAnmClr,
        pResMgr->GetPtr(RPGrpModelResManager::Type_ResAnmClr, anmFile, pAnmName));
    pModel->GetModelAnm()->Regist(RPGrpModelAnm::Anm_Clr, 0, anm);
}

void RPHkyPaddle::Reset() {
    mPos = sVecZero;
    mScale = sVecOne;
    SetShape(Shape_Rect);

    mHitTimer = 0;
    if (mPlayerIndex == 0) {
        mPos.x = 3.0f * -RPHkyField::cHalfDepth / 8.0f;
    } else {
        mPos.x = 3.0f * RPHkyField::cHalfDepth / 8.0f;
    }

    mTarget = mPrevPos1 = mPrevPos2 = mPos;
    mScore = 0;
    sCpuMaxSpeed = 0.3f;

    for (int i = 0; i < 5; i++) {
        mPosHistory[i] = sVecZero;
    }
    mPrevPos = sVecZero;

    for (int i = 0; i < 4; i++) {
        mAngleHistory[i] = 0.0f;
    }
    mAngleDelta = 0.0f;
    mPrevAngle = 0.0f;
    mAngle = 0.0f;

    mpEffect->create();
    mCpuStallTimer = 0;
    mCpuAngleOffset = 0.0f;
    mCpuSpinTimer = 0;
}

void RPHkyPaddle::Calculate() {
    if (mShapeType == Shape_Rect) {
        CalcModelMtx();
        mpTrail->set3DPosition(mPos);

        nw4r::math::MTX34 mtx;
        mtx._00 = mRot._00;
        mtx._10 = mRot._01;
        mtx._20 = mRot._02;
        mtx._01 = mRot._10;
        mtx._11 = mRot._11;
        mtx._21 = mRot._12;
        mtx._02 = mRot._20;
        mtx._12 = mRot._21;
        mtx._22 = mRot._22;
        mtx._03 = mPos.x;
        mtx._13 = mPos.y;
        mtx._23 = mPos.z;
        mpEffect->setMtx(mtx);
        mpEffect->update();
    }

    if ((EGG::CoreControllerMgr::instance()->getNthController(mPlayerIndex)->downTrigger(0x800) &&
         EGG::CoreControllerMgr::instance()->getNthController(mPlayerIndex)->down(0x400)) ||
        (EGG::CoreControllerMgr::instance()->getNthController(mPlayerIndex)->down(0x800) &&
         EGG::CoreControllerMgr::instance()->getNthController(mPlayerIndex)->downTrigger(0x400))) {
        switch (RPHkyHud::GetInstance()->GetStartMsgLyt()->GetState()) {
        case 2:
        case 3:
        case 4:
            if (!IsCpu()) {
                s32 shape = mShapeType != Shape_Rect;
                SetShape(shape);

                if (shape == Shape_Rect) {
                    mpTrail->startSound(20U);
                } else {
                    mpTrail->startSound(19U);
                }

                if (!RPHkyGame::GetInstance()->IsMultiPlayer()) {
                    RPHkyGame::GetInstance()->GetPaddle(1)->SetShape(shape);
                }
            }
            break;
        }
    }
}

void RPHkyPaddle::Exit() {}

void RPHkyPaddle::UpdateCpuStallTimer() {
    static nw4r::math::VEC3 sPrevPuckPos = sVecZero;
    static s32 sStallCount;

    nw4r::math::VEC3 puckPos = RPHkyGame::GetInstance()->GetPuck()->GetPos();
    if (sPrevPuckPos != puckPos) {
        if (nw4r::math::VEC3Len(PORT_TEMP_PTR(puckPos - sPrevPuckPos)) < 0.002f && puckPos.x > sRectWidth &&
            mCpuStallTimer == 0) {
            if (++sStallCount > 40) {
                mCpuStallTimer = 20;
                sStallCount = 0;
            }
        } else {
            sStallCount = 0;
        }
        sPrevPuckPos = puckPos;
    } else if (puckPos.x > sRectWidth && mCpuStallTimer == 0) {
        if (++sStallCount > 40) {
            mCpuStallTimer = 20;
            sStallCount = 0;
        }
    }

    if (--mCpuStallTimer < 0) {
        mCpuStallTimer = 0;
    }

    if (RPHkyGame::GetInstance()->GetTimer() < 0) {
        mCpuStallTimer = 0;
    }
}

/**
 * @brief Whether the CPU can still reach the predicted puck position: the further the puck is
 * on the CPU side, the closer to the goal mouth it must be
 */
// Preserve the original's in-place double-precision fabs followed by float rounding.
static inline f32 PuckSpeedAbs(f32 x) {
    return std::fabs(x);
}

static inline nw4r::math::VEC3 PuckVelocityValue(RPHkyPuck* pPuck) {
    nw4r::math::VEC3 value = pPuck->GetVelocity();
    return value;
}

static inline nw4r::math::VEC3 PuckPositionValue(RPHkyPuck* pPuck) {
    nw4r::math::VEC3 value = pPuck->GetPos();
    return value;
}

static inline bool IsPuckReachable(nw4r::math::VEC3 pos, const RPHkyGame* pGame) {
    nw4r::math::VEC3 vel = PuckVelocityValue(pGame->GetPuck());

    f32 t = pos.x / (RPHkyField::sWidth - RPHkyField::cCorner);
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t < 0.5) {
        t -= 0.5f;
        t *= 2.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }

    bool reachable = true;
    f32 margin = t * (RPHkyField::cHalfDepth - 0.8f * RPHkyField::cGoalWidth / 2.0f);
    if (!(PuckSpeedAbs(vel.x) < 0.01f)) {
        if (!(std::fabs(pos.z) <= RPHkyField::cHalfDepth - margin)) {
            reachable = false;
        }
    }

    return reachable;
}

f32 RPHkyPaddle::CalcCpuTargetRound() {
    RPHkyGame* pGame = RPHkyGame::GetInstance();

    s32 score = pGame->GetPaddle(0)->GetScore();
    s32 lead = score - pGame->GetPaddle(1)->GetScore();
    if (score >= 3 || lead >= 0) {
        sCpuMaxSpeed = sCpuSpeedLead[0] * score + sCpuSpeedBase[0];
    } else {
        sCpuMaxSpeed = sCpuSpeedTrail[0] * lead + sCpuSpeedBase[0];
    }

    if (sCpuMaxSpeed < sCpuSpeedMin[0]) {
        sCpuMaxSpeed = sCpuSpeedMin[0];
    } else if (sCpuMaxSpeed > sCpuSpeedMax[0]) {
        sCpuMaxSpeed = sCpuSpeedMax[0];
    }

    nw4r::math::VEC3 target;
    nw4r::math::VEC3 predict = PuckPositionValue(pGame->GetPuck());
    nw4r::math::VEC3 vel = pGame->GetPuck()->GetVelocity();
    predict += vel * sCpuLookAhead[0];

    if (predict.z > RPHkyField::cHalfDepth - RPHkyField::cCorner) {
        predict.z -= RPHkyField::cHalfDepth - RPHkyField::cCorner;
    } else if (predict.z < -(RPHkyField::cHalfDepth - RPHkyField::cCorner)) {
        predict.z += RPHkyField::cHalfDepth - RPHkyField::cCorner;
    }

    if (predict.y > 5.0f || predict.x == 0.0f || pGame->GetTimer() < 0) {
        target.x = 3.0f * RPHkyField::sWidth / 8.0f;
    } else if (mCpuStallTimer != 0) {
        target.x = RPHkyField::sWidth - RPHkyField::cCorner - sRoundRadius;
    } else if (predict.x < sRectWidth) {
        target.x = RPHkyField::sWidth - 2.0f * RPHkyField::cCorner;
    } else {
        IsPuckReachable(predict, pGame);
        target.x = sCpuTargetLerp * (predict.x - mPos.x) + mPos.x;
    }

    target.y = 0.0f;
    if (mCpuStallTimer != 0 || pGame->GetTimer() < 0) {
        target.z = 0.0f;
    } else if (predict.x < sRectWidth) {
        target.z = sCpuTargetLerp * (predict.z - mPos.z) + mPos.z;
    } else if (IsPuckReachable(predict, pGame)) {
        target.z = sCpuTargetLerp * (predict.z - mPos.z) + mPos.z;
    } else {
        target.z = mTarget.z;
    }

    if (target.x > RPHkyField::sWidth - sRectWidth) {
        target.x = RPHkyField::sWidth - sRectWidth;
    }

    if (target.z > RPHkyField::cHalfDepth - sRectLength) {
        target.z = RPHkyField::cHalfDepth - sRectLength;
    } else if (target.z < -RPHkyField::cHalfDepth + sRectLength) {
        target.z = -RPHkyField::cHalfDepth + sRectLength;
    }

    mTarget = target;
    f32 dz = -mPos.z;
    f32 dx = -RPHkyField::sWidth - mPos.x;
    f32 angle = std::atan2(dz, dx);
    angle += 3.1415927f;
    return angle;
}

f32 RPHkyPaddle::CalcCpuTargetRect() {
    RPHkyGame* pGame = RPHkyGame::GetInstance();

    s32 score = pGame->GetPaddle(0)->GetScore();
    s32 lead = score - pGame->GetPaddle(1)->GetScore();
    if (score >= 3 || lead >= 0) {
        sCpuMaxSpeed = sCpuSpeedLead[1] * score + sCpuSpeedBase[1];
    } else {
        sCpuMaxSpeed = sCpuSpeedTrail[1] * lead + sCpuSpeedBase[1];
    }

    if (sCpuMaxSpeed < sCpuSpeedMin[1]) {
        sCpuMaxSpeed = sCpuSpeedMin[1];
    } else if (sCpuMaxSpeed > sCpuSpeedMax[1]) {
        sCpuMaxSpeed = sCpuSpeedMax[1];
    }

    nw4r::math::VEC3 target;
    nw4r::math::VEC3 predict = PuckPositionValue(pGame->GetPuck());
    nw4r::math::VEC3 vel = pGame->GetPuck()->GetVelocity();
    predict += vel * sCpuLookAhead[1];

    if (predict.z > RPHkyField::cHalfDepth - RPHkyField::cCorner) {
        predict.z -= RPHkyField::cHalfDepth - RPHkyField::cCorner;
    } else if (predict.z < -(RPHkyField::cHalfDepth - RPHkyField::cCorner)) {
        predict.z += RPHkyField::cHalfDepth - RPHkyField::cCorner;
    }

    if (predict.y > 5.0f || predict.x == 0.0f || mCpuStallTimer != 0 || pGame->GetTimer() < 0) {
        target.x = 3.0f * RPHkyField::sWidth / 8.0f;
    } else if (predict.x < sRectWidth) {
        target.x = predict.x + (RPHkyField::sWidth - 2.0f * RPHkyField::cCorner);
    } else {
        IsPuckReachable(predict, pGame);
        target.x = sRectWidth / 2.0f + (sCpuTargetLerp * (predict.x - mPos.x) + mPos.x);
    }

    target.y = 0.0f;
    if (mCpuStallTimer != 0 || pGame->GetTimer() < 0) {
        target.z = 0.0f;
    } else if (predict.x < sRectWidth) {
        target.z = sCpuTargetLerp * (predict.z - mPos.z) + mPos.z;
    } else if (IsPuckReachable(predict, pGame)) {
        target.z = sCpuTargetLerp * (predict.z - mPos.z) + mPos.z;
    } else {
        target.z = mTarget.z;
    }

    f32 dx = -RPHkyField::sWidth + RPHkyField::cCorner - mPos.x;
    f32 dz = -mPos.z;
    f32 angle = std::atan2(dz, dx);
    angle += 3.1415927f;

    if (RPHkyGame::GetInstance()->GetTimer() < 0) {
        if (mCpuAngleOffset != 0.0f) {
            if (mCpuSpinTimer == 0) {
                mCpuAngleOffset += mPos.z > 0.0f ? 0.06981317f : -0.06981317f;
            }

            if (mCpuAngleOffset > 6.2831855f) {
                mCpuAngleOffset = 0.0f;
            } else if (mCpuAngleOffset < -6.2831855f) {
                mCpuAngleOffset = 0.0f;
            }
        }
    } else if (predict.x < sRectLength) {
        if (mCpuAngleOffset > 0.06981317f) {
            mCpuAngleOffset -= 0.06981317f;
        } else if (mCpuAngleOffset < -0.06981317f) {
            mCpuAngleOffset += 0.06981317f;
        } else {
            mCpuAngleOffset = 0.0f;
        }
    } else if (predict.x > RPHkyField::sWidth - RPHkyField::cCorner - sRectLength) {
        if (mCpuSpinTimer == 0) {
            mCpuAngleOffset += mPos.z > 0.0f ? 0.06981317f : -0.06981317f;
        }

        if (mCpuAngleOffset > 6.2831855f) {
            mCpuAngleOffset -= 6.2831855f;
        } else if (mCpuAngleOffset < -6.2831855f) {
            mCpuAngleOffset += 6.2831855f;
        }

        if (predict.x < mPos.x && mCpuSpinTimer == 0) {
            mCpuSpinTimer = 1;
        }
    } else if (mCpuAngleOffset != 0.0f) {
        if (mCpuSpinTimer == 0) {
            mCpuAngleOffset += mPos.z > 0.0f ? 0.06981317f : -0.06981317f;
        }

        if (mCpuAngleOffset > 6.2831855f) {
            mCpuAngleOffset = 0.0f;
        } else if (mCpuAngleOffset < -6.2831855f) {
            mCpuAngleOffset = 0.0f;
        }

        target.x = mTarget.x;
        target.z = mTarget.z;
    }

    if (target.x > RPHkyField::sWidth - sRectWidth) {
        target.x = RPHkyField::sWidth - sRectWidth;
    }

    if (target.z > RPHkyField::cHalfDepth - sRectLength) {
        target.z = RPHkyField::cHalfDepth - sRectLength;
    } else if (target.z < -RPHkyField::cHalfDepth + sRectLength) {
        target.z = -RPHkyField::cHalfDepth + sRectLength;
    }

    angle += mCpuAngleOffset;
    mTarget = target;

    if (mCpuSpinTimer != 0 && ++mCpuSpinTimer > 60) {
        mCpuSpinTimer = 0;
    }

    return angle;
}

static inline bool NormalizeAxisX(RPHkyPaddle* pPaddle) {
    f32 len = nw4r::math::VEC3Len(&pPaddle->GetAxisX());
    if (len == 0.0f) {
        return false;
    }

    f32 inv = 1.0f / len;
    pPaddle->GetAxisX().x *= inv;
    pPaddle->GetAxisX().y *= inv;
    pPaddle->GetAxisX().z *= inv;
    return true;
}

inline void RPHkyPaddle::UpdatePosHistory() {
    nw4r::math::VEC3 move = mPos - mPrevPos;
    for (int i = 0; i < 4; i++) {
        mPosHistory[i] = mPosHistory[i + 1];
    }
    mPosHistory[4] = move;
    mPrevPos = mPos;
    mHitTimer++;
}

// Wii Play: limits the remote's tilt to 8 degrees of turn per frame
static inline f32 LimitTilt(f32 angle, f32 tilt) {
    f32 up = angle + 0.13962634f;
    f32 down = angle - 0.13962634f;
    nw4r::math::VEC3 upDir(std::cosf(up), 0.0f, std::sinf(up));
    nw4r::math::VEC3 downDir(std::cosf(down), 0.0f, std::sinf(down));
    nw4r::math::VEC3 tiltDir(std::cosf(tilt), 0.0f, std::sinf(tilt));

    if (nw4r::math::VEC3Dot(&upDir, &tiltDir) > nw4r::math::VEC3Dot(&downDir, &tiltDir)) {
        if (nw4r::math::VEC3Dot(&upDir, &tiltDir) > 0.99f) {
            return tilt;
        }
        return up;
    } else {
        if (nw4r::math::VEC3Dot(&downDir, &tiltDir) > 0.99f) {
            return tilt;
        }
        return down;
    }
}

inline void RPHkyPaddle::UpdateAngleHistory(f32 angle) {
    mAngle = angle;
    for (int i = 0; i < 3; i++) {
        mAngleHistory[i] = mAngleHistory[i + 1];
    }
    mAngleHistory[3] = mAngleDelta;
    mAngleDelta = angle - mPrevAngle;
    if (mAngleDelta <= -6.2831855f) {
        mAngleDelta += 6.2831855f;
    } else if (mAngleDelta >= 6.2831855f) {
        mAngleDelta -= 6.2831855f;
    }
    mPrevAngle = angle * sAngleScale;
}

// Wii Play: bind the width load to r6; CodeWarrior removes the following register copy.
extern "C" u16 sTVModeWidths__Q23EGG6Screen[EGG::Screen::TV_MODE_MAX];

static inline u16 PointerScreenWidth() {
    register u16 width;
    register u16* table = sTVModeWidths__Q23EGG6Screen;
    register u32 offset = (u32)EGG::Screen::GetTVMode() * 2;
    ASM_PS(PointerScreenWidthLoad) (lhzx r6, table, offset)
    ASM_PS(PointerScreenWidthMove) (mr width, r6)
    return width;
}

void RPHkyPaddle::UpdateControl() {
    UpdatePosHistory();
    u32 soundId = mPlayerIndex == 0 ? 0x10 : 0x11;
    mpTrail->HoldSoundBySpeed(soundId, nw4r::math::VEC3Len(&mPosHistory[4]));

    f32 angle = 0.0f;
    if (IsCpu()) {
        UpdateCpuStallTimer();

        switch (mShapeType) {
        case Shape_Round:
            angle = CalcCpuTargetRound();
            break;
        case Shape_Rect:
            angle = CalcCpuTargetRect();
            break;
        }
    } else {
        EGG::CoreStatus* pStatus =
            EGG::CoreControllerMgr::instance()->getNthController(mPlayerIndex)->getCoreStatus();

        BOOL valid = FALSE;
        nw4r::math::VEC3 target;
        if (pStatus->getDPDValidFlag() > 0 && RPHkyGame::GetInstance()->GetState() != 5) {
            const RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(0)->GetScreen();
            f32 negY = -pStatus->pos.y;
            f32 x = pStatus->pos.x;
            x *= (f32)(u16)(PointerScreenWidth() / 2);
            nw4r::math::VEC3 planePoint(0.0f, 0.0f, 0.0f);
            nw4r::math::VEC3 planeNormal(0.0f, 1.0f, 0.0f);
            f32 y = negY * (f32)(u16)(EGG::Screen::GetSizeYMax() / 2);
            valid = RPHkyGame::GetInstance()->GetCamera()->GetScreenToPlane(
                &target, planePoint, planeNormal, pScreen, x, y);

            target.x *= 0.5f;
            if (mPlayerIndex == 0) {
                target.x -= RPHkyField::sWidth / 2.0f - (RPHkyField::cCorner - RPHkyField::cGoalInset);
            } else {
                target.x += RPHkyField::sWidth / 2.0f - (RPHkyField::cCorner - RPHkyField::cGoalInset);
            }
            target.z *= sPointerDepthScale[0];
        }

        if (valid) {
            mTarget = target;
        }

        f32 tilt = std::atan2(pStatus->getHorizon().y, pStatus->getHorizon().x);
        tilt = LimitTilt(mAngle, tilt);

        UpdateAngleHistory(tilt);
        angle = tilt * sAngleScale;
    }

    if (RPHkyGame::GetInstance()->GetState() == 5 && !IsCpu()) {
        return;
    }

    GetAxisX().x = std::cosf(angle);
    GetAxisX().y = 0.0f;
    GetAxisX().z = std::sinf(angle);
    if (!NormalizeAxisX(this)) {
        GetAxisX().x = 1.0f;
        GetAxisX().y = 0.0f;
        GetAxisX().z = 0.0f;
    }
    PSVECCrossProduct(GetAxisX(), GetAxisY(), GetAxisZ());
    PSVECNormalize(GetAxisZ(), GetAxisZ());
    PSVECCrossProduct(GetAxisZ(), GetAxisX(), GetAxisY());
}

void RPHkyPaddle::ApplyVelocity() {
    ResetPushes();

    if (RPHkyGame::GetInstance()->GetState() == 5 && !IsCpu()) {
        return;
    }

    mPos.x += mVelocity.x;
    mPos.z += mVelocity.z;
}

void RPHkyPaddle::CollideField() {
    switch (mShapeType) {
    case Shape_Round:
        CollideFieldRound();
        break;
    case Shape_Rect:
        CollideFieldRect();
        break;
    }
}

void RPHkyPaddle::CollideFieldRound() {
    RPHkyField* pField = RPHkyGame::GetInstance()->GetField();
    pField->ResetPaddleWallIter();

    nw4r::math::VEC3 normal;
    f32 depth;
    while (pField->TestCircleCollision(sRoundRadius, mPos, &normal, &depth)) {
        nw4r::math::VEC3 push;
        push.x = depth * normal.x;
        push.y = 0.0f;
        push.z = depth * normal.z;
        AddPush(push);

        f32 speed = -mVelocity.x * normal.x - mVelocity.z * normal.z;
        if (speed <= 0.0f) {
            continue;
        }

        push.x = speed * normal.x;
        push.y = 0.0f;
        push.z = speed * normal.z;
        AddVelocityPush(push);
        RequestRumble(1, 6);
    }
}

void RPHkyPaddle::CollideFieldRect() {
    nw4r::math::VEC3 dir(-mRot._20, 0.0f, -mRot._22);
    f32 halfWidth = 0.5f * sRectWidth;
    f32 halfLength = 0.5f * sRectLength;

    RPHkyField* pField = RPHkyGame::GetInstance()->GetField();
    pField->ResetPaddleWallIter();

    nw4r::math::VEC3 normal;
    f32 depth;
    while (pField->TestRectCollision(halfLength, halfWidth, mPos, dir, &normal, &depth)) {
        nw4r::math::VEC3 push;
        push.x = depth * normal.x;
        push.y = 0.0f;
        push.z = depth * normal.z;
        AddPush(push);

        f32 speed = -mVelocity.x * normal.x - mVelocity.z * normal.z;
        if (speed <= 0.0f) {
            continue;
        }

        push.x = speed * normal.x;
        push.y = 0.0f;
        push.z = speed * normal.z;
        AddVelocityPush(push);
        RequestRumble(1, 6);
    }
}

void RPHkyPaddle::UpdateMovement() {
    mVelocity.x *= sVelDamping;
    mVelocity.z *= sVelDamping;

    switch (mHitTimer) {
    case 0:
        mVelocity.x += 0.1f * (sCpuGain * (mTarget.x - mPos.x));
        mVelocity.z += 0.1f * (sCpuGain * (mTarget.z - mPos.z));
        break;
    case 1:
        mVelocity.x += 0.2f * (sCpuGain * (mTarget.x - mPos.x));
        mVelocity.z += 0.2f * (sCpuGain * (mTarget.z - mPos.z));
        break;
    case 2:
        mVelocity.x += 0.3f * (sCpuGain * (mTarget.x - mPos.x));
        mVelocity.z += 0.3f * (sCpuGain * (mTarget.z - mPos.z));
        break;
    case 3:
        mVelocity.x += 0.4f * (sCpuGain * (mTarget.x - mPos.x));
        mVelocity.z += 0.4f * (sCpuGain * (mTarget.z - mPos.z));
        break;
    case 4:
        mVelocity.x += 0.5f * (sCpuGain * (mTarget.x - mPos.x));
        mVelocity.z += 0.5f * (sCpuGain * (mTarget.z - mPos.z));
        break;
    default:
        mVelocity.x += sCpuGain * (mTarget.x - mPos.x);
        mVelocity.z += sCpuGain * (mTarget.z - mPos.z);
        break;
    }

    nw4r::math::VEC3 velPush;
    nw4r::math::VEC3 push;
    PSVECAdd(mVelPushMin, mVelPushMax, velPush);
    PSVECAdd(mVelocity, velPush, mVelocity);
    PSVECAdd(mPushMin, mPushMax, push);
    PSVECAdd(mPos, push, mPos);

    mPrevPos2 = mPrevPos1;
    mPrevPos1 = mPos;

    if (RPHkyGame::GetInstance()->IsMultiPlayer() == TRUE || mPlayerIndex == 0) {
        if (mVelocity.x * mVelocity.x + mVelocity.z * mVelocity.z >= sMaxSpeed * sMaxSpeed) {
            f32 scale = sMaxSpeed / std::sqrtf(mVelocity.x * mVelocity.x +
                                               mVelocity.z * mVelocity.z);
            mVelocity.x *= scale;
            mVelocity.z *= scale;
        }
    } else {
        if (mVelocity.x * mVelocity.x + mVelocity.z * mVelocity.z >= sCpuMaxSpeed * sCpuMaxSpeed) {
            f32 scale = sCpuMaxSpeed / std::sqrtf(mVelocity.x * mVelocity.x +
                                                  mVelocity.z * mVelocity.z);
            mVelocity.x *= scale;
            mVelocity.z *= scale;
        }
    }

    f32 limit = RPHkyField::sWidth - RPHkyField::cCorner + RPHkyField::cGoalInset;
    f32 halfWidth;
    if (mShapeType == Shape_Round) {
        halfWidth = sRoundRadius;
    } else {
        f32 angleA = std::atan2f(sRectLength, sRectWidth);
        f32 angleB = std::atan2f(-sRectLength, sRectWidth);
        f32 radius = std::sqrtf(sRectWidth * sRectWidth + sRectLength * sRectLength);
        radius *= 0.5f;
        f32 a = std::fabs(radius * std::cosf(mAngle * sAngleScale + angleA));
        f32 b = std::fabs(radius * std::cosf(mAngle * sAngleScale + angleB));
        halfWidth = MaxRef(a, b);
    }

    if (mPlayerIndex == 0) {
        if (mPos.x > -halfWidth) {
            mPos.x = -halfWidth;
            RequestRumble(3, 4);
        } else if (mPos.x < -limit) {
            mPos.x = -limit;
        }

        if (mTarget.x > 0.0f) {
            mTarget.x = 0.0f;
        } else if (mTarget.x < -limit) {
            mTarget.x = -limit;
        }
    } else {
        if (mPos.x < halfWidth) {
            mPos.x = halfWidth;
            RequestRumble(3, 4);
        } else if (mPos.x > limit) {
            mPos.x = limit;
        }

        if (mTarget.x < 0.0f) {
            mTarget.x = 0.0f;
        } else if (mTarget.x > limit) {
            mTarget.x = limit;
        }
    }

    f32 depthLimit = RPHkyField::cHalfDepth - RPHkyField::cCorner;
    if (mPos.z < -depthLimit) {
        mPos.z = -depthLimit;
    } else if (mPos.z > depthLimit) {
        mPos.z = depthLimit;
    }

    if (mTarget.z < -depthLimit) {
        mTarget.z = -depthLimit;
    } else if (mTarget.z > depthLimit) {
        mTarget.z = depthLimit;
    }
}

void RPHkyPaddle::UpdateRumble() {
    for (int i = 0; i < 4; i++) {
        if (mRumbleState[i] == 1) {
            switch (i) {
            case 0:
                StartControllerRumble(mRumbleFrames[i]);
                mpModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_Clr, 0, 0.0f);
                RPHkyGame::GetInstance()->GetPaddleRound(mPlayerIndex)->RestartHitAnim();
                break;
            case 1:
                StartControllerRumble(mRumbleFrames[i]);
                break;
            case 2:
                StartControllerRumble(mRumbleFrames[i]);
                break;
            case 3:
                StartControllerRumble(mRumbleFrames[i]);
                break;
            }
        }

        if (mRumbleState[i] != 0) {
            if (++mRumbleState[i] >= sRumbleFrames) {
                mRumbleState[i] = 0;
            }
        }
    }
}

void RPHkyPaddle::CollidePaddle(RPHkyPaddle* pOther) {
    switch (mShapeType) {
    case Shape_Round:
        CollideRoundVsPaddle(pOther);
        break;
    case Shape_Rect:
        CollideRectVsPaddle(pOther);
        break;
    }
}

void RPHkyPaddle::CollideRoundVsPaddle(RPHkyPaddle* pOther) {
    nw4r::math::VEC3 diff;
    diff.x = mPos.x - pOther->mPos.x;
    diff.y = 0.0f;
    diff.z = mPos.z - pOther->mPos.z;

    f32 radius = mShapeType == Shape_Round ? sRoundRadius : sRectWidth;
    radius += pOther->mShapeType == Shape_Round ? sRoundRadius : sRectWidth;
    if (diff.x * diff.x + diff.z * diff.z >= radius * radius) {
        return;
    }

    f32 dist = std::sqrtf(diff.x * diff.x + diff.z * diff.z);
    f32 scale = 0.5f * (radius - dist) / dist;
    nw4r::math::VEC3 push;
    push.x = diff.x * scale;
    push.y = 0.0f;
    push.z = diff.z * scale;
    AddPush(push);

    push.x = -push.x;
    push.z = -push.z;
    pOther->AddPush(push);
}

/**
 * @brief Pushes both paddles apart along the edge normal and rumbles both
 */
inline void RPHkyPaddle::PushApart(RPHkyPaddle* pOther, const nw4r::math::VEC3& rNormal,
                                   f32 depth) {
    depth *= 0.5f;
    nw4r::math::VEC3 push;
    push.x = rNormal.x * depth;
    push.y = 0.0f;
    push.z = rNormal.z * depth;
    AddPush(push);

    push.x = -push.x;
    push.z = -push.z;
    pOther->AddPush(push);

    RequestRumble(2, 8);
    RPHkyGame::GetInstance()->GetPaddle(pOther->mPlayerIndex)->RequestRumble(2, 8);
}

void RPHkyPaddle::CollideRectVsPaddle(RPHkyPaddle* pOther) {
    nw4r::math::_VEC3 dir = {0.0f, 0.0f, 0.0f};
    dir.x = -mRot._20;
    dir.z = -mRot._22;
    f32 halfWidth = 0.5f * sRectWidth;
    f32 halfLength = 0.5f * sRectLength;

    RPHkySegment* pEdge = &pOther->mEdges[3];
    do {
        nw4r::math::VEC3 edgeDir;
        edgeDir.x = pOther->mRot._00 * pEdge->mDir.x + pOther->mRot._20 * pEdge->mDir.z;
        edgeDir.z = pOther->mRot._02 * pEdge->mDir.x + pOther->mRot._22 * pEdge->mDir.z;
        nw4r::math::VEC3 edgePos;
        edgePos.x =
            pOther->mRot._00 * pEdge->mPos.x + pOther->mPos.x + pOther->mRot._20 * pEdge->mPos.z;
        edgePos.z =
            pOther->mRot._02 * pEdge->mPos.x + pOther->mPos.z + pOther->mRot._22 * pEdge->mPos.z;

        f32 along = dir.x * edgeDir.x + dir.z * edgeDir.z;
        f32 across = -dir.z * edgeDir.x + dir.x * edgeDir.z;

        // Corner of this paddle that is deepest towards the edge (local width, length)
        f32 cornerL;
        f32 cornerW;
        if (along == 0.0f) {
            nw4r::math::VEC3 rel;
            rel.x = mPos.x - edgePos.x;
            rel.z = mPos.z - edgePos.z;
            if (rel.x * edgeDir.x + rel.z * edgeDir.z >= 0.5f * pEdge->mLength) {
                if (across >= 0.0f) {
                    cornerW = halfWidth;
                    cornerL = halfLength;
                } else {
                    cornerW = -halfWidth;
                    cornerL = -halfLength;
                }
            } else if (across >= 0.0f) {
                cornerW = -halfWidth;
                cornerL = halfLength;
            } else {
                cornerW = halfWidth;
                cornerL = -halfLength;
            }
        } else if (across == 0.0f) {
            nw4r::math::VEC3 rel;
            rel.x = mPos.x - edgePos.x;
            rel.z = mPos.z - edgePos.z;
            if (rel.x * edgeDir.x + rel.z * edgeDir.z >= 0.5f * pEdge->mLength) {
                if (along >= 0.0f) {
                    cornerW = -halfWidth;
                    cornerL = halfLength;
                } else {
                    cornerW = halfWidth;
                    cornerL = -halfLength;
                }
            } else if (along >= 0.0f) {
                cornerW = -halfWidth;
                cornerL = -halfLength;
            } else {
                cornerW = halfWidth;
                cornerL = halfLength;
            }
        } else {
            cornerW = along >= 0.0f ? -halfWidth : halfWidth;
            cornerL = across >= 0.0f ? halfLength : -halfLength;
        }

        nw4r::math::VEC3 corner;
        corner.x = mPos.x + dir.x * cornerL - dir.z * cornerW;
        corner.y = 0.0f;
        corner.z = mPos.z + dir.z * cornerL + dir.x * cornerW;
        nw4r::math::VEC3 rel;
        rel.x = corner.x - edgePos.x;
        rel.y = 0.0f;
        rel.z = corner.z - edgePos.z;
        nw4r::math::VEC3 normal;
        normal.x = -edgeDir.z;
        normal.y = 0.0f;
        normal.z = edgeDir.x;

        f32 dist = normal.x * rel.x + normal.z * rel.z;
        if (dist >= 0.0f) {
            continue;
        }

        f32 t = edgeDir.x * rel.x + edgeDir.z * rel.z;
        if (t >= pEdge->mLength) {
            // Past the edge's end: test the neighbouring corner
            if (cornerW >= 0.0f) {
                if (cornerL >= 0.0f) {
                    cornerW = -cornerW;
                } else {
                    cornerL = -cornerL;
                }
            } else {
                bool front = cornerL >= 0.0f;
                if (front) {
                    cornerL = -cornerL;
                }
                if (!front) {
                    cornerW = -cornerW;
                }
            }

            nw4r::math::VEC3 corner2;
            corner2.x = mPos.x + dir.x * cornerL - dir.z * cornerW;
            corner2.y = 0.0f;
            corner2.z = mPos.z + dir.z * cornerL + dir.x * cornerW;
            nw4r::math::VEC3 rel2;
            rel2.x = corner2.x - edgePos.x;
            rel2.z = corner2.z - edgePos.z;

            f32 dist2 = normal.x * rel2.x + normal.z * rel2.z;
            if (dist2 <= -8.0f) {
                continue;
            }

            f32 t2 = edgeDir.x * rel2.x + edgeDir.z * rel2.z;
            if (t2 >= pEdge->mLength) {
                continue;
            }

            if (dist2 >= 0.0f) {
                nw4r::math::VEC3 side;
                side.x = corner2.x - corner.x;
                side.z = corner2.z - corner.z;
                f32 len = LengthXZ(side);
                if (len == 0.0f) {
                    continue;
                }
                f32 inv = 1.0f / len;
                side.x *= inv;
                side.z *= inv;
                if (side.x * edgeDir.x + side.z * edgeDir.z > -0.7071068f) {
                    continue;
                }

                nw4r::math::VEC3 toStart;
                nw4r::math::VEC3 toEnd;
                nw4r::math::VEC3 perp;
                toStart.x = -rel.x;
                toStart.z = -rel.z;
                perp.x = -side.z;
                perp.z = side.x;
                toEnd.x = edgeDir.x * pEdge->mLength + toStart.x;
                toEnd.z = edgeDir.z * pEdge->mLength + toStart.z;
                f32 startDot = toStart.x * perp.x + toStart.z * perp.z;
                f32 endDot = toEnd.x * perp.x + toEnd.z * perp.z;
                if (startDot * endDot >= 0.0f) {
                    continue;
                }

                PushApart(pOther, normal,
                          (dist2 - dist) * (pEdge->mLength - t2) / (t - t2) - dist2);
            } else {
                if (pEdge->mLength - t2 < -dist2) {
                    continue;
                }

                nw4r::math::VEC3 side;
                side.x = corner2.x - corner.x;
                side.z = corner2.z - corner.z;
                f32 len = std::sqrtf(side.x * side.x + side.z * side.z);
                if (len == 0.0f) {
                    continue;
                }
                f32 inv = 1.0f / len;
                side.x *= inv;
                side.z *= inv;
                if (side.x * edgeDir.x + side.z * edgeDir.z > -0.7071068f) {
                    continue;
                }

                PushApart(pOther, normal,
                          -dist - (dist2 - dist) * (t - pEdge->mLength) / (t - t2));
            }
        } else if (t <= 0.0f) {
            // Before the edge's start: test the other neighbouring corner
            if (cornerW >= 0.0f) {
                if (cornerL >= 0.0f) {
                    cornerL = -cornerL;
                } else {
                    cornerW = -cornerW;
                }
            } else {
                bool front = cornerL >= 0.0f;
                if (front) {
                    cornerW = -cornerW;
                }
                if (!front) {
                    cornerL = -cornerL;
                }
            }

            nw4r::math::VEC3 corner2;
            corner2.x = mPos.x + dir.x * cornerL - dir.z * cornerW;
            corner2.y = 0.0f;
            corner2.z = mPos.z + dir.z * cornerL + dir.x * cornerW;
            nw4r::math::VEC3 rel2;
            rel2.x = corner2.x - edgePos.x;
            rel2.z = corner2.z - edgePos.z;

            f32 dist2 = normal.x * rel2.x + normal.z * rel2.z;
            if (dist2 <= -8.0f) {
                continue;
            }

            f32 t2 = edgeDir.x * rel2.x + edgeDir.z * rel2.z;
            if (t2 <= 0.0f) {
                continue;
            }

            if (dist2 >= 0.0f) {
                nw4r::math::VEC3 side;
                side.x = corner2.x - corner.x;
                side.z = corner2.z - corner.z;
                f32 len = LengthXZ(side);
                if (len == 0.0f) {
                    continue;
                }
                f32 inv = 1.0f / len;
                side.x *= inv;
                side.z *= inv;
                if (side.x * edgeDir.x + side.z * edgeDir.z < 0.7071068f) {
                    continue;
                }

                nw4r::math::VEC3 toStart;
                nw4r::math::VEC3 toEnd;
                nw4r::math::VEC3 perp;
                toStart.x = -rel.x;
                toStart.z = -rel.z;
                perp.x = -side.z;
                perp.z = side.x;
                toEnd.x = edgeDir.x * pEdge->mLength + toStart.x;
                toEnd.z = edgeDir.z * pEdge->mLength + toStart.z;
                f32 startDot = toStart.x * perp.x + toStart.z * perp.z;
                f32 endDot = toEnd.x * perp.x + toEnd.z * perp.z;
                if (startDot * endDot >= 0.0f) {
                    continue;
                }

                PushApart(pOther, normal, t2 * (dist2 - dist) / (t2 - t) - dist2);
            } else {
                if (t2 < -dist2) {
                    continue;
                }

                nw4r::math::VEC3 side;
                side.x = corner2.x - corner.x;
                side.z = corner2.z - corner.z;
                f32 len = std::sqrtf(side.x * side.x + side.z * side.z);
                if (len == 0.0f) {
                    continue;
                }
                f32 inv = 1.0f / len;
                side.x *= inv;
                side.z *= inv;
                if (side.x * edgeDir.x + side.z * edgeDir.z < 0.7071068f) {
                    continue;
                }

                PushApart(pOther, normal, -dist - t * (dist2 - dist) / (t - t2));
            }
        } else {
            if (dist <= -8.0f) {
                continue;
            }

            PushApart(pOther, normal, -dist);
        }
    } while (--pEdge >= pOther->mEdges);
}

void RPHkyPaddle::ResetEdgeIter() {
    mpEdgeIter = &mEdges[3];
}

BOOL RPHkyPaddle::TestPuckCollision(f32 radius, const nw4r::math::VEC3& rPos,
                                    nw4r::math::VEC3* pNormal, f32* pDepth) {
    BOOL hit = FALSE;

    switch (mShapeType) {
    case Shape_Round:
        hit = CollideRound(radius, rPos, pNormal, pDepth);
        break;
    case Shape_Rect:
        hit = CollideRectEdges(radius, rPos, pNormal, pDepth);
        break;
    }

    if (hit) {
        mHitTimer = 0;
    }

    return hit;
}

BOOL RPHkyPaddle::CollideRound(f32 radius, const nw4r::math::VEC3& rPos,
                               nw4r::math::VEC3* pNormal, f32* pDepth) {
    if (mpEdgeIter == NULL) {
        return FALSE;
    }
    mpEdgeIter = NULL;

    nw4r::math::VEC3 diff;
    diff.x = rPos.x - mPos.x;
    diff.y = 0.0f;
    diff.z = rPos.z - mPos.z;
    f32 r = sRoundRadius + radius;
    if (diff.x * diff.x + diff.z * diff.z >= r * r) {
        return FALSE;
    }

    f32 dist = std::sqrtf(diff.x * diff.x + diff.z * diff.z);
    *pDepth = r - dist;
    f32 inv = 1.0f / dist;
    pNormal->x = diff.x * inv;
    pNormal->y = 0.0f;
    pNormal->z = diff.z * inv;

    nw4r::math::VEC3 vel = CalcHitVelocity(this);
    if (LengthXZ(vel) >= 0.01f) {
        vel.x = mPlayerIndex == 0 ? 1.0f / 3.0f : -1.0f / 3.0f;
        vel.y = 0.0f;
        vel.z = 0.0f;
        nw4r::math::VEC3 normal;
        normal = *pNormal;
        normal = normal + vel;

        f32 len = std::sqrtf(normal.x * normal.x + normal.z * normal.z);
        if (len >= 0.01f) {
            pNormal->x = normal.x / len;
            pNormal->y = 0.0f;
            pNormal->z = normal.z / len;
        }
    }

    return TRUE;
}

BOOL RPHkyPaddle::CollideRectEdges(f32 radius, const nw4r::math::VEC3& rPos,
                                   nw4r::math::VEC3* pNormal, f32* pDepth) {
    if (mpEdgeIter < mEdges) {
        return FALSE;
    }

    do {
        RPHkySegment* pEdge = mpEdgeIter;

        nw4r::math::VEC3 start;
        start.x = pEdge->mPos.x * mRot._00 + mPos.x + pEdge->mPos.z * mRot._20;
        start.z = pEdge->mPos.x * mRot._02 + mPos.z + pEdge->mPos.z * mRot._22;
        nw4r::math::VEC3 dir;
        dir.x = mRot._00 * pEdge->mDir.x + mRot._20 * pEdge->mDir.z;
        dir.z = mRot._02 * pEdge->mDir.x + mRot._22 * pEdge->mDir.z;

        nw4r::math::VEC3 normal;
        normal.x = -dir.z;
        normal.y = 0.0f;
        normal.z = dir.x;
        nw4r::math::VEC3 rel;
        rel.x = rPos.x - start.x;
        rel.y = 0.0f;
        rel.z = rPos.z - start.z;

        f32 dist = normal.x * rel.x + normal.z * rel.z;
        if (dist >= radius || dist <= -8.0f) {
            continue;
        }

        f32 t = dir.x * rel.x + dir.z * rel.z;
        if (t >= pEdge->mLength + radius || t <= -radius) {
            continue;
        }

        if (0.0f <= t) {
            if (t <= pEdge->mLength) {
                *pNormal = normal;
                *pDepth = radius - dist;
                mpEdgeIter--;
                return TRUE;
            }

            f32 over = t - pEdge->mLength;
            if (over > dist) {
                continue;
            }

            f32 overSq = over * over;
            f32 radiusSq = radius * radius;
            if (dist * dist + overSq >= radiusSq) {
                continue;
            }

            *pDepth = std::sqrtf(radiusSq - overSq) - dist;
            *pNormal = normal;
            mpEdgeIter--;
            return TRUE;
        }

        if (-t >= dist) {
            continue;
        }

        f32 tSq = t * t;
        f32 radiusSq = radius * radius;
        if (dist * dist + tSq >= radiusSq) {
            continue;
        }

        *pDepth = std::sqrtf(radiusSq - tSq) - dist;
        *pNormal = normal;
        mpEdgeIter--;
        return TRUE;
    } while (--mpEdgeIter >= mEdges);

    return FALSE;
}

void RPHkyPaddle::RequestRumble(s32 idx, s32 frames) {
    switch (mRumbleState[idx]) {
    case 0:
        mRumbleState[idx] = 1;
        mRumbleFrames[idx] = frames;
        break;
    case 1:
        break;
    default:
        mRumbleState[idx] = 2;
        break;
    }
}

void RPHkyPaddle::SetShape(s32 shape) {
    mShapeType = shape;

    switch (shape) {
    case Shape_Rect:
        sRectLength = 91.0f;
        SetRectEdges();
        mpEffect->setDisableCalcDraw(false);
        RPHkyGame::GetInstance()->GetPaddleRound(mPlayerIndex)->GetEffect()->setDisableCalcDraw(
            true);
        break;
    case Shape_Round:
        sRoundRadius = 35.19f;
        mpEffect->setDisableCalcDraw(true);
        RPHkyGame::GetInstance()->GetPaddleRound(mPlayerIndex)->GetEffect()->setDisableCalcDraw(
            false);
        break;
    }
}

nw4r::math::VEC3 RPHkyPaddle::CalcHitVelocity(RPHkyPaddle* pPaddle) {
    nw4r::math::VEC3 vel(sVecZero);
    for (int i = 0; i < 5; i++) {
        vel += pPaddle->mPosHistory[i];
    }
    vel *= 1.0f / (RPHkyGame::GetInstance()->GetSubSteps() * 5);
    return vel;
}

s32 RPHkyPaddle::GetHitEdgeIndex() const {
    // Seed the original literal-pool order without emitting instructions.
    f32 zero = 0.0f;
    f32 height = 5.0f;
    for (int i = 0; i < 4; i++) {
        if (&mEdges[i] == mpEdgeIter + 1) {
            return i;
        }
    }

    return -1;
}
