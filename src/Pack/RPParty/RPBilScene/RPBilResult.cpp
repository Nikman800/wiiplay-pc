// Billiards result flow (RPBilResult). Unit 802C9E18-802CABDC (dtk/game_units.tsv): score limits
// and state table 803273C0-803274C8, vtable 8035E270, RPBilResult::sInstance.
// CodeWarrior emits function definitions in reverse source order.
#include <Pack/RPParty/RPBilScene/RPBilGameMgr.h>
#include <Pack/RPParty/RPBilScene/RPBilPlayer.h>
#include <Pack/RPParty/RPBilScene/RPBilResult.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPMedalResult.h>
#include <Pack/RPParty/RPPartyCommon/RPResultMenu.h>
#include <Pack/RPParty/RPPartyCommon/RPVsWinner.h>
#include <Pack/RPGraphics/RPGrpRenderer.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <new>

RPBilResult* RPBilResult::sInstance;


inline s32 BilClamp(s32 value, s32 minimum, s32 maximum) {
    if (value < minimum) {
        value = minimum;
    } else if (value > maximum) {
        value = maximum;
    }
    return value;
}

struct BilScoreLimits {
    s32 mBounds[9][2];
};

inline s32 BilScoreMinimum(BilScoreLimits limits, s32 stat) {
    return limits.mBounds[stat][0];
}

inline s32 BilScoreMaximum(BilScoreLimits limits, s32 stat) {
    return limits.mBounds[stat][1];
}

/**
 * The score limits table (.rodata 803273C0) is a local aggregate initializer: every clamp copies
 * it to the stack, then passes it by value to the min/max getters.
 */
inline BilScoreLimits BilGetScoreLimits() {
    BilScoreLimits limits = {{
        {-999, 9999}, {0, 999}, {0, 999}, {0, 9999}, {0, 999},
        {-99, 99}, {-999, 9999}, {0, 999}, {0, 999},
    }};
    return limits;
}

// Arguments are evaluated right to left: the maximum, then the value, then the minimum.
// Each clamp is written out where it is used: a helper taking the value as a parameter would
// evaluate it before the limits copy, but the original reads it between the two getter copies.
inline s32 BilClampScore(s32 minimum, s32 value, s32 maximum) {
    return BilClamp(value, minimum, maximum);
}


// The state machine's code is instantiated here, ahead of the result functions (the unit is
// emitted in reverse source order, so these copies end the unit).
#pragma auto_inline off
template void RPStateMachine<RPBilResult>::CallExit();
template void RPStateMachine<RPBilResult>::CallCalc();
template void RPStateMachine<RPBilResult>::CallEnter();
template void RPStateMachine<RPBilResult>::Calculate();
template void RPStateMachine<RPBilResult>::ChangeState(s32);
template void RPStateMachine<RPBilResult>::Reset(s32);
template void RPStateMachine<RPBilResult>::SetStateFuncs(const StateFunc&, const StateFunc&, s32);
template RPStateMachine<RPBilResult>::RPStateMachine(RPBilResult*, s32);
#pragma auto_inline on

void RPBilResult::CreateInstance() {
    sInstance = new RPBilResult();
}

void RPBilResult::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPBilResult::~RPBilResult() {}

RPBilResult::RPBilResult() {
    mpStateMachine = new RPStateMachine<RPBilResult>(this, STATE_NUM);

    RPBilStateFuncs<RPBilResult> funcs[STATE_NUM] = {
        {NULL, NULL},
        {&RPBilResult::SummaryEnter, &RPBilResult::SummaryCalc},
        {&RPBilResult::WaitEnter, &RPBilResult::WaitCalc},
        {&RPBilResult::BranchEnter, &RPBilResult::BranchCalc},
        {&RPBilResult::Record1pEnter, &RPBilResult::Record1pCalc},
        {&RPBilResult::WinnerEnter, &RPBilResult::WinnerCalc},
        {&RPBilResult::EndMenuEnter, &RPBilResult::EndMenuCalc},
        {NULL, NULL},
    };
    for (s32 state = 0; state < STATE_NUM; state++) {
        mpStateMachine->SetStateFuncs(funcs[state].mEnter, funcs[state].mCalc, state);
    }
    mpLyt = new RPBilResultLyt;
    RPGrpRenderer::GetCurrent()->AppendDrawObject(
        static_cast<IRPGrpDrawObject&>(*this));
}

void RPBilResult::Load() {
    EGG::Archive* pArchive = RPSysResourceManager::LoadGameLocalArchive();
    mpLyt->Load(pArchive);
}

void RPBilResult::Reset() {
    mpStateMachine->Reset(0);
    mWinner = -1;
    mpLyt->Reset();
}

void RPBilResult::Exit() {}

void RPBilResult::Calculate() {
    mpStateMachine->Calculate();
    mpLyt->Calculate();
}

void RPBilResult::SummaryEnter() {
    mpLyt->ShowScores(RPBilGameMgr::sPlayerNum);
}

void RPBilResult::SummaryCalc() {
    if (mpLyt->IsShowDone()) {
        mpStateMachine->ChangeState(STATE_WAIT);
    }
}

void RPBilResult::WaitEnter() {}

void RPBilResult::WaitCalc() {
    bool shouldAdvance = false;
    RPBilResultLyt* pLyt = mpLyt;
    if (pLyt->IsShowDone() && pLyt->mpStateMachines[0]->GetTimer() >= 300) {
        shouldAdvance = true;
    }
    if (shouldAdvance || RPBilPlayerMgr::sInstance->IsAorBTriggered()) {
        mpStateMachine->ChangeState(STATE_BRANCH);
    }
}

void RPBilResult::BranchEnter() {}

void RPBilResult::BranchCalc() {
    if (RPBilPlayerMgr::sInstance->GetPlayerNum() == 1) {
        mpStateMachine->ChangeState(STATE_1P_RECORD);
        for (s32 side = 0; side < 2; side++) {
            mpLyt->HideSide(side);
        }
    } else {
        mpStateMachine->ChangeState(STATE_WINNER);
        for (s32 side = 0; side < 2; side++) {
            if (side != 1) {
                mpLyt->HideSide(side);
            }
        }
    }
}

void RPBilResult::Record1pEnter() {
    RPCommon::GetInstance()->GetMedalResult()->Start(mResults[0].mTotal);
}

void RPBilResult::Record1pCalc() {
    if (RPCommon::GetInstance()->GetMedalResult()->GetState() == 4) {
        mpStateMachine->ChangeState(STATE_END_MENU);
    }
}

void RPBilResult::WinnerEnter() {
    RPCommon::GetInstance()->GetVsWinner()->Start(mWinner);
}

void RPBilResult::WinnerCalc() {
    enum WinnerState { CLOSE = 3, DONE = 5 };
    bool hideSide = true;
    WinnerState state = static_cast<WinnerState>(RPCommon::GetInstance()->GetVsWinner()->GetState());
    if (state != CLOSE && state != DONE) {
        hideSide = false;
    }
    if (hideSide) {
        mpLyt->HideSide(1);
    }
    if (RPCommon::GetInstance()->GetVsWinner()->GetState() == 5) {
        mpStateMachine->ChangeState(STATE_END_MENU);
    }
}

void RPBilResult::EndMenuEnter() {
    if (RPBilGameMgr::sPlayerNum == 1) {
        RPCommon::GetInstance()->GetResultMenu()->Start(1, -1);
    } else {
        RPCommon::GetInstance()->GetResultMenu()->Start(2, mWinner);
    }
}

void RPBilResult::EndMenuCalc() {
    RPResultMenu* pMenu = RPCommon::GetInstance()->GetResultMenu();
    if (pMenu->GetState() == 4) {
        if (pMenu->GetChoice() == 0) {
            unk18 = true;
        } else {
            unk18 = false;
        }
        mpStateMachine->ChangeState(STATE_DONE);
    }
}

void RPBilResult::ComputeScores() {
    s32 playerNum;
    const RPBilPlayer* pPlayer;
    RPBilPlayerMgr* const& rMgr = RPBilPlayerMgr::sInstance;
    RPBilPlayerMgr* pMgr = rMgr;
    for (s32 player = 0; player < (playerNum = pMgr->GetPlayerNum()); player++) {
        // NONMATCHING-DEBT: volatile forces the copy of RPBilPlayerMgr::sInstance into r6 and playerNum/pPlayer
        // into r8; pMgr->GetPlayer(player) gives the same code with r6/r8 swapped (objcmp 0.959).
        // Tried: getter via pMgr/rMgr/RPBilPlayerMgr::sInstance, loop-scoped and const locals,
        // declaration-order sweep, read before/after the result writes, condition forms. The
        // byte-offset read of mpPlayers (private, at 0x10) goes with the volatile.
        // sim.py replay (mwcc_trace of the getter form): the getter's load becomes a CSE temporary
        // numbered above the optimizer's walker/this copies (@29966 = r48 > r39/r40); the target
        // colouring needs it numbered between pMgr (r38) and those copies, i.e. kept as a named
        // function-scope local. Declaring pPlayer after pMgr, non-const, or assigning it in the
        // if-condition still coalesces it into the load temporary (0.959 each).
        // 2026-10-29: `= NULL` at declaration and declaring it before playerNum are the same
        // r6/r7/r8 rotation (this/pMgr copies vs playerNum+pPlayer, which share one register in
        // the original). Walker rule: the copies are optimizer temps numbered between the named
        // locals and the codegen temps, and the getter load stays a codegen temp.
#if defined(PORT_HOST)
        // 0x10 and the 4-byte stride are the 32-bit layout of mpPlayers
        pPlayer = pMgr->GetPlayer(player);
#else
        pPlayer = reinterpret_cast<const RPBilPlayer* volatile*>(
            reinterpret_cast<char*>(pMgr) + player * 4 + 0x10)[0];
#endif
        if (playerNum == 1) {
            mResults[player].mScore = 0;
        } else {
            BilScoreLimits limits = BilGetScoreLimits();
            s32 maximum = BilScoreMaximum(limits, 5);
            mResults[player].mScore =
                BilClampScore(BilScoreMinimum(limits, 5), pPlayer->GetScore(), maximum);
        }
        if (pMgr->GetPlayerNum() == 1) {
            mResults[player].mShotCount = pPlayer->GetShotCount();
            s32 bonus;
            BilScoreLimits limits = BilGetScoreLimits();
            s32 maximum = BilScoreMaximum(limits, 5);
            bonus = BilClampScore(BilScoreMinimum(limits, 5),
                                  (27 - pPlayer->GetShotCount()) * 3, maximum);
            mResults[player].mShotBonus = bonus < 0 ? 0 : bonus;
        } else {
            mResults[player].mShotCount = 0;
            mResults[player].mShotBonus = 0;
        }
        mResults[player].mFoulCount = pPlayer->GetFoulCount();
        if (pMgr->GetPlayerNum() == 1) {
            BilScoreLimits limits = BilGetScoreLimits();
            s32 maximum = BilScoreMaximum(limits, 5);
            mResults[player].mFoulPenalty =
                BilClampScore(BilScoreMinimum(limits, 5), -pPlayer->GetFoulCount() * 3, maximum);
        } else {
            BilScoreLimits limits = BilGetScoreLimits();
            s32 maximum = BilScoreMaximum(limits, 5);
            mResults[player].mFoulPenalty =
                BilClampScore(BilScoreMinimum(limits, 5), -pPlayer->GetFoulCount() * 3, maximum);
        }
        mResults[player].mMaxStat = pPlayer->GetMaxStat();
        mResults[player].unk18 = 0;
        BilScoreLimits limits = BilGetScoreLimits();
        s32 maximum = BilScoreMaximum(limits, 5);
        mResults[player].mTotal =
            BilClampScore(BilScoreMinimum(limits, 5),
                          mResults[player].unk18 + mResults[player].mFoulPenalty +
                              (mResults[player].mScore + mResults[player].mShotBonus),
                          maximum);
    }
    if (playerNum > 1) {
        mWinner = -1;
        bool hasLeader = false;
        s32 leaderTotal;
        for (s32 player = 0; player < pMgr->GetPlayerNum(); player++) {
            const s32 total = mResults[player].mTotal;
            if (!hasLeader || total > leaderTotal) {
                mWinner = player;
                leaderTotal = total;
                hasLeader = true;
            } else if (total == leaderTotal) {
                mWinner = -1;
            }
        }
    } else {
        mWinner = 0;
    }
    mpStateMachine->ChangeState(STATE_SUMMARY);
}

const RPBilPlayerResult* RPBilResult::GetPlayerResult(s32 player) const {
    return &mResults[player];
}

s32 RPBilResult::GetWinner() const {
    return mWinner;
}

void RPBilResult::UserDraw() {
    if (mpStateMachine->GetState() != 0) {
        if (RPGrpRenderer::GetCurrentViewNo() == 9) {
            if (RPGrpRenderer::GetDrawStep() == 5) {
                mpLyt->Draw();
            }
        }
    }
}
