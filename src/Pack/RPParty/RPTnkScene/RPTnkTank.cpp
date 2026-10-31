#include <Pack/RPParty/RPTnkScene/RPTnkTank.h>
#include <Pack/RPParty/RPTnkScene/RPTnkAi.h>
#include <Pack/RPParty/RPTnkScene/RPTnkCamera.h>
#include <Pack/RPParty/RPTnkScene/RPTnkCheckMark.h>
#include <Pack/RPParty/RPTnkScene/RPTnkField.h>
#include <Pack/RPParty/RPTnkScene/RPTnkExGuide.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkLyt.h>
#include <Pack/RPParty/RPTnkScene/RPTnkPlayerController.h>
#include <Pack/RPParty/RPTnkScene/RPTnkStageMap.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTask.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <Pack/RPUtility/RPUtlDohMath.h>
#include <egg/gfxe/eggScreen.h>

using namespace nw4r::math;

extern "C" RPAxes RP_mtx33Identity;
extern "C" VEC3 RP_vecZero, RP_vecOne, RP_vecEy;
extern "C" RPAxes sFloorQuadBasis;

// The on-disk enemy record is copied by value by the original parameter getter.
struct TnkEnemyParamsData {
    be_s32 stealth; be_f32 f04; be_s32 i08, i0C, i10; be_f32 f14, f18, f1C;
    be_s32 i20, i24; be_f32 f28, f2C, f30; be_s32 i34, i38;
    be_f32 f3C, f40, f44, f48; be_s32 i4C; be_f32 f50; be_s32 i54;
    be_f32 f58, f5C, f60, f64, f68; be_s32 i6C; be_f32 f70; be_s32 i74;
    be_f32 f78, f7C, f80; be_s32 i84, i88, i8C, i90;
    be_f32 f94, f98; be_s32 i9C; be_f32 fA0; be_s32 iA4;
};

struct TnkEnemyParams : TnkEnemyParamsData {
    TnkEnemyParams(const TnkEnemyParamsData& r) { static_cast<TnkEnemyParamsData&>(*this) = r; }
};

// param/TnkGameParam.bin: lives, then one record per enemy type (stage records follow).
struct TnkGameParamData {
    s32 lives;
    TnkEnemyParamsData enemies[10];
};

inline TnkEnemyParamsData TnkGetEnemyParams(const void* pData, s32 type) {
    return static_cast<const TnkGameParamData*>(pData)->enemies[type];
}

// NONMATCHING-DEBT: volatile forces KeepNearest*Hit to reload mDist (`lfs f0,4(r4)`) after the
// compare and store mId before mDist; natural `mShellHit = rHit;` reuses f1 and stores stfs first
// (2 instructions per caller). Tried: struct assign, field copy, early return, bool/f32 compare
// inlines, by-value copy helper.
// Trace (2026-10-29): the optimizer IR keeps the second rHit.mDist load; the backend merges it with
// the compare's load (its FPR vreg is left unused in the PCode), then the scheduler puts stfs
// first. Pointer/reference copy helpers, named id/dist locals and a pHit local all merge it.
// Rule check (2026-10-29, vn_probe + alias_probe): blocked. The copy block is the compare's only
// successor (extended basic block, so it inherits the compare's load), and nothing in it can kill
// the load: rHit is `const HitInfo&` (mangled RC), so its dummy is created const=1 and stays out of
// the worst-case set, and the mId store through `this` can't conflict with it. No call, no
// redefinition of the first value's register. Only a non-const pointee would reload mDist.
inline void TnkCopyHit(RPTnkTank::HitInfo* dst, const RPTnkTank::HitInfo* src) {
    const volatile RPTnkTank::HitInfo* in = src;
    volatile RPTnkTank::HitInfo* out = dst;
    s32 id = in->mId;
    f32 dist = in->mDist;
    out->mId = id;
    out->mDist = dist;
}

inline f32 TnkTreadCoord(const f32& axis, const f32& pos) { return 10.0f * axis + pos; }


inline f32 TnkEnemyHitRadius() { return 15.0f; }
inline f32 TnkPlayerHitRadius() { return 15.0f; }

inline VEC2 TnkProjectTankPoint(const VEC3& pos) {
    VEC2 screen;
    RPTnkCamera::sInstance->ProjectToScreen(&screen, &pos);
    return screen;
}

inline void TnkSetBodyMtx(MTX34& dst, const RPAxes& axes, const VEC3& scale, const VEC3& pos) {
    dst._00 = scale.x * axes.x.x;
    dst._01 = scale.y * axes.y.x;
    dst._02 = scale.z * axes.z.x;
    dst._03 = pos.x;
    dst._10 = scale.x * axes.x.y;
    dst._11 = scale.y * axes.y.y;
    dst._12 = scale.z * axes.z.y;
    dst._13 = pos.y;
    dst._20 = scale.x * axes.x.z;
    dst._21 = scale.y * axes.y.z;
    dst._22 = scale.z * axes.z.z;
    dst._23 = pos.z;
}

inline void TnkCopyMtx(MTX34& dst, const MTX34& src) {
    dst._00 = src._00; dst._01 = src._01; dst._02 = src._02; dst._03 = src._03;
    dst._10 = src._10; dst._11 = src._11; dst._12 = src._12; dst._13 = src._13;
    dst._20 = src._20; dst._21 = src._21; dst._22 = src._22; dst._23 = src._23;
}

inline void TnkAccumulateScaled(VEC3& minimum, VEC3& maximum, f32 amount, const VEC3* normal) {
    f32 x = amount * normal->x;
    f32 z = amount * normal->z;
    f32 y = amount * normal->y;
    f32 a = minimum.x;
    if (a > x) a = x;
    minimum.x = a;
    f32 b = minimum.y;
    if (b > y) b = y;
    minimum.y = b;
    f32 c = minimum.z;
    if (c > z) c = z;
    minimum.z = c;
    maximum.x = maximum.x < x ? x : maximum.x;
    maximum.y = maximum.y < y ? y : maximum.y;
    maximum.z = maximum.z < z ? z : maximum.z;
}

VEC3 Tnk_cardinalDirs(0.0f, 0.0f, -1.0f);
static VEC3 Tnk_cardinalBack(0.0f, 0.0f, 1.0f);
static VEC3 Tnk_cardinalLeft(-1.0f, 0.0f, 0.0f);
static VEC3 Tnk_cardinalRight(1.0f, 0.0f, 0.0f);

RPTnkTank::RPTnkTank() {
    mStateFuncs[0] = &RPTnkTank::StateIdle;
    mStateFuncs[1] = &RPTnkTank::StatePlayerControl;
    mStateFuncs[2] = &RPTnkTank::StateEnemyControl;
    mStateFuncs[3] = &RPTnkTank::StatePlayerStop;
    mStateFuncs[4] = &RPTnkTank::StateEnemyStop;
    mIsEnemy = false;
}

RPTnkTank::~RPTnkTank() {}

// The chained scale assignment matters: it changes the scheduler's window so that the
// mPos.y load for _13 is hoisted while _13 keeps its natural row order.
void RPTnkTank::OnSpawn() {
    mHitRadius = 16.0f;
    mHitOffsetAlt.x = 0.0f;
    mHitOffsetAlt.y = 0.0f;
    mHitOffsetAlt.z = 9.0f;
    mScale.z = mScale.y = mScale.x = 1.6f;
    mBodyHeight = 11.0f * mScale.y;
    mRot = RP_mtx33Identity;
    mTurretRot = mRot;
    mSpeed = 0.0f;
    mMaxSpeed = 1.0f;
    mAlive = 1;
    mShellHit = sNoHit;
    mMineHit = sNoHit;
    mBodyMtx._00 = mScale.x * mRot.x.x;
    mBodyMtx._01 = mScale.y * mRot.y.x;
    mBodyMtx._02 = mScale.z * mRot.z.x;
    mBodyMtx._03 = mPos.x;
    mBodyMtx._10 = mScale.x * mRot.x.y;
    mBodyMtx._11 = mScale.y * mRot.y.y;
    mBodyMtx._12 = mScale.z * mRot.z.y;
    mBodyMtx._13 = mPos.y;
    mBodyMtx._20 = mScale.x * mRot.x.z;
    mBodyMtx._21 = mScale.y * mRot.y.z;
    mBodyMtx._22 = mScale.z * mRot.z.z;
    mBodyMtx._23 = mPos.z;
    TnkCopyMtx(mCannonMtx, mBodyMtx);
    f32 treadX = TnkTreadCoord(mRot.x.x, mPos.x);
    f32 treadZ = TnkTreadCoord(mRot.x.z, mPos.z);
    mTreadPos[0].x = treadX;
    mTreadPos[0].y = 0.0f;
    mTreadPos[0].z = treadZ;
    mTreadPos[1].x = treadX;
    mTreadPos[1].y = 0.0f;
    mTreadPos[1].z = treadZ;
    mPrevPos.x = mBodyMtx._03;
    mPrevPos.y = mBodyMtx._13;
    mPrevPos.z = mBodyMtx._23;
    mTreadDistance = 0.0f;
    mpTankSound->ClearTread();
    mpPlayerCtrl->Reset();
    mState = 0;
}

void RPTnkTank::ApplySpawnParam(u32 param) { mSpawnParam = param & 0xF; }

void RPTnkTank::InitOrientation() {
    f32 slope = RPTnkField::sInstance->GetAspect();
    bool a = mPos.z > slope * mPos.x;
    bool b = mPos.z > -slope * mPos.x;
    if (a && b) mRot.z = Tnk_cardinalDirs;
    else if (!a && !b) mRot.z = Tnk_cardinalBack;
    else if (!a && b) mRot.z = Tnk_cardinalLeft;
    else if (a && !b) mRot.z = Tnk_cardinalRight;
    mRot.y = RP_vecEy;
    PSVECCrossProduct(mRot.y, mRot.z, mRot.x);
    mTurretRot = mRot;
}
// String-pool references only (instructions match).
struct TnkPieceColors {
    u8 rgb[11][3];
};
extern const TnkPieceColors sTnkPieceColors;
void RPTnkTank::SpawnBombPieceEffect() {
    f32 px, py, pz;
    pz = mPos.z;
    py = mPos.y;
    px = mPos.x;
    f32 s = 1.6f;
    VEC3 pos(px, py, pz);
    VEC3 scale(s, s, s);
    TnkPieceColors colors = sTnkPieceColors;
    RPSysEffect pieces("Tnk_bomb01_piece", 0, 0);
    pieces.RPSysEffect::create();
    pieces.RPSysEffect::setPos(pos);
    pieces.RPSysEffect::setScale(scale);
    pieces.RPSysEffect::setColor(colors.rgb[mModelIdx][0], colors.rgb[mModelIdx][1], colors.rgb[mModelIdx][2]);
    pieces.RPSysEffect::update();
    RPSysEffect bomb("Tnk_bomb01", 0, 0);
    bomb.RPSysEffect::create();
    bomb.RPSysEffect::setPos(pos);
    bomb.RPSysEffect::setScale(scale);
    bomb.RPSysEffect::update();
}
void RPTnkTank::SetSeVariant() {
    if (!mIsEnemy) {
        if (mIndex == 0) { mpTankSound->SetTreadIndex(0); return; }
        if (mIndex != 1) return;
        mpTankSound->SetTreadIndex(1);
        return;
    }
    if (mEnemyType == 1) { mpTankSound->SetTreadIndex(2); return; }
    if (mEnemyType == 2) { mpTankSound->SetTreadIndex(3); return; }
    if (mEnemyType == 3) { mpTankSound->SetTreadIndex(4); return; }
    if (mEnemyType == 4) { mpTankSound->SetTreadIndex(5); return; }
    if (mEnemyType == 5) { mpTankSound->SetTreadIndex(6); return; }
    if (mEnemyType == 6) { mpTankSound->SetTreadIndex(7); return; }
    if (mEnemyType == 7) { mpTankSound->SetTreadIndex(8); return; }
    if (mEnemyType == 8) { mpTankSound->SetTreadIndex(9); return; }
    if (mEnemyType != 9) return;
    mpTankSound->SetTreadIndex(10);
}

void RPTnkTank::StateIdle() {
    if (!mIsEnemy) mpPlayerCtrl->AimAtCursor(&mTurretRot, mPos, mIndex);
}

void RPTnkTank::StatePlayerControl() {
    if (mMineHit.mId != 0) {
        if (--mAlive == 0) Explode(mMineHit.mId);
    } else if (mShellHit.mId != 0) {
        if (--mAlive == 0) Explode(mShellHit.mId);
    } else {
        mpPlayerCtrl->AimAtCursor(&mTurretRot, mPos, mIndex);
        VEC3 pos = mPos;
        pos.y += 11.0f * mScale.y;
        mpPlayerCtrl->HandleFireInput(25.6f, &mTurretRot, pos, mIndex);
        mpPlayerCtrl->CalcMoveVelocity(&mRot, &mVelocity, mIndex);
        mSpeed = PSVECMag(mVelocity);
        mMaxSpeed = mpPlayerCtrl->GetMaxSpeed();
        f32 ratio = mMaxSpeed != 0.0f ? mSpeed / mMaxSpeed : 0.0f;
        f32 floor = 0.0f > ratio ? 0.0f : ratio;
        f32 speed = 1.0f < floor ? 1.0f : floor;
        mpTankSound->UpdateTread(speed, 0);
    }
}
void RPTnkTank::StateEnemyControl() {
    if (mMineHit.mId != 0) {
        if (--mAlive == 0) Explode(mMineHit.mId);
    } else if (mShellHit.mId != 0) {
        if (--mAlive == 0) Explode(mShellHit.mId);
    } else {
        VEC3 pos = mPos;
        pos.y += 11.0f * mScale.y;
        mpAiCtrl->UpdateAttack(25.6f, &mTurretRot, pos);
        mSpeed = PSVECMag(mVelocity);
        mMaxSpeed = mpAiCtrl->GetMove()->GetSpeed();
        mpAiCtrl->UpdateMove(&mRot, &mVelocity, mPos);
        f32 ratio = mMaxSpeed != 0.0f ? mSpeed / mMaxSpeed : 0.0f;
        f32 floor = 0.0f > ratio ? 0.0f : ratio;
        f32 speed = 1.0f < floor ? 1.0f : floor;
        mpTankSound->UpdateTread(speed, 0);
    }
    if (mStealth) {
        VEC3 pos(mPos.x, mPos.y, mPos.z);
        mpStealthEffect->setPos(pos);
        VEC3 scale(1.6f, 1.6f, 1.6f);
        mpStealthEffect->setScale(scale);
        mpStealthEffect->update();
    }
}
void RPTnkTank::StatePlayerStop() {
    mpPlayerCtrl->AimAtCursor(&mTurretRot, mPos, mIndex);
    mVelocity = RP_vecZero;
    mSpeed = PSVECMag(mVelocity);
    mMaxSpeed = mpPlayerCtrl->GetMaxSpeed();
}
void RPTnkTank::StateEnemyStop() {
    VEC3 pos = mPos;
    pos.y += 11.0f * mScale.y;
    mVelocity.x = RP_vecZero.x;
    mVelocity.y = RP_vecZero.y;
    mVelocity.z = RP_vecZero.z;
    mSpeed = PSVECMag(mVelocity);
    mMaxSpeed = mpAiCtrl->GetMove()->GetSpeed();
}
void RPTnkTank::Create() {
    RPTnkActor::Create();
    mpPlayerCtrl = new RPTnkPlayerController(this);
    mpAiCtrl = new RPTnkAiController(this);
    mpStealthEffect = new RPSysEffect("Tnk_stealth_change", 0, 0);
    mpTankSound = new RPTnkTankSound;
}
inline void RPTnkTank::LoadTankModels(RPGrpModelResManager* manager, RPGrpHandle file, const char** names) {
    for (s32 i = 0; i < 11; ++i) {
        void* mdl = manager->GetPtr(RPGrpModelResManager::Type_ResMdl, file, names[i]);
        RPGrpHandle handle = manager->CreateData(RPGrpModelResManager::Type_ResMdl, mdl, 0);
        mpModels[i] = RPGrpModel::Construct(handle, 1, 0, 0);
        mpModels[i]->SetDrawGroup(0);
        mpModels[i]->SetDrawScene(0);
        mpModels[i]->SetUnk4(0);
        mpModels[i]->SetCallback(this);
    }
}

// DrawTreadMark's tread spacing per model; its .rodata copy precedes Load's name table.
static const f32 distances[11] = {10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10};

void RPTnkTank::Load() {
    const char* names[11] = {
        "tnk_tank_1p", "tnk_tank_2p", "tnk_tank_e_1", "tnk_tank_e_2",
        "tnk_tank_e_3", "tnk_tank_e_4", "tnk_tank_e_5", "tnk_tank_e_6",
        "tnk_tank_e_7", "tnk_tank_e_8", "tnk_tank_e_9"
    };
    EGG::Archive* archive = RPSysResourceManager::LoadGameCommonArchive(-1, NULL);
    RPGrpModelResManager* manager = RPGrpModelResManager::GetCurrent();
    void* data = RPSysResourceManager::GetFileFromArchive(archive, "G3D/tnk_tank.brres", NULL);
    RPGrpHandle file = manager->CreateData(RPGrpModelResManager::Type_ResFile, data, 0);
    LoadTankModels(manager, file, names);
}
void RPTnkTank::Reset() {
    RPTnkActor::Reset();
    mBodyHeight = 1.0f;
    mTurretRot = RP_mtx33Identity;
    mSpeed = 0.0f;
    mMaxSpeed = 1.0f;
    mAlive = 0;
    mShellHit = sNoHit;
    mMineHit = sNoHit;
    mModelIdx = 0;
    mState = 0;
    mStealth = false;
    mEnemyType = 0;
    mSpawnParam = 0;
    mpTankSound->ClearTread();
    mpPlayerCtrl->Reset();
    mpAiCtrl->Reset();
}

void RPTnkTank::ApplyVelocity() {
    mPrevPos = mPos;
    mPos.x += mVelocity.x;
    mPos.y += mVelocity.y;
    mPos.z += mVelocity.z;
}

void RPTnkTank::ClearForcesStep() {
    RPTnkActor::ClearForcesStep();
    mShellHit = sNoHit;
    mMineHit = sNoHit;
}

void RPTnkTank::CollideWithField() {
    mHitRadius = 16.0f;
    RPTnkTankManager::sInstance->SeparateTank(mIndex, mIsEnemy);
    mHitRadius = 16.0f;
    RPTnkField::sInstance->CollideActor(this, 0);
}
void RPTnkTank::IntegrateStep() {
    RPTnkActor::IntegrateStep();
    VEC3 leftPos;
    VEC3 rightPos;
    leftPos.x = 10.0f * mRot.x.x + mPos.x;
    leftPos.y = 0.0f;
    leftPos.z = 10.0f * mRot.x.z + mPos.z;
    rightPos.x = 10.0f * mRot.x.x + mPos.x;
    rightPos.y = 0.0f;
    rightPos.z = 10.0f * mRot.x.z + mPos.z;
    VEC3 left;
    left.x = leftPos.x - mTreadPos[0].x;
    left.y = 0.0f;
    left.z = leftPos.z - mTreadPos[0].z;
    VEC3 right;
    right.x = rightPos.x - mTreadPos[1].x;
    right.y = 0.0f;
    right.z = rightPos.z - mTreadPos[1].z;
    f32 a = PSVECMag(left);
    f32 b = PSVECMag(right);
    mTreadDistance += b <= a ? a : b;
    mTreadPos[0] = leftPos;
    mTreadPos[1] = rightPos;
}
void RPTnkTank::Calculate() {
    (this->*mStateFuncs[mState])();
    mpSound->set3DPosition(mPos);
    mpTankSound->set3DPosition(mPos);
}
void RPTnkTank::CalcModelMtx() {
    RPGrpModel* model = mpModels[mModelIdx];
    if (mState == 2 && mStealth) model->SetVisible(false);
    else model->SetVisible(true);
    MTX34 mtx;
    TnkSetBodyMtx(mtx, mRot, RP_vecOne, mPos);
    nw4r::g3d::ScnObj* scn = model->GetScnObj();
    if (scn) scn->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &mtx);
    model->SetBaseScale(mScale);
    model->Calc();
    model->Entry();
    model->UpdateFrame();
}
void RPTnkTank::EditWorldMtx(RPGrpModel* pModel) {
    MTX34* world = pModel->ReferWorldMtx(1);
    VEC3 pos = mPos;
    pos.y += mBodyHeight;
    TnkSetBodyMtx(*world, mTurretRot, mScale, pos);
    MTX34 rotate;
    MTX34RotXYZFIdx(&rotate, 0.0f, 0.0f, 64.0f);
    PSMTXConcat(*world, rotate, *world);
    PSMTXCopy(*pModel->ReferWorldMtx(pModel->GetJointIndex("base")), mBodyMtx);
    PSMTXCopy(*pModel->ReferWorldMtx(pModel->GetJointIndex("cannon")), mCannonMtx);
}
void RPTnkTank::SpawnPlayer(const VEC3& rPos, s32 param, BOOL show) {
    Spawn(rPos, param);
    mIsEnemy = false;
    InitOrientation();
    mModelIdx = mIndex;
    mState = !show;
    SetSeVariant();
}
inline VEC2 TnkToVec2(const VEC2& rVec) { return rVec; }
inline void RPTnkTank::SetExGuideTarget() {
    VEC3 pos = mPos;
    pos.y += 11.0f * mScale.y;
    // Three by-value copies of the projected point, as the original's stack layout shows.
    RPTnkExGuide::sInstance->SetScreenPos(TnkToVec2(EGG::Vector2f(TnkProjectTankPoint(pos))));
    RPTnkExGuide::sInstance->SetTarget(this);
}


void RPTnkTank::SpawnEnemy(const VEC3& rPos, s32 type, s32 idx, BOOL show) {
    Spawn(rPos, idx);
    mIsEnemy = true;
    InitOrientation();
    mpAiCtrl->SetMode(type);
    if (RPTnkGameManager::sInstance->GetStage() == 0) {
        SetExGuideTarget();
    }
    mEnemyType = type;
    mModelIdx = type + 1;
    const void* data = RPTnkGameManager::sInstance->GetParam();
    TnkEnemyParams params = TnkGetEnemyParams(data, type);
    if (params.stealth != 0) mStealth = true;
    else mStealth = false;
    if (mEnemyType > 0) RPTnkGameStats::sInstance->AddKilledType(mEnemyType - 1);
    VEC3 move = mRot.z;
    mpAiCtrl->SetMoveDirection(move);
    VEC3 aim = mRot.z;
    mpAiCtrl->SetAimDirection(aim);
    s32 state = 2;
    if (show) state = 0;
    mState = state;
    SetSeVariant();
}
void RPTnkTank::Kill() {
    RPTnkActor::Kill();
    if (mIsEnemy) RPTnkTankManager::sInstance->DecEnemyAliveNum();
    mpTankSound->ClearTread();
}
void RPTnkTank::Activate() {
    if (mState == 0) {
        if (!mIsEnemy) mState = 1;
        else {
            mState = 2;
            if (mStealth) {
                mpStealthEffect->create();
                VEC3 scale;
                VEC3 pos(mPos.x, mPos.y, mPos.z);
                mpStealthEffect->setPos(pos);
                scale.x = 1.6f;
                scale.y = 1.6f;
                scale.z = 1.6f;
                mpStealthEffect->setScale(scale);
                mpStealthEffect->update();
                mpTankSound->startSound(18U, 0);
            }
        }
    }
}
void RPTnkTank::Explode(s32 hitId) {
    VEC3 pos = mPos;
    pos.y += 11.0f * mScale.y;
    EGG::Vector2f normalized = TnkProjectTankPoint(pos);
    normalized.x /= static_cast<u16>(static_cast<u32>(EGG::Screen::GetSizeXMax()) >> 1);
    normalized.y /= static_cast<u16>(static_cast<u32>(EGG::Screen::GetSizeYMax()) >> 1);
    if (mIsEnemy) {
        s32 type = mEnemyType - 1;
        if (hitId == 1) {
            RPTnkGameStats::sInstance->AddKill(0, type, 1);
            RPTnkLayoutManager::sInstance->SpawnHitPopup(VEC2(normalized), 1, 0);
            if (RPTnkGameManager::sInstance->GetModeIdx() == 0) {
                RPSndAudioMgr::instance()->startSystemSe(0x25U);
                RPSndAudioMgr::instance()->startSystemSeWithRemote(0x28U, 1);
            } else {
                RPSndAudioMgr::instance()->startSystemSe(0x26U);
                RPSndAudioMgr::instance()->startSystemSeWithRemote(0x29U, 1);
            }
        } else if (hitId == 2) {
            RPTnkGameStats::sInstance->AddKill(1, type, 1);
            RPTnkLayoutManager::sInstance->SpawnHitPopup(VEC2(normalized), 1, 1);
            if (RPTnkGameManager::sInstance->GetModeIdx() == 0) {
                RPSndAudioMgr::instance()->startSystemSe(0x25U);
                RPSndAudioMgr::instance()->startSystemSeWithRemote(0x28U, 1);
            } else {
                RPSndAudioMgr::instance()->startSystemSe(0x27U);
                RPSndAudioMgr::instance()->startSystemSeWithRemote(0x2AU, 2);
            }
        } else if (hitId == 3 && RPTnkGameManager::sInstance->GetModeIdx() == 0) {
            RPTnkGameStats::sInstance->AddKill(0, type, 1);
            RPTnkLayoutManager::sInstance->SpawnHitPopup(VEC2(normalized), 1, 0);
            if (RPTnkGameManager::sInstance->GetModeIdx() == 0) {
                RPSndAudioMgr::instance()->startSystemSe(0x25U);
                RPSndAudioMgr::instance()->startSystemSeWithRemote(0x28U, 1);
            } else {
                RPSndAudioMgr::instance()->startSystemSe(0x26U);
                RPSndAudioMgr::instance()->startSystemSeWithRemote(0x29U, 1);
            }
        }
        RPTnkCheckMarkPool::sInstance->Spawn(mPos, 2);
        RPTnkStageMap::sInstance->SetEnemyActive(mSpawnParam, false);
    } else {
        if (mIndex == 0) RPTnkCheckMarkPool::sInstance->Spawn(mPos, 0);
        else if (mIndex == 1) RPTnkCheckMarkPool::sInstance->Spawn(mPos, 1);
        EGG::CoreControllerMgr::instance()->getNthController(static_cast<u16>(mIndex))
            ->startSimpleRumble(0x0FFFFFFF, 24, 32);
    }
    mSpeed = 0.0f;
    SpawnBombPieceEffect();
    mpTankSound->startSound(14U, 0);
    if (!mIsEnemy) mpTankSound->startSound(13U, 0);
    Kill();
}
template void TRPSndObject<0>::detailStartSound(nw4r::snd::SoundHandle*, u32, u32);

void RPTnkTank::PlayShotEffect() {
    RPSysEffect effect("Tnk_shot", 0, 0);
    effect.RPSysEffect::create();
    effect.RPSysEffect::setMtx(mCannonMtx);
    effect.RPSysEffect::update();
}
void RPTnkTank::SetSePlayerId() {
    if (mIsEnemy) { mpTankSound->SetRemoteFlag(0); return; }
    u32 flag = 0;
    if (mIndex == 0) flag = 1;
    else if (mIndex == 1) flag = 2;
    mpTankSound->SetRemoteFlag(flag);
}
void RPTnkTank::PlayFireSeByType() {
    if (!mIsEnemy) {
        if (mIndex == 0) mpTankSound->startSound(1U, 0);
        if (mIndex == 1) mpTankSound->startSound(2U, 0);
    } else {
        switch (mEnemyType) {
        case 1: mpTankSound->startSound(3U, 0); break;
        case 2: mpTankSound->startSound(4U, 0); break;
        case 4: mpTankSound->startSound(6U, 0); break;
        case 5: mpTankSound->startSound(7U, 0); break;
        case 6: mpTankSound->startSound(8U, 0); break;
        case 8: mpTankSound->startSound(10U, 0); break;
        }
    }
}
void RPTnkTank::PlayLayMineSe() {
    mpTankSound->startSound(20U, 0);
    if (!mIsEnemy) mpTankSound->startSound(19U, 0);
}
void RPTnkTank::HoldMineFuseSe() {
    if (!mIsEnemy) mpTankSound->holdSound(23U, 0);
}
BOOL RPTnkTank::TestHit(RPTnkActor* pOther, f32* pDist) {
    VEC3 other = pOther->GetHitCenter();
    f32 radiusSq = pOther->GetHitRadius();
    radiusSq = 14.0f + radiusSq;
    radiusSq *= radiusSq;
    f32 xx = other.x - (mPos.x + mHitOffset.x);
    xx *= xx;
    if (xx > radiusSq) return FALSE;
    f32 zz = other.z - (mPos.z + mHitOffset.z);
    zz *= zz;
    if (zz > radiusSq) return FALSE;
    if (xx + zz > radiusSq) return FALSE;
    if (pDist) *pDist = FSqrt(xx + zz);
    return TRUE;
}

// Separate enemy/player radii merged only after the branch is laid out: the comparison stays and
// the centers are reloaded from the stack.
BOOL RPTnkTank::TestHitAlt(RPTnkActor* pOther, f32* pDist) {
    VEC3 other = pOther->GetHitCenter();
    VEC3 center;
    center.x = mPos.x + mHitOffsetAlt.x;
    center.y = mPos.y + mHitOffsetAlt.y;
    center.z = mPos.z + mHitOffsetAlt.z;
    f32 otherRadius = pOther->GetHitRadius();
    f32 enemyRadius = 15.0f + otherRadius;
    f32 playerRadius = 15.0f + otherRadius;
    f32 radius;
    if (mIsEnemy) radius = enemyRadius;
    else radius = playerRadius;
    f32 radiusSq = radius * radius;
    f32 xx = other.x - center.x;
    xx *= xx;
    if (xx > radiusSq) return FALSE;
    f32 zz = other.z - center.z;
    zz *= zz;
    if (zz > radiusSq) return FALSE;
    if (xx + zz > radiusSq) return FALSE;
    if (pDist) *pDist = FSqrt(xx + zz);
    return TRUE;
}
void RPTnkTank::PushApartFrom(RPTnkTank* pOther) {
    VEC3 other = pOther->GetHitCenter();
    f32 radius = 32.0f;
    f32 radiusSq = radius * radius;
    f32 x = other.x - (mPos.x + mHitOffset.x);
    f32 xx = x * x;
    if (xx > radiusSq) return;
    f32 z = other.z - (mPos.z + mHitOffset.z);
    f32 zz = z * z;
    if (zz > radiusSq) return;
    if (xx + zz > radiusSq) return;
    f32 dist = FSqrt(xx + zz);
    if (dist != 0.0f) {
        VEC3 normal(x, 0.0f, z);
        PSVECNormalize(normal, normal);
        pOther->AccumulatePushOut(radius - dist, &normal, mVelocity);
    }
}
void RPTnkTank::OnHitWall(f32 depth, const VEC3* pNormal, s32 wall, const VEC3& rVel) {
    TnkAccumulateScaled(mPosAccum[0], mPosAccum[1], depth, pNormal);
    VEC3 relative;
    relative.x = mVelocity.x - rVel.x;
    relative.y = mVelocity.y - rVel.y;
    relative.z = mVelocity.z - rVel.z;
    f32 along = -VEC3Dot(&relative, pNormal);
    if (along > 0.0f) {
        TnkAccumulateScaled(mVelAccum[0], mVelAccum[1], along, pNormal);
    }
}
void RPTnkTank::AccumulatePushOut(f32 depth, const VEC3* pNormal, const VEC3& rVel) {
    TnkAccumulateScaled(mPosAccum[0], mPosAccum[1], depth, pNormal);
    VEC3 relative;
    relative.x = mVelocity.x - rVel.x;
    relative.y = mVelocity.y - rVel.y;
    relative.z = mVelocity.z - rVel.z;
    f32 along = -VEC3Dot(&relative, pNormal);
    if (along > 0.0f) {
        TnkAccumulateScaled(mVelAccum[0], mVelAccum[1], along, pNormal);
    }
}

void RPTnkTank::KeepNearestShellHit(const HitInfo& rHit) {
    if (rHit.mDist < mShellHit.mDist) {
        TnkCopyHit(&mShellHit, &rHit);
    }
}

void RPTnkTank::KeepNearestMineHit(const HitInfo& rHit) {
    if (rHit.mDist < mMineHit.mDist) {
        TnkCopyHit(&mMineHit, &rHit);
    }
}

void RPTnkTank::DrawTreadMark() {
    if (mTreadDistance < distances[mModelIdx]) return;
    {
        mTreadDistance -= distances[mModelIdx];
        VEC3 dir;
        dir.x = mPrevPos.x - mPos.x;
        dir.y = mPrevPos.y - mPos.y;
        dir.z = mPrevPos.z - mPos.z;
        if (PSVECMag(dir) != 0.0f) PSVECNormalize(dir, dir);
        VEC3 pos;
        pos.x = mTreadDistance * dir.x + mPos.x;
        pos.y = mTreadDistance * dir.y + mPos.y;
        pos.z = mTreadDistance * dir.z + mPos.z;
        f32 factor = RPTnkField::sInstance->GetWorldScale();
        VEC3 scaled(pos.x * factor, pos.y * factor, pos.z * factor);
        VEC3 scale(30.0f * factor, 30.0f * factor, 30.0f * factor);
        RPAxes axes = sFloorQuadBasis;
        axes.y = mRot.z;
        PSVECCrossProduct(axes.y, axes.z, axes.x);
        MTX34 world;
        TnkSetBodyMtx(world, axes, scale, scaled);
        const EGG::ResTIMG* texture = static_cast<const EGG::ResTIMG*>(RPTnkFootmarkTex::sInstance->GetTexture(mModelIdx));
        EGG::DrawGX::BeginDrawQuad(EGG::DrawGX::COLORCHAN_COLOR, EGG::DrawGX::ZMODE_ALWAYS,
            EGG::DrawGX::BLEND_NORMAL, true, false);
        EGG::DrawGX::LoadTexture(texture, GX_TEXMAP0);
        EGG::DrawGX::DrawDLWorld(EGG::DrawGX::DL_QUAD_TEXTURE, world, EGG::DrawGX::WHITE);
    }
}

const RPTnkTank::HitInfo RPTnkTank::sNoHit = {0, 1000.0f};

// Defined last: its .rodata copy follows Load's name table.
extern const TnkPieceColors sTnkPieceColors = {{
    {0x5D,0x98,0xE0}, {0xDE,0x5B,0x44}, {0xBD,0x93,0x41},
    {0x9E,0x96,0x87}, {0x5E,0xA6,0xAD}, {0xDB,0x64,0x8D},
    {0xED,0xE2,0x23}, {0xB5,0x6A,0xC4}, {0x6B,0xC4,0x71},
    {0xFF,0xFF,0xE8}, {0x63,0x63,0x63}
}};
