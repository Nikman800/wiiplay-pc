/**
 * What input_harness links but does not run for real (docs/HOST_BUILD.md §23).
 *
 *  - The sound and player managers RPSysCoreControllerMgr calls when a remote
 *    connects or disconnects (the connect sound effect, the remote speaker's
 *    on / off callbacks, RPSysPlayerMgr::resetOldData): recorded as
 *    "E f=FRAME ..." lines. Their instances are zeroed stand-ins (the
 *    recording functions do not read them).
 *  - RPSysProjectLocal: no instance (RPSysCoreControllerMgr only asks it when
 *    it searches controllers by address, and checks for NULL).
 *  - Drawing: EGG::Frustum / Screen's virtual functions that load the
 *    projection into GX or a g3d camera (kept by the vtables; never called
 *    here): each aborts with its name.
 */

#include <Pack/RPAudio.h>
#include <Pack/RPSystem.h>

#include <egg/gfxe.h>
#include <nw4r/g3d.h>

#include <cstdio>
#include <cstdlib>

extern int gInputFrame;

alignas(16) static u8 sAudioMgrStorage[sizeof(RPSndAudioMgr)];
alignas(16) static u8 sPlayerMgrStorage[sizeof(RPSysPlayerMgr)];

RPSndAudioMgr* RPSndAudioMgr::spInstance = reinterpret_cast<RPSndAudioMgr*>(sAudioMgrStorage);
RPSysPlayerMgr* RPSysPlayerMgr::spInstance = reinterpret_cast<RPSysPlayerMgr*>(sPlayerMgrStorage);
RPSysProjectLocal* RPSysProjectLocal::spInstance = NULL;

void RPSndStaticMgr::startSe(u32 id, s32 player) {
    std::printf("E f=%d se=%u player=%d\n", gInputFrame, static_cast<unsigned>(id),
                static_cast<int>(player));
}

void RPSndSpeakerMgr::setSpeakerOnCallback(s32 chan, s32 result) {
    std::printf("E f=%d speaker_on ch=%d result=%d\n", gInputFrame, static_cast<int>(chan),
                static_cast<int>(result));
}

void RPSndSpeakerMgr::setSpeakerOffCallback(s32 chan, s32 result) {
    std::printf("E f=%d speaker_off ch=%d result=%d\n", gInputFrame, static_cast<int>(chan),
                static_cast<int>(result));
}

void RPSysPlayerMgr::resetOldData() {
    std::printf("E f=%d reset_old_data\n", gInputFrame);
}

extern "C" void OSRegisterVersion(const char* pVersion) {
    (void)pVersion;
}

static void Hit(const char* name) {
    std::fprintf(stderr, "input_harness: unexpected call to %s\n", name);
    std::abort();
}

namespace EGG {
u16 StateGX::s_widthFb;
u16 StateGX::s_heightEfb;
void StateGX::GXSetProjection_(f32 (*)[4], GXProjectionType) {
    Hit("EGG::StateGX::GXSetProjection_");
}
void StateGX::GXSetProjectionv_(const f32*) {
    Hit("EGG::StateGX::GXSetProjectionv_");
}
void StateGX::GXSetScissorBoxOffset_(int, int) {
    Hit("EGG::StateGX::GXSetScissorBoxOffset_");
}
void StateGX::GXSetScissor_(u32, u32, u32, u32) {
    Hit("EGG::StateGX::GXSetScissor_");
}
void StateGX::GXSetViewport_(f32, f32, f32, f32, f32, f32) {
    Hit("EGG::StateGX::GXSetViewport_");
}
void GXUtility::setScaleOffsetPerspective(f32*, f32, f32, f32, f32) {
    Hit("EGG::GXUtility::setScaleOffsetPerspective");
}
} // namespace EGG

namespace nw4r {
namespace g3d {
void Camera::SetOrtho(f32, f32, f32, f32, f32, f32) {
    Hit("nw4r::g3d::Camera::SetOrtho");
}
void Camera::SetPerspective(f32, f32, f32, f32) {
    Hit("nw4r::g3d::Camera::SetPerspective");
}
void Camera::SetProjectionMtxDirectly(const math::MTX44*) {
    Hit("nw4r::g3d::Camera::SetProjectionMtxDirectly");
}
void Camera::SetScissorBoxOffset(s32, s32) {
    Hit("nw4r::g3d::Camera::SetScissorBoxOffset");
}
void Camera::SetScissor(u32, u32, u32, u32) {
    Hit("nw4r::g3d::Camera::SetScissor");
}
void Camera::SetViewport(f32, f32, f32, f32) {
    Hit("nw4r::g3d::Camera::SetViewport");
}
void Camera::SetViewportZRange(f32, f32) {
    Hit("nw4r::g3d::Camera::SetViewportZRange");
}
} // namespace g3d
} // namespace nw4r
