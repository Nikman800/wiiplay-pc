#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>

#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPLeadCursor.h>
#include <Pack/RPParty/RPPartyCommon/RPMedalResult.h>
#include <Pack/RPParty/RPPartyCommon/RPPauseText.h>
#include <Pack/RPParty/RPPartyCommon/RPResultMenu.h>
#include <Pack/RPParty/RPPartyCommon/RPVsWinner.h>
#include <Pack/RPParty/RPTnkScene/RPTnkBgmMix.h>
#include <Pack/RPParty/RPTnkScene/RPTnkBlock.h>
#include <Pack/RPParty/RPTnkScene/RPTnkExGuide.h>
#include <Pack/RPParty/RPTnkScene/RPTnkLyt.h>
#include <Pack/RPParty/RPTnkScene/RPTnkShell.h>
#include <Pack/RPParty/RPTnkScene/RPTnkStageMap.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTank.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTask.h>
#include <Pack/RPParty/RPTnkScene/RPTnkWeaponManager.h>
#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPSystem/RPSysResourceManager.h>

#include <port/be.h>

extern u32 RP_randSeedX;
extern u32 RP_randSeedY;
// This entry ignores its argument, but the original callers still supply it.
// NONMATCHING-DEBT: RPTnkHideExMsg() is declared without parameters in RPTnkLyt.h; a natural call needs a ledger rename to a one-parameter form.
extern "C" void RPTnkHideExMsg__Fv(RPTnkExGuide*);

// param/TnkGameParam.bin
struct TnkStageParam {
    be_s32 bonus[2];         // at 0x0 1P / 2P; > 0 shows the bonus fade
    be_s32 enemyTypes[2][8]; // at 0x8 1P / 2P (RPTnkStageMap::LoadStage)
    be_s32 variants[2][2];   // at 0x48 1P / 2P first and last stage variant
};
struct TnkGameParam {
    be_s32 lives;              // at 0x0
    be_s32 enemies[10][0x2A];  // at 0x4 one 0xA8 record per enemy type (RPTnkTank, RPTnkAiAttack)
    TnkStageParam stages[100]; // at 0x694 STAGE_NUM
};

inline u32 TnkGameRandom() {
    RP_randSeedX = RP_randSeedX * 0x41C64E6D + 12345;
    if (RP_randSeedY & 1) {
        RP_randSeedY ^= 0x11020;
    }
    RP_randSeedY >>= 1;
    return RP_randSeedX ^ RP_randSeedY;
}

RPTnkGameManager* RPTnkGameManager::sInstance;

void RPTnkGameManager::CreateInstance() {
    sInstance = new RPTnkGameManager;
}

void RPTnkGameManager::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPTnkGameManager::RPTnkGameManager() {
    mStateFuncs[0] = &RPTnkGameManager::StateIdle;
    mStateFuncs[1] = &RPTnkGameManager::StateWaitLifeNo;
    mStateFuncs[2] = &RPTnkGameManager::StateStageStart;
    mStateFuncs[3] = &RPTnkGameManager::StateCountdown;
    mStateFuncs[4] = &RPTnkGameManager::StatePlay;
    mStateFuncs[5] = &RPTnkGameManager::StateMissWait;
    mStateFuncs[6] = &RPTnkGameManager::StateStageClearWait;
    mStateFuncs[7] = &RPTnkGameManager::StateNextStage;
    mStateFuncs[8] = &RPTnkGameManager::StateCompleteWait;
    mStateFuncs[9] = &RPTnkGameManager::StateFailedWait;
    mStateFuncs[10] = &RPTnkGameManager::StateResultWait;
    mStateFuncs[11] = &RPTnkGameManager::StateResultMenu;
    mStateFuncs[12] = &RPTnkGameManager::StateEndMenu;
    mRetryCount = 0;
}

RPTnkGameManager::~RPTnkGameManager() {}

void RPTnkGameManager::SetupFirstStage() {
    RPTnkStepTimer::sInstance->Start(0.0f);
    mpStats->ClearCounts();
    mpStats->SetAllLives(static_cast<TnkGameParam*>(mpParam)->lives);
    unk30 = 0;
    mFreeze = false;
    mStageReady = false;
    switch (mModeIdx) {
    case 0:
        mStage = 0;
        if (RPCommon::GetInstance()->GetSavedMedal() >= 3) {
            mLastStage = GetStageNum() - 1;
        } else {
            mLastStage = 19;
        }
        PickStageVariant(mStage);
        mpEmptyTask->Reset();
        RPTnkStageMap::sInstance->LoadStage(mStage, unk10, TRUE, FALSE);
        RPTnkLayoutManager::sInstance->GetFade()->Start(false, true);
        mTimer = 0;
        mState = 1;
        break;
    case 1:
        mStage = 0;
        mLastStage = 19;
        PickStageVariant(0);
        RPTnkStageMap::sInstance->LoadStage(mStage, unk10, TRUE, TRUE);
        mpStats->SetAllLives(1);
        RPTnkLayoutManager::sInstance->GetFade()->Start(false, true);
        mTimer = 0;
        mState = 1;
        break;
    }
}

void RPTnkGameManager::PickStageVariant(s32 stage) {
    s32 first;
    s32 last;
    switch (mModeIdx) {
    case 0:
        first = static_cast<TnkGameParam*>(mpParam)->stages[stage].variants[0][0];
        last = static_cast<TnkGameParam*>(mpParam)->stages[stage].variants[0][1];
        break;
    case 1:
        first = static_cast<TnkGameParam*>(mpParam)->stages[stage].variants[1][0];
        last = static_cast<TnkGameParam*>(mpParam)->stages[stage].variants[1][1];
        break;
    }
    if (first == last) {
        unk10 = first;
    } else {
        s32 variant = TnkGameRandom() % (last - first + 1);
        unk10 = variant + first;
        if (unk14 == unk10) {
            do {
                s32 variant = TnkGameRandom() % (last - first + 1);
                unk10 = variant + first;
            } while (unk14 == unk10);
        }
    }
    unk14 = unk10;
}

BOOL RPTnkGameManager::CheckStageClear() {
    if (RPTnkTankManager::sInstance->GetEnemyAliveNum() == 0) {
        RPTnkShellPool::sInstance->StopAllEffects();
        mFreeze = true;
        if (mStage == mLastStage || mStage == GetStageNum() - 1) {
            RPTnkLayoutManager::sInstance->GetCompleteMsg()->Show();
            RPTnkLayoutManager::sInstance->GetHud()->PlayOut();
            {
                nw4r::math::VEC3 scale;
                nw4r::math::VEC3 position(0.0f, 0.0f, 0.0f);
                scale.x = 1.0f;
                scale.y = 1.0f;
                scale.z = 1.0f;
                RPSysEffect effect("Tnk_all_clear", 3, 0);
                effect.RPSysEffect::create();
                effect.RPSysEffect::setPos(position);
                effect.RPSysEffect::setScale(scale);
                effect.RPSysEffect::update();
            }
            mpBgmMix->Stop();
            if (mStage == 19) {
                RPSndAudioMgr::instance()->startBgm(0x3EU);
            } else {
                RPSndAudioMgr::instance()->startBgm(0x3FU);
            }
            mTimer = 0;
            mState = 8;
        } else {
            if (mStage == 0) {
                RPTnkHideExMsg__Fv(mpExGuide);
            }
            RPTnkLayoutManager::sInstance->GetClearMsg()->Show();
            RPTnkLayoutManager::sInstance->GetHud()->PlayOut();
            mpBgmMix->Stop();
            mpBgmMix->PlayStageClearBgm();
            unk30 = 1;
            mTimer = 0;
            mState = 6;
        }
        return TRUE;
    }
    return FALSE;
}

void RPTnkGameManager::StateIdle() {}

void RPTnkGameManager::StateWaitLifeNo() {
    if (!RPTnkLayoutManager::sInstance->GetFade()->IsDone()) {
        return;
    }
    if (mStage == 0 && mModeIdx == 0) {
        RPCommon::GetInstance()->GetPauseText()->SetEnabled(0, true);
    }
    mTimer = 165;
    mState = 2;
}

void RPTnkGameManager::StateStageStart() {
    if (--mTimer <= 0) {
        RPTnkTankManager::sInstance->ActivatePlayers();
        RPTnkTankManager::sInstance->ActivateEnemies();
        RPTnkLayoutManager::sInstance->GetHud()->SetShowBalloons(false);
        RPTnkLayoutManager::sInstance->GetStartMsg()->Play();
        RPTnkLayoutManager::sInstance->GetHud()->PlayIn();
        if (mStage == 0) {
            mpExGuide->Start();
        }
        mTimer = 0;
        mState = 4;
    }
}

void RPTnkGameManager::StateCountdown() {
    if (--mTimer == 0) {
        RPTnkTankManager::sInstance->ActivatePlayers();
        RPTnkTankManager::sInstance->ActivateEnemies();
        mTimer = 0;
        mState = 4;
    }
}

void RPTnkGameManager::StatePlay() {
    if (mStage == 0) {
        mpExGuide->Calculate();
    }
    s32 players = 1;
    if (mModeIdx == 0) {
        players = 1;
    }
    if (mModeIdx == 1) {
        players = 2;
    }
    switch (RPTnkTankManager::sInstance->GetPlayersDeadState(players)) {
    case 0:
    case 1:
    case 2:
        ++mTimer;
        CheckStageClear();
        break;
    case 3:
        RPTnkShellPool::sInstance->StopAllEffects();
        mFreeze = true;
        BOOL failed = mpStats->AddLives(0, -1);
        if (mModeIdx == 1) {
            mpStats->AddLives(1, -1);
        }
        if (failed) {
            RPTnkLayoutManager::sInstance->GetFailedMsg()->Show();
            RPTnkLayoutManager::sInstance->GetHud()->PlayOut();
            mpBgmMix->Stop();
            RPSndAudioMgr::instance()->startBgm(0x3DU);
            unk30 = 4;
            mTimer = 0;
            mState = 9;
        } else if (!CheckStageClear()) {
            mpBgmMix->Stop();
            RPSndAudioMgr::instance()->startBgm(0x40U);
            unk30 = 3;
            mTimer = 120;
            mState = 5;
        }
        if (mStage == 0) {
            RPTnkHideExMsg__Fv(mpExGuide);
        }
        break;
    }
}

void RPTnkGameManager::StateMissWait() {
    if (--mTimer == 0) {
        if (mModeIdx == 0) {
            RPCommon::GetInstance()->GetPauseText()->SetEnabled(0, false);
        }
        RPTnkLayoutManager::sInstance->GetFade()->Start(false, false);
        mState = 7;
    }
}

void RPTnkGameManager::StateStageClearWait() {
    if (RPTnkLayoutManager::sInstance->GetClearMsg()->IsActive()) {
        if (mModeIdx == 0) {
            RPCommon::GetInstance()->GetPauseText()->SetEnabled(0, false);
        }
        s32 bonus;
        if (mModeIdx == 0) {
            bonus = static_cast<TnkGameParam*>(mpParam)->stages[mStage].bonus[0];
        } else {
            bonus = static_cast<TnkGameParam*>(mpParam)->stages[mStage].bonus[1];
        }
        RPTnkLayoutManager::sInstance->GetFade()->Start(bonus > 0, false);
        mState = 7;
    }
}

void RPTnkGameManager::StateNextStage() {
    if (RPTnkLayoutManager::sInstance->GetFade()->IsDone()) {
        if (mStage == 0 && mModeIdx == 0) {
            RPCommon::GetInstance()->GetPauseText()->SetEnabled(0, true);
        }
        mTimer = 165;
        mState = 2;
    } else if (mStageReady) {
        RPTnkTankManager::sInstance->DeactivateAll();
        RPTnkWeaponManager::sInstance->DeactivateAll();
        RPTnkBlockPool::sInstance->KillAllBlocks();
        RPTnkStepTimer::sInstance->Start(0.0f);
        RPTnkLayoutManager::sInstance->GetHud()->PlayOut();
        RPTnkLayoutManager::sInstance->GetClearMsg()->Hide();
        RPTnkLayoutManager::sInstance->GetHud()->SetShowBalloons(true);
        mFreeze = false;
        if (unk30 == 1) {
            ++mStage;
            PickStageVariant(mStage);
            RPTnkStageMap::sInstance->LoadStage(mStage, unk10, TRUE, mModeIdx == 1);
        } else {
            RPTnkStageMap::sInstance->ReloadStage(unk10, TRUE, mModeIdx == 1);
        }
        unk30 = 0;
        mStageReady = false;
    }
}

void RPTnkGameManager::StateCompleteWait() {
    if (RPTnkLayoutManager::sInstance->GetCompleteMsg()->IsActive()) {
        RPTnkLayoutManager::sInstance->GetResultLyt()->Start();
        mState = 10;
    }
}

void RPTnkGameManager::StateFailedWait() {
    if (RPTnkLayoutManager::sInstance->GetFailedMsg()->IsActive()) {
        RPTnkLayoutManager::sInstance->GetExMsg()->PlayOutAll();
        RPTnkLayoutManager::sInstance->GetResultLyt()->Start();
        mState = 10;
    }
}

void RPTnkGameManager::StateResultWait() {
    if (RPTnkLayoutManager::sInstance->GetResultLyt()->IsFinished()) {
        switch (mModeIdx) {
        case 0:
            RPCommon::GetInstance()->GetMedalResult()->Start(mpStats->GetKills(0));
            mState = 11;
            break;
        case 1:
            s32 winner = mpStats->GetWinner();
            RPCommon::GetInstance()->GetVsWinner()->Start(winner);
            mState = 11;
            break;
        }
    }
}

void RPTnkGameManager::StateResultMenu() {
    switch (mModeIdx) {
    case 0:
        if (RPCommon::GetInstance()->GetMedalResult()->GetState() == 4) {
            RPCommon::GetInstance()->GetResultMenu()->Start(1, -1);
            mState = 12;
        }
        break;
    case 1:
        if (RPCommon::GetInstance()->GetVsWinner()->GetState() == 5) {
            s32 winner = mpStats->GetWinner();
            RPCommon::GetInstance()->GetResultMenu()->Start(2, winner);
            mState = 12;
        } else if (RPCommon::GetInstance()->GetVsWinner()->IsStateEntered(3)) {
            RPTnkLayoutManager::sInstance->GetResultLyt()->ShowTotals();
        }
        break;
    }
}

void RPTnkGameManager::StateEndMenu() {
    switch (mModeIdx) {
    case 0:
        if (RPCommon::GetInstance()->GetResultMenu()->GetState() == 4) {
            switch (RPCommon::GetInstance()->GetResultMenu()->GetChoice()) {
            case 0:
                mRetry = true;
                ++mRetryCount;
                break;
            case 1:
                mQuit = true;
                break;
            }
        }
        break;
    case 1:
        if (RPCommon::GetInstance()->GetResultMenu()->GetState() == 4) {
            switch (RPCommon::GetInstance()->GetResultMenu()->GetChoice()) {
            case 0:
                mRetry = true;
                ++mRetryCount;
                break;
            case 1:
                mQuit = true;
                break;
            }
        }
        break;
    }
}

void RPTnkGameManager::Create() {
    RPTnkGameStats::CreateInstance();
    mpStats = RPTnkGameStats::sInstance;
    RPTnkExGuide::CreateInstance();
    mpExGuide = RPTnkExGuide::sInstance;
    RPTnkEmptyTask::CreateInstance();
    mpEmptyTask = RPTnkEmptyTask::sInstance;
    mpEmptyTask->Create();
    mpBgmMix = new RPTnkBgmMix();
}

void RPTnkGameManager::Load() {
    EGG::Archive* archive = RPSysResourceManager::LoadGameCommonArchive();
    u32 size = 0;
    mpParam = RPSysResourceManager::GetFileFromArchive(archive, "param/TnkGameParam.bin", &size);
    mpEmptyTask->Load();
}

void RPTnkGameManager::ResetForMode(s32 playerNum) {
    mPlayerNum = playerNum;
    mStage = 0;
    mLastStage = 0;
    unk14 = -1;
    unk10 = 0;
    mTimer = 0;
    if (playerNum == 1) {
        mModeIdx = 0;
    } else if (playerNum == 2) {
        mModeIdx = 1;
    }
    mpEmptyTask->Reset();
    unk20 = false;
    mFreeze = false;
    mStageReady = false;
    mRetry = false;
    mQuit = false;
    unk30 = 0;
    mpStats->Setup(2);
    mpExGuide->Reset();
    mpBgmMix->Stop();
    mState = 0;
}

void RPTnkGameManager::Start() {
    RPCommon::GetInstance()->GetLeadCursor()->SetTimers(15);
    unk20 = false;
    SetupFirstStage();
}

void RPTnkGameManager::Calculate() {
    (this->*mStateFuncs[mState])();
    unk20 = mState == 4;
    RPCommon::GetInstance()->GetLeadCursor()->SetAllEnabled(unk20);
    if (unk20) {
        RPTnkTank* player = RPTnkTankManager::sInstance->GetPlayerTank(0);
        if (player->GetAlive() == 0) {
            RPCommon::GetInstance()->GetLeadCursor()->SetPlayerEnabled(0, false);
        } else {
            RPCommon::GetInstance()->GetLeadCursor()->SetPlayerEnabled(0, true);
        }
        if (mModeIdx == 1) {
            player = RPTnkTankManager::sInstance->GetPlayerTank(1);
            if (player->GetAlive() == 0) {
                RPCommon::GetInstance()->GetLeadCursor()->SetPlayerEnabled(1, false);
            } else {
                RPCommon::GetInstance()->GetLeadCursor()->SetPlayerEnabled(1, true);
            }
        }
    }
    s32 enemyNum = RPTnkTankManager::sInstance->GetEnemyAliveNum();
    s32 enemyType = RPTnkTankManager::sInstance->GetMaxActiveEnemyType();
    mpBgmMix->SetParams(enemyNum, enemyType);
}

void RPTnkGameManager::Exit() {
    RPTnkGameStats::DestroyInstance();
    mpExGuide->Exit();
    RPTnkExGuide::DestroyInstance();
    RPTnkEmptyTask::DestroyInstance();
}

void RPTnkGameManager::SetStageReady(bool ready) {
    mStageReady = ready;
}

void RPTnkGameManager::StartStageBgm() {
    RPTnkTankManager* manager = RPTnkTankManager::sInstance;
    int stage = mStage + 1;
    int enemyNum = manager->GetEnemyAliveNum();
    s32 enemyType = manager->GetMaxActiveEnemyType();
    mpBgmMix->StartStageBgm(stage, enemyNum, enemyType);
}

void RPTnkGameManager::StartBgm() {
    RPSndAudioMgr::instance()->startSystemSe(0x41U);
}

#pragma defer_codegen off
const s32 RPTnkGameManager::STAGE_NUM = 100;
