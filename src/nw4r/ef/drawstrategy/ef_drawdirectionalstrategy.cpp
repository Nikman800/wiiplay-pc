#include <nw4r/ef.h>

#include <revolution/GX.h>

namespace nw4r {
namespace ef {

static u8 directional_tex0_u8[] ALIGN(32) = {0x00, 0x01, 0x00, 0x00,
                                             0x01, 0x00, 0x01, 0x01};

DrawDirectionalStrategy::DrawDirectionalStrategy() {}

static void DrawQuad(const math::MTX34& rMtx, const math::_VEC3* pPosArray,
                     bool texCoord) {
    math::VEC3 p0, p1, p2, p3;

    math::VEC3Transform(&p0, &rMtx,
                        static_cast<const math::VEC3*>(&pPosArray[0]));
    math::VEC3Transform(&p1, &rMtx,
                        static_cast<const math::VEC3*>(&pPosArray[1]));
    math::VEC3Transform(&p2, &rMtx,
                        static_cast<const math::VEC3*>(&pPosArray[2]));
    math::VEC3Transform(&p3, &rMtx,
                        static_cast<const math::VEC3*>(&pPosArray[3]));

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    {
        GXPosition(p0);
        if (texCoord) {
            GXTexCoord1x8(0);
        }

        GXPosition(p1);
        if (texCoord) {
            GXTexCoord1x8(1);
        }

        GXPosition(p2);
        if (texCoord) {
            GXTexCoord1x8(2);
        }

        GXPosition(p3);
        if (texCoord) {
            GXTexCoord1x8(3);
        }
    }
    GXEnd();
}

void DrawQuad(const math::VEC3& rP, const math::VEC3& rD1,
              const math::VEC3& rD2, bool texCoord) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    {
        GXPosition(rP - rD1);
        if (texCoord) {
            GXTexCoord1x8(0);
        }

        GXPosition(rP - rD2);
        if (texCoord) {
            GXTexCoord1x8(1);
        }

        GXPosition(rP + rD1);
        if (texCoord) {
            GXTexCoord1x8(2);
        }

        GXPosition(rP + rD2);
        if (texCoord) {
            GXTexCoord1x8(3);
        }
    }
    GXEnd();
}

void DrawDirectionalStrategy::Draw(const DrawInfo& rInfo,
                                   ParticleManager* pManager) {
    InitGraphics(rInfo, pManager);

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    if ((rSetting.typeOption2 & 3) != 1) {
        DrawDirectional(rInfo, pManager);
    } else {
        DrawDirectionalBillboard(rInfo, pManager);
    }
}

inline math::MTX34 CalcRotate(Particle* pParticle, u8 axis) {
    math::VEC3 rot;
    pParticle->Draw_GetRotate(&rot);

    f32 sinX = 0.0f, cosX = 1.0f;
    f32 sinY = 0.0f, cosY = 1.0f;
    f32 sinZ = 0.0f, cosZ = 1.0f;

    switch (axis) {
    case 0: {
        if (rot.x != 0.0f) {
            math::SinCosRad(&sinX, &cosX, -rot.x);
        }

        return math::MTX34(1.0f, 0.0f, 0.0f, 0.0f, //
                           0.0f, cosX, -sinX, 0.0f, //
                           0.0f, sinX, cosX, 0.0f);
    }

    case 1: {
        if (rot.y != 0.0f) {
            math::SinCosRad(&sinY, &cosY, -rot.y);
        }

        return math::MTX34(cosY, 0.0f, sinY, 0.0f, //
                           0.0f, 1.0f, 0.0f, 0.0f, //
                           -sinY, 0.0f, cosY, 0.0f);
    }

    case 2: {
        if (rot.z != 0.0f) {
            math::SinCosRad(&sinZ, &cosZ, -rot.z);
        }

        return math::MTX34(cosZ, -sinZ, 0.0f, 0.0f, //
                           sinZ, cosZ, 0.0f, 0.0f,  //
                           0.0f, 0.0f, 1.0f, 0.0f);
    }

    case 3:
    default: {
        if (rot.x) {
            math::SinCosRad(&sinX, &cosX, -rot.x);
        }

        if (rot.y) {
            math::SinCosRad(&sinY, &cosY, -rot.y);
        }

        if (rot.z) {
            math::SinCosRad(&sinZ, &cosZ, -rot.z);
        }

        f32 sinXsinY = sinX * sinY;
        f32 cosXsinY = cosX * sinY;

        return math::MTX34(cosY * cosZ, sinXsinY * cosZ - cosX * sinZ,
                           cosXsinY * cosZ + sinX * sinZ, 0.0f, //
                           cosY * sinZ, sinXsinY * sinZ + cosX * cosZ,
                           cosXsinY * sinZ - sinX * cosZ, 0.0f, //
                           -sinY, cosY * sinX, cosX * cosY, 0.0f);
    }
    }
}

inline math::MTX34 CalcLocalTransform(u8 axis, f32 pivotX, f32 pivotY, f32 scaleX,
                               f32 scaleY, f32 ratio,
                               const math::MTX34& rRot) {
    switch (axis) {
    case 0: {
        f32 x = scaleX * pivotX;
        f32 sy = scaleY * ratio;
        f32 y = scaleY * (pivotY + ratio - 1.0f);

        return math::MTX34(
            rRot._00 * scaleX, rRot._01 * sy, rRot._02 * scaleX,
            pivotX + (-rRot._00 * x - rRot._01 * y), //
            rRot._10 * scaleX, rRot._11 * sy, rRot._12 * scaleX,
            pivotY + (-rRot._10 * x - rRot._11 * y), //
            rRot._20 * scaleX, rRot._21 * sy, rRot._22 * scaleX,
            -rRot._20 * x - rRot._21 * y);
    }

    case 1:
    default: {
        f32 x = scaleX * pivotX;
        f32 y = scaleY * pivotY;

        return math::MTX34(
            rRot._00 * scaleX, rRot._02 * scaleY, rRot._01 * scaleX,
            pivotX + (-rRot._00 * x - rRot._02 * y), //
            rRot._10 * scaleX, rRot._12 * scaleY, rRot._11 * scaleX,
            -rRot._10 * x - rRot._12 * y, //
            rRot._20 * scaleX, rRot._22 * scaleY, rRot._21 * scaleX,
            -rRot._20 * x - rRot._22 * y - pivotY);
    }
    }
}

void DrawDirectionalStrategy::DrawDirectional(const DrawInfo& rInfo,
                                              ParticleManager* pManager) {
    static const math::_VEC3 p[] = {
        {-1.0f, -1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f},
    };

    static const math::_VEC3 px[] = {
        {0.0f, -1.0f, 1.0f},
        {0.0f, 1.0f, 1.0f},
        {0.0f, 1.0f, -1.0f},
        {0.0f, -1.0f, -1.0f},
    };

    InitGraphics(rInfo, pManager);

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    AheadContext context(*rInfo.GetViewMtx(), pManager);
    CalcAheadFunc pCalcAheadFunc = GetCalcAheadFunc(pManager);

    math::MTX34 globalMtx;
    math::MTX34 posMtx;

    pManager->CalcGlobalMtx(&globalMtx);
    math::MTX34Mult(&posMtx, rInfo.GetViewMtx(), &globalMtx);
    GXLoadPosMtxImm(posMtx, GX_PNMTX0);

    math::MTX34 emitterMtx;
    math::MTX34 invMtx;

    pManager->mManagerEM->CalcGlobalMtx(&emitterMtx);
    math::MTX34Inv(&invMtx, &globalMtx);
    math::MTX34Mult(&emitterMtx, &invMtx, &emitterMtx);

    math::VEC3 emitterAxisY;
    math::VEC3 emitterAxisZ;

    emitterAxisY =
        math::VEC3(emitterMtx._01, emitterMtx._11, emitterMtx._21);
    emitterAxisZ =
        math::VEC3(emitterMtx._02, emitterMtx._12, emitterMtx._22);

    Normalize(&emitterAxisZ);

    for (Particle* pIt = GetYoungestParticle(pManager);
         pIt != NULL && pIt->mPrevAxis.x > 1.0f;
         pIt = GetElderParticle(pManager, pIt)) {

        pIt->mPrevAxis = emitterAxisY;
    }

    f32 pivotX = rSetting.pivotX / 100.0f;
    f32 pivotY = rSetting.pivotY / 100.0f;

    GetFirstDrawParticleFunc pGetFirstFunc = GetGetFirstDrawParticleFunc(
        rSetting.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);

    GetNextDrawParticleFunc pGetNextFunc = GetGetNextDrawParticleFunc(
        rSetting.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);

    bool first = true;

    for (Particle* pIt = pGetFirstFunc(pManager); pIt != NULL;
         pIt = pGetNextFunc(pManager, pIt)) {

        f32 sx = pIt->Draw_GetSizeX();
        if (sx < NW4R_MATH_FLT_EPSILON) {
            continue;
        }

        f32 sy = pIt->Draw_GetSizeY();
        if (sy < NW4R_MATH_FLT_EPSILON) {
            continue;
        }

        f32 ratio = 1.0f;
        if (rSetting.typeOption0) {
            math::VEC3 delta;
            pIt->GetMoveDir(&delta);
            f32 len = math::FSqrt(math::VEC3Dot(&delta, &delta));
            ratio += 0.5f * len / sy;
        }

        SetupGP(pIt, rSetting, rInfo, first, false);
        first = false;

        math::MTX34 rotMtx = CalcRotate(pIt, rSetting.typeAxis);
        math::MTX34 localMtx = CalcLocalTransform(
            rSetting.typeOption1, pivotX, pivotY, sx, sy, ratio, rotMtx);

        math::VEC3 axisX;
        math::VEC3 axisY;
        math::VEC3 axisZ;

        pCalcAheadFunc(&axisY, &context, pIt);

        math::VEC3Cross(&axisZ, &pIt->mPrevAxis, &axisY);
        if (!Normalize(&axisZ)) {
            axisZ = emitterAxisZ;
        }

        math::VEC3Cross(&axisX, &axisY, &axisZ);
        pIt->mPrevAxis = axisX;

        math::VEC3 pos = pIt->mParameter.mPosition;

        math::MTX34 baseMtx(axisX.x, axisY.x, axisZ.x, pos.x, //
                            axisX.y, axisY.y, axisZ.y, pos.y, //
                            axisX.z, axisY.z, axisZ.z, pos.z);

        math::MTX34 worldMtx;
        math::MTX34Mult(&worldMtx, &baseMtx, &localMtx);

        DrawQuad(worldMtx, p, mNumTexmap > 0);

        if (rSetting.typeOption == 1) {
            DrawQuad(worldMtx, px, mNumTexmap > 0);
        }
    }
}

void DrawDirectionalStrategy::DrawDirectionalBillboard(
    const DrawInfo& rInfo, ParticleManager* pManager) {

    InitGraphics(rInfo, pManager);

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    AheadContext context(*rInfo.GetViewMtx(), pManager);
    CalcAheadFunc pCalcAheadFunc = GetCalcAheadFunc(pManager);

    math::MTX34 viewMtx;
    math::MTX34 globalMtx;

    pManager->CalcGlobalMtx(&globalMtx);
    math::MTX34Mult(&viewMtx, rInfo.GetViewMtx(), &globalMtx);
    GXLoadPosMtxImm(mIdentityMtx, GX_PNMTX0);

    f32 lx = math::FSqrt(viewMtx._00 * viewMtx._00 +
                         viewMtx._10 * viewMtx._10 +
                         viewMtx._20 * viewMtx._20);

    f32 ly = math::FSqrt(viewMtx._01 * viewMtx._01 +
                         viewMtx._11 * viewMtx._11 +
                         viewMtx._21 * viewMtx._21);

    f32 lz = math::FSqrt(viewMtx._02 * viewMtx._02 +
                         viewMtx._12 * viewMtx._12 +
                         viewMtx._22 * viewMtx._22);

    for (Particle* pIt = GetYoungestParticle(pManager);
         pIt != NULL && pIt->mPrevAxis.x > 1.0f;
         pIt = GetElderParticle(pManager, pIt)) {

        pIt->mPrevAxis = mZeroVec;
    }

    f32 pivotX = rSetting.pivotX / 100.0f;
    f32 pivotY = rSetting.pivotY / 100.0f;

    GetFirstDrawParticleFunc pGetFirstFunc = GetGetFirstDrawParticleFunc(
        rSetting.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);

    GetNextDrawParticleFunc pGetNextFunc = GetGetNextDrawParticleFunc(
        rSetting.mFlags & EmitterDrawSetting::FLAG_DRAW_ORDER);

    bool first = true;

    for (Particle* pIt = pGetFirstFunc(pManager); pIt != NULL;
         pIt = pGetNextFunc(pManager, pIt)) {

        f32 sx = pIt->Draw_GetSizeX();
        if (sx < NW4R_MATH_FLT_EPSILON) {
            continue;
        }

        f32 sy = pIt->Draw_GetSizeY();
        if (sy < NW4R_MATH_FLT_EPSILON) {
            continue;
        }

        f32 ratio = 1.0f;
        if (rSetting.typeOption0) {
            math::VEC3 delta;
            pIt->GetMoveDir(&delta);
            f32 len = math::FSqrt(math::VEC3Dot(&delta, &delta));
            ratio += 0.5f * len / sy;
        }

        SetupGP(pIt, rSetting, rInfo, first, false);
        first = false;

        math::MTX34 rotMtx = CalcRotate(pIt, rSetting.typeAxis);
        math::MTX34 localMtx = CalcLocalTransform(
            rSetting.typeOption1, pivotX, pivotY, sx, sy, ratio, rotMtx);

        math::VEC3 axisX;
        math::VEC3 axisY;
        math::VEC3 axisZ;
        math::VEC3 ahead;

        pCalcAheadFunc(&ahead, &context, pIt);
        math::VEC3TransformNormal(&ahead, &viewMtx, &ahead);

        axisY = ahead;
        Normalize(&axisY);

        axisX = math::VEC3(ahead.y, -ahead.x, 0.0f);

        if (Normalize(&axisX)) {
            axisZ = math::VEC3(-ahead.x * ahead.z, -ahead.y * ahead.z,
                               ahead.x * ahead.x + ahead.y * ahead.y);
        } else {
            math::VEC3Cross(&axisX, &ahead, &pIt->mPrevAxis);

            if (!Normalize(&axisX)) {
                axisX = mXUnitVec;
            }

            math::VEC3Cross(&axisZ, &axisX, &ahead);
        }

        Normalize(&axisZ);
        pIt->mPrevAxis = axisZ;

        math::VEC3 pos;
        math::VEC3Transform(&pos, &viewMtx, &pIt->mParameter.mPosition);

        math::MTX34 baseMtx(lx * axisX.x, ly * axisY.x, lz * axisZ.x, pos.x,
                            lx * axisX.y, ly * axisY.y, lz * axisZ.y, pos.y,
                            lx * axisX.z, ly * axisY.z, lz * axisZ.z, pos.z);

        math::MTX34 worldMtx;
        math::MTX34Mult(&worldMtx, &baseMtx, &localMtx);

        math::VEC3 center(worldMtx._03, worldMtx._13, worldMtx._23);
        math::VEC3 d1(worldMtx._00 + worldMtx._01, worldMtx._10 + worldMtx._11,
                      worldMtx._20 + worldMtx._21);
        math::VEC3 d2(worldMtx._00 - worldMtx._01, worldMtx._10 - worldMtx._11,
                      worldMtx._20 - worldMtx._21);

        DrawQuad(center, d1, d2, mNumTexmap > 0);

        if (rSetting.typeOption == 1) {
            math::VEC3 d1(worldMtx._01 + worldMtx._02,
                          worldMtx._11 + worldMtx._12,
                          worldMtx._21 + worldMtx._22);
            math::VEC3 d2(worldMtx._01 - worldMtx._02,
                          worldMtx._11 - worldMtx._12,
                          worldMtx._21 - worldMtx._22);

            DrawQuad(center, d1, d2, mNumTexmap > 0);
        }
    }
}

void DrawDirectionalStrategy::InitGraphics(const DrawInfo& rInfo,
                                           ParticleManager* pManager) {

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    InitTexture(rSetting);
    InitTev(rSetting, rInfo);
    InitColor(pManager, rSetting, rInfo);

    GXEnableTexOffsets(GX_TEXCOORD0, TRUE, TRUE);

    GXSetArray(GX_VA_TEX0, PORT_GP_STATIC(directional_tex0_u8, sizeof(directional_tex0_u8)), 2);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);

    if (mNumTexmap > 0) {
        GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
    }

    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);

    GXSetCurrentMtx(GX_PNMTX0);
}

DrawStrategyImpl::CalcAheadFunc
DrawDirectionalStrategy::GetCalcAheadFunc(ParticleManager* pManager) {

    const EmitterDrawSetting& rSetting =
        *pManager->mResource->GetEmitterDrawSetting();

    switch (rSetting.typeDir) {
    case 0: {
        return CalcAhead_Speed;
    }

    case 1: {
        return CalcAhead_EmitterCenter;
    }

    case 2: {
        return CalcAhead_EmitterDesign;
    }

    case 3: {
        return CalcAhead_Particle;
    }

    case 5: {
        return CalcAhead_NoDesign;
    }

    case 6: {
        return CalcAhead_ParticleBoth;
    }

    default: {
        return CalcAhead_Speed;
    }
    }
}

} // namespace ef
} // namespace nw4r
