#include <Pack/RPParty/RPCowScene/RPCowHud_3.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysLytAnmObj.h>
#include <Pack/RPKernel/RPSysLytTextBox.h>
#include <Pack/RPKernel/RPSysStringUtility.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPPartyCommon/RPCls803572E0.h>
#include <egg/gfxe/eggScreen.h>

// Column of a 3x4 matrix (ledger RP_Mtx34_getColumn, RPCollisionShape.cpp) [M]
#if defined(PORT_HOST)
// RPCollisionShape.cpp defines it returning the column. The declaration in the #else is the
// Wii's ABI form of that (a returned struct through a hidden first argument, the matrix by
// address); x86-64 returns the 12 bytes in registers, so the host calls the definition's form.
// (Calling the Wii form crashed in RPCowPerfectMsg::Show on Charge!'s first knocked group.)
extern "C" EGG::Vector3f RP_Mtx34_getColumn(const EGG::Matrix34f& rMtx, int col);
static inline void RP_Mtx34_getColumnInto(nw4r::math::VEC3* pOut, const nw4r::math::MTX34& rMtx, int col) {
    EGG::Vector3f v = RP_Mtx34_getColumn(reinterpret_cast<const EGG::Matrix34f&>(rMtx), col);
    pOut->x = v.x;
    pOut->y = v.y;
    pOut->z = v.z;
}
#define RP_Mtx34_getColumn(pOut, rMtx, col) RP_Mtx34_getColumnInto(pOut, rMtx, col)
#else
extern "C" void RP_Mtx34_getColumn(nw4r::math::VEC3* pOut, nw4r::math::MTX34 mtx, int col);
#endif

void RPCowMainHud::Show() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    if (mAnmState == 0) {
        mpPanes[1]->SetVisible(true);
        mpPanes[5]->SetVisible(true);
        if (pGame->GetPlayerNum() >= 2) {
            mpPanes[6]->SetVisible(true);
        }
        mpLayout->unbindAnmObj(mpAnmObjs[0]);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[0], mpPanes[0], true);
        mpAnmObjs[0]->restart();
        mAnmState = 1;
    }
}

void RPCowMainHud::PlayOut() {
    mpLayout->unbindAnmObj(mpAnmObjs[1]);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[1], mpPanes[0], true);
    mpAnmObjs[1]->restart();
    mTimer = 0;
    mAnmState = 2;
}

void RPCowMainHud::PlayOut1P() {
    mpLayout->unbindAnmObj(mpAnmObjs[8]);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[8], mpPanes[0], true);
    mpAnmObjs[8]->restart();
}

void RPCowMainHud::PlayInResult() {
    mpPanes[4]->SetVisible(true);
    mpPanes[7]->SetVisible(true);
    mpLayout->unbindAnmObj(mpAnmObjs[9]);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[9], mpPanes[4], true);
    mpAnmObjs[9]->restart();
    mTimer = 0;
    mAnmState = 3;
}

void RPCowMainHud::PlayOutResult() {
    if (mAnmState != 7) {
        mpLayout->unbindAnmObj(mpAnmObjs[10]);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[10], mpPanes[4], true);
    }
    mpAnmObjs[10]->restart();
    mTimer = 0;
    mAnmState = 8;
}

void RPCowMainHud::PlayTimeBonus() {
    mpPanes[3]->SetVisible(true);
    mpPanes[1]->SetVisible(true);
    mpPanes[2]->SetVisible(false);
    mpLayout->unbindAnmObj(mpAnmObjs[4]);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[4], mpPanes[0], true);
    mpAnmObjs[4]->restart();
    mTimer = 30;
    mAnmState = 4;
}

bool RPCowMainHud::IsAnimIdle() const {
    bool idle = false;
    if (mAnmState != 4 && mAnmState != 5) {
        idle = true;
    }
    return idle;
}

void RPCowMainHud::PlayTotal() {
    mpPanes[4]->SetVisible(true);
    mpPanes[7]->SetVisible(true);
    mpLayout->unbindAnmObj(mpAnmObjs[10]);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[10], mpPanes[4], true);
    mpLayout->unbindAnmObj(mpAnmObjs[7]);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[7], mpPanes[4], true);
    mpAnmObjs[7]->restart();
    mTimer = 0;
    mAnmState = 7;
}

void RPCowMainHud::SetTime(f32 seconds) {
    wchar_t buf[8];
    RPSysStringUtility::GetStringFullSizeNumber((s32)seconds, buf, 8);
    mpTextBoxes[0]->setText(buf, 0);
    mpTextBoxes[1]->setText(buf, 0);
    mpTextBoxes[2]->setText(buf, 0);
    mpTextBoxes[3]->setText(buf, 0);
    RPSysStringUtility::GetStringFullSizeDigits((s32)((seconds - (s32)seconds) * 100.0f), buf, 8,
                                                2, RPSysStringUtility::PadType_Zero);
    mpTextBoxes[4]->setText(buf, 0);
    mpTextBoxes[5]->setText(buf, 0);
    mpTextBoxes[6]->setText(buf, 0);
    mpTextBoxes[7]->setText(buf, 0);
}

void RPCowMainHud::SetTimeWarning(bool warning) {
    if (warning) {
        mpPanes[1]->SetVisible(false);
        mpPanes[2]->SetVisible(true);
    } else {
        mpPanes[1]->SetVisible(true);
        mpPanes[2]->SetVisible(false);
    }
}

void RPCowMainHud::SetPlayerScore(s32 player, s32 score, s32 add) {
    wchar_t buf[4];
    RPSysStringUtility::GetStringFullSizeNumber(score, buf, 4);
    mpTextBoxes[14]->setText(buf, 0);
    mpTextBoxes[15]->setText(buf, 0);
    switch (player) {
    case 0:
        mpTextBoxes[10]->setText(buf, 0);
        mpTextBoxes[11]->setText(buf, 0);
        if (add) {
            mpLayout->unbindAnmObj(mpAnmObjs[2]);
            RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpPanes[5], true);
            mpAnmObjs[2]->restart();
        }
        break;
    case 1:
        mpTextBoxes[12]->setText(buf, 0);
        mpTextBoxes[13]->setText(buf, 0);
        if (add) {
            mpLayout->unbindAnmObj(mpAnmObjs[3]);
            RPSysLayout::bindAnmObjToPane(mpAnmObjs[3], mpPanes[6], true);
            mpAnmObjs[3]->restart();
        }
        break;
    }
}

void RPCowMainHud::ShowTimeout() {
    wchar_t buf[4];
    RPSysStringUtility::GetStringFullSizeNumber(0, buf, 4);
    mpTextBoxes[14]->setText(buf, 0);
    mpTextBoxes[15]->setText(buf, 0);
    RPSysEffect effect("Cow_2d_timeout", 4, 0);
    effect.create();
    nw4r::math::VEC3 pos;
    RP_Mtx34_getColumn(&pos, mpPanes[7]->GetGlobalMtx(), 3);
    effect.setPos(pos);
    effect.update();
    RPSndAudioMgr::instance()->startSystemSe(0x54u);
}

RPCowMainHud::RPCowMainHud() {}

RPCowMainHud::~RPCowMainHud() {}

void RPCowMainHud::LoadResource() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    if (EGG::Screen::GetTVMode() == EGG::Screen::TV_MODE_WIDE) {
        mpLayout = RPSysLayout::create(NULL, pGame->GetLytAccessor(), "cowMain_01.brlyt");
        const char* anm[] = {"cowMain_01_in_00.brlan",        "cowMain_01_out_00.brlan",
                             "cowMain_01_score_00.brlan",     "cowMain_01_score_01.brlan",
                             "cowMain_01_timeBonus_00.brlan", "cowMain_01_timeBonus_01.brlan",
                             "cowMain_01_addTotal_00.brlan",  "cowMain_01_total_00.brlan",
                             "cowMain_01_out1P_00.brlan",     "cowMain_01_inResult_00.brlan",
                             "cowMain_01_outResult_00.brlan"};
        for (int i = 0; i < 11; i++) {
            mpAnmObjs[i] = mpLayout->createAnmObj(NULL, pGame->GetLytAccessor(), anm[i]);
        }
    } else {
        mpLayout = RPSysLayout::create(NULL, pGame->GetLytAccessor(), "cowMain_00.brlyt");
        const char* anm[] = {"cowMain_00_in_00.brlan",        "cowMain_00_out_00.brlan",
                             "cowMain_00_score_00.brlan",     "cowMain_00_score_01.brlan",
                             "cowMain_00_timeBonus_00.brlan", "cowMain_00_timeBonus_01.brlan",
                             "cowMain_00_addTotal_00.brlan",  "cowMain_00_total_00.brlan",
                             "cowMain_00_out1P_00.brlan",     "cowMain_00_inResult_00.brlan",
                             "cowMain_00_outResult_00.brlan"};
        for (int i = 0; i < 11; i++) {
            mpAnmObjs[i] = mpLayout->createAnmObj(NULL, pGame->GetLytAccessor(), anm[i]);
        }
    }
    const char* pane[] = {"N_cow_00",    "N_time_00",   "N_time_02",  "N_timeBonus_00", "N_result_00",
                          "N_player_00", "N_player_01", "N_score_02", "P_line_00"};
    for (int i = 0; i < 9; i++) {
        mpPanes[i] = mpLayout->findPane(pane[i]);
    }
    const char* text[] = {"T_timer_00",    "T_timer_01",    "T_timer_02",     "T_timer_03",
                          "T_comma_00",    "T_comma_01",    "T_comma_02",     "T_comma_03",
                          "T_player00_00", "T_player00_01", "T_score00_00",   "T_score01_00",
                          "T_score00_01",  "T_score01_01",  "T_score00_02",   "T_score01_02",
                          "T_timeBonus_00", "T_timeBonus_01"};
    for (int i = 0; i < 18; i++) {
        mpTextBoxes[i] = mpLayout->findTextBox(text[i]);
    }
    RPSysMessage* pMsg = RPCls803572E0::GetInstance()->GetMessage();
    mpLayout->findTextBox("T_unit_00")->setMessage(pMsg, 0xA95, 0, 0);
    mpLayout->findTextBox("T_unit_01")->setMessage(pMsg, 0xA95, 0, 0);
    mpLayout->findTextBox("T_unit_02")->setMessage(pMsg, 0xA95, 0, 0);
    mpLayout->findTextBox("T_unit_03")->setMessage(pMsg, 0xA95, 0, 0);
    mpLayout->findTextBox("T_player00_00")->setMessage(pMsg, 0xA99, 0, 0);
    mpLayout->findTextBox("T_player00_01")->setMessage(pMsg, 0xA99, 1, 0);
    mpLayout->findTextBox("T_player00_02")->setMessage(pMsg, 0xA9A, 0, 0);
    mpLayout->findTextBox("T_timeBonus_00")->setMessage(pMsg, 0xA94, 0, 0);
    mpLayout->findTextBox("T_timeBonus_01")->setMessage(pMsg, 0xA94, 0, 0);
}

void RPCowMainHud::OnStart() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    for (int i = 0; i < 9; i++) {
        mpPanes[i]->SetVisible(false);
    }
    mpPanes[0]->SetVisible(true);
    if (pGame->GetPlayerNum() >= 2) {
        mpPanes[8]->SetVisible(true);
    } else {
        mpPanes[8]->SetVisible(false);
    }
    mpLayout->unbindAllAnmObj();
    for (int i = 0; i < 11; i++) {
        mpAnmObjs[i]->resetFrame();
    }
    mTimer = 0;
    mAnmState = 0;
}

void RPCowMainHud::OnStop() {}

void RPCowMainHud::OnActivate() {
    switch (mAnmState) {
    case 3:
        if (mpPanes[4]->IsVisible() && mpAnmObjs[9]->isFinished()) {
        }
        break;
    case 4:
        if (mpAnmObjs[4]->isFinished()) {
            if (--mTimer <= 0) {
                mpLayout->unbindAnmObj(mpAnmObjs[5]);
                RPSysLayout::bindAnmObjToPane(mpAnmObjs[5], mpPanes[0], true);
                mpAnmObjs[5]->restart();
                mTimer = 0;
                mAnmState = 5;
            }
        }
        break;
    case 5:
        if (mpAnmObjs[5]->isFinished()) {
            mpPanes[1]->SetVisible(false);
            mpPanes[2]->SetVisible(false);
            mTimer = 0;
            mAnmState = 1;
        }
        break;
    case 8:
        if (mpAnmObjs[10]->isFinished()) {
            mpPanes[4]->SetVisible(false);
            mTimer = 0;
            mAnmState = 0;
        }
        break;
    case 2:
        if (mpAnmObjs[1]->isFinished()) {
            mpPanes[5]->SetVisible(false);
            mpPanes[6]->SetVisible(false);
            mTimer = 0;
            mAnmState = 0;
        }
        break;
    }
    mpLayout->calc();
}

void RPCowMainHud::DrawScreen(u8 pass) {
    if (pass == 4) {
        mpLayout->draw();
    }
}
