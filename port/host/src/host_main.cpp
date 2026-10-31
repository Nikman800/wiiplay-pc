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
 *   boot --disc FILES --nand DIR --data-dir DIR [--sc FILE] [--frames N]
 *        [--input SCRIPT] [--save FRAME:PATH.rgba]... [--hash] [--audio PATH.raw]
 *        [--window] [--log-level N] [--trace]
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

#include "wpgx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern int PortGameMain(int argc, char** argv);
extern "C" u8 Health_strapArchive[];

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
const int MAX_SAVES = 16;

struct Options {
    const char* disc = nullptr;
    const char* nand = nullptr;
    const char* sc = nullptr;
    const char* dataDir = nullptr;
    const char* input = nullptr;
    const char* audio = nullptr;
    int frames = 600;
    int logLevel = 2;
    bool hash = false;
    bool window = false;
    bool trace = false;
    Save saves[MAX_SAVES];
    int saveCount = 0;
} gOpt;

PortInputScript* gScript = nullptr;
PortInputConfig gInputConfig;
PortInputState gInputStates[WPAD_MAX_CONTROLLERS];
FILE* gAudio = nullptr;
int gFrame = 0; // displayed frames (GXCopyDisp)
s32 gLastScene = -2;

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
    std::printf("FRAME\t%d\tretrace=%d\ttime=%lld\tscene=%d\taudio=%llu", gFrame,
                static_cast<int>(VIGetRetraceCount()), PortVIGameTimeUnits(), static_cast<int>(scene),
                PortVIAudioFrames());
    if (gOpt.hash) {
        std::printf("\thash=%016llx", static_cast<unsigned long long>(hash));
    }
    std::printf("\n");
    gFrame++;
    if (gFrame >= gOpt.frames) {
        std::printf("END\tframes=%d\tretrace=%d\tscene=%d\taudio=%llu\tgx=%llu\n", gFrame,
                    static_cast<int>(VIGetRetraceCount()), static_cast<int>(scene), PortVIAudioFrames(),
                    static_cast<unsigned long long>(PortGXStreamBytes()));
        std::fflush(stdout);
        if (gAudio != nullptr) {
            std::fclose(gAudio);
        }
        PortGXSetSink(nullptr);
        wpgx_shutdown();
        std::fflush(stdout);
        std::_Exit(0);
    }
    if (!wpgx_begin_frame()) {
        Die("Aurora skipped a frame");
    }
}

void SinkWrite(const void* data, u32 size, void*) {
    wpgx_fifo_write(data, size);
}

void SinkSync(u32 kind, void*) {
    switch (kind) {
    case PORT_GX_SYNC_FLUSH:
        wpgx_fifo_sync(WPGX_SYNC_FLUSH);
        break;
    case PORT_GX_SYNC_DRAWDONE:
        wpgx_fifo_sync(WPGX_SYNC_DRAWDONE);
        break;
    case PORT_GX_SYNC_DISPLAY:
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
}

void Usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --disc FILES --nand DIR --data-dir DIR [--sc FILE] [--frames N]\n"
                 "          [--input SCRIPT] [--save FRAME:PATH.rgba]... [--hash] [--audio PATH.raw]\n"
                 "          [--window] [--log-level N] [--trace]\n",
                 argv0);
    std::exit(2);
}

} // namespace

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
        } else if (is("--hash")) {
            gOpt.hash = true;
        } else if (is("--window")) {
            gOpt.window = true;
        } else if (is("--trace")) {
            gOpt.trace = true;
        } else if (is("--log-level") && more) {
            gOpt.logLevel = std::atoi(argv[++i]);
        } else {
            Usage(argv[0]);
        }
    }
    if (gOpt.disc == nullptr || gOpt.nand == nullptr || gOpt.dataDir == nullptr) {
        Usage(argv[0]);
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

    // The VI clock: retraces, AX frames, alarms
    PortVIInitClock();
    PortVISetRetraceHook(OnRetrace, nullptr);
    PortVISetAudioSink(OnAudio, nullptr);

    // GX on Aurora (§25-28): before GXInit, which EGG::GraphicsFifo calls
    wpgx_config cfg{};
    cfg.width = EFB_WIDTH;
    cfg.height = EFB_HEIGHT;
    cfg.headless = gOpt.window ? 0 : 1;
    cfg.data_dir = gOpt.dataDir;
    cfg.disk_cache = 0;
    cfg.blocking_pipelines = 1;
    cfg.allow_cpu_adapter = 1;
    cfg.log_level = gOpt.logLevel;
    if (wpgx_init(&cfg) != 0) {
        Die("wpgx_init failed");
    }
    wpgx_set_address_resolver(Resolve, nullptr);
    wpgx_set_copy_writeback(1, 0, nullptr, nullptr);
    PortGXSetSink(&kSink);
    if (!wpgx_begin_frame()) {
        Die("Aurora skipped the first frame");
    }
    std::printf("BOOT\tadapter=%s\tefb=%ux%u\n", wpgx_adapter_name(), EFB_WIDTH, EFB_HEIGHT);
    std::fflush(stdout);

    char name[] = "main.dol";
    char* gameArgv[] = {name, nullptr};
    return PortGameMain(1, gameArgv);
}
