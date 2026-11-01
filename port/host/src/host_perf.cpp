/**
 * The performance tracker (include/port/perf.h, port/docs/HOST_BUILD.md §35).
 *
 * A frame is the time from one Aurora frame begun to the next (each
 * GXCopyDisp ends one). Its wall time splits into:
 *   gx       the GX stream into Aurora (FIFO writes, flushes, draw-done syncs)
 *   end      wpgx_end_frame: the FIFO drained, the frame recorded and handed
 *            to the render thread
 *   gpu      waiting for the GPU: the copy writeback's readbacks, frame reads
 *   present  wpgx_begin_frame: waiting for a free frame slot (the GPU or the
 *            display's present queue is behind)
 *   sleep    the pacer sleeping (real time)
 *   input    the window system's events
 *   calc     the rest: the game's own code (and the AX mixer, the clock)
 * Uses only malloc'd memory (the game's heaps back operator new).
 */
#include <types.h>

#include <port/live.h>
#include <port/perf.h>

#include "wpgx.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

enum Col {
    C_WALL,
    C_CALC,
    C_GX,
    C_END,
    C_GPU,
    C_PRESENT,
    C_SLEEP,
    C_INPUT,
    C_PIPE_MS, // pipeline compilation (any thread)
    C_PIPE_WAIT_MS,
    C_AUDIO_MS, // audio queued at the frame's end
    C_COUNT
};
const char* const kColNames[C_COUNT] = {"wall_ms",   "calc_ms",  "gx_ms",       "end_ms",
                                        "gpu_ms",    "present_ms", "sleep_ms",  "input_ms",
                                        "pipe_compile_ms", "pipe_wait_ms", "audio_queue_ms"};

struct Row {
    int frame, scene;
    float v[C_COUNT];
    u32 pipelines, pipelineWaits, skippedDraws, drawCalls, copies;
    u32 underruns, skippedAudio, overruns, lateResets;
    float correction;
    float targetX, targetY, kpadX, kpadY;
    u8 targetValid, kpadValid;
};

bool sActive = false;
bool sOverlay = false;
char* sCsvPath = nullptr;
char sLabel[512];
Row* sRows = nullptr;
size_t sCount = 0, sCap = 0;
u64 sHost[PORT_PERF_HOST_PHASES];
u64 sLastFrame = 0;
u64 sStartNs = 0;
wpgx_perf sLastPerf;
PortLiveStats sLastLive;
bool sHaveLive = false;
// The overlay's graph and its numbers (the last 120 frames)
const int kGraph = 120;
float sGraph[kGraph];
int sGraphPos = 0;
char sOverlayText[1024];

double Ms(u64 ns) {
    return static_cast<double>(ns) / 1e6;
}

void Push(const Row& r) {
    if (sCount == sCap) {
        size_t cap = sCap ? sCap * 2 : 4096;
        Row* rows = static_cast<Row*>(std::realloc(sRows, cap * sizeof(Row)));
        if (rows == nullptr) {
            return; // out of memory: stop recording rows
        }
        sRows = rows;
        sCap = cap;
    }
    sRows[sCount++] = r;
}

struct Stat {
    double min, avg, p50, p95, p99, max;
};

Stat Summarise(int col, double* scratch) {
    Stat s{};
    if (sCount == 0) {
        return s;
    }
    double sum = 0;
    for (size_t i = 0; i < sCount; i++) {
        scratch[i] = sRows[i].v[col];
        sum += scratch[i];
    }
    std::sort(scratch, scratch + sCount);
    auto pct = [&](double p) { return scratch[std::min(sCount - 1, static_cast<size_t>(p * (sCount - 1) + 0.5))]; };
    s.min = scratch[0];
    s.max = scratch[sCount - 1];
    s.avg = sum / sCount;
    s.p50 = pct(0.50);
    s.p95 = pct(0.95);
    s.p99 = pct(0.99);
    return s;
}

// The phase that took most of a frame (not counting sleep).
const char* Dominant(const Row& r) {
    int best = C_CALC;
    for (int c = C_CALC; c <= C_INPUT; c++) {
        if (c != C_SLEEP && r.v[c] > r.v[best]) {
            best = c;
        }
    }
    static char name[32];
    std::snprintf(name, sizeof name, "%.*s", static_cast<int>(std::strlen(kColNames[best]) - 3), kColNames[best]);
    return name;
}

// The cursor: the game's KPAD pointer against the target (the mouse) over
// frames where both are valid. lag = the shift k (frames) for which the
// pointer best matches the target k frames earlier; error = the remaining
// distance at that shift, and at rest (the target unchanged for 30 frames).
struct CursorStats {
    int frames, moving, lag;
    double errAtLag, errLag0, restErr, restMax;
    int restFrames;
};

CursorStats CursorSummary() {
    CursorStats cs{};
    const int maxLag = 15;
    double best = 1e30;
    for (int k = 0; k <= maxLag; k++) {
        double sum = 0;
        int n = 0;
        for (size_t i = k; i < sCount; i++) {
            const Row& r = sRows[i];
            const Row& t = sRows[i - k];
            if (!r.kpadValid || !t.targetValid) {
                continue;
            }
            // only where the target moves (a still target fits any lag)
            if (i < 1 || (sRows[i - 1].targetX == r.targetX && sRows[i - 1].targetY == r.targetY &&
                          t.targetX == r.targetX && t.targetY == r.targetY)) {
                continue;
            }
            sum += std::hypot(r.kpadX - t.targetX, r.kpadY - t.targetY);
            n++;
        }
        if (n == 0) {
            continue;
        }
        double err = sum / n;
        if (k == 0) {
            cs.errLag0 = err;
            cs.moving = n;
        }
        if (err < best) {
            best = err;
            cs.lag = k;
        }
    }
    cs.errAtLag = best < 1e30 ? best : 0;
    int still = 0;
    double restSum = 0;
    for (size_t i = 0; i < sCount; i++) {
        const Row& r = sRows[i];
        if (!r.kpadValid || !r.targetValid) {
            still = 0;
            continue;
        }
        cs.frames++;
        if (i > 0 && sRows[i - 1].targetX == r.targetX && sRows[i - 1].targetY == r.targetY) {
            still++;
        } else {
            still = 0;
        }
        if (still >= 30) {
            double e = std::hypot(r.kpadX - r.targetX, r.kpadY - r.targetY);
            restSum += e;
            cs.restMax = std::max(cs.restMax, e);
            cs.restFrames++;
        }
    }
    cs.restErr = cs.restFrames ? restSum / cs.restFrames : 0;
    return cs;
}

void WriteSummary(FILE* f) {
    double* scratch = static_cast<double*>(std::malloc(sizeof(double) * (sCount ? sCount : 1)));
    if (scratch == nullptr) {
        return;
    }
    const double seconds = sCount ? Ms(sLastFrame - sStartNs) / 1000.0 : 0;
    std::fprintf(f, "=== wiiplay perf summary ===\n%s\n", sLabel);
    std::fprintf(f, "frames %zu over %.1f s (%.2f fps)\n", sCount, seconds, seconds > 0 ? sCount / seconds : 0.0);
    std::fprintf(f, "%-16s %8s %8s %8s %8s %8s %8s\n", "phase (ms)", "min", "avg", "p50", "p95", "p99", "max");
    for (int c = 0; c < C_COUNT; c++) {
        Stat s = Summarise(c, scratch);
        std::fprintf(f, "%-16s %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f\n", kColNames[c], s.min, s.avg, s.p50, s.p95,
                     s.p99, s.max);
    }
    size_t over16 = 0, over20 = 0, over33 = 0;
    u32 pipelines = 0, waits = 0, skipped = 0;
    double busySum = 0, busyMax = 0;
    for (size_t i = 0; i < sCount; i++) {
        const Row& r = sRows[i];
        over16 += r.v[C_WALL] > 16.7f;
        over20 += r.v[C_WALL] > 20.0f;
        over33 += r.v[C_WALL] > 33.3f;
        pipelines += r.pipelines;
        waits += r.pipelineWaits;
        skipped += r.skippedDraws;
        double busy = r.v[C_WALL] - r.v[C_SLEEP];
        busySum += busy;
        busyMax = std::max(busyMax, busy);
    }
    std::fprintf(f, "frames over 16.7 ms: %zu, over 20 ms: %zu, over 33.3 ms: %zu\n", over16, over20, over33);
    std::fprintf(f, "busy (wall - sleep): avg %.2f ms, max %.2f ms\n", sCount ? busySum / sCount : 0.0, busyMax);
    std::fprintf(f, "pipelines: %u compiled, %u draws waited, %u draws skipped\n", pipelines, waits, skipped);
    if (sCount > 0) {
        const Row& last = sRows[sCount - 1];
        std::fprintf(f,
                     "audio: underruns %u, AX frames skipped %u, overruns %u, pacer re-anchors %u, "
                     "last correction %+.3f %%\n",
                     last.underruns, last.skippedAudio, last.overruns, last.lateResets, 100.0 * last.correction);
    }
    CursorStats cs = CursorSummary();
    if (cs.moving > 0 || cs.restFrames > 0) {
        std::fprintf(f,
                     "cursor (KPAD pointer vs mouse, units of half the picture width): lag %d frames; "
                     "error moving %.4f at lag 0, %.4f at lag %d (%d frames); at rest %.4f avg, %.4f max (%d frames)\n",
                     cs.lag, cs.errLag0, cs.errAtLag, cs.lag, cs.moving, cs.restErr, cs.restMax, cs.restFrames);
    } else {
        std::fprintf(f, "cursor: no mouse movement recorded\n");
    }
    // The worst frames
    size_t* idx = static_cast<size_t*>(std::malloc(sizeof(size_t) * (sCount ? sCount : 1)));
    if (idx != nullptr) {
        for (size_t i = 0; i < sCount; i++) {
            idx[i] = i;
        }
        size_t top = std::min<size_t>(10, sCount);
        std::partial_sort(idx, idx + top, idx + sCount,
                          [](size_t a, size_t b) { return sRows[a].v[C_WALL] > sRows[b].v[C_WALL]; });
        std::fprintf(f, "worst frames (frame scene wall_ms busy_ms dominant pipelines):\n");
        for (size_t i = 0; i < top; i++) {
            const Row& r = sRows[idx[i]];
            std::fprintf(f, "  %6d %3d %7.2f %7.2f %-8s %u\n", r.frame, r.scene, r.v[C_WALL],
                         r.v[C_WALL] - r.v[C_SLEEP], Dominant(r), r.pipelines);
        }
        std::free(idx);
    }
    std::free(scratch);
}

void WriteCsv(const char* path) {
    FILE* f = std::fopen(path, "w");
    if (f == nullptr) {
        std::fprintf(stderr, "boot: perf: cannot write %s\n", path);
        return;
    }
    std::fprintf(f, "frame,scene");
    for (int c = 0; c < C_COUNT; c++) {
        std::fprintf(f, ",%s", kColNames[c]);
    }
    std::fprintf(f, ",pipelines,pipeline_waits,skipped_draws,draw_calls,copies,underruns,audio_skipped,overruns,"
                    "late_resets,correction,target_x,target_y,kpad_x,kpad_y\n");
    for (size_t i = 0; i < sCount; i++) {
        const Row& r = sRows[i];
        std::fprintf(f, "%d,%d", r.frame, r.scene);
        for (int c = 0; c < C_COUNT; c++) {
            std::fprintf(f, ",%.3f", r.v[c]);
        }
        std::fprintf(f, ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%.5f", r.pipelines, r.pipelineWaits, r.skippedDraws,
                     r.drawCalls, r.copies, r.underruns, r.skippedAudio, r.overruns, r.lateResets, r.correction);
        if (r.targetValid) {
            std::fprintf(f, ",%.4f,%.4f", r.targetX, r.targetY);
        } else {
            std::fprintf(f, ",,");
        }
        if (r.kpadValid) {
            std::fprintf(f, ",%.4f,%.4f\n", r.kpadX, r.kpadY);
        } else {
            std::fprintf(f, ",,\n");
        }
    }
    std::fclose(f);
}

void UpdateOverlayText(const Row& r) {
    // averages over the graph's window
    double sum[C_COUNT] = {};
    size_t n = std::min<size_t>(sCount, kGraph);
    double worst = 0;
    for (size_t i = sCount - n; i < sCount; i++) {
        for (int c = 0; c < C_COUNT; c++) {
            sum[c] += sRows[i].v[c];
        }
        worst = std::max<double>(worst, sRows[i].v[C_WALL]);
    }
    auto avg = [&](int c) { return n ? sum[c] / n : 0.0; };
    std::snprintf(sOverlayText, sizeof sOverlayText,
                  "%s\n"
                  "%.1f fps  frame %.2f ms (worst %.1f)  scene %d\n"
                  "calc %.2f  gx %.2f  end %.2f  gpu %.2f  present %.2f  sleep %.2f\n"
                  "audio %.0f ms queued  underruns %u  skipped %u  corr %+.2f %%\n"
                  "pipelines +%u (%.1f ms)  waits %u  skipped draws %u  draws %u\n"
                  "cursor %s target %.3f,%.3f  kpad %.3f,%.3f\n"
                  "F3 hides",
                  sLabel, avg(C_WALL) > 0 ? 1000.0 / avg(C_WALL) : 0.0, avg(C_WALL), worst, r.scene, avg(C_CALC),
                  avg(C_GX), avg(C_END), avg(C_GPU), avg(C_PRESENT), avg(C_SLEEP), r.v[C_AUDIO_MS], r.underruns,
                  r.skippedAudio, 100.0 * r.correction, r.pipelines, r.v[C_PIPE_MS], r.pipelineWaits, r.skippedDraws,
                  r.drawCalls, r.targetValid ? "" : "(no target)", r.targetX, r.targetY, r.kpadX, r.kpadY);
}

} // namespace

u64 PortPerfNow(void) {
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void PortPerfStart(const char* csvPath, int overlay, const char* label) {
    if (csvPath != nullptr) {
        std::free(sCsvPath);
        sCsvPath = strdup(csvPath);
    }
    if (label != nullptr) {
        std::snprintf(sLabel, sizeof sLabel, "%s", label);
    }
    sOverlay = sOverlay || overlay != 0;
    if (!sActive) {
        sActive = true;
        sStartNs = sLastFrame = PortPerfNow();
        std::memset(sHost, 0, sizeof sHost);
        wpgx_get_perf(&sLastPerf);
    }
}

int PortPerfActive(void) {
    return sActive ? 1 : 0;
}

void PortPerfAdd(int phase, u64 ns) {
    if (sActive && phase >= 0 && phase < PORT_PERF_HOST_PHASES) {
        sHost[phase] += ns;
    }
}

void PortPerfFrame(int frame, int scene, const PortPerfCursor* cursor) {
    if (!sActive) {
        return;
    }
    u64 now = PortPerfNow();
    wpgx_perf p;
    wpgx_get_perf(&p);
    PortLiveStats live{};
    PortLiveGetStats(&live);
    Row r{};
    r.frame = frame;
    r.scene = scene;
    r.v[C_WALL] = static_cast<float>(Ms(now - sLastFrame));
    r.v[C_GX] = static_cast<float>(Ms(sHost[PORT_PERF_GX]));
    r.v[C_SLEEP] = static_cast<float>(Ms(sHost[PORT_PERF_SLEEP]));
    r.v[C_INPUT] = static_cast<float>(Ms(sHost[PORT_PERF_INPUT]));
    r.v[C_END] = static_cast<float>(Ms(p.end_frame_ns - sLastPerf.end_frame_ns));
    r.v[C_GPU] = static_cast<float>(
        Ms((p.writeback_ns - sLastPerf.writeback_ns) + (p.read_frame_ns - sLastPerf.read_frame_ns)));
    r.v[C_PRESENT] = static_cast<float>(Ms(p.begin_frame_ns - sLastPerf.begin_frame_ns));
    float known = r.v[C_GX] + r.v[C_SLEEP] + r.v[C_INPUT] + r.v[C_END] + r.v[C_GPU] + r.v[C_PRESENT];
    r.v[C_CALC] = std::max(0.0f, r.v[C_WALL] - known);
    r.v[C_PIPE_MS] = static_cast<float>(Ms(p.pipeline_compile_ns - sLastPerf.pipeline_compile_ns));
    r.v[C_PIPE_WAIT_MS] = static_cast<float>(Ms(p.pipeline_wait_ns - sLastPerf.pipeline_wait_ns));
    r.v[C_AUDIO_MS] = static_cast<float>(live.queuedFrames / 32.0);
    r.pipelines = p.pipelines_created - sLastPerf.pipelines_created;
    r.pipelineWaits = p.pipeline_waits - sLastPerf.pipeline_waits;
    r.skippedDraws = p.skipped_draws - sLastPerf.skipped_draws;
    r.drawCalls = p.draw_calls;
    r.copies = p.copies_written - sLastPerf.copies_written;
    r.underruns = live.underruns;
    r.skippedAudio = live.skippedFrames;
    r.overruns = live.overruns;
    r.lateResets = live.lateResets;
    r.correction = static_cast<float>(live.correction);
    if (cursor != nullptr) {
        r.targetX = cursor->targetX;
        r.targetY = cursor->targetY;
        r.targetValid = cursor->targetValid != 0;
        r.kpadX = cursor->kpadX;
        r.kpadY = cursor->kpadY;
        r.kpadValid = cursor->kpadValid != 0;
    }
    Push(r);
    sLastPerf = p;
    std::memset(sHost, 0, sizeof sHost);
    sLastFrame = now;
    sGraph[sGraphPos] = r.v[C_WALL];
    sGraphPos = (sGraphPos + 1) % kGraph;
    if (sOverlay) {
        UpdateOverlayText(r);
    }
}

void PortPerfToggleOverlay(void) {
    if (!sActive) {
        PortPerfStart(nullptr, 1, nullptr);
        return;
    }
    sOverlay = !sOverlay;
}

void PortPerfDrawOverlay(void) {
    if (!sActive || !sOverlay || sCount == 0) {
        return;
    }
    float ordered[kGraph];
    for (int i = 0; i < kGraph; i++) {
        ordered[i] = sGraph[(sGraphPos + i) % kGraph];
    }
    wpgx_overlay(sOverlayText, ordered, kGraph, 50.0f, 16.7f);
}

void PortPerfFinish(void) {
    if (!sActive || sCsvPath == nullptr) {
        return;
    }
    WriteCsv(sCsvPath);
    char txt[4096];
    std::snprintf(txt, sizeof txt, "%s", sCsvPath);
    char* dot = std::strrchr(txt, '.');
    char* slash = std::strrchr(txt, '/');
    if (dot != nullptr && (slash == nullptr || dot > slash)) {
        *dot = '\0';
    }
    std::strncat(txt, ".txt", sizeof txt - std::strlen(txt) - 1);
    if (FILE* f = std::fopen(txt, "w")) {
        WriteSummary(f);
        std::fclose(f);
    }
    WriteSummary(stderr);
    std::fprintf(stderr, "perf: per-frame CSV %s\nperf: this summary %s\n", sCsvPath, txt);
}
