#include <Pack/RPKernel.h>
// Ported from doldecomp/ogws (CC0) src/Pack/RPSystem/RPSysPauseMgr.cpp @ f5a1bb5e; Wii Play changes marked "Wii Play:".
// Wii Play: the manager is a rewrite (Sys_Pause ledger); only draw is shared with Wii Sports.
#include <Pack/RPAudio.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPSingleton.h>
#include <Pack/RPSystem.h>

// Wii Play: DestroyInstance is not in the binary (unused, dead-stripped)
#pragma push
#pragma always_inline on
RP_SINGLETON_IMPL_EX_NO_DESTROY(RPSysPauseMgr);

// Wii Play: only inlined into CreateInstance
// NONMATCHING-DEBT: asm volatile (three blocks here and in init): `li clear, 0` keeps the zero
// for mPause/mUNK_0x0C in its own register, and `addi second, this, 0xF0` gives the second Work
// block the base this+0xF0 with displacements 0x10-0xFC. Natural C folds it: a nested
// `mWork[i].mValues[j] = 0.0f` loop stores everything off r3 (61 words in init); a Work& /
// Work* / f32* per block uses base this+0x100 and orders four stores differently (5 words).
// Tried: two plain loops, nested i/j loops (int/u32, for/do), j-outer, walking Work pointer.
// 2026-10-29: `f32* p = mWork[i].mValues` or `Work& work = mWork[i]` per outer iteration gives
// 121/126 (base this+0x100, disp 0x0-0xEC); a Work::clear() member inline and a flat 120-store loop
// (unrolled by 40, a ctr loop) don't help. The base this+0xF0 keeps mWork's 0x10 in the
// displacement, as from an induction pointer this + i*sizeof(Work) advanced by the unroller. No
// natural expression found that keeps that sum unfolded.
inline RPSysPauseMgr::RPSysPauseMgr(EGG::Heap* pHeap)
    : mpParentHeap(pHeap), mPause(FALSE) {
    mPauseMenu = new RPSysPauseMenu();
    register u32 clear;
#if defined(PORT_HOST)
    clear = 0;
#else
    asm volatile {
        li clear, 0
    }
#endif
    register f32 zero = 0.0f;
    register RPSysPauseMgr* base = this;
    register RPSysPauseMgr* second;
#if defined(PORT_HOST)
    // this + 0xF0: mWork[1] seen as a second object's mWork[0] (the Wii
    // offset; pointer-sized on the host).
    second = reinterpret_cast<RPSysPauseMgr*>(reinterpret_cast<u8*>(base) + 0xF0);
#else
    asm volatile {
        addi second, base, 0xF0
    }
#endif
    mPause = clear;
    mUNK_0x0C = clear;
    for (int j = 0; j < 60; ++j) {
        mWork[0].mValues[j] = zero;
    }
    for (int j = 0; j < 60; ++j) {
        second->mWork[0].mValues[j] = zero;
    }
}
#pragma pop

void RPSysPauseMgr::loadMenu() {
    mPauseMenu->load();
}

void RPSysPauseMgr::init() {
    register f32 zero = 0.0f;
    mPause = FALSE;
    register RPSysPauseMgr* base = this;
    register RPSysPauseMgr* second;
#if defined(PORT_HOST)
    // this + 0xF0: mWork[1] seen as a second object's mWork[0] (the Wii
    // offset; pointer-sized on the host).
    second = reinterpret_cast<RPSysPauseMgr*>(reinterpret_cast<u8*>(base) + 0xF0);
#else
    asm volatile {
        addi second, base, 0xF0
    }
#endif
    mUNK_0x0C = 0;

    for (int j = 0; j < 60; ++j) {
        mWork[0].mValues[j] = zero;
    }
    for (int j = 0; j < 60; ++j) {
        second->mWork[0].mValues[j] = zero;
    }
}

void RPSysPauseMgr::calc() {
    if (isPause()) {
        if (!RP_GET_INSTANCE(RPSysSceneMgr)
                 ->getCurrentSceneRP()
                 ->isSceneChangeEnable()) {
            return;
        }

        switch (mPauseMenu->calc()) {
        case 0: {
            RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneRP()->pauseCallBack(false);
            RP_GET_INSTANCE(RPSndAudioMgr)->exitPause();
            mPause &= ~1;
            break;
        }

        case 1: {
            RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneRP()->pauseCallBack(false);
            RP_GET_INSTANCE(RPSndAudioMgr)->exitPause();

            s32 id = RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneID();
            if (id == RPSysSceneCreator::ESceneID_RPWlyScene) {
                RPSceneInfo::spInstance->ClearUnk1C();
                RP_GET_INSTANCE(RPSysSceneCreator)->changeSceneAfterFade(-1, true);
            } else {
                RP_GET_INSTANCE(RPSysSceneCreator)->changeSceneAfterFade(id, false);
            }
            break;
        }

        case 2: {
            RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneRP()->pauseCallBack(false);
            RP_GET_INSTANCE(RPSndAudioMgr)->exitPause();

            if (RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneID() ==
                RPSysSceneCreator::ESceneID_RPWlyScene) {
                RPSceneInfo::spInstance->ClearUnk1C();
            }

            RP_GET_INSTANCE(RPSysSceneCreator)
                ->changeSceneAfterFade(RPSysSceneCreator::ESceneID_RPPartyMainScene,
                                       false);
            break;
        }
        }
    } else {
        checkStart();
    }
}

void RPSysPauseMgr::draw() {
    if (isPause()) {
        mPauseMenu->draw();
    }
}

void RPSysPauseMgr::checkStart() {
    if (!RP_GET_INSTANCE(RPSysSceneMgr)
             ->getCurrentSceneRP()
             ->isSceneChangeEnable()) {
        return;
    }

    s32 player = -1;
    s32 chan = -1;

    for (s32 i = 0; i < RP_GET_INSTANCE(RPSysPlayerMgr)->getPlayerNum(); i++) {
        s32 ch = RP_GET_INSTANCE(RPSysPlayerMgr)->getChannel(i);

        if (RP_GET_CORE_CTRL(ch)->isCoreBaseDevice() &&
            RP_GET_CORE_CTRL(ch)->downTrigger(WPAD_BUTTON_PLUS)) {
            player = i;
            chan = ch;
            break;
        }
    }

    if (player < 0) {
        return;
    }

    if (chan < 0) {
        return;
    }

    if (RP_GET_INSTANCE(RPSysSceneCreator)
            ->getExitType(RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneID()) !=
        RPSysSceneCreator::EExitType_Standard) {
        return;
    }

    bool enable = true;
    RPCommon* pCommon = RPCommon::GetInstance();
    if (pCommon != NULL && !pCommon->IsPauseEnable()) {
        enable = false;
    }

    if (!enable) {
        return;
    }

    RP_GET_INSTANCE(RPSysSceneMgr)->getCurrentSceneRP()->pauseCallBack(true);
    RPSysCoreController::stopMotorAll();
    RP_GET_INSTANCE(RPSndAudioMgr)->enterPause();

    mPause |= 1;
    mPauseMenu->open();
    mPauseMenu->setPlayer(player);

    RP_GET_INSTANCE(RPSndAudioMgr)
        ->startPauseSe("RP_COM_GAME_SE_PAUSE_01_CTSP",
                       "RP_COM_GAME_SE_PAUSE_01_TVSP", true, player);
}

RPSysPauseMgr::~RPSysPauseMgr() {
    delete mPauseMenu;
}
