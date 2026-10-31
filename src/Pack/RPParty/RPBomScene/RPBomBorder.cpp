// Pose Mii borders (RPBomBorder): the three miss-counter models, their intro drop, flash,
// break effect and rise. Unit 0x802E2160-0x802E35D8 (dtk/game_units.tsv). Functions in
// reverse address order.
#include <Pack/RPParty/RPBomScene/RPBomBorder.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPGraphics/RPGrpModelAnm.h>
#include <Pack/RPGraphics/RPGrpModelResManager.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPParty/RPBomScene/RPBomGameManager.h>
#include <Pack/RPParty/RPDucScene/RPDucUtil.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <revolution/MTX.h>

extern "C" {
// RPBomCamera_2
extern RPAxes Bom_rotXNeg90;
}

// NONMATCHING-DEBT: the loops over mpModels/mOffsetZ walk a pointer advanced from `this`.
// Indexed loops (mpModels[i], mOffsetZ[i]) give the same code except for a counter/pointer
// register swap that moves between functions: indexed everywhere misses StateIdle and StateFlash,
// indexed except ResetOffsets/PlayNormalAnm misses CalcModelMtx and StartFlash. Loop forms,
// declaration orders, a float-cast form and a GetModel accessor inline were tried. The counter
// must be declared before the pointer.
static inline RPBomBorder* NextSlot(RPBomBorder* p) {
    return reinterpret_cast<RPBomBorder*>(reinterpret_cast<RPGrpModel**>(p) + 1);
}

// The walk steps one pointer, which is both an mOffsetZ and an mpModels element only where
// pointers are 4 bytes. On a 64-bit host the slot's members are read by index instead (stepping
// 8 bytes read mOffsetZ[0], [2] and the first word of mNodeMtx: the borders were misplaced).
#if defined(PORT_HOST)
#define SLOT_MODEL(p, i) mpModels[i]
#define SLOT_OFFSET_Z(p, i) mOffsetZ[i]
#else
#define SLOT_MODEL(p, i) p->mpModels[0]
#define SLOT_OFFSET_Z(p, i) p->mOffsetZ[0]
#endif

// Frames each border waits before it drops in (StartIntro scales them by -10)
const f32 RPBomBorder::sIntroDelays[3] = {60.0f, 90.0f, 120.0f};

void RPBomBorder::CreateInstance() {
    sInstance = new RPBomBorder;
}

void RPBomBorder::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPBomBorder::~RPBomBorder() {}

void RPBomBorder::StateIntroDrop() {
    bool done = true;
    for (int i = 0; i < 3; i++) {
        if (mLanded[i] && mSettled[i]) {
            continue;
        }
        done = false;
        if (mLanded[i]) {
            f32 num = mpModels[i]->GetModelAnm()->GetNumFrame(RPGrpModelAnm::Anm_Chr, 1);
            f32 frame = mpModels[i]->GetModelAnm()->GetFrame(RPGrpModelAnm::Anm_Chr, 1);
            bool end = false;
            if (num - 1.0f <= frame && num >= frame) {
                end = true;
            }
            if (end) {
                mpModels[i]->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
                mSettled[i] = true;
            }
        } else {
            mOffsetZ[i] += 10.0f;
            if (mOffsetZ[i] > -16.0f * i) {
                mOffsetZ[i] = -16.0f * i;
                mpModels[i]->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 1, 0.0f);
                mLanded[i] = true;
            } else if (!mFallSePlayed[i] && mOffsetZ[i] > -480.0f) {
                switch (i) {
                case 2:
                    RPSndAudioMgr::instance()->startSystemSe(0x16);
                    break;
                case 1:
                    RPSndAudioMgr::instance()->startSystemSe(0x17);
                    break;
                case 0:
                    RPSndAudioMgr::instance()->startSystemSe(0x18);
                    break;
                }
                mFallSePlayed[i] = true;
            }
        }
    }
    if (done) {
        mState = 3;
    }
}

void RPBomBorder::StateNone() {}

void RPBomBorder::StateFlash() {
    if (--mTimer > 0) {
        return;
    }
    PlayNormalAnm();
    mTimer = 0;
    mState = 3;
}

void RPBomBorder::StateIdle() {
    RPBomBorder* p;
    s32 idx;
    s32 i;
    ResetOffsets();
    if (RPBomGameManager::sInstance->mState == 4) {
        // PlayFlashAnm, written out
        mScale.x = 13.75f;
        mScale.y = 27.5f;
        mScale.z = 2.0f;
        i = 0;
        p = this;
        do {
            SLOT_MODEL(p, i)->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 0);
            SLOT_MODEL(p, i)->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 1);
            SLOT_MODEL(p, i)->GetModelAnm()->Start(RPGrpModelAnm::Anm_Clr, 1, 0.0f);
            SLOT_MODEL(p, i)->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, 2.0f);
            SLOT_MODEL(p, i)->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Clr, 1, 2.0f);
            i++;
            p = NextSlot(p);
        } while (i < 3);

        idx = RPBomGameManager::sInstance->mMissMax - RPBomGameManager::sInstance->mMissCount;
        {
            RPSysEffect effect("Bom_borderbreak01", 0, 0);
            effect.create();
            effect.setMtx(mNodeMtx[idx]);
            effect.update();
        }
        switch (idx) {
        case 2:
            RPSndAudioMgr::instance()->startSystemSe(0x11);
            break;
        case 1:
            RPSndAudioMgr::instance()->startSystemSe(0x12);
            break;
        case 0:
            RPSndAudioMgr::instance()->startSystemSe(0x13);
            break;
        }
        mState = 4;
    }
}

void RPBomBorder::StateMissWait() {
    ResetOffsets();
    if (RPBomGameManager::sInstance->mState != 4) {
        PlayNormalAnm();
        mState = 3;
    }
}

void RPBomBorder::StateRise() {
    mPos.z += 5.0f;
    if (mPos.z > 350.0f) {
        mPos.z = 350.0f;
    }
}

void RPBomBorder::Create() {}

void RPBomBorder::Load() {
    EGG::Archive* pArchive = RPSysResourceManager::LoadGameCommonArchive();
    RPGrpModelResManager* pResMgr = RPGrpModelResManager::GetCurrent();
    RPGrpHandle file = pResMgr->CreateData(
        RPGrpModelResManager::Type_ResFile,
        RPSysResourceManager::GetFileFromArchive(pArchive, "G3D/border_model.brres"));
    for (int i = 0; i < 3; i++) {
        mpModels[i] = RPGrpModel::Construct(
            RPDucCreateRes(pResMgr, RPGrpModelResManager::Type_ResMdl, file, "border_model"), 1, 0,
            0x108);
        mpModels[i]->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Chr, 2);
        mpModels[i]->GetModelAnm()->Regist(
            RPGrpModelAnm::Anm_Chr, 0,
            RPDucCreateRes(pResMgr, RPGrpModelResManager::Type_ResAnmChr, file, "border_model"));
        mpModels[i]->GetModelAnm()->Regist(
            RPGrpModelAnm::Anm_Chr, 1,
            RPDucCreateRes(pResMgr, RPGrpModelResManager::Type_ResAnmChr, file, "border_landing"));
        mpModels[i]->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Clr, 2);
        mpModels[i]->GetModelAnm()->Regist(
            RPGrpModelAnm::Anm_Clr, 0,
            RPDucCreateRes(pResMgr, RPGrpModelResManager::Type_ResAnmClr, file, "border_normal"));
        mpModels[i]->GetModelAnm()->Regist(
            RPGrpModelAnm::Anm_Clr, 1,
            RPDucCreateRes(pResMgr, RPGrpModelResManager::Type_ResAnmClr, file, "border_miss"));
        mpModels[i]->SetDrawGroup(0);
        mpModels[i]->SetDrawScene(2);
        mpModels[i]->SetUnk4(0);
        mpModels[i]->SetCallback(this);
    }
}

void RPBomBorder::Reset() {
    mPos = RP_vecZero;
    mPos.y = 1500.0f;
    mPos.z = 190.0f;
    mScale = RP_vecOne;
    mScale.x = 27.5f;
    mScale.y = 27.5f;
    mScale.z = 1.0f;
    mRot = Bom_rotXNeg90;

    for (int i = 0; i < 3; i++) {
        mpModels[i]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Chr, 0);
        mpModels[i]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Chr, 1);
        mpModels[i]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 0);
        mpModels[i]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 1);
        mpModels[i]->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
        mpModels[i]->GetModelAnm()->Start(RPGrpModelAnm::Anm_Clr, 0, 0.0f);
    }
    ResetOffsets();
    int i = 0;
    do {
        RPDucMakeMtx(&mNodeMtx[i], mRot, mScale, mPos);
    } while (++i < 3);
    i = 0;
    do {
        mFallSePlayed[i] = false;
        mLanded[i] = false;
        mSettled[i] = false;
    } while (++i < 3);
    mAnimating = false;
    unk50 = 3;
    mTimer = 0;
    mState = 1;
}

void RPBomBorder::Calculate() {
    (this->*mStateFuncs[mState])();
}

void RPBomBorder::CalcModelMtx() {
    s32 left = RPBomGameManager::sInstance->mMissMax - RPBomGameManager::sInstance->mMissCount;
    RPBomBorder* p = this;
    s32 i = 0;
    do {
        nw4r::math::VEC3 pos;
        pos = mPos;
        pos.z += SLOT_OFFSET_Z(p, i);
        nw4r::math::MTX34 mtx;
        RPDucMakeMtx(&mtx, mRot, RP_vecOne, pos);
        if (SLOT_MODEL(p, i)->GetScnObj() != NULL) {
            SLOT_MODEL(p, i)->GetScnObj()->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &mtx);
        }
        SLOT_MODEL(p, i)->SetBaseScale(mScale);
        if (i < left) {
            SLOT_MODEL(p, i)->SetVisible(true);
        } else {
            SLOT_MODEL(p, i)->SetVisible(false);
        }
        if (SLOT_MODEL(p, i)->GetModelAnm()->GetPlayIndex(RPGrpModelAnm::Anm_Chr) != 0 ||
            mAnimating) {
            SLOT_MODEL(p, i)->UpdateFrame();
        }
        SLOT_MODEL(p, i)->Calc();
        SLOT_MODEL(p, i)->Entry();
        i++;
        p = NextSlot(p);
    } while (i < 3);
}

void RPBomBorder::EditWorldMtx(RPGrpModel* pModel) {
    nw4r::math::MTX34* pMtx = pModel->ReferWorldMtx(pModel->GetJointIndex("border_model"));
    for (int i = 0; i < 3; i++) {
        if (pModel == mpModels[i]) {
            PSMTXCopy(*pMtx, mNodeMtx[i]);
        }
    }
}

void RPBomBorder::StartIntro() {
    int i = 0;
    do {
        mOffsetZ[i] = -16.0f * i;
        mOffsetZ[i] -= 10.0f * sIntroDelays[i];
        mLanded[i] = false;
        mSettled[i] = false;
    } while (++i < 3);
    mState = 0;
}

void RPBomBorder::StartFlash() {
    PlayFlashAnm();
    mAnimating = true;
    mTimer = 80;
    mState = 2;
}

inline void RPBomBorder::ResetOffsets() {
    int i = 0;
    RPBomBorder* p = this;
    do {
        SLOT_OFFSET_Z(p, i) = -16.0f * i;
        p = NextSlot(p);
    } while (++i < 3);
}

inline void RPBomBorder::PlayFlashAnm() {
    mScale.x = 13.75f;
    mScale.y = 27.5f;
    mScale.z = 2.0f;
    s32 i = 0;
    RPBomBorder* p = this;
    do {
        SLOT_MODEL(p, i)->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 0);
        SLOT_MODEL(p, i)->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 1);
        SLOT_MODEL(p, i)->GetModelAnm()->Start(RPGrpModelAnm::Anm_Clr, 1, 0.0f);
        SLOT_MODEL(p, i)->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, 2.0f);
        SLOT_MODEL(p, i)->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Clr, 1, 2.0f);
        i++;
        p = NextSlot(p);
    } while (i < 3);
}

inline void RPBomBorder::PlayNormalAnm() {
    mScale.x = 27.5f;
    mScale.y = 27.5f;
    mScale.z = 1.0f;
    s32 i = 0;
    RPBomBorder* p = this;
    do {
        SLOT_MODEL(p, i)->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 0);
        SLOT_MODEL(p, i)->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Clr, 1);
        SLOT_MODEL(p, i)->GetModelAnm()->Start(RPGrpModelAnm::Anm_Clr, 0, 0.0f);
        SLOT_MODEL(p, i)->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, 1.0f);
        SLOT_MODEL(p, i)->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Clr, 0, 1.0f);
        i++;
        p = NextSlot(p);
    } while (i < 3);
}

// Defined last: CreateInstance inlines it, and this out-of-line copy (compiled first, then
// dead-stripped by the linker) puts the first set of state-function constants in .data.
RPBomBorder::RPBomBorder() {
    mStateFuncs[0] = &RPBomBorder::StateIntroDrop;
    mStateFuncs[1] = &RPBomBorder::StateNone;
    mStateFuncs[2] = &RPBomBorder::StateFlash;
    mStateFuncs[3] = &RPBomBorder::StateIdle;
    mStateFuncs[4] = &RPBomBorder::StateMissWait;
    mStateFuncs[5] = &RPBomBorder::StateRise;
}
