#include <Pack/RPParty/RPTnkScene/RPTnkAi.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTank.h>
#include <Pack/RPParty/RPTnkScene/RPTnkShell.h>
#include <Pack/RPParty/RPTnkScene/RPTnkMine.h>

#include <revolution/MTX.h>

#pragma opt_unroll_loops off

using nw4r::math::VEC3;

// The remainder with PowerPC's result for a zero divisor (a zero timer range in the
// parameter file; the host form is in port/host/include/decomp/compat.h)
#if !defined(PORT_PPC_MOD)
#define PORT_PPC_MOD(x, d) ((x) % (d))
#endif

struct MoveGameParams {
    be_s32 p00; be_f32 p04; be_s32 p08; be_s32 p0C; be_s32 p10;
    be_f32 p14; be_f32 p18; be_f32 p1C; be_s32 p20; be_s32 p24;
    be_f32 p28; be_f32 p2C; be_f32 p30; be_s32 p34; be_s32 p38;
    be_f32 p3C; be_f32 p40; be_f32 p44; be_f32 p48; be_s32 p4C;
    be_f32 p50; be_s32 p54; be_f32 p58; be_f32 p5C; be_f32 p60;
    be_f32 p64; be_f32 p68; be_s32 p6C; be_f32 p70; be_s32 p74;
    be_f32 p78; be_f32 p7C; be_f32 p80; be_s32 p84; be_s32 p88;
    be_s32 p8C; be_s32 p90; be_f32 p94; be_f32 p98; be_s32 p9C;
    be_f32 pA0; be_s32 pA4;

    static MoveGameParams Get(s32 type);
};

// The game parameter file: a header word, then one movement block per enemy type
struct MoveGameParamData {
    s32 lives;
    MoveGameParams moves[10];
};

inline MoveGameParams MoveGameParams::Get(s32 type) {
    void* data = RPTnkGameManager::sInstance->GetParam();
    return static_cast<MoveGameParamData*>(data)->moves[type];
}

extern VEC3 RP_vecEy;
extern VEC3 RP_vecEz;
extern VEC3 RP_vecZero;
extern BOOL RPSelectMoreAlignedVec(VEC3*, const VEC3*, const VEC3*, const VEC3*);
extern u32 RP_randSeedX;
extern u32 RP_randSeedY;


// The framework's xorshift-style generator: an LCG word and a 17-bit LFSR word
inline void AdvanceRandom() {
    RP_randSeedX = RP_randSeedX * 0x41C64E6D + 0x3039;
    if (RP_randSeedY & 1) RP_randSeedY ^= 0x11020;
    RP_randSeedY >>= 1;
}

struct AiMoveAccess {
    static VEC3 GetTankPos(RPTnkTank* tank) { return tank->GetPos(); }
    static BOOL IsAlive(RPTnkTank* tank) { return tank->mAlive != 0; }
    static f32 RandomRange(f32 minimum, f32 maximum) {
        if (minimum == maximum) return minimum;
        AdvanceRandom();
        f32 random = (f32)((RP_randSeedX ^ RP_randSeedY) & 0x7FFFFF) * 0.0000001192093f;
        return __fmadds(maximum - minimum, random, minimum);
    }
    static f32 ProbeDistance(f32 dist) { f32 scale = 3.0f; return dist * scale; }
};

static inline VEC3 AiMoveGetTargetDir(const VEC3& rPos) {
    VEC3 target;
    if (RPTnkGameManager::sInstance->GetModeIdx() == 0) {
        target = AiMoveAccess::GetTankPos(RPTnkTankManager::sInstance->GetPlayerTank(0));
    } else {
        RPTnkTank* second = RPTnkTankManager::sInstance->GetPlayerTank(1);
        RPTnkTank* first = RPTnkTankManager::sInstance->GetPlayerTank(0);
        BOOL firstAlive = AiMoveAccess::IsAlive(first);
        BOOL secondAlive = AiMoveAccess::IsAlive(second);
        if (!secondAlive) {
            target = AiMoveAccess::GetTankPos(first);
        } else if (!firstAlive && secondAlive) {
            target = AiMoveAccess::GetTankPos(second);
        } else {
            VEC3 firstPos = first->GetPos();
            VEC3 secondPos = second->GetPos();
            VEC3 firstDelta(rPos.x - firstPos.x, 0.0f, rPos.z - firstPos.z);
            VEC3 secondDelta(rPos.x - secondPos.x, 0.0f, rPos.z - secondPos.z);
            f32 secondDist = PSVECMag((Vec*)&secondDelta);
            f32 firstDist = PSVECMag((Vec*)&firstDelta);
            if (firstDist <= secondDist) target = firstPos;
            else target = secondPos;
        }
    }
    VEC3 dir(0.0f, 0.0f, 1.0f);
    dir.x = target.x - rPos.x;
    dir.y = 0.0f;
    dir.z = target.z - rPos.z;
    PSVECNormalize((Vec*)&dir, (Vec*)&dir);
    return dir;
}

inline void RPTnkAiMove::ClearShellThreats() {
    mShellThreatNum = 0;
    s32 i = 0;
    do {
        mShellThreats[i].mPos = RP_vecZero;
        mShellThreats[i].mDir = RP_vecZero;
        mShellThreats[i].mPerpendicularDir = RP_vecZero;
        mShellThreats[i].mDist = 0.0f;
        mShellThreats[i].mPerpendicularDist = 0.0f;
        i++;
    } while (i < 4);
}

inline void RPTnkAiMove::ClearMineThreats() {
    mMineThreatNum = 0;
    s32 i = 0;
    do {
        mMineThreats[i].mPos = RP_vecZero;
        mMineThreats[i].mDist = 0.0f;
        mMineThreats[i].mDir = RP_vecZero;
        i++;
    } while (i < 4);
}

static RPTnkAiMove::StateFunc sApproach = &RPTnkAiMove::StateApproach;
static RPTnkAiMove::StateFunc sEvade = &RPTnkAiMove::StateEvade;
static RPTnkAiMove::StateFunc sKeepDirection = &RPTnkAiMove::StateKeepDirection;

RPTnkAiMove::RPTnkAiMove(RPTnkTank* pTank) {
    mpProbe = new RPTnkAiProbe;
    mpProbe->Create();
    mpTank = pTank;
    mStateFuncs[0] = sApproach;
    mStateFuncs[1] = sEvade;
    mStateFuncs[2] = sKeepDirection;
}

RPTnkAiMove::~RPTnkAiMove() {}

void RPTnkAiMove::RotateBodyStep(RPAxes* pRot, const VEC3& rDir) {
    VEC3 forward = pRot->z;
    VEC3 positive;
    positive.x = pRot->x.x * mParams.mTurnStep + forward.x;
    positive.y = pRot->x.y * mParams.mTurnStep + forward.y;
    positive.z = pRot->x.z * mParams.mTurnStep + forward.z;
    PSVECNormalize((Vec*)&positive, (Vec*)&positive);
    VEC3 negative;
    negative.x = -(pRot->x.x * mParams.mTurnStep - forward.x);
    negative.y = -(pRot->x.y * mParams.mTurnStep - forward.y);
    negative.z = -(pRot->x.z * mParams.mTurnStep - forward.z);
    PSVECNormalize((Vec*)&negative, (Vec*)&negative);
    VEC3 selected;
    RPSelectMoreAlignedVec(&selected, &rDir, &negative, &positive);
    RPSelectMoreAlignedVec(&forward, &forward, &selected, &rDir);
    PSVECNormalize((Vec*)&forward, (Vec*)&forward);
    pRot->z.x = forward.x;
    pRot->z.y = forward.y;
    pRot->z.z = forward.z;
    pRot->y.x = RP_vecEy.x;
    pRot->y.y = RP_vecEy.y;
    pRot->y.z = RP_vecEy.z;
    PSVECCrossProduct((Vec*)&pRot->y, (Vec*)&pRot->z, (Vec*)&pRot->x);
}

void RPTnkAiMove::TurnTowardDirection(RPAxes* pRot, s32* pSign,
                                     const VEC3& rDir) {
    VEC3 forward = pRot->z;
    VEC3 backward(-forward.x, -forward.y, -forward.z);
    VEC3 selected;
    if (RPSelectMoreAlignedVec(&selected, &rDir, &forward, &backward) == 0) {
        RotateBodyStep(pRot, rDir);
        *pSign = 1;
    } else {
        VEC3 positive;
        positive.x = pRot->x.x * mParams.mTurnStep + backward.x;
        positive.y = pRot->x.y * mParams.mTurnStep + backward.y;
        positive.z = pRot->x.z * mParams.mTurnStep + backward.z;
        PSVECNormalize((Vec*)&positive, (Vec*)&positive);
        VEC3 negative;
        negative.x = -(pRot->x.x * mParams.mTurnStep - backward.x);
        negative.y = -(pRot->x.y * mParams.mTurnStep - backward.y);
        negative.z = -(pRot->x.z * mParams.mTurnStep - backward.z);
        PSVECNormalize((Vec*)&negative, (Vec*)&negative);
        VEC3 turn;
        RPSelectMoreAlignedVec(&turn, &rDir, &negative, &positive);
        RPSelectMoreAlignedVec(&backward, &backward, &turn, &rDir);
        forward.x = -backward.x;
        forward.y = -backward.y;
        forward.z = -backward.z;
        PSVECNormalize((Vec*)&forward, (Vec*)&forward);
        pRot->z.x = forward.x;
        pRot->z.y = forward.y;
        pRot->z.z = forward.z;
        pRot->y.x = RP_vecEy.x;
        pRot->y.y = RP_vecEy.y;
        pRot->y.z = RP_vecEy.z;
        PSVECCrossProduct((Vec*)&pRot->y, (Vec*)&pRot->z, (Vec*)&pRot->x);
        *pSign = -1;
    }
}

void RPTnkAiMove::StateApproach() {
    f32 maxAngle = mParams.mMaxAngle;
    f32 minAngle = -maxAngle;
    VEC3 direction = mDirHistory[0];
    VEC3 toTarget = AiMoveGetTargetDir(mPos);
    direction.x += toTarget.x * mParams.mTargetWeight;
    direction.z += toTarget.z * mParams.mTargetWeight;
    if (PSVECMag((Vec*)&direction) != 0.0f) {
        PSVECNormalize((Vec*)&direction, (Vec*)&direction);
    } else {
        direction = mDirHistory[0];
    }
    const VEC3& position = mPos;
    s32 steps = mParams.mProbeSteps;
    SetupProbe(position, direction, steps);
    RPTnkAiProbe::Result result;
    mpProbe->Sweep(&result);
    if (result.mFlags & 1) {
        direction.x += result.mHitNormal.x;
        direction.y = 0.0f;
        direction.z += result.mHitNormal.z;
        if (PSVECMag((Vec*)&direction) == 0.0f) direction = result.mHitNormal;
        else PSVECNormalize((Vec*)&direction, (Vec*)&direction);
    }
    f32 tangent = nw4r::math::TanDeg(AiMoveAccess::RandomRange(minAngle, maxAngle));
    VEC3 perturbed;
    perturbed.x = mSide.x * tangent + direction.x;
    perturbed.y = mSide.y * tangent + direction.y;
    perturbed.z = mSide.z * tangent + direction.z;
    PSVECNormalize((Vec*)&perturbed, (Vec*)&perturbed);
    PushDirectionHistory(perturbed);
    AverageDirectionHistory();
    unk1F8 = true;
}

void RPTnkAiMove::StateEvade() {
    f32 weights[4] = {1.0f, 0.8f, 0.6f, 0.4f};
    VEC3 direction = RP_vecZero;
    if (mShellThreatNum > 0) {
        s32 i = 0;
        do {
            direction.x += weights[i] * mShellThreats[i].mPerpendicularDir.x;
            direction.z += weights[i] * mShellThreats[i].mPerpendicularDir.z;
            i++;
        } while (i < mShellThreatNum);
        i = 0;
        do {
            VEC3 away;
            away.x = mPos.x - mShellThreats[i].mPos.x;
            away.y = 0.0f;
            away.z = mPos.z - mShellThreats[i].mPos.z;
            PSVECNormalize((Vec*)&away, (Vec*)&away);
            direction.x += away.x * weights[i];
            direction.z += away.z * weights[i];
            i++;
        } while (i < mShellThreatNum);
    }
    if (PSVECMag((Vec*)&direction) != 0.0f) {
        PSVECNormalize((Vec*)&direction, (Vec*)&direction);
        PushDirectionHistory(direction);
        PushDirectionHistory(direction);
    }
    if (mMineThreatNum > 0) {
        s32 i = 0;
        do {
            direction.x += weights[i] * mMineThreats[i].mDir.x;
            direction.z += weights[i] * mMineThreats[i].mDir.z;
            i++;
        } while (i < mMineThreatNum);
        if (PSVECMag((Vec*)&direction) != 0.0f) {
            PSVECNormalize((Vec*)&direction, (Vec*)&direction);
            PushDirectionHistory(direction);
            PushDirectionHistory(direction);
        }
    }
    AverageDirectionHistory();
    unk1F8 = true;
}

void RPTnkAiMove::StateKeepDirection() {
    AverageDirectionHistory();
    unk1F8 = true;
    // Written out here: as a header inline the loop counter and pointer swap registers
    s32 count = mMineThreatNum;
    if (count == 0) {
        unk1F9 = false;
    } else {
        bool nearby = false;
        s32 i = 0;
        do {
            if (mMineThreats[i].mDist < 16.0f) nearby = true;
        } while (++i < count);
        if (!nearby) unk1F9 = false;
    }
}

void RPTnkAiMove::CalcVelocity(RPAxes* pRot, nw4r::math::VEC3* pVel) {
    if (mpTank->GetAiController()->GetFlags() & 1) {
        unk16C = 0.0f;
    } else if (unk1F8) {
        TurnTowardDirection(pRot, &unk1F4, mDir);
        VEC3 forward;
        forward.x = pRot->z.x * unk1F4;
        forward.y = pRot->z.y * unk1F4;
        forward.z = pRot->z.z * unk1F4;
        if (nw4r::math::VEC3Dot(&forward, &mDir) < nw4r::math::CosDeg(mParams.mTurnAngle))
            unk16C *= mParams.mDecel;
        else unk16C += mParams.mAccel;
    } else {
        unk16C *= mParams.mDecel;
    }
    unk16C = mParams.mSpeed <= unk16C ? mParams.mSpeed : unk16C;
    f32 speed = unk16C * unk1F4;
    pVel->x = pRot->z.x * speed;
    pVel->y = pRot->z.y * speed;
    pVel->z = pRot->z.z * speed;
}

void RPTnkAiMove::LoadParams(s32 type) {
    MoveGameParams params;
    params = MoveGameParams::Get(type);
    mParams.mAccel = params.p28;
    mParams.mDecel = params.p2C;
    mParams.mSpeed = params.p58;
    mParams.mTurnAngle = params.p68;
    mParams.mTurnStep = params.p64;
    mParams.mMaxAngle = params.p30;
    mParams.mTargetWeight = params.p50;
    mParams.mTimerMin = params.p38;
    mParams.mTimerMax = params.p34;
    mParams.mProbeSteps = params.p6C;
    mParams.mShellRangeEnemy = params.p40;
    mParams.mShellRangePlayer = params.p48;
    mParams.mMineRadiusEnemy = params.p3C;
    mParams.mMineRadiusPlayer = params.p44;
    mParams.mSetFlag2 = params.p4C;
    mParams.mHistoryNum = params.p54;
}

void RPTnkAiMove::ResetMovement() {
    unk16C = 0.0f;
    SetDirection(RP_vecEz);
    unk1F4 = 1;
    unk1F8 = false;
    unk1F9 = false;
    mTimer = 0;
    mState = 0;
    mpProbe->Reset();
}

void RPTnkAiMove::SetDirection(nw4r::math::VEC3 dir) {
    FillDirectionHistory(dir);
    mDir = dir;
    PSVECNormalize((Vec*)&mDir, (Vec*)&mDir);
    PSVECNormalize((Vec*)&mDir, (Vec*)&mDir);
    PSVECCrossProduct((Vec*)&RP_vecEy, (Vec*)&mDir, (Vec*)&mSide);
}

void RPTnkAiMove::Update(RPAxes* pRot, nw4r::math::VEC3* pVel,
                         const nw4r::math::VEC3& rPos) {
    mPos.x = rPos.x;
    mPos.y = rPos.y;
    mPos.z = rPos.z;
    mTimer--;
    if (mTimer <= 0) {
        ClearShellThreats();
        RPTnkShellPool::sInstance->DetectIncomingShells(this);
        ClearMineThreats();
        RPTnkMinePool::sInstance->AddTankAvoidance(this);
        if (unk1F9) {
            mState = 2;
            if (mParams.mSetFlag2 != 0) mpTank->GetAiController()->SetFlags(2, TRUE);
        } else if (mShellThreatNum > 0 || mMineThreatNum > 0) {
            mState = 1;
            if (mParams.mSetFlag2 != 0) mpTank->GetAiController()->SetFlags(2, TRUE);
        } else {
            mState = 0;
            mpTank->GetAiController()->SetFlags(2, FALSE);
        }
        (this->*mStateFuncs[mState])();
        PSVECNormalize((Vec*)&mDir, (Vec*)&mDir);
        PSVECCrossProduct((Vec*)&RP_vecEy, (Vec*)&mDir, (Vec*)&mSide);
        s32 range = mParams.mTimerMax - mParams.mTimerMin;
        AdvanceRandom();
        s32 random = ((RP_randSeedX ^ RP_randSeedY) >> 4) & 0xFFFF;
        mTimer = mParams.mTimerMin + PORT_PPC_MOD(random, range);
    }
    CalcVelocity(pRot, pVel);
}

BOOL RPTnkAiMove::FindClearDirection(f32 dist, const nw4r::math::VEC3& rPos) {
    VEC3 candidate = mDir;
    VEC3 probed = mDir;
    f32 best = 0.0f;
    s32 steps = (s32)(AiMoveAccess::ProbeDistance(dist) / mParams.mSpeed);
    RPTnkAiProbe::Result result;
    probed.x = mDir.x;
    probed.y = mDir.y;
    probed.z = mDir.z;
    SetupProbe(rPos, probed, steps);
    mpProbe->Sweep(&result);
    if (result.mFlags == 0) {
        candidate = probed;
    } else {
        if (result.mFlags == 1 && result.mTime > dist && result.mTime > best) {
            candidate = probed;
            best = result.mTime;
        }
        probed.x = -mDir.x;
        probed.z = -mDir.z;
        SetupProbe(rPos, probed, steps);
        mpProbe->Sweep(&result);
        if (result.mFlags == 0) {
            candidate = probed;
        } else {
            if (result.mFlags == 1 && result.mTime > dist && result.mTime > best) {
                candidate = probed;
                best = result.mTime;
            }
            probed = mSide;
            SetupProbe(rPos, probed, steps);
            mpProbe->Sweep(&result);
            if (result.mFlags == 0) {
                candidate = probed;
            } else {
                if (result.mFlags == 1 && result.mTime > dist && result.mTime > best) {
                    candidate = probed;
                    best = result.mTime;
                }
                probed.x = -mSide.x;
                probed.z = -mSide.z;
                SetupProbe(rPos, probed, steps);
                mpProbe->Sweep(&result);
                if (result.mFlags == 0) candidate = probed;
                else {
                    if (result.mFlags == 1 && result.mTime > dist && result.mTime > best) {
                        candidate = probed;
                        best = result.mTime;
                    }
                    if (best < dist) return FALSE;
                }
            }
        }
    }
    ResetDirectionHistory(candidate);
    return TRUE;
}

void RPTnkAiMove::AddShellThreat(const ShellThreat& rThreat) {
    BOOL added = FALSE;
    s32 count = mShellThreatNum;
    if (count == 0) {
        mShellThreats[0] = rThreat;
        added = TRUE;
    } else {
        s32 i = 0;
        do {
            BOOL nearer = mShellThreats[i].mDist > rThreat.mDist;
            if (nearer) {
                s32 j = 3;
                if (i != j) {
                    do {
                        mShellThreats[j] = mShellThreats[j - 1];
                        j--;
                    } while (j > i);
                }
                mShellThreats[i] = rThreat;
                added = TRUE;
                break;
            }
            i++;
        } while (i < count);
        if (!added && mShellThreatNum < 4) {
            mShellThreats[mShellThreatNum] = rThreat;
            added = TRUE;
        }
    }
    if (added) {
        mShellThreatNum++;
        if (mShellThreatNum > 4) {
            mShellThreatNum = 4;
        }
    }
}

void RPTnkAiMove::AddMineThreat(const MineThreat& rThreat) {
    s32 count = mMineThreatNum;
    BOOL added = FALSE;
    if (count == 0) {
        mMineThreats[0] = rThreat;
        added = TRUE;
    } else {
        s32 i = 0;
        do {
            BOOL nearer = mMineThreats[i].mDist > rThreat.mDist;
            if (nearer) {
                s32 j = 3;
                if (i != j) {
                    do {
                        mMineThreats[j] = mMineThreats[j - 1];
                        j--;
                    } while (j > i);
                }
                mMineThreats[i] = rThreat;
                added = TRUE;
                break;
            }
            i++;
        } while (i < count);
        if (!added && mMineThreatNum < 4) {
            mMineThreats[mMineThreatNum] = rThreat;
            added = TRUE;
        }
    }
    if (added) {
        mMineThreatNum++;
        if (mMineThreatNum > 4) {
            mMineThreatNum = 4;
        }
    }
}
