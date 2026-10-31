#include <Pack/RPParty/RPCowScene/RPCowHud_8.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysLytAnmObj.h>
#include <Pack/RPKernel/RPSysLytTextBox.h>
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
inline nw4r::math::VEC3 GetPanePos(nw4r::lyt::Pane* pPane) {
    nw4r::math::VEC3 pos;
    RP_Mtx34_getColumn(&pos, pPane->GetGlobalMtx(), 3);
    return pos;
}
}

void RPCowGoalMsg::Show(s32 player) {
    mpPanes[0]->SetVisible(true);
    if (player != 0) {
        mpPanes[1]->SetVisible(true);
        mpLayout->unbindAnmObj(mpAnmObjs[0]);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[0], mpPanes[0], true);
        mpAnmObjs[0]->restart();
        mTimer = 30;
        mState = 2;
    } else {
        mpPanes[1]->SetVisible(false);
        mpLayout->unbindAnmObj(mpAnmObjs[0]);
        RPSysLayout::bindAnmObjToPane(mpAnmObjs[0], mpPanes[0], true);
        mpAnmObjs[0]->restart();
        mTimer = 180;
        mState = 1;
    }
}

RPCowGoalMsg::RPCowGoalMsg() {}

RPCowGoalMsg::~RPCowGoalMsg() {}

void RPCowGoalMsg::LoadResource() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpLayout = RPSysLayout::create(NULL, pGame->GetLytAccessor(), "cowGoal_00.brlyt");
    const char* anm[] = {"cowGoal_00_in_00.brlan", "cowGoal_00_out_00.brlan",
                         "cowGoal_00_inComplete_00.brlan"};
    for (int i = 0; i < 3; i++) {
        mpAnmObjs[i] = mpLayout->createAnmObj(NULL, pGame->GetLytAccessor(), anm[i]);
    }
    const char* pane[] = {"N_cow_00", "N_complete_00"};
    for (int i = 0; i < 2; i++) {
        mpPanes[i] = mpLayout->findPane(pane[i]);
    }
    RPSysMessage* pMsg = RPCls803572E0::GetInstance()->GetMessage();
    mpLayout->findTextBox("T_goal_00")->setMessage(pMsg, 0xA93, 0, 0);
    mpLayout->findTextBox("T_goal_01")->setMessage(pMsg, 0xA93, 0, 0);
    mpLayout->findTextBox("T_complete_00")->setMessage(pMsg, 0xA90, 0, 0);
    mpLayout->findTextBox("T_complete_01")->setMessage(pMsg, 0xA90, 0, 0);
    mpEffect = new RPSysEffect("Cow_2d_complete", 3, 0);
}

void RPCowGoalMsg::OnStart() {
    mpPanes[0]->SetVisible(false);
    mpPanes[1]->SetVisible(false);
    mTimer = 0;
    mState = 0;
    mpLayout->unbindAllAnmObj();
    for (int i = 0; i < 3; i++) {
        mpAnmObjs[i]->resetFrame();
    }
}

void RPCowGoalMsg::OnStop() {}

void RPCowGoalMsg::OnActivate() {
    switch (mState) {
    case 2:
        if (--mTimer <= 0) {
            mpPanes[0]->SetVisible(true);
            mpLayout->unbindAnmObj(mpAnmObjs[2]);
            RPSysLayout::bindAnmObjToPane(mpAnmObjs[2], mpPanes[1], true);
            mpAnmObjs[2]->restart();
            mTimer = 150;
            mState = 1;
            RPSndAudioMgr::instance()->startSystemSe(0x98u);
            mpEffect->create();
            mpEffect->setPos(GetPanePos(mpPanes[1]));
            mpEffect->update();
        }
        break;
    case 1:
        if (mpPanes[1]->IsVisible()) {
            nw4r::math::VEC3 pos;
            RP_Mtx34_getColumn(&pos, mpPanes[1]->GetGlobalMtx(), 3);
            mpEffect->setPos(pos);
            mpEffect->update();
        }
        if (--mTimer <= 0) {
            mpPanes[0]->SetVisible(true);
            mpLayout->unbindAnmObj(mpAnmObjs[1]);
            RPSysLayout::bindAnmObjToPane(mpAnmObjs[1], mpPanes[0], true);
            mpAnmObjs[1]->restart();
            mTimer = 0;
            mState = 3;
        }
        break;
    case 3:
        if (mpAnmObjs[1]->isFinished()) {
            if (mpPanes[1]->IsVisible()) {
                mpEffect->fade();
            }
            mpPanes[0]->SetVisible(false);
            mState = 0;
        }
        break;
    }
    mpLayout->calc();
}

void RPCowGoalMsg::DrawScreen(u8 pass) {
    if (pass == 4) {
        mpLayout->draw();
    }
}
