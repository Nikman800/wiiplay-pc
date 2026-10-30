#include <Pack/RPParty/RPTnkScene/RPTnkBlock.h>
#include <Pack/RPParty/RPTnkScene/RPTnkMine.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <revolution/MTX.h>

#ifndef ASM_PS
// Host builds give this block a C form (port/host/include/decomp/compat.h).
#define ASM_PS(name) ASM
#endif
#pragma opt_unroll_loops off

extern nw4r::math::VEC3 RP_vecZero;
extern nw4r::math::VEC3 RP_vecEy;
extern nw4r::math::VEC3 RP_vecEz;
extern RPAxes RP_mtx33Identity;

static RPTnkBlock::StateFunc sStateFuncs[1] = {
    &RPTnkBlock::StateBreakIfFlagged,
};

static inline f32 BlockAbs(register f32 value) {
    ASM_PS(BlockAbs) (fabs value, value)
    return value;
}

s32 RPTnkBlock::GetHeight() const {
    return mHeight;
}

s32 RPTnkBlock::GetKind() const {
    return mKind;
}

RPTnkBlock::RPTnkBlock() {
    mStateFuncs[0] = sStateFuncs[0];
}

RPTnkBlock::~RPTnkBlock() {}

void RPTnkBlock::OnSpawn() {
    mHitRadius = 1.0f;
    mScale.x = (0.5f * RPTnkField::sCellWidth) / 17.5f;
    mScale.y = (0.5f * RPTnkField::sCellWidth) / 17.5f;
    mScale.z = (0.5f * RPTnkField::sCellDepth) / 17.5f;
    mRot = RP_mtx33Identity;
    mKind = 0;
    s32 i = 0;
    do {
        mShadowFlags[i] = true;
        i++;
    } while (i < 8);
    mShadowAnm[0] = 0.0f;
    mShadowAnm[1] = 0.0f;
    mShadowAnm[2] = 1.0f;
    mpShadowModel->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexSrt, 0);
    mpShadowModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexSrt, 0, 0.0f);
    mState = 0;
}

void RPTnkBlock::ApplySpawnParam(u32 param) {
    mKind = param & 3;
    SetupEdges();
    // NONMATCHING-DEBT: the original copies pos into four slots above it (pos lowest, then
    // north/east/south/west) as words. `VEC3 north = pos` / `north = pos` / a VEC3 array forward
    // the floats (0.92); `GetBlockAt(VEC3(pos))`, scoped `VEC3 north(pos)` and a by-value helper
    // copy as words but place the copies below pos (stack offsets only, 0.9559).
    struct PositionCopy { u32 words[3]; };
    PositionCopy west, south, east, north;
    nw4r::math::VEC3 pos(0.0f, 0.0f, 0.0f);
    pos.x = mPos.x;
    pos.z = mPos.z - RPTnkField::sCellDepth;
    north = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[0] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(north));
    pos.x = mPos.x + RPTnkField::sCellWidth;
    pos.z = mPos.z;
    east = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[1] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(east));
    pos.x = mPos.x;
    pos.z = mPos.z + RPTnkField::sCellDepth;
    south = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[2] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(south));
    pos.x = mPos.x - RPTnkField::sCellWidth;
    pos.z = mPos.z;
    west = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[3] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(west));
    mHeight = ((param >> 4) & 7) + 1;
    mpModels[0]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexPat, 0);
    mpModels[1]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexPat, 0);
    mpModels[2]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexPat, 0);
    if (mKind == 0) {
        mpModels[0]->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, 1.0f);
        mpModels[1]->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, 1.0f);
        mpModels[2]->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, 1.0f);
    } else if (mKind == 1) {
        mpModels[0]->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, 0.0f);
        mpModels[1]->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, 0.0f);
        mpModels[2]->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, 0.0f);
    }
}

void RPTnkBlock::SetupEdges() {
    const f32& x = mPos.x;
    const f32& z = mPos.z;
    mEdges[0].mPos.x = -(0.5f * RPTnkField::sCellWidth - x);
    mEdges[0].mPos.y = 0.0f;
    mEdges[0].mPos.z = -(0.5f * RPTnkField::sCellDepth - z);
    mEdges[0].mNormal.x = 0.0f;
    mEdges[0].mNormal.y = 0.0f;
    mEdges[0].mNormal.z = -1.0f;
    mEdges[0].mLength = RPTnkField::sCellWidth;
    mEdges[0].unk1C = 0;
    mEdges[1].mPos.x = 0.5f * RPTnkField::sCellWidth + x;
    mEdges[1].mPos.y = 0.0f;
    mEdges[1].mPos.z = -(0.5f * RPTnkField::sCellDepth - z);
    mEdges[1].mNormal.x = 1.0f;
    mEdges[1].mNormal.y = 0.0f;
    mEdges[1].mNormal.z = 0.0f;
    mEdges[1].mLength = RPTnkField::sCellDepth;
    mEdges[1].unk1C = 0;
    mEdges[2].mPos.x = 0.5f * RPTnkField::sCellWidth + x;
    mEdges[2].mPos.y = 0.0f;
    mEdges[2].mPos.z = 0.5f * RPTnkField::sCellDepth + z;
    mEdges[2].mNormal.x = 0.0f;
    mEdges[2].mNormal.y = 0.0f;
    mEdges[2].mNormal.z = 1.0f;
    mEdges[2].mLength = RPTnkField::sCellWidth;
    mEdges[2].unk1C = 0;
    mEdges[3].mPos.x = -(0.5f * RPTnkField::sCellWidth - x);
    mEdges[3].mPos.y = 0.0f;
    mEdges[3].mPos.z = 0.5f * RPTnkField::sCellDepth + z;
    mEdges[3].mNormal.x = -1.0f;
    mEdges[3].mNormal.y = 0.0f;
    mEdges[3].mNormal.z = 0.0f;
    mEdges[3].mLength = RPTnkField::sCellDepth;
    mEdges[3].unk1C = 0;
}

void RPTnkBlock::UpdateShadow() {
    s32 i = 0;
    do {
        mShadowFlags[i] = true;
        i++;
    } while (i < 8);
    nw4r::math::VEC3 pos(0.0f, 0.0f, 0.0f);
    pos.x = mPos.x;
    pos.z = mPos.z - RPTnkField::sCellDepth;
    if (RPTnkBlockPool::sInstance->GetBlockAt(nw4r::math::VEC3(pos)) == 2U) {
        mShadowFlags[0] = false;
        mShadowFlags[6] = false;
    }
    pos.x = mPos.x;
    pos.z = mPos.z + RPTnkField::sCellDepth;
    if (RPTnkBlockPool::sInstance->GetBlockAt(nw4r::math::VEC3(pos)) == 2U) {
        mShadowFlags[2] = false;
        mShadowFlags[4] = false;
    }
    pos.x = mPos.x - RPTnkField::sCellWidth;
    pos.z = mPos.z;
    if (RPTnkBlockPool::sInstance->GetBlockAt(nw4r::math::VEC3(pos)) == 2U) {
        mShadowFlags[0] = false;
        mShadowFlags[2] = false;
    }
    pos.x = mPos.x + RPTnkField::sCellWidth;
    pos.z = mPos.z;
    if (RPTnkBlockPool::sInstance->GetBlockAt(nw4r::math::VEC3(pos)) == 2U) {
        mShadowFlags[4] = false;
        mShadowFlags[6] = false;
    }
    nw4r::math::VEC3 forward(0.0f, 0.0f, 1.0f);
    nw4r::math::VEC3 back(0.0f, 0.0f, -1.0f);
    nw4r::math::VEC3 left(-1.0f, 0.0f, 0.0f);
    nw4r::math::VEC3 right(1.0f, 0.0f, 0.0f);
    s32 count = 0;
    if (!mShadowFlags[0]) count++;
    if (!mShadowFlags[2]) count++;
    if (!mShadowFlags[4]) count++;
    if (!mShadowFlags[6]) count++;
    switch (count) {
    case 0:
        mpShadowModel->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexSrt, 0);
        mpShadowModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexSrt, 0, 0.0f);
        mShadowAnm[0] = RP_vecEz.x;
        mShadowAnm[1] = RP_vecEz.y;
        mShadowAnm[2] = RP_vecEz.z;
        break;
    case 2:
        mpShadowModel->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexSrt, 0);
        mpShadowModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexSrt, 0, 1.0f);
        if (!mShadowFlags[0] && !mShadowFlags[2]) {
            mShadowAnm[0] = left.x;
            mShadowAnm[1] = left.y;
            mShadowAnm[2] = left.z;
        } else if (!mShadowFlags[2] && !mShadowFlags[4]) {
            mShadowAnm[0] = forward.x;
            mShadowAnm[1] = forward.y;
            mShadowAnm[2] = forward.z;
        } else if (!mShadowFlags[4] && !mShadowFlags[6]) {
            mShadowAnm[0] = right.x;
            mShadowAnm[1] = right.y;
            mShadowAnm[2] = right.z;
        } else if (!mShadowFlags[6] && !mShadowFlags[0]) {
            mShadowAnm[0] = back.x;
            mShadowAnm[1] = back.y;
            mShadowAnm[2] = back.z;
        }
        break;
    case 3:
        mpShadowModel->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexSrt, 0);
        mpShadowModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexSrt, 0, 2.0f);
        if (mShadowFlags[0]) {
            mShadowAnm[0] = forward.x;
            mShadowAnm[1] = forward.y;
            mShadowAnm[2] = forward.z;
        } else if (mShadowFlags[2]) {
            mShadowAnm[0] = right.x;
            mShadowAnm[1] = right.y;
            mShadowAnm[2] = right.z;
        } else if (mShadowFlags[4]) {
            mShadowAnm[0] = back.x;
            mShadowAnm[1] = back.y;
            mShadowAnm[2] = back.z;
        } else if (mShadowFlags[6]) {
            mShadowAnm[0] = left.x;
            mShadowAnm[1] = left.y;
            mShadowAnm[2] = left.z;
        }
        break;
    case 4:
        mpShadowModel->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_TexSrt, 0);
        mpShadowModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexSrt, 0, 3.0f);
        mShadowAnm[0] = RP_vecEz.x;
        mShadowAnm[1] = RP_vecEz.y;
        mShadowAnm[2] = RP_vecEz.z;
        break;
    }
}

#pragma auto_inline off
void RPTnkBlock::SpawnBreakEffect() {
    RPSysEffect effect("Tnk_blockbreak01", 0, 0);
    nw4r::math::VEC3 pos(mPos.x, mPos.y, mPos.z);
    f32 scale = (0.5f * RPTnkField::sCellWidth) / 17.5f;
    nw4r::math::VEC3 effectScale(scale, scale, scale);
    s32 i = 0;
    do {
        effect.create();
        effect.setPos(pos);
        effect.setScale(effectScale);
        effect.update();
        i += 2;
        pos.y += 35.0f;
    } while (i < mHeight);
}
#pragma auto_inline on

void RPTnkBlock::StateBreakIfFlagged() {
    if (mBreakRequested) {
        SpawnBreakEffect();
        Kill();
    }
}

void RPTnkBlock::Create() {
    RPTnkActor::Create();
}

void RPTnkBlock::Load() {
    const char* modelNames[3] = {"tnk_block_1", "tnk_block_7", "tnk_block_8"};
    const char* anmNames[3] = {"tnk_block_1", "tnk_block_7", "tnk_block_8"};
    RPGrpModelResManager* manager;
    EGG::Archive* archive = RPSysResourceManager::LoadGameCommonArchive();
    {
        // A const temporary schedules the next call's `li r5,0` after the name address.
        RPGrpModelResManager* const current = RPGrpModelResManager::GetCurrent();
        manager = current;
    }
    void* binary = RPSysResourceManager::GetFileFromArchive(archive, "G3D/tnk_block.brres");
    RPGrpHandle file = manager->CreateData(RPGrpModelResManager::Type_ResFile, binary);
    for (s32 i = 0; i < 3; i++) {
        RPGrpHandle model = manager->CreateData(RPGrpModelResManager::Type_ResMdl,
            manager->GetPtr(RPGrpModelResManager::Type_ResMdl, file, modelNames[i]));
        mpModels[i] = RPGrpModel::Construct(model, 1, 0, 3);
        RPGrpHandle anm = manager->CreateData(RPGrpModelResManager::Type_ResAnmTexPat,
            manager->GetPtr(RPGrpModelResManager::Type_ResAnmTexPat, file, anmNames[i]));
        mpModels[i]->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_TexPat, 1);
        mpModels[i]->GetModelAnm()->Regist(RPGrpModelAnm::Anm_TexPat, 0, anm, 0);
        mpModels[i]->SetDrawGroup(0);
        mpModels[i]->SetDrawScene(0);
        mpModels[i]->SetUnkCFlags(0x12);
    }
    binary = RPSysResourceManager::GetFileFromArchive(archive, "G3D/tnk_block_shadow.brres");
    file = manager->CreateData(RPGrpModelResManager::Type_ResFile, binary);
    const char* shadowModelName = "tnk_block_shadow";
    binary = manager->GetPtr(RPGrpModelResManager::Type_ResMdl, file, shadowModelName);
    RPGrpHandle model = manager->CreateData(RPGrpModelResManager::Type_ResMdl, binary);
    mpShadowModel = RPGrpModel::Construct(model, 1, 0, 0x204);
    const char* shadowAnmName = "tnk_shadow_animation";
    binary = manager->GetPtr(RPGrpModelResManager::Type_ResAnmTexSrt, file, shadowAnmName);
    RPGrpHandle anm = manager->CreateData(RPGrpModelResManager::Type_ResAnmTexSrt, binary);
    mpShadowModel->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_TexSrt, 1);
    mpShadowModel->GetModelAnm()->Regist(RPGrpModelAnm::Anm_TexSrt, 0, anm, 0);
    mpShadowModel->SetDrawGroup(0);
    mpShadowModel->SetDrawScene(0);
    mpShadowModel->SetUnkCFlags(0x12);
}

void RPTnkBlock::ApplyVelocity() {
    mPos.x += mVelocity.x;
    mPos.y += mVelocity.y;
    mPos.z += mVelocity.z;
}

void RPTnkBlock::ClearForcesStep() {
    RPTnkActor::ClearForcesStep();
    // NONMATCHING-DEBT: the original copies pos into four slots above it (pos lowest, then
    // north/east/south/west) as words. `VEC3 north = pos` / `north = pos` / a VEC3 array forward
    // the floats (0.92); `GetBlockAt(VEC3(pos))`, scoped `VEC3 north(pos)` and a by-value helper
    // copy as words but place the copies below pos (stack offsets only, 0.9559).
    struct PositionCopy { u32 words[3]; };
    PositionCopy west, south, east, north;
    nw4r::math::VEC3 pos(0.0f, 0.0f, 0.0f);
    pos.x = mPos.x;
    pos.z = mPos.z - RPTnkField::sCellDepth;
    north = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[0] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(north));
    pos.x = mPos.x + RPTnkField::sCellWidth;
    pos.z = mPos.z;
    east = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[1] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(east));
    pos.x = mPos.x;
    pos.z = mPos.z + RPTnkField::sCellDepth;
    south = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[2] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(south));
    pos.x = mPos.x - RPTnkField::sCellWidth;
    pos.z = mPos.z;
    west = *reinterpret_cast<const PositionCopy*>(&pos);
    mpNeighbors[3] = RPTnkBlockPool::sInstance->GetBlockAt(
        reinterpret_cast<const nw4r::math::VEC3&>(west));
    mBreakRequested = false;
}

void RPTnkBlock::CollideWithField() {
    if (mKind == 0) {
        mHitRadius = 0.5f * RPTnkField::sCellWidth;
        if (RPTnkMinePool::sInstance->IsInExplosion(this)) {
            mBreakRequested = true;
        }
    }
}

void RPTnkBlock::Calculate() {
    (this->*mStateFuncs[mState])();
    UpdateShadow();
    mpSound->set3DPosition(mPos);
}

void RPTnkBlock::CalcModelMtx() {
    union {
        struct {
            Vec right;
            Vec up;
            Vec forward;
            u32 pad;
            nw4r::math::MTX34 mtx;
        } values;
        f64 alignment;
    } transform;
    s32 modelIndices[8] = {0, 2, 2, 1, 2, 2, 1, 2};
    f32 offsets[8] = {0.0f, 105.0f, 87.5f, 52.5f, 52.5f, 35.0f, 0.0f, 0.0f};
    nw4r::math::VEC3 pos = mPos;
    offsets[0] = -0.2f;
    pos.y -= offsets[mHeight - 1];
    transform.values.mtx._00 = mScale.x * mRot.x.x;
    transform.values.mtx._01 = mScale.y * mRot.y.x;
    transform.values.mtx._02 = mScale.z * mRot.z.x;
    transform.values.mtx._03 = pos.x;
    transform.values.mtx._10 = mScale.x * mRot.x.y;
    transform.values.mtx._11 = mScale.y * mRot.y.y;
    transform.values.mtx._12 = mScale.z * mRot.z.y;
    transform.values.mtx._13 = pos.y;
    transform.values.mtx._20 = mScale.x * mRot.x.z;
    transform.values.mtx._21 = mScale.y * mRot.y.z;
    transform.values.mtx._22 = mScale.z * mRot.z.z;
    transform.values.mtx._23 = pos.z;
    RPGrpModel* model = mpModels[modelIndices[mHeight - 1]];
    if (model->GetScnObj() != NULL) {
        model->GetScnObj()->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &transform.values.mtx);
    }
    model->Calc();
    model->Entry();
    if (mHeight != 1) {
        transform.values.forward.x = mShadowAnm[0];
        transform.values.forward.y = mShadowAnm[1];
        transform.values.forward.z = mShadowAnm[2];
        transform.values.up.x = RP_vecEy.x;
        transform.values.up.y = RP_vecEy.y;
        transform.values.up.z = RP_vecEy.z;
        PSVECCrossProduct(&transform.values.up, &transform.values.forward,
                          &transform.values.right);
        transform.values.mtx._00 = mScale.x * transform.values.right.x;
        transform.values.mtx._01 = mScale.y * transform.values.up.x;
        transform.values.mtx._02 = mScale.z * transform.values.forward.x;
        transform.values.mtx._03 = mPos.x;
        transform.values.mtx._10 = mScale.x * transform.values.right.y;
        transform.values.mtx._11 = mScale.y * transform.values.up.y;
        transform.values.mtx._12 = mScale.z * transform.values.forward.y;
        transform.values.mtx._13 = mPos.y;
        transform.values.mtx._20 = mScale.x * transform.values.right.z;
        transform.values.mtx._21 = mScale.y * transform.values.up.z;
        transform.values.mtx._22 = mScale.z * transform.values.forward.z;
        transform.values.mtx._23 = mPos.z;
        if (mpShadowModel->GetScnObj() != NULL) {
            mpShadowModel->GetScnObj()->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &transform.values.mtx);
        }
        mpShadowModel->Calc();
        mpShadowModel->Entry();
    }
}

BOOL RPTnkBlock::CollideObject(RPTnkActor* other, s32 mode) {
    if ((mode & 1) && mHeight == 1) {
        return FALSE;
    }
    const f32& ownX = mPos.x;
    f32 radius = GetActorHitRadius(other);
    nw4r::math::VEC3 center = other->GetHitCenter();
    if (BlockAbs(center.x - ownX) > RPTnkField::sCellWidth + radius) {
        return FALSE;
    }
    if (BlockAbs(center.z - mPos.z) > RPTnkField::sCellDepth + radius) {
        return FALSE;
    }
    BOOL hit = FALSE;
    for (s32 i = 0; i < 4; i++) {
        if ((mode & 2) && mpNeighbors[i] == 2U) {
            continue;
        }
        // NONMATCHING-DEBT: mEdges is a POD copy of RPTnkWall (_VEC3 members): RPTnkWall's VEC3
        // members make `RPTnkWall wall = mEdges[i]` copy mLength as a float (104/112), and a POD
        // RPTnkWall breaks RPTnkField_3's VEC3 -> Vec* conversions.
        Wall wall = mEdges[i];
        nw4r::math::VEC3 normal;
        nw4r::math::VEC3 testPos = center;
        f32 depth;
        u32 wallId;
        if (RPTnkTestCircleVsWall(radius, &normal, &depth, &wallId, &testPos,
                                 reinterpret_cast<RPTnkWall*>(&wall))) {
            other->OnHitWall(depth, &normal, wallId, RP_vecZero);
            hit = TRUE;
        }
    }
    return hit;
}
