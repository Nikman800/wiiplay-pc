// Shooting Range result banner (RPDucEndLayout): final scores, the winner's switch animation and
// the Mii faces.
// Unit 0x80255064-0x80255D8C (dtk/game_units.tsv). Functions in reverse address order.
// PlayOutSingle and PlayOutMsg are only inlined into Calculate; the linker strips their own
// copies (the string pool still holds "N_msg_00" between PlayOut's and PlayIn's strings).
#include <Pack/RPParty/RPDucScene/RPDucHud.h>

#include <Pack/RPGraphics.h>
#include <Pack/RPKernel.h>
#include <Pack/RPParty/RPDucScene/RPDucGame.h>
#include <Pack/RPParty/RPPartyCommon/RPLytUtil.h>
#include <Pack/RPParty/RPPartyCommon/RPMiiIconSet.h>

RPDucEndLayout::RPDucEndLayout() {}

RPDucEndLayout::~RPDucEndLayout() {}

void RPDucEndLayout::UpdateAnimStates() {
    if (mState == 2) {
        if (mpAnmObjs[0]->isFinished()) {
            mState = 0;
            if (RPDucGame::sInstance->GetMode() == 1) {
                s32 leader = RPDucScoreMgr::sInstance->GetLeader();
                if (leader != -1) {
                    mpLayout->unbindAnmObj(mpAnmObjs[2]);
                    mpLayout->unbindAnmObj(mpAnmObjs[3]);
                    if (leader == 0) {
                        mpLayout->bindAnmObj(mpAnmObjs[2]);
                        mpAnmObjs[2]->restart();
                    } else if (leader == 1) {
                        mpLayout->bindAnmObj(mpAnmObjs[3]);
                        mpAnmObjs[3]->restart();
                    }
                }
            }
        }
    } else if (mState == 3) {
        if (mpAnmObjs[1]->isFinished()) {
            mState = 1;
            mDone = true;
        }
    } else if (mState == 4) {
        if (mpAnmObjs[1]->isFinished()) {
            mState = 5;
            mDone = true;
        }
    }
}

#pragma auto_inline off
void RPDucEndLayout::DrawMiiIcons() {
    if (mState == 1) {
        return;
    }

    s32 best = RPDucScoreMgr::sInstance->GetScore(1);
    if (best <= RPDucScoreMgr::sInstance->GetScore(0)) {
        best = RPDucScoreMgr::sInstance->GetScore(0);
    }
    // NONMATCHING-DEBT: reads RPGrpView::mpScreen by offset. A plain member read matches (tested
    // with mpScreen public), but GetView(1)->GetScreen() swaps best/pScreen (r31/r30, 94/106) in
    // every declaration order tried (pScreen first, split declarations, an RPGrpView* local,
    // NULL-initialised); no friend of RPGrpView fits this layout class.
#if defined(PORT_HOST)
    // 0x10 is the 32-bit layout's offset
    RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(1)->GetScreen();
#else
    RPGrpScreen* pScreen = *reinterpret_cast<RPGrpScreen**>(
        reinterpret_cast<char*>(RPGrpRenderer::GetCurrent()->GetView(1)) + 0x10);
#endif

    RPDrawKokeshiIcon(RPMiiIconSet::sInstance->GetIcon(0),
                      best >= 100 ? mpLayout->findPane("N_1pFaceA_01")
                                  : mpLayout->findPane("N_1pFaceB_01"),
                      TRUE, pScreen);
    RPDrawKokeshiIcon(RPMiiIconSet::sInstance->GetIcon(0),
                      best >= 100 ? mpLayout->findPane("N_1pFaceA_00")
                                  : mpLayout->findPane("N_1pFaceB_00"),
                      FALSE, pScreen);
    RPDrawKokeshiIcon(RPMiiIconSet::sInstance->GetIcon(1),
                      best >= 100 ? mpLayout->findPane("N_2pFaceA_01")
                                  : mpLayout->findPane("N_2pFaceB_01"),
                      TRUE, pScreen);
    RPDrawKokeshiIcon(RPMiiIconSet::sInstance->GetIcon(1),
                      best >= 100 ? mpLayout->findPane("N_2pFaceA_00")
                                  : mpLayout->findPane("N_2pFaceB_00"),
                      FALSE, pScreen);
}
#pragma auto_inline reset

void RPDucEndLayout::Load(EGG::Archive* pArchive) {
    const char* anmNames[4] = {
        "duckhuntEnd_00_inAll_00.brlan",
        "duckhuntEnd_00_outAll_00.brlan",
        "duckhuntEnd_00_switch1p_00.brlan",
        "duckhuntEnd_00_switch2p_00.brlan",
    };
    AttachArcFile(pArchive, "duckhunt.arc", mpResAccessor);
    mpLayout = RPSysLayout::create(NULL, mpResAccessor, "duckhuntEnd_00.brlyt");
    for (s32 i = 0; i < 4; i++) {
        mpAnmObjs[i] = mpLayout->createAnmObj(NULL, mpResAccessor, anmNames[i]);
        if ((u32)(i - 2) <= 1) {
            mpAnmObjs[i]->setPlayMode(RPSysFrameCtrl::EPlayMode_Loop);
        } else {
            mpAnmObjs[i]->setPlayMode(RPSysFrameCtrl::EPlayMode_OneTime);
        }
    }
}

void RPDucEndLayout::Reset() {
    mState = 1;
    mTimer = 180;
    unk24 = false;
    mDone = false;

    RPSysMessage* pMsg;
    if (RPDucGame::sInstance->GetMode() != 1) {
        pMsg = RPDucHud::sInstance->GetMessage();
        RPSysStringUtility::GetStringFullSizeNumber(RPDucScoreMgr::sInstance->GetScore(0),
                                                    mScoreText[0], 4, false);
        mpLayout->findTextBox("T_scoreNumS_00")->setMessage(pMsg, 0x840, 0, 1, mScoreText[0]);
    } else {
        pMsg = RPDucHud::sInstance->GetMessage();
        RPSysStringUtility::GetStringFullSizeNumber(RPDucScoreMgr::sInstance->GetScore(0),
                                                    mScoreText[0], 4, false);
        mpLayout->findTextBox("T_scoreNumL_00")->setMessage(pMsg, 0x83C, 0, 1, mScoreText[0]);
        RPSysStringUtility::GetStringFullSizeNumber(RPDucScoreMgr::sInstance->GetScore(1),
                                                    mScoreText[1], 4, false);
        mpLayout->findTextBox("T_scoreNumR_00")->setMessage(pMsg, 0x83C, 1, 1, mScoreText[1]);
    }

    mpLayout->reset();
    s32 i = 0;
    do {
        mpAnmObjs[i]->resetFrame();
        mpAnmObjs[i]->stop();
        mpAnmObjs[i]->setFrame(0.0f);
    } while (++i < 4);

    pMsg = RPDucHud::sInstance->GetMessage();
    mpLayout->findTextBox("T_1pTextA_00")->setMessage(pMsg, 0x83D, 0, 0);
    mpLayout->findTextBox("T_1pTextB_00")->setMessage(pMsg, 0x83D, 0, 0);
    mpLayout->findTextBox("T_2pTextA_00")->setMessage(pMsg, 0x83D, 1, 0);
    mpLayout->findTextBox("T_2pTextB_00")->setMessage(pMsg, 0x83D, 1, 0);
    mpLayout->findTextBox("T_1pTextSA_00")->setMessage(pMsg, 0x83F, 0, 0);
    mpLayout->findTextBox("T_msgText_00")->setMessage(pMsg, 0x86A, 0, 0);
    mpLayout->findTextBox("T_msgText_01")->setMessage(pMsg, 0x86A, 0, 0);

    mpLayout->unbindAllAnmObj();
    mpLayout->unbindAnmObj(mpAnmObjs[0]);
    mpLayout->bindAnmObj(mpAnmObjs[0]);
    mpAnmObjs[0]->restart();
    mpAnmObjs[0]->stop();
    mpAnmObjs[0]->setFrame(0.0f);
}

void RPDucEndLayout::Calculate() {
    UpdateAnimStates();

    if (mState == 2 || mState == 0) {
        if (--mTimer <= 0) {
            if (RPDucGame::sInstance->GetMode() != 1) {
                PlayOutSingle();
            } else {
                PlayOutMsg();
            }
        }
    }

    mpLayout->calc();
}

void RPDucEndLayout::Draw() {
    mpLayout->draw();
    if (RPDucGame::sInstance->GetMode() == 1) {
        DrawMiiIcons();
    } else if (mState != 1) {
        RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(1)->GetScreen();
        nw4r::lyt::Pane* pPane = mpLayout->findPane("N_1pFaceSA_01");
        RPDrawKokeshiIcon(RPMiiIconSet::sInstance->GetIcon(0),
                          pPane, TRUE, pScreen);
        pPane = mpLayout->findPane("N_1pFaceSA_00");
        RPDrawKokeshiIcon(RPMiiIconSet::sInstance->GetIcon(0),
                          pPane, FALSE, pScreen);
    }
}

void RPDucEndLayout::VF_0x18() {}

void RPDucEndLayout::PlayIn() {
    if (mState == 0) {
        return;
    }

    RPDucGame::sInstance->GetCurrentRound();
    if (RPDucGame::sInstance->GetMode() != 1) {
        mpLayout->findPane("N_score_00")->SetVisible(false);
        mpLayout->findPane("N_scoreS_00")->SetVisible(true);
        RPSysMessage* pMsg = RPDucHud::sInstance->GetMessage();
        RPSysStringUtility::GetStringFullSizeNumber(RPDucScoreMgr::sInstance->GetScore(0),
                                                    mScoreText[0], 4, false);
        mpLayout->findTextBox("T_scoreNumS_00")->setMessage(pMsg, 0x840, 0, 1, mScoreText[0]);
    } else {
        mpLayout->findPane("N_score_00")->SetVisible(true);
        mpLayout->findPane("N_scoreS_00")->SetVisible(false);
        s32 best =
            RPDucScoreMgr::sInstance->GetScore(1) <= RPDucScoreMgr::sInstance->GetScore(0)
                ? RPDucScoreMgr::sInstance->GetScore(0)
                : RPDucScoreMgr::sInstance->GetScore(1);
        if (best >= 100) {
            mpLayout->findPane("N_figureA_00")->SetVisible(true);
            mpLayout->findPane("N_figureB_00")->SetVisible(false);
        } else {
            mpLayout->findPane("N_figureA_00")->SetVisible(false);
            mpLayout->findPane("N_figureB_00")->SetVisible(true);
        }
        RPSysMessage* pMsg = RPDucHud::sInstance->GetMessage();
        RPSysStringUtility::GetStringFullSizeNumber(RPDucScoreMgr::sInstance->GetScore(0),
                                                    mScoreText[0], 4, false);
        mpLayout->findTextBox("T_scoreNumL_00")->setMessage(pMsg, 0x83C, 0, 1, mScoreText[0]);
        RPSysStringUtility::GetStringFullSizeNumber(RPDucScoreMgr::sInstance->GetScore(1),
                                                    mScoreText[1], 4, false);
        mpLayout->findTextBox("T_scoreNumR_00")->setMessage(pMsg, 0x83C, 1, 1, mScoreText[1]);
    }

    mpLayout->unbindAnmObj(mpAnmObjs[0]);
    mpLayout->bindAnmObj(mpAnmObjs[0]);
    mpAnmObjs[0]->restart();
    unk24 = true;
    mState = 2;
}

void RPDucEndLayout::PlayOutSingle() {
    if (mState == 1) {
        return;
    }
    mpLayout->unbindAnmObj(mpAnmObjs[0]);
    mpLayout->unbindAnmObj(mpAnmObjs[1]);
    mpLayout->bindAnmObj(mpAnmObjs[1]);
    mpAnmObjs[1]->restart();
    mState = 3;
}

void RPDucEndLayout::PlayOutMsg() {
    if (mState == 1) {
        return;
    }
    mpLayout->unbindAnmObj(mpAnmObjs[0]);
    mpLayout->unbindAnmObj(mpAnmObjs[1]);
    mpLayout->bindAnmObjToPane(mpAnmObjs[1], "N_msg_00", true);
    mpAnmObjs[1]->restart();
    mState = 4;
}

void RPDucEndLayout::PlayOut() {
    mpLayout->unbindAnmObj(mpAnmObjs[0]);
    mpLayout->unbindAnmObj(mpAnmObjs[1]);
    mpLayout->unbindAnmObj(mpAnmObjs[2]);
    mpLayout->unbindAnmObj(mpAnmObjs[3]);
    mpLayout->bindAnmObjToPane(mpAnmObjs[1], "N_score_00", true);
    mpAnmObjs[1]->restart();
    mState = 3;
}
