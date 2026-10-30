/**
 * The health and safety screen drawn by the game's own code through Aurora
 * (port/docs/HOST_BUILD.md §26; checked by tests/test_gxlyt.py).
 *
 *   gxlyt_harness <disc files dir> <out.rgba> <data dir> [--frames N] [--layout NAME]
 *                 [--log-level N] [--dump-stream FILE] [--clear 0xRRGGBB] [--display-list]
 *
 * RPSysBootScene::Init's loading, on the real code: the Yaz0 archive
 * Health_strapArchive (main.dol, .data6 0x802E9E60), EGG::Decomp,
 * EGG::Archive::mount and getFile("strap.arc"), an lyt::ArcResourceAccessor
 * attached with root "" (RPSysLytResAccessor::mountArchive) and
 * lyt::Layout::Build("strap_00_U.brlyt"; TPLBind on its textures).
 *
 * Drawing: the decomp's GX SDK (src/revolution/GX, GXInit included) encodes
 * every command; the host write-gather pipe hands the stream to Aurora
 * (libwiiplay_gx) with an address resolver over M0, so CP array bases,
 * display lists, texture images and TLUTs are read from the same Wii memory
 * the game uses. Per frame: copy clear white (RPSysBootScene::Init's
 * RPGrpRenderer::SetClearColor(255, 255, 255)), viewport and scissor over the
 * 608x456 EFB, the 2D view's orthographic projection (RPGrpRenderer: the
 * layout rectangle, near -10000, far 10000), RPSysLayout::drawInit's state,
 * the DrawInfo RPSysLayout::create sets up (view rect = layout rect, identity
 * view matrix, location adjust on) and RPSysLayout::calc's root-pane scale
 * (1 at 4:3), then Layout::CalculateMtx and Layout::Draw; GXDrawDone and
 * GXCopyDisp end the frame. The frame is read back from Aurora after the last
 * frame. The layout is drawn at frame 0 of its animation (the file's values).
 */
#include <types.h>

#include <egg/core.h>
#include <nw4r/lyt.h>
#include <nw4r/math.h>
#include <revolution/GX.h>
#include <revolution/MEM.h>
#include <revolution/MTX.h>

#include <port/arena.h>
#include <port/gxsink.h>
#include <port/host.h>

#include "wpgx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nw4r;

namespace {

const u32 kEfbWidth = 608;
const u32 kEfbHeight = 456;

// Health_strapArchive: the start of main.dol's .data6 (the archive fills the section).
const u32 kStrapAddress = 0x802E9E60;

[[noreturn]] void Die(const char* what) {
    std::fprintf(stderr, "gxlyt_harness: %s\n", what);
    std::exit(1);
}

u32 Be32(const u8* p) {
    return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 |
           static_cast<u32>(p[2]) << 8 | p[3];
}

// The DOL section that holds `address`, from `address` to the section end.
std::vector<u8> ReadDolData(const std::string& dolPath, u32 address) {
    FILE* f = std::fopen(dolPath.c_str(), "rb");
    if (f == nullptr) {
        Die("cannot open main.dol");
    }
    std::vector<u8> dol;
    u8 buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        dol.insert(dol.end(), buf, buf + n);
    }
    std::fclose(f);
    if (dol.size() < 0x100) {
        Die("main.dol too short");
    }
    for (u32 i = 0; i < 18; i++) {
        u32 offset = Be32(&dol[i * 4]);
        u32 start = Be32(&dol[0x48 + i * 4]);
        u32 size = Be32(&dol[0x90 + i * 4]);
        if (address >= start && address - start < size && offset + size <= dol.size()) {
            return std::vector<u8>(dol.begin() + offset + (address - start), dol.begin() + offset + size);
        }
    }
    Die("address not in main.dol");
}

void* HostAlloc(MEMAllocator*, u32 size) {
    return PortArenaAlloc(size, 32);
}

void HostFree(MEMAllocator*, void* p) {
    PortArenaFree(p);
}

// --- The sink: the host GX stream into Aurora ---------------------------------

FILE* gDump = nullptr;

void SinkWrite(const void* data, u32 size, void*) {
    if (gDump != nullptr) {
        std::fwrite(data, 1, size, gDump);
    }
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
        // GXCopyDisp: the frame is complete (the host has no XFB scan-out)
        wpgx_end_frame();
        break;
    }
}

const PortGXSink kSink = {SinkWrite, SinkSync, nullptr};

// Emulated addresses in the stream -> M0 (MEM1 and MEM2; the gap is unmapped).
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: %s <disc files dir> <out.rgba> <data dir> [--frames N] "
                     "[--layout NAME] [--log-level N] [--dump-stream FILE] [--clear 0xRRGGBB] [--display-list]\n",
                     argv[0]);
        return 2;
    }
    const std::string discDir = argv[1];
    const char* outPath = argv[2];
    const char* dataDir = argv[3];
    int frames = 2;
    int logLevel = 2;
    std::string layoutName = "strap_00_U.brlyt";
    const char* dumpPath = nullptr;
    u32 clearRgb = 0xFFFFFF;
    bool displayList = false;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "--layout" && i + 1 < argc) {
            layoutName = argv[++i];
        } else if (a == "--log-level" && i + 1 < argc) {
            logLevel = std::atoi(argv[++i]);
        } else if (a == "--display-list") {
            displayList = true;
        } else if (a == "--clear" && i + 1 < argc) {
            clearRgb = static_cast<u32>(std::strtoul(argv[++i], nullptr, 0));
        } else if (a == "--dump-stream" && i + 1 < argc) {
            dumpPath = argv[++i];
        } else {
            Die("unknown option");
        }
    }

    PortArenaInit();

    // --- RPSysBootScene::Init's loading -----------------------------------------
    std::vector<u8> dolData = ReadDolData(discDir + "/../sys/main.dol", kStrapAddress);
    u8* pStrap = static_cast<u8*>(PortArenaAlloc(static_cast<u32>(dolData.size()), 32));
    std::memcpy(pStrap, dolData.data(), dolData.size());
    if (EGG::Decomp::checkCompressed(pStrap) == EGG::Decomp::cCompress_None) {
        Die("Health_strapArchive is not compressed");
    }
    u32 size = EGG::Decomp::getExpandSize(pStrap);
    u8* pBinary = static_cast<u8*>(PortArenaAlloc(size, 32));
    EGG::Decomp::decode(pStrap, pBinary);
    EGG::Archive* pArchive = EGG::Archive::mount(pBinary, NULL, 4);
    if (pArchive == NULL) {
        Die("EGG::Archive::mount failed");
    }
    void* pLayoutArchive = pArchive->getFile("strap.arc", NULL);
    if (pLayoutArchive == NULL) {
        Die("strap.arc not found");
    }
    static const MEMAllocatorFuncs sFuncs = {HostAlloc, HostFree};
    static MEMAllocator sAllocator = {&sFuncs, NULL, 0, 0};
    lyt::Layout::SetAllocator(&sAllocator);

    lyt::ArcResourceAccessor* pAccessor = new lyt::ArcResourceAccessor();
    if (!pAccessor->Attach(pLayoutArchive, "")) {
        Die("ArcResourceAccessor::Attach failed");
    }
    void* pLayoutBin = pAccessor->GetResource('blyt', layoutName.c_str(), NULL);
    lyt::Layout* pLayout = new lyt::Layout();
    if (pLayoutBin == NULL || !pLayout->Build(pLayoutBin, pAccessor)) {
        Die("Layout::Build failed");
    }

    // RPSysLayout::create / calc
    lyt::DrawInfo* pDrawInfo = new lyt::DrawInfo();
    pDrawInfo->SetViewRect(pLayout->GetLayoutRect());
    math::MTX34 ident;
    math::MTX34Identity(&ident);
    pDrawInfo->SetViewMtx(ident);
    pDrawInfo->SetLocationAdjust(true);
    pDrawInfo->SetLocationAdjustScale(math::VEC2(1.0f, 1.0f));
    pLayout->GetRootPane()->SetScale(math::VEC2(1.0f, 1.0f));
    pLayout->CalculateMtx(*pDrawInfo);
    const ut::Rect rect = pLayout->GetLayoutRect();

    // --- GX on Aurora ------------------------------------------------------------
    wpgx_config cfg{};
    cfg.width = kEfbWidth;
    cfg.height = kEfbHeight;
    cfg.headless = 1;
    cfg.data_dir = dataDir;
    cfg.disk_cache = 0;
    cfg.blocking_pipelines = 1;
    cfg.allow_cpu_adapter = 1;
    cfg.log_level = logLevel;
    if (wpgx_init(&cfg) != 0) {
        Die("wpgx_init failed");
    }
    wpgx_set_address_resolver(Resolve, nullptr);
    if (dumpPath != nullptr) {
        gDump = std::fopen(dumpPath, "wb");
    }
    PortGXSetSink(&kSink);

    const u32 kFifoSize = 256 * 1024;
    void* pFifo = PortArenaAlloc(kFifoSize, 32);
    GXInit(pFifo, kFifoSize);
    void* pXfb = PortArenaAlloc(kEfbWidth * kEfbHeight * 2, 32);
    const u32 kListSize = 64 * 1024;
    void* pList = PortArenaAlloc(kListSize, 32);
    u32 listSize = 0;

    Mtx44 proj;
    for (int frame = 0; frame < frames; frame++) {
        if (!wpgx_begin_frame()) {
            Die("frame skipped");
        }
        GXColor clear = {static_cast<u8>(clearRgb >> 16), static_cast<u8>(clearRgb >> 8),
                         static_cast<u8>(clearRgb), 255};
        GXSetCopyClear(clear, 0xFFFFFF);
        GXSetViewport(0.0f, 0.0f, static_cast<f32>(kEfbWidth), static_cast<f32>(kEfbHeight), 0.0f,
                      1.0f);
        GXSetScissor(0, 0, kEfbWidth, kEfbHeight);
        C_MTXOrtho(proj, rect.top, rect.bottom, rect.left, rect.right, -10000.0f, 10000.0f);
        GXSetProjection(proj, GX_ORTHOGRAPHIC);
        // RPSysLayout::drawInit
        GXSetCullMode(GX_CULL_NONE);
        GXSetZMode(GX_FALSE, GX_NEVER, GX_FALSE);
        GXSetClipMode(GX_CLIP_ENABLE);

        if (displayList) {
            // Recorded into a display list in Wii memory, then called (CALL_DL)
            GXBeginDisplayList(pList, kListSize);
            pLayout->Draw(*pDrawInfo);
            listSize = GXEndDisplayList();
            if (listSize == 0) {
                Die("display list overflow");
            }
            GXCallDisplayList(pList, listSize);
        } else {
            pLayout->Draw(*pDrawInfo);
        }

        GXDrawDone();
        GXCopyDisp(pXfb, GX_TRUE);
    }

    std::vector<uint8_t> rgba(kEfbWidth * kEfbHeight * 4);
    uint32_t w = 0, h = 0;
    if (wpgx_read_frame(rgba.data(), rgba.size(), &w, &h) != 0 || w != kEfbWidth || h != kEfbHeight) {
        Die("wpgx_read_frame failed");
    }
    FILE* out = std::fopen(outPath, "wb");
    if (out == nullptr) {
        Die("cannot write output");
    }
    std::fwrite(rgba.data(), 1, rgba.size(), out);
    std::fclose(out);
    if (gDump != nullptr) {
        std::fclose(gDump);
    }

    std::printf("gxlyt_harness: %s %ux%u after %d frames on %s; layout rect (%g, %g)-(%g, %g); "
                "GX stream %llu bytes%s\n",
                layoutName.c_str(), w, h, frames, wpgx_adapter_name(), rect.left, rect.top,
                rect.right, rect.bottom, static_cast<unsigned long long>(PortGXStreamBytes()),
                displayList ? (", display list " + std::to_string(listSize) + " bytes").c_str() : "");
    PortGXSetSink(nullptr);
    wpgx_shutdown();
    return 0;
}
