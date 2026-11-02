// Charge! course object base (RPCowFieldObj) and its twelve states. Unit 0x802A65F0-0x802A94F8 (dtk/game_units.tsv). Functions are defined in
// reverse address order.
#include <Pack/RPParty/RPCowScene/RPCowFieldObj.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPGraphics/RPGrpModelMaterial.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPParty/RPCowScene/RPCowCamera.h>
#include <Pack/RPParty/RPPartyCommon/RPCollisionModel.h>
#include <Pack/RPParty/RPCowScene/RPCowField.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPCowScene/RPCowModelBatch.h>
#include <Pack/RPParty/RPCowScene/RPCowPlayer.h>
#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>
#include <Pack/RPUtility/RPUtlRandom.h>

#include <egg/math.h>
#include <nw4r/math.h>

extern "C" u8 Lyt_getCurrentViewNo();
extern "C" const EGG::Matrix34f RP_eggMtxIdentity;
extern "C" const EGG::Vector3f RP_eggVecZero;
extern "C" void RP_CollisionShape_getMtx(EGG::Matrix34f* pOut, const RPCollisionShape* pShape);
#if defined(PORT_HOST)
// PORT_HOST: RPCollisionShape.cpp defines these with the pointers first. The PowerPC ABI (and
// x86-64 System V) pass floats and pointers in separate registers, so the order did not
// matter; Windows x64 assigns registers by position. These wrappers keep this unit's calls
// and call the definitions' form (port/docs/HOST_BUILD.md §41).
extern "C" void RP_dampVec3AndApply_Def(EGG::Vector3f*, EGG::Vector3f*, f32) __asm__("RP_dampVec3AndApply");
static inline void RP_dampVec3AndApply(f32 rate, EGG::Vector3f* pPos, EGG::Vector3f* pVel) {
    RP_dampVec3AndApply_Def(pPos, pVel, rate);
}
#else
extern "C" void RP_dampVec3AndApply(f32 rate, EGG::Vector3f* pPos, EGG::Vector3f* pVel);
#endif
extern "C" void RP_dampAndApply(f32* pPos, f32* pVel, f32 rate);
#if defined(PORT_HOST)
// PORT_HOST: RPCollisionShape.cpp defines these with the pointers first. The PowerPC ABI (and
// x86-64 System V) pass floats and pointers in separate registers, so the order did not
// matter; Windows x64 assigns registers by position. These wrappers keep this unit's calls
// and call the definitions' form (port/docs/HOST_BUILD.md §41).
extern "C" bool RP_isVec3DiffWithinAbs_Def(const EGG::Vector3f*, const EGG::Vector3f*, f32) __asm__("RP_isVec3DiffWithinAbs");
static inline bool RP_isVec3DiffWithinAbs(f32 abs, const EGG::Vector3f* pLhs, const EGG::Vector3f* pRhs) {
    return RP_isVec3DiffWithinAbs_Def(pLhs, pRhs, abs);
}
extern "C" bool RP_isVec3WithinAbs_Def(const EGG::Vector3f*, f32) __asm__("RP_isVec3WithinAbs");
static inline bool RP_isVec3WithinAbs(f32 abs, const EGG::Vector3f* pVec) {
    return RP_isVec3WithinAbs_Def(pVec, abs);
}
extern "C" void RP_wrapAngleNear_Def(f32*, f32) __asm__("RP_wrapAngleNear");
static inline void RP_wrapAngleNear(f32 ref, f32* pAngle) {
    RP_wrapAngleNear_Def(pAngle, ref);
}
extern "C" void RP_springStep_Def(f32*, f32*, f32, f32) __asm__("RP_springStep");
static inline void RP_springStep(f32 target, f32 rate, f32* pValue, f32* pVel) {
    RP_springStep_Def(pValue, pVel, target, rate);
}
#else
extern "C" bool RP_isVec3DiffWithinAbs(f32 abs, const EGG::Vector3f* pLhs,
                                       const EGG::Vector3f* pRhs);
extern "C" bool RP_isVec3WithinAbs(f32 abs, const EGG::Vector3f* pVec);
extern "C" void RP_wrapAngleNear(f32 ref, f32* pAngle);
extern "C" void RP_springStep(f32 target, f32 rate, f32* pValue, f32* pVel);
#endif
// 0.01 in the framework's .sdata2 (8043ECB8)
extern "C" const f32 RP_epsilon;

extern "C" bool Cow_Camera_isInView(RPCowCamera* pCamera, const EGG::Vector3f* pPos);

namespace {

inline RPCowPlayer* GetPlayer(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowPlayer*>(pGame->GetSlotActor(7))[idx];
}

inline RPCowCamera* GetCamera(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowCamera*>(pGame->GetSlotActor(0))[idx];
}

// TEV color register 0
inline GXColorS10* ShadowColor(RPGrpModelMaterial* pMat) {
    return &pMat->GetTevColor(RPGrpModelMaterial::ETevColor_Reg0);
}

// Alpha of TEV color register 0
inline s16& ShadowAlpha(RPGrpModelMaterial* pMat) {
    return ShadowColor(pMat)->a;
}

} // namespace

nw4r::snd::SoundHandle Cow_bgmHandle;
nw4r::snd::SoundHandle Cow_seHandle;

extern "C" void Cow_playBgm(u32 id) {
    Cow_bgmHandle.Stop(0);
    RPSndAudioMgr::instance()->startSound(&Cow_bgmHandle, id);
}

extern "C" bool Cow_stopBgm() {
    if (Cow_bgmHandle.IsAttachedSound()) {
        Cow_bgmHandle.Stop(0);
        return true;
    }
    return false;
}

extern "C" bool Cow_holdGlobalSe(u32 id) {
    return RPSndAudioMgr::instance()->getPlayer().HoldSound(&Cow_seHandle, id);
}

EGG::Vector3f RPCowFieldObj::GetForwardDir() {
    EGG::Vector3f dir;
    dir.x = nw4r::math::SinRad(mpCollision->GetVec40().y);
    dir.y = 0.0f;
    dir.z = nw4r::math::CosRad(mpCollision->GetVec40().y);
    return dir;
}

void RPCowFieldObj::SpawnDrop(const EGG::Vector3f& rPos, s32 drop) {
    mpCollision->GetPos().x = rPos.x;
    mpCollision->GetPos().y = sStopY;
    mpCollision->GetPos().z = rPos.z;
    mpCollision->GetRot().y = sSinkSpeed;
    mpCollision->GetVec40().y = drop != 0 ? 0.0f : 6.2831855f * RPUtlRandom::getF32();
    mpCollision->GetVec4C().y = 0.0f;
    mpState = &mStateDrop;
    mpNextState = &mStateHop;

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, mpCollision);
    mMtx = mtx;
    mBaseMtx = mMtx;

    if (mpShadowMaterial != NULL) {
        ShadowAlpha(mpShadowMaterial) = 0;
        mpShadowMaterial->SetSyncFlags(0x10);
    }

    RPSysEffect effect("Cow_kksking_jumpsmk", 0, 0);
    effect.create();
    effect.setPos(rPos);
    effect.update();
}

void RPCowFieldObj::SpawnAppear(const EGG::Vector3f& rPos, const EGG::Vector3f& rRot) {
    mpCollision->GetPos().x = rPos.x;
    mpCollision->GetPos().y = sStopY;
    mpCollision->GetPos().z = rPos.z;
    mpCollision->GetRot().y = 1.5f;
    mpCollision->GetVec40().y = 6.2831855f * RPUtlRandom::getF32();
    mpCollision->GetVec4C().y = 0.2f;
    unkEC.z = rRot.y;
    mpState = &mStateRise;
    mpNextState = &mStateKingJump3;

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, mpCollision);
    mMtx = mtx;
    mBaseMtx = mMtx;

    if (mpShadowMaterial != NULL) {
        ShadowAlpha(mpShadowMaterial) = 0;
        mpShadowMaterial->SetSyncFlags(0x10);
    }

    RPSysEffect effect("Cow_kksking_appear", 0, 0);
    effect.create();
    effect.setPos(rPos);
    effect.update();
}

void RPCowFieldObj::StartSink() {
    bool knocked = (mpState == &mStateKnockSlide || mpState == &mStateKnockFly) || mpState == &mStateKnockHurdle;
    if (!knocked) {
        mpCollision->GetRot().y = -sSinkSpeed;
        mpState = &mStateSink;
        mpNextState = NULL;
    }
}

void RPCowFieldObj::KnockSlide() {
    mpState = &mStateKnockSlide;
    mpNextState = NULL;
}

void RPCowFieldObj::KnockFly() {
    mpState = &mStateKnockFly;
    mpNextState = NULL;
}

void RPCowFieldObj::KnockHurdle() {
    mpState = &mStateKnockHurdle;
    mpNextState = NULL;
}

void RPCowFieldObj::StateDrop::Calc(RPCowFieldObj* pOwner) {
    if (pOwner->mpCollision->GetPos().y >= sGroundY) {
        pOwner->mpCollision->GetPos().y = sGroundY;
        pOwner->mpCollision->GetRot().y = 0.0f;
        if (pOwner->mpNextState != NULL) {
            pOwner->mpState = pOwner->mpNextState;
            pOwner->unk104 = 0;
        } else {
            pOwner->mpState = &pOwner->mStateSink;
        }
    } else {
        pOwner->mpCollision->GetPos().y += pOwner->mpCollision->GetRot().y;
    }
}

void RPCowFieldObj::StateRise::Calc(RPCowFieldObj* pOwner) {
    if (pOwner->mpCollision->GetPos().y >= sGroundY) {
        pOwner->mpCollision->GetRot().y -= 0.1f;
        RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
        RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetVec40(),
                            &pOwner->mpCollision->GetVec4C());
        if (pOwner->mpCollision->GetPos().y <= sGroundY && pOwner->mpCollision->GetRot().y < 0.0f) {
            pOwner->mpCollision->GetPos().y = sGroundY;
            pOwner->mpCollision->GetRot().y = 0.0f;
            if (pOwner->mpNextState != NULL) {
                pOwner->mpState = pOwner->mpNextState;
                pOwner->unk104 = 0;
            } else {
                pOwner->mpState = &pOwner->mStateSink;
            }
        }
    } else {
        pOwner->mpCollision->GetPos().y += pOwner->mpCollision->GetRot().y;
        pOwner->mpCollision->GetVec40().y += pOwner->mpCollision->GetVec4C().y;
    }

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, pOwner->mpCollision);
    pOwner->mMtx = mtx;
    pOwner->mBaseMtx = pOwner->mMtx;
}

void RPCowFieldObj::StateSink::Calc(RPCowFieldObj* pOwner) {
    pOwner->mpCollision->GetRot().y -= 0.02f;
    RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
    pOwner->mpCollision->GetVec40().y += pOwner->mpCollision->GetVec4C().y;
    if (pOwner->mpCollision->GetPos().y <= sStopY) {
        RPActorScene::GetActorList()->StopActor(pOwner);
    }
}

void RPCowFieldObj::StateIdle::Calc(RPCowFieldObj* /* pOwner */) {}

void RPCowFieldObj::StateHop::Calc(RPCowFieldObj* pOwner) {
    pOwner->mpCollision->GetRot().y -= 0.02f;
    if (pOwner->mpCollision->GetPos().y <= sGroundY) {
        pOwner->mpCollision->GetPos().y = sGroundY;
        pOwner->mpCollision->GetRot().y = 0.2f;
    }
    RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
}

void RPCowFieldObj::StateKingJump3::Calc(RPCowFieldObj* pOwner) {
    RPCowGame* pGame = RPCowGame::GetInstance();
    pOwner->mpCollision->GetRot().y -= 0.1f;
    if (pOwner->mpCollision->GetPos().y <= sGroundY) {
        pOwner->mpCollision->GetPos().y = sGroundY;
        pOwner->mpCollision->GetRot().x = 0.0f;
        pOwner->mpCollision->GetRot().y = 1.0f;
        pOwner->mpCollision->GetRot().z = 0.0f;
        if ((u32)(pGame->GetState() - 9) > 15) {
            pOwner->mpSound->startSound(0x4F);
        }
        RPSysEffect effect("Cow_kksking_jumpsmk", 0, 0);
        effect.create();
        effect.setPos(pOwner->mpCollision->GetPos());
        effect.update();
    }
    RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());

    EGG::Vector3f dir = RP_eggVecZero;
    s32 num = pGame->GetPlayerNum();
    for (int i = 0; i < num; i++) {
        dir += GetPlayer(pGame, i)->GetPos();
    }
    dir = dir / num - pOwner->mpCollision->GetPos();
    if (!RP_isVec3WithinAbs(RP_epsilon, &dir)) {
        f32 yaw = nw4r::math::Atan2Rad(dir.x, dir.z);
        RP_wrapAngleNear(yaw, &pOwner->mpCollision->GetVec40().y);
        RP_springStep(yaw, 0.1f, &pOwner->mpCollision->GetVec40().y,
                      &pOwner->mpCollision->GetVec4C().y);
    }

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, pOwner->mpCollision);
    pOwner->mMtx = mtx;
    pOwner->mBaseMtx = pOwner->mMtx;

    if (++pOwner->unk104 >= (pGame->GetPlayerNum() <= 1 ? 40 : 0) &&
        pOwner->mpCollision->GetPos().y <= sGroundY) {
        pOwner->unk114 = 0;
        pOwner->unk104 = 0;
        pOwner->mpState = &pOwner->mStateKingJump2;
    }
}

void RPCowFieldObj::StateKingJump2::Calc(RPCowFieldObj* pOwner) {
    RPCowGame* pGame = RPCowGame::GetInstance();
    switch (pOwner->unk110) {
    case 1:
        pOwner->mpCollision->GetRot().y -= 0.1f;
        break;
    case 2:
        pOwner->mpCollision->GetRot().y -= 0.1f;
        break;
    case 3:
        pOwner->mpCollision->GetRot().y -= 0.1f;
        break;
    }

    EGG::Vector3f dir = RP_eggVecZero;
    s32 num = pGame->GetPlayerNum();
    for (int i = 0; i < num; i++) {
        dir += GetPlayer(pGame, i)->GetPos();
    }
    dir = dir / num - pOwner->mpCollision->GetPos();
    if (!RP_isVec3WithinAbs(RP_epsilon, &dir)) {
        f32 yaw = nw4r::math::Atan2Rad(dir.x, dir.z);
        RP_wrapAngleNear(yaw, &pOwner->mpCollision->GetVec40().y);
        RP_springStep(yaw, 0.1f, &pOwner->mpCollision->GetVec40().y,
                      &pOwner->mpCollision->GetVec4C().y);
    }

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, pOwner->mpCollision);
    pOwner->mMtx = mtx;
    pOwner->mBaseMtx = pOwner->mMtx;

    EGG::Vector3f fwd;
    fwd.x = nw4r::math::SinRad(pOwner->unkEC.z);
    fwd.y = 0.0f;
    fwd.z = nw4r::math::CosRad(pOwner->unkEC.z);
    EGG::Vector3f side;
    side.x = nw4r::math::SinRad(1.5707964f + pOwner->unkEC.z);
    side.y = 0.0f;
    side.z = nw4r::math::CosRad(1.5707964f + pOwner->unkEC.z);

    if (pOwner->mpCollision->GetPos().y <= sGroundY) {
        f32 fwdSpeed = 0.5f * pOwner->unkF8;
        f32 sideSpeed = 0.5f * pOwner->unkFC;
        f32 jump = 0.0f;
        switch (pOwner->unk110) {
        case 1:
            jump = 0.45f + (0.5f - 0.45f) * pOwner->unkF8;
            break;
        case 2:
            jump = 0.8f + (0.9f - 0.8f) * pOwner->unkF8;
            break;
        case 3:
            jump = 0.8f + (0.9f - 0.8f) * pOwner->unkF8;
            break;
        }

        u16 flags = 0;
        EGG::Vector3f probe;
        probe.x = pOwner->mpCollision->GetPos().x;
        probe.y = 0.0f;
        probe.z = pOwner->mpCollision->GetPos().z;
        if (pGame->GetCollisionModel()
                ->CheckSphere(sProbeRadius, probe, NULL, NULL, &flags)) {
            if (flags & 2) {
                if (++pOwner->unk114 >= 5) {
                    pOwner->StartSink();
                    jump = 0.0f;
                }
            } else {
                pOwner->unk114 = 0;
            }
        }

        pOwner->mpCollision->GetPos().y = sGroundY;
        if (jump > 0.0f) {
            pOwner->mpCollision->GetRot().x = fwd.x * fwdSpeed + side.x * sideSpeed;
            pOwner->mpCollision->GetRot().y = jump;
            pOwner->mpCollision->GetRot().z = fwd.z * fwdSpeed + side.z * sideSpeed;
            if (pOwner->unk110 == 3) {
                pOwner->unkFC = -pOwner->unkFC;
            }
            if ((u32)(pGame->GetState() - 9) > 15) {
                pOwner->mpSound->startSound(0x4F);
            }
            RPSysEffect effect("Cow_kksking_jumpsmk", 0, 0);
            effect.create();
            effect.setPos(pOwner->mpCollision->GetPos());
            effect.update();
        }
    }

    RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());

    if (pOwner->unk110 == 1) {
        u16 flags = 0;
        EGG::Vector3f probe;
        probe.x = pOwner->mpCollision->GetPos().x + pOwner->unkF8 * (fwd.x * sProbeDist) +
                  pOwner->unkFC * (side.x * sProbeDist);
        probe.y = 0.0f;
        probe.z = pOwner->mpCollision->GetPos().z + pOwner->unkF8 * (fwd.z * sProbeDist) +
                  pOwner->unkFC * (side.z * sProbeDist);
        if (pGame->GetCollisionModel()
                ->CheckSphere(sProbeRadius, probe, NULL, NULL, &flags) &&
            (flags & 2)) {
            pOwner->unkFC = -pOwner->unkFC;
        }
    }
}

void RPCowFieldObj::StateKingJump1::Calc(RPCowFieldObj* pOwner) {
    RPCowGame* pGame = RPCowGame::GetInstance();
    pOwner->mpCollision->GetRot().y -= 0.1f;
    if (pOwner->mpCollision->GetPos().y <= sGroundY) {
        pOwner->mpCollision->GetPos().y = sGroundY;
        pOwner->mpCollision->GetRot().x = 0.0f;
        pOwner->mpCollision->GetRot().y = 1.2f;
        pOwner->mpCollision->GetRot().z = 0.0f;
        if ((u32)(pGame->GetState() - 9) > 15) {
            pOwner->mpSound->startSound(0x4F);
        }
        RPSysEffect effect("Cow_kksking_jumpsmk", 0, 0);
        effect.create();
        effect.setPos(pOwner->mpCollision->GetPos());
        effect.update();
    }
    RP_dampAndApply(&pOwner->mpCollision->GetPos().x, &pOwner->mpCollision->GetRot().x, 0.9f);
    RP_dampAndApply(&pOwner->mpCollision->GetPos().y, &pOwner->mpCollision->GetRot().y, 0.99f);
    RP_dampAndApply(&pOwner->mpCollision->GetPos().z, &pOwner->mpCollision->GetRot().z, 0.9f);

    EGG::Vector3f dir = RP_eggVecZero;
    s32 num = pGame->GetPlayerNum();
    for (int i = 0; i < num; i++) {
        dir += GetPlayer(pGame, i)->GetPos();
    }
    dir = dir / num - pOwner->mpCollision->GetPos();
    if (!RP_isVec3WithinAbs(RP_epsilon, &dir)) {
        f32 yaw = nw4r::math::Atan2Rad(dir.x, dir.z);
        RP_wrapAngleNear(yaw, &pOwner->mpCollision->GetVec40().y);
        RP_springStep(yaw, 0.1f, &pOwner->mpCollision->GetVec40().y,
                      &pOwner->mpCollision->GetVec4C().y);
    }

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, pOwner->mpCollision);
    pOwner->mMtx = mtx;
    pOwner->mBaseMtx = pOwner->mMtx;

    if (++pOwner->unk104 >= 60) {
        pOwner->mpCollision->GetRot().x = 1.5f * nw4r::math::SinRad(pOwner->unkEC.z);
        pOwner->mpCollision->GetRot().y = sKingJumpSpeed;
        pOwner->mpCollision->GetRot().z = 1.5f * nw4r::math::CosRad(pOwner->unkEC.z);
        pOwner->unk104 = 0;
        pOwner->mpState = &pOwner->mStateFallSpin;
        pOwner->mpSound->startSound(0x4F);
    }
}

void RPCowFieldObj::StateFallSpin::Calc(RPCowFieldObj* pOwner) {
    pOwner->mpCollision->GetRot().y -= 0.1f;
    RP_dampVec3AndApply(0.99f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
    pOwner->mpCollision->GetVec40().y += pOwner->mpCollision->GetVec4C().y;
    if (pOwner->mpCollision->GetPos().y <= sStopY) {
        RPActorScene::GetActorList()->StopActor(pOwner);
    }
}

void RPCowFieldObj::StateKnockSlide::Calc(RPCowFieldObj* pOwner) {
    pOwner->mpCollision->GetRot().y -= 0.02f;
    RP_dampVec3AndApply(1.0f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
    RP_dampAndApply(&pOwner->unkEC.x, &pOwner->unkEC.y, 0.98f);
    if (!RP_isVec3DiffWithinAbs(RP_epsilon, &pOwner->unkE0, &RP_eggVecEy)) {
        EGG::Matrix34f rot;
        pOwner->mBaseMtx.makeR(EGG::Vector3f(0.0f, pOwner->mpCollision->GetVec40().y, 0.0f));
        rot.setAxisRotation(pOwner->unkE0, pOwner->unkEC.x);
        rot.multiplyTo(pOwner->mBaseMtx, pOwner->mMtx);
    } else {
        pOwner->mBaseMtx.makeR(
            EGG::Vector3f(0.0f, pOwner->unkEC.x + pOwner->mpCollision->GetVec40().y, 0.0f));
        pOwner->mMtx = pOwner->mBaseMtx;
    }
    if (pOwner->mpCollision->GetPos().y <= sStopY) {
        RPActorScene::GetActorList()->StopActor(pOwner);
    }
}

void RPCowFieldObj::StateKnockFly::Calc(RPCowFieldObj* pOwner) {
    pOwner->mpCollision->GetRot().y -= 0.1f;
    RP_dampVec3AndApply(1.0f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
    RP_dampAndApply(&pOwner->unkEC.x, &pOwner->unkEC.y, 0.96f);
    if (!RP_isVec3DiffWithinAbs(RP_epsilon, &pOwner->unkE0, &RP_eggVecEy)) {
        EGG::Matrix34f rot;
        pOwner->mBaseMtx.makeR(EGG::Vector3f(0.0f, pOwner->mpCollision->GetVec40().y, 0.0f));
        rot.setAxisRotation(pOwner->unkE0, pOwner->unkEC.x);
        rot.multiplyTo(pOwner->mBaseMtx, pOwner->mMtx);
        f32 yaw = nw4r::math::Atan2Rad(pOwner->mMtx._01, pOwner->mMtx._21);
        EGG::Vector3f up;
        up.x = pOwner->mMtx._01;
        up.y = 0.0f;
        up.z = pOwner->mMtx._21;
        f32 len = pOwner->unkD4.z * up.length();
        pOwner->mBaseMtx.makeSRT(EGG::Vector3f(1.0f, 1.0f, 1.0f + len),
                                 EGG::Vector3f(0.0f, yaw, 0.0f),
                                 EGG::Vector3f(0.0f, 0.0f, 0.0f));
    } else {
        pOwner->mBaseMtx.makeR(
            EGG::Vector3f(0.0f, pOwner->unkEC.x + pOwner->mpCollision->GetVec40().y, 0.0f));
        pOwner->mMtx = pOwner->mBaseMtx;
    }
    if (pOwner->mpCollision->GetPos().y <= sStopY) {
        RPActorScene::GetActorList()->StopActor(pOwner);
    }
}

void RPCowFieldObj::StateKnockHurdle::Calc(RPCowFieldObj* pOwner) {
    pOwner->mpCollision->GetRot().y -= 0.1f;
    RP_dampVec3AndApply(0.98f, &pOwner->mpCollision->GetPos(), &pOwner->mpCollision->GetRot());
    RP_dampAndApply(&pOwner->unkEC.x, &pOwner->unkEC.y, 0.94f);
    if (!RP_isVec3DiffWithinAbs(RP_epsilon, &pOwner->unkE0, &RP_eggVecEy)) {
        EGG::Matrix34f rot;
        pOwner->mBaseMtx.makeR(EGG::Vector3f(0.0f, pOwner->mpCollision->GetVec40().y, 0.0f));
        rot.setAxisRotation(pOwner->unkE0, pOwner->unkEC.x);
        rot.multiplyTo(pOwner->mBaseMtx, pOwner->mMtx);
        f32 yaw = nw4r::math::Atan2Rad(pOwner->mMtx._01, pOwner->mMtx._21);
        EGG::Vector3f up;
        up.x = pOwner->mMtx._01;
        up.y = 0.0f;
        up.z = pOwner->mMtx._21;
        f32 len = pOwner->unkD4.z * up.length();
        pOwner->mBaseMtx.makeSRT(EGG::Vector3f(1.0f, 1.0f, 1.0f + len),
                                 EGG::Vector3f(0.0f, yaw, 0.0f),
                                 EGG::Vector3f(0.0f, 0.0f, 0.0f));
    } else {
        pOwner->mBaseMtx.makeR(
            EGG::Vector3f(0.0f, pOwner->unkEC.x + pOwner->mpCollision->GetVec40().y, 0.0f));
        pOwner->mMtx = pOwner->mBaseMtx;
    }
    if (pOwner->mpCollision->GetPos().y <= sStopY) {
        RPActorScene::GetActorList()->StopActor(pOwner);
    }
}

// FAKEMATCH: the registers of the inlined RPCowObjSound constructor's vtable stores (vtable+0x24
// of TRPSndAudioActorBaseWithHandles<4,4> in r0, `li r0, 2` after its store) need the default
// constructor (player fetched inside, else r28-r31 rotate) with its CtorScope stand-in AND
// opt_dead_code off; with the TRPSndGameObject<4> level + helper instead (as RPFshFishSnd) the
// code matches too, but that emits TRPSndGameObject<4> data in the Charge! units.
// opt_propagation off (RPFshFish's pragma) fixes the vtable stores but swaps `li r26, 0` /
// `mr r24, r28` before the 3D actor loop.
#pragma opt_dead_code off
void RPCowFieldObj::VF_0x3C() {
    mpLodModels[0] = NULL;
    mpLodModels[1] = NULL;
    mpLodModels[2] = NULL;
    mpLodModels[3] = NULL;
    mpLodModels[4] = NULL;
    mpLodModels[5] = NULL;
    mpSound = new RPCowObjSound();
}
#pragma opt_dead_code reset

void RPCowFieldObj::VF_0x40() {
    if (mpSound != NULL) {
        delete mpSound;
    }
}

void RPCowFieldObj::SetSoundMode() {
    if (RPCowGame::GetInstance()->GetPlayerNum() <= 1) {
        mpSound->SetObjMode(0);
    } else {
        mpSound->SetObjMode(1);
    }
}

void RPCowFieldObj::ResetTransform() {
    mpCollision->Reset();
    mpCollision->GetPos().x = mpPathObj->GetPos().x;
    mpCollision->GetPos().y = sStopY;
    mpCollision->GetPos().z = mpPathObj->GetPos().z;
    mpCollision->GetVec40().y =
        nw4r::math::Atan2Rad(mpPathObj->GetVec20().x, mpPathObj->GetVec20().z);
    mpCollision->mpOwner = static_cast<IRPActorEnable*>(this);

    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, mpCollision);
    mMtx = mtx;
    mBaseMtx = mMtx;

    unkE0 = RP_eggVecEy;
    unkEC.x = 0.0f;
    unkEC.y = 0.0f;
    unkEC.z = 0.0f;
    unkF8 = 0.0f;
    unkFC = 0.0f;
    mScore = 0;
    unk104 = 0;
    unk110 = 1;
    unk114 = 0;
    mpState = NULL;
    mpNextState = NULL;

    if (mpShadowMaterial != NULL) {
        ShadowAlpha(mpShadowMaterial) = 0;
        mpShadowMaterial->SetSyncFlags(0x10);
    }
}

void RPCowFieldObj::OnActivate() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    if (mpState != NULL) {
        mpState->Calc(this);
    }

    if (mpShadowMaterial != NULL) {
        GXColorS10* pColor = ShadowColor(mpShadowMaterial);
        f32 height = mMtx._11 * unkD4.x + mpCollision->GetPos().y;
        if (height < sFadeOutY && mpCollision->GetRot().y < 0.0f) {
            s32 alpha = pColor->a;
            if (alpha > 0) {
                alpha -= 10;
                pColor->a = alpha > 0 ? alpha : 0;
                mpShadowMaterial->SetSyncFlags(0x10);
            } else if (alpha < 0) {
                pColor->a = 0;
                mpShadowMaterial->SetSyncFlags(0x10);
            }
        }
        if (height >= sFadeInY && mpCollision->GetRot().y >= 0.0f) {
            s32 alpha = pColor->a;
            if (alpha < 100) {
                s32 next = alpha + 10;
                pColor->a = next < 100 ? next : 100;
                mpShadowMaterial->SetSyncFlags(0x10);
            } else if (alpha > 100) {
                pColor->a = 100;
                mpShadowMaterial->SetSyncFlags(0x10);
            }
        }
    }

    f32 nearDist = pGame->GetPlayerNum() <= 1 ? sLodNear1P * sLodNear1P : sLodNear2P * sLodNear2P;
    f32 midDist = pGame->GetPlayerNum() <= 1 ? sLodMid1P * sLodMid1P : sLodMid2P * sLodMid2P;
    f32 farDist = pGame->GetPlayerNum() <= 1 ? sLodFar1P * sLodFar1P : sLodFar2P * sLodFar2P;
    f32 minDist = farDist;
    s32 num = pGame->GetPlayerNum();
    for (int i = 0; i < num; i++) {
        RPCowCamera* pCamera = GetCamera(pGame, i);
        f32 dist = pCamera->CalcSquareDistance(mpCollision->GetPos());
        if (Cow_Camera_isInView(pCamera, &mpCollision->GetPos()) && dist < farDist) {
            if (dist < minDist) {
                minDist = dist;
            }
            mDrawMode[i] = 2;
        } else {
            mDrawMode[i] = 4;
        }
    }

    if (minDist < nearDist) {
        mpModel = static_cast<RPGrpModel*>(mpLodModels[0]);
        mpBatch = static_cast<RPCowModelBatch*>(mpLodModels[3]);
        mpShadowModel = static_cast<RPGrpModel*>(mpShadowLodModels[0]);
        mpShadowMaterial = static_cast<RPGrpModelMaterial*>(mpShadowLodModels[2]);
        mpShadowBatch = static_cast<RPCowModelBatch*>(mpShadowLodModels[4]);
    } else if (minDist < midDist) {
        mpModel = static_cast<RPGrpModel*>(mpLodModels[1]);
        mpBatch = static_cast<RPCowModelBatch*>(mpLodModels[4]);
        mpShadowModel = static_cast<RPGrpModel*>(mpShadowLodModels[1]);
        mpShadowMaterial = static_cast<RPGrpModelMaterial*>(mpShadowLodModels[3]);
        mpShadowBatch = static_cast<RPCowModelBatch*>(mpShadowLodModels[5]);
    } else if (minDist < farDist) {
        mpModel = static_cast<RPGrpModel*>(mpLodModels[2]);
        mpBatch = static_cast<RPCowModelBatch*>(mpLodModels[5]);
        mpShadowModel = NULL;
        mpShadowMaterial = NULL;
        mpShadowBatch = NULL;
    } else {
        mpModel = NULL;
        mpBatch = NULL;
        mpShadowModel = NULL;
        mpShadowMaterial = NULL;
        mpShadowBatch = NULL;
    }

    mFlagBits.mCalc = false;
    if (mpModel != NULL || mpBatch != NULL) {
        mMtx._03 = mpCollision->GetPos().x;
        mMtx._13 = mpCollision->GetPos().y;
        mMtx._23 = mpCollision->GetPos().z;
        if (mpModel != NULL) {
            UpdateModelFrame(mpModel);
            CalcModel(mpModel, sModelScale, &mMtx);
            mFlagBits.mCalc = true;
        }
        if (mpBatch != NULL) {
            mpBatch->AddInstance(mMtx);
        }
        mBaseMtx._03 = mMtx._01 * unkD4.y + mpCollision->GetPos().x;
        mBaseMtx._13 = 0.0f;
        mBaseMtx._23 = mMtx._21 * unkD4.y + mpCollision->GetPos().z;
        if (mpShadowModel != NULL) {
            CalcModel(mpShadowModel, sModelScale, &mBaseMtx);
            mFlagBits.mCalc = true;
        } else if (mpShadowBatch != NULL) {
            mpShadowBatch->AddInstance(mBaseMtx);
        }
    }

    if (mpSound != NULL) {
        mpSound->set3DPosition(mpCollision->GetPos());
    }
}

void RPCowFieldObj::Calculate() {
    if (mpModel != NULL) {
        EntryModel(mpModel);
    }
    if (mpShadowModel != NULL) {
        EntryModel(mpShadowModel);
    }
}

void RPCowFieldObj::SetVisibleForScreen() {
    int screen = 0;
    if (Lyt_getCurrentViewNo() == 0) {
        screen = 0;
    } else if (Lyt_getCurrentViewNo() == 1) {
        screen = 1;
    }
    if (mpModel != NULL) {
        if (mDrawMode[screen] == 4) {
            mpModel->SetVisible(false);
        } else {
            mpModel->SetVisible(true);
        }
    }
}

const f32 RPCowFieldObj::sModelScale = 1.0f;
const f32 RPCowFieldObj::sProbeRadius = 1.0f;
const f32 RPCowFieldObj::sProbeDist = 5.0f;
const f32 RPCowFieldObj::sStopY = -20.0f;
const f32 RPCowFieldObj::sSinkSpeed = 1.0f;
const f32 RPCowFieldObj::sKingJumpSpeed = 2.0f;
const f32 RPCowFieldObj::sLodNear1P = 500.0f;
const f32 RPCowFieldObj::sLodMid1P = 1000.0f;
const f32 RPCowFieldObj::sLodFar1P = 5000.0f;
const f32 RPCowFieldObj::sLodNear2P = 500.0f;
const f32 RPCowFieldObj::sLodMid2P = 1000.0f;
const f32 RPCowFieldObj::sLodFar2P = 5000.0f;
const f32 RPCowFieldObj::sGroundY = 0;
const f32 RPCowFieldObj::sFadeInY = 0;
const f32 RPCowFieldObj::sFadeOutY = 0;
