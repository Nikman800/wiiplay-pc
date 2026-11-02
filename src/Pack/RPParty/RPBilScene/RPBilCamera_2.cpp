// Billiards main camera (RPBilCamera): default, overview, aim and look-at views, orbiting, and
// the camera's sound actor (RPBilSoundActor destructor and vtable).
// Unit 0x802BBCD0-0x802BCC5C. Functions in reverse address order.
#include <Pack/RPParty/RPBilScene/RPBilCamera.h>

#include <Pack/RPParty/RPBilScene/RPBilSound.h>

#include <Pack/RPGraphics.h>

#include <egg/core/eggController.h>
#include <egg/gfxe.h>
#include <egg/math.h>

f32 RPBilCamera::sFovy = 40.0f;
s32 RPBilCamera::sLookAtFrames = 120;
f32 RPBilCamera::sLookAtDist = 20.0f;
s32 RPBilCamera::sAimFrames = 60;
f32 RPBilCamera::sAimHeight = 3.0f;
f32 RPBilCamera::sPitchMin = 0.2f;
f32 RPBilCamera::sPitchMax = 0.95f;
f32 RPBilCamera::sZoomNearAlt = 25.0f;
f32 RPBilCamera::sZoomNear = 30.0f;
f32 RPBilCamera::sZoomFar = 45.0f;
f32 RPBilCamera::sOverviewDist = 270.0f;
f32 RPBilCamera::sOverviewDir = -1.0f;
s32 RPBilCamera::sDefaultFrames = 60;
f32 RPBilCamera::sBaseDist = 280.0f;
f32 RPBilCamera::sDefaultDrop = -15.0f;

EGG::Vector3f RPBilCamera::sDefaultPos(-300.0f, 100.0f, 300.0f);
EGG::Vector3f RPBilCamera::sDefaultTarget(100.0f, 0.0f, 0.0f);
EGG::Vector3f RPBilCamera::sDefaultUp(0.0f, 1.0f, 0.0f);
EGG::Vector3f RPBilCamera::sOverviewOffset(0.0f, 20.0f, 0.0f);


namespace {

// A Vector3f made from a VEC3 element by element (Vector3f(const VEC3&) copies words)
struct BilVec3 : EGG::Vector3f {
    BilVec3(const nw4r::math::VEC3& rVec) : EGG::Vector3f(rVec.x, rVec.y, rVec.z) {}

    BilVec3& operator=(const EGG::Vector3f& rRhs) {
        EGG::Vector3f::operator=(rRhs);
        return *this;
    }
};

} // namespace

// The binary inlines RPSndObject2D's (empty) destructor into the derived one. The host uses
// RPWlySound_2.cpp's out-of-line one: ELF linkers let the strong copy win over
// such a copy, PE/COFF's report two definitions (port/docs/HOST_BUILD.md §41).
#if !defined(PORT_HOST)
inline RPSndObject2D::~RPSndObject2D() {}
#endif

RPBilSoundActor::~RPBilSoundActor() {}

RPBilCamera::~RPBilCamera() {}

f32 RPBilCamera::GetZoomDistance() {
    return EGG::Screen::GetTVMode() == EGG::Screen::TV_MODE_STD ? sZoomNear
                                                                : sZoomNearAlt;
}

f32 RPBilCamera::GetBaseDistance() {
    return sBaseDist;
}

RPBilCamera::RPBilCamera() {
    mpSoundActor = new RPBilSoundActor();
}

void RPBilCamera::Reset() {
    RPInterpCamera::Reset();

    SetPosition(sDefaultPos);
    SetTarget(sDefaultTarget);
    SetUp(sDefaultUp);
    RPGrpRenderer::GetCurrent()->GetView(4)->GetScreen()->SetFovy(sFovy);
    CalcMatrix();

    mHasSavedView = false;
    mAimTarget = EGG::Vector3f::zero;
}

void RPBilCamera::Calculate() {
    EGG::CoreControllerMgr::instance()->func_800A6F50(0);
    RPGrpRenderer::GetCurrent()->GetView(4)->GetScreen()->SetFovy(sFovy);
    RPInterpCamera::Calculate();

    EGG::LightObject* pLight =
        RPGrpModelManager::GetCurrent()->GetLightManager(1)->GetLightObj(0);

    BilVec3 dir = GetPosition() - pLight->GetAt();
    dir.y = 0.0f;
    if (dir.normalise() > FLT_EPSILON) {
        dir *= 7000.0f;
        BilVec3 pos = pLight->GetAt();
        pos += dir;
        pos.y += 10000.0f;
        pLight->SetPos(pos);
        pLight->CalcDirDist();
    }
}

// STAND-IN: dead-stripped; puts FLT_EPSILON (lbl_804411EC) after -1.0f and before Reset/SetFovy's
// 180.0f, 0.5f, 0.711111f (804411F0..F8), as in the original .sdata2. Without it FLT_EPSILON
// lands after 0.711111f, ahead of Calculate's 7000.0f.
extern "C" bool Bil_cameraPoolOrderEpsilon(f32 x) {
    return x > FLT_EPSILON;
}

void RPBilCamera::SaveView() {
    mHasSavedView = true;
    static_cast<nw4r::math::VEC3&>(mSavedPos) = GetPosition();
    static_cast<nw4r::math::VEC3&>(mSavedTarget) = GetTargetRef();
    static_cast<nw4r::math::VEC3&>(mSavedUp) = GetUp();
}

void RPBilCamera::MoveToLookAt(const EGG::Vector3f& rTarget, const EGG::Vector3f& rDir) {
    EGG::Vector3f pos = rTarget + rDir * sLookAtDist;
    EGG::Vector3f target = rTarget;
    EGG::Vector3f up = -rDir.cross(EGG::Vector3f(0.0f, 0.0f, -1.0f));
    up.normalise();

    Start(sLookAtFrames, pos, target, up, 1);
}

void RPBilCamera::MoveToAimAt(const EGG::Vector3f& rFrom, const EGG::Vector3f& rTarget) {
    EGG::Vector3f dir = rTarget - rFrom;
    dir.y = 0.0f;
    dir.normalise();

    EGG::Vector3f up = dir * EGG::Math<f32>::sqrt(1.0f - sPitchMax * sPitchMax);
    up.y = sPitchMax;

    EGG::Vector3f side = up.cross(dir);
    side.normalise();
    EGG::Vector3f back = side.cross(up);

    EGG::Vector3f target = rFrom + EGG::Vector3f(0.0f, 1.0f, 0.0f) * sAimHeight;
    f32 zoom = GetZoomDistance();
    EGG::Vector3f pos =
        target - back * ((sZoomFar - zoom) * ((sPitchMax - up.y) /
                                                       (sPitchMax - sPitchMin)) +
                         zoom);

    Start(sAimFrames, pos, target, up, 1);
    mAimTarget = rFrom;
}

void RPBilCamera::OrbitYaw(f32 delta) {
    if (mActive) {
        return;
    }

    EGG::Quatf q;
    EGG::Vector3f axis(0.0f, 1.0f, 0.0f);
    q.setAxisRotation(axis, delta);

    BilVec3 dir = GetTargetRef() - GetPosition();
    f32 dist = dir.normalise();
    dir = q.rotateVector(dir);
    f32 zoom = GetZoomDistance();

    SetPosition(GetTargetRef() - dir * dist);
    SetUp(q.rotateVector(BilVec3(GetUp())));
    mpSoundActor->PlayCameraTick(delta);
}

void RPBilCamera::OrbitPitch(f32 delta) {
    if (mActive) {
        return;
    }

    EGG::Quatf q;
    BilVec3 dir = GetTargetRef() - GetPosition();
    dir.normalise();
    EGG::Vector3f worldUp(0.0f, 1.0f, 0.0f);
    EGG::Vector3f axis = dir.cross(worldUp);
    axis.normalise();
    q.setAxisRotation(axis, delta);

    dir = q.rotateVector(dir);
    EGG::Vector3f up = q.rotateVector(BilVec3(GetUp()));

    if (up.y < sPitchMin || up.y > sPitchMax) {
        return;
    }

    f32 zoom = GetZoomDistance();
    f32 dist = (sZoomFar - zoom) *
                   ((sPitchMax - up.y) / (sPitchMax - sPitchMin)) +
               zoom;
    f32 scale = dist / zoom;

    EGG::Vector3f target = mAimTarget + worldUp * sAimHeight * scale;
    EGG::Vector3f pos = target - dir * dist;

    SetPosition(pos);
    SetTarget(target);
    SetUp(up);
    mpSoundActor->PlayCameraTick(delta);
}

void RPBilCamera::MoveToDefaultView() {
    BilVec3 dir = GetTargetRef() - GetPosition();
    RPGrpCamera camera;
    ComputeDefaultView(&camera, dir);

    Start(sDefaultFrames, BilVec3(camera.GetPosition()), BilVec3(camera.GetTargetRef()),
          BilVec3(camera.GetUp()), 1);
}

void RPBilCamera::MoveToOverview(s32 frames) {
    BilVec3 dir = GetTargetRef() - GetPosition();
    EGG::Vector3f back(0.0f, 0.0f, 1.0f);
    if (dir.z < 0.0f) {
        back.z = -back.z;
    }

    back.y += sOverviewDir;
    back.normalise();

    EGG::Vector3f up(0.0f, 1.0f, 0.0f);
    EGG::Vector3f side = up.cross(back);
    side.normalise();
    up = back.cross(side);

    Start(frames, sOverviewOffset - back * sOverviewDist, sOverviewOffset, up, 1);
}

void RPBilCamera::ComputeDefaultView(RPGrpCamera* pCamera, const EGG::Vector3f& rDir) {
    EGG::Vector3f back(0.0f, 0.0f, 1.0f);
    if (rDir.z < 0.0f) {
        back.z = -back.z;
    }

    EGG::Vector3f down(0.0f, -1.0f, 0.0f);
    EGG::Vector3f offset = -down * sBaseDist;
    pCamera->SetPosition(-down * sBaseDist + back * sDefaultDrop);
    pCamera->SetTarget(back * sDefaultDrop);
    pCamera->SetUp(back);
}
