#include <nw4r/ef.h>
#include <nw4r/math.h>
#include <nw4r/ut.h>

#include <cstring>

namespace nw4r {
namespace ef {

math::MTX34 ParticleManager::smDrawMtxPMtoEM;
math::MTX34 ParticleManager::smMtxInv;

s32 ParticleManager::smMtxInvId = 0;

ParticleManager::ParticleManager()
    : mActivityList(offsetof(Particle, mActivityLink)) {}

void ParticleManager::SendClosing() {
    mManagerEM->Closing(this);
}

void ParticleManager::DestroyFunc() {}

bool ParticleManager::Closing(Particle* pParticle) {
    pParticle->mParticleManager->UnRef();

    mActivityList.ToClosing(pParticle);
    pParticle->mLifeStatus = NW4R_EF_LS_CLOSING;

    return true;
}

int ParticleManager::RetireParticle(Particle* pParticle) {
    if (pParticle->mLifeStatus != NW4R_EF_LS_ACTIVE) {
        return 0;
    }

    mActivityList.ToWait(pParticle);
    pParticle->Destroy();
    return 1;
}

int ParticleManager::RetireParticleAll() {
    int num = 0;

    Particle* pIt;
    Particle* pNext;

    for (pIt = static_cast<Particle*>(mActivityList.mActiveList.headObject);
         pIt != NULL; pIt = pNext) {

        pNext = static_cast<Particle*>(
            NW4R_UT_LIST_GET_LINK(mActivityList.mActiveList, pIt)->nextObject);

        if (pIt->mLifeStatus == NW4R_EF_LS_ACTIVE) {
            num += RetireParticle(pIt);
        }
    }

    return num;
}

bool ParticleManager::Initialize(Emitter* pParent, EmitterResource* pResource) {
    ReferencedObject::Initialize();

    mActivityList.Initialize();

    mModifier.mScale.x = 1.0f;
    mModifier.mScale.y = 1.0f;

    mModifier.mRotate.x = 0.0f;
    mModifier.mRotate.y = 0.0f;
    mModifier.mRotate.z = 0.0f;

    mManagerEM = pParent;
    pParent->Ref();

    mResource = pResource;
    mFlag = 0;
    SetMtxDirty();

    EmitterDesc* pDesc = mResource->GetEmitterDesc();

    mDrawStrategy =
        mManagerEM->mManagerEF->mManagerES->mDrawStrategyBuilder->Create(
            pDesc->drawSetting.ptcltype);

    mLastCalced = NULL;

    Modifier_SetSimpleLightParameter(pDesc->drawSetting);

    return true;
}

ParticleManager::~ParticleManager() {}

Particle*
ParticleManager::CreateParticle(u16 life, math::VEC3 pos, math::VEC3 vel,
                                const math::MTX34* pSpace, f32 momentum,
                                const EmitterInheritSetting* pSetting,
                                Particle* pReferencePtcl, u16 calcRemain) {

    Particle* pParticle =
        mManagerEM->mManagerEF->mManagerES->GetMemoryManager()->AllocParticle();

    if (pParticle == NULL) {
        return NULL;
    }

    if (!pParticle->Initialize(life, pos, vel, this, pSpace, momentum, pSetting,
                               pReferencePtcl)) {
        return NULL;
    }

    pParticle->mCalcRemain += calcRemain;
    mActivityList.ToActive(pParticle);
    pParticle->mLifeStatus = NW4R_EF_LS_ACTIVE;

    return pParticle;
}

void ParticleManager::Calc() {
    Particle* pFirst = static_cast<Particle*>(
        ut::List_GetNext(&mActivityList.mActiveList, mLastCalced));

    if (pFirst == NULL) {
        return;
    }

    Particle* pIt = pFirst;

    if (mManagerEM->mManagerEF->mCallBack.mPrevPtclCalc != NULL) {
        mManagerEM->mManagerEF->mCallBack.mPrevPtclCalc(
            this, &mActivityList.mActiveList, pFirst);
    }

    math::MTX34 mtxLocToGlb;
    CalcGlobalMtx(&mtxLocToGlb);

    math::MTX34 mtxGlbToLoc;
    math::MTX34Inv(&mtxGlbToLoc, &mtxLocToGlb);

    math::MTX34 mtxEmToGlb;
    mManagerEM->CalcGlobalMtx(&mtxEmToGlb);

    math::MTX34 mtxEmToLoc;
    math::MTX34Mult(&mtxEmToLoc, &mtxGlbToLoc, &mtxEmToGlb);

    math::MTX34 mtxLocToEm;
    math::MTX34Inv(&mtxLocToEm, &mtxEmToLoc);

    math::MTX34 mtxGlbToLocNoTrans = mtxGlbToLoc;
    mtxGlbToLocNoTrans._03 = 0.0f;
    mtxGlbToLocNoTrans._13 = 0.0f;
    mtxGlbToLocNoTrans._23 = 0.0f;

    math::MTX34 mtxLocToEmNoTrans = mtxLocToEm;
    mtxLocToEmNoTrans._03 = 0.0f;
    mtxLocToEmNoTrans._13 = 0.0f;
    mtxLocToEmNoTrans._23 = 0.0f;

    math::MTX34 mtxEmToLocNoTrans = mtxEmToLoc;
    mtxEmToLocNoTrans._03 = 0.0f;
    mtxEmToLocNoTrans._13 = 0.0f;
    mtxEmToLocNoTrans._23 = 0.0f;

    Particle* pNext;

    for (; pIt != NULL; pIt = pNext) {
        pNext = static_cast<Particle*>(
            NW4R_UT_LIST_GET_LINK(mActivityList.mActiveList, pIt)->nextObject);

        if (pIt->mLifeStatus != NW4R_EF_LS_ACTIVE) {
            continue;
        }

        if (pIt->mEvalStatus != NW4R_EF_ES_WAIT) {
            continue;
        }

        pIt->mEvalStatus = NW4R_EF_ES_DONE;

        if (pIt->mCalcRemain != 0) {
            mManagerEM->mManagerEF->SetFlagExistCalcRemain(true);
        }

        math::VEC3 prevPos = pIt->mParameter.mPosition;
        math::VEC3 prevVel = pIt->mParameter.mVelocity;

        math::VEC3 prevDir;
        pIt->GetMoveDir(&prevDir);

        pIt->mParameter.mPrevPosition = pIt->mParameter.mPosition;

        if (pIt->mLife <= pIt->mTick) {
            RetireParticle(pIt);
            continue;
        }

        math::VEC3 addVel(0.0f, 0.0f, 0.0f);
        math::VEC3 addPos(0.0f, 0.0f, 0.0f);
        f32 vortexSpeed;

        for (u16 i = pIt->mTick == 0 ? 0 : mResource->NumPtclInitTrack();
             i < mResource->NumPtclTrack(); i++) {

            u8* pPtclTrack = mResource->GetPtclTrack(i);

#define pTrackAsHeader reinterpret_cast<AnimCurveHeader*>(pPtclTrack)

            if (pTrackAsHeader->processFlag & AnimCurveHeader::PROC_FLAG_STOP) {
                continue;
            }

            if (pIt->mTick != 0 && pTrackAsHeader->frameLength <= 1 &&
                pTrackAsHeader->kindType < AC_TARGET_TEXTUREINDTRANSLATE + 1) {
                continue;
            }

            u32 tick;
            u32 life;
            u16 seed;

            if (pTrackAsHeader->processFlag &
                AnimCurveHeader::PROC_FLAG_TIMING) {

                tick = pIt->mParticleManager->mManagerEM->mTick;

                if (pIt->mParticleManager->mManagerEM->mParameter.mComFlags &
                    EmitterDesc::CMN_FLAG_MAX_LIFE) {
                    life = 0xFFFFFFFF;
                } else {
                    life =
                        pIt->mParticleManager->mManagerEM->mParameter.mEmitSpan;
                }

                seed = pIt->mParticleManager->mManagerEM->mRandSeed;
            } else {
                tick = pIt->mTick;
                life = pIt->mLife;
                seed = pIt->mRandSeed;
            }

            if (pTrackAsHeader->magic != NW4R_EF_MAGIC_ANIMCURVE) {
                continue;
            }

            // Load curveFlag and kindEnable
            u16 ctrl = *reinterpret_cast<be_u16*>(&pTrackAsHeader->curveFlag);
            u8 kind = pTrackAsHeader->kindType;

            switch (ctrl >> 8) {
            case AC_TYPE_PARTICLE_U8: {
                u8* pTarget;

                switch (kind) {
                case AC_TARGET_ALPHA0PRI: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_0][COLOR_IDX_PRI].a;
                    break;
                }
                case AC_TARGET_ALPHA0SEC: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_0][COLOR_IDX_SEC].a;
                    break;
                }
                case AC_TARGET_ALPHA1PRI: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_1][COLOR_IDX_PRI].a;
                    break;
                }
                case AC_TARGET_ALPHA1SEC: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_1][COLOR_IDX_SEC].a;
                    break;
                }

                case AC_TARGET_ACMPREF0: {
                    pTarget = &pIt->mParameter.mACmpRef0;
                    break;
                }
                case AC_TARGET_ACMPREF1: {
                    pTarget = &pIt->mParameter.mACmpRef1;
                    break;
                }

                case AC_TARGET_COLOR0PRI: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_0][COLOR_IDX_PRI].r;
                    break;
                }
                case AC_TARGET_COLOR0SEC: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_0][COLOR_IDX_SEC].r;
                    break;
                }
                case AC_TARGET_COLOR1PRI: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_1][COLOR_IDX_PRI].r;
                    break;
                }
                case AC_TARGET_COLOR1SEC: {
                    pTarget =
                        &pIt->mParameter.mColor[COLOR_LAYER_1][COLOR_IDX_SEC].r;
                    break;
                }

                default: {
                    continue;
                }
                }

                AnimCurveExecuteU8(pPtclTrack, pTarget, tick, seed, life);
                break;
            }

            case AC_TYPE_PARTICLE_F32: {
                switch (kind) {
                case AC_TARGET_SIZE: {
                    AnimCurveExecuteF32(
                        pPtclTrack,
                        reinterpret_cast<f32*>(&pIt->mParameter.mSize), tick,
                        seed, life);
                    break;
                }

                case AC_TARGET_SCALE: {
                    AnimCurveExecuteF32(
                        pPtclTrack,
                        reinterpret_cast<f32*>(&pIt->mParameter.mScale), tick,
                        seed, life);
                    break;
                }

                case AC_TARGET_TEXTURE1SCALE: {
                    AnimCurveExecuteF32(pPtclTrack,
                                        reinterpret_cast<f32*>(
                                            &pIt->mParameter
                                                 .mTextureScale[TEX_LAYER_1]),
                                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTURE2SCALE: {
                    AnimCurveExecuteF32(pPtclTrack,
                                        reinterpret_cast<f32*>(
                                            &pIt->mParameter
                                                 .mTextureScale[TEX_LAYER_2]),
                                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTUREINDSCALE: {
                    AnimCurveExecuteF32(pPtclTrack,
                                        reinterpret_cast<f32*>(
                                            &pIt->mParameter
                                                 .mTextureScale[TEX_LAYER_IND]),
                                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTURE1TRANSLATE: {
                    AnimCurveExecuteF32(
                        pPtclTrack,
                        reinterpret_cast<f32*>(
                            &pIt->mParameter.mTextureTranslate[TEX_LAYER_1]),
                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTURE2TRANSLATE: {
                    AnimCurveExecuteF32(
                        pPtclTrack,
                        reinterpret_cast<f32*>(
                            &pIt->mParameter.mTextureTranslate[TEX_LAYER_2]),
                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTUREINDTRANSLATE: {
                    AnimCurveExecuteF32(
                        pPtclTrack,
                        reinterpret_cast<f32*>(
                            &pIt->mParameter.mTextureTranslate[TEX_LAYER_IND]),
                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTURE1ROTATE: {
                    AnimCurveExecuteF32(
                        pPtclTrack, &pIt->mParameter.mTextureRotate[TEX_LAYER_1],
                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTURE2ROTATE: {
                    AnimCurveExecuteF32(
                        pPtclTrack, &pIt->mParameter.mTextureRotate[TEX_LAYER_2],
                        tick, seed, life);
                    break;
                }

                case AC_TARGET_TEXTUREINDROTATE: {
                    AnimCurveExecuteF32(
                        pPtclTrack,
                        &pIt->mParameter.mTextureRotate[TEX_LAYER_IND], tick,
                        seed, life);
                    break;
                }

                case AC_TARGET_FIELD_SPEED: {
                    f32 speed;
                    AnimCurveExecuteF32(pPtclTrack, &speed, tick, seed, life);
                    math::VEC3 v;
                    math::VEC3Scale(&v, &prevVel, speed - 1.0f);
                    addVel += v;
                    break;
                }

                case AC_TARGET_UNK32: {
                    FieldGravityParam* pParam = reinterpret_cast<FieldGravityParam*>(
                        pPtclTrack + sizeof(AnimCurveHeader) +
                        pTrackAsHeader->keyTable + pTrackAsHeader->rangeTable +
                        pTrackAsHeader->randomTable + pTrackAsHeader->nameTable);

                    f32 power = pParam->power;
                    AnimCurveExecuteF32(pPtclTrack, &power, tick, seed, life);

                    math::VEC3 v;
                    v.x = pParam->dir.x * power;
                    v.y = pParam->dir.y * power;
                    v.z = pParam->dir.z * power;

                    if (pParam->space) {
                        math::VEC3Transform(&v, &mtxEmToLocNoTrans, &v);
                    } else {
                        math::VEC3Transform(&v, &mtxGlbToLocNoTrans, &v);
                    }

                    switch (pParam->type) {
                    case 0: {
                        addVel += v;
                        break;
                    }
                    case 1: {
                        addPos += v;
                        break;
                    }
                    }
                    break;
                }

                case AC_TARGET_UNK39: {
                    FieldRandomParam* pParam = reinterpret_cast<FieldRandomParam*>(
                        pPtclTrack + sizeof(AnimCurveHeader) +
                        pTrackAsHeader->keyTable + pTrackAsHeader->rangeTable +
                        pTrackAsHeader->randomTable + pTrackAsHeader->nameTable);

                    f32 power;

                    union {
                        u32 value;
                        u8 bytes[4];
                    } rndSeed;

                    rndSeed.value = seed * 0x3F81F635 +
                                    pTrackAsHeader->randomSeed * 0x30A74193 +
                                    static_cast<u16>(tick) * 0x371097E7 +
                                    0x4BF53;

                    rndSeed.bytes[BE_BYTE(2, 4)] ^= rndSeed.bytes[BE_BYTE(3, 4)];
                    rndSeed.bytes[BE_BYTE(1, 4)] ^= rndSeed.bytes[BE_BYTE(2, 4)];
                    rndSeed.bytes[BE_BYTE(0, 4)] ^= rndSeed.bytes[BE_BYTE(1, 4)];

                    if (!(pTrackAsHeader->processFlag &
                          AnimCurveHeader::PROC_FLAG_TIMING)) {
                        if (tick == 0) {
                            break;
                        }

                        if (tick % (pParam->interval + 1) != 0) {
                            break;
                        }
                    } else {
                        if (pIt->mTick != 0 &&
                            tick % (pParam->interval + 1) != 0) {
                            break;
                        }
                    }

                    power = pParam->power;
                    AnimCurveExecuteF32(pPtclTrack, &power, tick, seed, life);

                    math::VEC3 v;

                    if (pParam->flags & 2) {
                        u32 rnd0 = rndSeed.value;
                        v.x = static_cast<s16>(rnd0 >> 16) / 32768.0f * power;
                        u32 rnd1 = rnd0 * 0x343FD + 0x269EC3;
                        v.y = static_cast<s16>(rnd1 >> 16) / 32768.0f * power;
                        u32 rnd2 = rnd1 * 0x343FD + 0x269EC3;
                        v.z = static_cast<s16>(rnd2 >> 16) / 32768.0f * power;
                    } else {
                        math::VEC3 dir;

                        if (pIt->mTick == 0) {
                            dir = prevVel;
                        } else {
                            dir = prevDir;
                        }

                        math::VEC3Transform(&dir, &mtxLocToEmNoTrans, &dir);

                        if (math::VEC3LenSq(&dir) < NW4R_MATH_FLT_MIN) {
                            dir.y = 1.0f;
                        } else {
                            math::VEC3Normalize(&dir, &dir);
                        }

                        math::MTX34 mtxDir;
                        GetDirMtxY(&mtxDir, dir);

                        if (pParam->diffuse != 0.0f) {
                            u32 rnd0 = rndSeed.value;
                            f32 theta =
                                (rnd0 >> 16) / 65535.0f * pParam->diffuse;
                            u32 rnd1 = rnd0 * 0x343FD + 0x269EC3;
                            f32 phi =
                                2.0f * (NW4R_MATH_PI * ((rnd1 >> 16) / 65535.0f));

                            v.x = sinf(theta) * sinf(phi);
                            v.y = cosf(theta);
                            v.z = sinf(theta) * cosf(phi);

                            u32 rnd2 = rnd1 * 0x343FD + 0x269EC3;
                            math::VEC3Scale(&v, &v, (rnd2 >> 16) / 65535.0f * power);
                            math::VEC3Transform(&v, &mtxDir, &v);
                        } else {
                            v.x = 0.0f;
                            v.y = static_cast<s16>(rndSeed.value >> 16) / 32768.0f * power;
                            v.z = 0.0f;
                            math::VEC3Transform(&v, &mtxDir, &v);
                        }
                    }

                    math::VEC3Transform(&v, &mtxEmToLocNoTrans, &v);

                    switch (pParam->type) {
                    case 0: {
                        addVel += v;
                        break;
                    }
                    case 1: {
                        addPos += v;
                        break;
                    }
                    }
                    break;
                }

                case AC_TARGET_UNK38: {
                    NW4R_EF_FIELD_VALUE(FieldSpinParam) param = *reinterpret_cast<FieldSpinParam*>(
                        pPtclTrack + sizeof(AnimCurveHeader) +
                        pTrackAsHeader->keyTable + pTrackAsHeader->rangeTable +
                        pTrackAsHeader->randomTable + pTrackAsHeader->nameTable);

                    AnimCurveExecuteF32(pPtclTrack, &param.speed, tick, seed,
                                        life);

                    math::VEC3 axis;
                    Rotation2VecY(param.rot, &axis);

                    math::MTX34 mtxRot;
                    math::MTX34RotAxisRad(&mtxRot, &axis, param.speed);

                    math::VEC3 pos;
                    if (param.space) {
                        math::VEC3Transform(&pos, &mtxLocToEm, &prevPos);
                    } else {
                        math::VEC3Transform(&pos, &mtxLocToGlb, &prevPos);
                        math::VEC3 origin(0.0f, 0.0f, 0.0f);
                        math::VEC3Transform(&origin, &mtxEmToGlb, &origin);
                        math::VEC3Sub(&pos, &pos, &origin);
                    }

                    math::VEC3 v;
                    math::VEC3Transform(&v, &mtxRot, &pos);
                    math::VEC3Sub(&v, &v, &pos);

                    if (param.space) {
                        math::VEC3Transform(&v, &mtxEmToLocNoTrans, &v);
                    } else {
                        math::VEC3Transform(&v, &mtxGlbToLocNoTrans, &v);
                    }

                    switch (param.type) {
                    case 0: {
                        addVel += v;
                        break;
                    }
                    case 1: {
                        addPos += v;
                        break;
                    }
                    }
                    break;
                }

                case AC_TARGET_UNK34: {
                    NW4R_EF_FIELD_VALUE(FieldMagnetParam) param = *reinterpret_cast<FieldMagnetParam*>(
                        pPtclTrack + sizeof(AnimCurveHeader) +
                        pTrackAsHeader->keyTable + pTrackAsHeader->rangeTable +
                        pTrackAsHeader->randomTable + pTrackAsHeader->nameTable);

                    AnimCurveExecuteF32(pPtclTrack, &param.power, tick, seed,
                                        life);

                    math::VEC3 pos;
                    if (param.space) {
                        math::VEC3Transform(&pos, &mtxLocToEm, &prevPos);
                    } else {
                        math::VEC3Transform(&pos, &mtxLocToGlb, &prevPos);
                        math::VEC3 origin(0.0f, 0.0f, 0.0f);
                        math::VEC3Transform(&origin, &mtxEmToGlb, &origin);
                        math::VEC3Sub(&pos, &pos, &origin);
                    }

                    math::VEC3 v;
                    math::VEC3Sub(&v, &param.pos, &pos);
                    if (v.x != 0.0f || v.y != 0.0f || v.z != 0.0f) {
                        math::VEC3Normalize(&v, &v);
                    }

                    math::VEC3Scale(&v, &v, param.power);

                    if (param.space) {
                        math::VEC3Transform(&v, &mtxEmToLocNoTrans, &v);
                    } else {
                        math::VEC3Transform(&v, &mtxGlbToLocNoTrans, &v);
                    }

                    switch (param.type) {
                    case 0: {
                        addVel += v;
                        break;
                    }
                    case 1: {
                        addPos += v;
                        break;
                    }
                    }
                    break;
                }

                case AC_TARGET_UNK35: {
                    NW4R_EF_FIELD_VALUE(FieldNewtonParam) param = *reinterpret_cast<FieldNewtonParam*>(
                        pPtclTrack + sizeof(AnimCurveHeader) +
                        pTrackAsHeader->keyTable + pTrackAsHeader->rangeTable +
                        pTrackAsHeader->randomTable + pTrackAsHeader->nameTable);

                    AnimCurveExecuteF32(pPtclTrack, &param.power, tick, seed,
                                        life);

                    math::VEC3 pos;
                    if (param.space) {
                        math::VEC3Transform(&pos, &mtxLocToEm, &prevPos);
                    } else {
                        math::VEC3Transform(&pos, &mtxLocToGlb, &prevPos);
                        math::VEC3 origin(0.0f, 0.0f, 0.0f);
                        math::VEC3Transform(&origin, &mtxEmToGlb, &origin);
                        math::VEC3Sub(&pos, &pos, &origin);
                    }

                    math::VEC3 v;
                    math::VEC3Sub(&v, &param.pos, &pos);
                    f32 lenSq = math::VEC3LenSq(&v);

                    if (v.x != 0.0f || v.y != 0.0f || v.z != 0.0f) {
                        math::VEC3Normalize(&v, &v);
                    }

                    math::VEC3Scale(&v, &v, param.power);

                    f32 rangeSq = param.range * param.range;
                    if (lenSq > rangeSq) {
                        math::VEC3Scale(&v, &v, rangeSq / lenSq);
                    }

                    if (param.space) {
                        math::VEC3Transform(&v, &mtxEmToLocNoTrans, &v);
                    } else {
                        math::VEC3Transform(&v, &mtxGlbToLocNoTrans, &v);
                    }

                    switch (param.type) {
                    case 0: {
                        addVel += v;
                        break;
                    }
                    case 1: {
                        addPos += v;
                        break;
                    }
                    }
                    break;
                }

                case AC_TARGET_UNK36: {
                    NW4R_EF_FIELD_VALUE(FieldVortexParam) param = *reinterpret_cast<FieldVortexParam*>(
                        pPtclTrack + sizeof(AnimCurveHeader) +
                        pTrackAsHeader->keyTable + pTrackAsHeader->rangeTable +
                        pTrackAsHeader->randomTable + pTrackAsHeader->nameTable);

                    AnimCurveExecuteF32(pPtclTrack, &param.innerSpeed, tick,
                                        seed, life);

                    math::VEC3 axis;
                    Rotation2VecY(param.rot, &axis);

                    math::VEC3 pos;
                    if (param.space) {
                        math::VEC3Transform(&pos, &mtxLocToEm, &prevPos);
                    } else {
                        math::VEC3Transform(&pos, &mtxLocToGlb, &prevPos);
                        math::VEC3 origin(0.0f, 0.0f, 0.0f);
                        math::VEC3Transform(&origin, &mtxEmToGlb, &origin);
                        math::VEC3Sub(&pos, &pos, &origin);
                    }

                    param.radius *= param.radius;

                    math::VEC3 dist;
                    math::VEC3Scale(&dist, &axis, math::VEC3Dot(&axis, &pos));
                    math::VEC3Sub(&dist, &pos, &dist);
                    f32 distSq = math::VEC3LenSq(&dist);

                    if (distSq == 0.0f) {
                        break;
                    }

                    if (distSq >= param.radius) {
                        vortexSpeed = param.outerSpeed;
                    } else {
                        f32 ratio = distSq / param.radius;
                        vortexSpeed = (1.0f - ratio) * param.innerSpeed +
                                ratio * param.outerSpeed;
                    }

                    math::VEC3Normalize(&dist, &dist);

                    math::VEC3 v;
                    math::VEC3Cross(&v, &dist, &axis);
                    math::VEC3Scale(&v, &v, vortexSpeed);

                    if (param.space) {
                        math::VEC3Transform(&v, &mtxEmToLocNoTrans, &v);
                    } else {
                        math::VEC3Transform(&v, &mtxGlbToLocNoTrans, &v);
                    }

                    switch (param.type) {
                    case 0: {
                        addVel += v;
                        break;
                    }
                    case 1: {
                        addPos += v;
                        break;
                    }
                    }
                    break;
                }
                }
                break;
            }

            case AC_TYPE_PARTICLE_ROTATE: {
                switch (kind) {
                case AC_TARGET_ROTATE: {
                    AnimCurveExecuteRotate(
                        pPtclTrack,
                        reinterpret_cast<f32*>(&pIt->mParameter.mRotate), tick,
                        seed, life);
                    break;
                }
                }
                break;
            }

            case AC_TYPE_PARTICLE_TEXTURE: {
                AnimCurveExecuteTexture(pPtclTrack, pIt, tick, seed, life);
                break;
            }

            case AC_TYPE_CHILD: {
                AnimCurveExecuteChild(pPtclTrack, pIt, tick, seed, life);
                break;
            }
            }

#undef pTrackAsHeader
        }

        pIt->mTick++;

        pIt->mParameter.mVelocity += addVel;

        pIt->AddPosition(&addPos);
        pIt->AddPosition(&pIt->mParameter.mVelocity);
    }

    mLastCalced =
        static_cast<Particle*>(ut::List_GetLast(&mActivityList.mActiveList));

    if (mManagerEM->mManagerEF->mCallBack.mPostPtclCalc != NULL) {
        mManagerEM->mManagerEF->mCallBack.mPostPtclCalc(
            this, &mActivityList.mActiveList, pFirst);
    }
}

void ParticleManager::Draw(const DrawInfo& rInfo) {
    const EmitterDesc* pDesc = mResource->GetEmitterDesc();

    if ((pDesc->drawSetting.mFlags & EmitterDrawSetting::FLAG_HIDDEN) ||
        (mManagerEM->mParameter.mComFlags &
         EmitterDesc::CMN_FLAG_DISABLE_DRAW)) {
        return;
    }

    mDrawStrategy->Draw(rInfo, this);
}

math::MTX34* ParticleManager::CalcGlobalMtx(math::MTX34* pResult) {
    if (mMtxDirty) {
        math::MTX34 orig;
        mManagerEM->CalcGlobalMtx(&orig);

        mManagerEM->RestructMatrix(&mMtx, &orig, mFlag & FLAG_MTX_INHERIT_SCALE,
                                   mFlag & FLAG_MTX_INHERIT_ROT,
                                   mInheritTranslate);

        mMtxDirty = false;
    }

    *pResult = mMtx;
    return pResult;
}

void ParticleManager::BeginCalc(bool onlyIfRemain) {
    mLastCalced = NULL;

    Particle* pIt =
        static_cast<Particle*>(mActivityList.mActiveList.headObject);

    // clang-format off
    for (; pIt != NULL; pIt = static_cast<Particle*>(
            NW4R_UT_LIST_GET_LINK(mActivityList.mActiveList, pIt)->nextObject))
    // clang-format on
    {
        if (!onlyIfRemain || pIt->mCalcRemain != 0) {
            if (pIt->mCalcRemain != 0) {
                pIt->mCalcRemain--;
            }

            if (pIt->GetLifeStatus() == NW4R_EF_LS_ACTIVE &&
                pIt->mEvalStatus == NW4R_EF_ES_DONE) {

                pIt->mEvalStatus = NW4R_EF_ES_WAIT;
            }
        }
    }
}

void ParticleManager::EndCalc() {
    Particle* pIt =
        static_cast<Particle*>(mActivityList.mActiveList.headObject);

    // clang-format off
    for (; pIt != NULL; pIt = static_cast<Particle*>(
            NW4R_UT_LIST_GET_LINK(mActivityList.mActiveList, pIt)->nextObject))
    // clang-format on
    {
        if (pIt->GetLifeStatus() == NW4R_EF_LS_ACTIVE &&
            pIt->mEvalStatus == NW4R_EF_ES_SKIP) {

            pIt->mEvalStatus = NW4R_EF_ES_DONE;
        }
    }
}

void ParticleManager::BeginDraw() {
    math::MTX34 emMtx;
    math::MTX34 pmMtx;

    mManagerEM->CalcGlobalMtx(&emMtx);
    CalcGlobalMtx(&pmMtx);

    math::MTX34Inv(&emMtx, &emMtx);
    math::MTX34Mult(&smDrawMtxPMtoEM, &emMtx, &pmMtx);
}

const math::MTX34* ParticleManager::Draw_GetMtxPMtoEM() const {
    return &smDrawMtxPMtoEM;
}

void ParticleManager::EndDraw() {}

void ParticleManager::Draw_ModifyColor(Particle* pParticle, GXColor* pColorPri,
                                       GXColor* pColorSec) {
    switch (mModifier.mLight.mType) {
    case ParticleModifier::SIMPLELIGHT_AMBIENT: {
        pColorPri->r = (pColorPri->r * mModifier.mLight.mAmbient.r + 128) >> 8;
        pColorPri->g = (pColorPri->g * mModifier.mLight.mAmbient.g + 128) >> 8;
        pColorPri->b = (pColorPri->b * mModifier.mLight.mAmbient.b + 128) >> 8;
        pColorPri->a = (pColorPri->a * mModifier.mLight.mAmbient.a + 128) >> 8;

        pColorSec->r = (pColorSec->r * mModifier.mLight.mAmbient.r + 128) >> 8;
        pColorSec->g = (pColorSec->g * mModifier.mLight.mAmbient.g + 128) >> 8;
        pColorSec->b = (pColorSec->b * mModifier.mLight.mAmbient.b + 128) >> 8;
        pColorSec->a = (pColorSec->a * mModifier.mLight.mAmbient.a + 128) >> 8;
        break;
    }

    case ParticleModifier::SIMPLELIGHT_DIFFUSE: {
        if (mModifier.mLight.mRadius < NW4R_MATH_FLT_EPSILON) {
            // clang-format off
            pColorPri->r = (pColorPri->r * mModifier.mLight.mAmbient.r + 128) >> 8;
            pColorPri->g = (pColorPri->g * mModifier.mLight.mAmbient.g + 128) >> 8;
            pColorPri->b = (pColorPri->b * mModifier.mLight.mAmbient.b + 128) >> 8;
            pColorPri->a = (pColorPri->a * mModifier.mLight.mAmbient.a + 128) >> 8;

            pColorSec->r = (pColorSec->r * mModifier.mLight.mAmbient.r + 128) >> 8;
            pColorSec->g = (pColorSec->g * mModifier.mLight.mAmbient.g + 128) >> 8;
            pColorSec->b = (pColorSec->b * mModifier.mLight.mAmbient.b + 128) >> 8;
            pColorSec->a = (pColorSec->a * mModifier.mLight.mAmbient.a + 128) >> 8;
            // clang-format on
        } else {
            const math::MTX34* pMtxPMtoEM = Draw_GetMtxPMtoEM();

            math::VEC3 pos;
            math::VEC3Transform(&pos, pMtxPMtoEM,
                                &pParticle->mParameter.mPosition);

            math::VEC3Sub(&pos, &pos, &mModifier.mLight.mPosition);
            f32 dist = math::VEC3Len(&pos);

            if (dist > mModifier.mLight.mRadius) {
                // clang-format off
                pColorPri->r = (pColorPri->r * mModifier.mLight.mAmbient.r + 128) >> 8;
                pColorPri->g = (pColorPri->g * mModifier.mLight.mAmbient.g + 128) >> 8;
                pColorPri->b = (pColorPri->b * mModifier.mLight.mAmbient.b + 128) >> 8;
                pColorPri->a = (pColorPri->a * mModifier.mLight.mAmbient.a + 128) >> 8;

                pColorSec->r = (pColorSec->r * mModifier.mLight.mAmbient.r + 128) >> 8;
                pColorSec->g = (pColorSec->g * mModifier.mLight.mAmbient.g + 128) >> 8;
                pColorSec->b = (pColorSec->b * mModifier.mLight.mAmbient.b + 128) >> 8;
                pColorSec->a = (pColorSec->a * mModifier.mLight.mAmbient.a + 128) >> 8;
                // clang-format on
            } else {
                s32 attn = (256 * dist) / mModifier.mLight.mRadius;

                u16 lr = mModifier.mLight.mDiffuse.r * 256 +
                         attn * (mModifier.mLight.mAmbient.r -
                                 mModifier.mLight.mDiffuse.r);

                u16 lg = mModifier.mLight.mDiffuse.g * 256 +
                         attn * (mModifier.mLight.mAmbient.g -
                                 mModifier.mLight.mDiffuse.g);

                u16 lb = mModifier.mLight.mDiffuse.b * 256 +
                         attn * (mModifier.mLight.mAmbient.b -
                                 mModifier.mLight.mDiffuse.b);

                u16 la = mModifier.mLight.mDiffuse.a * 256 +
                         attn * (mModifier.mLight.mAmbient.a -
                                 mModifier.mLight.mDiffuse.a);

                pColorPri->r = (pColorPri->r * lr + 128) >> 16;
                pColorPri->g = (pColorPri->g * lg + 128) >> 16;
                pColorPri->b = (pColorPri->b * lb + 128) >> 16;
                pColorPri->a = (pColorPri->a * la + 128) >> 16;

                pColorSec->r = (pColorSec->r * lr + 128) >> 16;
                pColorSec->g = (pColorSec->g * lg + 128) >> 16;
                pColorSec->b = (pColorSec->b * lb + 128) >> 16;
                pColorSec->a = (pColorSec->a * la + 128) >> 16;
            }
        }
        break;
    }

    default: {
        break;
    }
    }
}

} // namespace ef
} // namespace nw4r
