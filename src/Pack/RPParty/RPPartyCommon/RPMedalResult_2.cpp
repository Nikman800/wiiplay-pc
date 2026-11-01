// Single-player result: record list, medal award and save (RPMedalResult).
// Unit 0x801E556C-0x801E747C (the ledger range ends at 0x801E7570; the last two functions there
// belong to the RPCommon file). Functions in reverse address order; the state machine
// instantiation and the static initializer follow.
#include <Pack/RPParty/RPPartyCommon/RPMedalResult.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPSystem/RPSysPlayer.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPKernel.h>
#include <Pack/RPSystem.h>
#include <Pack/RPParty/RPPartyCommon/RPFaceCtrl.h>
#include <Pack/RPParty/RPPartyCommon/RPLeadCursor.h>
#include <Pack/RPParty/RPPartyCommon/RPMsgBoard.h>
#include <Pack/RPParty/RPPartyCommon/RPPauseText.h>
#include <Pack/RPParty/RPPartyCommon/RPScreenFade.h>

#if defined(PORT_HOST)
// On the Wii these are RPSceneInfo_2.cpp's functions of the same names, written for Laser
// Hockey's RPHkyGame: the object's word at 0x1C / 0x20 (here the player and rank position
// panes), then that pane's global translation at 0x90 / 0xA0 (mGlbMtx._03, _13). Those are
// offsets of the 32-bit layout; on the host the call read other members and their bytes, so
// the result's Mii and medal were placed from garbage. The host reads the panes themselves.
static void RP_getPairFromField1C(EGG::Vector2f* pOut, const RPRankGetLyt* pLyt) {
    const nw4r::math::MTX34& rMtx = pLyt->GetPlayerPosPane()->GetGlobalMtx();
    pOut->x = rMtx._03;
    pOut->y = rMtx._13;
}
static void RP_getPairFromField20(EGG::Vector2f* pOut, const RPRankGetLyt* pLyt) {
    const nw4r::math::MTX34& rMtx = pLyt->GetRankPosPane()->GetGlobalMtx();
    pOut->x = rMtx._03;
    pOut->y = rMtx._13;
}
#else
extern "C" {
void RP_getPairFromField1C(EGG::Vector2f* pOut, const RPRankGetLyt* pLyt);
void RP_getPairFromField20(EGG::Vector2f* pOut, const RPRankGetLyt* pLyt);
}
#endif

f32 RPMedalResult::sActorScale[2] = {16.0f, 16.0f};
f32 RPMedalResult::sAngleA[2] = {-16.0f, -16.0f};
f32 RPMedalResult::sMedalScale[2] = {18.5f, 20.5f};
bool RPMedalResult::sFlag = true;
f32 RPMedalResult::sAngleB[2];

EGG::Vector2f RPMedalResult::sRecordOffsets[2] = {EGG::Vector2f(20.0f, 110.0f),
                                                    EGG::Vector2f(20.0f, 110.0f)};
EGG::Vector2f RPMedalResult::sModelOffsets[2] = {EGG::Vector2f(50.0f, 75.0f),
                                                   EGG::Vector2f(25.0f, 90.0f)};

namespace {
enum Medal {
    Medal_Bronze = 1,
    Medal_Silver,
    Medal_Gold,
    Medal_Platinum
};

struct MedalThresholds {
    s32 mScore[9][4];
};

const MedalThresholds sMedalThresholds = {{
    {200, 320, 450, 600},
    {20, 40, 60, 80},
    {40, 70, 100, 200},
    {150, 400, 800, 1200},
    {3, 7, 10, 14},
    {15, 27, 51, 60},
    {1000, 1600, 2300, 2600},
    {170, 250, 300, 325},
    {11, 32, 84, 125},
}};

struct MedalResultStateFuncs {
    RPMedalResultSeq::StateFunc mEnter;
    RPMedalResultSeq::StateFunc mCalc;
};

inline s32 GetThreshold(s32 game, MedalThresholds t, s32 medal) {
    return t.mScore[game][medal - 1];
}

inline f32 MedalIdentity(f32 value) {
    return value;
}

inline s32 SavedMedal(RPPartyPlayerData* pData, RPPartyPlayerData::EGame game) {
    s32 medal = pData->getMedal(game);
    return medal;
}

inline s32 CalcNewMedal(bool enabled, s32 score, s32 savedMedal) {
    RPPartyPlayerData::EGame game;
    if (!enabled) {
        return 0;
    }
    for (s32 medal = 4; medal > savedMedal; medal--) {
        game = static_cast<RPPartyPlayerData::EGame>(RPCommon::GetInstance()->GetGame());
        MedalThresholds thresholds = sMedalThresholds;
        if (score >= GetThreshold(game, thresholds, medal)) {
            return medal;
        }
    }
    return 0;
}

inline void ApplyPaneOffset(EGG::Vector2f& rPos, const RPGrpView* pView,
                            const EGG::Vector2f* pOffsets) {
    f32 ratio = static_cast<f32>(static_cast<u16>(pView->GetScreen()->GetWidth())) /
                EGG::Screen::GetSizeXMax(EGG::Screen::TV_MODE_STD);
    rPos.x = ratio * pOffsets[EGG::Screen::GetTVMode()].x + rPos.x;
    rPos.y += pOffsets[EGG::Screen::GetTVMode()].y;
}

template <typename T>
inline bool IsStateFor(const T* pLyt, s32 state, s32 time) {
    bool result = false;
    if (pLyt->mpStateMachine->GetState() == state && pLyt->mpStateMachine->GetTimer() >= time) {
        result = true;
    }
    return result;
}

inline void UpdateBestEffects(RPSysEffect** ppEffects, const nw4r::math::MTX34& rMtx,
                              const f32& scale, const f32& fade) {
    ppEffects[0]->setMtx(rMtx);
    ppEffects[1]->setMtx(rMtx);
    nw4r::math::VEC3 scaleVec(scale * fade, scale, scale);
    ppEffects[0]->setScale(scaleVec);
    ppEffects[1]->setScale(scale);
}

inline RPGrpModel* MedalModel(const RPMedalResult* p) {
    return p->mpMedalModels[static_cast<u32>(p->mMedal - 1)];
}

template <typename T>
inline void GetEffectRange(T medal, int& rFirst, int& rLast) {
    switch (medal) {
    case 1:
        rFirst = 2;
        rLast = 5;
        break;
    case 2:
        rFirst = 6;
        rLast = 9;
        break;
    case 3:
        rFirst = 10;
        rLast = 15;
        break;
    case 4:
        rFirst = 16;
        rLast = 21;
        break;
    }
}

inline void SetColumns(nw4r::math::MTX34& m, const nw4r::math::VEC3& x,
                       const nw4r::math::VEC3& y, const nw4r::math::VEC3& z,
                       const nw4r::math::VEC3& p) {
    m._00 = x.x;
    m._10 = x.y;
    m._20 = x.z;
    m._01 = y.x;
    m._11 = y.y;
    m._21 = y.z;
    m._02 = z.x;
    m._12 = z.y;
    m._22 = z.z;
    m._03 = p.x;
    m._13 = p.y;
    m._23 = p.z;
}
} // namespace

static void UpdateFade(f32& rFade, s32 dir);

RPMedalResult::~RPMedalResult() {}

RPMedalResult::RPMedalResult() {
    mpRecordLyt = new RPRecordLyt;
    mpRankGetLyt = new RPRankGetLyt;
    mpStateMachine = new RPMedalResultSeq(this, 5);

    MedalResultStateFuncs funcs[5] = {
        {NULL, NULL},
        {&RPMedalResult::EnterRecord, &RPMedalResult::CalcRecord},
        {&RPMedalResult::EnterMedal, &RPMedalResult::CalcMedal},
        {&RPMedalResult::EnterSave, &RPMedalResult::CalcSave},
        {NULL, NULL},
    };
    for (int i = 0; i < 5; i++) {
        mpStateMachine->SetStateFuncs(funcs[i].mEnter, funcs[i].mCalc, i);
    }

    RPGrpRenderer::GetCurrent()->AppendDrawObject(&static_cast<IRPGrpDrawObject&>(*this));
}

void RPMedalResult::Load() {
    EGG::Archive* pCommonArc = RPSysResourceManager::instance()->GetStaticCommonArchive();
    EGG::Archive* pLocalArc = RPSysResourceManager::instance()->GetStaticLocalArchive();
    mpRecordLyt->Load(pLocalArc);
    mpRankGetLyt->Load(pLocalArc);

    RPGrpModelResManager* pResMgr = RPGrpModelResManager::GetCurrent();
    void* pData =
        RPSysResourceManager::GetFileFromArchive(pCommonArc, "G3D/medal_model.brres", NULL);
    RPGrpHandle handle = pResMgr->CreateData(RPGrpModelResManager::Type_ResFile, pData, 0);

    const char* modelNames[MEDAL_MODEL_NUM] = {"medal_bronze", "medal_silver", "medal_gold",
                                               "medal_platinum"};
    const char* const anmNames[2] = {"medal_appear", "medal_wait"};
    for (int i = 0; i < MEDAL_MODEL_NUM; i++) {
        mpMedalModels[i] = RPGrpModel::Construct(handle, modelNames[i], 1, 0, 0);
        mpMedalModels[i]->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Chr, 2);
        for (int j = 0; j < 2; j++) {
            mpMedalModels[i]->GetModelAnm()->RegistFromResFile(RPGrpModelAnm::Anm_Chr, j,
                                                                handle, anmNames[j], 0);
        }
        mpMedalModels[i]->SetDrawScene(7);
    }

    const char* effectNames[EFFECT_NUM] = {
        "Bcm_best_spot",         "Bcm_best_shine",        "Bcm_mdl_bronze_bk",
        "Bcm_mdl_bronze_glow",   "Bcm_mdl_bronze_ptc",    "Bcm_mdl_bronze_kira",
        "Bcm_mdl_silver_bk",     "Bcm_mdl_silver_glow",   "Bcm_mdl_silver_ptc",
        "Bcm_mdl_silver_kira",   "Bcm_mdl_gold_bk01",     "Bcm_mdl_gold_bk02",
        "Bcm_mdl_gold_glow",     "Bcm_mdl_gold_ptc",      "Bcm_mdl_gold_kira",
        "Bcm_mdl_gold_ring",     "Bcm_mdl_platinum_bk01", "Bcm_mdl_platinum_bk02",
        "Bcm_mdl_platinum_glow", "Bcm_mdl_platinum_ptc",  "Bcm_mdl_platinum_kira",
        "Bcm_mdl_platinum_ring",
    };
    for (int i = 0; i < EFFECT_NUM; i++) {
        mpEffects[i] = new RPSysEffect(effectNames[i], RPCommon::GetEffectDrawLayer(), 0);
    }
}

void RPMedalResult::Reset() {
    mpStateMachine->Reset(0);

    mPos = EGG::Vector3f::zero;
    mAxisX = EGG::Vector3f(1.0f, 0.0f, 0.0f);
    mAxisY = EGG::Vector3f(0.0f, 1.0f, 0.0f);
    mAxisZ = EGG::Vector3f(0.0f, 0.0f, 1.0f);
    mMedalPos = EGG::Vector3f::zero;
    mMedalAxisX = EGG::Vector3f(1.0f, 0.0f, 0.0f);
    mMedalAxisY = EGG::Vector3f(0.0f, 1.0f, 0.0f);
    mMedalAxisZ = EGG::Vector3f(0.0f, 0.0f, 1.0f);
    mScale = sActorScale[EGG::Screen::GetTVMode()];
    mMedalScale = sMedalScale[EGG::Screen::GetTVMode()];

    RPCommon* pCommon = RPCommon::GetInstance();
    for (int i = 0; i < RECORD_NUM; i++) {
        mRecords[i] = RPSysPlayerMgr::instance()->getPlayer(0).mpPartyPlayerData->getMyRecord(
            static_cast<RPPartyPlayerData::EGame>(pCommon->GetGame()), i);
    }

    RPPartySystemData data = RPSysSaveDataMgr::instance()->getPartySystemData();
    if (data.isMsgFlag(9)) {
        mMedalsEnabled = true;
    } else {
        mMedalsEnabled = false;
    }

    mFadeDir = 0;
    mFade = 0.01f;
    mpRecordLyt->Reset();
    mpRankGetLyt->Reset();
}

void RPMedalResult::Calculate() {
    if (mpStateMachine->GetState() != 0) {
        EGG::Vector3f axis(0.0f, 1.0f, 0.0f);
        EGG::Matrix34f m;
        m.setAxisRotation(axis,
                          MedalIdentity(sAngleA[EGG::Screen::GetTVMode()]) * MedalIdentity(0.017453292f));
        m.getBase(0, mAxisX);
        m.getBase(1, mAxisY);
        m.getBase(2, mAxisZ);
        m.setAxisRotation(axis,
                          MedalIdentity(sAngleB[EGG::Screen::GetTVMode()]) * MedalIdentity(0.017453292f));
        m.getBase(0, static_cast<EGG::Vector3f&>(mMedalAxisX));
        m.getBase(1, static_cast<EGG::Vector3f&>(mMedalAxisY));
        m.getBase(2, static_cast<EGG::Vector3f&>(mMedalAxisZ));
    }

    mpStateMachine->Calculate();

    if (mpStateMachine->GetState() != 0) {
        UpdateFade(mFade, mFadeDir);

        for (int i = 0; i < EFFECT_NUM; i++) {
            mpEffects[i]->update();
        }
    }
}

void RPMedalResult::EnterRecord() {
    s32 savedMedal;
    s32 score;
    RPCommon* pCommon;
    { RPCommon* const current = RPCommon::GetInstance(); pCommon = current; }

    mRank = GetRankForScore(mScore);
    for (int i = 0; i < RECORD_NUM; i++) {
        mNewRecords[i] = mRecords[i];
    }
    if (mRank >= 0) {
        for (int i = RECORD_NUM - 1; i > mRank; i--) {
            mNewRecords[i] = mNewRecords[i - 1];
        }
        mNewRecords[mRank] = mScore;
    }

    if (mRank >= 0) {
        mHasRecords = false;
        for (int i = mRank; i < RECORD_NUM; i++) {
            if (mRecords[i] != 0) {
                mHasRecords = true;
                break;
            }
        }
    } else {
        mHasRecords = false;
    }

    mpRecordLyt->Open(mScore, mRank, mNewRecords, mHasRecords);

    score = mScore;
    savedMedal =
        SavedMedal(mpPlayerData, static_cast<RPPartyPlayerData::EGame>(pCommon->GetGame()));
    mMedal = CalcNewMedal(mMedalsEnabled, score, savedMedal);

    pCommon->PlayAvatarAnm(0, 0, 0.0f);
    RPSysKokeshiManager::ChangeExpression(pCommon->GetAvatar(0)->GetNigaoeModel(),
                                          static_cast<RFLExpression>(0));
    pCommon->mpFaceCtrls[0]->Reset(pCommon->GetAvatar(0));
    pCommon->unk30 = false;
    pCommon->GetScreenFade()->SetFadeIn(true);

    if (mMedal == 0) {
        SaveResults();
    }
}

void RPMedalResult::CalcRecord() {
    RPCommon* pCommon = RPCommon::GetInstance();

    mpRecordLyt->Calculate();

    if (mpRecordLyt->unk35) {
        if (mRank == 0 && mHasRecords) {
            pCommon->PlayAvatarAnm(0, 1, 10.0f);
        } else {
            pCommon->PlayAvatarAnm(0, 2, 10.0f);
        }
        RPSysKokeshiManager::ChangeExpression(pCommon->GetAvatar(0)->GetNigaoeModel(),
                                              static_cast<RFLExpression>(1));
        pCommon->mpFaceCtrls[0]->Reset(pCommon->GetAvatar(0));
        mpEffects[0]->create();
        if (mRank == 0 && mHasRecords) {
            mpEffects[1]->create();
        }
        mFadeDir = 1;
        RPSndAudioMgr::instance()->startSystemSe("RP_COM_BGM_RENEW_RECORD");
    }

    if (mpRecordLyt->mpStateMachine->GetState() != 0) {
        const RPGrpView* pView = RPGrpRenderer::GetCurrent()->GetView(1);
        EGG::Vector2f pos = mpRecordLyt->GetPanePos();
        ApplyPaneOffset(pos, pView, sRecordOffsets);
        mPos = RPCommon::GetInstance()->ScreenToWorld(pos);
        pCommon->mpFaceCtrls[0]->Calculate(pCommon->GetAvatar(0));

        nw4r::math::MTX34 m;
        SetColumns(m, mAxisX, mAxisY, mAxisZ, mPos);
        mScale = sActorScale[EGG::Screen::GetTVMode()];
        RPCommon::GetInstance()->SetAvatarScale(0, mScale, m);

        nw4r::math::MTX34 m2;
        SetColumns(m2, mAxisX, mAxisY, mAxisZ, mPos);
        UpdateBestEffects(mpEffects, m2, mScale, mFade);
    }

    if (mMedal != 0 && mpRecordLyt->mpStateMachine->GetState() == 4) {
        mpRecordLyt->LatchPanePos();
    }

    if (sFlag) {
        if ((mpRecordLyt->mpStateMachine->GetState() == 4 && RPCommon::IsAnyABPressed(1)) ||
            IsStateFor(mpRecordLyt, 4, 300)) {
            mpRecordLyt->Close();
            if (mMedal == 0) {
                mFadeDir = -1;
            }
        }
    }

    if (mpRecordLyt->mpStateMachine->GetState() == 6) {
        if (mMedal != 0) {
            mpStateMachine->ChangeState(2);
        } else {
            mpStateMachine->ChangeState(3);
            mpEffects[0]->fade();
            mpEffects[1]->fade();
        }
    }
}

void RPMedalResult::EnterMedal() {
    RPCommon* pCommon = RPCommon::GetInstance();

    mpRankGetLyt->Open(mMedal);
    MedalModel(this)->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
    unkA4 = false;

    if (mRank != 0 || !mHasRecords) {
        pCommon->PlayAvatarAnm(0, 1, 10.0f);
        RPSysKokeshiManager::ChangeExpression(pCommon->GetAvatar(0)->GetNigaoeModel(),
                                              static_cast<RFLExpression>(1));
        pCommon->mpFaceCtrls[0]->Reset(pCommon->GetAvatar(0));
    }

    mpEffects[0]->create();
    mpEffects[1]->create();
    mFadeDir = 1;

    switch (mMedal) {
    case 1:
        RPSndAudioMgr::instance()->startFanfare("RP_COM_BGM_GET_COPPER");
        break;
    case 2:
        RPSndAudioMgr::instance()->startFanfare("RP_COM_BGM_GET_SILVER");
        break;
    case 3:
        RPSndAudioMgr::instance()->startFanfare("RP_COM_BGM_GET_GOLD");
        break;
    case 4:
        RPSndAudioMgr::instance()->startFanfare("RP_COM_BGM_GET_PLATINA");
        break;
    }

    int first, last;
    GetEffectRange(mMedal, first, last);
    for (int i = first; i <= last; i++) {
        mpEffects[i]->create();
    }

    SaveResults();
}

void RPMedalResult::CalcMedal() {
    nw4r::math::MTX34 m3;
    const RPGrpView* pView = RPGrpRenderer::GetCurrent()->GetView(1);
    int first, last;
    Medal medal = static_cast<Medal>(mMedal);
    GetEffectRange(medal, first, last);

    mpRankGetLyt->Calculate();

    if (mpRankGetLyt->mpStateMachine->GetState() != 0) {
        EGG::Vector2f pos;
        RP_getPairFromField1C(&pos, mpRankGetLyt);
        ApplyPaneOffset(pos, pView, sRecordOffsets);
        mPos = RPCommon::GetInstance()->ScreenToWorld(pos);
    }

    RPCommon* pCommon = RPCommon::GetInstance();
    pCommon->mpFaceCtrls[0]->Calculate(pCommon->GetAvatar(0));

    nw4r::math::MTX34 m;
    SetColumns(m, mAxisX, mAxisY, mAxisZ, mPos);
    mScale = sActorScale[EGG::Screen::GetTVMode()];
    RPCommon::GetInstance()->SetAvatarScale(0, mScale, m);

    if (sFlag) {
        if ((IsStateFor(mpRankGetLyt, 2, 280) && RPCommon::IsAnyABPressed(1)) ||
            IsStateFor(mpRankGetLyt, 2, 580)) {
            mpRankGetLyt->Close();
            mFadeDir = -1;
        }
        if (mpRankGetLyt->mpStateMachine->GetState() == 4) {
            mpStateMachine->ChangeState(3);
            mpEffects[0]->fade();
            mpEffects[1]->fade();
            for (int i = first; i <= last; i++) {
                mpEffects[i]->fade();
            }
        }
    }

    RPGrpModelAnm* pAnm = MedalModel(this)->GetModelAnm();
    if (!unkA4) {
        f32 lastFrame = pAnm->GetNumFrame(RPGrpModelAnm::Anm_Chr, 0) - 1;
        if (pAnm->GetFrame(RPGrpModelAnm::Anm_Chr, 0) >= lastFrame) {
            pAnm->Start(RPGrpModelAnm::Anm_Chr, 1, 0.0f);
            unkA4 = true;
        }
    }

    EGG::Vector2f pos;
    RP_getPairFromField20(&pos, mpRankGetLyt);
    ApplyPaneOffset(pos, pView, sModelOffsets);
    mMedalPos = RPCommon::GetInstance()->ScreenToWorld(pos);

    nw4r::math::MTX34 medalMtx;
    SetColumns(medalMtx, mMedalAxisX, mMedalAxisY, mMedalAxisZ, mMedalPos);
    nw4r::g3d::ScnObj* pScnObj = MedalModel(this)->GetScnObj();
    if (pScnObj != NULL) {
        pScnObj->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &medalMtx);
    }
    mMedalScale = sMedalScale[EGG::Screen::GetTVMode()];
    MedalModel(this)->SetBaseScale(mMedalScale);
    MedalModel(this)->UpdateFrame();
    MedalModel(this)->Calc();
    if (RPCommon::GetInstance()->mEntryAvatars) {
        MedalModel(this)->Entry();
    }

    nw4r::math::MTX34 m2;
    SetColumns(m2, mAxisX, mAxisY, mAxisZ, mPos);
    UpdateBestEffects(mpEffects, m2, mScale, mFade);

    SetColumns(m3, mMedalAxisX, mMedalAxisY, mMedalAxisZ, mMedalPos);
    for (int i = first; i <= last; i++) {
        mpEffects[i]->setMtx(m3);
        mpEffects[i]->setScale(mMedalScale);
    }
}

void RPMedalResult::EnterSave() {
    if (mMedal != 0 && mPostMessage) {
        RPCommon* pCommon = RPCommon::GetInstance();
        RPMsgBoard::GetInstance()->PostMedalMessage(pCommon->GetAvatar(0), pCommon->GetGame(),
                                                 mMedal);
    }
}

void RPMedalResult::CalcSave() {
    if (RPSysSaveDataMgr::instance()->isAsyncFinish()) {
        mpStateMachine->ChangeState(4);
    }
    RPCommon* pCommon = RPCommon::GetInstance();
    pCommon->mpFaceCtrls[0]->Calculate(pCommon->GetAvatar(0));
}

void RPMedalResult::DrawLayer1A() {
    if (mpStateMachine->GetState() != 0) {
        mpRecordLyt->DrawLayer1A();
        mpRankGetLyt->DrawLayer1A();
    }
}

void RPMedalResult::DrawLayer1C() {
    if (mpStateMachine->GetState() != 0) {
        mpRecordLyt->DrawLayer1C();
        mpRankGetLyt->DrawLayer1C();
    }
}

void RPMedalResult::Start(s32 score) {
    if (mpStateMachine->GetState() == 0) {
        mpPlayerData = RPSysPlayerMgr::instance()->getPlayer(0).mpPartyPlayerData;
        mScore = score;
        mpStateMachine->ChangeState(1);
        RPCommon::GetInstance()->GetLeadCursor()->SetAllEnabled(false);
        RPCommon::GetInstance()->GetPauseText()->SetEnabled(false);
    }
}

s32 RPMedalResult::GetRankForScore(s32 score) const {
    for (int i = 0; i < RECORD_NUM; i++) {
        if (score > mRecords[i]) {
            return i;
        }
    }
    return -1;
}

void RPMedalResult::SaveResults() {
    RPSceneInfo* pInfo = RPSceneInfo::spInstance;
    if (mRank == -1) {
        if (mMedal == 0) {
            pInfo->unk18 = 1;
        } else {
            pInfo->unk18 = 3;
        }
    } else {
        pInfo->unk18 = 2;
    }

    RPCommon* pCommon = RPCommon::GetInstance();
    s32 medal;
    if (mRank >= 0) {
        for (int i = 0; i < RECORD_NUM; i++) {
            mpPlayerData->setMyRecord(mNewRecords[i],
                                      static_cast<RPPartyPlayerData::EGame>(pCommon->GetGame()),
                                      i);
        }
    }

    medal = mMedal;
    if (medal > mpPlayerData->getMedal(static_cast<RPPartyPlayerData::EGame>(pCommon->GetGame()))) {
        mpPlayerData->setMedal(medal, static_cast<RPPartyPlayerData::EGame>(pCommon->GetGame()));
    }

    RPSysPlayerMgr::instance()->saveAllData();

    RPPartySystemData data = RPSysSaveDataMgr::instance()->getPartySystemData();
    data.setGameOpen(RPCommon::GetInstance()->GetNextGame(), true);

    mPostMessage = false;
    if (mMedal != 0) {
        RPMsgBoard* pBoard = RPMsgBoard::GetInstance();
        if (pBoard->CanPost(pCommon->GetAvatar(0), data)) {
            pBoard->RecordPost(pCommon->GetAvatar(0), data);
            mPostMessage = true;
        }
    }

    RPSysSaveDataMgr::instance()->setPartySystemData(data);
    RPSysSaveDataMgr::instance()->saveAsync();
}

static void UpdateFade(f32& rFade, s32 dir) {
    if (dir == -1) {
        rFade -= 1.0f / 15.0f;
    } else if (dir == 1) {
        rFade += 1.0f / 15.0f;
    }
    rFade = rFade < 1.0f ? rFade : 1.0f;
    rFade = rFade > 0.0f ? rFade : 0.0f;
}

// The state machine instantiation in original address order.
#pragma defer_codegen off
#pragma auto_inline off
template void RPStateMachine<RPMedalResult>::Calculate();
template void RPStateMachine<RPMedalResult>::ChangeState(s32);
template void RPStateMachine<RPMedalResult>::Reset(s32);
template void RPStateMachine<RPMedalResult>::SetStateFuncs(const StateFunc&, const StateFunc&,
                                                           s32);
template RPStateMachine<RPMedalResult>::RPStateMachine(RPMedalResult*, s32);
template void RPStateMachine<RPMedalResult>::CallEnter();
template void RPStateMachine<RPMedalResult>::CallCalc();
template void RPStateMachine<RPMedalResult>::CallExit();
