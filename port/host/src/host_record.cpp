/**
 * The run recorder and the latency marker (include/port/record.h,
 * port/docs/HOST_BUILD.md §37). Host code; malloc / stdio only.
 */
#include <port/perf.h>
#include <port/platform.h>
#include <port/record.h>

#include "wpgx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

FILE* sFrames = nullptr;
FILE* sInput = nullptr;
char sDir[512];
int sEvery = 0;
u64 sStart = 0;
int sFrame = 0;
bool sMarker = false;
bool sPressed = false;   // an input pressed since the last frame began
bool sMarkerShown = false;
u8* sPixels = nullptr;

void WriteFramePpm(int frame) {
    const size_t size = 640u * 528u * 4u;
    if (sPixels == nullptr) {
        sPixels = static_cast<u8*>(std::malloc(size));
    }
    uint32_t w = 0, h = 0;
    if (sPixels == nullptr || !wpgx_read_frame(sPixels, size, &w, &h) || w < 2 || h < 2) {
        return;
    }
    char path[600];
    std::snprintf(path, sizeof path, "%s/frame_%06d.ppm", sDir, frame);
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", w / 2, h / 2);
    for (uint32_t y = 0; y + 1 < h; y += 2) {
        for (uint32_t x = 0; x + 1 < w; x += 2) {
            u8 rgb[3];
            for (int c = 0; c < 3; c++) {
                const u8* p = sPixels + (static_cast<size_t>(y) * w + x) * 4 + c;
                rgb[c] = static_cast<u8>((p[0] + p[4] + p[w * 4] + p[w * 4 + 4] + 2) / 4);
            }
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
}

} // namespace

extern "C" {

int PortRecordStart(const char* spec, char* err, int errLen) {
    std::snprintf(sDir, sizeof sDir, "%s", spec);
    // DIR[:N]: a trailing colon and digits (a Windows drive's colon is no N)
    char* colon = std::strrchr(sDir, ':');
    if (colon != nullptr && colon[1] != '\0' && std::strspn(colon + 1, "0123456789") == std::strlen(colon + 1)) {
        *colon = '\0';
        sEvery = std::atoi(colon + 1);
    }
    PortMakeDir(sDir);
    char path[600];
    std::snprintf(path, sizeof path, "%s/frames.csv", sDir);
    sFrames = std::fopen(path, "w");
    std::snprintf(path, sizeof path, "%s/input.csv", sDir);
    sInput = std::fopen(path, "w");
    if (sFrames == nullptr || sInput == nullptr) {
        std::snprintf(err, errLen, "cannot write to %s", sDir);
        return -1;
    }
    std::fprintf(sFrames, "frame,wall_ns,game_units,retrace,gpu_latency_ms,in_flight,marker\n");
    std::fprintf(sInput, "wall_ns,frame,event\n");
    sStart = PortPerfNow();
    return 0;
}

void PortRecordStop(void) {
    if (sFrames != nullptr) {
        std::fclose(sFrames);
        sFrames = nullptr;
    }
    if (sInput != nullptr) {
        std::fclose(sInput);
        sInput = nullptr;
    }
}

int PortRecordActive(void) {
    return sFrames != nullptr ? 1 : 0;
}

void PortRecordFrame(int frame, long long gameUnits, u32 retrace) {
    sFrame = frame + 1;
    if (sFrames == nullptr) {
        return;
    }
    wpgx_frame_stats fs{};
    wpgx_get_frame_stats(&fs);
    std::fprintf(sFrames, "%d,%llu,%lld,%u,%.3f,%llu,%d\n", frame,
                 static_cast<unsigned long long>(PortPerfNow() - sStart), gameUnits, retrace,
                 fs.last_gpu_latency_ns / 1e6, static_cast<unsigned long long>(fs.ended - fs.done),
                 sMarkerShown ? 1 : 0);
    if (sEvery > 0 && frame % sEvery == 0) {
        WriteFramePpm(frame);
    }
}

void PortRecordInput(const char* what, int pressed) {
    if (pressed) {
        sPressed = true;
    }
    if (sInput != nullptr) {
        std::fprintf(sInput, "%llu,%d,%s\n", static_cast<unsigned long long>(PortPerfNow() - sStart), sFrame,
                     what);
    }
}

void PortRecordSetMarker(int enabled) {
    sMarker = enabled != 0;
}

void PortRecordDrawMarker(void) {
    sMarkerShown = sMarker && sPressed && wpgx_marker() != 0;
    sPressed = false;
}

} // extern "C"
