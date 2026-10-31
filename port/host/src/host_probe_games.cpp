/*
 * Test probes and players for the minigames after Shooting Range and Laser Hockey
 * (docs/HOST_BUILD.md §33), boot only.
 *
 *   PortGameProbeMore  the FRAME line's `game=` field for Find Mii (wly:), Pose Mii (bom:),
 *                      Table Tennis (pnp:), Billiards (bil:), Fishing (fsh:), Charge! (cow:)
 *                      and Tanks! (tnk:): the game's own state, progress and score.
 *   PortInputPlayHook  the input script's `play 1`: each retrace, a player for the scene
 *                      running sets remote 0's pointer, buttons, tilt and acceleration from
 *                      the game's own state (its positions, projections and input rules),
 *                      and on the results screens points at "Play a different game" and
 *                      presses A.
 *
 * Reads only; nothing here changes game state. The game's classes keep these fields private,
 * so this file opens them up, as host_probe.cpp does.
 */
#include <cmath>
#include <cstdio>
#include <cstring>

#include <port/input.h>

#define private public
#define protected public
#include <Pack/RPGraphics/RPGrpCamera.h>
#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPGraphics/RPGrpRenderer.h>
#include <Pack/RPParty/RPBilScene/RPBilBall.h>
#include <Pack/RPParty/RPBilScene/RPBilBallMgr.h>
#include <Pack/RPParty/RPBilScene/RPBilCamera.h>
#include <Pack/RPParty/RPBilScene/RPBilGameMgr.h>
#include <Pack/RPParty/RPBilScene/RPBilPlayer.h>
#include <Pack/RPParty/RPBilScene/RPBilTable.h>
#include <Pack/RPParty/RPBomScene/RPBomBorder.h>
#include <Pack/RPParty/RPBomScene/RPBomCursor.h>
#include <Pack/RPParty/RPBomScene/RPBomGameManager.h>
#include <Pack/RPParty/RPBomScene/RPBomPanel_2.h>
#include <Pack/RPParty/RPBomScene/RPBomPanel_3.h>
#include <Pack/RPParty/RPBomScene/RPBomPlayer.h>
#include <Pack/RPParty/RPBomScene/RPBomScore.h>
#include <Pack/RPParty/RPCowScene/RPCowField.h>
#include <Pack/RPParty/RPCowScene/RPCowFieldObj.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPCowScene/RPCowKakasiGroup.h>
#include <Pack/RPParty/RPCowScene/RPCowPlayer.h>
#include <Pack/RPParty/RPPartyCommon/RPCollisionShape.h>
#include <Pack/RPParty/RPFshScene/RPFshCamera.h>
#include <Pack/RPParty/RPFshScene/RPFshFish.h>
#include <Pack/RPParty/RPFshScene/RPFshFishMgr.h>
#include <Pack/RPParty/RPFshScene/RPFshPlayer.h>
#include <Pack/RPParty/RPFshScene/RPFshPlayerMgr.h>
#include <Pack/RPParty/RPFshScene/RPFshResultLayout.h>
#include <Pack/RPParty/RPFshScene/RPFshRod.h>
#include <Pack/RPParty/RPFshScene/RPFshScoreLayout.h>
#include <Pack/RPParty/RPFshScene/RPFshStartLayout.h>
#include <Pack/RPParty/RPFshScene/RPFshTimer.h>
#include <Pack/RPParty/RPPnpScene/RPPnpBall.h>
#include <Pack/RPParty/RPPnpScene/RPPnpField.h>
#include <Pack/RPParty/RPPnpScene/RPPnpGame.h>
#include <Pack/RPParty/RPPnpScene/RPPnpPlayer.h>
#include <Pack/RPParty/RPWlyScene/RPWlyAvatar.h>
#include <Pack/RPParty/RPWlyScene/RPWlyAvatarMgr.h>
#include <Pack/RPParty/RPWlyScene/RPWlyCursor.h>
#include <Pack/RPParty/RPWlyScene/RPWlyField_5.h>
#include <Pack/RPParty/RPWlyScene/RPWlyGameMgr.h>
#include <Pack/RPParty/RPWlyScene/RPWlyLevelTable.h>
#include <Pack/RPParty/RPTnkScene/RPTnkBlock.h>
#include <Pack/RPParty/RPTnkScene/RPTnkCamera.h>
#include <Pack/RPParty/RPTnkScene/RPTnkCursor.h>
#include <Pack/RPParty/RPTnkScene/RPTnkField.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTank.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>
#include <Pack/RPSystem/RPSysSceneCreator.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>
#include <egg/core/eggController.h>
#include <egg/gfxe/eggScreen.h>
#include <nw4r/g3d/g3d_scnobj.h>
#undef private
#undef protected

extern "C" int PortGameProbeMore(char* buf, int len);

namespace {

s32 CurrentScene() {
    RPSysSceneMgr* mgr = RPSysSceneMgr::instance();
    return mgr != nullptr ? mgr->getCurrentSceneID() : -1;
}

f32 Clamp(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

f32 Wrap180(f32 deg) {
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg <= -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

f32 HalfW() {
    return static_cast<f32>(static_cast<u16>(EGG::Screen::GetSizeXMax() / 2));
}

f32 HalfH() {
    return static_cast<f32>(static_cast<u16>(EGG::Screen::GetSizeYMax() / 2));
}

// The pointer position KPAD reported last frame (what the game read)
bool KpadPos(f32* x, f32* y) {
    EGG::CoreControllerMgr* mgr = EGG::CoreControllerMgr::instance();
    if (mgr == nullptr) {
        return false;
    }
    EGG::CoreController* c = mgr->getNthController(0);
    if (c == nullptr) {
        return false;
    }
    const EGG::CoreStatus* s = c->getCoreStatus();
    if (s->getDPDValidFlag() <= 0) {
        return false;
    }
    *x = s->pos.x;
    *y = s->pos.y;
    return true;
}

/*
 * Per-scene player state: buttons pressed for a few frames at a time (the games read
 * triggers), and a pointer servo. The games' KPAD settings damp pointer moves below a play
 * radius (KPAD's pos_play_radius), so the reported position stalls short of a small step;
 * the servo adds the remaining error, as a player's hand would.
 */
struct Player {
    int lastFrame = -10;
    int frame = 0;
    u16 held = 0;      // buttons held this frame
    int holdUntil = 0; // held until this frame (exclusive)
    int nextTap = 0;   // no new press before this frame
    f32 biasX = 0.0f, biasY = 0.0f;
    int step = 0;      // a game's own sub-state
    int target = -1;
    int timer = 0;
    f32 vals[8] = {};
};

Player sPlayer;
s32 sPlayerScene = -1;

Player& Begin(int frame) {
    s32 scene = CurrentScene();
    if (scene != sPlayerScene || frame != sPlayer.lastFrame + 1) {
        sPlayer = Player();
        sPlayerScene = scene;
    }
    sPlayer.lastFrame = frame;
    sPlayer.frame = frame;
    if (frame >= sPlayer.holdUntil) {
        sPlayer.held = 0;
    }
    return sPlayer;
}

// Press `bit` for `frames` frames, then nothing new for `gap` frames after the press
bool Tap(Player& p, u16 bit, int frames = 2, int gap = 6) {
    if (p.frame < p.nextTap || p.held != 0) {
        return false;
    }
    p.held = bit;
    p.holdUntil = p.frame + frames;
    p.nextTap = p.frame + frames + gap;
    return true;
}

void PointAt(Player& p, PortInputState* st, f32 x, f32 y, bool servo) {
    if (servo) {
        f32 ax, ay;
        if (KpadPos(&ax, &ay) && st->pointerValid) {
            // the error of the position commanded last frame
            p.biasX = Clamp(p.biasX + 0.5f * (st->pointerX - p.biasX - ax), -0.08f, 0.08f);
            p.biasY = Clamp(p.biasY + 0.5f * (st->pointerY - p.biasY - ay), -0.08f, 0.08f);
        }
    } else {
        p.biasX = p.biasY = 0.0f;
    }
    st->pointerValid = 1;
    st->pointerX = x + p.biasX;
    st->pointerY = y + p.biasY;
}

void Neutral(PortInputState* st) {
    st->buttons = 0;
    st->roll = st->pitch = 0.0f;
    st->accel[0] = st->accel[1] = st->accel[2] = 0.0f;
    st->stickX = st->stickY = 0.0f;
}

// The shared results (RPMedalResult, RPResultMenu): the pointer on "Play a different game"
// (window 320, 348 of 608 x 456, as the Laser Hockey script), A every 150 frames
void Results(Player& p, PortInputState* st) {
    Neutral(st);
    f32 x, y;
    PortInputPointerFromWindow(320.0f, 348.0f, 608.0f, 456.0f, &x, &y);
    PointAt(p, st, x, y, false);
    Tap(p, WPAD_BUTTON_A, 3, 147);
    st->buttons = p.held;
}

/* ---- Find Mii (RPWlyScene) ---- */

// Stages played to the answer; after these the player picks wrong Miis until the time is up
const s32 WLY_STAGES = 5;

RPWlyLevelData* WlyLevel() {
    return RPWlyLevelTable::sInstance != nullptr ? RPWlyLevelTable::sInstance->GetCurrentLevel() : nullptr;
}

bool WlyLive() {
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPWlyScene && RPWlyGameMgr::sInstance != nullptr &&
           RPWlyAvatarMgr::sInstance != nullptr && RPWlyAvatarMgr::sInstance->mpStage != nullptr &&
           WlyLevel() != nullptr;
}

int WlyProbe(char* buf, int len) {
    RPWlyGameMgr* g = RPWlyGameMgr::sInstance;
    RPWlyStage* s = RPWlyAvatarMgr::sInstance->mpStage;
    std::snprintf(buf, len, "wly:%d,%d,%d,%d,%d,%d,%d", static_cast<int>(g->mState),
                  static_cast<int>(g->mStageNo), static_cast<int>(g->mTimer), static_cast<int>(s->mTargetNum),
                  static_cast<int>(s->mFoundNum), static_cast<int>(static_cast<s32>(WlyLevel()->mQuestionType)),
                  static_cast<int>(g->mPenaltyTimers[0]));
    return 1;
}

bool WlyOnScreen(const RPWlyAvatar* a, f32* x, f32* y) {
    if (a == nullptr || !a->mVisible) {
        return false;
    }
    *x = a->mScreenPos.x / HalfW();
    *y = -a->mScreenPos.y / HalfH();
    return std::fabs(*x) < 0.9f && std::fabs(*y) < 0.9f && a->mScreenPos.z > 0.0f;
}

void WlyPlay(Player& p, PortInputState* st) {
    RPWlyGameMgr* g = RPWlyGameMgr::sInstance;
    RPWlyAvatarMgr* am = RPWlyAvatarMgr::sInstance;
    RPWlyStage* s = am->mpStage;
    Neutral(st);
    if (g->mState == 4) {
        Results(p, st);
        return;
    }
    if (g->mState != 2 || g->mpWipe == nullptr || !g->mpWipe->mDone || g->mTimer <= 2) {
        p.target = -1;
        return;
    }
    s32 num = WlyLevel()->mAvatarNum;
    s32 qtype = WlyLevel()->mQuestionType;
    bool wrong = g->mStageNo > WLY_STAGES && qtype != 6;
    f32 x, y;
    // keep the avatar being picked until its pick is made (it is then hovered or found)
    if (p.target >= 0) {
        bool done = p.target < s->mTargetNum && (s->mFound[p.target] || s->mHover[p.target].mHovered);
        if (done || p.timer-- <= 0 || !WlyOnScreen(am->mpAvatars[p.target], &x, &y)) {
            p.target = -1;
        }
    }
    if (p.target < 0) {
        for (s32 i = wrong ? s->mTargetNum : 0; i < num && i < 100; i++) {
            bool want = wrong || qtype == 6 || (i < s->mTargetNum && !s->mFound[i]);
            if (want && !s->mHover[i].mHovered && am->mpAvatars[i]->mReaction == 0 &&
                WlyOnScreen(am->mpAvatars[i], &x, &y)) {
                p.target = i;
                p.timer = 90;
                break;
            }
        }
    }
    if (p.target < 0 || !WlyOnScreen(am->mpAvatars[p.target], &x, &y)) {
        return;
    }
    PointAt(p, st, x, y, false);
    if (am->mHoverIdx[0] == p.target && g->mPenaltyTimers[0] == 0) {
        Tap(p, WPAD_BUTTON_A, 2, 38);
    }
    st->buttons = p.held;
}

/* ---- Pose Mii (RPBomScene) ---- */

// Levels played; in the next one the player lets the bubbles fall (the misses end the game)
const s32 BOM_LEVELS = 2;

bool BomLive() {
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPBomScene && RPBomGameManager::sInstance != nullptr &&
           RPBomScore::sInstance != nullptr && RPBomPlayerManager::sInstance != nullptr &&
           RPBomCursorManager::sInstance != nullptr && RPBomPanelPool::sInstance != nullptr;
}

// The miss-counter borders' z in the world (their models' local matrices), x10
void BomBorders(int z[3]) {
    z[0] = z[1] = z[2] = 0;
    RPBomBorder* b = RPBomBorder::sInstance;
    if (b == nullptr) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        nw4r::math::MTX34 m;
        if (b->mpModels[i] != nullptr && b->mpModels[i]->GetScnObj() != nullptr &&
            b->mpModels[i]->GetScnObj()->GetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &m)) {
            z[i] = static_cast<int>(std::floor(m._23 * 10.0f + 0.5f));
        }
    }
}

int BomProbe(char* buf, int len) {
    RPBomGameManager* g = RPBomGameManager::sInstance;
    RPBomScore* sc = RPBomScore::sInstance;
    int z[3];
    BomBorders(z);
    std::snprintf(buf, len, "bom:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(g->mState),
                  static_cast<int>(g->mLevel), static_cast<int>(sc->mScores[0]), static_cast<int>(sc->mHits[0]),
                  static_cast<int>(sc->mCombos[0]), static_cast<int>(g->mMissCount),
                  static_cast<int>(g->mMissMax), static_cast<int>(g->mLevelTimer), z[0], z[1], z[2]);
    return 1;
}

// The pose order A steps through (RPBomPlayer::StatePosing: A 1 -> 2 -> 0, B back)
int BomPoseIndex(s32 pose) {
    return pose == 1 ? 0 : pose == 2 ? 1 : 2;
}

void BomPlay(Player& p, PortInputState* st) {
    RPBomGameManager* g = RPBomGameManager::sInstance;
    RPBomPlayer* pl = RPBomPlayerManager::sInstance->mpPlayers[0];
    RPBomCursor* cur = RPBomCursorManager::sInstance->mpCursors[0];
    Neutral(st);
    if (g->mState >= 9) {
        Results(p, st);
        return;
    }
    if (pl == nullptr || cur == nullptr) {
        return;
    }
    // The bubble to match: the start bubble while waiting for it, else the lowest falling one
    RPBomPanelPool* pool = RPBomPanelPool::sInstance;
    RPBomPanel* best = nullptr;
    bool wantStart = g->mState == 1 || g->mState == 7 || g->mState == 6;
    for (RPBomPoolObj** pp = pool->mppBegin; pp != nullptr && pp <= pool->mppLast; pp++) {
        RPBomPanel* b = static_cast<RPBomPanel*>(*pp);
        if (b == nullptr || b->mAge < 0 || b->mMatched || (b->mState != 1 && !b->mIsStartBubble) ||
            b->mIsStartBubble != wantStart) {
            continue;
        }
        if (best == nullptr || b->mPos.z > best->mPos.z) {
            best = b;
        }
    }
    if (best == nullptr) {
        PointAt(p, st, 0.0f, 0.0f, true);
        return;
    }
    f32 range = cur->mRange;
    if (g->mLevel >= BOM_LEVELS && !wantStart) {
        // away from the bubble and turned from it
        PointAt(p, st, best->mPos.x > 0.0f ? -0.9f : 0.9f, -0.5f, true);
        st->roll = Wrap180(Wrap180(best->mAngle) / 4.0f + 30.0f);
        return;
    }
    PointAt(p, st, (best->mPos.x + best->mVel.x) / range, (best->mPos.z + best->mVel.z) / range, true);
    // twist: the Mii turns by four times the remote's roll
    st->roll = Wrap180(best->mAngle + best->mRotSpeed) / 4.0f;
    int want = BomPoseIndex(best->mPose), have = BomPoseIndex(pl->mPose);
    if (want != have && pl->mPoseCooldown == 0 && !pl->mInBubble) {
        Tap(p, want > have ? WPAD_BUTTON_A : WPAD_BUTTON_B, 2, 10);
    }
    st->buttons = p.held;
}

/* ---- Table Tennis (RPPnpScene) ---- */

// The racket target the player last chose (x, z) and how: 0 waiting, 1 predicted, 2 aimed
f32 sPnpGoal[2] = {0.0f, 0.0f};
int sPnpHow = 0;

bool PnpLive() {
    RPPnpGame* g = RPPnpGame::sInstance;
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPPnpScene && g != nullptr && g->GetBall() != nullptr &&
           g->GetPlayer(0) != nullptr && g->GetCamera() != nullptr && g->GetTable() != nullptr;
}

int PnpProbe(char* buf, int len) {
    RPPnpGame* g = RPPnpGame::sInstance;
    RPPnpBall* b = g->GetBall();
    RPPnpPlayer* pl = g->GetPlayer(0);
    std::snprintf(buf, len, "pnp:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(g->GetState()),
                  static_cast<int>(b->mState), static_cast<int>(b->mHitCount), static_cast<int>(g->mRallyCount),
                  static_cast<int>(std::floor(b->mPos.x)), static_cast<int>(std::floor(b->mPos.y)),
                  static_cast<int>(std::floor(b->mPos.z)), static_cast<int>(std::floor(pl->mTarget.x)),
                  static_cast<int>(std::floor(pl->mTarget.z)), static_cast<int>(b->mHitterSide),
                  static_cast<int>(std::floor(sPnpGoal[0])), static_cast<int>(std::floor(sPnpGoal[1])), sPnpHow);
    return 1;
}

// Player 0's racket target for a pointer position: RPPnpPlayer::UpdateHuman's mapping
bool PnpPointerToTable(f32 px, f32 py, f32* x, f32* z) {
    RPPnpGame* g = RPPnpGame::sInstance;
    const RPGrpScreen* screen = RPGrpRenderer::GetCurrent()->GetView(0)->GetScreen();
    nw4r::math::VEC3 planePos(0.0f, g->GetTable()->GetHeight() + RPPnpPlayer::sPlayerHeight, 0.0f);
    nw4r::math::VEC3 planeNrm(0.0f, 1.0f, 0.0f);
    nw4r::math::VEC3 t;
    if (!g->GetCamera()->GetScreenToPlane(&t, planePos, planeNrm, screen, px * HalfW(), -py * HalfH())) {
        return false;
    }
    t.z *= RPPnpPlayer::sHumanFollowRate;
    nw4r::math::VEC3 camTarget = g->GetCamera()->GetTarget();
    f32 base = RPPnpPlayer::sHumanMaxX -
               ((RPPnpPlayer::sHumanMaxX + RPPnpPlayer::sHumanMinX) * (camTarget.z + RPPnpGame::sCameraOffsetFar)) /
                   (RPPnpGame::sCameraOffsetFar - RPPnpGame::sCameraOffsetNear);
    *x = t.x;
    *z = t.z + base;
    return true;
}

// The pointer position for a racket target (gx, gz): Newton's method on the game's mapping
bool PnpAim(Player& p, f32 gx, f32 gz, f32* outx, f32* outy) {
    f32 px = p.vals[0], py = p.vals[1];
    for (int it = 0; it < 8; it++) {
        f32 ax, az, bxx, bxz, byx, byz;
        const f32 h = 0.01f;
        if (!PnpPointerToTable(px, py, &ax, &az) || !PnpPointerToTable(px + h, py, &bxx, &bxz) ||
            !PnpPointerToTable(px, py + h, &byx, &byz)) {
            return false;
        }
        f32 ex = gx - ax, ez = gz - az;
        if (std::fabs(ex) < 0.05f && std::fabs(ez) < 0.05f) {
            break;
        }
        f32 j00 = (bxx - ax) / h, j01 = (byx - ax) / h, j10 = (bxz - az) / h, j11 = (byz - az) / h;
        f32 det = j00 * j11 - j01 * j10;
        if (std::fabs(det) < 1e-6f) {
            return false;
        }
        px = Clamp(px + (j11 * ex - j01 * ez) / det, -1.2f, 1.2f);
        py = Clamp(py + (-j10 * ex + j00 * ez) / det, -1.2f, 1.2f);
    }
    p.vals[0] = px;
    p.vals[1] = py;
    *outx = px;
    *outy = py;
    return true;
}

// The racket waits on the plane z = PNP_RACKET_Z (where it meets a ball that has bounced on
// player 0's half at a height RPPnpPlayer::CheckBallHit accepts); x where the ball crosses that
// plane: the ball's x moves linearly (RPPnpBall::Integrate: no sideways gravity)
const f32 PNP_RACKET_Z = 165.0f;
// Rallies played; then the racket stays away and the next ball ends the game
const s32 PNP_RALLIES = 30;

bool PnpPredict(f32* tx, f32* tz) {
    RPPnpBall* b = RPPnpGame::sInstance->GetBall();
    if (b->mVelocity.z <= 0.0f || b->mPos.z >= PNP_RACKET_Z) {
        return false;
    }
    *tx = Clamp(b->mPos.x + b->mVelocity.x / b->mVelocity.z * (PNP_RACKET_Z - b->mPos.z), -140.0f, 140.0f);
    *tz = PNP_RACKET_Z;
    return true;
}

void PnpPlay(Player& p, PortInputState* st) {
    RPPnpGame* g = RPPnpGame::sInstance;
    RPPnpBall* b = g->GetBall();
    Neutral(st);
    if (g->GetState() == RPPnpGame::STATE_RESULT) {
        Results(p, st);
        return;
    }
    // the racket waits in the middle of player 0's end (also where the game wants it held)
    f32 gx = 0.0f, gz = 160.0f;
    if (b->mHitCount / 2 >= PNP_RALLIES) {
        gx = 120.0f;
    } else if (g->GetState() == RPPnpGame::STATE_RALLY && b->mHitterSide == 1) {
        f32 tx, tz;
        if (PnpPredict(&tx, &tz)) {
            gx = tx;
            gz = tz;
        }
    }
    sPnpGoal[0] = gx;
    sPnpGoal[1] = gz;
    sPnpHow = gz != 160.0f || gx != 0.0f ? 1 : 0;
    f32 x, y;
    if (PnpAim(p, gx, gz, &x, &y)) {
        PointAt(p, st, x, y, true);
        sPnpHow += 2;
    } else {
        PointAt(p, st, 0.0f, 0.3f, false);
    }
    // the CPU serves when player 0 presses A with the racket held
    if (g->GetState() == RPPnpGame::STATE_SERVE && b->mState == 1 && g->GetPlayer(0)->unk98) {
        Tap(p, WPAD_BUTTON_A, 2, 60);
    }
    st->buttons = p.held;
}


/* ---- Tanks! (RPTnkScene) ---- */

// Missions the player tries to clear; after these it stops (the enemies end the game)
const s32 TNK_MISSIONS = 2;
const int TNK_ROWS = 17;
const f32 TNK_CELL = 35.0f;

bool TnkLive() {
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPTnkScene && RPTnkGameManager::sInstance != nullptr &&
           RPTnkGameStats::sInstance != nullptr && RPTnkTankManager::sInstance != nullptr &&
           RPTnkTankManager::sInstance->GetPlayerTank(0) != nullptr && RPTnkField::sInstance != nullptr &&
           RPTnkCamera::sInstance != nullptr && RPTnkCursorManager::sInstance != nullptr &&
           RPTnkBlockPool::sInstance != nullptr;
}

int TnkProbe(char* buf, int len) {
    RPTnkGameManager* g = RPTnkGameManager::sInstance;
    RPTnkGameStats* st = RPTnkGameStats::sInstance;
    RPTnkTank* t = RPTnkTankManager::sInstance->GetPlayerTank(0);
    std::snprintf(buf, len, "tnk:%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(g->mState), static_cast<int>(g->mStage),
                  static_cast<int>(st->mKills[0]), static_cast<int>(st->mLives[0]),
                  static_cast<int>(RPTnkTankManager::sInstance->mEnemyAliveNum),
                  static_cast<int>(std::floor(t->GetPos().x)), static_cast<int>(std::floor(t->GetPos().z)),
                  static_cast<int>(t->mAlive));
    return 1;
}

// The blocks shells and tanks meet (cork and solid; holes stop tanks only)
bool TnkBlocked(f32 x0, f32 z0, f32 x1, f32 z1, f32 inflate, bool holes) {
    RPTnkBlockPool* pool = RPTnkBlockPool::sInstance;
    for (RPTnkActor** pp = pool->mppBegin; pp != nullptr && pp <= pool->mppLast; pp++) {
        RPTnkBlock* b = static_cast<RPTnkBlock*>(*pp);
        if (b == nullptr || !b->IsActive() || (b->mKind == 2 && !holes)) {
            continue;
        }
        // segment against the inflated square (slab test)
        f32 h = TNK_CELL / 2.0f + inflate;
        f32 lo = 0.0f, hi = 1.0f;
        f32 o[2] = {x0 - b->GetPos().x, z0 - b->GetPos().z};
        f32 d[2] = {x1 - x0, z1 - z0};
        bool hit = true;
        for (int k = 0; k < 2 && hit; k++) {
            if (std::fabs(d[k]) < 1e-6f) {
                hit = std::fabs(o[k]) <= h;
            } else {
                f32 ta = (-h - o[k]) / d[k], tb = (h - o[k]) / d[k];
                if (ta > tb) {
                    f32 tmp = ta;
                    ta = tb;
                    tb = tmp;
                }
                lo = ta > lo ? ta : lo;
                hi = tb < hi ? tb : hi;
                hit = lo <= hi;
            }
        }
        if (hit) {
            return true;
        }
    }
    return false;
}

RPTnkTank* TnkTarget(const nw4r::math::VEC3& from) {
    RPTnkTankManager* m = RPTnkTankManager::sInstance;
    RPTnkTank* best = nullptr;
    f32 bestD = 0.0f;
    for (RPTnkTank** pp = m->mppEnemyBegin; pp != nullptr && pp <= m->mppEnemyLast; pp++) {
        RPTnkTank* e = *pp;
        if (e == nullptr || !e->IsActive() || e->GetAlive() <= 0) {
            continue;
        }
        f32 dx = e->GetPos().x - from.x, dz = e->GetPos().z - from.z;
        f32 d = dx * dx + dz * dz;
        if (!TnkBlocked(from.x, from.z, e->GetPos().x, e->GetPos().z, 6.0f, false)) {
            d *= 0.01f;  // prefer one in sight
        }
        if (best == nullptr || d < bestD) {
            best = e;
            bestD = d;
        }
    }
    return best;
}

nw4r::math::VEC3 TnkCellPos(int c, int r) {
    s32 cols = RPTnkField::sInstance->mCols;
    return nw4r::math::VEC3((c - (cols - 1) / 2.0f) * TNK_CELL, 0.0f, (r - (TNK_ROWS - 1) / 2.0f) * TNK_CELL);
}

// The next cell centre on a shortest path (8 directions, no corner cutting) to a cell that
// sees the target; false when none is reachable
bool TnkPath(const nw4r::math::VEC3& from, const nw4r::math::VEC3& to, nw4r::math::VEC3* next) {
    s32 cols = RPTnkField::sInstance->mCols;
    if (cols <= 0 || cols > 32) {
        return false;
    }
    static bool wall[32][TNK_ROWS];
    static int prev[32 * TNK_ROWS];
    int queue[32 * TNK_ROWS];
    std::memset(wall, 0, sizeof(wall));
    RPTnkBlockPool* pool = RPTnkBlockPool::sInstance;
    for (RPTnkActor** pp = pool->mppBegin; pp != nullptr && pp <= pool->mppLast; pp++) {
        RPTnkBlock* b = static_cast<RPTnkBlock*>(*pp);
        if (b == nullptr || !b->IsActive()) {
            continue;
        }
        int c = static_cast<int>(std::floor(b->GetPos().x / TNK_CELL + (cols - 1) / 2.0f + 0.5f));
        int r = static_cast<int>(std::floor(b->GetPos().z / TNK_CELL + (TNK_ROWS - 1) / 2.0f + 0.5f));
        if (c >= 0 && c < cols && r >= 0 && r < TNK_ROWS) {
            wall[c][r] = true;
        }
    }
    int sc = static_cast<int>(std::floor(from.x / TNK_CELL + (cols - 1) / 2.0f + 0.5f));
    int sr = static_cast<int>(std::floor(from.z / TNK_CELL + (TNK_ROWS - 1) / 2.0f + 0.5f));
    if (sc < 0 || sc >= cols || sr < 0 || sr >= TNK_ROWS) {
        return false;
    }
    for (int i = 0; i < cols * TNK_ROWS; i++) {
        prev[i] = -2;
    }
    int head = 0, tail = 0, goal = -1;
    queue[tail++] = sc * TNK_ROWS + sr;
    prev[sc * TNK_ROWS + sr] = -1;
    while (head < tail) {
        int cur = queue[head++];
        int c = cur / TNK_ROWS, r = cur % TNK_ROWS;
        nw4r::math::VEC3 p = TnkCellPos(c, r);
        f32 dx = to.x - p.x, dz = to.z - p.z;
        // a cell away from the border walls that sees the target from a distance
        if (cur != queue[0] && c > 0 && c < cols - 1 && r > 0 && r < TNK_ROWS - 1 && dx * dx + dz * dz > 90.0f * 90.0f &&
            !TnkBlocked(p.x, p.z, to.x, to.z, 8.0f, false)) {
            goal = cur;
            break;
        }
        for (int k = 0; k < 8; k++) {
            static const int DC[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            static const int DR[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            int nc = c + DC[k], nr = r + DR[k];
            if (nc < 0 || nc >= cols || nr < 0 || nr >= TNK_ROWS || wall[nc][nr] || prev[nc * TNK_ROWS + nr] != -2) {
                continue;
            }
            if (DC[k] != 0 && DR[k] != 0 && (wall[c + DC[k]][r] || wall[c][r + DR[k]])) {
                continue;
            }
            prev[nc * TNK_ROWS + nr] = cur;
            queue[tail++] = nc * TNK_ROWS + nr;
        }
    }
    if (goal < 0) {
        return false;
    }
    int step = goal;
    while (prev[step] >= 0 && prev[step] != queue[0]) {
        step = prev[step];
    }
    *next = TnkCellPos(step / TNK_ROWS, step % TNK_ROWS);
    return true;
}

void TnkPlay(Player& p, PortInputState* st) {
    RPTnkGameManager* g = RPTnkGameManager::sInstance;
    RPTnkTank* me = RPTnkTankManager::sInstance->GetPlayerTank(0);
    RPTnkCursor* cur = RPTnkCursorManager::sInstance->GetCursor(0);
    Neutral(st);
    if (g->mState >= 9) {
        Results(p, st);
        return;
    }
    if (g->mState != 4 || me->GetAlive() <= 0 || cur == nullptr) {
        p.target = -1;
        PointAt(p, st, 0.0f, 0.0f, false);
        return;
    }
    nw4r::math::VEC3 pos = me->GetPos();
    RPTnkTank* e = TnkTarget(pos);
    if (e == nullptr) {
        return;
    }
    nw4r::math::VEC3 ep = e->GetPos();
    f32 dx = ep.x - pos.x, dz = ep.z - pos.z;
    f32 d = std::sqrt(dx * dx + dz * dz);
    if (d < 1.0f) {
        return;
    }
    // the turret follows the cursor's direction from the tank: aim 150 along it
    nw4r::math::VEC3 aim(pos.x + dx / d * 150.0f, me->GetBodyHeight(), pos.z + dz / d * 150.0f);
    nw4r::math::VEC2 v;
    RPTnkCamera::sInstance->ProjectToScreen(&v, &aim);
    PointAt(p, st, v.x / cur->mScreenHalfWidth, -v.y / cur->mScreenHalfWidth, true);
    if (g->mStage >= TNK_MISSIONS) {
        return;  // done: sit still and hold fire
    }
    bool clear = !TnkBlocked(pos.x, pos.z, ep.x, ep.z, 7.0f, false);
    if (clear) {
        // fire when the turret points at the enemy (to within the tank's half width)
        const nw4r::math::VEC3& tz = me->mTurretRot.z;
        f32 cross = tz.x * dz / d - tz.z * dx / d;
        f32 dot = tz.x * dx / d + tz.z * dz / d;
        if (dot > 0.0f && std::fabs(cross) * d < 12.0f) {
            Tap(p, WPAD_BUTTON_B, 2, 8);
        }
    } else {
        nw4r::math::VEC3 next;
        if (TnkPath(pos, ep, &next)) {
            f32 mx = next.x - pos.x, mz = next.z - pos.z;
            u16 b = 0;
            if (mx > 3.0f) {
                b |= WPAD_BUTTON_RIGHT;
            } else if (mx < -3.0f) {
                b |= WPAD_BUTTON_LEFT;
            }
            if (mz > 3.0f) {
                b |= WPAD_BUTTON_DOWN;
            } else if (mz < -3.0f) {
                b |= WPAD_BUTTON_UP;
            }
            st->buttons = b;
        }
    }
    st->buttons |= p.held;
}


/* ---- Fishing (RPFshScene) ---- */

bool FshLive() {
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPFshScene && RPFshPlayerMgr::sInstance != nullptr &&
           RPFshPlayerMgr::sInstance->GetPlayer(0) != nullptr && RPFshFishMgr::sInstance != nullptr &&
           RPFshStartLayout::sInstance != nullptr && RPFshTimer::sInstance != nullptr &&
           RPFshCameraMgr::sInstance != nullptr && RPFshCameraMgr::sInstance->GetCamera() != nullptr &&
           RPFshScoreLayout::sInstance != nullptr && RPFshResultLayout::sInstance != nullptr;
}

int FshProbe(char* buf, int len) {
    RPFshPlayer* pl = RPFshPlayerMgr::sInstance->GetPlayer(0);
    RPFshTimer* t = RPFshTimer::sInstance;
    s32 hooked = pl->mHookedFish >= 0 ? RPFshFishMgr::sInstance->GetFish(pl->mHookedFish)->mType : -1;
    std::snprintf(buf, len, "fsh:%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(RPFshStartLayout::sInstance->GetState()),
                  static_cast<int>(pl->mState), static_cast<int>(t->mFrame), static_cast<int>(t->mTotalFrame),
                  static_cast<int>(pl->mCaughtNum), static_cast<int>(RPFshScoreLayout::sInstance->CalcTotalPoints(0)),
                  static_cast<int>(hooked), static_cast<int>(RPFshResultLayout::sInstance->mState));
    return 1;
}

// A world position as a pointer position (the game's camera and view 0's screen)
bool FshToPointer(const nw4r::math::VEC3& w, f32* x, f32* y) {
    nw4r::math::VEC3 s;
    RPFshCameraMgr::sInstance->GetCamera()->GetWorldToScreen(&s, w,
                                                             RPGrpRenderer::GetCurrent()->GetView(0)->GetScreen());
    *x = s.x / HalfW();
    *y = -s.y / HalfH();
    return std::fabs(*x) < 1.5f && std::fabs(*y) < 1.5f;
}

void FshPlay(Player& p, PortInputState* st) {
    RPFshPlayer* pl = RPFshPlayerMgr::sInstance->GetPlayer(0);
    Neutral(st);
    if (RPFshResultLayout::sInstance->mState >= RPFshResultLayout::STATE_INTRO) {
        Results(p, st);
        return;
    }
    s32 start = RPFshStartLayout::sInstance->GetState();
    f32 x, y;
    if (start < RPFshStartLayout::STATE_WAIT_READY || pl->mState == RPFshPlayer::STATE_NONE) {
        PointAt(p, st, 0.0f, 0.0f, false);
        return;
    }
    switch (pl->mState) {
    case RPFshPlayer::STATE_AIM: {
        // the hand casts when it reaches the rod's grip on the screen: point at the grip
        EGG::Vector3f grip = pl->mpRod->GetGripPos();
        nw4r::math::VEC3 g(grip.x, grip.y, grip.z);
        if (FshToPointer(g, &x, &y)) {
            PointAt(p, st, Clamp(x, -1.0f, 1.0f), Clamp(y, -1.0f, 1.0f), false);
        }
        p.target = -1;
        p.vals[2] = st->pointerX;
        p.vals[3] = st->pointerY;
        break;
    }
    case RPFshPlayer::STATE_WAIT: {
        // the nearest fish that is not a "suka" (it costs points); the hook goes just in front
        // of its mouth, then the pointer holds still while it comes to bite
        RPFshFishMgr* fm = RPFshFishMgr::sInstance;
        const nw4r::math::VEC3& hook = pl->mpRod->GetRopePoint(7);
        if (p.target >= 0) {
            s32 fs = fm->GetFish(p.target)->mState;
            if (fs != 1 && fs != 4) {
                p.target = -1;
            }
        }
        if (p.target < 0) {
            f32 bestD = 0.0f;
            for (s32 i = 0; i < 30; i++) {
                RPFshFish* f = fm->GetFish(i);
                if (f == nullptr || f->mType == 3 || (f->mState != 1 && f->mState != 4)) {
                    continue;
                }
                f32 dx = f->GetPos().x - hook.x, dz = f->GetPos().z - hook.z;
                f32 d = dx * dx + dz * dz;
                if (p.target < 0 || d < bestD) {
                    p.target = i;
                    bestD = d;
                }
            }
        }
        x = p.vals[2];
        y = p.vals[3];
        if (p.target >= 0) {
            RPFshFish* f = fm->GetFish(p.target);
            const nw4r::math::VEC3& ax = f->GetAxisZ();
            f32 h = std::sqrt(ax.x * ax.x + ax.z * ax.z);
            nw4r::math::VEC3 want(f->mMouthPos.x + (h > 0.0f ? ax.x / h : 0.0f) * 40.0f, hook.y,
                                  f->mMouthPos.z + (h > 0.0f ? ax.z / h : 0.0f) * 40.0f);
            f32 wx, wy, hx, hy;
            if (f->mState != 4 && FshToPointer(want, &wx, &wy) && FshToPointer(hook, &hx, &hy)) {
                x = Clamp(x + 0.08f * (wx - hx), -0.95f, 0.95f);
                y = Clamp(y + 0.08f * (wy - hy), -0.95f, 0.95f);
            }
        }
        p.vals[2] = x;
        p.vals[3] = y;
        PointAt(p, st, x, y, false);
        p.timer = 0;
        break;
    }
    case RPFshPlayer::STATE_FIGHT: {
        // a fish on the line: yank (lift the remote sharply) on the first frames, unless it is a
        // "suka"
        PointAt(p, st, p.vals[2], p.vals[3], false);
        s32 type = pl->mHookedFish >= 0 ? RPFshFishMgr::sInstance->GetFish(pl->mHookedFish)->mType : 3;
        if (type != 3 && p.timer < 3) {
            st->accel[1] = -1.2f;
        }
        p.timer++;
        break;
    }
    default:
        PointAt(p, st, p.vals[2], p.vals[3], false);
        break;
    }
}


/* ---- Charge! (RPCowScene) ---- */

// RPCowGame's actor slots (RPCowScene's creation order)
const int COW_SLOT_KAKASI = 2, COW_SLOT_HURDLE = 5, COW_SLOT_PLAYER = 7, COW_SLOT_GOAL = 15;

RPCowPlayer* CowPlayer() {
    RPCowGame* g = RPCowGame::GetInstance();
    return g != nullptr && g->GetSlotNum(COW_SLOT_PLAYER) > 0 ? static_cast<RPCowPlayer*>(g->GetSlotActor(COW_SLOT_PLAYER))
                                                             : nullptr;
}

bool CowLive() {
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPCowScene && RPCowGame::GetInstance() != nullptr &&
           CowPlayer() != nullptr && CowPlayer()->mpCollision != nullptr;
}

int CowProbe(char* buf, int len) {
    RPCowGame* g = RPCowGame::GetInstance();
    RPCowPlayer* c = CowPlayer();
    EGG::Vector3f pos = c->mpCollision->GetPos();
    std::snprintf(buf, len, "cow:%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(g->mState), static_cast<int>(c->mScore),
                  static_cast<int>(c->mBonusScore), static_cast<int>(c->mTime), static_cast<int>(std::floor(pos.x)),
                  static_cast<int>(std::floor(pos.z)), static_cast<int>(g->mKnockedGroups),
                  static_cast<int>(std::floor(c->mSpeed * 100.0f)));
    return 1;
}

void CowPlay(Player& p, PortInputState* st) {
    RPCowGame* g = RPCowGame::GetInstance();
    RPCowPlayer* c = CowPlayer();
    Neutral(st);
    st->pointerValid = 0;  // held on its side: not at the screen
    if (g->mState >= 22) {
        Results(p, st);  // the medal (22), the results menu (24)
        return;
    }
    if (g->mState != 7 && g->mState != 8) {
        return;
    }
    EGG::Vector3f pos = c->mpCollision->GetPos();
    f32 yaw = c->unk71C8;
    f32 fx = std::sin(yaw), fz = std::cos(yaw);
    // the target: the next scarecrow group along the course (the course runs to -z; the groups
    // line it, so this follows it), its nearest standing scarecrow once close; after the last
    // group, the goal
    f32 tx = 0.0f, tz = 0.0f;
    bool have = false;
    const int COW_SLOT_GROUP = 8;
    s32 gn = g->GetSlotNum(COW_SLOT_GROUP);
    RPCowKakasiGroup* gs = static_cast<RPCowKakasiGroup*>(g->GetSlotActor(COW_SLOT_GROUP));
    RPCowKakasiGroup* next = nullptr;
    f32 nextZ = 0.0f;
    for (s32 i = 0; gs != nullptr && i < gn; i++) {
        EGG::Vector3f gp = gs[i].GetPos();
        if (gs[i].mState == 2 || (gs[i].mNum != 0 && gs[i].mHits >= gs[i].mNum) || gp.z > pos.z - 15.0f) {
            continue;
        }
        if (next == nullptr || gp.z > nextZ) {
            next = &gs[i];
            nextZ = gp.z;
        }
    }
    if (next != nullptr) {
        EGG::Vector3f gp = next->GetPos();
        tx = gp.x;
        tz = gp.z;
        have = true;
        f32 bestD = 0.0f;
        bool near = false;
        s32 kn = g->GetSlotNum(COW_SLOT_KAKASI);
        RPCowKakasi* ks = static_cast<RPCowKakasi*>(g->GetSlotActor(COW_SLOT_KAKASI));
        for (s32 i = 0; ks != nullptr && i < kn; i++) {
            RPCowKakasi* k = &ks[i];
            if (k->mGroup != next || k->mpCollision == nullptr || k->mpCollision->mpOwner == nullptr) {
                continue;
            }
            EGG::Vector3f kp = k->mpCollision->GetPos();
            f32 dx = kp.x - pos.x, dz = kp.z - pos.z;
            f32 d = dx * dx + dz * dz;
            if (dx * fx + dz * fz < 5.0f) {
                continue;
            }
            if (!near || d < bestD) {
                near = true;
                bestD = d;
                tx = kp.x;
                tz = kp.z;
            }
        }
    } else if (g->GetSlotNum(COW_SLOT_GOAL) > 0) {
        RPCowGoal* goal = static_cast<RPCowGoal*>(g->GetSlotActor(COW_SLOT_GOAL));
        EGG::Vector3f gp = goal->GetPos();
        tx = gp.x;
        tz = gp.z;
        have = true;
    }
    // tilt forward (the remote's roll as the game reads it on its side) to run, twist to steer
    st->roll = 45.0f;
    if (have) {
        f32 err = Wrap180((std::atan2(tx - pos.x, tz - pos.z) - yaw) * 57.29578f);
        st->pitch = Clamp(err * 4.0f, -45.0f, 45.0f);
    }
    // jump the hurdle ahead: an upward jerk about 15 frames before reaching it
    s32 hn = g->GetSlotNum(COW_SLOT_HURDLE);
    RPCowHurdle* hs = static_cast<RPCowHurdle*>(g->GetSlotActor(COW_SLOT_HURDLE));
    for (s32 i = 0; hs != nullptr && i < hn && p.timer <= 0; i++) {
        if (hs[i].mpCollision == nullptr) {
            continue;
        }
        EGG::Vector3f hp = hs[i].mpCollision->GetPos();
        f32 dx = hp.x - pos.x, dz = hp.z - pos.z;
        f32 ahead = dx * fx + dz * fz, side = std::fabs(dx * fz - dz * fx);
        f32 v = c->mSpeed * 6.0f;
        if (side < 14.0f && ahead > 0.0f && ahead < v * 16.0f + 6.0f) {
            p.timer = 45;
            p.step = 3;
        }
    }
    if (p.step > 0) {
        st->accel[1] = -2.5f;
        p.step--;
    }
    if (p.timer > 0) {
        p.timer--;
    }
}


/* ---- Billiards (RPBilScene) ---- */

bool BilLive() {
    return CurrentScene() == RPSysSceneCreator::ESceneID_RPBilScene && RPBilGameMgr::sInstance != nullptr &&
           RPBilGameMgr::sInstance->mpStateMachine != nullptr && RPBilPlayerMgr::sInstance != nullptr &&
           RPBilPlayerMgr::sInstance->mpPlayers[0] != nullptr && RPBilBallMgr::sInstance != nullptr &&
           RPBilTable::sInstance != nullptr && RPBilCameraMgr::sInstance != nullptr &&
           RPBilCameraMgr::sInstance->GetCamera() != nullptr;
}

int BilOff() {
    RPBilBallMgr* bm = RPBilBallMgr::sInstance;
    int n = 0;
    for (s32 i = 1; i < bm->GetBallNum(); i++) {
        n += bm->GetBall(i)->GetState() == RPBilBall::STATE_POCKETED || bm->GetBall(i)->GetState() == RPBilBall::STATE_OUT;
    }
    return n;
}

int BilProbe(char* buf, int len) {
    RPBilPlayer* pl = RPBilPlayerMgr::sInstance->mpPlayers[0];
    const nw4r::math::VEC3& cue = RPBilBallMgr::sInstance->GetBall(0)->GetPos();
    std::snprintf(buf, len, "bil:%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(RPBilGameMgr::sInstance->mpStateMachine->GetState()),
                  static_cast<int>(pl->GetState()), static_cast<int>(pl->mShotCount), static_cast<int>(pl->mFoulCount),
                  BilOff(), static_cast<int>(std::floor(cue.x)), static_cast<int>(std::floor(cue.z)),
                  static_cast<int>(RPBilBallMgr::sInstance->GetLowestBall()));
    return 1;
}

f32 BilHeading(f32 dx, f32 dz) {
    return std::atan2(dx, dz) * 57.29578f;
}

// The heading (degrees, atan2(x, z)) to pot the lowest ball: the ghost-ball point for the
// pocket with the smallest cut; straight at the ball when no cut is under 60 degrees
f32 BilAim() {
    RPBilBallMgr* bm = RPBilBallMgr::sInstance;
    s32 lo = bm->GetLowestBall();
    const nw4r::math::VEC3& cue = bm->GetBall(0)->GetPos();
    if (lo <= 0 || lo >= bm->GetBallNum()) {
        return 0.0f;
    }
    const nw4r::math::VEC3& t = bm->GetBall(lo)->GetPos();
    f32 r = bm->GetBall(0)->mpPhysSphere->mParam.radius;
    f32 bestCut = 60.0f;
    f32 dx = t.x - cue.x, dz = t.z - cue.z;
    f32 best = BilHeading(dx, dz);
    for (s32 k = 0; k < 6; k++) {
        EGG::Vector3f pk = RPBilTable::sInstance->GetPocketPos(k);
        f32 px = pk.x - t.x, pz = pk.z - t.z;
        f32 pd = std::sqrt(px * px + pz * pz);
        if (pd < 1.0f) {
            continue;
        }
        f32 gx = t.x - px / pd * 2.0f * r, gz = t.z - pz / pd * 2.0f * r;
        f32 cx = gx - cue.x, cz = gz - cue.z;
        f32 cut = std::fabs(Wrap180(BilHeading(px, pz) - BilHeading(cx, cz)));
        if (cut >= bestCut) {
            continue;
        }
        // the cue ball's first contact on that line must be the lowest ball
        f32 cd = std::sqrt(cx * cx + cz * cz);
        EGG::Vector3f dir(cx / cd, 0.0f, cz / cd);
        s32 first = -1;
        EGG::Vector3f hit;
        if (!bm->RaycastObjectBalls(dir, &first, &hit) || first != lo) {
            continue;
        }
        bestCut = cut;
        best = BilHeading(cx, cz);
    }
    return best;
}

void BilPlay(Player& p, PortInputState* st) {
    RPBilPlayer* pl = RPBilPlayerMgr::sInstance->mpPlayers[0];
    RPBilCamera* cam = RPBilCameraMgr::sInstance->GetCamera();
    s32 gs = RPBilGameMgr::sInstance->mpStateMachine->GetState();
    Neutral(st);
    st->distance = 1.2f;
    if (gs == RPBilGameMgr::STATE_GAME_OVER) {
        Results(p, st);
        return;
    }
    s32 ps = pl->GetState();
    if (ps != RPBilPlayer::STATE_ADDRESS && ps != RPBilPlayer::STATE_STROKE) {
        p.vals[5] = 0.0f;  // shot sequence frames
    }
    switch (ps) {
    case RPBilPlayer::STATE_PLACE_BALL: {
        // ball in hand: try spots near the head string until one is allowed
        static const f32 SPOTS[4][2] = {{0.0f, 0.3f}, {-0.3f, 0.3f}, {0.3f, 0.3f}, {0.0f, 0.5f}};
        int k = (p.frame / 120) % 4;
        PointAt(p, st, SPOTS[k][0], SPOTS[k][1], false);
        if (!cam->IsActive() && p.frame % 120 > 40) {
            Tap(p, WPAD_BUTTON_A, 2, 30);
        }
        break;
    }
    case RPBilPlayer::STATE_AIM: {
        if (cam->IsActive()) {
            p.vals[4] = 0.0f;
            break;
        }
        const nw4r::math::VEC3& cp = cam->GetPosition();
        const nw4r::math::VEC3& ct = cam->GetTargetRef();
        f32 err = Wrap180(BilAim() - BilHeading(ct.x - cp.x, ct.z - cp.z));
        // the D-pad orbits the camera (LEFT: +, about 0.09 degrees a frame, ten times that
        // after 100 frames held); release to slow down near the aim
        if (std::fabs(err) > 0.12f && !(p.vals[4] > 95.0f && std::fabs(err) < 4.0f)) {
            st->buttons = err > 0.0f ? WPAD_BUTTON_LEFT : WPAD_BUTTON_RIGHT;
            p.vals[4] += 1.0f;
            p.timer = 0;
            PointAt(p, st, 0.0f, 0.6f, false);
            return;
        }
        p.vals[4] = 0.0f;
        // aimed: the pointer on the cue ball, then B
        f32 x = pl->mCueBallScreenPos.x / HalfW(), y = -pl->mCueBallScreenPos.y / HalfH();
        PointAt(p, st, x, y, true);
        if (pl->unk6C == 0 && std::fabs(err) <= 0.12f) {
            if (++p.timer > 8) {
                Tap(p, WPAD_BUTTON_B, 3, 30);
            }
        } else {
            p.timer = 0;
        }
        break;
    }
    case RPBilPlayer::STATE_ADDRESS:
    case RPBilPlayer::STATE_STROKE: {
        // B held: pull the remote back, then thrust it forward and let go of B
        f32 x = pl->mCueBallScreenPos.x / HalfW(), y = -pl->mCueBallScreenPos.y / HalfH();
        PointAt(p, st, x, y, true);
        int f = static_cast<int>(p.vals[5]);
        p.vals[5] += 1.0f;
        st->buttons = WPAD_BUTTON_B;
        st->distance = 1.35f;
        if (ps == RPBilPlayer::STATE_STROKE && f > 4) {
            f32 k = pl->mShotCount <= 1 ? 3.8f : 2.0f;
            st->accel[2] = -k;
            if (f >= 14) {
                st->buttons = 0;
            }
        }
        break;
    }
    default:
        PointAt(p, st, 0.0f, 0.6f, false);
        // the score popup and foul message close on A
        if (gs == RPBilGameMgr::STATE_FOUL || gs == RPBilGameMgr::STATE_SCORE) {
            Tap(p, WPAD_BUTTON_A, 2, 60);
        }
        break;
    }
    st->buttons |= p.held;
}

} // namespace

extern "C" int PortGameProbeMore(char* buf, int len) {
    if (WlyLive()) {
        return WlyProbe(buf, len);
    }
    if (BomLive()) {
        return BomProbe(buf, len);
    }
    if (PnpLive()) {
        return PnpProbe(buf, len);
    }
    if (TnkLive()) {
        return TnkProbe(buf, len);
    }
    if (FshLive()) {
        return FshProbe(buf, len);
    }
    if (CowLive()) {
        return CowProbe(buf, len);
    }
    if (BilLive()) {
        return BilProbe(buf, len);
    }
    return 0;
}

extern "C" int PortInputPlayHook(int chan, int frame, PortInputState* state) {
    if (chan != 0) {
        return 0;
    }
    Player& p = Begin(frame);
    if (WlyLive()) {
        WlyPlay(p, state);
    } else if (BomLive()) {
        BomPlay(p, state);
    } else if (PnpLive()) {
        PnpPlay(p, state);
    } else if (TnkLive()) {
        TnkPlay(p, state);
    } else if (FshLive()) {
        FshPlay(p, state);
    } else if (CowLive()) {
        CowPlay(p, state);
    } else if (BilLive()) {
        BilPlay(p, state);
    } else {
        return 0;
    }
    return 1;
}
