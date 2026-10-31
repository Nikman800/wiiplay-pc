// Fishing result screen (RPFshResultLayout): the caught fish scroll past with their points, then
// the medal (one player) or the winner (two players) and the result menu.
// Unit 0x8020C478-0x8020E1F0 (dtk/game_units.tsv). Functions in reverse address order.
#include <Pack/RPParty/RPFshScene/RPFshResultLayout.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPKernel.h>
#include <Pack/RPParty/RPFshScene/RPFshCamera.h>
#include <Pack/RPParty/RPFshScene/RPFshPlayer.h>
#include <Pack/RPParty/RPFshScene/RPFshPlayerMgr.h>
#include <Pack/RPParty/RPFshScene/RPFshResultFish.h>
#include <Pack/RPParty/RPFshScene/RPFshScoreLayout.h>
#include <Pack/RPParty/RPFshScene/RPFshSound.h>
#include <Pack/RPParty/RPPartyCommon/RPCls803572E0.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPLytObjBase.h>
#include <Pack/RPParty/RPPartyCommon/RPLytUtil.h>
#include <Pack/RPParty/RPPartyCommon/RPMedalResult.h>
#include <Pack/RPParty/RPPartyCommon/RPResultMenu.h>
#include <Pack/RPParty/RPPartyCommon/RPVsWinner.h>
#include <Pack/RPSystem.h>

#include <egg/core.h>
#include <egg/gfxe.h>

// Keep the original screen-position copy as its own function in this unit.
#pragma auto_inline off
static void ResultFishGetScreenPos(nw4r::math::VEC2* pos, const RPFshResultFish* fish);
#pragma auto_inline reset

f32 RPFshResultLayout::sScrollRange[PLAYER_MAX] = {-0.2f, 0.2f};
f32 RPFshResultLayout::sLabelMinY = -0.7f;
f32 RPFshResultLayout::sLabelMaxY = 0.7f;
f32 RPFshResultLayout::sLabelStep = 0.25f;

RPFshResultLayout* RPFshResultLayout::sInstance;

void RPFshResultLayout::CreateInstance() {
    sInstance = new RPFshResultLayout();
}

void RPFshResultLayout::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPFshResultLayout::~RPFshResultLayout() {}

RPFshResultLayout::RPFshResultLayout() {
    for (int i = 0; i < PLAYER_MAX; i++) {
        for (int j = 0; j < FISH_TYPE_NUM; j++) {
            for (int k = 0; k < FISH_MAX; k++) {
                mpFish[i][j][k] = new RPFshResultFish(j);
            }
        }
    }

    mpResAccessor = RPSysLytResAccessor::create(NULL);
    // A reference cast: converting the this pointer would add a NULL check
    RPGrpRenderer::GetCurrent()->AppendDrawObject(&static_cast<IRPGrpDrawObject&>(*this));
}

// String-pool relocation names differ; instructions and string offsets match.
void RPFshResultLayout::Create() {
    for (int i = 0; i < PLAYER_MAX; i++) {
        for (int j = 0; j < FISH_TYPE_NUM; j++) {
            for (int k = 0; k < FISH_MAX; k++) {
                mpFish[i][j][k]->Create();
            }
        }
    }

    RPLytObjBase::AttachArcFile(RPSysResourceManager::LoadGameLocalArchive(), "fishing.arc",
                                mpResAccessor);
    mpLayout = RPSysLayout::create(NULL, mpResAccessor, "fishingResult_00.brlyt");

    for (int i = 0; i < PLAYER_MAX; i++) {
        const char* pointNames[PLAYER_MAX] = {"N_pointL_00", "N_pointR_00"};
        mpPointPanes[i] = mpLayout->findPane(pointNames[i]);
        mScrollMax[i] = mpPointPanes[i]->GetTranslate().y /
                        EGG::Screen::GetSizeYMax(EGG::Screen::TV_MODE_STD);
    }

    for (int i = 0; i < PLAYER_MAX; i++) {
        const char* sideNames[PLAYER_MAX] = {"N_L_00", "N_R_00"};
        mpSidePanes[i] = mpLayout->findPane(sideNames[i]);
    }

    const char* anmNames[3] = {"fishingResult_00_inAll_00.brlan",
                               "fishingResult_00_switchScore_00.brlan",
                               "fishingResult_00_outAll_00.brlan"};
    for (int i = 0; i < 3; i++) {
        mpAnmObjs[i] = mpLayout->createAnmObj(NULL, mpResAccessor, anmNames[i]);
    }

    const char* faceNames[PLAYER_MAX][2] = {{"N_playerFaceL_00", "N_playerFaceL_01"},
                                            {"N_playerFaceR_00", "N_playerFaceR_01"}};
    for (int i = 0; i < PLAYER_MAX; i++) {
        for (int j = 0; j < 2; j++) {
            mpFacePanes[i][j] = mpLayout->findPane(faceNames[i][j]);
        }
    }

    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();
    for (int i = pPlayerMgr->GetPlayerNum(); i < PLAYER_MAX; i++) {
        mpPointPanes[i]->SetVisible(false);
    }
    for (int i = pPlayerMgr->GetPlayerNum(); i < PLAYER_MAX; i++) {
        mpSidePanes[i]->SetVisible(false);
    }

    const char* labelLayoutName = "fishingResult_01.brlyt";
    for (int i = 0; i < PLAYER_MAX; i++) {
        for (int k = 0; k < FISH_MAX; k++) {
            mpLabelLayouts[i][k] = RPSysLayout::create(NULL, mpResAccessor, labelLayoutName);
        }
    }

    const char* labelPointNames[PLAYER_MAX] = {"N_pointL_00", "N_pointR_00"};
    for (int i = 0; i < PLAYER_MAX; i++) {
        for (int k = 0; k < FISH_MAX; k++) {
            mpLabelPointPanes[i][k] = mpLabelLayouts[i][k]->findPane(labelPointNames[i]);
            mpLabelLayouts[i][k]->findPane(labelPointNames[1 - i])->SetVisible(false);
            mpLabelPanes[i][k] = mpLabelLayouts[i][k]->findPane("N_B2_00");
        }
    }

    RPSysMessage* pMessage = RPCls803572E0::GetInstance()->GetMessage();
    const char* playerNames[PLAYER_MAX] = {"T_playerL_00", "T_playerR_00"};
    for (int i = 0; i < PLAYER_MAX; i++) {
        mpLayout->findTextBox(playerNames[i])->setMessage(pMessage, 0x807, i, 0);
    }
}

void RPFshResultLayout::Reset() {
    mState = STATE_NONE;
    mNextState = STATE_NONE;
    mIsChanging = false;
    mStateTimer = 0;
    mRetry = true;

    for (int i = 0; i < PLAYER_MAX; i++) {
        for (int j = 0; j < FISH_TYPE_NUM; j++) {
            for (int k = 0; k < FISH_MAX; k++) {
                mpFish[i][j][k]->Reset();
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        mpAnmObjs[i]->resetFrame();
        mpAnmObjs[i]->stop();
        mpAnmObjs[i]->setFrame(0.0f);
    }

    for (int i = 0; i < RPFshPlayerMgr::GetInstance()->GetPlayerNum(); i++) {
        mpPointPanes[i]->UnbindAllAnimation(true);
        mpSidePanes[i]->UnbindAllAnimation(true);
    }

    mScroll[0] = -1.0f;
    mScroll[1] = -1.0f;
}

void RPFshResultLayout::Exit() {}

void RPFshResultLayout::Calculate() {
    RPFshPlayerMgr* pPlayerMgr;
    int i;
    RPFshStateFuncs<RPFshResultLayout> funcs[STATE_NUM] = {
        {NULL, NULL},
        {&RPFshResultLayout::CameraMoveEnter, &RPFshResultLayout::CameraMoveCalc},
        {&RPFshResultLayout::IntroEnter, &RPFshResultLayout::IntroCalc},
        {&RPFshResultLayout::CountPointsEnter, &RPFshResultLayout::CountPointsCalc},
        {&RPFshResultLayout::JudgeEnter, &RPFshResultLayout::JudgeCalc},
        {&RPFshResultLayout::SingleEndEnter, &RPFshResultLayout::SingleEndCalc},
        {&RPFshResultLayout::VersusEndEnter, &RPFshResultLayout::VersusEndCalc},
        {&RPFshResultLayout::FinishEnter, &RPFshResultLayout::FinishCalc},
        {NULL, NULL},
    };

    if (mIsChanging) {
        mState = mNextState;
        mIsChanging = false;
        mStateTimer = 0;

        RPFshStateFuncs<RPFshResultLayout>::Func enter = funcs[mState].mEnter;
        if (enter != NULL) {
            (this->*enter)();
        }
    }

    RPFshStateFuncs<RPFshResultLayout>::Func calc = funcs[mState].mCalc;
    if (calc != NULL) {
        (this->*calc)();
    }

    pPlayerMgr = RPFshPlayerMgr::GetInstance();
    if (mState != STATE_NONE) {
        for (i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
            nw4r::lyt::Pane* pPane = mpPointPanes[i];
            nw4r::math::VEC3 trans(pPane->GetTranslate().x,
                                   pPane->GetTranslate().y,
                                   pPane->GetTranslate().z);
            trans.y = mScroll[i] * EGG::Screen::GetSizeYMax(EGG::Screen::TV_MODE_STD);
            pPane->SetTranslate(trans);
        }

        mpLayout->calc();
    }
}

void RPFshResultLayout::CameraMoveEnter() {
    RPFshCameraMgr::GetInstance()->GetCamera()->MoveToResultView();
}

void RPFshResultLayout::CameraMoveCalc() {
    s32 fadeFrames = RPFshGetBgmFadeFrames(0x68);
    if (mStateTimer == fadeFrames) {
        RPSndAudioMgr::instance()->startBgm(0x71U);
    }

    if (!RPFshCameraMgr::GetInstance()->GetCamera()->IsActive() && mStateTimer >= fadeFrames) {
        mNextState = STATE_INTRO;
        mIsChanging = true;
    }

    mStateTimer++;
}

void RPFshResultLayout::IntroEnter() {
    for (int i = 0; i < RPFshPlayerMgr::GetInstance()->GetPlayerNum(); i++) {
        mpPointPanes[i]->UnbindAllAnimation(true);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[0], mpPointPanes[i], true);
        mpSidePanes[i]->UnbindAllAnimation(true);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[0], mpSidePanes[i], true);
    }

    mpAnmObjs[0]->restart();
}

void RPFshResultLayout::IntroCalc() {
    if (mpAnmObjs[0]->isFinished()) {
        mNextState = STATE_COUNT_POINTS;
        mIsChanging = true;
    }
}

void RPFshResultLayout::CountPointsEnter() {
    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();

    s32 maxNum = 0;
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = pPlayerMgr->GetPlayer(i);
        if (pPlayer->GetCaughtNum() > maxNum) {
            maxNum = pPlayer->GetCaughtNum();
        }
    }

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = pPlayerMgr->GetPlayer(i);
        for (int k = 0; k < pPlayer->GetCaughtNum(); k++) {
            const RPFshCaughtFish& rCaught = pPlayer->GetCaughtFish(k);
            RPFshResultFish* pFish = mpFish[i][rCaught.mType][k];
            nw4r::math::VEC2 pos(sScrollRange[i], sLabelMinY - sLabelStep * k);
            pFish->SetScreenPos(nw4r::math::VEC2(pos));
            pFish->SetScale(1.3f * rCaught.mSize);
            pFish->Show();
        }
    }

    mCountDone[0] = false;
    mCountDone[1] = false;
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        mScroll[i] = sLabelMinY - sLabelStep * (0.5f + maxNum);
    }

    // Scalar replacement keeps the message pointer ahead of the pane-name induction pointers.
    struct Message { RPSysMessage* ptr; };
    Message message = {RPCls803572E0::GetInstance()->GetMessage()};
    wchar_t text[20];
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        s32 total = RPFshScoreLayout::GetInstance()->CalcTotalPoints(i);

        const char* ptsNames[PLAYER_MAX] = {"T_ptsL_00", "T_ptsR_00"};
        RPSysLytTextBox* pPts = mpLayout->findTextBox(ptsNames[i]);
        pPts->setMessage(message.ptr, 0x809, 1, 0);
        if (RPSysStringUtility::IsSingular(total)) {
            pPts->setMessage(message.ptr, 0x809, 0, 0);
        }

        const char* scoreNames[PLAYER_MAX] = {"T_scoreL_00", "T_scoreR_00"};
        RPSysStringUtility::GetStringHalfSizeNumber(total, text, 20, false);
        mpLayout->findTextBox(scoreNames[i])->setText(text, 0);
    }

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = pPlayerMgr->GetPlayer(i);
        for (int k = 0; k < pPlayer->GetCaughtNum(); k++) {
            const RPFshCaughtFish& rCaught = pPlayer->GetCaughtFish(k);
            s32 points = RPFshScoreLayout::CalcFishPoints(rCaught.mType, rCaught.mSize);

            const char* ptsNames[PLAYER_MAX] = {"T_ptsL_00", "T_ptsR_00"};
            RPSysLytTextBox* pPts = mpLabelLayouts[i][k]->findTextBox(ptsNames[i]);
            pPts->setMessage(message.ptr, 0x808, 1, 0);
            if (RPSysStringUtility::IsSingular(points)) {
                pPts->setMessage(message.ptr, 0x808, 0, 0);
            }

            RPSysStringUtility::GetStringHalfSizeNumber(points, text, 20, false);
            const char* pointNames[PLAYER_MAX] = {"T_fishPointL_00", "T_fishPointR_00"};
            mpLabelLayouts[i][k]->findTextBox(pointNames[i])->setText(text, 0);
        }
    }
}

void RPFshResultLayout::CountPointsCalc() {
    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = pPlayerMgr->GetPlayer(i);
        if (mCountDone[i]) {
            continue;
        }

        const RPFshCaughtFish* caught = &pPlayer->GetCaughtFish(0);
        for (int k = 0; k < pPlayer->GetCaughtNum(); k++) {
            RPFshResultFish* pFish = mpFish[i][caught[k].mType][k];
            nw4r::math::VEC2 pos;
            ResultFishGetScreenPos(&pos, pFish);
            pos.y += GetScrollSpeed();
            pFish->SetScreenPos(nw4r::math::VEC2(pos));

            if (pos.y > sLabelMinY && pos.y < sLabelMaxY) {
                mpFish[i][caught[k].mType][k]->Calculate();
            }

            nw4r::lyt::Pane* pPointPane = mpLabelPointPanes[i][k];
            if (pos.y > sLabelMinY && pos.y < sLabelMaxY) {
                pPointPane->SetVisible(true);
                if (caught[k].mBonus > 1.0f) {
                    mpLabelPanes[i][k]->SetVisible(true);
                } else {
                    mpLabelPanes[i][k]->SetVisible(false);
                }

                f32 x = pos.x * EGG::Screen::GetSizeXMax(EGG::Screen::TV_MODE_STD);
                f32 y = pos.y * EGG::Screen::GetSizeYMax(EGG::Screen::TV_MODE_STD);
                nw4r::math::VEC3 trans(x, y, pPointPane->GetTranslate().z);
                pPointPane->SetTranslate(trans);
                mpLabelPanes[i][k]->SetTranslate(trans);
            } else {
                pPointPane->SetVisible(false);
                mpLabelPanes[i][k]->SetVisible(false);
            }

            if (k == pPlayer->GetCaughtNum() - 1 && pos.y >= sLabelMaxY) {
                mCountDone[i] = true;
            }
        }

        if (pPlayer->GetCaughtNum() == 0) {
            mCountDone[i] = true;
        }
    }

    bool allDone = true;
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        if (!mCountDone[i]) {
            allDone = false;
        }
    }

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        mScroll[i] += GetScrollSpeed();
    }

    bool scrolled = true;
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        if (mScroll[i] < mScrollMax[i]) {
            scrolled = false;
        } else {
            mScroll[i] = mScrollMax[i];
        }
    }

    if (allDone && scrolled) {
        mNextState = STATE_JUDGE;
        mIsChanging = true;
    }

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = pPlayerMgr->GetPlayer(i);
        for (int k = 0; k < pPlayer->GetCaughtNum(); k++) {
            mpLabelLayouts[i][k]->calc();
        }
    }
}

void RPFshResultLayout::JudgeEnter() {
    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();

    mBestScore = -10000;
    bool tie = false;
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        s32 total = RPFshScoreLayout::GetInstance()->CalcTotalPoints(i);
        if (total == mBestScore) {
            tie = true;
        }
        if (total > mBestScore) {
            mBestScore = total;
        }
    }

    if (!tie) {
        for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
            s32 total = RPFshScoreLayout::GetInstance()->CalcTotalPoints(i);
            mpPointPanes[i]->UnbindAllAnimation(true);
            if (total == mBestScore) {
                RPSysLayout::bindAnmObjToPane(mpAnmObjs[1], mpPointPanes[i], true);
            }
        }

        mpAnmObjs[1]->restart();
    }
}

void RPFshResultLayout::JudgeCalc() {
    if (mStateTimer > 120) {
        if (RPFshPlayerMgr::GetInstance()->GetPlayerNum() == 1) {
            mNextState = STATE_SINGLE_END;
            mIsChanging = true;
        } else {
            mNextState = STATE_VERSUS_END;
            mIsChanging = true;
        }
    }

    mStateTimer++;
}

void RPFshResultLayout::SingleEndEnter() {
    RPCommon::GetInstance()->GetMedalResult()->Start(
        RPFshScoreLayout::GetInstance()->CalcTotalPoints(0));

    for (int i = 0; i < RPFshPlayerMgr::GetInstance()->GetPlayerNum(); i++) {
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpPointPanes[i], true);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpSidePanes[i], true);
    }

    mpAnmObjs[2]->restart();
}

void RPFshResultLayout::SingleEndCalc() {
    if (RPCommon::GetInstance()->GetMedalResult()->GetState() == 4) {
        mNextState = STATE_FINISH;
        mIsChanging = true;
    }
}

void RPFshResultLayout::VersusEndEnter() {
    s32 winner = -1;
    bool tie = false;
    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();
    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        s32 total = RPFshScoreLayout::GetInstance()->CalcTotalPoints(i);
        if (total == mBestScore) {
            if (winner > -1) {
                tie = true;
            }
            if (winner <= -1) {
                winner = i;
            }
        }
    }

    mWinner = winner;
    if (tie) {
        mWinner = -1;
    }

    RPCommon::GetInstance()->GetVsWinner()->Start(mWinner);
    RPSndAudioMgr::instance()->startSystemSe(0x3AU);
}

void RPFshResultLayout::VersusEndCalc() {
    if (RPCommon::GetInstance()->GetVsWinner()->IsStateEntered(3)) {
        for (int i = 0; i < RPFshPlayerMgr::GetInstance()->GetPlayerNum(); i++) {
            RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpPointPanes[i], true);
            RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpSidePanes[i], true);
        }

        mpAnmObjs[2]->restart();
    }

    if (RPCommon::GetInstance()->GetVsWinner()->GetState() == 5) {
        mNextState = STATE_FINISH;
        mIsChanging = true;
    }
}

void RPFshResultLayout::FinishEnter() {
    if (RPFshPlayerMgr::GetInstance()->GetPlayerNum() == 1) {
        RPCommon::GetInstance()->GetResultMenu()->Start(1, -1);
    } else {
        RPCommon::GetInstance()->GetResultMenu()->Start(2, mWinner);
    }
}

void RPFshResultLayout::FinishCalc() {
    RPResultMenu* pMenu = RPCommon::GetInstance()->GetResultMenu();
    if (pMenu->GetState() == 4) {
        if (pMenu->GetChoice() == 0) {
            mRetry = true;
        } else {
            mRetry = false;
        }

        mNextState = STATE_DONE;
        mIsChanging = true;
    }
}

#pragma auto_inline off
f32 RPFshResultLayout::GetScrollSpeed() const {
    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        if (EGG::CoreControllerMgr::instance()
                ->getNthController(pPlayerMgr->GetPlayer(i)->GetControllerNo())
                ->down(WPAD_BUTTON_A)) {
            return 0.0175f;
        }
    }

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        if (EGG::CoreControllerMgr::instance()
                ->getNthController(pPlayerMgr->GetPlayer(i)->GetControllerNo())
                ->down(WPAD_BUTTON_B)) {
            return 0.0f;
        }
    }

    return 0.007f;
}
#pragma auto_inline reset

void RPFshResultLayout::Start() {
    mNextState = STATE_CAMERA_MOVE;
    mIsChanging = true;
}

void RPFshResultLayout::UserDraw() {
    s32 state = mState;
    if (state != STATE_NONE && state != STATE_CAMERA_MOVE &&
        RPGrpRenderer::GetCurrentViewNo() == 4 && RPGrpRenderer::GetDrawStep() == 5) {
        switch (state) {
        case STATE_COUNT_POINTS:
        case STATE_JUDGE:
            DrawFishLabels();
            break;
        }

        RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();
        RPFshScoreLayout* pScore = RPFshScoreLayout::GetInstance();
        mpLayout->draw();

        for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
            RPSysKokeshiIcon* icon = pScore->GetIcon(i);
            RPDrawKokeshiIcon(icon, mpFacePanes[i][1], TRUE, NULL);
        }
        for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
            RPSysKokeshiIcon* icon = pScore->GetIcon(i);
            RPDrawKokeshiIcon(icon, mpFacePanes[i][0], FALSE, NULL);
        }
    }
}

#pragma auto_inline off
// NONMATCHING-DEBT: copies RPFshResultFish::mScreenPos (private, at 0x70) as two words
// (lwz/stw). *pos = fish->GetScreenPos() copies it as floats (lfs/stfs, through a stack temporary
// with the by-value getter); a by-value return uses r3/r4 instead of the out pointer. A
// const-reference getter (`*pos = fish->GetScreenPos();`, header change) or an out-pointer getter
// still copies as floats (lfs/stfs, objcmp 0.9981): VEC2's implicit copy is member-wise; only a
// _VEC2/POD-typed copy gives lwz/stw.
static void ResultFishGetScreenPos(nw4r::math::VEC2* pos, const RPFshResultFish* fish) {
#if defined(PORT_HOST)
    // 0x70 is mScreenPos's offset in the 32-bit layout
    *pos = fish->GetScreenPos();
#else
    struct Words { u32 x, y; };
    *reinterpret_cast<Words*>(pos) =
        *reinterpret_cast<const Words*>(reinterpret_cast<const u8*>(fish) + 0x70);
#endif
}

void RPFshResultLayout::DrawFishLabels() {
    RPFshPlayerMgr* pPlayerMgr = RPFshPlayerMgr::GetInstance();

    for (int i = 0; i < pPlayerMgr->GetPlayerNum(); i++) {
        RPFshPlayer* pPlayer = pPlayerMgr->GetPlayer(i);
        for (int k = 0; k < pPlayer->GetCaughtNum(); k++) {
            const RPFshCaughtFish& caught = pPlayer->GetCaughtFish(k);
            nw4r::math::VEC2 pos;
            ResultFishGetScreenPos(&pos, mpFish[i][caught.mType][k]);
            if (pos.y > sLabelMinY && pos.y < sLabelMaxY) {
                mpLabelLayouts[i][k]->draw();
            }
        }
    }
}
#pragma auto_inline reset
