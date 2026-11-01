/*
 * Test probes into the minigames' state (docs/HOST_BUILD.md §31, §32), boot only.
 *
 *   PortGameProbe     the FRAME line's `game=` field, for the scene running:
 *                     Shooting Range (duc:) its game state, round, player 0's
 *                     score and hits; Laser Hockey (hky:) its game state, the
 *                     time left, both scores, the puck's position and its
 *                     distance from player 0's paddle.
 *   PortInputAimHook  the input script's `aim` command: where the pointer
 *                     should be to hit a live Shooting Range target (the
 *                     game's own target positions and projection), or in
 *                     Laser Hockey to put player 0's paddle on the puck (the
 *                     game's own screen-to-rink mapping, solved for the puck).
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
#include <Pack/RPParty/RPHkyScene/RPHkyField.h>
#include <Pack/RPParty/RPHkyScene/RPHkyGame.h>
#include <Pack/RPParty/RPHkyScene/RPHkyPaddle.h>
#include <Pack/RPParty/RPHkyScene/RPHkyPuck.h>
#include <Pack/RPGraphics/RPGrpRenderer.h>
#include <Pack/RPSystem/RPSysPlayerMgr.h>
#include <Pack/RPSystem/RPSysSceneCreator.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>
#include <egg/gfxe/eggScreen.h>
#undef private
#undef protected

extern "C" int PortGameProbe(char* buf, int len);
extern "C" int PortGameProbeMore(char* buf, int len); // host_probe_games.cpp

namespace {

// The target each remote aims at, kept until it goes (no jumping between targets)
const RPDucTarget* sAimTarget[4] = {nullptr, nullptr, nullptr, nullptr};

// Live and not yet hit by this player
bool Aimable(const RPDucTarget* t, int player) {
    return t->mAge >= 0 && t->unkA4 && t->mHitOrder[player] < 0;
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

s32 CurrentScene() {
    RPSysSceneMgr* mgr = RPSysSceneMgr::instance();
    return mgr != nullptr ? mgr->getCurrentSceneID() : -1;
}

/* ---- Laser Hockey ---- */

bool HkyLive() {
    RPHkyGame* game = RPHkyGame::sInstance;
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPHkyScene && game != nullptr &&
           game->mpPuck != nullptr && game->mpPaddles[0] != nullptr && game->mpCamera != nullptr;
}

// A paddle's target for a pointer position: RPHkyPaddle::UpdateControl's mapping (paddle 1,
// player 2 in a two-player game, takes the +x half)
bool HkyPointerToRink(int paddle, f32 px, f32 py, nw4r::math::VEC3* out) {
    const RPGrpScreen* screen = RPGrpRenderer::GetCurrent()->GetView(0)->GetScreen();
    f32 x = px * static_cast<f32>(static_cast<u16>(EGG::Screen::GetSizeXMax() / 2));
    f32 y = -py * static_cast<f32>(static_cast<u16>(EGG::Screen::GetSizeYMax() / 2));
    nw4r::math::VEC3 planePoint(0.0f, 0.0f, 0.0f);
    nw4r::math::VEC3 planeNormal(0.0f, 1.0f, 0.0f);
    nw4r::math::VEC3 t;
    if (!RPHkyGame::sInstance->mpCamera->GetScreenToPlane(&t, planePoint, planeNormal, screen, x, y)) {
        return false;
    }
    f32 offset = RPHkyField::sWidth / 2.0f - (RPHkyField::cCorner - RPHkyField::cGoalInset);
    t.x = paddle == 0 ? t.x * 0.5f - offset : t.x * 0.5f + offset;
    t.z *= RPHkyPaddle::sPointerDepthScale[0];
    *out = t;
    return true;
}

// The pointer position whose rink target is (gx, gz): Newton's method on the game's mapping
f32 sHkyPx[2] = {0.0f, 0.0f}, sHkyPy[2] = {0.0f, 0.0f};

bool HkyAim(int paddle, f32 gx, f32 gz, f32* x, f32* y) {
    f32 px = sHkyPx[paddle], py = sHkyPy[paddle];
    for (int it = 0; it < 8; it++) {
        nw4r::math::VEC3 a, bx, by;
        const f32 h = 0.01f;
        if (!HkyPointerToRink(paddle, px, py, &a) || !HkyPointerToRink(paddle, px + h, py, &bx) ||
            !HkyPointerToRink(paddle, px, py + h, &by)) {
            return false;
        }
        f32 ex = gx - a.x, ez = gz - a.z;
        if (std::fabs(ex) < 0.05f && std::fabs(ez) < 0.05f) {
            break;
        }
        f32 j00 = (bx.x - a.x) / h, j01 = (by.x - a.x) / h;
        f32 j10 = (bx.z - a.z) / h, j11 = (by.z - a.z) / h;
        f32 det = j00 * j11 - j01 * j10;
        if (std::fabs(det) < 1e-6f) {
            return false;
        }
        px += (j11 * ex - j01 * ez) / det;
        py += (-j10 * ex + j00 * ez) / det;
        px = px < -1.5f ? -1.5f : px > 1.5f ? 1.5f : px;
        py = py < -1.5f ? -1.5f : py > 1.5f ? 1.5f : py;
    }
    sHkyPx[paddle] = px;
    sHkyPy[paddle] = py;
    *x = px;
    *y = py;
    return true;
}

int HkyAimHook(int chan, f32* x, f32* y) {
    RPHkyGame* game = RPHkyGame::sInstance;
    if (game->mState != 1 || game->mpPaddles[chan] == nullptr) {  // play
        return 0;
    }
    // Strike through the puck: aim a little past its centre, away from the paddle's own goal
    const nw4r::math::VEC3& puck = game->mpPuck->GetPos();
    nw4r::math::VEC3 paddle = game->mpPaddles[chan]->GetPos();
    f32 dx = puck.x - paddle.x, dz = puck.z - paddle.z;
    f32 d = std::sqrt(dx * dx + dz * dz);
    f32 gx = puck.x, gz = puck.z;
    if (game->mIsMultiPlayer) {
        // two players (each paddle stays on its own half): each goal is given away on purpose by
        // one side (player 2 two times in three), which knocks the puck into its own goal when it
        // is on its half and otherwise waits by the side wall; the other side strikes the puck at
        // the other goal from behind it (going round it) and otherwise waits in front of its goal.
        // So the first-to-8 game ends, both paddles playing the puck
        int goals = game->mpPaddles[0]->mScore + game->mpPaddles[1]->mScore;
        bool giver = chan == (goals % 3 == 2 ? 0 : 1);
        f32 sgn = chan == 0 ? 1.0f : -1.0f, r = RPHkyPuck::sRadius;
        if (giver) {
            sgn = -sgn;  // toward its own goal
        }
        if ((chan == 0 ? puck.x > 0.0f : puck.x < 0.0f)) {
            return giver ? HkyAim(chan, chan == 0 ? -200.0f : 200.0f, 170.0f, x, y)
                         : HkyAim(chan, chan == 0 ? -150.0f : 150.0f, puck.z * 0.5f, x, y);
        }
        if ((paddle.x - puck.x) * sgn > -r) {
            return HkyAim(chan, puck.x - sgn * 3.0f * r, puck.z + (paddle.z > puck.z ? 3.0f : -3.0f) * r, x, y);
        }
    }
    if (d > 1.0f) {
        gx += dx / d * RPHkyPuck::sRadius;
        gz += dz / d * RPHkyPuck::sRadius;
    }
    return HkyAim(chan, gx, gz, x, y);
}

int HkyProbe(char* buf, int len) {
    RPHkyGame* game = RPHkyGame::sInstance;
    const nw4r::math::VEC3& puck = game->mpPuck->GetPos();
    nw4r::math::VEC3 paddle = game->mpPaddles[0]->GetPos();
    f32 dx = puck.x - paddle.x, dz = puck.z - paddle.z;
    std::snprintf(buf, len, "hky:%d,%d,%d,%d,%d,%d,%d", static_cast<int>(game->mState),
                  static_cast<int>(game->mTimer), static_cast<int>(game->mpPaddles[0]->mScore),
                  static_cast<int>(game->mpPaddles[1] != nullptr ? game->mpPaddles[1]->mScore : 0),
                  static_cast<int>(std::floor(puck.x)), static_cast<int>(std::floor(puck.z)),
                  static_cast<int>(std::floor(std::sqrt(dx * dx + dz * dz))));
    if (game->mIsMultiPlayer && game->mpPaddles[1] != nullptr) {
        // two players: paddle 1's position and its distance from the puck
        int n = static_cast<int>(std::strlen(buf));
        nw4r::math::VEC3 p1 = game->mpPaddles[1]->GetPos();
        f32 ex = puck.x - p1.x, ez = puck.z - p1.z;
        std::snprintf(buf + n, len - n, ",%d,%d,%d", static_cast<int>(std::floor(p1.x)),
                      static_cast<int>(std::floor(p1.z)), static_cast<int>(std::floor(std::sqrt(ex * ex + ez * ez))));
    }
    return 1;
}

} // namespace

extern "C" int PortGameProbe(char* buf, int len) {
    if (HkyLive()) {
        return HkyProbe(buf, len);
    }
    if (CurrentScene() != RPSysSceneCreator::ESceneID_RPDucScene) {
        return PortGameProbeMore(buf, len);
    }
    RPDucGame* game = RPDucGame::sInstance;
    RPDucScoreMgr* score = RPDucScoreMgr::sInstance;
    if (game == nullptr || score == nullptr) {
        return 0;
    }
    int n = std::snprintf(buf, len, "duc:%d,%d,%d,%d", static_cast<int>(game->mState),
                          static_cast<int>(game->mRound), static_cast<int>(score->mScores[0]),
                          static_cast<int>(score->mHits[0]));
    if (RPSysPlayerMgr::instance() != nullptr && RPSysPlayerMgr::instance()->getPlayerNum() >= 2 && n > 0 &&
        n < len) {
        // two players: player 2's score and hits
        std::snprintf(buf + n, len - n, ",%d,%d", static_cast<int>(score->mScores[1]),
                      static_cast<int>(score->mHits[1]));
    }
    return 1;
}

extern "C" int PortInputAimHook(int chan, f32* x, f32* y) {
    if (HkyLive()) {
        // remote 2 drives paddle 1 only in a two-player game (else it is the CPU's)
        return chan == 0 || (chan == 1 && RPHkyGame::sInstance->mIsMultiPlayer) ? HkyAimHook(chan, x, y) : 0;
    }
    if (chan < 0 || chan >= 4) {
        return 0;
    }
    if (CurrentScene() != RPSysSceneCreator::ESceneID_RPDucScene) {
        sAimTarget[chan] = nullptr;
        return 0;
    }
    RPDucTargetRegistry* reg = RPDucTargetRegistry::sInstance;
    if (reg == nullptr || RPDucCamera::sInstance == nullptr || RPDucGame::sInstance == nullptr ||
        !RPDucGame::sInstance->mInPlay) {
        sAimTarget[chan] = nullptr;
        return 0;
    }
    // Remote n plays player n. Player 1 takes the first live target it finds, player 2 the
    // last (so that two players do not chase the same one)
    const RPDucTarget* best = nullptr;
    for (u32 k = 0; k < reg->mMgrNum; k++) {
        u32 i = chan == 0 ? k : reg->mMgrNum - 1 - k;
        RPDucTargetMgr* mgr = reg->mpMgrs[i];
        if (mgr->mType == 6) {  // the Miis of the last round: not targets
            continue;
        }
        for (s32 m = 0; m < mgr->mPoolNum; m++) {
            s32 j = chan == 0 ? m : mgr->mPoolNum - 1 - m;
            const RPDucTarget* t = mgr->mppPool[j];
            f32 tx, ty;
            if (!Aimable(t, chan) || !ScreenPos(t, &tx, &ty)) {
                continue;
            }
            if (t == sAimTarget[chan]) {
                *x = tx;
                *y = ty;
                return 1;
            }
            if (best == nullptr) {
                best = t;
            }
        }
    }
    sAimTarget[chan] = best;
    return best != nullptr && ScreenPos(best, x, y);
}
