#include <Pack/RPParty/RPBilScene/RPBilBall.h>

#include <Pack/RPGraphics.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPUtility.h>
#include <Pack/RPParty/RPBilScene/RPBilBallMgr.h>
#include <Pack/RPParty/RPBilScene/RPBilPocketCamWindow.h>
#include <Pack/RPParty/RPBilScene/RPBilSound.h>
#include <Pack/RPParty/RPBilScene/RPBilTable.h>
#include <Pack/RPParty/RPPartyCommon/RPPhysSphere.h>

f32 RPBilBall::sMass = 100.0f;
f32 RPBilBall::sParam8 = 0.01f;
f32 RPBilBall::sRadius = 2.85f;
f32 RPBilBall::sParam10 = 0.00005f;
f32 RPBilBall::sRestSpeed = 0.0004f;
f32 RPBilBall::sParam18 = 0.0001f;

namespace {

// The phys sphere's parameters, with the defaults its constructor (inline) sets first
struct BallSphereParam : public RPPhysSphereParam {
    BallSphereParam() {
        unk10 = 1.1920929e-7f;
        restSpeed = 1.1920929e-7f;
        unk18 = 0.0f;
        unk1C = 0.0f;
    }
};

struct RackPos {
    f32 x, y, z;
};

inline f32 RandomScaled(f32 range) {
    return range * RPUtlRandom::getF32();
}

} // namespace

RPBilBall::~RPBilBall() {}

RPBilBall::RPBilBall(s32 index) {
    mIndex = index;
    mpStateMachine = new RPBilBallSeq(this, STATE_NUM);

    RPBilStateFuncs<RPBilBall> funcs[STATE_NUM] = {
        {NULL, NULL},
        {&RPBilBall::OnTableEnter, &RPBilBall::OnTableCalc},
        {&RPBilBall::PocketedEnter, &RPBilBall::PocketedCalc},
        {&RPBilBall::OutEnter, &RPBilBall::OutCalc},
    };
    for (s32 i = 0; i < STATE_NUM; i++) {
        mpStateMachine->SetStateFuncs(funcs[i].mEnter, funcs[i].mCalc, i);
    }

    BallSphereParam param;
    param.unk0 = mIndex;
    param.mass = RPBilBall::sMass;
    param.unk8 = RPBilBall::sParam8;
    param.radius = RPBilBall::sRadius;
    param.unk10 = RPBilBall::sParam10;
    param.restSpeed = RPBilBall::sRestSpeed;
    param.unk18 = RPBilBall::sParam18;
    param.unk1C = 0.0f;
    mpPhysSphere = new RPPhysSphere(param);

    RPGrpRenderer::GetCurrent()->AppendDrawObject(&static_cast<IRPGrpDrawObject&>(*this));
    mpSound = new RPBilSound3D();

    if (mIndex == 0) {
        const char* effectNames[] = {"Bil_ball_tail02"};
        for (int i = 0; i < 1; i++) {
            mpTailEffect = new RPSysEffect(effectNames[i], 1, 0);
        }
    }
}

void RPBilBall::Load() {
    if (mIndex == 0) {
        mpModel = RPGrpModel::Construct(4, "bad_ball_0_face_2", 4, 0, 0);
        mpModel->SetDrawScene(1);
        mpModel->CreateBoundingInfo(1);
    } else {
        mpModel = RPGrpModel::Construct(2, "bad_ball", 4, 0, 3);
        mpModel->SetDrawScene(1);
        mpModel->CreateBoundingInfo(1);

        mpModel->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_TexPat, 1);
        mpModel->GetModelAnm()->RegistFromResFile(RPGrpModelAnm::Anm_TexPat, 0, 2, "bad_ball", 0);
        mpModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_TexPat, 0, (f32)mIndex);
        mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_TexPat, 0, 0.0f);
    }

    mpModel->GetBoundingInfo()->setUnk4(1.1f);
}

void RPBilBall::Reset() {
    mpStateMachine->Reset(STATE_NONE);
    mPocketedShot = -1;
    mOutShot = -1;
    unk70 = true;
    unk71 = true;
    mHitFlags = 0;
}

void RPBilBall::Exit() {}

void RPBilBall::PreCalc() {}

void RPBilBall::Calculate() {
    mpStateMachine->Calculate();
}

void RPBilBall::UpdateModel() {
    if (unk70 && mpStateMachine->GetState() == STATE_ON_TABLE) {
        static_cast<nw4r::math::VEC3&>(mAxisX) = mpPhysSphere->GetAxisX();
        static_cast<nw4r::math::VEC3&>(mAxisY) = mpPhysSphere->GetAxisY();
        static_cast<nw4r::math::VEC3&>(mAxisZ) = mpPhysSphere->GetAxisZ();
        mPos = mpPhysSphere->GetPos();

        if (mPos.y > RPBilTable::GetHeight()) {
            RPBilTable::sInstance->GetShadowTex()->AddModel(mpModel, 1.0f);
        }
        CalcModelMtx();

        if (mIndex == 0) {
            EGG::Vector3f dir = GetVec1C();
            if (dir.normalise() > sRestSpeed) {
                EGG::Vector3f up(0.0f, 1.0f, 0.0f);
                EGG::Vector3f side = up.cross(dir);
                if (side.normalise() < 1.1920929e-7f) {
                    side = EGG::Vector3f(1.0f, 0.0f, 0.0f);
                    up = dir.cross(side);
                    up.normalise();
                    side = up.cross(dir);
                } else {
                    up = dir.cross(side);
                }

                EGG::Vector3f pos = GetPos();
                pos.y -= mpPhysSphere->GetRadius();
                nw4r::math::MTX34 mtx;
                mtx._03 = pos.x;
                mtx._13 = pos.y;
                mtx._23 = pos.z;
                mtx._00 = side.x;
                mtx._10 = side.y;
                mtx._20 = side.z;
                mtx._01 = up.x;
                mtx._11 = up.y;
                mtx._21 = up.z;
                mtx._02 = dir.x;
                mtx._12 = dir.y;
                mtx._22 = dir.z;
                mpTailEffect->create();
                mpTailEffect->setMtx(mtx);
            } else {
                mpTailEffect->fade();
            }
        }

        if (mIndex == 0) {
            mpTailEffect->update();
        }

        mpSound->set3DPosition(mPos);
    }
}

void RPBilBall::OnTableEnter() {}

void RPBilBall::OnTableCalc() {
    mpPhysSphere->Update();
}

void RPBilBall::PocketedEnter() {}

void RPBilBall::PocketedCalc() {}

void RPBilBall::OutEnter() {
    mpSound->startSound(static_cast<unsigned int>(0x21), 0);
}

void RPBilBall::OutCalc() {}

void RPBilBall::ResetToRack() {
    mpStateMachine->ChangeState(STATE_ON_TABLE);
    PlaceInRack();
}

bool RPBilBall::SetPocketed(s32 shot) {
    if (!mpStateMachine->IsChanging()) {
        mpStateMachine->ChangeState(STATE_POCKETED);
        mPocketedShot = shot;
        return true;
    }
    return false;
}

bool RPBilBall::SetOut(s32 shot) {
    if (!mpStateMachine->IsChanging()) {
        mpStateMachine->ChangeState(STATE_OUT);
        mOutShot = shot;
        return true;
    }
    return false;
}

void RPBilBall::UserDraw() {}

void RPBilBall::PlaceInRack() {
    mpPhysSphere->SetPos(GetRackPosition(mIndex));
    mpPhysSphere->SetVec1C(EGG::Vector3f::zero);
    mpPhysSphere->mAngVel = EGG::Vector3f::zero;

    if (mIndex == 0) {
        EGG::Vector3f axisZ(-0.5f, -1.0f, 0.0f);
        EGG::Vector3f axisY(-1.0f, 0.5f, 0.0f);
        axisZ.normalise();
        axisY.normalise();
        EGG::Vector3f axisX = axisY.cross(axisZ);
        axisX.normalise();
        axisY = axisZ.cross(axisX);
        mpPhysSphere->SetAxisZ(axisZ);
        mpPhysSphere->SetAxisY(axisY);
        mpPhysSphere->SetAxisX(axisX);
    } else {
        mpPhysSphere->SetAxisZ(EGG::Vector3f(0.0f, 0.0f, 1.0f));
        mpPhysSphere->SetAxisY(EGG::Vector3f(0.0f, 1.0f, 0.0f));
        mpPhysSphere->SetAxisX(EGG::Vector3f(1.0f, 0.0f, 0.0f));
    }
    mpPhysSphere->ResetMatrices();
}

EGG::Vector3f RPBilBall::GetRackPosition(s32 index) {
    f32 radius = 1.01f * sRadius;
    f32 x = RPBilTable::GetRackOrigin().x;
    nw4r::math::VEC3 cue(-x, radius, 0.0f);
    nw4r::math::VEC3 head(x, radius, 0.0f);
    nw4r::math::VEC3 rack[2][10] = {
        {
            cue,
            head,
            nw4r::math::VEC3(head.x + 4.0f * radius, head.y, head.z + 2.0f * radius),
            nw4r::math::VEC3(head.x + 4.0f * radius, head.y, head.z - 2.0f * radius),
            nw4r::math::VEC3(head.x + 2.0f * radius, head.y, head.z + radius),
            nw4r::math::VEC3(head.x + 2.0f * radius, head.y, head.z - radius),
            nw4r::math::VEC3(head.x + 4.0f * radius, head.y, head.z),
        },
        {
            cue,
            head,
            nw4r::math::VEC3(head.x + 8.0f * radius, head.y, head.z),
            nw4r::math::VEC3(head.x + 4.0f * radius, head.y, head.z + 2.0f * radius),
            nw4r::math::VEC3(head.x + 4.0f * radius, head.y, head.z - 2.0f * radius),
            nw4r::math::VEC3(head.x + 2.0f * radius, head.y, head.z + radius),
            nw4r::math::VEC3(head.x + 2.0f * radius, head.y, head.z - radius),
            nw4r::math::VEC3(head.x + 6.0f * radius, head.y, head.z + radius),
            nw4r::math::VEC3(head.x + 6.0f * radius, head.y, head.z - radius),
            nw4r::math::VEC3(head.x + 4.0f * radius, head.y, head.z),
        },
    };

    EGG::Vector3f pos = rack[RPBilBallMgr::sInstance->GetRackMode()][index];
    if (index != 0) {
        f32 jitter = 0.05f * radius;
        pos.x += RandomScaled(2.0f * jitter) - jitter;
        pos.z += RandomScaled(2.0f * jitter) - jitter;
    }
    return pos;
}

#if !RP_BIL_BALL_ORIGINAL_CONSTNESS
// NONMATCHING: the state machine load is scheduled into the prologue (const this); matches with
// RP_BIL_BALL_ORIGINAL_CONSTNESS 1 (see RPBilBall.h)
#endif
bool RPBilBall::IntersectRay(const RPBilSegment& rSeg, EGG::Vector3f* pHitPos, f32* pDist,
                             f32 radius) RP_BIL_BALL_INTERSECT_RAY_CONST {
    if (mpStateMachine->GetState() == STATE_NONE) {
        return false;
    }

    EGG::Vector3f rel = rSeg.mEnd - static_cast<const EGG::Vector3f&>(mPos);
    EGG::Vector3f dir = rSeg.mStart;
    f32 a = dir.squaredLength();
    f32 b = 2.0f * rel.dot(dir);
    f32 r = radius + mpPhysSphere->GetRadius();
    f32 c = rel.squaredLength() - r * r;
    f32 disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return false;
    }

    f32 t = (-b - EGG::Math<f32>::sqrt(disc)) / (2.0f * a);
    if (t < 0.0f) {
        return false;
    }

    *pHitPos = rel + dir * t + static_cast<const EGG::Vector3f&>(mPos);
    if (pDist != NULL) {
        *pDist = t * dir.length();
    }
    return true;
}

#if !RP_BIL_BALL_ORIGINAL_CONSTNESS
// NONMATCHING: the state machine load is not scheduled into the prologue (non-const this);
// matches with RP_BIL_BALL_ORIGINAL_CONSTNESS 1 (see RPBilBall.h)
#endif
s32 RPBilBall::CheckPocket() RP_BIL_BALL_CHECK_POCKET_CONST {
    RPBilPocketCamWindow* pWindow = RPBilPocketCamWindow::sInstance;
    if (mpStateMachine->GetState() == STATE_ON_TABLE &&
        static_cast<const EGG::Vector3f&>(GetVec1C()).length() > sRestSpeed) {
        return pWindow->FindEnteredPocket(static_cast<const EGG::Vector3f&>(GetPos()),
                                          static_cast<const EGG::Vector3f&>(GetVec1C()));
    }
    return -1;
}

void RPBilBall::PlayKclHitSound(u32 kind, f32 speed) {
    f32 unit = sMass * sParam8;
    if (speed < 3.0f * unit) {
        return;
    }

    switch (kind) {
    case 1:
        if (speed > 20.0f * unit) {
            mpSound->playImpact(speed, 6);
        }
        break;
    case 2:
    case 3:
        mpSound->playImpact(speed, 7);
        break;
    case 4:
        mpSound->playImpact(speed, 0xF);
        break;
    case 6:
        mpSound->playImpact(speed, 0x20);
        break;
    }
}

// Wii Play: the accessors forward to the phys sphere. GetPos is in RPBilPlayer.cpp's range,
// GetVec1C here and the others after the state machine members (see the end of the file).
const nw4r::math::VEC3& RPBilBall::GetVec1C() const {
    return mpPhysSphere->GetVec1C();
}

void RPBilBall::UpdateRollingSound() {
    f32 linearSpeed = static_cast<const EGG::Vector3f&>(GetVec1C()).length();
    RPPhysSphere* pSphere = mpPhysSphere;
    EGG::Vector3f point(0.0f, -pSphere->GetRadius(), 0.0f);
    EGG::Vector3f spin = pSphere->CalcSpinVelocity(point);
    f32 contactSpeed = (static_cast<const EGG::Vector3f&>(GetVec1C()) + spin).length();
    mpSound->playRollingLoop(linearSpeed, contactSpeed, mIndex == 0);
}

void RPBilBall::PlayBallHitSound(f32 speed) {
    if (speed < 0.01f * sMass) {
        return;
    }
    mpSound->playImpact(speed, 8);
}

void RPBilBall::PlayCueHitSound(f32 power, s32 player, s32 ballIndex) {
    if (player >= 2) {
        return;
    }
    if (player < 0) {
        return;
    }
    if (ballIndex == 0) {
        mpSound->playCueHit(power, player, 1);
    } else {
        mpSound->playCueHit(power, player, 2);
    }
}

void RPBilBall::PlayPlaceSound() {
    mpSound->startSound(static_cast<unsigned int>(0x18), 0);
}

void RPBilBall::PlayRollSound(f32 speed) {
    mpSound->playRoll(speed);
}

// STAND-IN: dead-stripped; compiled first (defined last), it puts the u32->f32 magic double
// (lbl_804412C8) right after 0.0f and ahead of 0.01f (804412D0), as in the original .sdata2.
// Without it the double lands after 0.05f (804412F4), before PlaceInRack's -0.5f.
// (Wii only: GCC rejects `extern "C" static`, and the host has no .sdata2 order to keep.)
#if !defined(PORT_HOST)
extern "C" static f32 Bil_ballPoolOrderU32(u32 x) {
    return (f32)x;
}
#endif


#pragma defer_codegen off
#pragma auto_inline off
template void RPStateMachine<RPBilBall>::Calculate();
template void RPStateMachine<RPBilBall>::ChangeState(s32);
template void RPStateMachine<RPBilBall>::Reset(s32);
template void RPStateMachine<RPBilBall>::SetStateFuncs(const StateFunc&, const StateFunc&, s32);
template RPStateMachine<RPBilBall>::RPStateMachine(RPBilBall*, s32);

void RPBilBall::SetPos(const nw4r::math::VEC3& rPos) {
    mpPhysSphere->SetPos(rPos);
}

void RPBilBall::SetVec1C(const nw4r::math::VEC3& rVec) {
    mpPhysSphere->SetVec1C(rVec);
}

const nw4r::math::VEC3& RPBilBall::GetAxisZ() const {
    return mpPhysSphere->GetAxisZ();
}

void RPBilBall::SetAxisZ(const nw4r::math::VEC3& rVec) {
    mpPhysSphere->SetAxisZ(rVec);
}

const nw4r::math::VEC3& RPBilBall::GetAxisY() const {
    return mpPhysSphere->GetAxisY();
}

void RPBilBall::SetAxisY(const nw4r::math::VEC3& rVec) {
    mpPhysSphere->SetAxisY(rVec);
}

const nw4r::math::VEC3& RPBilBall::GetAxisX() const {
    return mpPhysSphere->GetAxisX();
}

void RPBilBall::SetAxisX(const nw4r::math::VEC3& rVec) {
    mpPhysSphere->SetAxisX(rVec);
}

f32 RPBilBall::GetScale() const {
    return mpPhysSphere->GetScale();
}

void RPBilBall::SetScale(f32 scale) {
    mpPhysSphere->SetScale(scale);
}

template void RPStateMachine<RPBilBall>::CallEnter();
template void RPStateMachine<RPBilBall>::CallCalc();
template void RPStateMachine<RPBilBall>::CallExit();
