/*
 * Test probes into the HOME Menu (docs/HOST_BUILD.md §34), boot only.
 *
 *   PortHomeProbe     the FRAME line's `home=` field while the HOME Menu is
 *                     open or opening: RPSysHomeMenuMgr's menu state, the
 *                     library's sequence (normal / Wii Remote settings /
 *                     dialog), its internal state and selected button, the
 *                     pane remote 0 points at (by name, the library's own
 *                     hit test), the sound RPSndHomeMenuArcMgr is playing,
 *                     and what the settings panel shows for remote 0
 *                     (battery level, speaker volume, rumble); and
 *                     RPSysSceneMgr's state (a soft reset, a return to the
 *                     Wii Menu); the ban icon's state (HOME while the menu
 *                     is disabled).
 *   PortInputHomeHook the input script's `hbm PANE`: where remote CHAN must
 *                     point for the library's hit test to find the centre of
 *                     the named pane (the layout's own pane rectangle and
 *                     global matrix, through the inverse of
 *                     HomeButton::update_controller's mapping).
 *
 * Reads only; nothing here changes game or library state. The classes keep
 * these fields private, so this file opens them up, as host_probe.cpp does.
 */
#include <cmath>
#include <cstdio>
#include <cstring>

#include <port/input.h>

#define private public
#define protected public
#include <Pack/RPAudio/RPSndHomeMenuArcMgr.h>
#include <Pack/RPKernel/RPSysHomeMenuMgr.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>
#include <homebuttonMiniLib/HBMBase.h>
#include <homebuttonMiniLib/HBMController.h>
#include <homebuttonMiniLib/HBMGUIManager.h>
#include <nw4r/lyt.h>
#undef private
#undef protected

extern "C" int PortHomeProbe(char* buf, int len);

namespace {

using homebutton::HomeButton;

HomeButton* Live() {
    RPSysHomeMenuMgr* mgr = RPSysHomeMenuMgr::instance();
    if (mgr == nullptr || !mgr->mIsInitialized) {
        return nullptr;
    }
    HomeButton* hb = HomeButton::getInstance();
    if (hb == nullptr || hb->mpLayout == nullptr || hb->mpPaneManager == nullptr) {
        return nullptr;
    }
    return hb;
}

nw4r::lyt::Pane* FindPane(HomeButton* hb, const char* name) {
    return hb->mpLayout->GetRootPane()->FindPaneByName(name, true);
}

// The pane a press of remote CHAN would go to: as gui::Manager::update, the last
// component (in layout order) that the library's hit test has pointed and that
// takes presses now; "-" for none
const char* Pointed(HomeButton* hb, int chan) {
    homebutton::gui::PaneManager* pm = hb->mpPaneManager;
    const char* name = "-";
    for (u32 i = 0; i < nw4r::ut::List_GetSize(&pm->mPaneToComponent); i++) {
        const homebutton::gui::PaneManager::PaneToComponent* it =
            static_cast<const homebutton::gui::PaneManager::PaneToComponent*>(
                nw4r::ut::List_GetNthConst(&pm->mPaneToComponent, i));
        if (it->mpComponent->mabPointed[chan] && it->mpComponent->isTriggerTarger()) {
            name = it->mpPane->GetName();
        }
    }
    return name;
}

} // namespace

extern "C" int PortHomeProbe(char* buf, int len) {
    RPSysHomeMenuMgr* mgr = RPSysHomeMenuMgr::instance();
    if (mgr == nullptr || !mgr->mIsInitialized) {
        return 0;
    }
    RPSndHomeMenuArcMgr* arc = RPSysHomeMenuMgr::spHomeMenuArcMgr;
    int sound = -1;
    if (arc != nullptr && arc->mSoundHandle.IsAttachedSound()) {
        sound = static_cast<int>(arc->mSoundHandle.GetId());
    }
    // RPSysSceneMgr's state: 0 normal, 1 guard, 2 soft reset, 3 return to menu, 4 shutdown
    RPSysSceneMgr* scenes = RPSysSceneMgr::instance();
    int sys = scenes != nullptr ? static_cast<int>(scenes->mState) : 0;
    // The ban icon (HOME pressed while the menu is disabled): 0 hidden, 1 fading in, 2 shown,
    // 3 fading out
    int ban = mgr->mpBanIcon != nullptr ? static_cast<int>(mgr->mpBanIcon->mState) : 0;
    if (!mgr->mIsActive && mgr->mMenuState == RPSysHomeMenuMgr::EMenuState_End && sound < 0 && sys == 0 &&
        ban == 0) {
        return 0;
    }
    HomeButton* hb = Live();
    if (hb == nullptr || !mgr->mIsActive) {
        std::snprintf(buf, len, "%d,%d,%d,-,-,-,-,%d,-,-,-,%d,%d", static_cast<int>(mgr->mMenuState),
                      mgr->mIsActive ? 1 : 0, mgr->mIsVisible ? 1 : 0, sound, sys, ban);
        return 1;
    }
    // MENU,ACTIVE,VISIBLE,SEQUENCE,STATE,SELECTED,POINTED,SOUND,BATTERY,VOLUME,RUMBLE,SCENEMGR,BAN
    std::snprintf(buf, len, "%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d", static_cast<int>(mgr->mMenuState),
                  mgr->mIsActive ? 1 : 0, mgr->mIsVisible ? 1 : 0, static_cast<int>(hb->mSequence), hb->mState,
                  static_cast<int>(hb->mSelectBtnNum), Pointed(hb, 0), sound,
                  static_cast<int>(hb->mWpadInfo[0].battery), hb->mVolumeNum, hb->mVibFlag ? 1 : 0, sys, ban);
    return 1;
}

extern "C" int PortInputHomeHook(int chan, const char* name, f32* x, f32* y) {
    RPSysHomeMenuMgr* mgr = RPSysHomeMenuMgr::instance();
    HomeButton* hb = Live();
    if (hb == nullptr || !mgr->mIsActive || chan < 0 || chan >= WPAD_MAX_CONTROLLERS) {
        return 0;
    }
    nw4r::lyt::Pane* pane = FindPane(hb, name);
    if (pane == nullptr) {
        return 0;
    }
    // PaneComponent::contain: the point (x, y) in layout space is inside when the
    // pane's inverse global matrix takes it into GetPaneRect. The pane's rectangle
    // through its global matrix ...
    nw4r::ut::Rect rect = pane->GetPaneRect(hb->mDrawInfo);
    const nw4r::math::MTX34& m = pane->GetGlobalMtx();
    f32 left = 1e30f, right = -1e30f, bottom = 1e30f, top = -1e30f;
    for (int i = 0; i < 4; i++) {
        f32 lx = (i & 1) ? rect.right : rect.left;
        f32 ly = (i & 2) ? rect.bottom : rect.top;
        f32 gx = m._00 * lx + m._01 * ly + m._03;
        f32 gy = m._10 * lx + m._11 * ly + m._13;
        left = std::fmin(left, gx);
        right = std::fmax(right, gx);
        bottom = std::fmin(bottom, gy);
        top = std::fmax(top, gy);
    }
    // ... is mapped from the pointer by HomeButton::update_controller
    // (x = pos.x * 304 * adjust.x, y = -pos.y * 228 * adjust.y with the adjust flag).
    // The bars' panes reach past the screen: aim at the centre of the part a
    // pointer can reach (pos within +-0.95).
    f32 sx = 608.0f / 2.0f, sy = 456.0f / 2.0f;
    if (hb->mAdjustFlag) {
        sx *= hb->mpHBInfo->adjust.x;
        sy *= hb->mpHBInfo->adjust.y;
    }
    const f32 reach = 0.95f;
    left = std::fmax(left, -reach * sx);
    right = std::fmin(right, reach * sx);
    bottom = std::fmax(bottom, -reach * sy);
    top = std::fmin(top, reach * sy);
    if (left > right || bottom > top) {
        return 0;
    }
    *x = (left + right) * 0.5f / sx;
    *y = -(bottom + top) * 0.5f / sy;
    return 1;
}
