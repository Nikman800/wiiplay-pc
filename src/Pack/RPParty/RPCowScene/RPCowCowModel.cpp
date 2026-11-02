// Charge! cow model (RPCowCowModel): the knitted cow and its rider's animations. Unit
// 0x802B66D4-0x802B8508 (dtk/game_units.tsv; the old RPCowPlayer_9 range was merged in).
// Functions in reverse address order; CalcAnim (ledger Cow_Player_calcFootEffects, 0x802B66D4)
// is defined last.
#include <Pack/RPParty/RPCowScene/RPCowCowModel.h>

#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPGraphics/RPGrpModelAnm.h>
#include <Pack/RPKernel/RPSysKokeshi.h>
#include <Pack/RPKernel/RPSysKokeshiGenInfo.h>
#include <Pack/RPKernel/RPSysKokeshiOverloadInfo.h>
#include <Pack/RPUtility/RPUtlRandom.h>

#include <nw4r/math.h>

// Ledger RP_approachStep: *pVel = rate * (target - *pValue); *pValue += *pVel. Floats and
// pointers use separate registers; this parameter order gives the original argument
// scheduling (target first did not) [M]. RP_springStep: a damped spring step.
extern "C" void RP_approachStep(f32* pValue, f32* pVel, f32 target, f32 rate);
#if defined(PORT_HOST)
// PORT_HOST: RPCollisionShape.cpp defines these with the pointers first. The PowerPC ABI (and
// x86-64 System V) pass floats and pointers in separate registers, so the order did not
// matter; Windows x64 assigns registers by position. These wrappers keep this unit's calls
// and call the definitions' form (port/docs/HOST_BUILD.md §41).
extern "C" void RP_springStep_Def(f32*, f32*, f32, f32) __asm__("RP_springStep");
static inline void RP_springStep(f32 target, f32 rate, f32* pValue, f32* pVel) {
    RP_springStep_Def(pValue, pVel, target, rate);
}
#else
extern "C" void RP_springStep(f32 target, f32 rate, f32* pValue, f32* pVel);
#endif
// Column of a 3x4 matrix (ledger RP_Mtx34_getColumn, RPCollisionShape.cpp) [M]
extern "C" EGG::Vector3f RP_Mtx34_getColumn(const nw4r::math::MTX34& rMtx, int col);

namespace {
inline f32 Clamp01(f32 x) {
    return x > 1.0f ? 1.0f : (x < 0.0f ? 0.0f : x);
}
}

bool RPCowCowModel::SetIntroState() {
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 6);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 6, 0.0f);
    mState = 1;
    mLandTimer = 1.0f;
    return true;
}

bool RPCowCowModel::SetRunState() {
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 6);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 6, 0.0f);
    mState = 0;
    mLandTimer = 0.0f;
    return true;
}

bool RPCowCowModel::PlayJumpAnim() {
    if (mState != 2 && mState != 4) {
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 7);
        mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 7, 0.0f);
        mState = 2;
        mLandTimer = 0.0f;
        return true;
    }
    return false;
}

bool RPCowCowModel::PlayLandAnim() {
    if (mState == 2 && mState != 4) {
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 7, 8);
        mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 8, 0.0f);
        mState = 3;
        mLandTimer = 0.0f;
        return true;
    }
    return false;
}

void RPCowCowModel::PlayGoalAnim() {
    if (mState != 4) {
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 9);
        mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 9, 0.0f);
        mState = 4;
        mLandTimer = 0.0f;
    }
}

void RPCowCowModel::ClearLandState() {
    if (mState == 3) {
        mState = 0;
    }
}


void RPCowCowModel::EditJointLocalMtx(RPGrpModel* pModel, u16 idx, nw4r::math::MTX34* pMtx,
                                      nw4r::math::VEC3* pScale) {
    static u16 sJointCache[3];
    if (sJointCache[0] == 0) {
        sJointCache[0] = 1;
        sJointCache[1] = pModel->GetJointIndex("cow_root");
        sJointCache[2] = pModel->GetJointIndex("j_tail");
    }
    if (idx == sJointCache[1]) {
        nw4r::math::MTX34 rot;
        nw4r::math::VEC3 trans;
        trans = nw4r::math::VEC3(0.0f, 0.0f, -6.15f);
        nw4r::math::MTX34Trans(pMtx, &trans, pMtx);
        nw4r::math::MTX34RotXYZRad(&rot, mSprings[4].mAngle, 0.0f, 0.0f);
        nw4r::math::MTX34Mult(pMtx, pMtx, &rot);
        trans = nw4r::math::VEC3(0.0f, 0.0f, 6.15f);
        nw4r::math::MTX34Trans(pMtx, pMtx, &trans);
    } else if (idx == sJointCache[2]) {
        nw4r::math::MTX34 rot;
        nw4r::math::MTX34RotXYZRad(&rot, 0.0f, mSprings[7].mAngle, 0.0f);
        nw4r::math::MTX34Mult(pMtx, pMtx, &rot);
        nw4r::math::MTX34RotXYZRad(&rot, mSprings[6].mAngle, 0.0f, 0.0f);
        nw4r::math::MTX34Mult(pMtx, pMtx, &rot);
    }
    if (idx + 1 < pModel->GetJointNum()) {
        pModel->SetCallbackJointIndex(idx + 1);
    }
}

RPCowCowModel::RPCowCowModel(s32 player) : mPlayer(player) {
    RPSysKokeshiGenInfo info;
    info.SetViewNo(2);
    info.SetGenType(RPSysKokeshiManager::GenType_Player);
    info.SetPlayerNo(mPlayer);
    info.SetBodyType(4);
    mpAvatar = new RPSysKokeshi(&info);
}

RPCowCowModel::~RPCowCowModel() {}

void RPCowCowModel::LoadModels() {
    if (mPlayer == 0) {
        mpModel = RPGrpModel::Construct(0x22, 2, 0, 0);
    } else {
        mpModel = RPGrpModel::Construct(0x23, 2, 0, 0);
    }
    mpModelAnm = mpModel->GetModelAnm();
    mpModelAnm->CreateBuffer(RPGrpModelAnm::Anm_Chr, 8);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 0, 0x28, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 1, 0x29, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 2, 0x2A, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 3, 0x2B, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 4, 0x24, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 5, 0x25, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 6, 0x26, 0);
    mpModelAnm->Regist(RPGrpModelAnm::Anm_Chr, 7, 0x27, 0);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 0, 0);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 1, 1);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 2, 2);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 3, 3);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 4, 4);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 5, 5);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 6);
    mpModelAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 7, 7);
    mpModel->CreateBoundingInfo(1);
    mpModel->SetDrawGroup(2);

    RPSysKokeshiOverloadInfo info;
    info.SetClothesType(mPlayer != 0 ? 11 : 10);
    mpAvatar->LoadResource(&info);
    mpAvatar->ApplyLightTexture(0);
    mpAvatarModel = mpAvatar->GetBodyModel();
    mpAvatarAnm = mpAvatarModel->GetModelAnm();
    mpAvatarAnm->CreateBuffer(RPGrpModelAnm::Anm_Chr, 10);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 0, 0x31, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 1, 0x32, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 2, 0x34, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 3, 0x33, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 4, 0x36, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 5, 0x35, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 6, 0x37, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 7, 0x38, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 8, 0x39, 0);
    mpAvatarAnm->Regist(RPGrpModelAnm::Anm_Chr, 9, 0x3A, 0);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 0, 0);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 1, 1);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 2, 2);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 3, 3);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 4, 4);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 5, 5);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 6);
    mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 7, 9);
    mpAvatarModel->CreateBoundingInfo(1);
    mpAvatarModel->SetDrawGroup(2);
    mpAvatar->GetNigaoeModel()->CreateBoundingInfo(1);
    mpAvatar->GetNigaoeModel()->SetDrawGroup(2);
}

void RPCowCowModel::Reset() {
    mState = 0;
    unk1A4 = 0.0f;
    unk1A8 = 0.0f;
    for (int i = 0; i < 8; i++) {
        mSprings[i].Reset();
    }
    mLandTimer = 0.0f;
    unk19C = 0.0f;
    f32 numFrame = mpModelAnm->GetNumFrame(RPGrpModelAnm::Anm_Chr, 0);
    f32 frame = numFrame * RPUtlRandom::getF32();
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 0, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 1, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 2, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 3, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 4, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 5, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 6, frame);
    mpModelAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 7, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 0, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 1, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 2, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 3, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 4, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 5, frame);
    mpAvatarAnm->SetFrame(RPGrpModelAnm::Anm_Chr, 6, frame);
}

void RPCowCowModel::CalcAnim() {
    static u16 sNodeCache[8];

    RP_approachStep(&mSprings[0].mAngle, &mSprings[0].mSpeed, unk1A4, 0.4f);
    RP_approachStep(&mSprings[1].mAngle, &mSprings[1].mSpeed, unk1A8, 0.4f);
    RP_approachStep(&mSprings[4].mAngle, &mSprings[4].mSpeed, unk1A4 * 0.6f, 0.5f);
    RP_approachStep(&mSprings[5].mAngle, &mSprings[5].mSpeed, unk1A8 * 0.6f, 1.0f);
    if (mSprings[4].mAngle > sRootPitchMax) {
        mSprings[4].mAngle = sRootPitchMax;
    } else if (mSprings[4].mAngle < -0.8f) {
        mSprings[4].mAngle = -0.8f;
    }
    mSprings[6].mSpeed += -unk1A0 * 0.04f;
    RP_springStep(-mSprings[4].mAngle, 0.02f, &mSprings[6].mAngle, &mSprings[6].mSpeed);
    RP_springStep(mSprings[5].mAngle, 0.02f, &mSprings[7].mAngle, &mSprings[7].mSpeed);
    if (mSprings[6].mAngle > 0.8f) {
        mSprings[6].mAngle = 0.8f;
    } else if (mSprings[6].mAngle < -0.1f) {
        mSprings[6].mAngle = -0.1f;
    }
    RP_approachStep(&mSprings[2].mAngle, &mSprings[2].mSpeed, mSprings[0].mAngle * 2.0f, 0.1f);
    RP_approachStep(&mSprings[3].mAngle, &mSprings[3].mSpeed, mSprings[1].mAngle * 4.0f, 0.1f);

    if (sNodeCache[0] == 0) {
        sNodeCache[0] = 1;
        sNodeCache[1] = mpModel->GetJointIndex("cow_root");
        sNodeCache[2] = mpModel->GetJointIndex("j_head");
        sNodeCache[3] = mpModel->GetJointIndex("j_body");
        sNodeCache[4] = mpModel->GetJointIndex("j_L_front");
        sNodeCache[5] = mpModel->GetJointIndex("j_R_front");
        sNodeCache[6] = mpModel->GetJointIndex("j_L_back");
        sNodeCache[7] = mpModel->GetJointIndex("j_R_back");
    }

    f32 t = unk19C;
    f32 modelRate = t * (sModelAnmRateMax - 0.5f) + 0.5f;
    f32 avatarRate = t * (sAvatarAnmRateMax - 0.5f) + 0.5f;

    {
        f32 fwd = mSprings[0].mAngle >= 0.0f ? mSprings[0].mAngle : 0.0f;
        f32 side = (1.0f + mSprings[1].mAngle) * 0.5f;
        f32 back = mSprings[0].mAngle < 0.0f ? -mSprings[0].mAngle : 0.0f;
        fwd = Clamp01(fwd);
        side = Clamp01(side);
        back = Clamp01(back);
        f32 w10 = fwd * (1.0f - side);
        f32 w11 = fwd * side;
        f32 w00 = (1.0f - fwd) * (1.0f - side);
        f32 w01 = (1.0f - fwd) * side;

        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 0, t * w00, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 1, t * w01, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 2, t * w10, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 3, t * w11, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 4, (1.0f - t) * w00, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 5, (1.0f - t) * w01, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 6, (1.0f - t) * w10, 0);
        mpModelAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 7, (1.0f - t) * w11, 0);
    }
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 1, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 2, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 3, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 4, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 5, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 6, modelRate);
    mpModelAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 7, modelRate);
    mpModel->UpdateFrame();

    f32 fwd = mSprings[2].mAngle >= 0.0f ? mSprings[2].mAngle : 0.0f;
    f32 side = (1.0f + mSprings[3].mAngle) * 0.5f;
    f32 back = mSprings[2].mAngle < 0.0f ? -mSprings[2].mAngle : 0.0f;
    f32 land = mLandTimer;
    fwd = Clamp01(fwd);
    side = Clamp01(side);
    back = Clamp01(back);
    land = Clamp01(land);
    f32 fwd10 = fwd * (1.0f - side);
    f32 fwd11 = fwd * side;
    f32 fwd00 = (1.0f - fwd) * (1.0f - side);
    f32 fwd01 = (1.0f - fwd) * side;
    f32 back10 = back * (1.0f - side);
    f32 back11 = back * side;
    f32 back00 = (1.0f - back) * (1.0f - side);
    f32 back01 = (1.0f - back) * side;

    switch (mState) {
    case 0:
        mLandTimer = mLandTimer - 0.1f > 0.0f ? mLandTimer - 0.1f : 0.0f;
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 6, 0.0f, 0);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 7, land, 0);
        break;
    case 1:
        mLandTimer = 1.0f;
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 6);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 6, land, 0);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 7, 0.0f, 0);
        break;
    case 2:
        mLandTimer = mLandTimer + 0.1f < 1.0f ? mLandTimer + 0.1f : 1.0f;
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 7);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 6, land, 0);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 7, 0.0f, 0);
        break;
    case 3:
        mLandTimer = mLandTimer + 0.2f < 1.0f ? mLandTimer + 0.2f : 1.0f;
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 7);
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 7, 8);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 6, 1.0f - land, 0);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 7, land, 0);
        break;
    case 4:
        mLandTimer = mLandTimer + 0.1f < 1.0f ? mLandTimer + 0.1f : 1.0f;
        mpAvatarAnm->SetBlendAnm(RPGrpModelAnm::Anm_Chr, 6, 9);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 6, land, 0);
        mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 7, 0.0f, 0);
        break;
    }

    if (mState != 3) {
        if (fwd > 0.0f) {
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 0, (1.0f - land) * fwd00, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 1, (1.0f - land) * fwd01, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 2, (1.0f - land) * fwd10, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 3, (1.0f - land) * fwd11, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 4, 0.0f, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 5, 0.0f, 0);
        } else {
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 0, (1.0f - land) * back00, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 1, (1.0f - land) * back01, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 2, 0.0f, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 3, 0.0f, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 4, (1.0f - land) * back10, 0);
            mpAvatarAnm->SetBlendWeight(RPGrpModelAnm::Anm_Chr, 5, (1.0f - land) * back11, 0);
        }
    }
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, avatarRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 1, avatarRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 2, avatarRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 3, avatarRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 4, avatarRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 5, avatarRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 6, 0.5f);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 7, sJumpAnmRate);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 8, 1.2f);
    mpAvatarAnm->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 9, sGoalAnmRate);
    mpAvatarModel->UpdateFrame();

    mpModel->GetWorldMtx(sNodeCache[1], &mRootMtx);
    mpModel->GetWorldMtx(sNodeCache[2], &mHeadMtx);
    for (int i = 0; i < 4; i++) {
        switch (i) {
        case 0:
            mpModel->GetWorldMtx(sNodeCache[4], &mFootMtx[i]);
            mFootPos[i] = RP_Mtx34_getColumn(mFootMtx[i], 3) -
                          RP_Mtx34_getColumn(mRootMtx, 1) * 5.0f;
            break;
        case 1:
            mpModel->GetWorldMtx(sNodeCache[5], &mFootMtx[i]);
            mFootPos[i] = RP_Mtx34_getColumn(mFootMtx[i], 3) -
                          RP_Mtx34_getColumn(mRootMtx, 1) * 5.0f;
            break;
        case 2:
            mpModel->GetWorldMtx(sNodeCache[6], &mFootMtx[i]);
            mFootPos[i] = RP_Mtx34_getColumn(mFootMtx[i], 3) -
                          RP_Mtx34_getColumn(mRootMtx, 1) * 4.5f;
            break;
        case 3:
            mpModel->GetWorldMtx(sNodeCache[7], &mFootMtx[i]);
            mFootPos[i] = RP_Mtx34_getColumn(mFootMtx[i], 3) -
                          RP_Mtx34_getColumn(mRootMtx, 1) * 4.5f;
            break;
        }
    }
}

const f32 RPCowCowModel::sModelAnmRateMax = 1.0f;
const f32 RPCowCowModel::sAvatarAnmRateMax = 1.0f;
const f32 RPCowCowModel::sJumpAnmRate = 1.0f;
const f32 RPCowCowModel::sGoalAnmRate = 1.0f;
const f32 RPCowCowModel::sRootPitchMax = 0;
