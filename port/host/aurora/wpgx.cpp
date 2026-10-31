// wpgx: C glue between the port and Aurora (see wpgx.h, port/docs/HOST_BUILD.md §25).
#include "wpgx.h"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/vi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gx/fifo.hpp"
#include "webgpu/gpu.hpp"

#define WPGX_API extern "C" __attribute__((visibility("default")))

namespace {

// Bumped whenever our Aurora patches change what a cached pipeline means, so
// a stale pipeline_cache.db is discarded instead of loaded.
constexpr uint32_t kPipelineCacheKey = 3;

int gLogLevel = LOG_INFO;
bool gInitialized = false;
bool gInFrame = false;
std::string gDataDir;
std::string gAdapterName;
alignas(32) uint8_t gFifo[256 * 1024];
GXTexObj gTexObjs[GX_MAX_TEXMAP];
wpgx_address_resolver gResolver = nullptr;
void* gResolverUser = nullptr;
bool gCopyWriteback = false;
wpgx_copy_observer gCopyObserver = nullptr;
void* gCopyObserverUser = nullptr;
uint32_t gCopiesWritten = 0;

void LogCallback(AuroraLogLevel level, const char* module, const char* message, unsigned int) {
    static const char* const kNames[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (level >= gLogLevel) {
        std::fprintf(stderr, "[aurora %s %s] %s\n", kNames[level], module, message);
    }
    if (level == LOG_FATAL) {
        std::abort();
    }
}

} // namespace

WPGX_API int wpgx_init(const wpgx_config* config) {
    if (gInitialized || config == nullptr || config->data_dir == nullptr || config->data_dir[0] == '\0') {
        return -1;
    }
    gLogLevel = config->log_level;
    gDataDir = config->data_dir;
    const uint32_t width = config->width ? config->width : 640;
    const uint32_t height = config->height ? config->height : 480;

    AuroraConfig cfg{};
    cfg.appName = "wiiplay";
    cfg.userPath = gDataDir.c_str();
    cfg.cachePath = gDataDir.c_str();
    cfg.resourcesPath = gDataDir.c_str();
    cfg.desiredBackend = BACKEND_AUTO;
    cfg.allowCpuAdapter = config->allow_cpu_adapter != 0;
    cfg.windowWidth = width;
    cfg.windowHeight = height;
    cfg.logCallback = LogCallback;
    cfg.logLevel = static_cast<AuroraLogLevel>(config->log_level);
    cfg.disableShaderCache = config->disk_cache == 0;
    cfg.pipelineCacheKey = config->cache_key ? config->cache_key : kPipelineCacheKey;
    if (config->headless) {
        cfg.surface.type = AURORA_SURFACE_HEADLESS;
        cfg.surface.width = width;
        cfg.surface.height = height;
        cfg.surface.scale = 1.0f;
    }
    aurora_initialize(0, nullptr, &cfg);
    gAdapterName = std::string(aurora::webgpu::g_adapterInfo.device.data, aurora::webgpu::g_adapterInfo.device.length);

    GXInit(gFifo, sizeof gFifo);
    // The game's EFB is the configured size: viewports and scissors map 1:1
    // (Aurora scales GX coordinates from the VI's frame buffer size).
    GXRenderModeObj mode = GXNtsc480IntDf;
    mode.fbWidth = static_cast<u16>(width);
    mode.efbHeight = static_cast<u16>(height);
    mode.xfbHeight = static_cast<u16>(height);
    VIConfigure(&mode);
    AuroraSetDrawsBlocking(config->blocking_pipelines != 0);
    gInitialized = true;
    return 0;
}

WPGX_API void wpgx_shutdown(void) {
    if (gInitialized) {
        aurora_shutdown();
        gInitialized = false;
    }
}

WPGX_API int wpgx_begin_frame(void) {
    const AuroraEvent* event = aurora_update();
    while (event != nullptr && event->type != AURORA_NONE) {
        ++event;
    }
    gInFrame = aurora_begin_frame();
    return gInFrame ? 1 : 0;
}

WPGX_API void wpgx_end_frame(void) {
    gInFrame = false;
    aurora_end_frame();
    // Policy (HOST_BUILD.md §28): the frame's texture copies reach memory when it ends.
    if (gCopyWriteback) {
        AuroraFlushCopyWriteback();
    }
}

WPGX_API int wpgx_read_frame(uint8_t* rgba, size_t size, uint32_t* width, uint32_t* height) {
    return aurora_read_framebuffer(rgba, size, width, height) ? 0 : -1;
}

WPGX_API const char* wpgx_adapter_name(void) { return gAdapterName.c_str(); }

// --- The port's GX command stream -----------------------------------------------

WPGX_API void wpgx_fifo_write(const void* data, uint32_t size) { aurora::gx::fifo::write_data(data, size); }

WPGX_API void wpgx_fifo_sync(uint32_t kind) {
    // Outside a frame the commands wait for the next one (Aurora runs them
    // against the frame's render target).
    if (!gInFrame) {
        return;
    }
    if (kind == WPGX_SYNC_DRAWDONE) {
        aurora::gx::fifo::drain();
    } else {
        aurora::gx::fifo::publish();
    }
}

WPGX_API void wpgx_set_address_resolver(wpgx_address_resolver resolver, void* user) {
    gResolver = resolver;
    gResolverUser = user;
    AuroraSetAddressResolver(resolver, user);
}

namespace {
void CopyWriteback(const AuroraCopyWritebackInfo* copy, void*) {
    // The destination is memory the resolver maps (the emulated MEM1/MEM2)
    uint32_t size = 0;
    const void* p = gResolver != nullptr ? gResolver(copy->address, &size, gResolverUser) : nullptr;
    if (p == nullptr || size < copy->size) {
        std::fprintf(stderr, "wpgx: copy writeback to unmapped 0x%08X (%u bytes)\n", copy->address, copy->size);
        return;
    }
    std::memcpy(const_cast<void*>(p), copy->data, copy->size);
    ++gCopiesWritten;
    if (gCopyObserver != nullptr) {
        const wpgx_copy_info info = {copy->address,
                                     copy->format,
                                     copy->width,
                                     copy->height,
                                     copy->srcLeft,
                                     copy->srcTop,
                                     copy->srcWidth,
                                     copy->srcHeight,
                                     copy->half,
                                     copy->clear,
                                     copy->depth,
                                     copy->vfilter,
                                     copy->pixelFormat,
                                     {copy->vfilterWeights[0], copy->vfilterWeights[1], copy->vfilterWeights[2]},
                                     copy->data,
                                     copy->size,
                                     copy->efb,
                                     copy->efbWidth,
                                     copy->efbHeight};
        gCopyObserver(&info, gCopyObserverUser);
    }
}
} // namespace

WPGX_API void wpgx_set_copy_writeback(int writeback, int capture_sources, wpgx_copy_observer observer, void* user) {
    gCopyWriteback = writeback != 0;
    gCopyObserver = observer;
    gCopyObserverUser = user;
    AuroraSetCopyWriteback(gCopyWriteback ? CopyWriteback : nullptr, nullptr, capture_sources != 0);
}

WPGX_API uint32_t wpgx_copies_written(void) { return gCopiesWritten; }

// --- Test helpers -------------------------------------------------------------

WPGX_API void wpgx_test_setup_2d(uint32_t clear_rgba, uint32_t num_texgens) {
    const GXColor clear = {static_cast<u8>(clear_rgba >> 24), static_cast<u8>(clear_rgba >> 16),
                           static_cast<u8>(clear_rgba >> 8), static_cast<u8>(clear_rgba)};
    GXSetCopyClear(clear, GX_MAX_Z24);
    GXSetViewport(0, 0, 640, 480, 0, 1);
    GXSetScissor(0, 0, 640, 480);
    Mtx44 proj;
    C_MTXOrtho(proj, 0, 480, 0, 640, 0, 1);
    GXSetProjection(proj, GX_ORTHOGRAPHIC);
    Mtx id;
    PSMTXIdentity(id);
    GXLoadPosMtxImm(id, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(num_texgens);
    for (uint32_t i = 0; i < num_texgens; i++) {
        GXSetTexCoordGen(static_cast<GXTexCoordID>(GX_TEXCOORD0 + i), GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    }
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
}

WPGX_API void wpgx_test_load_texture(uint32_t map, const void* texels, uint16_t width, uint16_t height,
                                     uint32_t gx_format, uint32_t wrap_s, uint32_t wrap_t, int linear) {
    if (map >= GX_MAX_TEXMAP) {
        return;
    }
    GXTexObj* obj = &gTexObjs[map];
    GXInitTexObj(obj, const_cast<void*>(texels), width, height, static_cast<GXTexFmt>(gx_format),
                 static_cast<GXTexWrapMode>(wrap_s), static_cast<GXTexWrapMode>(wrap_t), GX_FALSE);
    const GXTexFilter filter = linear ? GX_LINEAR : GX_NEAR;
    GXInitTexObjLOD(obj, filter, filter, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(obj, static_cast<GXTexMapID>(map));
}

WPGX_API void wpgx_test_set_tev_replace(int passclr) {
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, passclr ? GX_PASSCLR : GX_REPLACE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
}

WPGX_API void wpgx_test_set_num_tev_stages(uint32_t n) { GXSetNumTevStages(n); }

WPGX_API void wpgx_test_call_display_list(const void* dl, uint32_t size) {
    GXCallDisplayList(const_cast<void*>(dl), size);
}

WPGX_API void wpgx_test_quad(float x0, float y0, float x1, float y1) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x0, y0, -0.5f);
    GXColor4u8(255, 255, 255, 255);
    GXTexCoord2f32(0, 0);
    GXPosition3f32(x1, y0, -0.5f);
    GXColor4u8(255, 255, 255, 255);
    GXTexCoord2f32(1, 0);
    GXPosition3f32(x1, y1, -0.5f);
    GXColor4u8(255, 255, 255, 255);
    GXTexCoord2f32(1, 1);
    GXPosition3f32(x0, y1, -0.5f);
    GXColor4u8(255, 255, 255, 255);
    GXTexCoord2f32(0, 1);
    GXEnd();
}
