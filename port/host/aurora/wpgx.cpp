// wpgx: C glue between the port and Aurora (see wpgx.h, port/docs/HOST_BUILD.md §25).
#include "wpgx.h"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <magic_enum.hpp>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/vi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <elf.h>
#include <link.h>

#include "gx/fifo.hpp"
#include "webgpu/gpu.hpp"

#define WPGX_API extern "C" __attribute__((visibility("default")))

// wpgx_platform.cpp: window, input and audio (milestone 5)
void wpgx_platform_init(bool headless, uint32_t width, uint32_t height);
void wpgx_platform_take_events(const AuroraEvent* events);
void wpgx_platform_shutdown();

namespace {

// Bumped whenever our Aurora patches change what a cached pipeline means, so
// a stale pipeline_cache.db is discarded instead of loaded.
constexpr uint32_t kPipelineCacheKey = 4;
// Lines of the hardware EFB (528 on the Wii).
constexpr uint32_t kEfbMaxHeight = 528;

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
std::string gAdapterType;
std::string gBackendName;
std::string gAdapterList;
wpgx_perf gPerf{};

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

// The GNU build ID of this library (the linker's hash of its contents): the
// on-disk pipeline cache is keyed by it, so every build starts its own cache.
uint32_t BuildIdKey() {
    struct Ctx {
        const void* self;
        uint32_t key;
    } ctx{reinterpret_cast<const void*>(&BuildIdKey), 0};
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            auto* c = static_cast<Ctx*>(data);
            const auto addr = reinterpret_cast<uintptr_t>(c->self);
            bool mine = false;
            for (int i = 0; i < info->dlpi_phnum && !mine; i++) {
                const auto& ph = info->dlpi_phdr[i];
                const uintptr_t start = info->dlpi_addr + ph.p_vaddr;
                mine = ph.p_type == PT_LOAD && addr >= start && addr < start + ph.p_memsz;
            }
            if (!mine) {
                return 0;
            }
            for (int i = 0; i < info->dlpi_phnum; i++) {
                const auto& ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_NOTE) {
                    continue;
                }
                const auto* p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
                const uint8_t* end = p + ph.p_memsz;
                while (p + sizeof(ElfW(Nhdr)) <= end) {
                    const auto* n = reinterpret_cast<const ElfW(Nhdr)*>(p);
                    const uint8_t* desc = p + sizeof(ElfW(Nhdr)) + ((n->n_namesz + 3) & ~3u);
                    if (n->n_type == NT_GNU_BUILD_ID) {
                        uint32_t h = 2166136261u;
                        for (uint32_t k = 0; k < n->n_descsz; k++) {
                            h = (h ^ desc[k]) * 16777619u;
                        }
                        c->key = h;
                    }
                    p = desc + ((n->n_descsz + 3) & ~3u);
                }
            }
            return 1;
        },
        &ctx);
    return ctx.key;
}

std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

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
    // The library's own key: our patches' version and this build's ID
    uint32_t key = config->cache_key;
    if (key == 0) {
        key = (kPipelineCacheKey * 0x9E3779B1u) ^ BuildIdKey();
        key = key != 0 ? key : kPipelineCacheKey;
    }
    cfg.pipelineCacheKey = key;
    cfg.vsync = config->vsync != 0;
    cfg.adapterName = config->adapter;
    if (config->headless) {
        cfg.surface.type = AURORA_SURFACE_HEADLESS;
        cfg.surface.width = width;
        cfg.surface.height = height;
        cfg.surface.scale = 1.0f;
    } else {
        cfg.appName = config->title != nullptr ? config->title : "wiiplay";
        cfg.windowWidth = config->window_width ? config->window_width : width;
        cfg.windowHeight = config->window_height ? config->window_height : height;
        cfg.startFullscreen = config->fullscreen != 0;
    }
    aurora_initialize(0, nullptr, &cfg);
    if (!config->headless) {
        aurora_set_vsync_mailbox(config->vsync_mailbox != 0);
        aurora_enable_vsync(config->vsync != 0);
    }
    wpgx_platform_init(config->headless != 0, width, height);
    if (!config->headless) {
        wpgx_fit_window();
    }
    gAdapterName = std::string(aurora::webgpu::g_adapterInfo.device.data, aurora::webgpu::g_adapterInfo.device.length);
    const auto& info = aurora::webgpu::g_adapterInfo;
    switch (info.adapterType) {
    case wgpu::AdapterType::DiscreteGPU:
        gAdapterType = "discrete";
        break;
    case wgpu::AdapterType::IntegratedGPU:
        gAdapterType = "integrated";
        break;
    case wgpu::AdapterType::CPU:
        gAdapterType = "cpu";
        break;
    default:
        gAdapterType = "unknown";
        break;
    }
    gBackendName = Lower(magic_enum::enum_name(info.backendType));
    gAdapterList.resize(aurora_get_adapter_list(nullptr, 0) + 1);
    aurora_get_adapter_list(gAdapterList.data(), gAdapterList.size());
    gAdapterList.resize(gAdapterList.size() - 1);

    GXInit(gFifo, sizeof gFifo);
    // The EFB is the hardware's: the configured width and 528 lines (or the
    // configured height if larger). Viewports and scissors map 1:1 (Aurora
    // scales GX coordinates from the VI's frame buffer size). The picture is
    // the configured size (the XFB height), which Aurora presents and reads
    // back; the lines below it are scratch space games render into and copy
    // out (EGG's light textures draw at y >= 456 under a 456-line picture).
    GXRenderModeObj mode = GXNtsc480IntDf;
    mode.fbWidth = static_cast<u16>(width);
    mode.efbHeight = static_cast<u16>(std::max<uint32_t>(height, kEfbMaxHeight));
    mode.xfbHeight = static_cast<u16>(height);
    VIConfigure(&mode);
    // The EFB stays that size (no resolution scaling, no widening to the
    // window's or the headless surface's aspect); the window scales the picture.
    VISetFrameBufferScale(1.0f);
    AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
    AuroraSetDrawsBlocking(config->blocking_pipelines != 0);
    gInitialized = true;
    return 0;
}

WPGX_API void wpgx_shutdown(void) {
    if (gInitialized) {
        wpgx_platform_shutdown();
        aurora_shutdown();
        gInitialized = false;
    }
}

WPGX_API void wpgx_get_frame_stats(wpgx_frame_stats* stats) {
    AuroraFrameStats a{};
    aurora_get_frame_stats(&a);
    stats->ended = a.framesEnded;
    stats->done = a.framesDone;
    stats->last_gpu_latency_ns = a.lastGpuLatencyNs;
    stats->gpu_latency_sum_ns = a.gpuLatencySumNs;
    stats->gpu_latency_max_ns = a.gpuLatencyMaxNs;
    stats->in_flight_wait_ns = a.inFlightWaitNs;
    stats->max_in_flight_seen = a.maxInFlightSeen;
    stats->max_frames_in_flight = a.maxFramesInFlight;
}

WPGX_API void wpgx_set_max_frames_in_flight(uint32_t frames) { aurora_set_max_frames_in_flight(frames); }

WPGX_API void wpgx_test_set_gpu_load(uint32_t mib, uint32_t repeats) { aurora_test_set_gpu_load(mib, repeats); }

WPGX_API int wpgx_begin_frame(void) {
    wpgx_platform_take_events(aurora_update());
    const uint64_t start = NowNs();
    gInFrame = aurora_begin_frame();
    gPerf.begin_frame_ns += NowNs() - start;
    return gInFrame ? 1 : 0;
}

WPGX_API void wpgx_end_frame(void) {
    gInFrame = false;
    const uint64_t start = NowNs();
    aurora_end_frame();
    const uint64_t ended = NowNs();
    gPerf.end_frame_ns += ended - start;
    // Policy (HOST_BUILD.md §28): the frame's texture copies reach memory when it ends.
    if (gCopyWriteback) {
        AuroraFlushCopyWriteback();
        gPerf.writeback_ns += NowNs() - ended;
    }
}

WPGX_API int wpgx_read_frame(uint8_t* rgba, size_t size, uint32_t* width, uint32_t* height) {
    const uint64_t start = NowNs();
    const bool ok = aurora_read_framebuffer(rgba, size, width, height);
    gPerf.read_frame_ns += NowNs() - start;
    return ok ? 0 : -1;
}

WPGX_API const char* wpgx_adapter_name(void) { return gAdapterName.c_str(); }
WPGX_API const char* wpgx_adapter_type(void) { return gAdapterType.c_str(); }
WPGX_API const char* wpgx_backend_name(void) { return gBackendName.c_str(); }
WPGX_API const char* wpgx_adapter_list(void) { return gAdapterList.c_str(); }
WPGX_API const char* wpgx_present_mode(void) { return aurora_get_present_mode(); }
WPGX_API void wpgx_set_blocking_pipelines(int blocking) { AuroraSetDrawsBlocking(blocking != 0); }

WPGX_API void wpgx_get_perf(wpgx_perf* perf) {
    const AuroraStats* st = aurora_get_stats();
    gPerf.pipeline_compile_ns = __atomic_load_n(&st->pipelineCompileNs, __ATOMIC_RELAXED);
    gPerf.pipeline_wait_ns = __atomic_load_n(&st->pipelineWaitNs, __ATOMIC_RELAXED);
    gPerf.pipelines_created = __atomic_load_n(&st->createdPipelines, __ATOMIC_RELAXED);
    gPerf.pipelines_queued = __atomic_load_n(&st->queuedPipelines, __ATOMIC_RELAXED);
    gPerf.pipeline_waits = __atomic_load_n(&st->pipelineWaits, __ATOMIC_RELAXED);
    gPerf.skipped_draws = __atomic_load_n(&st->skippedDraws, __ATOMIC_RELAXED);
    gPerf.draw_calls = st->drawCallCount;
    gPerf.copies_written = gCopiesWritten;
    *perf = gPerf;
}

WPGX_API int wpgx_overlay(const char* text, const float* values, int count, float graph_max, float graph_mark) {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr || !ctx->WithinFrameScope) {
        return 0;
    }
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("wiiplay perf", nullptr, flags)) {
        ImGui::TextUnformatted(text);
        if (values != nullptr && count > 0) {
            const ImVec2 size(static_cast<float>(count) * 2.0f, 60.0f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::PlotHistogram("##frames", values, count, 0, nullptr, 0.0f, graph_max, size);
            if (graph_mark > 0.0f && graph_mark < graph_max) {
                const float y = origin.y + size.y * (1.0f - graph_mark / graph_max);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
                                                    IM_COL32(255, 80, 80, 200));
            }
        }
    }
    ImGui::End();
    return 1;
}

WPGX_API int wpgx_marker(void) {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr || !ctx->WithinFrameScope) {
        return 0;
    }
    AuroraPresentViewport vp{};
    float x = 0.f, y = 0.f, side = 48.f;
    if (aurora_get_present_viewport(&vp) && vp.valid && vp.windowWidth > 0 && vp.surfaceWidth > 0) {
        // ImGui draws in window points
        const float k = static_cast<float>(vp.windowWidth) / static_cast<float>(vp.surfaceWidth);
        x = vp.left * k;
        y = vp.top * k;
        side = vp.width * k / 12.f;
    }
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + side, y + side), IM_COL32(255, 255, 255, 255));
    return 1;
}

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
