// Charge! scarecrow group (RPCowKakasiGroup): spawns its scarecrows or scarecrow kings in a
// row and scores the group once all are knocked. Unit 0x802AA9F8-0x802ABAD0
// (dtk/game_units.tsv). Functions in reverse address order.
#include <Pack/RPParty/RPCowScene/RPCowKakasiGroup.h>

#include <Pack/RPParty/RPCowScene/RPCowFieldObj.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPCowScene/RPCowPlayer.h>
#include <Pack/RPParty/RPCowScene/RPCowSound.h>
#include <Pack/RPParty/RPPartyCommon/RPCollisionShape.h>
#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>
#include <Pack/RPParty/RPPartyCommon/RPResFile.h>
#include <Pack/RPUtility/RPUtlRandom.h>

#include <nw4r/math.h>

extern "C" const f32 lbl_80440D1C;

namespace {
inline bool IsStarted(RPActor* pActor) {
    return pActor->GetFlags().mActivate; // activate pending or active
}
template <typename T>
inline T* FindFreeActor(RPCowGame* pGame, int slot, s32 start) {
    s32 num = pGame->GetSlotNum(slot);
    if (start >= num) {
        start = 0;
    }
    T* pTable = static_cast<T*>(pGame->GetSlotActor(slot));
    s32 i = start;
    do {
        if (!IsStarted(&pTable[i])) {
            return &pTable[i];
        }
        i++;
        i = (i < num) ? i : 0;
    } while (i != start);
    return NULL;
}
inline RPCowPlayer* GetPlayer(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowPlayer*>(pGame->GetSlotActor(7))[idx];
}
inline RPCowKakasi* GetKakasi(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowKakasi*>(pGame->GetSlotActor(2))[idx];
}
inline RPCowKakasiGroup* GetGroup(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowKakasiGroup*>(pGame->GetSlotActor(8))[idx];
}
inline bool IsPlaying(RPCowGame* pGame) {
    return pGame->GetState() == 7 || pGame->GetState() == 8;
}
inline s32 GetCheckNum(RPCowGame* pGame) {
    return pGame->GetPlayerNum() > 1 ? 2 : 1;
}
inline bool IsKingRes(RPPathObject* pPathObj) {
    return pPathObj->GetParam(1, 1) >= 2;
}
}

inline RPCowGame* KakasiGame() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    return pGame;
}

void RPCowKakasiGroup::SpawnKakasi(const EGG::Vector3f& rPos, const EGG::Vector3f& rRot,
                                   s32 start, s32 end, s32 drop, f32 spacing) {
    RPCowGame* pGame = KakasiGame();
    for (s32 i = start; i < end; i++) {
        if (IsKingRes(mpPathObj)) {
            RPCowKakasiKing* p = FindFreeActor<RPCowKakasiKing>(pGame, 4, 0);
            if (p != NULL) {
                pGame->StartActor(p);
                f32 z = spacing * i * nw4r::math::CosRad(rRot.y) + rPos.z;
                f32 x = spacing * i * nw4r::math::SinRad(rRot.y) + rPos.x;
                EGG::Vector3f pos(x, 0.0f, z);
                EGG::Vector3f rot(0.0f, rRot.y, 0.0f);
                p->SpawnAppear(pos, rot);
                Cow_objectColliders.Append(p->mpCollision, 0x40);
                p->mGroup = this;
                p->unk158 = mpPathObj->GetParam(1, 1);
                p->unkF8 = mpPathObj->GetParam(2, 0) / 100.0f;
                p->unkFC = mpPathObj->GetParam(4, 0) / 100.0f;
                p->unk110 = mpPathObj->GetParam(5, 0);
                if (i == 0) {
                    p->mpSound->startSound(0x49);
                }
            }
        } else {
            RPCowKakasi* p = FindFreeActor<RPCowKakasi>(pGame, 2, 0);
            if (p != NULL) {
                pGame->StartActor(p);
                f32 z = spacing * i * nw4r::math::CosRad(rRot.y) + rPos.z;
                f32 x = spacing * i * nw4r::math::SinRad(rRot.y) + rPos.x;
                EGG::Vector3f pos(x, 0.0f, z);
                p->SpawnDrop(pos, drop);
                Cow_objectColliders.Append(p->mpCollision, 2);
                p->mGroup = this;
                p->mScore = mpPathObj->GetParam(1, 1);
                p->unkF8 = 1.0f;
                p->unkFC = 0.0f;
                p->unk110 = 0;
                if (i == 0) {
                    p->mpSound->startSound(0x47);
                }
            }
        }
        if (i == 0) {
            RPCowGame::GetInstance()->GetSpawnedGroups()++;
        }
    }
    mpCollision->mpOwner = NULL;
    mTimer = 0;
    mState = 1;
}

void RPCowKakasiGroup::SpawnAll() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    s32 groupNum = pGame->GetSlotNum(8);
    for (int i = 0; i < groupNum; i++) {
        RPCowKakasiGroup* pGroup = GetGroup(pGame, i);
        if (pGroup->mGroupsAhead == 0) {
            pGroup->mNum = 3;
            RPCollisionShape* pShape = pGroup->mpCollision;
            pGroup->SpawnKakasi(pShape->GetPos(), pShape->GetVec40(), 0, pGroup->mNum,
                                1, lbl_80440D1C);
            s32 num = GetCheckNum(pGame);
            for (int j = 0; j < num; j++) {
                GetPlayer(pGame, j)->SetTargetGroup(pGroup->GetUnk1A());
            }
        }
    }
}

bool RPCowKakasiGroup::IsEnabled(IRPActorPriority* pOther) {
    return true;
}

void RPCowKakasiGroup::OnKakasiHit(s32 player) {
    RPCowGame* pGame = RPCowGame::GetInstance();
    if (IsPlaying(pGame)) {
        if (mPlayer >= 2) {
            mPlayer = player;
        }
        if (mPlayer == player) {
            mHits++;
        }
        if (mHits == mNum) {
            RPCowPlayer* pPlayer = GetPlayer(pGame, mPlayer);
            pGame->GetKnockedGroups()++;
            if (mNum >= 3) {
                pGame->GetGroupChain()++;
                mScoreTimer = 10;
                mBonusLevel = pGame->GetGroupChain();
                if (mBonusLevel >= 3) {
                    pGame->GetGroupChain() = 0;
                }
                pPlayer->PlayKakasiHitSound(mBonusLevel);
            }
            SinkAll();
        }
    }
}

EGG::Vector3f RPCowKakasiGroup::GetPos() const {
    return mpCollision->GetPos();
}

RPCowKakasiGroup::RPCowKakasiGroup() {}

RPCowKakasiGroup::~RPCowKakasiGroup() {}

void RPCowKakasiGroup::LoadResource() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpCollision = new RPCollisionShape();
    mpPathObj = new RPPathObject(
        0.1f, 1.0f, pGame->GetResTable()->FindNthEntryOfType(0x70005, GetUnk1A()));
}

void RPCowKakasiGroup::OnStart() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpCollision->Reset();
    mpCollision->GetPos() = mpPathObj->GetPos();
    mpCollision->GetVec40().y =
        nw4r::math::Atan2Rad(mpPathObj->GetVec20().x, mpPathObj->GetVec20().z);
    mpCollision->mpOwner = static_cast<IRPActorEnable*>(this);
    mpCollision->SetRadius(sRadiusScale * mpPathObj->GetRadius());
    Cow_objectColliders.Append(mpCollision, 8);
    mNum = 0;
    mHits = 0;
    mScoreTimer = 0;
    mBonusLevel = 0;
    unk48 = 0;
    mPlayer = 2;
    mTimer = 0;
    mState = 0;
    mGroupsAhead = 0;
    s32 groupNum = pGame->GetSlotNum(8);
    for (int i = 0; i < groupNum; i++) {
        if (i != GetUnk1A()) {
            if (GetGroup(pGame, i)->mpPathObj->GetPos().z > mpCollision->GetPos().z) {
                mGroupsAhead++;
            }
        }
    }
    sSpawnCounter = 5;
}

void RPCowKakasiGroup::OnStop() {
    Cow_objectColliders.Remove(mpCollision);
}

// Preserve the original register allocation after C++ variants leave the target
// loop's counter and player pointer interchanged. The C++ equivalent follows.
// Replay finding (2026-10-29, mwcc_trace + sim.py on the #if 0 version below): two values
// differ. (1) Score block: pPlayer must be a named local (any vreg below kingNear); with
// GetPlayer(pGame, mPlayer) the member argument adds a parameter temporary and an extra copy, so
// pPlayer is propagated away; `&static_cast<RPCowPlayer*>(pGame->GetSlotActor(7))[(u16)mPlayer]`
// keeps it and matches that block. (2) SetTargetGroup loop: pPlayer must be numbered above index
// and below kingNear, i.e. declared textually BEFORE the counter (named locals are numbered in
// reverse declaration order) yet not propagated. Only the `RPCowPlayer* const` initialisation
// survives propagation, and it cannot precede the loop; a non-const pointer declared before the
// loop is propagated into GetPlayer's temporary (or strength-reduced when written without the
// inline). Moving the block into an inline numbers all three values too high. No natural form found.
// NONMATCHING-DEBT: mangled names are only for the asm body below; they go when OnActivate is C++.
extern "C" {
extern u32 spActorList__12RPActorScene;
extern const f32 lbl_80440D20;
extern u32 sSpawnCounter__16RPCowKakasiGroup;
extern u32 sRandom__11RPUtlRandom;
extern u32 MULT__11RPUtlRandom;
extern u32 RANDF_MAX__11RPUtlRandom;
extern u32 RANDF_SHIFT__11RPUtlRandom;
extern const f64 lbl_80440D28;
extern const f32 lbl_80440D24;
extern const f32 lbl_80440D1C;
void _savegpr_22();
void GetParam__12RPPathObjectCFUsl();
void GetPos__11RPCowPlayerCFv();
u32 __cvt_fp2unsigned(f64);
void SpawnKakasi__16RPCowKakasiGroupFRCQ23EGG8Vector3fRCQ23EGG8Vector3flllf();
void SetTargetGroup__11RPCowPlayerFl();
void StartSink__13RPCowFieldObjFv();
void AddGroupScore__11RPCowPlayerFll();
void _restgpr_22();
}

#if !defined(PORT_HOST)
asm void RPCowKakasiGroup::OnActivate() {
    nofralloc
    stwu r1, -0x80(r1)
    mflr r0
    stw r0, 0x84(r1)
    stfd f31, 0x70(r1)
    psq_st f31, 0x78(r1), 0, 0
    addi r11, r1, 0x70
    bl _savegpr_22
    lwz r30, spActorList__12RPActorScene
    mr r27, r3
    lwz r0, 0x2c(r3)
    lwz r6, 0x40(r30)
    cmpwi r0, 0x1
    xori r4, r6, 0x1
    srawi r5, r4, 1
    and r4, r4, r6
    subf r4, r4, r5
    srwi r4, r4, 31
    addi r31, r4, 0x1
    beq L_802AAD68
    bge L_802AAEA8
    cmpwi r0, 0x0
    bge L_802AAA5C
    b L_802AAEA8
    L_802AAA5C:
    lwz r3, 0x1c(r30)
    li r0, 0x1
    cmpwi r3, 0x7
    beq L_802AAA78
    cmpwi r3, 0x8
    beq L_802AAA78
    li r0, 0x0
    L_802AAA78:
    cmpwi r0, 0x0
    beq L_802AAEA8
    lfs f31, lbl_80440D20
    li r29, 0x0
    li r28, 0x0
    li r25, 0x2
    li r26, 0x0
    b L_802AAB94
    L_802AAA98:
    clrlwi r0, r28, 16
    lwz r4, 0x10(r30)
    mulli r0, r0, 0x72e8
    lwz r3, 0x24(r27)
    lwz r4, 0x1c(r4)
    li r23, 0x1
    li r22, 0x0
    add r24, r4, r0
    li r4, 0x1
    li r5, 0x1
    bl GetParam__12RPPathObjectCFUsl
    srawi r4, r3, 31
    subfc r0, r25, r3
    adde. r0, r4, r26
    beq L_802AAB10
    mr r4, r24
    addi r3, r1, 0x20
    bl GetPos__11RPCowPlayerCFv
    lwz r3, 0x28(r27)
    lwz r12, 0x10(r3)
    lwz r12, 0x8(r12)
    mtctr r12
    bctrl
    lfs f1, 0x8(r3)
    lfs f0, 0x28(r1)
    fadds f1, f31, f1
    fcmpo cr0, f0, f1
    cror 2, 0, 2
    bne L_802AAB10
    li r22, 0x1
    L_802AAB10:
    cmpwi r22, 0x0
    bne L_802AAB84
    lwz r3, 0x24(r27)
    li r22, 0x0
    li r4, 0x1
    li r5, 0x1
    bl GetParam__12RPPathObjectCFUsl
    srawi r4, r3, 31
    subfc r0, r25, r3
    adde. r0, r4, r26
    bne L_802AAB78
    mr r4, r24
    addi r3, r1, 0x14
    bl GetPos__11RPCowPlayerCFv
    lwz r3, 0x28(r27)
    lwz r12, 0x10(r3)
    lwz r12, 0x8(r12)
    mtctr r12
    bctrl
    lfs f1, 0x8(r3)
    lfs f0, 0x1c(r1)
    fadds f1, f31, f1
    fcmpo cr0, f0, f1
    cror 2, 0, 2
    bne L_802AAB78
    li r22, 0x1
    L_802AAB78:
    cmpwi r22, 0x0
    bne L_802AAB84
    li r23, 0x0
    L_802AAB84:
    cmpwi r23, 0x0
    beq L_802AAB90
    li r29, 0x1
    L_802AAB90:
    addi r28, r28, 0x1
    L_802AAB94:
    cmpw r28, r31
    blt L_802AAA98
    cmpwi r29, 0x0
    beq L_802AAEA8
    lwz r0, 0x40(r30)
    cmpwi r0, 0x2
    blt L_802AAC5C
    lwz r3, 0x24(r27)
    li r4, 0x1
    li r5, 0x1
    bl GetParam__12RPPathObjectCFUsl
    srawi r4, r3, 31
    li r0, 0x2
    subfc r0, r0, r3
    li r0, 0x0
    adde. r0, r4, r0
    beq L_802AAC5C
    lwz r3, sSpawnCounter__16RPCowKakasiGroup
    subic. r0, r3, 0x1
    stw r0, sSpawnCounter__16RPCowKakasiGroup
    bgt L_802AAC58
    lwz r4, sRandom__11RPUtlRandom
    lis r3, 0x4330
    lwz r0, MULT__11RPUtlRandom
    lwz r6, RANDF_MAX__11RPUtlRandom
    mullw r5, r4, r0
    lwz r4, RANDF_SHIFT__11RPUtlRandom
    addi r0, r6, 0x1
    stw r3, 0x30(r1)
    lfd f3, lbl_80440D28
    stw r0, 0x3c(r1)
    addi r5, r5, 0x1
    stw r3, 0x38(r1)
    srw r0, r5, r4
    lfs f0, lbl_80440D24
    and r0, r6, r0
    lfd f1, 0x38(r1)
    clrlwi r0, r0, 16
    stw r5, sRandom__11RPUtlRandom
    fsubs f1, f1, f3
    stw r0, 0x34(r1)
    lfd f2, 0x30(r1)
    fsubs f2, f2, f3
    fdivs f1, f2, f1
    fmuls f1, f0, f1
    bl __cvt_fp2unsigned
    addi r0, r3, 0x3
    stw r0, sSpawnCounter__16RPCowKakasiGroup
    b L_802AAC5C
    L_802AAC58:
    li r29, 0x0
    L_802AAC5C:
    cmpwi r29, 0x0
    beq L_802AAD14
    lwz r3, 0x24(r27)
    li r4, 0x0
    li r5, 0x0
    bl GetParam__12RPPathObjectCFUsl
    cmpwi r3, 0x0
    ble L_802AAD14
    lwz r3, 0x24(r27)
    li r4, 0x0
    li r5, 0x0
    bl GetParam__12RPPathObjectCFUsl
    lwz r22, 0x28(r27)
    stw r3, 0x38(r27)
    mr r3, r22
    lwz r12, 0x10(r22)
    lwz r12, 0x8(r12)
    mtctr r12
    bctrl
    lwz r7, 0x38(r27)
    mr r4, r3
    lfs f1, lbl_80440D1C
    mr r3, r27
    addi r5, r22, 0x40
    li r6, 0x0
    li r8, 0x0
    bl SpawnKakasi__16RPCowKakasiGroupFRCQ23EGG8Vector3fRCQ23EGG8Vector3flllf
    li r23, 0x0
    b L_802AAD08
    L_802AACD0:
    clrlwi r0, r23, 16
    lwz r4, 0x10(r30)
    lwz r12, 0x20(r27)
    mulli r0, r0, 0x72e8
    lwz r4, 0x1c(r4)
    mr r3, r27
    lwz r12, 0x34(r12)
    add r22, r4, r0
    mtctr r12
    bctrl
    clrlwi r4, r3, 16
    mr r3, r22
    bl SetTargetGroup__11RPCowPlayerFl
    addi r23, r23, 0x1
    L_802AAD08:
    cmpw r23, r31
    blt L_802AACD0
    b L_802AAEA8
    L_802AAD14:
    lwz r22, spActorList__12RPActorScene
    li r25, 0x0
    L_802AAD1C:
    clrlwi r0, r25, 16
    lwz r3, 0x10(r22)
    mulli r0, r0, 0x154
    lwz r3, 0x8(r3)
    add r3, r3, r0
    lwz r0, 0x150(r3)
    cmplw r0, r27
    bne L_802AAD40
    bl StartSink__13RPCowFieldObjFv
    L_802AAD40:
    addi r25, r25, 0x1
    cmpwi r25, 0x20
    blt L_802AAD1C
    lwz r3, 0x28(r27)
    li r4, 0x0
    li r0, 0x2
    stw r4, 0xc(r3)
    stw r4, 0x30(r27)
    stw r0, 0x2c(r27)
    b L_802AAEA8
    L_802AAD68:
    lwz r3, 0x24(r3)
    li r4, 0x0
    li r5, 0x0
    bl GetParam__12RPPathObjectCFUsl
    lwz r4, 0x38(r27)
    cmpw r4, r3
    bge L_802AADE0
    lwz r3, 0x30(r27)
    addi r0, r3, 0x1
    cmpwi r0, 0x4
    stw r0, 0x30(r27)
    blt L_802AADE0
    addi r22, r4, 0x1
    li r0, 0x0
    lwz r23, 0x28(r27)
    stw r22, 0x38(r27)
    mr r3, r23
    stw r0, 0x30(r27)
    lwz r12, 0x10(r23)
    lwz r12, 0x8(r12)
    mtctr r12
    bctrl
    lfs f1, lbl_80440D1C
    mr r4, r3
    mr r3, r27
    mr r7, r22
    addi r5, r23, 0x40
    subi r6, r22, 0x1
    li r8, 0x0
    bl SpawnKakasi__16RPCowKakasiGroupFRCQ23EGG8Vector3fRCQ23EGG8Vector3flllf
    L_802AADE0:
    lfs f31, lbl_80440D20
    li r22, 0x1
    li r23, 0x0
    b L_802AAE48
    L_802AADF0:
    clrlwi r0, r23, 16
    lwz r4, 0x10(r30)
    mulli r0, r0, 0x72e8
    addi r3, r1, 0x8
    lwz r4, 0x1c(r4)
    add r4, r4, r0
    bl GetPos__11RPCowPlayerCFv
    lwz r3, 0x28(r27)
    lwz r12, 0x10(r3)
    lwz r12, 0x8(r12)
    mtctr r12
    bctrl
    lfs f1, 0x8(r3)
    lfs f0, 0x10(r1)
    fsubs f1, f1, f31
    fcmpo cr0, f0, f1
    cror 2, 1, 2
    mfcr r0
    extrwi. r0, r0, 1, 2
    beq L_802AAE44
    li r22, 0x0
    L_802AAE44:
    addi r23, r23, 0x1
    L_802AAE48:
    cmpw r23, r31
    blt L_802AADF0
    cmpwi r22, 0x0
    beq L_802AAEA8
    lwz r22, spActorList__12RPActorScene
    li r25, 0x0
    L_802AAE60:
    clrlwi r0, r25, 16
    lwz r3, 0x10(r22)
    mulli r0, r0, 0x154
    lwz r3, 0x8(r3)
    add r3, r3, r0
    lwz r0, 0x150(r3)
    cmplw r0, r27
    bne L_802AAE84
    bl StartSink__13RPCowFieldObjFv
    L_802AAE84:
    addi r25, r25, 0x1
    cmpwi r25, 0x20
    blt L_802AAE60
    lwz r3, 0x28(r27)
    li r4, 0x0
    li r0, 0x2
    stw r4, 0xc(r3)
    stw r4, 0x30(r27)
    stw r0, 0x2c(r27)
    L_802AAEA8:
    lwz r3, 0x40(r27)
    cmpwi r3, 0x0
    ble L_802AAF18
    subic. r0, r3, 0x1
    stw r0, 0x40(r27)
    bgt L_802AAF18
    lwz r0, 0x4c(r27)
    li r4, 0x3
    lwz r6, 0x10(r30)
    li r5, 0x5
    clrlwi r0, r0, 16
    lwz r3, 0x24(r27)
    mulli r0, r0, 0x72e8
    lwz r6, 0x1c(r6)
    add r22, r6, r0
    bl GetParam__12RPPathObjectCFUsl
    lwz r5, 0x44(r27)
    mr r4, r3
    mr r3, r22
    bl AddGroupScore__11RPCowPlayerFll
    lwz r0, 0x44(r27)
    cmpwi r0, 0x3
    blt L_802AAF0C
    li r0, 0xf
    stw r0, 0x48(r27)
    L_802AAF0C:
    li r0, 0x0
    stw r0, 0x40(r27)
    stw r0, 0x44(r27)
    L_802AAF18:
    psq_l f31, 0x78(r1), 0, 0
    addi r11, r1, 0x70
    lfd f31, 0x70(r1)
    bl _restgpr_22
    lwz r0, 0x84(r1)
    mtlr r0
    addi r1, r1, 0x80
    blr
}

#endif

// The C++ equivalent: the host form.
#if defined(PORT_HOST)
// NONMATCHING: C++ equivalent, 99.90%; target loop swaps r22 and r23.
void RPCowKakasiGroup::OnActivate() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    s32 num = GetCheckNum(pGame);
    switch (mState) {
    case 0:
        if (IsPlaying(pGame)) {
            bool near = false;
            for (int i = 0; i < num; i++) {
                RPCowPlayer* pPlayer = GetPlayer(pGame, i);
                bool isNear = true;
                bool kingNear = false;
                if (IsKingRes(mpPathObj)) {
                    EGG::Vector3f pos = pPlayer->GetPos();
                    if (pos.z <= 700.0f + mpCollision->GetPos().z) {
                        kingNear = true;
                    }
                }
                if (!kingNear) {
                    bool kakasiNear = false;
                    if (!IsKingRes(mpPathObj)) {
                        EGG::Vector3f pos = pPlayer->GetPos();
                        if (pos.z <= 700.0f + mpCollision->GetPos().z) {
                            kakasiNear = true;
                        }
                    }
                    if (!kakasiNear) {
                        isNear = false;
                    }
                }
                if (isNear) {
                    near = true;
                }
            }
            if (near) {
                if (pGame->GetPlayerNum() >= 2 && IsKingRes(mpPathObj)) {
                    if (--sSpawnCounter <= 0) {
                        sSpawnCounter = (u32)(2.0f * RPUtlRandom::getF32()) + 3;
                    } else {
                        near = false;
                    }
                }
                if (near && mpPathObj->GetParam(0, 0) > 0) {
                    mNum = mpPathObj->GetParam(0, 0);
                    RPCollisionShape* pShape = mpCollision;
                    SpawnKakasi(pShape->GetPos(), pShape->GetVec40(), 0, mNum, 0, lbl_80440D1C);
                    for (s32 index = 0; index < num; index++) {
                        RPCowPlayer* const pPlayer = GetPlayer(pGame, index);
                        pPlayer->SetTargetGroup(GetUnk1A());
                    }
                } else {
                    {
                    RPCowGame* pSinkGame = RPCowGame::GetInstance();
                    SinkActors(pSinkGame);
                    mpCollision->mpOwner = NULL;
                    mTimer = 0;
                    mState = 2;
                }
                }
            }
        }
        break;
    case 1: {
        if (mNum < mpPathObj->GetParam(0, 0)) {
            if (++mTimer >= 4) {
                s32 n = mNum + 1;
                mNum = n;
                mTimer = 0;
                RPCollisionShape* pShape = mpCollision;
                SpawnKakasi(pShape->GetPos(), pShape->GetVec40(), n - 1, n, 0, lbl_80440D1C);
            }
        }
        bool passed = true;
        for (int i = 0; i < num; i++) {
            EGG::Vector3f pos = GetPlayer(pGame, i)->GetPos();
            bool ahead = pos.z >= mpCollision->GetPos().z - 700.0f;
            if (ahead) {
                passed = false;
            }
        }
        if (passed) {
            {
                    RPCowGame* pSinkGame = RPCowGame::GetInstance();
                    SinkActors(pSinkGame);
                    mpCollision->mpOwner = NULL;
                    mTimer = 0;
                    mState = 2;
                }
        }
        break;
    }
    }

    if (mScoreTimer > 0) {
        if (--mScoreTimer <= 0) {
            RPCowPlayer* pPlayer = GetPlayer(pGame, mPlayer);
            pPlayer->AddGroupScore(mpPathObj->GetParam(3, 5), mBonusLevel);
            if (mBonusLevel >= 3) {
                unk48 = 15;
            }
            mScoreTimer = 0;
            mBonusLevel = 0;
        }
    }
}

#endif

void RPCowKakasiGroup::Calculate() {}

void RPCowKakasiGroup::DrawScreen(u8 pass) {}

inline bool RPCowKakasiGroup::IsKingNear(RPCowPlayer* pPlayer) {
    bool near = false;
    if (IsKingRes(mpPathObj)) {
        EGG::Vector3f pos = pPlayer->GetPos();
        if (pos.z <= 700.0f + mpCollision->GetPos().z) {
            near = true;
        }
    }
    return near;
}

inline bool RPCowKakasiGroup::IsKakasiNear(RPCowPlayer* pPlayer) {
    bool near = false;
    if (!IsKingRes(mpPathObj)) {
        EGG::Vector3f pos = pPlayer->GetPos();
        if (pos.z <= 700.0f + mpCollision->GetPos().z) {
            near = true;
        }
    }
    return near;
}

inline bool RPCowKakasiGroup::IsPlayerNear(RPCowPlayer* pPlayer) {
    return IsKingNear(pPlayer) || IsKakasiNear(pPlayer);
}

inline bool RPCowKakasiGroup::IsPlayerAhead(RPCowPlayer* pPlayer) {
    EGG::Vector3f pos = pPlayer->GetPos();
    return pos.z >= mpCollision->GetPos().z - 700.0f;
}

inline void RPCowKakasiGroup::SinkAll() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    for (int i = 0; i < 32; i++) {
        RPCowKakasi* p = GetKakasi(pGame, i);
        if (p->mGroup == this) {
            p->StartSink();
        }
    }
    mpCollision->mpOwner = NULL;
    mTimer = 0;
    mState = 2;
}

s32 RPCowKakasiGroup::sSpawnCounter;
const f32 RPCowKakasiGroup::sRadiusScale = 5.0f;
const f32 lbl_80440D1C = 20.0f;
const f32 lbl_80440D20 = 700.0f;
const f32 lbl_80440D24 = 2.0f;
const f64 lbl_80440D28 = 4503599627370496.0;

inline void RPCowKakasiGroup::SinkActors(RPCowGame* pGame) {
    for (int i = 0; i < 32; i++) {
        RPCowKakasi* p = GetKakasi(pGame, i);
        if (p->mGroup == this) {
            p->StartSink();
        }
    }
}
