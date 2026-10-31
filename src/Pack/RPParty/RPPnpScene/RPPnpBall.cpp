// Table Tennis ball: integration, rally state machine, trajectory prediction, tail effects and the
// shadow; also the sound actors' destructors. Unit 0x80225240-0x80226D04 (dtk/game_units.tsv).
// Functions in reverse address order.
// Wii Play: the sound object templates are complete before RPPnpBall and its sound actors are
// declared, so the weak vtables come after the three strong ones
#include <Pack/RPAudio/RPSndObject.h>
typedef char RPPnpBallSndGameObjectSize[sizeof(TRPSndGameObject<0>)];

#include <Pack/RPParty/RPPnpScene/RPPnpBall.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPPauseText.h>
#include <Pack/RPParty/RPPnpScene/RPPnpBgmCtrl.h>
#include <Pack/RPParty/RPPnpScene/RPPnpField.h>
#include <Pack/RPParty/RPPnpScene/RPPnpGame.h>
#include <Pack/RPParty/RPPnpScene/RPPnpHud.h>
#include <Pack/RPParty/RPPnpScene/RPPnpLevelTable.h>
#include <Pack/RPParty/RPPnpScene/RPPnpLyt.h>
#include <Pack/RPParty/RPPnpScene/RPPnpPhase.h>
#include <Pack/RPParty/RPPnpScene/RPPnpPlayer.h>
#include <Pack/RPParty/RPPnpScene/RPPnpSound.h>

#include <egg/math.h>

#include <cmath>

// Shadow volume pass of a scaled shape (801B84F0, owner unit unknown)
extern "C" void RP_drawShadowVolume(EGG::Matrix34f* pMtx);

nw4r::math::VEC3 RPPnpBall::sStartPos(60.0f, 126.0f, -130.0f);
nw4r::math::VEC3 RPPnpBall::sServeVelocity(0.0f, 15.0f, 0.0f);
nw4r::math::VEC3 RPPnpBall::sGravity(0.0f, -0.98f, 0.0f);
f32 RPPnpBall::sDrag = 1.1f;
s32 RPPnpBall::sDebugStartRally;

// The ball bounced on side's own half of the table (event 4: side 0's half, 5: side 1's)
static inline bool IsOwnHalf(s32 event, s32 side) {
    bool result = false;
    switch (side) {
    case 0:
        if (event == 4) {
            result = true;
        }
        break;
    case 1:
        if (event == 5) {
            result = true;
        }
        break;
    }
    return result;
}

// The ball bounced on the half opposite to side
static inline bool IsOtherHalf(s32 event, s32 side) {
    bool result = false;
    switch (side) {
    case 0:
        if (event == 5) {
            result = true;
        }
        break;
    case 1:
        if (event == 4) {
            result = true;
        }
        break;
    }
    return result;
}

RPPnpBall::~RPPnpBall() {}

RPPnpBallTrail::~RPPnpBallTrail() {}

RPPnpRacketTrail::~RPPnpRacketTrail() {}

void RPPnpBall::Create() {
    mpTrail = new RPPnpBallTrail();
    mpTailEffect = new RPSysEffect("Pnp_balltail", 0, 0);
    mpTailEffect2 = new RPSysEffect("Pnp_balltail02", 0, 0);
    mpTailGlowEffect = new RPSysEffect("Pnp_balltail02_glow", 0, 0);

    mHitCount = sDebugStartRally * 2;
    unk8C = 0;
    mRadius = 3.0f;
    mPos = sStartPos;
    mShadowScale = 2.2f;
    mPrevPos = mPos;
    mVelocity = sServeVelocity;
    mGravity = sGravity;
}

void RPPnpBall::Load() {
    RPGrpHandle file = RPSimpleModel::LoadResource("/G3D/pin_ball.brres");
    RPSimpleModel::LoadModel("pin_ball", file, 1, 0, 0);
    mpModel->SetUnkCFlags(2);
    mpModel->CreateBoundingInfo(1);
}

void RPPnpBall::Reset() {
    mHitCount = sDebugStartRally * 2;
    mRallyBonus = 0;
    mPos = mPrevPos = mVelocity = mGravity = sVecZero;
    mWillLandOnTable = false;
    mState = 0;
    mHitterSide = 2;
    mTargetSide = RPPnpGame::GetInstance()->GetPointPhase()->GetServerSide();
    mVisible = true;
    mHidden = false;
}

void RPPnpBall::Calculate() {
    // NONMATCHING-DEBT: RPSimpleModel::sMtxIdentity is declared VEC3[3]; this is one 9-word MTX33
    // block copy, which only an MTX33 source gives (as in RPWlyField_2.cpp). Fix with the
    // RPSimpleModel declaration (PartyCommon), not here.
    nw4r::math::MTX33 axes = *reinterpret_cast<const nw4r::math::MTX33*>(sMtxIdentity);
    nw4r::math::VEC3 dir = mPos - mPrevPos;

    if (PSVECMag(dir) > 0.0001f) {
        PSVECNormalize(dir, dir);
        axes.m[2][0] = dir.x;
        axes.m[2][1] = dir.y;
        axes.m[2][2] = dir.z;

        if (0.9999f < axes.m[2][1]) {
            axes.m[1][0] = 0.0f;
            axes.m[1][1] = 0.0f;
            axes.m[1][2] = -1.0f;
        } else if (-0.9999f > axes.m[2][1]) {
            axes.m[1][0] = 0.0f;
            axes.m[1][1] = 0.0f;
            axes.m[1][2] = 1.0f;
        } else {
            PSVECCrossProduct((Vec*)axes.m[1], (Vec*)axes.m[2], (Vec*)axes.m[0]);
            PSVECNormalize((Vec*)axes.m[0], (Vec*)axes.m[0]);
            PSVECCrossProduct((Vec*)axes.m[2], (Vec*)axes.m[0], (Vec*)axes.m[1]);
        }
    }

    mpTailEffect->setDisableDraw(!mVisible || mHidden);
    mpTailEffect2->setDisableDraw(!mHidden);
    mpTailGlowEffect->setDisableDraw(!mHidden);

    mEffectMtx._00 = axes.m[0][0];
    mEffectMtx._10 = axes.m[0][1];
    mEffectMtx._20 = axes.m[0][2];
    mEffectMtx._01 = axes.m[1][0];
    mEffectMtx._11 = axes.m[1][1];
    mEffectMtx._21 = axes.m[1][2];
    mEffectMtx._02 = axes.m[2][0];
    mEffectMtx._12 = axes.m[2][1];
    mEffectMtx._22 = axes.m[2][2];
    mEffectMtx._03 = mPos.x;
    mEffectMtx._13 = mPos.y;
    mEffectMtx._23 = mPos.z;

    mpTailEffect->setMtx(mEffectMtx);
    mpTailEffect->update();
    mpTailEffect2->setMtx(mEffectMtx);
    mpTailEffect2->update();
    mpTailGlowEffect->setMtx(mEffectMtx);
    mpTailGlowEffect->update();

    CalcModelMtx();
    mpTrail->set3DPosition(mPos);

    if (!RPPnpGame::GetInstance()->IsMultiPlayer()) {
        f32 speed;
        s32 level;
        RPPnpLevelTable::GetInstance()->LookupRally(mHitCount / 2, &speed, &level);
        RPPnpGame::GetInstance()->GetPlayer(1)->mCpuLevel = level;
        mSpeedScale = speed;
    } else {
        f32 speed;
        RPPnpLevelTable::GetInstance()->LookupRally(mHitCount / 2 + mRallyBonus, &speed, NULL);
        mSpeedScale = speed;
    }
}

void RPPnpBall::Exit() {}

void RPPnpBall::Integrate() {
    mPrevPos = mPos;

    f32 step = mVisible ? 1.0f : 0.5f;
    mVelocity.x += step * (mGravity.x * mSpeedScale);
    mVelocity.y += sDrag * (step * (mGravity.y * mSpeedScale));
    mVelocity.z += step * (mGravity.z * mSpeedScale);
    mPos.x += step * (mVelocity.x * mSpeedScale);
    mPos.y += step * (mVelocity.y * mSpeedScale);
    mPos.z += step * (mVelocity.z * mSpeedScale);

    f32 scale = 7.5f * mRadius;
    mScale.x = mScale.y = mScale.z = scale;
}

void RPPnpBall::DrawShadow() {
    if (RPGrpRenderer::GetCurrentViewNo() == 0 && RPGrpRenderer::GetDrawStep() == 9) {
        nw4r::math::VEC3 pos = mPos;
        RPPnpTable* pTable = RPPnpGame::GetInstance()->GetTable();

        f32 width = pTable->GetWidth();
        f32 length;
        if (-width / 2.0f <= pos.x && pos.x <= width / 2.0f &&
            (length = pTable->GetLength(), -length / 2.0f <= pos.z) && pos.z <= length / 2.0f &&
            pTable->GetHeight() <= pos.y) {
            pos.y = pTable->GetHeight();
        } else {
            pos.y = 0.0f;
        }

        f32 scale = 1.0f - (0.3f * ((mPos.y - GetRadius()) - pos.y)) / pTable->GetHeight();
        if (scale < 0.5f) {
            scale = 0.5f;
        } else if (scale > 1.0f) {
            scale = 1.0f;
        }

        f32 size = scale * (2.0f * GetRadius());
        EGG::Vector3f scl(size, 0.3f, size);
        EGG::Vector3f trans(pos.x, pos.y, pos.z);
        EGG::Matrix34f mtx;
        mtx.makeST(scl, trans);
        RP_drawShadowVolume(&mtx);
    }
}

void RPPnpBall::ApplyReceiverSpin() {
    f32 sign = 0.0f;
    RPPnpPlayer* pPlayer;

    switch (mHitterSide) {
    case 0:
        sign = 1.0f;
        pPlayer = RPPnpGame::GetInstance()->GetPlayer(0);
        break;
    case 1:
        sign = -1.0f;
        pPlayer = RPPnpGame::GetInstance()->GetPlayer(1);
        break;
    default:
        return;
    }

    if (pPlayer->mIsCpu != TRUE && mPos.z * sign < 0.0f) {
        f32 limitX = std::fabs(mVelocity.x / 4.0f);
        f32 limitZ = std::fabs(mVelocity.z / 4.0f);
        nw4r::math::VEC3 spin = pPlayer->mSwingVecs[2];

        if (spin.x < -limitX) {
            spin.x = -limitX;
        } else if (spin.x > limitX) {
            spin.x = limitX;
        }

        if (spin.z < -limitZ) {
            spin.z = -limitZ;
        } else if (spin.z > limitZ) {
            spin.z = limitZ;
        }

        mVelocity.x += spin.x;
        mVelocity.z += spin.z;
        pPlayer->unk94 = 0;
    }
}

void RPPnpBall::OnEventState4(s32 event, s32 side) {
    switch (event) {
    case 4:
    case 5:
        if (IsOtherHalf(event, mHitterSide)) {
            mState = 6;
        } else {
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
            mState = 11;
        }
        break;
    case 7: {
        s32 opponent = RPPnpGame::GetInstance()->GetOpponentSide(side);
        RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(opponent);
        mState = 12;
        break;
    }
    case 6:
        mState = 5;
        break;
    default:
        RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
        mState = 11;
        break;
    }
}

void RPPnpBall::OnEventState7(s32 event, s32 side) {
    switch (event) {
    case 4:
    case 5:
        if (IsOtherHalf(event, mHitterSide)) {
            mState = 6;
        } else {
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
            mState = 11;
        }
        break;
    case 6:
        mState = 8;
        break;
    case 7: {
        s32 opponent = RPPnpGame::GetInstance()->GetOpponentSide(side);
        RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(opponent);
        mState = 12;
        break;
    }
    default:
        RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
        mState = 9;
        break;
    }
}

void RPPnpBall::OnEventState8(s32 event, s32 side) {
    switch (event) {
    case 4:
    case 5:
        if (IsOtherHalf(event, mHitterSide)) {
            mState = 6;
        } else {
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
            mState = 10;
        }
        break;
    case 6:
        mState = 8;
        break;
    case 7: {
        s32 opponent = RPPnpGame::GetInstance()->GetOpponentSide(side);
        RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(opponent);
        mState = 11;
        break;
    }
    default:
        RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
        mState = 10;
        break;
    }
}

void RPPnpBall::PrepareServe() {
    mPos = mPrevPos = mVelocity = mGravity = sVecZero;
    mState = 1;
    mHitterSide = 2;
    mTargetSide = RPPnpGame::GetInstance()->GetPointPhase()->GetServerSide();
    mVisible = true;
}

void RPPnpBall::TossServe(const nw4r::math::VEC3& rPos) {
    if (mState == 1) {
        RPPnpPointPhase* pPointPhase = RPPnpGame::GetInstance()->GetPointPhase();
        s32 points = pPointPhase->GetScore(0) + pPointPhase->GetScore(1);
        if (points > 0) {
            mRallyBonus = (mHitCount / 2 + mRallyBonus * points) / (points + 1);
        } else {
            mRallyBonus = 0;
        }

        mHitCount = sDebugStartRally * 2;
        unk8C = 0;
        mSpeedScale = 1.0f;
        mPos = rPos;
        mPrevPos = rPos;
        mVelocity = sServeVelocity;
        mGravity = sGravity;
        mState = 2;
        RPPnpGame::GetInstance()->SetState(RPPnpGame::STATE_RALLY);

        mpTailEffect->create();
        mpTailEffect2->create();
        mpTailGlowEffect->create();

        RPPnpHud::GetInstance()->GetServerLyt()->Hide();
        RPPnpGame::GetInstance()->GetBgmCtrl()->StartRallyBgm(
            RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        if (RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE) {
            RPPnpReadyPhase::FadeOutBgm();
        }

        RPCommon::GetInstance()->GetPauseText()->SetEnabled(1, false);
    }
}

void RPPnpBall::OnEvent(s32 event, s32 side) {
    switch (mState) {
    case 0:
    case 1:
        return;
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
        mpTailEffect->forceKill();
        mpTailEffect2->forceKill();
        mpTailGlowEffect->forceKill();
        unk8C++;
        break;
    case 2:
        if (event == 7) {
            if (mTargetSide == side) {
                mHitterSide = side;
                mTargetSide = RPPnpGame::GetInstance()->GetOpponentSide(side);
                mState = 3;
            } else {
                RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
                mState = 11;
            }
        } else {
            s32 opponent = RPPnpGame::GetInstance()->GetOpponentSide(mTargetSide);
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(opponent);
            mState = 11;
        }
        break;
    case 3:
        switch (event) {
        case 4:
        case 5:
            if (IsOwnHalf(event, mHitterSide)) {
                mState = 4;
                mHitCount++;
                if (!RPPnpGame::GetInstance()->IsMultiPlayer() && mHitCount % 2 == 0) {
                    RPPnpHud::GetInstance()->GetCountLyt()->SetCount(mHitCount / 2);
                }
            } else {
                RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
                mState = 11;
            }
            break;
        default:
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
            mState = 11;
            break;
        }
        break;
    case 4:
        OnEventState4(event, side);
        break;
    case 5:
        switch (event) {
        case 4:
        case 5:
            if (IsOtherHalf(event, mHitterSide)) {
                mState = 13;
            } else {
                RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
                mState = 10;
            }
            break;
        case 7: {
            s32 opponent = RPPnpGame::GetInstance()->GetOpponentSide(side);
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(opponent);
            mState = 11;
            break;
        }
        default:
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mTargetSide);
            mState = 10;
            break;
        }
        break;
    case 6:
        if (event == 7) {
            if (mTargetSide == side) {
                mHitterSide = side;
                mTargetSide = RPPnpGame::GetInstance()->GetOpponentSide(side);
                mState = 7;
                mHitCount++;
                if (!RPPnpGame::GetInstance()->IsMultiPlayer() && mHitCount % 2 == 0) {
                    RPPnpHud::GetInstance()->GetCountLyt()->SetCount(mHitCount / 2);
                }
            } else {
                s32 opponent = RPPnpGame::GetInstance()->GetOpponentSide(side);
                RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(opponent);
                mState = 11;
            }
        } else {
            RPPnpGame::GetInstance()->GetPointPhase()->AddPoint(mHitterSide);
            mState = 11;
        }
        break;
    case 7:
        OnEventState7(event, side);
        break;
    case 8:
        OnEventState8(event, side);
        break;
    }

    mWillLandOnTable = PredictTableLanding(&mLandingPos);

    switch (event) {
    case 0:
        mpTrail->StartHitSound(mHitCount, unk8C, 9, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 1:
        mpTrail->StartHitSound(mHitCount, unk8C, 11, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 2:
        mpTrail->StartHitSound(mHitCount, unk8C, 10, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 3:
        mpTrail->StartHitSound(mHitCount, unk8C, 12, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 4:
        mpTrail->StartHitSound(mHitCount, unk8C, 6, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 5:
        mpTrail->StartHitSound(mHitCount, unk8C, 7, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 6:
        mpTrail->StartHitSound(mHitCount, unk8C, 8, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
        break;
    case 7:
        if (side == 0) {
            if (!mVisible) {
                mpTrail->StartHitSound(mHitCount, unk8C, 2, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
            } else if (mHidden) {
                mpTrail->StartHitSound(mHitCount, unk8C, 4, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
            } else {
                mpTrail->StartHitSound(mHitCount, unk8C, 0, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
            }
        } else {
            if (!mVisible) {
                mpTrail->StartHitSound(mHitCount, unk8C, 3, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
            } else if (mHidden) {
                mpTrail->StartHitSound(mHitCount, unk8C, 5, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
            } else {
                mpTrail->StartHitSound(mHitCount, unk8C, 1, RPPnpGame::GetInstance()->IsMultiPlayer() == TRUE);
            }
        }
        break;
    }
}

bool RPPnpBall::PredictTableLanding(nw4r::math::VEC3* pLandingPos) {
    bool lands = false;

    switch (mState) {
    case 4:
    case 7: {
        RPPnpTable* pTable = RPPnpGame::GetInstance()->GetTable();
        f32 restHeight = pTable->GetBallRestHeight();
        nw4r::math::VEC3 pos = mPos;

        if (pos.y > restHeight) {
            nw4r::math::VEC3 vel = mVelocity;
            for (int i = 0; pos.y > restHeight && i < 100; i++) {
                f32 step = mVisible ? 1.0f : 0.5f;
                vel.x += step * (mGravity.x * mSpeedScale);
                vel.y += sDrag * (step * (mGravity.y * mSpeedScale));
                vel.z += step * (mGravity.z * mSpeedScale);
                pos.x += step * (vel.x * mSpeedScale);
                pos.y += step * (vel.y * mSpeedScale);
                pos.z += step * (vel.z * mSpeedScale);
            }

            f32 width = pTable->GetWidth();
            if (-width / 2.0f <= pos.x && pos.x <= width / 2.0f) {
                f32 length = pTable->GetLength();
                if (-length / 2.0f <= pos.z && pos.z <= length / 2.0f) {
                    lands = true;
                    *pLandingPos = pos;
                }
            }
        }
        break;
    }
    }

    return lands;
}
