/**
 * GX bring-up test on Aurora (port/docs/HOST_BUILD.md §25), 64-bit only.
 *
 * Loads banner.tpl and one BRRES material from the disc through the port's
 * loaders (EGG::DvdFile/DvdRipper/Decomp, ARC, TPLBind/TPLGet,
 * nw4r::g3d::ResFile), brings Aurora up through libwiiplay_gx (headless
 * unless --window), and draws two quads:
 *
 *   top:    the TPL image through GXInitTexObj/GXLoadTexObj, TEV REPLACE,
 *           blended over the clear colour by its alpha;
 *   bottom: the same texture on maps 0 and 1, with the material's pixel,
 *           TEV colour and TEV display lists (big-endian, as stored in the
 *           BRRES) submitted with GXCallDisplayList.
 *
 * Then it reads the frame back and writes it (raw RGBA8) to <out>. It
 * prints what it submitted (texture parameters, display-list bytes in hex)
 * so tests/test_gx.py can check them against its own parse of the disc
 * files and model the expected image independently.
 *
 *   gx_harness <disc-files-dir> <out.rgba> <aurora-data-dir> [options]
 *     --frames N       frames to draw before the readback (default 3)
 *     --window         a window instead of headless
 *     --async          draws do not wait for pipeline compilation
 *     --disk-cache     keep Aurora's pipeline/Dawn caches in <aurora-data-dir>
 *     --cache-key N    pipeline cache key (default: libwiiplay_gx's own)
 *     --log-level N    Aurora log level (0 debug .. 3 error; default 2)
 */
#include <nw4r/g3d.h>
#include <revolution/ARC.h>
#include <revolution/TPL.h>

#include <port/arena.h>
#include <port/host.h>

#include "disc_walk.h"

#include <wpgx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nw4r::g3d;

namespace {

const char* const kTplPath = "/banner.tpl";
const char* const kArcPath = "/Common/RPFshScene/common.carc";
const char* const kBrresPath = "G3D/fsh_lake.brres";
const char* const kMatName = "a_a_lake_mat_v";
const u32 kClear = 0x1E1E3CFF;

void PrintHex(const char* tag, const void* p, u32 size) {
    const u8* b = static_cast<const u8*>(p);
    std::printf("DL\t%s\t%u\t", tag, size);
    for (u32 i = 0; i < size; i++) {
        std::printf("%02x", b[i]);
    }
    std::printf("\n");
}

u8* Load(const char* path, u32* pSize) {
    s32 bufSize = 0;
    u8* p = disc_walk::LoadFromDVD(path, &bufSize, pSize);
    if (p == NULL) {
        std::fprintf(stderr, "ERROR: cannot load %s\n", path);
        std::exit(1);
    }
    return p;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> <out.rgba> <aurora-data-dir> [options]\n", argv[0]);
        return 2;
    }
    int frames = 3;
    bool window = false;
    bool async = false;
    bool diskCache = false;
    u32 cacheKey = 0;
    int logLevel = 2;
    for (int i = 4; i < argc; i++) {
        const bool more = i + 1 < argc;
        if (std::strcmp(argv[i], "--frames") == 0 && more) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--window") == 0) {
            window = true;
        } else if (std::strcmp(argv[i], "--async") == 0) {
            async = true;
        } else if (std::strcmp(argv[i], "--disk-cache") == 0) {
            diskCache = true;
        } else if (std::strcmp(argv[i], "--cache-key") == 0 && more) {
            cacheKey = static_cast<u32>(std::strtoul(argv[++i], NULL, 0));
        } else if (std::strcmp(argv[i], "--log-level") == 0 && more) {
            logLevel = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    // --- Texture: banner.tpl image 0 through TPLBind/TPLGet -------------------
    u32 tplSize = 0;
    u8* pTpl = Load(kTplPath, &tplSize);
    TPLPalette* pPal = reinterpret_cast<TPLPalette*>(pTpl);
    TPLBind(pPal);
    const TPLHeader* pTex = TPLGet(pPal, 0)->textureHeader;
    const u16 texW = pTex->width;
    const u16 texH = pTex->height;
    const u32 texFmt = pTex->format;
    const char* pTexels = pTex->data;
    std::printf("TEX\t%s\t0\t%u\t%u\t%u\t%u\n", kTplPath + 1, texW, texH, texFmt,
                static_cast<u32>(pTexels - reinterpret_cast<char*>(pTpl)));

    // --- Material: ResFile from the archive, display lists via ResMat/ResTev ---
    u32 arcSize = 0;
    u8* pArcFile = Load(kArcPath, &arcSize);
    s32 expandSize = 0;
    u8* pArc = disc_walk::ExpandCompressed(pArcFile, static_cast<s32>(arcSize), &expandSize);
    ARCHandle handle;
    ARCFileInfo info;
    if (pArc == NULL || !ARCInitHandle(pArc, &handle) || !ARCOpen(&handle, kBrresPath, &info)) {
        std::fprintf(stderr, "ERROR: cannot open %s in %s\n", kBrresPath, kArcPath);
        return 1;
    }
    u8* pBrres = static_cast<u8*>(ARCGetStartAddrInMem(&info));
    ResFile file(pBrres);
    if (!file.CheckRevision()) {
        std::fprintf(stderr, "ERROR: %s: revision check failed\n", kBrresPath);
        return 1;
    }
    file.Init();
    file.Bind(file);

    ResMat mat;
    for (u32 m = 0; m < file.GetResMdlNumEntries() && !mat.IsValid(); m++) {
        mat = file.GetResMdl(m).GetResMat(kMatName);
    }
    if (!mat.IsValid()) {
        std::fprintf(stderr, "ERROR: material %s not found\n", kMatName);
        return 1;
    }
    ResTev tev = mat.GetResTev();
    const u32 nStages = tev.ref().nStages;
    std::printf("MAT\t%s%s/%s\t%s\t%u\n", kArcPath + 1, "", kBrresPath, kMatName, nStages);
    const void* pPixDL = mat.GetResMatPix().ptr();
    const void* pTevColorDL = mat.GetResMatTevColor().ptr();
    const void* pTevDL = &tev.ref().dl;
    PrintHex("pix", pPixDL, sizeof(ResPixDL));
    PrintHex("tevcolor", pTevColorDL, sizeof(ResTevColorDL));
    PrintHex("tev", pTevDL, sizeof(ResTevDL));

    // --- Aurora -------------------------------------------------------------------
    wpgx_config cfg = {};
    cfg.width = 640;
    cfg.height = 480;
    cfg.headless = window ? 0 : 1;
    cfg.data_dir = argv[3];
    cfg.disk_cache = diskCache ? 1 : 0;
    cfg.cache_key = cacheKey;
    cfg.blocking_pipelines = async ? 0 : 1;
    cfg.allow_cpu_adapter = 1;
    cfg.log_level = logLevel;
    if (wpgx_init(&cfg) != 0) {
        std::fprintf(stderr, "ERROR: wpgx_init failed\n");
        return 1;
    }
    std::printf("ADAPTER\t%s\n", wpgx_adapter_name());

    int drawn = 0;
    for (int f = 0; f < frames; f++) {
        if (!wpgx_begin_frame()) {
            continue;
        }
        wpgx_test_setup_2d(kClear, 2);
        wpgx_test_load_texture(0, pTexels, texW, texH, texFmt, 0, 0, 0);
        wpgx_test_load_texture(1, pTexels, texW, texH, texFmt, 0, 0, 0);

        wpgx_test_set_tev_replace(0);
        wpgx_test_quad(32, 32, 32 + 576, 32 + 192);

        wpgx_test_call_display_list(pPixDL, sizeof(ResPixDL));
        wpgx_test_call_display_list(pTevColorDL, sizeof(ResTevColorDL));
        wpgx_test_call_display_list(pTevDL, sizeof(ResTevDL));
        wpgx_test_set_num_tev_stages(nStages);
        wpgx_test_quad(32, 256, 32 + 576, 256 + 192);
        wpgx_end_frame();
        drawn++;
    }

    std::vector<u8> rgba(640 * 480 * 4);
    u32 w = 0;
    u32 h = 0;
    const int rc = wpgx_read_frame(rgba.data(), rgba.size(), &w, &h);
    wpgx_shutdown();
    if (rc != 0 || drawn == 0) {
        std::fprintf(stderr, "ERROR: readback failed (%ux%u, %d frames drawn)\n", w, h, drawn);
        return 1;
    }
    FILE* out = std::fopen(argv[2], "wb");
    if (out == NULL || std::fwrite(rgba.data(), 1, size_t(w) * h * 4, out) != size_t(w) * h * 4) {
        std::fprintf(stderr, "ERROR: cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(out);
    std::printf("FRAME\t%u\t%u\t%d\n", w, h, drawn);
    return 0;
}
