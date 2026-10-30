// wpgx: C glue between the port and Aurora (see wpgx.h, port/docs/HOST_BUILD.md §25).
#include "wpgx.h"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "webgpu/gpu.hpp"

#define WPGX_API extern "C" __attribute__((visibility("default")))

namespace {

// Bumped whenever our Aurora patches change what a cached pipeline means, so
// a stale pipeline_cache.db is discarded instead of loaded.
constexpr uint32_t kPipelineCacheKey = 1;

int gLogLevel = LOG_INFO;
bool gInitialized = false;
std::string gDataDir;
std::string gAdapterName;
alignas(32) uint8_t gFifo[256 * 1024];
GXTexObj gTexObjs[GX_MAX_TEXMAP];

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
    return aurora_begin_frame() ? 1 : 0;
}

WPGX_API void wpgx_end_frame(void) { aurora_end_frame(); }

WPGX_API int wpgx_read_frame(uint8_t* rgba, size_t size, uint32_t* width, uint32_t* height) {
    return aurora_read_framebuffer(rgba, size, width, height) ? 0 : -1;
}

WPGX_API const char* wpgx_adapter_name(void) { return gAdapterName.c_str(); }

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
