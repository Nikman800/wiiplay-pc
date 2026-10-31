#include <Pack/RPParty/RPTnkScene/RPTnkPlayerController.h>

#include <Pack/RPParty/RPTnkScene/RPTnkCursor.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkMine.h>
#include <Pack/RPParty/RPTnkScene/RPTnkShell.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTank.h>
#include <Pack/RPParty/RPTnkScene/RPTnkWeaponManager.h>

#include <egg/core/eggController.h>

using namespace nw4r::math;

extern VEC3 RP_vecEy;
// The common header currently declares this helper void. Its result selects
// candidate 0 or 1; the second argument is the reference direction.
extern BOOL RPSelectMoreAlignedVec(VEC3*, const VEC3*, const VEC3*, const VEC3*);

// The 0xA8-byte player entry starts after the parameter file's first word.
// Keep the complete entry: Reset copies it by value, including unused fields.
struct RPTnkPlayerParams {

    be_s32 unk00;
    be_f32 unk04;
    be_s32 maxMines;
    be_s32 unk0C;
    be_s32 unk10;
    be_f32 unk14;
    be_f32 unk18;
    be_f32 unk1C;
    be_s32 mineCooldown;
    be_s32 mineMoveDelay;
    be_f32 acceleration;
    be_f32 friction;
    be_f32 unk30;
    be_s32 unk34;
    be_s32 unk38;
    be_f32 unk3C;
    be_f32 unk40;
    be_f32 unk44;
    be_f32 unk48;
    be_s32 unk4C;
    be_f32 unk50;
    be_s32 unk54;
    be_f32 maxSpeed;
    be_f32 inputMin;
    be_f32 inputMax;
    be_f32 turnSpeed;
    be_f32 moveAngle;
    be_s32 unk6C;
    be_f32 unk70;
    be_s32 maxShells;
    be_f32 unk78;
    be_f32 unk7C;
    be_f32 unk80;
    be_s32 shellBounces;
    be_s32 unk88;
    be_s32 unk8C;
    be_s32 fireCooldown;
    be_f32 shellSpeed;
    be_f32 unk98;
    be_s32 unk9C;
    be_f32 unkA0;
    be_s32 fireMoveDelay;
};

struct RPTnkVectorBits {
    u32 words[3];
};

inline void SetPlayerVector(VEC3* pOut, f32 x, f32 y, f32 z) {
    pOut->x = x;
    pOut->y = y;
    pOut->z = z;
}

// param/TnkGameParam.bin: lives, then one record per tank type (0 = player; stage records follow).
struct RPTnkGameParamFile {
    s32 lives;
    RPTnkPlayerParams tanks[10];
};

inline RPTnkPlayerParams GetPlayerParams() {
    const void* pFile = RPTnkGameManager::sInstance->GetParam();
    return static_cast<const RPTnkGameParamFile*>(pFile)->tanks[0];
}

// This SDK routine is misidentified as a matrix conversion in the current
// symbol map. The original call operates on a two-component vector.
RPTnkPlayerController::RPTnkPlayerController(RPTnkTank* pTank)
    : mpTank(pTank) {}

RPTnkPlayerController::~RPTnkPlayerController() {}

BOOL RPTnkPlayerController::GetMoveInput(VEC2* pOut, s32 player) {
    EGG::CoreController* pController =
        EGG::CoreControllerMgr::instance()->getNthController(player);
    BOOL freestyle;
    if (static_cast<u32>(pController->getDevType()) == EGG::cDEV_FREESTYLE) {
        pOut->x = pController->getCoreStatus()->getFSStickX();
        pOut->y = pController->getCoreStatus()->getFSStickY();
        freestyle = TRUE;
    } else {
        VEC2 input(0.0f, 0.0f);
        if (pController->down(EGG::cCORE_BUTTON_UP)) {
            input.y += 1.0f;
        }
        if (pController->down(EGG::cCORE_BUTTON_DOWN)) {
            input.y -= 1.0f;
        }
        if (pController->down(EGG::cCORE_BUTTON_LEFT)) {
            input.x -= 1.0f;
        }
        if (pController->down(EGG::cCORE_BUTTON_RIGHT)) {
            input.x += 1.0f;
        }
        if (input.x != 0.0f || input.y != 0.0f) {
            nw4r::math::VEC2Normalize(
                &input, &input);
        }
        pOut->x = input.x;
        pOut->y = input.y;
        freestyle = FALSE;
    }
    return freestyle;
}

void RPTnkPlayerController::Reset() {
    RPTnkPlayerParams params;
    params = GetPlayerParams();
    mParams[0] = params.inputMin;
    mParams[1] = params.inputMax;
    mParams[2] = params.acceleration;
    mParams[3] = params.friction;
    mMaxSpeed = params.maxSpeed;
    unk18 = params.moveAngle;
    mTurnSpeed = params.turnSpeed;
    mFireCooldown = params.fireCooldown;
    mMaxShells = params.maxShells;
    mShellBounces = params.shellBounces;
    mShellSpeed = params.shellSpeed;
    mMineCooldownInit = params.fireMoveDelay;
    unk34 = params.mineCooldown;
    mMaxMines = params.maxMines;
    unk3C = params.mineMoveDelay;
    unk40 = 0.0f;
    unk44 = 1;
    mShellTimer = 0;
    mMineTimer = 0;
    unk50 = 0;
    mMineHeld = false;
    mFired = false;
    mLaidMine = false;
}

void RPTnkPlayerController::AimAtCursor(RPAxes* pTurret, const VEC3& rPos, s32 player) {
    VEC3 cursor = RPTnkCursorManager::sInstance->GetCursor(player)->GetPos();
    VEC3 dir;
    dir.x = cursor.x - rPos.x;
    dir.y = 0.0f;
    dir.z = cursor.z - rPos.z;
    f32 length = VEC3Len(&dir);
    if (length != 0.0f) {
        f32 invLength = 1.0f / length;
        pTurret->z.x = dir.x * invLength;
        pTurret->z.y = dir.y * invLength;
        pTurret->z.z = dir.z * invLength;
        pTurret->y.x = RP_vecEy.x;
        pTurret->y.y = RP_vecEy.y;
        pTurret->y.z = RP_vecEy.z;
        VEC3Cross(&pTurret->x, &pTurret->y, &pTurret->z);
    }
}

void RPTnkPlayerController::TurnBodyStep(RPAxes* pRot, const VEC3& rDir) {
    VEC3 forward = pRot->z;
    VEC3 plus;
    plus.x = pRot->x.x * mTurnSpeed + forward.x;
    plus.y = pRot->x.y * mTurnSpeed + forward.y;
    plus.z = pRot->x.z * mTurnSpeed + forward.z;
    VEC3Normalize(&plus, &plus);
    VEC3 minus;
    minus.x = forward.x - pRot->x.x * mTurnSpeed;
    minus.y = forward.y - pRot->x.y * mTurnSpeed;
    minus.z = forward.z - pRot->x.z * mTurnSpeed;
    VEC3Normalize(&minus, &minus);
    VEC3 selected;
    RPSelectMoreAlignedVec(&selected, &rDir, &minus, &plus);
    RPSelectMoreAlignedVec(&forward, &forward, &selected, &rDir);
    VEC3Normalize(&forward, &forward);
    pRot->z.x = forward.x;
    pRot->z.y = forward.y;
    pRot->z.z = forward.z;
    pRot->y.x = RP_vecEy.x;
    pRot->y.y = RP_vecEy.y;
    pRot->y.z = RP_vecEy.z;
    VEC3Cross(&pRot->x, &pRot->y, &pRot->z);
}

void RPTnkPlayerController::TurnBodyTowards(RPAxes* pRot, s32* pSign, const VEC3& rDir) {
    VEC3 forward = pRot->z;
    VEC3 backward(-forward.x, -forward.y, -forward.z);
    VEC3 selected;
    if (RPSelectMoreAlignedVec(&selected, &rDir, &forward, &backward) == 0) {
        TurnBodyStep(pRot, rDir);
        *pSign = 1;
    } else {
        VEC3 plus;
        plus.x = pRot->x.x * mTurnSpeed + backward.x;
        plus.y = pRot->x.y * mTurnSpeed + backward.y;
        plus.z = pRot->x.z * mTurnSpeed + backward.z;
        VEC3Normalize(&plus, &plus);
        VEC3 minus;
        minus.x = backward.x - pRot->x.x * mTurnSpeed;
        minus.y = backward.y - pRot->x.y * mTurnSpeed;
        minus.z = backward.z - pRot->x.z * mTurnSpeed;
        VEC3Normalize(&minus, &minus);
        VEC3 candidate;
        RPSelectMoreAlignedVec(&candidate, &rDir, &minus, &plus);
        RPSelectMoreAlignedVec(&backward, &backward, &candidate, &rDir);
        forward.x = -backward.x;
        forward.y = -backward.y;
        forward.z = -backward.z;
        VEC3Normalize(&forward, &forward);
        pRot->z.x = forward.x;
        pRot->z.y = forward.y;
        pRot->z.z = forward.z;
        pRot->y.x = RP_vecEy.x;
        pRot->y.y = RP_vecEy.y;
        pRot->y.z = RP_vecEy.z;
        VEC3Cross(&pRot->x, &pRot->y, &pRot->z);
        *pSign = -1;
    }
}

void RPTnkPlayerController::CalcMoveVelocity(RPAxes* pRot, VEC3* pVel, s32 player) {
    VEC2 input;
    BOOL freestyle = GetMoveInput(&input, player);
    VEC3 dir;
    dir.x = input.x;
    dir.y = 0.0f;
    dir.z = -input.y;
    f32 magnitude = VEC3Len(&dir);
    magnitude = 0.0f > magnitude ? 0.0f : magnitude;
    magnitude = 1.0f < magnitude ? 1.0f : magnitude;
    if (unk50 > 0) {
        unk40 = 0.0f;
    } else if (magnitude >= mParams[0]) {
        f32 invMagnitude = 1.0f / magnitude;
        dir.x *= invMagnitude;
        dir.y *= invMagnitude;
        dir.z *= invMagnitude;
        TurnBodyTowards(pRot, &unk44, dir);
        f32 angle = freestyle ? unk18 : 46.0f;
        VEC3 forward(pRot->z.x * unk44, pRot->z.y * unk44, pRot->z.z * unk44);
        f32 alignment = VEC3Dot(&forward, &dir);
        if (alignment < CosFIdx(0.7111111f * angle)) {
            unk40 *= mParams[3];
        } else {
            f32 maxSpeed;
            f32 inputRange = mParams[1] - mParams[0];
            if (inputRange != 0.0f) {
                f32 factor = (magnitude - mParams[0]) / inputRange;
                factor = 0.0f > factor ? 0.0f : factor;
                factor = 1.0f < factor ? 1.0f : factor;
                maxSpeed = factor * mMaxSpeed;
            } else {
                maxSpeed = mMaxSpeed;
            }
            unk40 += mParams[2];
            unk40 = maxSpeed <= unk40 ? maxSpeed : unk40;
        }
    } else {
        unk40 *= mParams[3];
    }
    f32 speed = unk40 * unk44;
    pVel->x = pRot->z.x * speed;
    pVel->y = pRot->z.y * speed;
    pVel->z = pRot->z.z * speed;
    if (--unk50 < 0) {
        unk50 = 0;
    }
}

void RPTnkPlayerController::HandleFireInput(f32 muzzleDist, const RPAxes* pTurret,
                                          const VEC3& rPos, s32 player) {
    VEC3 mineAt;
    VEC3 shellAt;
    RPTnkVectorBits minePos;
    RPTnkVectorBits target;
    RPTnkVectorBits shellPos;
    RPTnkVectorBits dir;
    mFired = false;
    mLaidMine = false;
    u32 trigger = EGG::CoreControllerMgr::instance()->getNthController(player)
                      ->getCoreStatus()->getTrigger();
    if (trigger & EGG::cCORE_BUTTON_A) {
        if (!mMineHeld && mMineTimer == 0 &&
            mMaxMines > RPTnkWeaponManager::sInstance->GetMinePool()->GetMineNum(player)) {
            SetPlayerVector(&mineAt, rPos.x, 0.0f, rPos.z);
            // NONMATCHING-DEBT: the original copies mineAt as words through memory (stfs, then
            // lwz/stw into both copies). VEC3 locals (`target = mineAt`) forward the floats instead
            // (stfs straight into the copies, 0.9857); a by-value VEC3 helper does the same. Kept
            // as RPTnkVectorBits word copies, see docs/NONMATCHING.md (VEC3 word copies).
            target = *reinterpret_cast<const RPTnkVectorBits*>(&mineAt);
            minePos = *reinterpret_cast<const RPTnkVectorBits*>(&mineAt);
            RPTnkWeaponManager::sInstance->LayMine(0.0f, mpTank,
                reinterpret_cast<VEC3*>(&minePos), reinterpret_cast<const VEC3*>(&target), player);
            EGG::CoreControllerMgr::instance()->getNthController(static_cast<u16>(player))
                ->startSimpleRumble(0x0FFFFFFF, 16, 32);
            mpTank->PlayLayMineSe();
            mLaidMine = true;
            mMineTimer = unk34;
            unk50 = unk3C;
        }
        mMineHeld = true;
    } else {
        if (trigger & EGG::cCORE_BUTTON_B) {
            shellAt.x = muzzleDist * pTurret->z.x + rPos.x;
            shellAt.y = muzzleDist * pTurret->z.y + rPos.y;
            shellAt.z = muzzleDist * pTurret->z.z + rPos.z;
            if (mShellTimer == 0 &&
                mMaxShells > RPTnkWeaponManager::sInstance->GetShellPool()->GetShellNum(player)) {
                // NONMATCHING-DEBT: volatile pins the interleaved word copies (turret row _20/_21 and
                // shellAt.x loaded before the first stw to dir, _22 loaded after dir.y is stored), so
                // only r0/r4/r8/r11 are live and this/pTurret/rPos get r29/r30/r28. The natural
                // `VEC3 pos = shellAt; VEC3 dir = row 2; FireShell(mShellSpeed, mpTank, &pos, &dir, ...)`
                // hoists all six loads (r29/r12/r11 temps) and rotates the saved registers: 16
                // instructions. Tried: both copy orders, VectorBits vs VEC3 locals, pTank/speed locals,
                // by-value and const-ref FireShell wrappers (static and member), axes-pointer reads.
                // Rule check (2026-10-29): blocked by the const turret pointer. RPTnkAi_4's same
                // copy matches from a by-value-parameter member inline because its pTurret is a
                // non-const RPAxes* (dummy in the worst-case set): the stores into the by-value
                // copies (address-taken locals) get mem-raw edges to the row loads, which gives the
                // interleave. Here pTurret is `const RPAxes*` (mangled PC6RPAxes): its dummy stays out, so no
                // store orders the row loads and the scheduler hoists them (best natural: per-word
                // dir/shellPos copies interleaved, 153/164). The this-loads (mpTank, mShellSpeed,
                // mShellBounces) after the stores are natural (non-const this vs address-taken locals).
                u32 y, sz, x, sy, sx, z;
                x = *reinterpret_cast<const volatile u32*>(&pTurret->z.x);
                y = *reinterpret_cast<const volatile u32*>(&pTurret->z.y);
                sx = reinterpret_cast<const volatile RPTnkVectorBits*>(&shellAt)->words[0];
                reinterpret_cast<volatile RPTnkVectorBits*>(&dir)->words[0] = x;
                sy = reinterpret_cast<const volatile RPTnkVectorBits*>(&shellAt)->words[1];
                reinterpret_cast<volatile RPTnkVectorBits*>(&dir)->words[1] = y;
                sz = reinterpret_cast<const volatile RPTnkVectorBits*>(&shellAt)->words[2];
                z = *reinterpret_cast<const volatile u32*>(&pTurret->z.z);
                reinterpret_cast<volatile RPTnkVectorBits*>(&dir)->words[2] = z;
                reinterpret_cast<volatile RPTnkVectorBits*>(&shellPos)->words[0] = sx;
                reinterpret_cast<volatile RPTnkVectorBits*>(&shellPos)->words[1] = sy;
                reinterpret_cast<volatile RPTnkVectorBits*>(&shellPos)->words[2] = sz;
                RPTnkTank* pTank = *reinterpret_cast<RPTnkTank* const volatile*>(&mpTank);
                f32 shellSpeed = *reinterpret_cast<const volatile f32*>(&mShellSpeed);
                RPTnkWeaponManager::sInstance->FireShell(shellSpeed, pTank,
                    reinterpret_cast<VEC3*>(&shellPos), reinterpret_cast<VEC3*>(&dir),
                    player, mShellBounces, 0, 0);
                EGG::CoreControllerMgr::instance()->getNthController(static_cast<u16>(player))
                    ->startSimpleRumble(0x0FFFFFFF, 6, 32);
                mpTank->PlayFireSeByType();
                mpTank->PlayShotEffect();
                mFired = true;
                mShellTimer = mFireCooldown;
                unk50 = mMineCooldownInit;
            }
        }
        mMineHeld = false;
    }
    if (--mShellTimer < 0) {
        mShellTimer = 0;
    }
    if (--mMineTimer < 0) {
        mMineTimer = 0;
    }
}

f32 RPTnkPlayerController::GetMaxSpeed() const {
    return mMaxSpeed;
}
