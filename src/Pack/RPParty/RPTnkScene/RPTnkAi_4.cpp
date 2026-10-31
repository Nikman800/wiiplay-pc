#include <Pack/RPParty/RPTnkScene/RPTnkAi.h>
#include <Pack/RPParty/RPDucScene/RPDucUtil.h>
#include <Pack/RPParty/RPTnkScene/RPTnkField.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTank.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkWeaponManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkShell.h>
#include <Pack/RPParty/RPTnkScene/RPTnkMine.h>
#include <Pack/RPParty/RPTnkScene/RPTnkBlock.h>

// The remainder with PowerPC's result for a zero divisor (an enemy type that lays no mines has
// a zero interval range; the host form is in port/host/include/decomp/compat.h)
#if !defined(PORT_PPC_MOD)
#define PORT_PPC_MOD(x, d) ((x) % (d))
#endif

extern "C" f32 Tnk_getShellRadius();
extern "C" f32 Tnk_getMineProximityRadius();
extern "C" f32 RP_calcPerpendicularDistance(const nw4r::math::VEC3*,
    const nw4r::math::VEC3*, const nw4r::math::VEC3*);

static inline u32 AttackRandom() {
    RP_randSeedX = RP_randSeedX * 0x41C64E6D + 12345;
    if (RP_randSeedY & 1) RP_randSeedY ^= 0x11020;
    RP_randSeedY >>= 1;
    return RP_randSeedX ^ RP_randSeedY;
}

static inline f32 AttackRandomFloat() {
    return 1.192093e-7f * (f32)(AttackRandom() & 0x7FFFFF);
}

static inline f32 AttackRandomRange(f32 lo, f32 hi) {
    if (lo == hi) return lo;
    return (hi - lo) * AttackRandomFloat() + lo;
}

static inline nw4r::math::VEC3 AttackHitCenter(RPTnkTank* tank) {
    nw4r::math::VEC3 center;
    center.x = tank->GetPos().x + tank->GetHitOffsetAlt().x;
    center.y = tank->GetPos().y + tank->GetHitOffsetAlt().y;
    center.z = tank->GetPos().z + tank->GetHitOffsetAlt().z;
    return center;
}

static inline void AttackStoreHitCenter(nw4r::math::VEC3* center, RPTnkTank* tank) {
    center->x = tank->GetPos().x + tank->GetHitOffsetAlt().x;
    center->y = tank->GetPos().y + tank->GetHitOffsetAlt().y;
    center->z = tank->GetPos().z + tank->GetHitOffsetAlt().z;
}

static inline void AttackSetVector(nw4r::math::VEC3* vector, f32 x, f32 y, f32 z) {
    vector->x = x;
    vector->y = y;
    vector->z = z;
}

static inline void AttackCopyActionVector(void* destination, const void* source) {
    // NONMATCHING-DEBT: the binary copies the mine position into both LayMine argument
    // temporaries with integer lwz/stw words. Plain struct assignment (VEC3 or _VEC3 locals),
    // a _VEC3& cast assignment and a field-by-field copy all give float copies and a different
    // schedule (ExecuteActions 186/203), so the word-struct copy stays.
    struct Words { u32 x, y, z; };
    *(Words*)destination = *(const Words*)source;
}

struct AttackEnemyParamsData {
    be_s32 i00; be_f32 f04; be_s32 i08; be_s32 i0C; be_s32 i10; be_f32 f14;
    be_f32 f18; be_f32 f1C; be_s32 i20; be_s32 i24; be_f32 f28; be_f32 f2C;
    be_f32 f30; be_s32 i34; be_s32 i38; be_f32 f3C; be_f32 f40; be_f32 f44;
    be_f32 f48; be_s32 i4C; be_f32 f50; be_s32 i54; be_f32 f58; be_f32 f5C;
    be_f32 f60; be_f32 f64; be_f32 f68; be_s32 i6C; be_f32 f70; be_s32 i74;
    be_f32 f78; be_f32 f7C; be_f32 f80; be_s32 i84; be_s32 i88; be_s32 i8C;
    be_s32 i90; be_f32 f94; be_f32 f98; be_s32 i9C; be_f32 fA0; be_s32 iA4;
};

struct AttackEnemyParams : AttackEnemyParamsData {
    AttackEnemyParams(const AttackEnemyParamsData& rhs) {
        i00 = rhs.i00; f04 = rhs.f04; i08 = rhs.i08; i0C = rhs.i0C;
        i10 = rhs.i10; f14 = rhs.f14; f18 = rhs.f18; f1C = rhs.f1C;
        i20 = rhs.i20; i24 = rhs.i24; f28 = rhs.f28; f2C = rhs.f2C;
        f30 = rhs.f30; i34 = rhs.i34; i38 = rhs.i38; f3C = rhs.f3C;
        f40 = rhs.f40; f44 = rhs.f44; f48 = rhs.f48; i4C = rhs.i4C;
        f50 = rhs.f50; i54 = rhs.i54; f58 = rhs.f58; f5C = rhs.f5C;
        f60 = rhs.f60; f64 = rhs.f64; f68 = rhs.f68; i6C = rhs.i6C;
        f70 = rhs.f70; i74 = rhs.i74; f78 = rhs.f78; f7C = rhs.f7C;
        f80 = rhs.f80; i84 = rhs.i84; i88 = rhs.i88; i8C = rhs.i8C;
        i90 = rhs.i90; f94 = rhs.f94; f98 = rhs.f98; i9C = rhs.i9C;
        fA0 = rhs.fA0; iA4 = rhs.iA4;
    }
};

// param/TnkGameParam.bin: lives, then one record per enemy type (stage records follow).
struct AttackGameParamData {
    s32 lives;
    AttackEnemyParamsData enemies[10];
};

static inline AttackEnemyParamsData AttackGetParams(s32 type) {
    void* data = RPTnkGameManager::sInstance->GetParam();
    return static_cast<AttackGameParamData*>(data)->enemies[type];
}

RPTnkAiAttack::RPTnkAiAttack(RPTnkTank* pTank) {
    mpProbe = new RPTnkAiProbe;
    mpProbe->Create();
    mpTank = pTank;
}

RPTnkAiAttack::~RPTnkAiAttack() {}

void RPTnkAiAttack::PredictShotPath(f32 muzzleDist) {
// NONMATCHING-DEBT: macro with a comma expression. Natural forms tried (2026-10-29), none
// byte-identical: plain statements 203/248 words; SetDirection before SetStartPos 201/248;
// RPTnkAiProbe inline members SetShot(pos, dir, speed, radius) by value 91/248 or const& 179/248,
// SetRay(pos, dir) by value 154/254 or const& 218/248 (245 words); a named dir copy 153/257.
#define CONFIGURE_SHOT_PROBE(position, direction) \
    { \
        f32 radius = Tnk_getShellRadius(); \
        /* the direction temporary is allocated before the position one */ \
        mpProbe->SetDirection((mpProbe->SetStartPos(position), direction)); \
        mpProbe->mSpeed = mShellSpeed; \
        mpProbe->unkCC = radius; \
        mpProbe->mStepSize = 1; \
        mpProbe->mMaxSteps = 250; \
        mpProbe->mMode = 1; \
        mpProbe->mStopFlags = 1; \
    }
    nw4r::math::VEC3 start;
    start.x = muzzleDist * mAimDir.x + mPos.x;
    start.y = mPos.y;
    start.z = muzzleDist * mAimDir.z + mPos.z;
    mSegmentNum = mShellBounces + 1;
    mSegments[0].Set(start, mAimDir, 0);
    if (RPTnkField::sInstance->IsBlockAtPos(start)) return;

    CONFIGURE_SHOT_PROBE(start, mAimDir);
    RPTnkAiProbe::Result first;
    mpProbe->Sweep(&first);
    s32 steps;
    if ((first.mFlags & 1) && (steps = first.mSteps) > 6) {
        mSegments[0].mEndStep = steps;
        mSegments[1].Set(first.mEndPos, first.mEndVel, steps);
        PSVECNormalize((Vec*)&mSegments[1].mDir, (Vec*)&mSegments[1].mDir);
        if (mSegmentNum >= 2) {
            CONFIGURE_SHOT_PROBE(mSegments[1].mPos, mSegments[1].mDir);
            RPTnkAiProbe::Result second;
            mpProbe->Sweep(&second);
            if (second.mFlags & 1) {
                mSegments[1].mEndStep = second.mSteps + mSegments[1].mStartStep;
                mSegments[2].Set(second.mEndPos, second.mEndVel, mSegments[1].mEndStep);
                PSVECNormalize((Vec*)&mSegments[2].mDir, (Vec*)&mSegments[2].mDir);
                if (mSegmentNum == 3) {
                    CONFIGURE_SHOT_PROBE(mSegments[2].mPos, mSegments[2].mDir);
                    RPTnkAiProbe::Result third;
                    mpProbe->Sweep(&third);
                    if (third.mFlags & 1)
                        mSegments[2].mEndStep = third.mSteps + mSegments[2].mStartStep;
                }
            } else {
                mSegmentNum = 2;
            }
        }
    } else {
        mSegmentNum = 1;
    }
#undef CONFIGURE_SHOT_PROBE
}

BOOL RPTnkAiAttack::PathHitsSelf() {
    if (mSegmentNum <= 1) return FALSE;
    f32 cosine = nw4r::math::CosDeg(mParams.mSelfAngle);
    nw4r::math::VEC3 center = AttackHitCenter(mpTank);
    nw4r::math::VEC3 delta(0.0f, 0.0f, 0.0f);
    s32 i = 1;
    do {
        nw4r::math::VEC3 origin(mSegments[i].mPos.x, 0.0f, mSegments[i].mPos.z);
        f32 length = (f32)(mSegments[i].mEndStep - mSegments[i].mStartStep);
        length *= mShellSpeed;
        delta.x = center.x - origin.x;
        delta.y = center.y - origin.y;
        delta.z = center.z - origin.z;
        if (!(PSVECMag((Vec*)&delta) > length)) {
            if (RP_calcPerpendicularDistance(&center, &origin, &mSegments[i].mDir) < 22.0f) {
                f32 along = nw4r::math::VEC3Dot(&delta, &mSegments[i].mDir);
                BOOL inside = FALSE;
                if (0.0f <= along && length >= along) inside = TRUE;
                if (inside) return TRUE;
            }
            PSVECNormalize((Vec*)&delta, (Vec*)&delta);
            if (nw4r::math::VEC3Dot(&delta, &mSegments[i].mDir) > cosine) return TRUE;
        }
        i++;
    } while (i < mSegmentNum);
    return FALSE;
}

BOOL RPTnkAiAttack::PathHitsPlayer() {
    nw4r::math::VEC3 center;
    nw4r::math::VEC3 delta;
    nw4r::math::VEC3 originFirst;
    nw4r::math::VEC3 originSecond;
    nw4r::math::VEC3 secondCenter;
    f32 cosine = nw4r::math::CosDeg(mParams.mPlayerAngle);
    RPTnkTank* player = RPTnkTankManager::sInstance->GetPlayerTank(0);
    if (player->GetAlive()) {
        AttackStoreHitCenter(&center, player);
        delta.x = 0.0f;
        delta.y = 0.0f;
        delta.z = 0.0f;
        s32 i = 0;
        do {
            f32 length = GetSegmentLength(mSegments[i]);
            AttackSetVector(&originFirst, mSegments[i].mPos.x, 0.0f,
                            mSegments[i].mPos.z);
            delta.x = center.x - originFirst.x;
            delta.y = 0.0f;
            delta.z = center.z - originFirst.z;
            if (!(PSVECMag((Vec*)&delta) > length)) {
                PSVECNormalize((Vec*)&delta, (Vec*)&delta);
                if (nw4r::math::VEC3Dot(&delta, &mSegments[i].mDir) > cosine) return TRUE;
            }
            i++;
        } while (i < mSegmentNum);
    }
    if (RPTnkGameManager::sInstance->GetModeIdx() == 1) {
        RPTnkTank* player = RPTnkTankManager::sInstance->GetPlayerTank(1);
        if (player->GetAlive()) {
            AttackStoreHitCenter(&secondCenter, player);
            center.x = secondCenter.x;
            center.y = secondCenter.y;
            center.z = secondCenter.z;
            s32 i = 0;
            do {
                AttackSetVector(&originSecond, mSegments[i].mPos.x, 0.0f,
                                mSegments[i].mPos.z);
                f32 length = GetSegmentLength(mSegments[i]);
                delta.x = center.x - originSecond.x;
                delta.y = 0.0f;
                delta.z = center.z - originSecond.z;
                if (!(PSVECMag((Vec*)&delta) > length)) {
                    PSVECNormalize((Vec*)&delta, (Vec*)&delta);
                    if (nw4r::math::VEC3Dot(&delta, &mSegments[i].mDir) > cosine) return TRUE;
                }
                i++;
            } while (i < mSegmentNum);
        }
    }
    return FALSE;
}

BOOL RPTnkAiAttack::PathHitsAlly() {
    nw4r::math::VEC3 center;
    nw4r::math::VEC3 delta;
    nw4r::math::VEC3 nearby;
    nw4r::math::VEC3 origin;
    nw4r::math::VEC3 temporary;
    f32 cosine = nw4r::math::CosDeg(mParams.mAllyAngle);
    delta.x = 0.0f;
    delta.y = 0.0f;
    delta.z = 0.0f;
    RPTnkTank** last = RPTnkTankManager::sInstance->GetEnemyLast();
    RPTnkTank** entry = RPTnkTankManager::sInstance->GetEnemyBegin();
    do {
        RPTnkTank* tank = *entry;
        if (mpTank != tank && tank->GetAge() >= 0) {
            AttackStoreHitCenter(&temporary, tank);
            center.x = temporary.x;
            center.y = temporary.y;
            center.z = temporary.z;
            nearby.x = 0.0f;
            nearby.y = 0.0f;
            nearby.z = 0.0f;
            nearby.x = center.x - mPos.x;
            nearby.z = center.z - mPos.z;
            if (PSVECMag((Vec*)&nearby) < mParams.mAllyDist) return TRUE;
            s32 i = 0;
            do {
                AttackSetVector(&origin, mSegments[i].mPos.x, 0.0f,
                                mSegments[i].mPos.z);
                f32 length = GetSegmentLength(mSegments[i]);
                delta.x = center.x - origin.x;
                delta.y = 0.0f;
                delta.z = center.z - origin.z;
                if (!(PSVECMag((Vec*)&delta) > length)) {
                    if (RP_calcPerpendicularDistance(&center, &origin, &mSegments[i].mDir) < 22.0f) {
                        f32 along = nw4r::math::VEC3Dot(&delta, &mSegments[i].mDir);
                        BOOL inside = FALSE;
                        if (0.0f <= along && length >= along) inside = TRUE;
                        if (inside) return TRUE;
                    }
                    PSVECNormalize((Vec*)&delta, (Vec*)&delta);
                    if (nw4r::math::VEC3Dot(&delta, &mSegments[i].mDir) > cosine) return TRUE;
                }
                i++;
            } while (i < mSegmentNum);
        }
        entry++;
    } while (entry <= last);
    return FALSE;
}

void RPTnkAiAttack::PickAimTarget() {
    nw4r::math::MTX34 rotation;
    nw4r::math::VEC3 direction;
    nw4r::math::VEC3 secondOnly;
    nw4r::math::VEC3 firstOnly;
    nw4r::math::VEC3 solo;
    nw4r::math::VEC3 secondDelta;
    nw4r::math::VEC3 firstDelta;
    nw4r::math::VEC3 secondCenter;
    nw4r::math::VEC3 firstCenter;
    nw4r::math::VEC3 target;
    f32 maximum = mParams.mAimAngle;
    f32 minimum = -maximum;
    if (RPTnkGameManager::sInstance->GetModeIdx() == 0) {
        AttackStoreHitCenter(&solo, RPTnkTankManager::sInstance->GetPlayerTank(0));
        target.x = solo.x;
        target.y = solo.y;
        target.z = solo.z;
    } else {
        RPTnkTank* first = RPTnkTankManager::sInstance->GetPlayerTank(0);
        RPTnkTank* second = RPTnkTankManager::sInstance->GetPlayerTank(1);
        bool firstAlive = first->GetAlive();
        bool secondAlive = second->GetAlive();
        if (!secondAlive) {
            AttackStoreHitCenter(&firstOnly, first);
            target.x = firstOnly.x;
            target.y = firstOnly.y;
            target.z = firstOnly.z;
        } else if (!firstAlive && secondAlive) {
            AttackStoreHitCenter(&secondOnly, second);
            target.x = secondOnly.x;
            target.y = secondOnly.y;
            target.z = secondOnly.z;
        } else {
            f32 z1, y1, x1, z0, y0, x0, dx0, dz0;
            x0 = first->GetPos().x + first->GetHitOffsetAlt().x;
            z0 = first->GetPos().z + first->GetHitOffsetAlt().z;
            dx0 = mPos.x - x0;
            dz0 = mPos.z - z0;
            y0 = first->GetPos().y + first->GetHitOffsetAlt().y;
            x1 = second->GetPos().x + second->GetHitOffsetAlt().x;
            y1 = second->GetPos().y + second->GetHitOffsetAlt().y;
            z1 = second->GetPos().z + second->GetHitOffsetAlt().z;
            firstDelta.x = dx0;
            firstDelta.y = 0.0f;
            firstDelta.z = dz0;
            f32 dz1 = mPos.z - z1;
            f32 dx1 = mPos.x - x1;
            firstCenter.x = x0;
            firstCenter.y = y0;
            firstCenter.z = z0;
            secondCenter.x = x1;
            secondCenter.y = y1;
            secondCenter.z = z1;
            secondDelta.x = dx1;
            secondDelta.y = 0.0f;
            secondDelta.z = dz1;
            f32 secondDistance = PSVECMag((Vec*)&secondDelta);
            f32 firstDistance = PSVECMag((Vec*)&firstDelta);
            if (firstDistance <= secondDistance) {
                target.x = firstCenter.x;
                target.y = firstCenter.y;
                target.z = firstCenter.z;
            } else {
                target.x = secondCenter.x;
                target.y = secondCenter.y;
                target.z = secondCenter.z;
            }
        }
    }
    direction.x = 0.0f;
    direction.y = 0.0f;
    direction.z = 1.0f;
    direction.x = target.x - mPos.x;
    direction.y = 0.0f;
    direction.z = target.z - mPos.z;
    PSVECNormalize((Vec*)&direction, (Vec*)&direction);
    f32 angle = AttackRandomRange(minimum, maximum);
    nw4r::math::MTX34RotXYZFIdx(&rotation, 0.0f, angle * (256.0f / 360.0f), 0.0f);
    PSMTXMultVec((const f32(*)[4])&rotation, (Vec*)&direction, (Vec*)&mAimTarget);
    PSVECNormalize((Vec*)&mAimTarget, (Vec*)&mAimTarget);
}

void RPTnkAiAttack::DecideFire(f32 muzzleDist) {
    mFire = false;
    s32 slot = mpTank->GetIndex() + 2;
    if (RPTnkWeaponManager::sInstance->GetShellPool()->GetShellNum(slot) < mMaxShells) {
        PredictShotPath(muzzleDist);
        if (mSegments[0].mEndStep > 6 && !PathHitsSelf() &&
            !PathHitsAlly() && PathHitsPlayer()) mFire = true;
    }
}

void RPTnkAiAttack::DecideLayMine() {
    mLayMine = false;
    s32 slot = mpTank->GetIndex() + 2;
    if (RPTnkWeaponManager::sInstance->GetMinePool()->GetMineNum(slot) < mParams.mMineMax) {
        f32 distance = RPTnkTankManager::sInstance->GetNearestEnemyDistance(mpTank);
        if (distance <= mParams.mMineEnemyDist) return;
        f32 radius = Tnk_getMineProximityRadius();
        nw4r::math::VEC3 pos = mPos;
        BOOL nearby = RPTnkBlockPool::sInstance->IsKind0BlockNear(radius, pos);
        bool lay = false;
        f32 chance = AttackRandomRange(0.0f, 100.0f);
        if (nearby && chance < mParams.mMineChanceNearBlock) lay = true;
        else if (!nearby && chance < mParams.mMineChance) lay = true;
        if (lay) {
            RPTnkAiController* controller = mpTank->GetAiController();
            RPTnkAiMove* move = controller->GetMove();
            nw4r::math::VEC3 pos = mPos;
            if (move->FindClearDirection(mParams.mMineClearDist, pos)) mLayMine = true;
        }
    }
}

void RPTnkAiAttack::ExecuteActions(f32 muzzleDist, RPAxes* pTurret) {
    nw4r::math::VEC3 minePos;
    nw4r::math::VEC3 muzzle;
    nw4r::math::_VEC3 mineSpawn;
    nw4r::math::_VEC3 mineTarget;
    nw4r::math::_VEC3 shellPos;
    nw4r::math::_VEC3 velocity;
    nw4r::math::VEC3 direction;
    direction.x = mAimDir.x;
    direction.y = 0.0f;
    direction.z = mAimDir.z;
    f32 magnitude = PSVECMag((Vec*)&direction);
    if (magnitude != 0.0f) {
        f32 inverse = 1.0f / magnitude;
        pTurret->z.x = direction.x * inverse;
        pTurret->z.y = direction.y * inverse;
        pTurret->z.z = direction.z * inverse;
        pTurret->y.x = RP_vecEy.x;
        pTurret->y.y = RP_vecEy.y;
        pTurret->y.z = RP_vecEy.z;
        PSVECCrossProduct((Vec*)&pTurret->y, (Vec*)&pTurret->z, (Vec*)&pTurret->x);
    }
    if (mLayMine) {
        f32 x;
        f32 z;
        z = mPos.z;
        x = mPos.x;
        minePos.y = 0.0f;
        minePos.x = x;
        minePos.z = z;
        s32 slot = mpTank->GetIndex() + 2;
        AttackCopyActionVector(&mineTarget, &minePos);
        AttackCopyActionVector(&mineSpawn, &minePos);
        RPTnkWeaponManager::sInstance->LayMine(0.0f, mpTank,
            (nw4r::math::VEC3*)&mineSpawn, (nw4r::math::VEC3*)&mineTarget, slot);
        mpTank->PlayLayMineSe();
        unk74 = mParams.mFireUnk74;
        unk78 = mParams.mFireUnk78;
        mFire = false;
        s32 range = mParams.mFireIntervalMax - mParams.mFireIntervalMin;
        mFireInterval = mParams.mFireIntervalMin + PORT_PPC_MOD((s32)((AttackRandom() >> 4) & 0xFFFF), range);
        RPTnkAiController* controller = mpTank->GetAiController();
        RPTnkAiMove* move = controller->GetMove();
        move->SetUnk1F9(true);
    }
    if (mFire) {
        muzzle.x = muzzleDist * pTurret->z.x + mPos.x;
        muzzle.y = muzzleDist * pTurret->z.y + mPos.y;
        muzzle.z = muzzleDist * pTurret->z.z + mPos.z;
        s32 slot = mpTank->GetIndex() + 2;
        s32 smoke = 0;
        if (mType == 3) smoke = 1;
        else if (mType == 7) smoke = 2;
        else if (mType == 9) smoke = 1;
        // NONMATCHING-DEBT: volatile pins the interleaved word copies of axes[2] and muzzle and the
        // mpTank-before-mShellSpeed load order. Closest natural form (2 instructions: lwz mpTank /
        // lfs mShellSpeed swapped) needs a member inline RPTnkAiAttack::FireShell(VEC3 pos, VEC3 dir,
        // s32 slot, s32 smoke) and the aim block as an inline (header patch /tmp/pvol1/RPTnkAi.h.patch).
        // Without the header: 14 (by-value static wrapper) or 13+ (VEC3 locals) instructions.
        // Rule check (2026-10-29, --sched): with that inline the interleave is natural: axes is
        // a non-const VEC3* (dummy in the worst-case set) and the by-value copies are address-taken
        // locals, so each copy store has a mem-raw edge to the later row loads. Now 202/203
        // (/tmp/pvol3/ai4_a.cpp + RPTnkAi.h.patch): only lfs mShellSpeed / lwz mpTank are swapped.
        // Both follow the stores (this-dummy vs the copies) with equal height; the tie goes to PCode
        // order, and codegen emits FireShell's arguments left to right (speed first). pTank/speed
        // locals, a manager local and GetTank()/GetShellSpeed() getters all leave it unchanged.
        u32 y, sz, x, sy, sx, z;
        x = *(const volatile u32*)&pTurret->z.x;
        y = *(const volatile u32*)&pTurret->z.y;
        sx = *(const volatile u32*)&muzzle.x;
        *(volatile u32*)&velocity.x = x;
        sy = *(const volatile u32*)&muzzle.y;
        *(volatile u32*)&velocity.y = y;
        sz = *(const volatile u32*)&muzzle.z;
        z = *(const volatile u32*)&pTurret->z.z;
        *(volatile u32*)&velocity.z = z;
        *(volatile u32*)&shellPos.x = sx;
        *(volatile u32*)&shellPos.y = sy;
        *(volatile u32*)&shellPos.z = sz;
        RPTnkTank* tank = *(RPTnkTank* const volatile*)&mpTank;
        f32 speed = *(const volatile f32*)&mShellSpeed;
        RPTnkWeaponManager::sInstance->FireShell(speed, tank,
            (nw4r::math::VEC3*)&shellPos, (nw4r::math::VEC3*)&velocity,
            slot, mShellBounces, smoke, mType);
        mpTank->PlayFireSeByType();
        mpTank->PlayShotEffect();
        unk70 = unk8;
        unk78 = mParams.mMineUnk78;
    }
    if (--unk70 < 0) unk70 = 0;
    if (--unk74 < 0) unk74 = 0;
    if (unk78 > 0) {
        mpTank->GetAiController()->SetFlags(1, TRUE);
        unk78--;
    } else {
        mpTank->GetAiController()->SetFlags(1, FALSE);
    }
}

void RPTnkAiAttack::LoadParams(s32 type) {
    mType = type;
    AttackEnemyParams params = AttackGetParams(type);
    unk8 = params.i90;
    mMaxShells = params.i74;
    mShellBounces = params.i84;
    mShellSpeed = params.f94;
    mParams.mMineUnk78 = params.iA4;
    mParams.mAimAngle = params.f70;
    mParams.mAimStep = params.f98;
    mParams.mAimTime = params.i9C;
    mParams.mFireIntervalMin = params.i8C;
    mParams.mFireIntervalMax = params.i88;
    mParams.mSelfAngle = params.f78;
    mParams.mPlayerAngle = params.f80;
    mParams.mAllyAngle = params.f7C;
    mParams.mAllyDist = params.fA0;
    mParams.mFireUnk74 = params.i20;
    mParams.mMineMax = params.i08;
    mParams.mFireUnk78 = params.i24;
    mParams.mMineClearDist = params.f04;
    mParams.mMineEnemyDist = params.f14;
    mParams.mMineIntervalMin = params.i10;
    mParams.mMineIntervalMax = params.i0C;
    mParams.mMineChance = params.f1C;
    mParams.mMineChanceNearBlock = params.f18;
}

// Wii Play: not an override; the attack vtable keeps RPSceneObj::Reset.
void RPTnkAiAttack::ResetAttack() {
    nw4r::math::VEC3 dir = RP_vecEz;
    mAimTarget.x = dir.x;
    mAimTarget.y = dir.y;
    mAimTarget.z = dir.z;
    mAimDir.x = dir.x;
    mAimDir.y = dir.y;
    mAimDir.z = dir.z;
    PSVECNormalize((Vec*)&mAimDir, (Vec*)&mAimDir);
    PSVECNormalize((Vec*)&mAimDir, (Vec*)&mAimDir);
    PSVECCrossProduct((const Vec*)&RP_vecEy, (const Vec*)&mAimDir,
                      (Vec*)&mAimSide);
    mFire = false;
    mLayMine = false;
    unk70 = 0;
    unk74 = 0;
    unk78 = 0;
    mAimTimer = 0;
    s32 fireRange = mParams.mFireIntervalMax - mParams.mFireIntervalMin;
    mFireInterval = mParams.mFireIntervalMin + PORT_PPC_MOD((s32)((AttackRandom() >> 4) & 0xFFFF), fireRange);
    s32 mineRange = mParams.mMineIntervalMax - mParams.mMineIntervalMin;
    mMineInterval = mParams.mMineIntervalMin + PORT_PPC_MOD((s32)((AttackRandom() >> 4) & 0xFFFF), mineRange);
    mpProbe->Reset();
}

void RPTnkAiAttack::SetAimDirection(nw4r::math::VEC3 dir) {
    mAimTarget.x = dir.x;
    mAimTarget.y = dir.y;
    mAimTarget.z = dir.z;
    mAimDir.x = dir.x;
    mAimDir.y = dir.y;
    mAimDir.z = dir.z;
    PSVECNormalize((Vec*)&mAimDir, (Vec*)&mAimDir);
    PSVECNormalize((Vec*)&mAimDir, (Vec*)&mAimDir);
    PSVECCrossProduct((const Vec*)&RP_vecEy, (const Vec*)&mAimDir,
                      (Vec*)&mAimSide);
}

void RPTnkAiAttack::Update(f32 muzzleDist, RPAxes* pTurret,
                         const nw4r::math::VEC3& rPos) {
    nw4r::math::VEC3 selected;
    nw4r::math::VEC3 negative;
    nw4r::math::VEC3 positive;
    mPos.x = rPos.x;
    mPos.y = rPos.y;
    mPos.z = rPos.z;
    mFire = false;
    mLayMine = false;
    if (--mAimTimer <= 0) {
        PickAimTarget();
        mAimTimer = mParams.mAimTime;
    }
    positive.x = mAimSide.x * mParams.mAimStep + mAimDir.x;
    positive.y = mAimSide.y * mParams.mAimStep + mAimDir.y;
    positive.z = mAimSide.z * mParams.mAimStep + mAimDir.z;
    PSVECNormalize((Vec*)&positive, (Vec*)&positive);
    negative.x = mAimDir.x - mAimSide.x * mParams.mAimStep;
    negative.y = mAimDir.y - mAimSide.y * mParams.mAimStep;
    negative.z = mAimDir.z - mAimSide.z * mParams.mAimStep;
    PSVECNormalize((Vec*)&negative, (Vec*)&negative);
    RPSelectMoreAlignedVec(&selected, &mAimTarget, &negative, &positive);
    RPSelectMoreAlignedVec(&mAimDir, &mAimDir, &selected, &mAimTarget);
    PSVECNormalize((Vec*)&mAimDir, (Vec*)&mAimDir);
    PSVECCrossProduct((Vec*)&RP_vecEy, (Vec*)&mAimDir, (Vec*)&mAimSide);
    RPTnkAiController* controller = mpTank->GetAiController();
    if (!(controller->GetFlags() & 2)) {
        if ((s32)--mFireInterval <= 0) {
            if (unk70 == 0) DecideFire(muzzleDist);
            s32 range = mParams.mFireIntervalMax - mParams.mFireIntervalMin;
            mFireInterval = mParams.mFireIntervalMin + PORT_PPC_MOD((s32)((AttackRandom() >> 4) & 0xFFFF), range);
        }
        if ((s32)--mMineInterval <= 0) {
            if (unk74 == 0) DecideLayMine();
            s32 range = mParams.mMineIntervalMax - mParams.mMineIntervalMin;
            mMineInterval = mParams.mMineIntervalMin + PORT_PPC_MOD((s32)((AttackRandom() >> 4) & 0xFFFF), range);
        }
    }
    ExecuteActions(muzzleDist, pTurret);
}
