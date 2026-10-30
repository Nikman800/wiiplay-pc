/**
 * Stand-ins for the GX, g3d and game symbols that the EGG light, light-map and
 * bloom objects, the game table loaders and the nw4r::ef draw strategies
 * reference but that are not built on the host yet (port/docs/HOST_BUILD.md
 * §14, §19). The harnesses link them with --gc-sections, so only what the kept
 * code references is listed.
 *
 * Everything here aborts with its name if called: drawing, capture and the
 * game's scene singletons are Phase 7 work, and nothing on the load or calc
 * paths reaches them. The exceptions are data (zero-initialised, as in .bss)
 * and OSRegisterVersion.
 * PSVECMag and C_MTXOrtho come from src/revolution/MTX (docs/HOST_BUILD.md §18).
 *
 * C++ symbols are given by their (Itanium) mangled names, which are the same
 * for the 64- and 32-bit builds.
 */
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
/* The nw4r::ef draw strategies (ptcl_harness, docs/HOST_BUILD.md §19). */
PORT_STUB(GXEnableTexOffsets)
PORT_STUB(GXGetVtxAttrFmt)
PORT_STUB(GXGetVtxDesc)
PORT_STUB(GXSetChanAmbColor)
PORT_STUB(GXSetClipMode)
PORT_STUB(GXSetCoPlanar)
PORT_STUB(GXSetLineWidth)
PORT_STUB(GXSetPointSize)
PORT_STUB(GXSetZCompLoc)

/* OSRegisterVersion records a library's version banner (nw4r::ef's
 * EffectSystem constructor, a static initialiser): nothing to do on the host. */
void OSRegisterVersion(const char* pVersion);
void OSRegisterVersion(const char* pVersion) {
    (void)pVersion;
}

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
