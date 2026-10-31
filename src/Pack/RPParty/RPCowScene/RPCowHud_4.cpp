#include <Pack/RPParty/RPCowScene/RPCowHud_4.h>

#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysLytAnmObj.h>
#include <Pack/RPKernel/RPSysLytTextBox.h>
#include <Pack/RPKernel/RPSysStringUtility.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPPartyCommon/RPCls803572E0.h>

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

namespace {
// Layout element of a player: one-player layout, or player 1 / player 2 of the versus layout
inline s32 GetElement(s32 player, s32 single, s32 first, s32 second) {
    if (RPCowGame::GetInstance()->GetPlayerNum() <= 1) {
        return single;
    }
    if (player == 0) {
        return first;
    }
    if (player == 1) {
        return second;
    }
    return 0;
}
}

void RPCowPerfectMsg::Show(s32 player, s32 score, s32) {
    wchar_t buf[4];
    RPSysStringUtility::GetStringFullSizeNumber(score, buf, 4, true);
    mpTextBoxes[GetElement(player, 0, 2, 4)]->setText(buf, 0);
    mpTextBoxes[GetElement(player, 1, 3, 5)]->setText(buf, 0);
    mpPanes[GetElement(player, 0, 1, 2)]->SetVisible(true);
    mpPanes[GetElement(player, 3, 4, 5)]->SetVisible(true);
    mpAnmObjs[GetElement(player, 0, 1, 2)]->restart();
    mpEffects[player]->create();
    nw4r::math::VEC3 pos;
    RP_Mtx34_getColumn(&pos, mpPanes[GetElement(player, 3, 4, 5)]->GetGlobalMtx(), 3);
    mpEffects[player]->setPos(pos);
    mpEffects[player]->update();
    mStates[player] = 1;
}

RPCowPerfectMsg::RPCowPerfectMsg() {}

RPCowPerfectMsg::~RPCowPerfectMsg() {}

void RPCowPerfectMsg::LoadResource() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpLayout = RPSysLayout::create(NULL, pGame->GetLytAccessor(), "cowPerfect_00.brlyt");
    const char* anm[] = {"cowPerfect_00_Perfect_00.brlan", "cowPerfect_00_Perfect_01.brlan",
                         "cowPerfect_00_Perfect_02.brlan"};
    for (int i = 0; i < 3; i++) {
        mpAnmObjs[i] = mpLayout->createAnmObj(NULL, pGame->GetLytAccessor(), anm[i]);
    }
    const char* pane[] = {"N_perfect_00",    "N_perfect_01",    "N_perfect_02",
                          "N_tPerfect_00",   "N_tPerfect_01",   "N_tPerfect_02",
                          "P_crown_00",      "P_crown_01",      "P_crown_02",
                          "P_crownOn00_00",  "P_crownOn00_01",  "P_crownOn00_02",
                          "P_crownOff00_00", "P_crownOff00_01", "P_crownOff00_02",
                          "P_crownOn01_00",  "P_crownOn01_01",  "P_crownOn01_02",
                          "P_crownOff01_00", "P_crownOff01_01", "P_crownOff01_02"};
    for (int i = 0; i < 21; i++) {
        mpPanes[i] = mpLayout->findPane(pane[i]);
    }
    const char* text[] = {"N_perfectL00_00", "N_perfectL01_00", "N_perfectL00_00",
                          "N_perfectL01_00", "N_perfectL00_01", "N_perfectL01_01"};
    for (int i = 0; i < 6; i++) {
        mpTextBoxes[i] = mpLayout->findTextBox(text[i]);
    }
    RPSysMessage* pMsg = RPCls803572E0::GetInstance()->GetMessage();
    mpLayout->findTextBox("T_perfect00_00")->setMessage(pMsg, 0xA8C, 0, 0);
    mpLayout->findTextBox("T_perfect01_00")->setMessage(pMsg, 0xA8C, 0, 0);
    mpLayout->findTextBox("T_perfect00_01")->setMessage(pMsg, 0xA8C, 1, 0);
    mpLayout->findTextBox("T_perfect01_01")->setMessage(pMsg, 0xA8C, 1, 0);
    mpLayout->findTextBox("T_perfect00_02")->setMessage(pMsg, 0xA8C, 1, 0);
    mpLayout->findTextBox("T_perfect01_02")->setMessage(pMsg, 0xA8C, 1, 0);
    if (pGame->GetPlayerNum() <= 1) {
        mpEffects[0] = new RPSysEffect("Cow_2d_perfect_1p_blue", 3, 0);
        mpEffects[1] = NULL;
    } else {
        mpEffects[0] = new RPSysEffect("Cow_2d_perfect_vs_blue", 3, 0);
        mpEffects[1] = new RPSysEffect("Cow_2d_perfect_vs_red", 3, 0);
    }
}

void RPCowPerfectMsg::OnStart() {
    for (int i = 0; i < 21; i++) {
        mpPanes[i]->SetVisible(false);
    }
    mpLayout->unbindAllAnmObj();
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[0], mpPanes[0], true);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[1], mpPanes[1], true);
    RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpPanes[2], true);
    for (int i = 0; i < 3; i++) {
        mpAnmObjs[i]->resetFrame();
    }
}

void RPCowPerfectMsg::OnStop() {}

void RPCowPerfectMsg::OnActivate() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpLayout->calc();
    s32 playerNum = pGame->GetPlayerNum();
    for (int i = 0; i < playerNum; i++) {
        if (mStates[i] == 1) {
            if (mpAnmObjs[GetElement(i, 0, 1, 2)]->isFinished()) {
                mpPanes[GetElement(i, 0, 1, 2)]->SetVisible(false);
                mpEffects[i]->fade();
                mStates[i] = 0;
            } else {
                nw4r::math::VEC3 pos;
                RP_Mtx34_getColumn(&pos, mpPanes[GetElement(i, 3, 4, 5)]->GetGlobalMtx(), 3);
                mpEffects[i]->setPos(pos);
                mpEffects[i]->update();
            }
        }
    }
}

void RPCowPerfectMsg::DrawScreen(u8 pass) {
    if (pass == 4) {
        mpLayout->draw();
    }
}
