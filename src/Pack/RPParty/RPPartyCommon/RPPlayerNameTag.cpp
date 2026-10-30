#include <Pack/RPParty/RPPartyCommon/RPPlayerNameTag.h>

#include <Pack/RPParty/RPPartyCommon/RPPlayerConfirm.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysFrameCtrl.h>
#include <Pack/RPKernel/RPSysLytAnmObj.h>
#include <Pack/RPKernel/RPSysLytPicture.h>
#include <Pack/RPKernel/RPSysLytTextBox.h>
#include <egg/core/eggController.h>
#include <egg/math/eggVector.h>
#if defined(PORT_HOST)
#include <Pack/RPParty/RPPartyCommon/RPKokeshiMgr.h>
#endif

extern "C" void Lyt_setPaneColorFromTable(nw4r::lyt::Pane* pPane, u32 player);

namespace {

inline RPPlayerConfirm::Params* GetConfirmParams() {
    return &RPPlayerConfirm::sInstance->mParams;
}

} // namespace

#pragma auto_inline off
RPPlayerNameTag::RPPlayerNameTag(s32 player) {
    mpStateMachine = new RPStateMachine<RPPlayerNameTag>(this, 2);

    typedef void (RPPlayerNameTag::*StateFunc)();
    StateFunc stateFuncs[2][2] = {
        {NULL, NULL},
        {&RPPlayerNameTag::ShownEnter, &RPPlayerNameTag::ShownCalc},
    };

    for (s32 i = 0; i < 2; i++) {
        mpStateMachine->SetStateFuncs(stateFuncs[i][0], stateFuncs[i][1], i);
    }

    mPlayer = player;
}
#pragma auto_inline on

void RPPlayerNameTag::Load(EGG::Archive* pArchive) {
    AttachArcFile(pArchive, "playerSelect.arc", mpResAccessor);
    mpLayout = RPSysLayout::create(NULL, mpResAccessor, "playerSelect_03.brlyt");

    for (s32 i = 0; i < 4; i++) {
        mpLampAnmObjs[i] =
            mpLayout->createAnmObj(NULL, mpResAccessor,
                                   "playerSelect_03_onOffLamp_00.brlan");
    }

    mpNamePane = mpLayout->findPane("N_name_00");
    unk30 = mpLayout->findPane("N_3d_00");
    Lyt_setPaneColorFromTable(mpLayout->findPicture("P_bg_01"),
                              static_cast<u32>(mPlayer));
    Lyt_setPaneColorFromTable(mpLayout->findPicture("P_bgLine_00"),
                              static_cast<u32>(mPlayer));

    s32 player = mPlayer;
    RPSysMessage* message = RPPlayerConfirm::sInstance->mpMessage;
    if (player < 2) {
        mpLayout->findTextBox("T_P_00")->setMessage(
            message,
            static_cast<u32>(player) + 0x276, 0, 0);
    }
}

void RPPlayerNameTag::Reset() {
    mpStateMachine->Reset(0);

    for (s32 i = 0; i < 4; i++) {
        mpLampAnmObjs[i]->getFrameCtrl()->reset();
        mpLampAnmObjs[i]->getFrameCtrl()->mIsEnabled = 0;
        mpLampAnmObjs[i]->setFrame(0.0f);
    }

    mpLayout->unbindAllAnmObj();
    mNamePosX = EGG::Vector2f::zero.x;
    mNamePosY = EGG::Vector2f::zero.y;
}

void RPPlayerNameTag::Calculate() {
#pragma auto_inline off
    mpStateMachine->Calculate();
#pragma auto_inline on

    if (mpStateMachine->GetState() != 0) {
        mpNamePane->SetTranslate(nw4r::math::VEC3(
            mNamePosX, mNamePosY, 0.0f));
        mpLayout->calc();
    }
}

void RPPlayerNameTag::ShownEnter() {}

void RPPlayerNameTag::ShownCalc() {}

void RPPlayerNameTag::Draw() {
    if (mpStateMachine->GetState() == 0) {
        return;
    }

    mpLayout->draw();
}

void RPPlayerNameTag::Show(bool show) {
    RPPlayerConfirm::Params* pParams;

    if (!show) {
        mpStateMachine->ChangeState(0);
        return;
    }

    if (!mpStateMachine->IsStateSettled(0)) {
        return;
    }

    pParams = GetConfirmParams();
    const char* lampPanes[4] = {
        "P_lamp_00",
        "P_lamp_01",
        "P_lamp_02",
        "P_lamp_03",
    };
    for (s32 i = 0; i < 4; i++) {
        nw4r::lyt::Pane* pane = mpLayout->findPane(lampPanes[i]);
        pane->UnbindAllAnimation(true);
        RPSysLayout::forceBindAnmObjToPane(mpLampAnmObjs[i], pane, true);
        mpLampAnmObjs[i]->restart();

        EGG::CoreController* controller =
            EGG::CoreControllerMgr::instance()->getNthController(
            pParams->controllers[mPlayer]);
        if (i == controller->getChannelID()) {
            mpLampAnmObjs[i]->setRate(1.0f);
        } else {
            mpLampAnmObjs[i]->setRate(0.0f);
        }
    }

    // The Mii name, RPSysAvatar::mAdditionalInfo (0x68) of the head. Not reached through the
    // class in the Wii build: its header chain (RPSysKokeshiGenInfo.h -> RPSysKokeshiManager.h)
    // adds g3d header data (GetAnmPlayPolicy's policyTable, fifo::cm2hw) that this unit does not
    // have. The host build has no such constraint and the offset differs there.
    mpLayout->findTextBox("T_name_00")
        ->setMessage(RPPlayerConfirm::sInstance->mpMessage, 0x280, 0,
#if defined(PORT_HOST)
                     pParams->pHeads[mPlayer]->mAdditionalInfo.name);
#else
                     reinterpret_cast<const wchar_t*>(reinterpret_cast<u32>(pParams->pHeads[mPlayer]) + 0x68));
#endif

    if (RPPlayerConfirm::sInstance->mNoABPress == 0) {
        mpLayout->findPane("P_bg_00")->SetVisible(true);
        mpLayout->findPane("N_lamp_00")->SetVisible(true);
        mpLayout->findPane("T_P_00")->SetVisible(false);
    } else {
        mpLayout->findPane("P_bg_00")->SetVisible(false);
        mpLayout->findPane("N_lamp_00")->SetVisible(false);
        mpLayout->findPane("T_P_00")->SetVisible(true);
    }

    mpStateMachine->ChangeState(1);
}

// Keep the state-machine copies in their original address order.
#pragma defer_codegen off
#pragma auto_inline off
template RPStateMachine<RPPlayerNameTag>::RPStateMachine(RPPlayerNameTag*, s32);
template void RPStateMachine<RPPlayerNameTag>::SetStateFuncs(
    const StateFunc&, const StateFunc&, s32);
template void RPStateMachine<RPPlayerNameTag>::Reset(s32);
template void RPStateMachine<RPPlayerNameTag>::ChangeState(s32);
template void RPStateMachine<RPPlayerNameTag>::Calculate();
template void RPStateMachine<RPPlayerNameTag>::CallExit();
template void RPStateMachine<RPPlayerNameTag>::CallCalc();
template void RPStateMachine<RPPlayerNameTag>::CallEnter();
template bool RPStateMachine<RPPlayerNameTag>::IsStateSettled(s32) const;
