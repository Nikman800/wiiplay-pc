/**
 * Stand-ins for the GX, g3d and game symbols that the EGG light, light-map and
 * bloom objects and the game table loaders reference but that are not built
 * on the host yet (port/docs/HOST_BUILD.md §14). data_harness links them with
 * --gc-sections, so only what the kept code references is listed.
 *
 * Everything here aborts with its name if called: drawing, capture and the
 * game's scene singletons are Phase 7 work, and nothing on the load path
 * reaches them. The exceptions are data (zero-initialised, as in .bss) and
 * PSVECMag, which the light objects call while loading; the Wii's version is
 * paired-single assembly, this one computes the same magnitude in C.
 *
 * C++ symbols are given by their (Itanium) mangled names, which are the same
 * for the 64- and 32-bit builds.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void PortStubHit(const char* pName) {
    fprintf(stderr, "host_gfx_stubs: %s called on the host (not implemented)\n", pName);
    abort();
}

#define PORT_STUB(name)                                                            \
    void name(void);                                                               \
    void name(void) {                                                              \
        PortStubHit(#name);                                                        \
    }

#define PORT_STUB_MANGLED(id, mangled, readable)                                   \
    void port_stub_##id(void) __asm__(mangled);                                    \
    void port_stub_##id(void) {                                                    \
        PortStubHit(readable);                                                     \
    }

/* GX (src/revolution/GX is not built on the host; Phase 7 replaces it). */
PORT_STUB(GXCopyTex)
PORT_STUB(GXGetProjectionv)
PORT_STUB(GXGetScissor)
PORT_STUB(GXGetViewportv)
PORT_STUB(GXInvalidateTexAll)
PORT_STUB(GXPixModeSync)
PORT_STUB(GXSetAlphaUpdate)
PORT_STUB(GXSetColorUpdate)
PORT_STUB(GXSetCopyClear)
PORT_STUB(GXSetCopyFilter)
PORT_STUB(GXSetCullMode)
PORT_STUB(GXSetDither)
PORT_STUB(GXSetPixelFmt)
PORT_STUB(GXSetProjectionv)
PORT_STUB(GXSetScissor)
PORT_STUB(GXSetScissorBoxOffset)
PORT_STUB(GXSetTexCopyDst)
PORT_STUB(GXSetTexCopySrc)
PORT_STUB(GXSetViewport)
PORT_STUB(GXSetZMode)
PORT_STUB(C_MTXOrtho)

/* Game code in units that do not build on the host yet. */
PORT_STUB(Bom_shuffleArray)

/* nw4r::g3d scene objects (not built on the host yet). */
PORT_STUB_MANGLED(cam_ortho, "_ZN4nw4r3g3d6Camera8SetOrthoEffffff", "g3d::Camera::SetOrtho")
PORT_STUB_MANGLED(cam_persp, "_ZN4nw4r3g3d6Camera14SetPerspectiveEffff",
                  "g3d::Camera::SetPerspective")
PORT_STUB_MANGLED(cam_proj, "_ZN4nw4r3g3d6Camera24SetProjectionMtxDirectlyEPKNS_4math5MTX44E",
                  "g3d::Camera::SetProjectionMtxDirectly")
PORT_STUB_MANGLED(cam_sbo, "_ZN4nw4r3g3d6Camera19SetScissorBoxOffsetEii",
                  "g3d::Camera::SetScissorBoxOffset")
PORT_STUB_MANGLED(cam_sc, "_ZN4nw4r3g3d6Camera10SetScissorEjjjj", "g3d::Camera::SetScissor")
PORT_STUB_MANGLED(cam_vp, "_ZN4nw4r3g3d6Camera11SetViewportEffff", "g3d::Camera::SetViewport")
PORT_STUB_MANGLED(cam_vpz, "_ZN4nw4r3g3d6Camera17SetViewportZRangeEff",
                  "g3d::Camera::SetViewportZRange")
PORT_STUB_MANGLED(obj_destroy, "_ZN4nw4r3g3d6G3dObj7DestroyEv", "g3d::G3dObj::Destroy")

/* Data the kept code refers to (.bss on the Wii). */
void* port_stub_bom_game_manager __asm__("_ZN16RPBomGameManager9sInstanceE") = 0;
unsigned int RP_randSeedX;
unsigned int RP_randSeedY;
float RP_vecZero[3];

/* PSVECMag (src/revolution/MTX/vec.c is paired-single code). */
float PSVECMag(const float* pVec);
float PSVECMag(const float* pVec) {
    return sqrtf(pVec[0] * pVec[0] + pVec[1] * pVec[1] + pVec[2] * pVec[2]);
}
