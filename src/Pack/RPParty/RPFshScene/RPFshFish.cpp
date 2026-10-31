// Fishing fish (RPFshFish): swimming, biting, the fight, the jump out of the water and the
// catch view, with the TRPSndObject<0> copies its sound actor needs.
// Unit 0x801FF03C-0x80205A64 (dtk/game_units.tsv); the file continues with the four thunks of
// RPFshFish.cpp (0x80205A64-0x80205A84). Functions in reverse address order.
// The indexed TRPSndObject overloads use the inline getIndexedHandle* members here (weak
// @STRING@ literals at 0x80358AAC); see RPSndObject.h.
#define RP_SND_OBJECT_INDEXED_HANDLE_MEMBERS
#include <Pack/RPParty/RPFshScene/RPFshFish.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPGraphics/RPGrpDrawPathManager.h>
#include <Pack/RPGraphics/RPGrpDrawPathXluSnap.h>
#include <Pack/RPGraphics/RPGrpShadowTexture.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPParty/RPFshScene/RPFshBonusFish.h>
#include <Pack/RPParty/RPFshScene/RPFshCamera.h>
#include <Pack/RPParty/RPFshScene/RPFshFishMgr.h>
#include <Pack/RPParty/RPFshScene/RPFshPlayer.h>
#include <Pack/RPParty/RPFshScene/RPFshPlayerMgr.h>
#include <Pack/RPParty/RPFshScene/RPFshPointPopup.h>
#include <Pack/RPParty/RPFshScene/RPFshRod.h>
#include <Pack/RPParty/RPFshScene/RPFshScoreLayout.h>
#include <Pack/RPParty/RPFshScene/RPFshStage.h>
#include <Pack/RPParty/RPFshScene/RPFshStartLayout.h>
#include <Pack/RPParty/RPFshScene/RPFshTimer.h>
#include <Pack/RPParty/RPPartyCommon/RPCurve.h>
#include <Pack/RPParty/RPPartyCommon/RPInterpCamera.h>
#include <Pack/RPParty/RPPartyCommon/RPJointMtxBuffer.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPUtility.h>

#include <egg/math.h>

namespace {

inline f32 FishRandom(f32 max) {
    return max * RPUtlRandom::getF32();
}

#pragma fp_contract off
inline f32 FishRandomRange(f32 min, f32 max) {
    return min + (max - min) * RPUtlRandom::getF32();
}
#pragma fp_contract on

inline f32 FishInterpolate(const f32& min, const f32& max, f32 time, f32 frames) {
    return min + (time * (max - min)) / frames;
}

#pragma fp_contract off
inline f32 FishAngle(const EGG::Vector3f& from, const EGG::Vector3f& to) {
    f32 dotY = from.y * to.y;
    f32 dotX = __fmadds(from.x, to.x, dotY);
    f32 dot = __fmadds(from.z, to.z, dotX);
    EGG::Vector3f cross = from.cross(to);
    f32 length = EGG::Math<f32>::sqrt(
        __fmadds(cross.z, cross.z, __fmadds(cross.x, cross.x, cross.y * cross.y)));
    return EGG::Math<f32>::atan2(length, dot);
}
#pragma fp_contract on

} // namespace

// Wii Play: the TRPSndObject<N> member copies come from their definitions in RPSndObject.h.

namespace {

// RPActorBase keeps its position as a VEC3; the fish moves it with EGG vector math
inline EGG::Vector3f& AsVector3f(nw4r::math::VEC3& rVec) {
    return static_cast<EGG::Vector3f&>(rVec);
}

inline const EGG::Vector3f& AsVector3f(const nw4r::math::VEC3& rVec) {
    return static_cast<const EGG::Vector3f&>(rVec);
}

// An EGG vector built from a VEC3 element by element (float copies, not the word copy of
// EGG::Vector3f(const VEC3&))
struct FishVec3 : EGG::Vector3f {
    FishVec3(const nw4r::math::VEC3& rVec) : EGG::Vector3f(rVec.x, rVec.y, rVec.z) {}
};

// scale * vec: the scale is read after the vector expression is evaluated
inline nw4r::math::VEC3 operator*(f32 scale, const nw4r::math::VEC3& rVec) {
    return rVec * scale;
}

} // namespace

f32 RPFshFish::sSwimTurnRate = 0.2f;
f32 RPFshFish::sSwimAccel = 0.08f;
f32 RPFshFish::sApproachSpeed = 2.0f;
f32 RPFshFish::sApproachRate = 0.8f;
s32 RPFshFish::sApproachFrames = 60;
f32 RPFshFish::sMouthRadiusScale = 0.65f;
f32 RPFshFish::sMouthRadiusAdd = 25.0f;
f32 RPFshFish::sNearRadiusScale = 1.0f;
f32 RPFshFish::sNearRadiusAdd = 25.0f;
f32 RPFshFish::sHookViewAngle = 2.1991148f;
f32 RPFshFish::sHookViewDist = 100.0f;
s32 RPFshFish::sCatchViewFrames = 12;
s32 RPFshFish::sCatchViewHoldFrames = 10;
s32 RPFshFish::sShowPointsFrames = 85;
s32 RPFshFish::sShowPointsEndFrames = 120;
f32 RPFshFish::sPopScale = 0.2f;
s32 RPFshFish::sPopFrames = 20;
s32 RPFshFish::sJumpFrames = 20;
f32 RPFshFish::sJumpHeight = 4.0f;
s32 RPFshFish::sEscapeFrames = 180;
s32 RPFshFish::sEscapeTurnFrames = 40;
f32 RPFshFish::sEscapeSpeed = 4.0f;
f32 RPFshFish::sEscapeDamp = 0.85f;
f32 RPFshFish::sEscapeAccel = 1.6f;
f32 RPFshFish::sGetEffectOffset = 1.0f;
f32 RPFshFish::sGetEffectHeightA = 7.0f;
f32 RPFshFish::sGetEffectHeightB = 2.5f;
bool RPFshFish::sShowPointsClose = true;

EGG::Vector3f RPFshFish::sCatchViewPos(0.0f, 280.0f, 250.0f);

// Wii Play: comes after the constructor (and the TRPSndObject<0> destructors) in the binary
RPFshFishSnd::~RPFshFishSnd() {}

// The sound-base tail matches with propagation off.
#pragma opt_propagation off
RPFshFish::RPFshFish(s32 index, s32 type) {
    mIndex = index;
    mType = type;
    mpEase = new RPRange();
    RPGrpRenderer::GetCurrent()->AppendDrawObject(&static_cast<IRPGrpDrawObject&>(*this));

    const char* effectNames[9] = {"Fsh_run_a",    "Fsh_run_b",    "Fsh_run_c",
                                  "Fsh_get_back", "Fsh_get_glow", "Fsh_get_kira",
                                  "Fsh_exmark",   "Fsh_run02_a",  "Fsh_run02_b"};
    for (int i = 0; i < 9; i++) {
        u32 flag = 0;
        if (i == 5) {
            flag = 1;
        }
        mpEffects[i] = new RPSysEffect(effectNames[i], flag, 0);
    }

    mpSnd = new RPFshFishSnd();
}
#pragma opt_propagation reset

void RPFshFish::Create() {
    const char* modelNames[TYPE_NUM] = {"fsh_fish_A",     "fsh_fish_B",     "fsh_fish_C",
                                        "fsh_fish_sukaA", "fsh_fish_rareA", "fsh_fish_rareB"};
    // Wii Play: the (handle, int) overload is called with the model name
    mpModel = RPGrpModel::Construct(mType + 8, modelNames[mType], 1, 0, 0);
    mpModel->SetDrawScene(0);
    mpModel->CreateBoundingInfo(1);

    const char* anmNames[3] = {"fsh_fish_appearanim_scl", "fsh_fish_swimanim",
                               "fsh_fish_pullanim"};
    mpModel->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Chr, 3);
    for (int i = 0; i < 3; i++) {
        mpModel->GetModelAnm()->RegistFromResFile(RPGrpModelAnm::Anm_Chr, i, 0xE, anmNames[i]);
    }

    mpJointMtx = new RPJointMtxBuffer(mpModel);
    s32 mouthIndex = mpModel->GetJointIndex("mouth_loc");
    const nw4r::math::MTX34& rMouth = mpJointMtx->GetLocalMtx(mouthIndex);
    mMouthOffset.x = rMouth._03;
    mMouthOffset.y = rMouth._13;
    mMouthOffset.z = rMouth._23;
}

void RPFshFish::Reset() {
    mState = STATE_NONE;
    mNextState = STATE_NONE;
    mIsChanging = false;
    mStateTimer = 0;
}

void RPFshFish::Exit() {}

// Weak copy the original emits right after Calculate.
template void TRPSndObject<0>::set3DPosition(const nw4r::math::VEC3&);

void RPFshFish::Calculate() {
    RPFshStateFuncsEx<RPFshFish> funcs[STATE_NUM] = {
        {NULL, NULL, NULL},
        {&RPFshFish::SwimEnter, &RPFshFish::SwimCalc, NULL},
        {&RPFshFish::EscapeEnter, &RPFshFish::EscapeCalc, &RPFshFish::EscapeExit},
        {&RPFshFish::VanishEnter, &RPFshFish::VanishCalc, NULL},
        {&RPFshFish::ApproachEnter, &RPFshFish::ApproachCalc, NULL},
        {&RPFshFish::HookedEnter, &RPFshFish::HookedCalc, &RPFshFish::HookedExit},
        {&RPFshFish::JumpEnter, &RPFshFish::JumpCalc, NULL},
        {&RPFshFish::CatchViewEnter, &RPFshFish::CatchViewCalc, NULL},
        {&RPFshFish::ShowPointsEnter, &RPFshFish::ShowPointsCalc, NULL},
        {&RPFshFish::FadeOutEnter, &RPFshFish::FadeOutCalc, NULL},
    };

    if (mIsChanging) {
        RPFshStateFuncsEx<RPFshFish>::Func exit = funcs[mState].mExit;
        if (exit != NULL) {
            (this->*exit)();
        }

        mState = mNextState;
        mIsChanging = false;
        mStateTimer = 0;

        RPFshStateFuncsEx<RPFshFish>::Func enter = funcs[mState].mEnter;
        if (enter != NULL) {
            (this->*enter)();
        }
    }

    RPFshStateFuncsEx<RPFshFish>::Func calc = funcs[mState].mCalc;
    if (calc != NULL) {
        (this->*calc)();
    }

    EGG::Vector3f mouth = (mAxisZ * mMouthOffset.z + mAxisY * mMouthOffset.y) +
                          mAxisX * mMouthOffset.x;
    mMouthPos = AsVector3f(mPos) + mouth * mScale;

    if (mPopTimer >= 0) {
        f32 t = 1.0f;
        if (mPopTimer < sPopFrames / 2) {
            t = RPRange::CalcEase(mPopTimer / (f32)(sPopFrames / 2), 1);
        }
        if (mPopTimer >= sPopFrames / 2) {
            t = 1.0f - (mPopTimer - sPopFrames / 2) / (f32)(sPopFrames / 2);
        }

        mPopScale = sPopScale * t + 1.0f;
        mPopTimer++;
        if (mPopTimer > sPopFrames) {
            mPopScale = 1.0f;
            mPopTimer = -1;
        }
    }

    if (mState != STATE_NONE &&
        (RPFshCameraMgr::GetInstance()->GetCamera()->IsOffPreset() ||
         (u32)(mState - STATE_CATCH_VIEW) <= STATE_FADE_OUT - STATE_CATCH_VIEW)) {
        CalcModelMtx();
    }

    for (int i = 0; i < 9; i++) {
        mpEffects[i]->update();
    }

    mpSnd->set3DPosition(mPos);

    if (RPFshStartLayout::GetInstance()->GetState() == RPFshStartLayout::STATE_PLAY) {
        mFrame++;
    }
}

inline void RPFshFish::FixAxes() {
    mAxisZ.normalise();
    mAxisY = mAxisZ.cross(mAxisX);
    mAxisY.normalise();
    mAxisX = mAxisY.cross(mAxisZ);
}

void RPFshFish::SwimEnter() {
    unkC8 = -1;
    unkC4 = 0;
    mIsBiting = false;
    mIsHooked = false;
}

#pragma fp_contract off
// The FishInterpolate call with a named 0.3f fixed the 15.0f/0.3f registers. The dead `numFrame`
// conversion is needed: CodeWarrior alternates the two int->float scratch slots (0x90/0x98) per
// conversion, even a dead one. moveRate declared first and the hook radius passed without a local
// give moveRate/angle/radius f28 and damping f30.
void RPFshFish::SwimCalc() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    RPGrpModelAnm* pAnm = mpModel->GetModelAnm();

    f32 moveRate = 1.0f;
    f32 damping = pParam->mDamping;
    f32 frame;
    s16 anmIdx = pAnm->GetPlayIndex(RPGrpModelAnm::Anm_Chr);
    if (anmIdx == 1) {
        frame = pAnm->GetFrame(RPGrpModelAnm::Anm_Chr, anmIdx);
        f32 numFrame = pAnm->GetNumFrame(RPGrpModelAnm::Anm_Chr, anmIdx);
        if (frame > 1.0f && frame <= 15.0f) {
            damping = 1.0f;
            f32 minRate = 0.3f;
            moveRate = FishInterpolate(minRate, damping, frame, 15.0f);
        }
    }

    EGG::Vector3f velocity = mAxisZ * mVec1C.z;
    EGG::Vector3f movement = velocity * moveRate;
    AsVector3f(mPos) += movement;
    mVec1C.z = mVec1C.z * damping;

    if (unkC4 > 0) {
        f32 angle = FishAngle(mAxisZ, mJumpAxisZ);
        angle = angle > 0.0f ? angle : -angle;

        if (angle > 0.08726647f) {
            f32 side = FishAngle(mJumpAxisZ, mAxisY);
            side = side > 0.0f ? side : -side;

            if (side < 1.5707964f) {
                EGG::Vector3f turn = mAxisY * angle;
                EGG::Vector3f change = turn * pParam->unkC;
                mAxisZ += change;
            } else {
                EGG::Vector3f turn = mAxisY * angle;
                EGG::Vector3f change = turn * pParam->unkC;
                mAxisZ -= change;
            }

            mAxisZ.normalise();
            FixAxes();
        }

        if (--unkC4 <= 0) {
            f32 rangeSpeed;
            f32 minSpeed;
            minSpeed = 0.5f * pParam->mMaxSize;
            rangeSpeed = pParam->mMaxSize - minSpeed;
            mVec1C.z = minSpeed + rangeSpeed * RPUtlRandom::getF32();
            pAnm->Start(RPGrpModelAnm::Anm_Chr, 1, 0.0f);
        }
    } else if (mVec1C.z < sSwimTurnRate) {
        f32 frame = pAnm->GetFrame(RPGrpModelAnm::Anm_Chr, anmIdx);
        f32 lastFrame = pAnm->GetNumFrame(RPGrpModelAnm::Anm_Chr, anmIdx) - 1;
        if (anmIdx == 2 || frame >= lastFrame - 1.0f) {
            unkC4 = pParam->unk8 * (s32)(1.0f + FishRandom(0.2f));
            EGG::Vector3f direction = RandomDirXZ();
            mJumpAxisZ = direction;
        }
    }

    ResolveCollision();

    for (int i = 0; i < RPFshPlayerMgr::GetInstance()->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = RPFshPlayerMgr::GetInstance()->GetPlayer(i);
        if (pPlayer->GetState() == RPFshPlayer::STATE_NONE) {
            continue;
        }
        if (RPFshTimer::GetInstance()->IsTimeUp()) {
            continue;
        }
        if (RPFshStartLayout::GetInstance()->GetState() != RPFshStartLayout::STATE_PLAY) {
            continue;
        }

        if (IsNearHook(i)) {
            f32 ratio = pPlayer->GetRod()->GetBendRatio();
            f32 accel = __fmadds(sSwimAccel, ratio, sSwimAccel);
            if (FishRandom(1.0f) < accel && !mIsChanging) {
                ChangeState(STATE_APPROACH);
                unkC8 = i;
            }
        } else if (pPlayer->GetRod()->IsHookInWater()) {
            if (RPUtlRandom::getU32(100) < pParam->unk2C) {
                if (IsNearPoint(pPlayer->GetRod()->GetHookRadius(),
                                pPlayer->GetRod()->GetRope()->GetPoint(7))) {
                    mNextState = STATE_ESCAPE;
                    mIsChanging = true;
                    pPlayer->Rumble(pParam->unk24 * mScale);
                }
            }
        }
    }

    if (unkE0 >= 0 && mFrame > unkE0 && !mIsChanging) {
        ChangeState(STATE_VANISH);
    }
}

#pragma fp_contract on

void RPFshFish::EscapeEnter() {
    RPGrpModelAnm* pAnm = mpModel->GetModelAnm();
    pAnm->Start(RPGrpModelAnm::Anm_Chr, 1, 0.0f);
    pAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 1, 0.0f);

    mpEffects[6]->create();
    mpEffects[6]->setScale(EGG::Vector3f(1.0f, 1.0f, 1.0f) * sEscapeDamp);
}

// Weak copies the original emits right after EscapeCalc.
template void TRPSndObject<0>::detailHoldSound(nw4r::snd::SoundHandle*, u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSound(ui32, u32);

void RPFshFish::EscapeCalc() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    RPGrpModelAnm* pAnm = mpModel->GetModelAnm();

    if (mStateTimer <= sEscapeTurnFrames) {
        if (mStateTimer == sEscapeTurnFrames) {
            mVec1C.z = 2.5f * pParam->mMaxSize;
            for (int i = 7; i <= 8; i++) {
                mpEffects[i]->create();
                mpEffects[i]->setScale(CalcModelScale());
            }
        }
    } else {
        f32 rate = RPRange::CalcEase(
            (sEscapeFrames - mStateTimer) / (f32)sEscapeFrames, 1);
        pAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 1, rate * (sEscapeSpeed - 1.0f) + 1.0f);

        if (mStateTimer % 20 == 0) {
            f32 turn = 0.5f * (sEscapeFrames - mStateTimer) / sEscapeFrames;
            s32 dir = (mIndex % 2) * 2 - 1;
            if (mStateTimer % 40 == 0) {
                dir = -dir;
            }

            mAxisZ += (mAxisY * turn) * (f32)dir;
            mAxisZ.normalise();
            FixAxes();
        }

        AsVector3f(mPos) += mAxisZ * mVec1C.z;

        if (mStateTimer >= sEscapeFrames) {
            ChangeState(STATE_SWIM);
        }

        for (int i = 7; i <= 8; i++) {
            mpEffects[i]->setPos(mPos);
        }

        mpSnd->holdSound(0xBU);
    }

    mStateTimer++;
    ResolveCollision();

    nw4r::math::VEC3 markPos = mMouthPos;
    markPos.y += sEscapeAccel;
    mpEffects[6]->setPos(markPos);
}

void RPFshFish::EscapeExit() {
    for (int i = 7; i <= 8; i++) {
        mpEffects[i]->fade();
    }
}

void RPFshFish::VanishEnter() {
    RPGrpModelAnm* pAnm = mpModel->GetModelAnm();
    int numFrame = pAnm->GetNumFrame(RPGrpModelAnm::Anm_Chr, 0);
    pAnm->Start(RPGrpModelAnm::Anm_Chr, 0, numFrame - 1.0f);
    mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, -1.0f);
}

void RPFshFish::VanishCalc() {
    if (mpModel->GetModelAnm()->GetFrame(RPGrpModelAnm::Anm_Chr, 0) <= 0.0f) {
        ChangeState(STATE_NONE);
        if (mType != TYPE_RARE_A && mType != TYPE_RARE_B) {
            RPFshFishMgr::GetInstance()->SpawnRandomFish();
        }
    }
}

void RPFshFish::ApproachEnter() {
    unkD0 = 0;
    unkCC = true;
    mIsBiting = false;
}

#pragma fp_contract off
void RPFshFish::ApproachCalc() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    RPFshPlayer* pPlayer = RPFshPlayerMgr::GetInstance()->GetPlayer(unkC8);

    if (pPlayer == NULL) {
        ChangeState(STATE_SWIM);
        return;
    }

    if (mIsBiting) {
        ChangeState(STATE_SWIM);
        mIsBiting = false;
        return;
    }

    EGG::Vector3f hook = pPlayer->GetRod()->GetRope()->GetPoint(7);
    EGG::Vector3f dir = hook - AsVector3f(mPos);
    dir.y = 0.0f;
    dir.normalise();

    u32 isZero = __fmadds(dir.z, dir.z, __fmadds(dir.x, dir.x, dir.y * dir.y)) <=
                 1.1920929e-7f;
    if (!isZero) {
        mAxisZ = mAxisZ + dir * 0.5f;
        mAxisZ.normalise();
        FixAxes();
    }

    if (unkCC) {
        if (IsNearHook(unkC8) && !RPFshTimer::GetInstance()->IsTimeUp()) {
            AsVector3f(mPos) += mAxisZ * sApproachSpeed;
            if (IsMouthNearPoint(pPlayer->GetRod()->GetHookRadius(), hook)) {
                pPlayer->Rumble(pParam->unk24 * mScale);
                if (++unkD0 > pParam->unk10) {
                    mIsBiting = true;
                } else {
#pragma fp_contract off
                    f32 minFrames = sApproachFrames;
                    f32 duration = FishRandomRange(minFrames, 1.5f * sApproachFrames);
                    unkCC = false;
                    unkA4 = (s32)duration;
                    if (unkC8 == 0) {
                        mpSnd->StartBite(1);
                    } else {
                        mpSnd->StartBite(2);
                    }
                }
            }
        } else if (!mIsChanging) {
            ChangeState(STATE_SWIM);
            mVec1C.z = 0.0f;
            mJumpAxisZ = -dir;
#pragma fp_contract off
            unkC4 = (s32)(pParam->unk8 * (1.0f + FishRandom(0.2f)));
        }
    } else {
        AsVector3f(mPos) -= ((mAxisZ * sApproachRate) * (f32)(unkA4 - mStateTimer)) / (f32)unkA4;
        if (++mStateTimer > unkA4) {
            unkCC = true;
            mStateTimer = 0;
        }
    }

    ResolveCollision();
}
#pragma fp_contract on

#pragma fp_contract off
void RPFshFish::HookedEnter() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    f32 duration = pParam->unk14;
    f32 maxDuration = 1.2f * pParam->unk14;
    f32 random = FishRandom(maxDuration - duration);
    mIsBiting = false;
    mIsHooked = true;
    unkA4 = (s32)(duration + random);
    mpModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 2, 0.0f);

    for (int i = 0; i <= 2; i++) {
        mpEffects[i]->create();
        mpEffects[i]->setScale(CalcModelScale());
    }
}
#pragma fp_contract on

void RPFshFish::HookedCalc() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    EGG::Vector3f pos = mPos;
    AsVector3f(mPos) += mAxisZ * pParam->mPullSpeed;
    ResolveCollision();

    if (mStateTimer > unkA4 || RPFshTimer::GetInstance()->IsTimeUp()) {
        if (!mIsChanging) {
            ChangeState(STATE_JUMP);
            mIsHooked = false;
        }
    }

    mStateTimer++;
    for (int i = 0; i <= 2; i++) {
        mpEffects[i]->setPos(mPos);
    }

    if (unkC8 == 0) {
        mpSnd->HoldStruggle(1);
    } else {
        mpSnd->HoldStruggle(2);
    }
}

void RPFshFish::HookedExit() {
    RPGrpModelAnm* pAnm = mpModel->GetModelAnm();
    pAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 2, 0.0f);
    mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 2, 0.0f);

    for (int i = 0; i <= 2; i++) {
        mpEffects[i]->fade();
    }
}

// Weak copies the original emits right after JumpEnter (its first user in output order).
template void TRPSndObject<0>::detailStartSound(nw4r::snd::SoundHandle*, u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSound(ui32, u32);

#pragma fp_contract off
void RPFshFish::JumpEnter() {
    GetTypeParam(mType);
    mJumpAxisZ = -mAxisZ + mAxisY * (2.0f * RPUtlRandom::getF32() - 1.0f);
    mJumpAxisZ.normalise();
    mJumpAxisY = -mAxisX;
    unk90 = mPos.y;
    mVec1C.y = sJumpHeight;
    mPopTimer = 0;
    mpEase->Start(1, sJumpFrames);
    mpSnd->startSound(0x22U);
}
#pragma fp_contract on

void RPFshFish::JumpCalc() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);

    if (mStateTimer > sJumpFrames) {
        ChangeState(STATE_SWIM);
        mAxisZ = mJumpAxisZ;
        mAxisX = mJumpAxisY;
        FixAxes();
        mVec1C.y = 0.0f;
        mVec1C.z = pParam->mMaxSize;
        mPos.y = unk90;
        if (mType == TYPE_SUKA_A) {
            unkE0 = mFrame + 60;
        }
    } else {
        mPos.y += mVec1C.y;
        mVec1C.y -= 2.0f * sJumpHeight / sJumpFrames;
        EGG::Vector3f x = mJumpAxisZ.cross(mJumpAxisY);
        f32 t = (f32)mStateTimer / sJumpFrames;
        RPSlerpBasis(t, &mAxisX, &mAxisY, &mAxisZ, mJumpAxisY, x,
                     mJumpAxisZ, mAxisX, mAxisY, mAxisZ);
    }

    if (mStateTimer == sJumpFrames - 4) {
        RPSysEffect effect("Fsh_catch01", 0, 0);
        effect.create();
        effect.setPos(mPos);
        effect.setScale(CalcModelScale());
        effect.update();
    }

    mStateTimer++;
}

void RPFshFish::CatchViewEnter() {
    RPFshCameraMgr::GetInstance()->GetCamera()->MoveToCatchView();
    mpEase->Start(1, sCatchViewFrames);
    mCatchFromPos = mPos;
    RPFshTimer::GetInstance()->Pause();
    RPFshBonusFish::GetInstance()->Hide();

    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();
    RPSndAudioMgr* pAudioMgr = RPSndAudioMgr::instance();
    if (mType == TYPE_SUKA_A) {
        if (pPlayerMgr->GetCatchingPlayer() == 0) {
            mpSnd->StartCatch(1);
            pAudioMgr->startSystemSe(0x6BU);
        } else {
            mpSnd->StartCatch(2);
            pAudioMgr->startSystemSe(0x6CU);
        }
    } else if (pPlayerMgr->GetCatchingPlayer() == 0) {
        mpSnd->StartCatch(1);
        if (mType == RPFshBonusFish::GetInstance()->GetType()) {
            pAudioMgr->startSystemSe(0x6DU);
        } else {
            pAudioMgr->startSystemSe(0x69U);
        }
    } else {
        mpSnd->StartCatch(2);
        if (mType == RPFshBonusFish::GetInstance()->GetType()) {
            pAudioMgr->startSystemSe(0x6EU);
        } else {
            pAudioMgr->startSystemSe(0x6AU);
        }
    }
}

void RPFshFish::CatchViewCalc() {
    RPSysEffect effect("Fsh_catch02", 0, 0);
    effect.create();
    effect.setPos(mPos);
    effect.setScale(CalcModelScale());
    effect.update();

    if (mStateTimer >= sCatchViewHoldFrames) {
        mIsHooked = false;
    }

    f32 t = (f32)mStateTimer / sCatchViewFrames;
    f32 ease = mpEase->GetValue();
    AsVector3f(mPos) = AsVector3f(mCatchFromPos) * (1.0f - ease) + sCatchViewPos * ease;

    EGG::Vector3f toZ(-1.0f, 0.0f, 0.0f);
    EGG::Vector3f toY(0.0f, 1.0f, 0.0f);
    EGG::Vector3f toX(0.0f, 0.0f, 1.0f);
    RPSlerpBasis(t, &mAxisX, &mAxisY, &mAxisZ, toX, toY, toZ, mAxisX, mAxisY, mAxisZ);

    mStateTimer++;
    mpEase->Step();

    if (mStateTimer >= sCatchViewFrames) {
        mAxisZ = toZ;
        mAxisY = toY;
        mAxisX = toX;
        ChangeState(STATE_SHOW_POINTS);
    }
}

void RPFshFish::ShowPointsEnter() {
    RPFshPointPopup::GetInstance()->Show(RPFshPlayerMgr::GetInstance()->GetCatchingPlayer(),
                                         mIndex);

    if (mType != TYPE_SUKA_A) {
        for (int i = 3; i <= 5; i++) {
            mpEffects[i]->create();
            mpEffects[i]->setScale(CalcModelScale());
        }
    }

    mpModel->SetDrawGroup(1);

    RPGrpDrawPathXluSnap* pXluSnap = RPGrpModelManager::GetCurrent()
                                         ->GetDrawPathManager(0)
                                         ->GetDrawPathXluSnap();
    pXluSnap->GetXluSnap()->setAlpha(0xFF);
    pXluSnap->GetXluSnap()->onFlag(4);
}

void RPFshFish::ShowPointsCalc() {
    if (RPFshPointPopup::GetInstance()->IsShowStart()) {
        s32 player = RPFshPlayerMgr::GetInstance()->GetCatchingPlayer();
        RPFshPlayerMgr::GetInstance()->GetPlayer(player)->AddCaughtFish(mIndex);
        RPFshScoreLayout::GetInstance()->AddFishCount(player);
    }

    s32 frames = sShowPointsFrames;
    if (mType == RPFshBonusFish::GetInstance()->GetType()) {
        frames = sShowPointsEndFrames;
    }

    if (mStateTimer >= frames && RPFshPointPopup::GetInstance()->IsShowing() &&
        sShowPointsClose) {
        ChangeState(STATE_FADE_OUT);
        RPFshPointPopup::GetInstance()->Close();
    }

    mStateTimer++;
    UpdateGetEffects();
}

void RPFshFish::FadeOutEnter() {
    RPSndAudioMgr::instance()->startSystemSe(0x21U);
}

void RPFshFish::FadeOutCalc() {
    s32 length = RPFshPointPopup::GetInstance()->GetOutAnmLength();
    s32 alpha = 255.0f * (length - mStateTimer) / length;
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }

    RPGrpModelManager::GetCurrent()->GetDrawPathManager(0)->GetDrawPathXluSnap()->GetXluSnap()
        ->setAlpha(alpha);
    for (int i = 3; i <= 5; i++) {
        mpEffects[i]->setColor(0xFF, 0xFF, 0xFF, alpha);
    }

    mStateTimer++;
    mScaleMul += 0.5f / length;

    if (RPFshPointPopup::GetInstance()->IsClosed()) {
        RPFshFishMgr::GetInstance()->SetUnk10(-1);
        ChangeState(STATE_NONE);
        if (mType != TYPE_RARE_A && mType != TYPE_RARE_B) {
            RPFshFishMgr::GetInstance()->SpawnRandomFish();
        }

        RPFshCameraMgr::GetInstance()->GetCamera()->MoveToDefault();
        for (int i = 3; i <= 5; i++) {
            mpEffects[i]->forceKill();
        }

        RPFshFishMgr::GetInstance()->AddUnkC();
        RPFshTimer::GetInstance()->Resume();
        RPFshBonusFish::GetInstance()->Show(0);
    } else {
        UpdateGetEffects();
    }
}

#pragma fp_contract off
void RPFshFish::Spawn() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);

    ChangeState(STATE_SWIM);
    f32 rangeSpeed;
    f32 minSpeed;
    minSpeed = 0.5f * pParam->mMaxSize;
    rangeSpeed = pParam->mMaxSize - minSpeed;
    EGG::Vector3f vel(0.0f, 0.0f, minSpeed + rangeSpeed * RPUtlRandom::getF32());
    mVec1C.x = vel.x;
    mVec1C.y = vel.y;
    unkC4 = 0;
    mIsBiting = false;
    mIsHooked = false;
    mVec1C.z = vel.z;

    f32 depth = RPFshStage::GetWaterSurfaceY() - RPFshStage::GetPondBottomY();
    f32 y = RPFshStage::GetWaterSurfaceY() - depth / 5.0f;
    EGG::Vector3f pos(0.0f, y, 0.0f);
    f32 layer = (3.0f + mIndex) / 36.0f;
    mPos.x = pos.x;
    mPos.y = pos.y;
    mPos.z = pos.z;
    mPos.y = __fnmsubs(y - RPFshStage::GetPondBottomY(), layer, mPos.y);

    EGG::Vector3f dir = RandomDirXZ();
    mAxisZ.x = dir.x;
    mAxisZ.y = dir.y;
    mAxisZ.z = dir.z;
    EGG::Vector3f up(0.0f, 1.0f, 0.0f);
    mAxisX.x = up.x;
    mAxisX.y = up.y;
    mAxisX.z = up.z;
    FixAxes();

    mPopScale = 1.0f;
    mPopTimer = -1;
    mScaleMul = 1.0f;

    if (mType == TYPE_RARE_B) {
        mPos.x = RPFshStage::GetPondRadius() - pParam->mLength - 30.0f;
        mVec1C.z = pParam->mMaxSize;
        EGG::Vector3f rareDir(-1.0f, 0.0f, 0.0f);
        mAxisZ = rareDir;
        if (RPUtlRandom::getU32(2) == 0) {
            mPos.x = -mPos.x;
            EGG::Vector3f reverseDir = -mAxisZ;
            mAxisZ = reverseDir;
        }

        EGG::Vector3f up2(0.0f, 1.0f, 0.0f);
        mAxisX.x = up2.x;
        mAxisX.y = up2.y;
        mAxisX.z = up2.z;
        FixAxes();
    }

    if (mType == TYPE_RARE_A || mType == TYPE_RARE_B) {
        f32 rand = 0.2f * RPUtlRandom::getF32();
        mScale = 0.8f + rand;
    } else {
        mScale = 0.5f + FishRandom(0.5f);
    }

    mpModel->SetDrawGroup(0);
    mpModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
    mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, 1.0f);

    f32 maxLength = 0.0f;
    for (int i = 0; i < TYPE_NUM; i++) {
        f32 length = GetTypeParam(i)->mLength;
        if (length > maxLength) {
            maxLength = length;
        }
    }

    mpSnd->SetSizeClass(mType == TYPE_RARE_A, mType == TYPE_SUKA_A,
                        mScale * pParam->mLength / maxLength);

    mFrame = 0;
    if (pParam->mMinFrame >= 0) {
        unkE0 = pParam->mMinFrame +
                RPUtlRandom::getU32(pParam->mMaxFrame - pParam->mMinFrame);
    } else {
        unkE0 = -1;
    }
}
#pragma fp_contract on

bool RPFshFish::IsNearPoint(f32 radius, const nw4r::math::VEC3& rPoint) const {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    EGG::Vector3f diff = AsVector3f(rPoint) - AsVector3f(mPos);
    f32 size = mScale * (pParam->mLength * sNearRadiusScale);

    f32 dy = diff.y > 0.0f ? diff.y : -diff.y;
    if (dy > radius + sNearRadiusAdd) {
        return false;
    }

    diff.y = 0.0f;
    if (diff.length() < radius + size) {
        return true;
    }
    return false;
}

bool RPFshFish::IsMouthNearPoint(f32 radius, const nw4r::math::VEC3& rPoint) const {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);

    EGG::Vector3f offset = mMouthOffset;
    offset.normalise();
    offset = offset.x * mAxisX + offset.y * mAxisY + offset.z * mAxisZ;

    f32 size = mScale * (pParam->mLength * sMouthRadiusScale);
    f32 reach = sMouthRadiusAdd + radius;

    EGG::Vector3f point = rPoint;
    f32 bottom = RPFshStage::GetPondBottomY();
    if (point.y - bottom > reach) {
        point.y = bottom + reach;
    }

    EGG::Vector3f mouth = mMouthPos - (offset * size) * 0.7f;
    EGG::Vector3f diff = point - mouth;
    f32 dy = diff.y > 0.0f ? diff.y : -diff.y;
    if (dy > reach) {
        return false;
    }

    diff.y = 0.0f;
    if (mIsBiting) {
        size *= pParam->unk20;
    }

    if (diff.length() < radius + size) {
        return true;
    }
    return false;
}

bool RPFshFish::IsNearHook(s32 player) const {
    RPFshPlayer* pPlayer = RPFshPlayerMgr::GetInstance()->GetPlayer(player);
    if (pPlayer != NULL && pPlayer->IsHookWaiting()) {

        f32 reach = 0.5f * pPlayer->GetRod()->GetBendRatio() + 1.0f;
        f32 hookRadius = pPlayer->GetRod()->GetHookRadius();
        if (IsMouthNearPoint(reach * hookRadius, pPlayer->GetRod()->GetRope()->GetPoint(7))) {
            return true;
        }

        EGG::Vector3f hook = pPlayer->GetRod()->GetRope()->GetPoint(7);
        EGG::Vector3f diff = hook - mMouthPos;
        u32 isZero = diff.squaredLength() <= 1.1920929e-7f;
        if (isZero) {
            return true;
        }

        f32 dy = diff.y > 0.0f ? diff.y : -diff.y;
        if (dy > 2.0f * (sHookViewDist * reach)) {
            return false;
        }

        diff.y = 0.0f;
        f32 dist = diff.length();
        f32 dot = diff.dot(mAxisZ);
        f32 angle = EGG::Math<f32>::atan2(diff.cross(mAxisZ).length(), dot);
        angle = angle > 0.0f ? angle : -angle;

        if (dist < sHookViewDist * reach && angle < sHookViewAngle / 2.0f) {
            return true;
        }
    }
    return false;
}

bool RPFshFish::ResolveCollision() {
    const RPFshFishTypeParam* pParam = GetTypeParam(mType);
    EGG::Vector3f push = RPFshStage::GetInstance()->GetKcl()->CalcPushOut(
        mPos, pParam->mLength * mScale);
    push.y = 0.0f;

    u32 isZero = push.squaredLength() <= 1.1920929e-7f;
    if (isZero) {
        return false;
    }

    AsVector3f(mPos) += push;
    mAxisZ += push * 0.1f;
    mAxisZ.normalise();
    mAxisY = mAxisZ.cross(mAxisX);
    return true;
}

void RPFshFish::UpdateGetEffects() {
    RPFshCamera* const pCamera = RPFshCameraMgr::GetInstance()->GetCamera();
    FishVec3 dir(pCamera->GetTargetRef() - pCamera->GetPosition());
    dir.normalise();

    for (int i = 3; i <= 5; i++) {
        EGG::Vector3f pos = mPos;
        if (i == 5) {
            pos -= dir * sGetEffectOffset;
        } else {
            pos += dir * sGetEffectOffset;
        }

        if (mType == TYPE_RARE_A || mType == TYPE_RARE_B) {
            pos += FishVec3(sGetEffectHeightB * (pCamera->GetUp() * CalcModelScale().x));
        } else {
            pos += FishVec3(sGetEffectHeightA * (pCamera->GetUp() * CalcModelScale().x));
        }

        if (i == 5) {
            nw4r::math::MTX34 mtx;
            mtx._00 = mAxisX.x;
            mtx._10 = mAxisX.y;
            mtx._20 = mAxisX.z;
            mtx._01 = mAxisY.x;
            mtx._11 = mAxisY.y;
            mtx._21 = mAxisY.z;
            mtx._02 = mAxisZ.x;
            mtx._12 = mAxisZ.y;
            mtx._22 = mAxisZ.z;
            mtx._03 = pos.x;
            mtx._13 = pos.y;
            mtx._23 = pos.z;
            mpEffects[i]->setMtx(mtx);
        } else {
            mpEffects[i]->setPos(pos);
        }
    }
}

#pragma fp_contract off
EGG::Vector3f RPFshFish::RandomDirXZ() {
    EGG::Vector3f dir;
    f32 x = 2.0f * RPUtlRandom::getF32() - 1.0f;
    dir.y = 0.0f;
    dir.x = x;
    dir.z = 2.0f * RPUtlRandom::getF32() - 1.0f;
    dir.normalise();
    u32 isZero = __fmadds(dir.z, dir.z, __fmadds(dir.x, dir.x, dir.y * dir.y)) <=
                 1.1920929e-7f;
    if (isZero) {
        dir.x = 1.0f;
    }
    return dir;
}
#pragma fp_contract on

f32 RPFshFish::GetPullSpeed() const {
    return GetTypeParam(mType)->mPullSpeed;
}

#pragma auto_inline off
nw4r::math::VEC3 RPFshFish::CalcModelScale() const {
    f32 baseLength = GetTypeParam(TYPE_B)->mLength;
    f32 s = mScale * GetTypeParam(mType)->mLength / baseLength;
    nw4r::math::VEC3 scale(s, s, s);
    if (mType == TYPE_RARE_A || mType == TYPE_RARE_B) {
        scale.x *= 0.9f;
        scale.y = scale.x;
        scale.z = scale.x;
    }
    return scale;
}
#pragma auto_inline on

void RPFshFish::UserDraw() {}

// STAND-IN: dead-stripped; puts the s32->f32 magic double (lbl_8043F368) right after CalcModelMtx's
// 0.001f (8043F364) and before CalcModelScale's 0.9f (8043F370), as in the original .sdata2.
// Without it the double lands after 0.8f (8043F3AC), before 255.0f.
extern "C" { static f32 Fsh_fishPoolOrderS32(s32 x) { return (f32)x; } }

void RPFshFish::CalcModelMtx() {
    RPFshStage::GetInstance()->GetShadowTexture()->AddModel(mpModel, 1.0f);

    nw4r::math::MTX34 mtx;
    mtx._00 = mAxisX.x;
    mtx._10 = mAxisX.y;
    mtx._20 = mAxisX.z;
    mtx._01 = mAxisY.x;
    mtx._11 = mAxisY.y;
    mtx._21 = mAxisY.z;
    mtx._02 = mAxisZ.x;
    mtx._12 = mAxisZ.y;
    mtx._22 = mAxisZ.z;
    mtx._03 = mPos.x;
    mtx._13 = mPos.y;
    mtx._23 = mPos.z;
    mpModel->GetModelEx()->setLocalMtx(&mtx);

    f32 scale = mScaleMul * (mScale * mPopScale);
    scale = 0.001f > scale ? 0.001f : scale;
    mpModel->SetBaseScale(scale);
    mpModel->UpdateFrame();
    mpModel->Calc();
    mpModel->Entry();
}

// The original emits the remaining weak copies in this order (vtable-only TRPSndObject<0>
// virtuals in reverse slot order, then the getters and the state machine helpers).
// Stand-in (dead-stripped): compiled first, it makes the stopping index lookup's assertion strings
// come before the other lookup's, as in the original .data after the vtables.
extern "C" { static void Fsh_fishIndexedHandleOrder(TRPSndObject<0>* pObj) { pObj->getIndexedHandle(0); } }

#pragma defer_codegen off
// Unreferenced stand-ins (dead-stripped by the linker). The first flushes the deferred
// functions above before dont_inline applies; the second's direct calls put the in-class
// getters' weak copies first, as in the original.
extern "C" { static void Fsh_fishDeferFlush() {} }
#pragma dont_inline on
extern "C" { static void Fsh_fishSndGetterOrder(TRPSndAudioActorBaseWithHandles<4, 0>* pActor) {
    // emitted in reverse call order
    pActor->TRPSndAudioActorBaseWithHandles<4, 0>::get3DPosition(0);
    pActor->TRPSndAudioHandles<4>::getOuterVolume();
    pActor->TRPSndAudioHandles<4>::getOuterPitch();
} }
#pragma dont_inline off
template void TRPSndObject<0>::detailHoldSoundWithRemotePlayer(nw4r::snd::SoundHandle*, u32, u32, s32);
template void TRPSndObject<0>::detailStartSoundWithRemotePlayer(nw4r::snd::SoundHandle*, u32, u32, s32);
template void TRPSndObject<0>::detailPrepareSound(nw4r::snd::SoundHandle*, u32, u32);
template f32 TRPSndObject<0>::getOuterVolume();
template void TRPSndObject<0>::setActorVolume(f32, int);
template void TRPSndObject<0>::stopAllSound(int);
template void TRPSndObject<0>::stopSound(const char*, int);
template void TRPSndObject<0>::stopSound(ui32, int);
template void TRPSndObject<0>::stopSound(u32, int);
template nw4r::snd::SoundHandle* TRPSndObject<0>::prepareSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::prepareSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::prepareSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::prepareSound(ui32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::prepareSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSoundWithRemotePlayer(const char*, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSoundWithRemotePlayer(ui32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::holdSoundWithRemotePlayer(u32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSound(const char*, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSound(u32, int, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSound(const char*, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSound(u32, u32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSoundWithRemotePlayer(const char*, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSoundWithRemotePlayer(ui32, u32, s32);
template nw4r::snd::SoundHandle* TRPSndObject<0>::startSoundWithRemotePlayer(u32, u32, s32);
template void TRPSndObject<0>::update();
template const nw4r::math::VEC3* TRPSndObject<0>::get3DPosition(int);
template u32 TRPSndObject<0>::getSoundId(ui32, int);
template const char* TRPSndObject<0>::getSoundLabel(const char*, int);
template u32 TRPSndObject<0>::getSoundId(u32, int);
template bool RPStateMachine<RPFshTimer>::IsStateOrNext(s32) const;
template void RPStateMachine<RPFshTimer>::ChangeState(s32);
