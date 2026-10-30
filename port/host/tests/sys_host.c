/**
 * Entries sys_harness links but never reaches (port/docs/HOST_BUILD.md §22).
 *
 * RPSysSystem, RPSysSceneCreator and RPSysSaveDataMgr are linked whole; with
 * --gc-sections only what their kept code references remains: the scene
 * classes RPSysSceneCreator::create builds (it is a virtual function, so its
 * vtable keeps it), the system window and controller calls of the save
 * manager's error paths and of RPSysSystem's reset callbacks, and the VI calls
 * of RPSysSystem's render-mode wait. Each function aborts with its name if
 * called; the data symbols are zero (no instance, an empty vtable that nothing
 * uses because the scene constructors abort first). VFIsAvailable answers "no
 * virtual file system" (RPSysSaveDataMgr's constructor then skips VFInitEx).
 */

#include <types.h>

#include <stdio.h>
#include <stdlib.h>

static void Hit(const char* name) {
    fprintf(stderr, "sys_harness: unexpected call to %s\n", name);
    abort();
}

#define STUB(name)                                                             \
    void name(void);                                                           \
    void name(void) {                                                          \
        Hit(#name);                                                            \
    }
#define STUB_MANGLED(id, mangled, readable)                                    \
    void sys_stub_##id(void) __asm__(mangled);                                 \
    void sys_stub_##id(void) {                                                 \
        Hit(readable);                                                         \
    }
#define DATA_MANGLED(id, mangled) void* sys_data_##id[16] __asm__(mangled)

STUB(OSReturnToMenu)
STUB(OSShutdownSystem)
STUB(VFInitEx)
STUB(VIFlush)
STUB(VISetBlack)

s32 VFIsAvailable(void);
s32 VFIsAvailable(void) {
    return 0xB001; /* VF_ERROR_B001: not available */
}

STUB_MANGLED(1, "_ZN10RPSysScene13setChildSceneEv", "RPSysScene::setChildScene")
STUB_MANGLED(2, "_ZN10RPSysScene17enableLoadMessageEv", "RPSysScene::enableLoadMessage")
STUB_MANGLED(3, "_ZN10RPSysScene17setCreatorSceneIDEi", "RPSysScene::setCreatorSceneID")
STUB_MANGLED(4, "_ZN10RPSysSceneC2Ev", "RPSysScene::RPSysScene")
STUB_MANGLED(5, "_ZN12RPActorSceneC2Ev", "RPActorScene::RPActorScene")
STUB_MANGLED(6, "_ZN13RPSysSceneMgr12returnToMenuEb", "RPSysSceneMgr::returnToMenu")
STUB_MANGLED(7, "_ZN13RPSysSceneMgr24isApplicationEndReservedEv",
             "RPSysSceneMgr::isApplicationEndReserved")
STUB_MANGLED(8, "_ZN17RPSysSystemWinMgr10setCursorsEv", "RPSysSystemWinMgr::setCursors")
STUB_MANGLED(9, "_ZN17RPSysSystemWinMgr15setSystemWindowENS_13E_WINDOW_TYPEEjP12RPSysMessageiz",
             "RPSysSystemWinMgr::setSystemWindow")
STUB_MANGLED(10, "_ZN19RPSysCoreController12stopMotorAllEv", "RPSysCoreController::stopMotorAll")
STUB_MANGLED(11, "_ZN19RPSysCoreController14shutdownSystemEb",
             "RPSysCoreController::shutdownSystem")
STUB_MANGLED(12, "_ZNK13RPSysSceneMgr13isNormalStateEv", "RPSysSceneMgr::isNormalState")
STUB_MANGLED(13, "_ZNK13RPSysSceneMgr18isShutDownReservedEv", "RPSysSceneMgr::isShutDownReserved")

DATA_MANGLED(1, "_ZN13RPSndAudioMgr10spInstanceE");
DATA_MANGLED(2, "_ZN17RPSysSystemWinMgr10spInstanceE");
DATA_MANGLED(3, "_ZN3EGG17CoreControllerMgr9sInstanceE");
DATA_MANGLED(4, "_ZTV10RPBilScene");
DATA_MANGLED(5, "_ZTV10RPBomScene");
DATA_MANGLED(6, "_ZTV10RPCowScene");
DATA_MANGLED(7, "_ZTV10RPDucScene");
DATA_MANGLED(8, "_ZTV10RPFshScene");
DATA_MANGLED(9, "_ZTV10RPHkyScene");
DATA_MANGLED(10, "_ZTV10RPPnpScene");
DATA_MANGLED(11, "_ZTV10RPTnkScene");
DATA_MANGLED(12, "_ZTV10RPWlyScene");
DATA_MANGLED(13, "_ZTV13RPAvatarScene");
DATA_MANGLED(14, "_ZTV14RPSysBootScene");
DATA_MANGLED(15, "_ZTV15RPCommonMessage");
DATA_MANGLED(16, "_ZTV16RPPartyRootScene");
DATA_MANGLED(17, "_ZTV22RPSysNunchukCheckScene");
DATA_MANGLED(18, "_ZTV22RPSysPlayerSelectScene");
