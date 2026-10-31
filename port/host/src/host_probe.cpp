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
#include <Pack/RPSystem/RPSysSceneCreator.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>
#include <egg/gfxe/eggScreen.h>
#undef private
#undef protected

extern "C" int PortGameProbe(char* buf, int len);
extern "C" int PortGameProbeMore(char* buf, int len); // host_probe_games.cpp

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

// Player 0's paddle target for a pointer position: RPHkyPaddle::UpdateControl's mapping
bool HkyPointerToRink(f32 px, f32 py, nw4r::math::VEC3* out) {
    const RPGrpScreen* screen = RPGrpRenderer::GetCurrent()->GetView(0)->GetScreen();
    f32 x = px * static_cast<f32>(static_cast<u16>(EGG::Screen::GetSizeXMax() / 2));
    f32 y = -py * static_cast<f32>(static_cast<u16>(EGG::Screen::GetSizeYMax() / 2));
    nw4r::math::VEC3 planePoint(0.0f, 0.0f, 0.0f);
    nw4r::math::VEC3 planeNormal(0.0f, 1.0f, 0.0f);
    nw4r::math::VEC3 t;
    if (!RPHkyGame::sInstance->mpCamera->GetScreenToPlane(&t, planePoint, planeNormal, screen, x, y)) {
        return false;
    }
    t.x = t.x * 0.5f - (RPHkyField::sWidth / 2.0f - (RPHkyField::cCorner - RPHkyField::cGoalInset));
    t.z *= RPHkyPaddle::sPointerDepthScale[0];
    *out = t;
    return true;
}

// The pointer position whose rink target is (gx, gz): Newton's method on the game's mapping
f32 sHkyPx = 0.0f, sHkyPy = 0.0f;

bool HkyAim(f32 gx, f32 gz, f32* x, f32* y) {
    f32 px = sHkyPx, py = sHkyPy;
    for (int it = 0; it < 8; it++) {
        nw4r::math::VEC3 a, bx, by;
        const f32 h = 0.01f;
        if (!HkyPointerToRink(px, py, &a) || !HkyPointerToRink(px + h, py, &bx) ||
            !HkyPointerToRink(px, py + h, &by)) {
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
    sHkyPx = px;
    sHkyPy = py;
    *x = px;
    *y = py;
    return true;
}

int HkyAimHook(f32* x, f32* y) {
    RPHkyGame* game = RPHkyGame::sInstance;
    if (game->mState != 1) {  // play
        return 0;
    }
    // Strike through the puck: aim a little past its centre, away from player 0's goal
    const nw4r::math::VEC3& puck = game->mpPuck->GetPos();
    nw4r::math::VEC3 paddle = game->mpPaddles[0]->GetPos();
    f32 dx = puck.x - paddle.x, dz = puck.z - paddle.z;
    f32 d = std::sqrt(dx * dx + dz * dz);
    f32 gx = puck.x, gz = puck.z;
    if (d > 1.0f) {
        gx += dx / d * RPHkyPuck::sRadius;
        gz += dz / d * RPHkyPuck::sRadius;
    }
    return HkyAim(gx, gz, x, y);
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
    std::snprintf(buf, len, "duc:%d,%d,%d,%d", static_cast<int>(game->mState),
                  static_cast<int>(game->mRound), static_cast<int>(score->mScores[0]),
                  static_cast<int>(score->mHits[0]));
    return 1;
}

extern "C" int PortInputAimHook(int chan, f32* x, f32* y) {
    if (chan == 0 && HkyLive()) {
        return HkyAimHook(x, y);
    }
    if (CurrentScene() != RPSysSceneCreator::ESceneID_RPDucScene) {
        sAimTarget = nullptr;
        return 0;
    }
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
