/**
 * The host's entry point for the game (port/docs/HOST_BUILD.md §29).
 *
 * Sets up what the Wii's IPL, apploader and OS leave for the game (the arenas, the
 * disc, the NAND, SC), the host devices (the VI clock, scripted Wii Remotes, the
 * GX stream into Aurora) and then calls the decomp's own main (src/main.cpp,
 * compiled as PortGameMain): RPSysSystem::initialize, create, setup and mainLoop,
 * which never returns. The host ends the run from the GX stream: after the frame
 * given with --frames it writes what was asked for and exits.
 *
 * Two modes (§30): headless (the default; tests) runs on the deterministic
 * virtual clock with scripted input and no devices; --window plays: a window,
 * real-time pacing, the audio device and live keyboard / mouse / gamepad input
 * (src/host_live.cpp). Usage() lists the options.
 *
 * Output on stdout, one line per displayed frame (FRAME index, retrace count,
 * game time, scene ID, audio frames, and with --hash an FNV-1a hash of the
 * frame), SCENE lines when the current scene changes, and an END line.
 *
 * Host code here allocates with malloc: operator new is the game's (EGG::Heap).
 */

#include <types.h>

#include <Pack/RPKernel/RPSysSystem.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>

#include <revolution/GX.h>
#include <revolution/NAND.h>
#include <revolution/OS.h>
#include <revolution/SC.h>
#include <revolution/WPAD.h>

#include <port/arena.h>
#include <port/gxsink.h>
#include <port/host.h>
#include <port/input.h>
#include <port/live.h>
#include <port/perf.h>
#include <egg/core/eggController.h>

#include "wpgx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <cerrno>
#include <ctime>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

extern int PortGameMain(int argc, char** argv);
extern "C" u8 Health_strapArchive[];
// Minigame state for the FRAME line (host_probe.cpp)
extern "C" int PortGameProbe(char* buf, int len);
extern "C" int PortHomeProbe(char* buf, int len); // host_probe_hbm.cpp

namespace {

// RPSysRenderMode's NTSC modes: a 640x456 EFB
const u32 EFB_WIDTH = 640;
const u32 EFB_HEIGHT = 456;
// Health_strapArchive: the start of main.dol's .data6 (the archive fills the section)
const u32 STRAP_ADDRESS = 0x802E9E60;

struct Save {
    int frame;
    const char* path;
};
const int MAX_SAVES = 256;

struct Options {
    const char* disc = nullptr;
    const char* nand = nullptr;
    const char* sc = nullptr;
    const char* dataDir = nullptr;
    const char* input = nullptr;
    const char* audio = nullptr;
    const char* config = nullptr;
    const char* events = nullptr;
    int frames = -1; // -1: 600 headless, unlimited with a window
    int logLevel = 2;
    int scale = 2;
    float speed = 1.0f;
    bool hash = false;
    bool window = false;
    bool fullscreen = false;
    bool vsync = true;
    int realtime = -1;  // -1: with --window
    int audioOut = -1;  // -1: with --window
    int liveInput = -1; // -1: with --window
    bool trace = false;
    // Performance (HOST_BUILD.md §35)
    bool perf = false;
    const char* perfOut = nullptr;
    const char* gpu = nullptr;
    int liveGfx = -1;        // -1: with --window: disk pipeline cache, no CPU adapter, mailbox
    int blockingPipes = -1;  // -1: the mode's default
    int mailbox = -1;        // -1: with --realtime
    int writeback = -1;      // copy writeback (§28): 0 off, 1 on; -1: off with --live-gfx
    // --dump-copies FIRST:LAST:DIR (debugging): every EFB copy of those frames
    int dumpFirst = -1, dumpLast = -1;
    const char* dumpDir = nullptr;
    Save saves[MAX_SAVES];
    int saveCount = 0;
} gOpt;

PortInputScript* gScript = nullptr;
PortInputConfig gInputConfig;
PortInputState gInputStates[WPAD_MAX_CONTROLLERS];
FILE* gAudio = nullptr;
int gFrame = 0; // displayed frames (GXCopyDisp)
s32 gLastScene = -2;
bool gLive = false; // any live device: print the LIVE line at the end

[[noreturn]] void Die(const char* what, const char* detail = "") {
    std::fflush(stdout);
    std::fprintf(stderr, "boot: %s%s\n", what, detail);
    std::exit(1);
}

u32 Be32(const u8* p) {
    return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 |
           static_cast<u32>(p[2]) << 8 | p[3];
}

// Copies the DOL section that holds `address`, from there to the section end.
u32 ReadDolData(const char* dolPath, u32 address, u8* dst, u32 max) {
    FILE* f = std::fopen(dolPath, "rb");
    if (f == nullptr) {
        Die("cannot open ", dolPath);
    }
    u8 header[0x100];
    if (std::fread(header, 1, sizeof header, f) != sizeof header) {
        Die("main.dol is too short");
    }
    for (u32 i = 0; i < 18; i++) {
        u32 offset = Be32(&header[i * 4]);
        u32 start = Be32(&header[0x48 + i * 4]);
        u32 size = Be32(&header[0x90 + i * 4]);
        if (address >= start && address - start < size) {
            u32 count = size - (address - start);
            if (count > max) {
                Die("main.dol's .data6 is larger than expected");
            }
            if (std::fseek(f, offset + (address - start), SEEK_SET) != 0 ||
                std::fread(dst, 1, count, f) != count) {
                Die("cannot read main.dol");
            }
            std::fclose(f);
            return count;
        }
    }
    Die("address not in main.dol");
}

// --- GX: the stream into Aurora ------------------------------------------------

u64 Fnv1a(const u8* p, size_t n) {
    u64 h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

s32 CurrentScene() {
    RPSysSceneMgr* mgr = RPSysSceneMgr::instance();
    return mgr != NULL ? mgr->getCurrentSceneID() : -1;
}

// The end of the run: --frames reached, the window closed, or the game left
// through OSReturnToMenu / OSShutdownSystem (`exit=` names which: the HOME
// Menu's "Wii Menu" ends the program here, §34)
[[noreturn]] void Finish(const char* exitReason = nullptr) {
    std::printf("END\tframes=%d\tretrace=%d\tscene=%d\taudio=%llu\tgx=%llu", gFrame,
                static_cast<int>(VIGetRetraceCount()), static_cast<int>(CurrentScene()), PortVIAudioFrames(),
                static_cast<unsigned long long>(PortGXStreamBytes()));
    if (exitReason != nullptr) {
        std::printf("\texit=%s", exitReason);
    }
    std::printf("\n");
    if (gLive) {
        PortLiveStats st;
        PortLiveGetStats(&st);
        std::printf("LIVE\twall=%.3f\tlate=%u\tunderruns=%u\toverruns=%u\tqueued=%u\tevents=%u\trumble=%u"
                    "\ttestrumble=%u\taudiodriver=%s\n",
                    st.wallSeconds, st.lateResets, st.underruns, st.overruns, st.queuedFrames, st.events,
                    st.rumbleOn, st.testRumbles, gOpt.audioOut == 1 ? wpgx_audio_driver() : "none");
        PortLiveShutdown();
    }
    PortPerfFinish();
    std::fflush(stdout);
    if (gAudio != nullptr) {
        std::fclose(gAudio);
    }
    PortGXSetSink(nullptr);
    wpgx_shutdown();
    std::fflush(stdout);
    std::_Exit(0);
}

// The next Aurora frame. A window may refuse one for a while (minimised, a
// surface being re-created): keep the window system running and retry.
void BeginFrame() {
    for (int tries = 0; !wpgx_begin_frame(); tries++) {
        if (!gOpt.window || tries > 3000) {
            Die("Aurora skipped a frame");
        }
        if (gOpt.liveInput == 1 && PortLivePoll(gInputStates)) {
            Finish();
        }
        usleep(10000);
    }
}

void PrintFrame(s32 scene, u64 hash);

// Remote 0's pointer: the target the input layer gives (the mouse, a script)
// against the KPAD position the game read this frame (its first sample).
void CursorSample(PortPerfCursor* c) {
    const PortInputState& st = gInputStates[0];
    c->targetX = st.pointerX;
    c->targetY = st.pointerY;
    c->targetValid = st.pointerValid && st.connected;
    c->kpadX = c->kpadY = 0;
    c->kpadValid = 0;
    EGG::CoreControllerMgr* mgr = EGG::CoreControllerMgr::instance();
    EGG::CoreController* ctrl = mgr != NULL ? mgr->getNthController(0) : NULL;
    if (ctrl != NULL) {
        EGG::CoreStatus* cs = ctrl->getCoreStatus();
        c->kpadX = cs->pos.x;
        c->kpadY = cs->pos.y;
        c->kpadValid = cs->dpd_valid_fg > 0;
    }
}

void FrameDone() {
    bool wantSave = false;
    for (int i = 0; i < gOpt.saveCount; i++) {
        wantSave = wantSave || gOpt.saves[i].frame == gFrame;
    }
    static u8 rgba[EFB_WIDTH * EFB_HEIGHT * 4];
    u64 hash = 0;
    if (gOpt.hash || wantSave) {
        u32 w = 0, h = 0;
        if (wpgx_read_frame(rgba, sizeof rgba, &w, &h) != 0 || w != EFB_WIDTH || h != EFB_HEIGHT) {
            Die("wpgx_read_frame failed");
        }
        hash = Fnv1a(rgba, sizeof rgba);
    }
    for (int i = 0; i < gOpt.saveCount; i++) {
        const Save& s = gOpt.saves[i];
        if (s.frame != gFrame) {
            continue;
        }
        FILE* f = std::fopen(s.path, "wb");
        if (f == nullptr || std::fwrite(rgba, 1, sizeof rgba, f) != sizeof rgba) {
            Die("cannot write ", s.path);
        }
        std::fclose(f);
    }
    s32 scene = CurrentScene();
    // A window prints no FRAME lines (a terminal at 60 lines a second) unless
    // traced or scripted (the probes in them drive scripted players)
    if (!gOpt.window || gOpt.trace || gScript != nullptr) {
        PrintFrame(scene, hash);
    }
    gFrame++;
    if (gOpt.frames > 0 && gFrame >= gOpt.frames) {
        Finish();
    }
    BeginFrame();
    if (PortPerfActive()) {
        PortPerfCursor cursor;
        CursorSample(&cursor);
        PortPerfFrame(gFrame - 1, static_cast<int>(scene), &cursor);
    }
}

void PrintFrame(s32 scene, u64 hash) {
    std::printf("FRAME\t%d\tretrace=%d\ttime=%lld\tscene=%d\taudio=%llu", gFrame,
                static_cast<int>(VIGetRetraceCount()), PortVIGameTimeUnits(), static_cast<int>(scene),
                PortVIAudioFrames());
    if (gOpt.hash) {
        std::printf("\thash=%016llx", static_cast<unsigned long long>(hash));
    }
    char game[64];
    if (PortGameProbe(game, sizeof game)) {
        std::printf("\tgame=%s", game);
    }
    char home[96];
    if (PortHomeProbe(home, sizeof home)) {
        std::printf("\thome=%s", home);
    }
    std::printf("\n");
}

// --dump-copies: the copy as written to memory and the EFB before it (raw files;
// the name has the frame, the copy's index, address, format, size and source)
void DumpCopy(const wpgx_copy_info* c, void*) {
    static int sIndex = 0;
    if (gFrame < gOpt.dumpFirst || gFrame > gOpt.dumpLast) {
        return;
    }
    char path[512];
    std::snprintf(path, sizeof path, "%s/copy_%05d_%03d_%08x_f%u_%ux%u_src%u,%u,%ux%u.bin", gOpt.dumpDir, gFrame,
                  sIndex++ % 1000, c->address, c->format, c->width, c->height, c->src_left, c->src_top,
                  c->src_width, c->src_height);
    if (FILE* f = std::fopen(path, "wb")) {
        std::fwrite(c->data, 1, c->size, f);
        std::fclose(f);
    }
    if (c->efb != nullptr) {
        std::snprintf(path, sizeof path, "%s/efb_%05d_%ux%u.rgba", gOpt.dumpDir, gFrame, c->efb_width,
                      c->efb_height);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(c->efb, 4, static_cast<size_t>(c->efb_width) * c->efb_height, f);
            std::fclose(f);
        }
    }
}

void SinkWrite(const void* data, u32 size, void*) {
    u64 start = PortPerfNow();
    wpgx_fifo_write(data, size);
    PortPerfAdd(PORT_PERF_GX, PortPerfNow() - start);
}

void SinkSync(u32 kind, void*) {
    u64 start = PortPerfNow();
    switch (kind) {
    case PORT_GX_SYNC_FLUSH:
        wpgx_fifo_sync(WPGX_SYNC_FLUSH);
        PortPerfAdd(PORT_PERF_GX, PortPerfNow() - start);
        break;
    case PORT_GX_SYNC_DRAWDONE:
        wpgx_fifo_sync(WPGX_SYNC_DRAWDONE);
        PortPerfAdd(PORT_PERF_GX, PortPerfNow() - start);
        break;
    case PORT_GX_SYNC_DISPLAY:
        PortPerfDrawOverlay();
        wpgx_end_frame();
        FrameDone();
        break;
    }
}

const PortGXSink kSink = {SinkWrite, SinkSync, nullptr};

// Wii addresses in the command stream -> host memory (MEM1 above the reserved
// low block, MEM2)
const void* Resolve(uint32_t address, uint32_t* size, void*) {
    u32 phys = address & 0x3FFFFFFF;
    if (phys >= PORT_MEM1_OFFSET + PORT_MEM1_RESERVED && phys < PORT_MEM1_OFFSET + PORT_MEM1_SIZE) {
        *size = PORT_MEM1_OFFSET + PORT_MEM1_SIZE - phys;
        return port_arena_base + phys;
    }
    if (phys >= PORT_MEM2_OFFSET && phys < PORT_MEM2_OFFSET + PORT_MEM2_SIZE) {
        *size = PORT_MEM2_OFFSET + PORT_MEM2_SIZE - phys;
        return port_arena_base + phys;
    }
    return nullptr;
}

// --- Retrace: scripted input ---------------------------------------------------

void OnRetrace(u32 retraceCount, void*) {
    if (gScript != nullptr) {
        PortInputScriptApply(gScript, static_cast<int>(retraceCount), gInputStates);
    }
    if (gOpt.liveInput == 1) {
        if (gOpt.events != nullptr) {
            PortLivePushEvents(retraceCount);
        }
        if (PortLivePoll(gInputStates)) {
            Finish();
        }
        if (gOpt.trace) {
            static PortInputState sLast;
            const PortInputState& st = gInputStates[0];
            if (std::memcmp(&st, &sLast, sizeof st) != 0) {
                std::printf("INPUT\tretrace=%u\tbuttons=%04x\tpointer=%.3f,%.3f\tvalid=%d\troll=%.1f\tpitch=%.1f\n",
                            retraceCount, st.buttons, st.pointerX, st.pointerY, st.pointerValid, st.roll,
                            st.pitch);
                sLast = st;
            }
        }
    }
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        PortWPADRemote remote;
        PortInputToRemote(&gInputConfig, &gInputStates[chan], &remote);
        PortWPADSetRemote(chan, &remote);
    }
    PortWPADRunFrame();
    if (gOpt.trace) {
        s32 scene = CurrentScene();
        if (scene != gLastScene) {
            std::printf("SCENE\tretrace=%u\tscene=%d\n", retraceCount, static_cast<int>(scene));
            gLastScene = scene;
        }
    }
}

void OnAudio(const short* samples, u32 frames, void*) {
    if (gAudio != nullptr) {
        std::fwrite(samples, sizeof(short) * 2, frames, gAudio);
    }
    PortLiveAudioFrame(samples, frames);
}

void Usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --disc DIR [options]\n"
                 "  --disc DIR          the extracted disc (its root with files/ and sys/, or files/)\n"
                 "  --nand DIR          saves (default: $XDG_DATA_HOME/wiiplay/nand)\n"
                 "  --data-dir DIR      Aurora's data and caches (default: $XDG_DATA_HOME/wiiplay/aurora)\n"
                 "  --sc FILE           console settings (language, aspect 4:3/16:9, sensor bar, ...)\n"
                 "  --config FILE       input bindings and settings (default: $XDG_CONFIG_HOME/wiiplay/input.cfg)\n"
                 "  --window            play: a window, real time, sound, keyboard / mouse / gamepads\n"
                 "  --headless          no window, virtual time, scripted input (the default; tests)\n"
                 "  --fullscreen        start fullscreen (F11 or Alt+Enter toggles)\n"
                 "  --scale N           window size N x the game's 640x456 (default 2)\n"
                 "  --no-vsync          present without waiting for the display\n"
                 "  --realtime | --no-realtime, --audio-out | --no-audio, --live-input\n"
                 "                      the live parts on their own (default: on with --window)\n"
                 "  --speed F           game speed with --realtime (1 = real time)\n"
                 "  --frames N          stop after N frames (default: 600 headless, none with a window)\n"
                 "  --input SCRIPT      scripted remotes (tests)   --events SCRIPT  synthetic SDL events (tests)\n"
                 "  --save FRAME:PATH.rgba, --hash, --audio PATH.raw, --trace, --log-level N\n"
                 "  --perf              per-frame timings: a CSV and a summary at exit (F3: overlay)\n"
                 "  --perf-out FILE.csv where (default: $XDG_STATE_HOME/wiiplay/perf-TIME.csv)\n"
                 "  --gpu NAME|N|list   graphics adapter: part of its name, its index, discrete,\n"
                 "                      integrated or cpu (default: $WIIPLAY_GPU, else the discrete GPU)\n"
                 "  --live-gfx | --test-gfx  graphics settings of the window (pipeline cache on disk,\n"
                 "                      no CPU adapter) or of the tests (default: by mode)\n"
                 "  --async-pipelines | --blocking-pipelines, --mailbox | --fifo,\n"
                 "  --writeback on|off  EFB texture copies written back to memory (default: on, off with\n"
                 "                      --live-gfx)   (HOST_BUILD.md §35)\n",
                 argv0);
    std::exit(2);
}

bool IsDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void MakeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            std::string sub = path.substr(0, i);
            if (mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                Die("cannot create ", sub.c_str());
            }
        }
    }
}

std::string RealPath(const std::string& path) {
    char buf[4096]; // PATH_MAX (the decomp's limits.h shadows the system one)
    if (realpath(path.c_str(), buf) == nullptr) {
        return path;
    }
    return buf;
}

std::string UserDir(const char* xdgVar, const char* fallback) {
    const char* xdg = std::getenv(xdgVar);
    if (xdg != nullptr && xdg[0] == '/') {
        return std::string(xdg) + "/wiiplay";
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        Die("HOME is not set: give --nand and --data-dir");
    }
    return std::string(home) + fallback + "/wiiplay";
}

// The source tree this executable was built in (it lives in
// port/host/build/host64/), so that saves never land inside the repository.
std::string RepoRoot() {
    std::string dir = RealPath("/proc/self/exe");
    for (int up = 0; up < 6; up++) {
        size_t slash = dir.rfind('/');
        if (slash == std::string::npos || slash == 0) {
            break;
        }
        dir.resize(slash);
        struct stat st;
        if (stat((dir + "/port/host/Makefile").c_str(), &st) == 0) {
            return dir;
        }
    }
    return "";
}

bool Inside(const std::string& path, const std::string& dir) {
    return !dir.empty() && (path == dir || path.compare(0, dir.size() + 1, dir + "/") == 0);
}

// Writable locations must be outside the repository and the disc (read-only
// user data). The directory is created first so that realpath resolves it.
void CheckWritable(const char* what, const std::string& path, bool isDir, const std::string& disc) {
    std::string dir = path;
    if (!isDir) {
        size_t slash = dir.rfind('/');
        dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : dir.substr(0, slash));
    } else {
        MakeDirs(path);
    }
    std::string real = RealPath(dir);
    static const std::string root = RepoRoot();
    if (Inside(real, root)) {
        std::fprintf(stderr, "boot: %s %s is inside the source tree; use a directory outside it\n", what,
                     path.c_str());
        std::exit(2);
    }
    if (Inside(real, disc)) {
        std::fprintf(stderr, "boot: %s %s is inside the disc directory (read only)\n", what, path.c_str());
        std::exit(2);
    }
}

} // namespace

// The game left: OSReturnToMenu ("menu": the HOME Menu's "Wii Menu") or
// OSShutdownSystem ("shutdown"), both after the game's own shutdown
// (RPSysSystem::systemShutDown: saves finished, sound faded, remotes off).
// The program ends as at --frames: the END line, the window and audio closed.
extern "C" [[noreturn]] void PortBootExit(const char* reason) {
    Finish(reason);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        auto is = [arg](const char* opt) { return std::strcmp(arg, opt) == 0; };
        bool more = i + 1 < argc;
        if (is("--disc") && more) {
            gOpt.disc = argv[++i];
        } else if (is("--nand") && more) {
            gOpt.nand = argv[++i];
        } else if (is("--sc") && more) {
            gOpt.sc = argv[++i];
        } else if (is("--data-dir") && more) {
            gOpt.dataDir = argv[++i];
        } else if (is("--frames") && more) {
            gOpt.frames = std::atoi(argv[++i]);
        } else if (is("--input") && more) {
            gOpt.input = argv[++i];
        } else if (is("--audio") && more) {
            gOpt.audio = argv[++i];
        } else if (is("--save") && more) {
            const char* v = argv[++i];
            const char* colon = std::strchr(v, ':');
            if (colon == nullptr || gOpt.saveCount == MAX_SAVES) {
                Usage(argv[0]);
            }
            gOpt.saves[gOpt.saveCount++] = {std::atoi(v), colon + 1};
        } else if (is("--dump-copies") && more) {
            const char* v = argv[++i];
            const char* c1 = std::strchr(v, ':');
            const char* c2 = c1 != nullptr ? std::strchr(c1 + 1, ':') : nullptr;
            if (c2 == nullptr) {
                Usage(argv[0]);
            }
            gOpt.dumpFirst = std::atoi(v);
            gOpt.dumpLast = std::atoi(c1 + 1);
            gOpt.dumpDir = c2 + 1;
        } else if (is("--hash")) {
            gOpt.hash = true;
        } else if (is("--window")) {
            gOpt.window = true;
        } else if (is("--headless")) {
            gOpt.window = false;
        } else if (is("--fullscreen")) {
            gOpt.fullscreen = true;
        } else if (is("--no-vsync")) {
            gOpt.vsync = false;
        } else if (is("--scale") && more) {
            gOpt.scale = std::atoi(argv[++i]);
            if (gOpt.scale < 1 || gOpt.scale > 8) {
                Usage(argv[0]);
            }
        } else if (is("--config") && more) {
            gOpt.config = argv[++i];
        } else if (is("--events") && more) {
            gOpt.events = argv[++i];
        } else if (is("--speed") && more) {
            gOpt.speed = static_cast<float>(std::atof(argv[++i]));
        } else if (is("--realtime")) {
            gOpt.realtime = 1;
        } else if (is("--no-realtime")) {
            gOpt.realtime = 0;
        } else if (is("--audio-out")) {
            gOpt.audioOut = 1;
        } else if (is("--no-audio")) {
            gOpt.audioOut = 0;
        } else if (is("--live-input")) {
            gOpt.liveInput = 1;
        } else if (is("--trace")) {
            gOpt.trace = true;
        } else if (is("--perf")) {
            gOpt.perf = true;
        } else if (is("--perf-out") && more) {
            gOpt.perf = true;
            gOpt.perfOut = argv[++i];
        } else if (is("--gpu") && more) {
            gOpt.gpu = argv[++i];
        } else if (is("--live-gfx")) {
            gOpt.liveGfx = 1;
        } else if (is("--test-gfx")) {
            gOpt.liveGfx = 0;
        } else if (is("--async-pipelines")) {
            gOpt.blockingPipes = 0;
        } else if (is("--blocking-pipelines")) {
            gOpt.blockingPipes = 1;
        } else if (is("--writeback") && more) {
            const char* v = argv[++i];
            gOpt.writeback = std::strcmp(v, "off") == 0 ? 0 : std::strcmp(v, "on") == 0 ? 1 : -2;
            if (gOpt.writeback == -2) {
                Usage(argv[0]);
            }
        } else if (is("--mailbox")) {
            gOpt.mailbox = 1;
        } else if (is("--fifo")) {
            gOpt.mailbox = 0;
        } else if (is("--log-level") && more) {
            gOpt.logLevel = std::atoi(argv[++i]);
        } else {
            Usage(argv[0]);
        }
    }
    if (gOpt.disc == nullptr) {
        Usage(argv[0]);
    }
    auto byMode = [](int v) { return v < 0 ? (gOpt.window ? 1 : 0) : v; };
    gOpt.realtime = byMode(gOpt.realtime);
    gOpt.audioOut = byMode(gOpt.audioOut);
    gOpt.liveInput = byMode(gOpt.liveInput);
    if (gOpt.events != nullptr) {
        gOpt.liveInput = 1;
    }
    if (gOpt.frames < 0) {
        gOpt.frames = gOpt.window ? 0 : 600;
    }
    gLive = gOpt.realtime || gOpt.audioOut || gOpt.liveInput;
    // Graphics (§35): the window's settings (pipelines cached on disk, a real
    // GPU, mailbox presentation under the pacer) or the tests' (no cache,
    // every draw waits for its pipeline, any adapter): deterministic output.
    gOpt.liveGfx = gOpt.liveGfx < 0 ? (gOpt.window ? 1 : 0) : gOpt.liveGfx;
    gOpt.blockingPipes = gOpt.blockingPipes < 0 ? 1 : gOpt.blockingPipes;
    gOpt.mailbox = gOpt.mailbox < 0 ? (gOpt.realtime ? 1 : 0) : gOpt.mailbox;
    // Texture copies stay on the GPU in the window (§35: the readback cost up to 20 ms a frame,
    // and no CPU reader of a copy changes any frame of the menus or the nine games)
    gOpt.writeback = gOpt.writeback < 0 ? (gOpt.liveGfx ? 0 : 1) : gOpt.writeback;
    if (gOpt.gpu == nullptr) {
        const char* env = std::getenv("WIIPLAY_GPU");
        gOpt.gpu = env != nullptr && env[0] != '\0' ? env : nullptr;
    }

    // The disc: the extracted root (files/, sys/) or its files/ directory
    static std::string discFiles, discRoot, nandDir, dataDir;
    discFiles = gOpt.disc;
    if (IsDir(discFiles + "/files")) {
        discFiles += "/files";
    }
    discRoot = RealPath(discFiles + "/..");
    gOpt.disc = discFiles.c_str();
    // Saves and caches: outside the source tree and the disc
    nandDir = gOpt.nand != nullptr ? gOpt.nand : UserDir("XDG_DATA_HOME", "/.local/share") + "/nand";
    dataDir = gOpt.dataDir != nullptr ? gOpt.dataDir : UserDir("XDG_DATA_HOME", "/.local/share") + "/aurora";
    CheckWritable("the NAND directory", nandDir, true, discRoot);
    CheckWritable("the data directory", dataDir, true, discRoot);
    gOpt.nand = nandDir.c_str();
    gOpt.dataDir = dataDir.c_str();
    for (int i = 0; i < gOpt.saveCount; i++) {
        CheckWritable("the frame file", gOpt.saves[i].path, false, discRoot);
    }
    if (gOpt.audio != nullptr) {
        CheckWritable("the audio file", gOpt.audio, false, discRoot);
    }
    static std::string perfPath;
    if (gOpt.perf) {
        if (gOpt.perfOut != nullptr) {
            perfPath = gOpt.perfOut;
        } else {
            char stamp[32];
            time_t now = time(nullptr);
            struct tm tmNow;
            localtime_r(&now, &tmNow);
            strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmNow);
            perfPath = UserDir("XDG_STATE_HOME", "/.local/state") + "/perf-" + stamp + ".csv";
        }
        size_t slash = perfPath.rfind('/');
        if (slash != std::string::npos && slash > 0) {
            MakeDirs(perfPath.substr(0, slash));
        }
        CheckWritable("the perf file", perfPath, false, discRoot);
    }
    static std::string configPath;
    if (gOpt.config == nullptr) {
        configPath = UserDir("XDG_CONFIG_HOME", "/.config") + "/input.cfg";
        struct stat st;
        if (stat(configPath.c_str(), &st) == 0) {
            gOpt.config = configPath.c_str();
        }
    }
    {
        char err[256];
        if (PortLiveLoadConfig(gOpt.config, err, sizeof err) != 0) {
            Die("config: ", err);
        }
    }

    // What the IPL / apploader / OSInit leave behind, then the platform (§22)
    PortOSInit();
    if (PortDVDSetDisc(gOpt.disc, nullptr) != 0) {
        Die("cannot use the disc at ", gOpt.disc);
    }
    if (PortNANDSetRoot(gOpt.nand) != 0) {
        Die("cannot use the NAND directory ", gOpt.nand);
    }
    if (PortSCLoadConfig(gOpt.sc) != 0) {
        Die("bad SC config ", gOpt.sc != nullptr ? gOpt.sc : "");
    }
    // OSInit's part: the NAND and SC libraries are up before main
    NANDInit();
    SCInit();
    char dolPath[4096];
    std::snprintf(dolPath, sizeof dolPath, "%s/../sys/main.dol", gOpt.disc);
    ReadDolData(dolPath, STRAP_ADDRESS, Health_strapArchive, PORT_STRAP_ARCHIVE_MAX);

    // Input (§23): four remotes, scripted
    PortInputDefaultConfig(&gInputConfig);
    for (int i = 0; i < WPAD_MAX_CONTROLLERS; i++) {
        PortInputDefaultState(&gInputStates[i]);
    }
    if (gOpt.input != nullptr) {
        char err[256];
        gScript = PortInputScriptLoad(gOpt.input, err, sizeof err);
        if (gScript == nullptr) {
            Die("input script: ", err);
        }
    }
    if (gOpt.audio != nullptr) {
        gAudio = std::fopen(gOpt.audio, "wb");
        if (gAudio == nullptr) {
            Die("cannot write ", gOpt.audio);
        }
    }

    if (gOpt.events != nullptr) {
        char err[256];
        if (PortLiveLoadEvents(gOpt.events, err, sizeof err) != 0) {
            Die("events: ", err);
        }
    }

    // The VI clock: retraces, AX frames, alarms
    PortVIInitClock();
    PortVISetRetraceHook(OnRetrace, nullptr);
    PortVISetAudioSink(OnAudio, nullptr);
    if (gOpt.realtime) {
        PortLiveStartPacing(gOpt.speed);
    }

    // GX on Aurora (§25-28): before GXInit, which EGG::GraphicsFifo calls
    wpgx_config cfg{};
    cfg.width = EFB_WIDTH;
    cfg.height = EFB_HEIGHT;
    cfg.headless = gOpt.window ? 0 : 1;
    // The TV's aspect (SC): 4:3 shows the 640x456 frame with square pixels,
    // 16:9 stretches it by 4/3 (anamorphic); the window starts at that shape.
    const bool wide = SCGetAspectRatio() == SC_ASPECT_WIDE;
    const float aspect = wide ? EFB_WIDTH * 4.0f / (EFB_HEIGHT * 3.0f) : 0.0f;
    cfg.window_width = static_cast<u32>((wide ? EFB_HEIGHT * aspect : EFB_WIDTH) * gOpt.scale + 0.5f);
    cfg.window_height = EFB_HEIGHT * gOpt.scale;
    cfg.fullscreen = gOpt.fullscreen ? 1 : 0;
    cfg.vsync = gOpt.vsync ? 1 : 0;
    cfg.title = "Wii Play";
    cfg.data_dir = gOpt.dataDir;
    cfg.disk_cache = gOpt.liveGfx ? 1 : 0;
    cfg.blocking_pipelines = gOpt.blockingPipes;
    const bool wantCpu = gOpt.gpu != nullptr && strcasecmp(gOpt.gpu, "cpu") == 0;
    cfg.allow_cpu_adapter = gOpt.liveGfx && !wantCpu ? 0 : 1;
    const bool listGpus = gOpt.gpu != nullptr && std::strcmp(gOpt.gpu, "list") == 0;
    cfg.adapter = listGpus ? nullptr : gOpt.gpu;
    cfg.vsync_mailbox = gOpt.mailbox;
    cfg.log_level = gOpt.logLevel;
    if (wpgx_init(&cfg) != 0) {
        Die("wpgx_init failed (no usable graphics adapter? try --gpu list, --gpu cpu)");
    }
    if (listGpus) {
        std::printf("%s", wpgx_adapter_list());
        wpgx_shutdown();
        return 0;
    }
    wpgx_set_display_aspect(aspect);
    if (gOpt.audioOut && PortLiveStartAudio() != 0) {
        std::fprintf(stderr, "boot: no audio device; continuing without sound\n");
        gOpt.audioOut = 0;
    }
    if (gOpt.liveInput && PortLiveStartInput() != 0) {
        Die("cannot open the input devices");
    }
    wpgx_set_address_resolver(Resolve, nullptr);
    wpgx_set_copy_writeback(gOpt.writeback != 0, gOpt.dumpDir != nullptr, gOpt.dumpDir != nullptr ? DumpCopy : nullptr,
                            nullptr);
    PortGXSetSink(&kSink);
    BeginFrame();
    std::printf("BOOT\tadapter=%s\tefb=%ux%u\n", wpgx_adapter_name(), EFB_WIDTH, EFB_HEIGHT);
    std::fflush(stdout);
    char gfxLabel[512];
    std::snprintf(gfxLabel, sizeof gfxLabel,
                  "gpu: %s (%s, %s); present %s%s; pipelines %s, disk cache %s; copy writeback %s; %s, %s, audio %s",
                  wpgx_adapter_name(), wpgx_adapter_type(), wpgx_backend_name(), wpgx_present_mode(),
                  gOpt.window ? (gOpt.vsync ? " (vsync)" : " (no vsync)") : "", gOpt.blockingPipes ? "blocking" : "async",
                  cfg.disk_cache ? "on" : "off",
                  gOpt.writeback ? "on" : "off", gOpt.window ? "window" : "headless",
                  gOpt.realtime ? "real time" : "virtual time", gOpt.audioOut ? wpgx_audio_driver() : "off");
    if (gOpt.window || gOpt.perf) {
        std::fprintf(stderr, "boot: %s\n", gfxLabel);
    }
    if (std::strcmp(wpgx_adapter_type(), "cpu") == 0 && gOpt.window) {
        std::fprintf(stderr, "boot: warning: rendering on the CPU (software); see --gpu list\n");
    }
    if (gOpt.perf || gOpt.window) {
        // the window always tracks (F3 shows it); --perf also writes the files
        PortPerfStart(gOpt.perf ? perfPath.c_str() : nullptr, 0, gfxLabel);
    }

    char name[] = "main.dol";
    char* gameArgv[] = {name, nullptr};
    return PortGameMain(1, gameArgv);
}
