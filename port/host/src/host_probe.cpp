/*
 * Test probes into the minigames' state (docs/HOST_BUILD.md §31), boot only.
 *
 *   PortGameProbe     the FRAME line's `game=` field: Shooting Range's game
 *                     state, round, player 0's score and hits.
 *   PortInputAimHook  the input script's `aim` command: where the pointer
 *                     should be to hit a live Shooting Range target, from the
 *                     game's own target positions and its own projection.
 *
 * Reads only; the probes never change game state. The game's classes keep
 * these fields private, so this file (and only this one) opens them up.
 */
#include <cmath>
#include <cstdio>
#include <cstring>

#include <port/input.h>

#define private public
#define protected public
#include <Pack/RPParty/RPDucScene/RPDucCamera.h>
#include <Pack/RPParty/RPDucScene/RPDucGame.h>
#include <Pack/RPParty/RPDucScene/RPDucTarget.h>
#include <Pack/RPParty/RPDucScene/RPDucTargetMgr.h>
#undef private
#undef protected

extern "C" int PortGameProbe(char* buf, int len);

namespace {

// The target being aimed at, kept until it goes (no jumping between targets)
const RPDucTarget* sAimTarget = nullptr;

bool Aimable(const RPDucTarget* t) {
    return t->mAge >= 0 && t->unkA4 && t->mHitOrder[0] < 0;
}

bool ScreenPos(const RPDucTarget* t, f32* x, f32* y) {
    nw4r::math::VEC3 c(t->mPos.x + t->mHitOffset.x, t->mPos.y + t->mHitOffset.y,
                       t->mPos.z + t->mHitOffset.z);
    nw4r::math::VEC2 s;
    RPDucCamera::sInstance->ProjectToScreen(&s, c);
    if (!(std::fabs(s.x) < 0.95f && std::fabs(s.y) < 0.95f)) {
        return false;
    }
    // The projection's y is up, the pointer's down
    *x = s.x;
    *y = -s.y;
    return true;
}

} // namespace

extern "C" int PortGameProbe(char* buf, int len) {
    RPDucGame* game = RPDucGame::sInstance;
    RPDucScoreMgr* score = RPDucScoreMgr::sInstance;
    if (game == nullptr || score == nullptr) {
        return 0;
    }
    std::snprintf(buf, len, "duc:%d,%d,%d,%d", static_cast<int>(game->mState),
                  static_cast<int>(game->mRound), static_cast<int>(score->mScores[0]),
                  static_cast<int>(score->mHits[0]));
    return 1;
}

extern "C" int PortInputAimHook(int chan, f32* x, f32* y) {
    RPDucTargetRegistry* reg = RPDucTargetRegistry::sInstance;
    if (chan != 0 || reg == nullptr || RPDucCamera::sInstance == nullptr ||
        RPDucGame::sInstance == nullptr || !RPDucGame::sInstance->mInPlay) {
        sAimTarget = nullptr;
        return 0;
    }
    const RPDucTarget* best = nullptr;
    for (u32 i = 0; i < reg->mMgrNum; i++) {
        RPDucTargetMgr* mgr = reg->mpMgrs[i];
        if (mgr->mType == 6) {  // the Miis of the last round: not targets
            continue;
        }
        for (s32 j = 0; j < mgr->mPoolNum; j++) {
            const RPDucTarget* t = mgr->mppPool[j];
            f32 tx, ty;
            if (!Aimable(t) || !ScreenPos(t, &tx, &ty)) {
                continue;
            }
            if (t == sAimTarget) {
                *x = tx;
                *y = ty;
                return 1;
            }
            if (best == nullptr) {
                best = t;
            }
        }
    }
    sAimTarget = best;
    return best != nullptr && ScreenPos(best, x, y);
}
