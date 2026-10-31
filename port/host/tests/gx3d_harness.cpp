/**
 * A minigame's 3D scene drawn by the game's own g3d and EGG code through
 * Aurora (port/docs/HOST_BUILD.md §27; checked by tests/test_gx3d.py).
 *
 *   gx3d_harness <disc files dir> <out.rgba> <data dir> [--frames N] [--log-level N]
 *                [--clear 0xRRGGBB] [--no-puck] [--no-xlu] [--puck-first] [--dump-stream FILE]
 *
 * The scene: Laser Hockey (RPHkyScene) at 4:3. What the game does, on the real
 * code where it links:
 *
 *  - RPGrpRenderer::Initialize(608, 456): EGG::GfxEngine::initialize (StateGX,
 *    Screen::Initialize {608, 832} x 456, G3DUtility, DrawGX's display lists,
 *    light textures, texture buffers), RGBA6_Z24 pixel format, StateGX's cache
 *    on, g3d::G3dInit(false), RPGrpCamera::Initialize;
 *  - RPHkyGame's constructor: model scene 0 (RPGrpModelScene: ScnRoot::Construct
 *    (0x400, 0x400, 16, 8), 32 identity cameras, a light manager (16, 8, 1 view)
 *    with every light set selecting ambient 0 and light 0, a fog manager, an
 *    EGG::ScnRenderer with them). The scene's draw paths (0x28: shadow volume
 *    and bloom) are not created: both need EFB copies (§27, not covered);
 *  - RPGrpScreen(0, 0, SizeXMax, SizeYMax, CANVAS_LU), fovy 40, CANVAS_CC;
 *    RPGrpCamera, orbit type, target (0, 0, 0), distance -745, rotation
 *    (-90, 0, 0) (RPHkyGame::Reset);
 *  - RPHkyGame::Create: light 0 at (0, 745, 0), CalcDirDist, ambient
 *    (100, 100, 100);
 *  - RPHkyField::Load / Reset: Common/RPHkyScene/common.carc through
 *    EGG::DvdFile / DvdRipper / Decomp and ARC (RPSysResourceManager's path),
 *    G3D/Hck_Field_2.brres (ResFile Init + Bind), model Hck_Field_2 as
 *    RPGrpModelG3D builds it with buffer option 0x108 (ScnMdl, 1 view); the six
 *    CLR0 of G3D/Hck_anim.brres (line1, line3, line4, line6, goal_L, goal_R) in
 *    an AnmObjMatClrOverride, each at its last frame with rate 0;
 *  - RPHkyPuck: G3D/Hck_pack.brres model at (0, 0, 0) unless --no-puck.
 *
 * Per frame, RPGrpRenderer's order for a 3D view: GfxEngine::beginDraw
 * (StateGX::frameInit, initGXCache, the clear colour, G3DUtility::reset);
 * ScnRoot::Clear, PushBack (RPGrpModelScene::CalcDrawList); ScnRenderer
 * UpdateFrame / CalcWorld (light and fog managers' Calc); CalcVtx /
 * CalcMaterial (ResetViews); the camera's CalcMatrix and SaveCameraMatrix;
 * DrawGX::SetCameraMtx, colour update on, alpha update off;
 * SetCurrentCamera(0, saved camera matrix, the screen as a perspective
 * screen) and CalcView (light manager CalcView, fog screen clip, g3d view
 * matrices); PreConditionForRenderingGX (Screen::SetProjectionGX, ambient,
 * light objects, fog 0); GatherDrawScnObj, ZSort, DrawOpa, DrawXlu;
 * finishDraw; then GXDrawDone and GXCopyDisp. The harness adds a GXDrawDone
 * before DrawOpa and after DrawXlu so that it can tell which addresses the
 * model draw resolved (the frame is the same without them).
 *
 * --no-xlu skips DrawXlu, --no-puck the puck, --puck-first enters the puck
 * before the table (a depth-test check).
 *
 * Output (stdout, for the test): the matrices the frame used (camera,
 * projection, viewport, world matrices), the BRRES files', model buffers',
 * GP scratch and static copies' emulated address ranges, and every address
 * Aurora resolved, by phase.
 */
#include <types.h>

#include <Pack/RPGraphics/RPGrpCamera.h>
#include <Pack/RPGraphics/RPGrpScreen.h>
#include <egg/core.h>
#include <egg/gfxe.h>
#include <nw4r/g3d.h>
#include <nw4r/math.h>
#include <revolution/ARC.h>
#include <revolution/GX.h>
#include <revolution/MEM.h>
#include <revolution/MTX.h>

#include <port/arena.h>
#include <port/gxsink.h>
#include <port/host.h>

#include "disc_walk.h"
#include "wpgx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <string>
#include <vector>

using namespace nw4r;
using namespace nw4r::g3d;

namespace {

const u32 kEfbWidth = 608;
const u32 kEfbHeight = 456;
const u32 kHeapSize = 8 * 1024 * 1024;
const char* const kArcPath = "/Common/RPHkyScene/common.carc";

[[noreturn]] void Die(const char* what) {
    std::fprintf(stderr, "gx3d_harness: %s\n", what);
    std::exit(1);
}

MEMAllocator sAllocator;

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
        wpgx_end_frame();
        break;
    }
}

const PortGXSink kSink = {SinkWrite, SinkSync, nullptr};

// Every address Aurora resolved, by phase: "setup" (everything before the scene's
// DrawOpa: GXInit, StateGX, DrawGX, the light objects) and "draw" (DrawOpa / DrawXlu;
// the harness drains the FIFO with GXDrawDone before and after them so the phases
// do not mix).
const char* gPhase = "setup";
std::map<std::pair<std::string, u32>, int> gResolved;

const void* Resolve(uint32_t address, uint32_t* size, void*) {
    u32 phys = address & 0x3FFFFFFF;
    const void* p = nullptr;
    if (phys >= PORT_MEM1_OFFSET + PORT_MEM1_RESERVED && phys < PORT_MEM1_OFFSET + PORT_MEM1_SIZE) {
        *size = PORT_MEM1_OFFSET + PORT_MEM1_SIZE - phys;
        p = port_arena_base + phys;
    } else if (phys >= PORT_MEM2_OFFSET && phys < PORT_MEM2_OFFSET + PORT_MEM2_SIZE) {
        *size = PORT_MEM2_OFFSET + PORT_MEM2_SIZE - phys;
        p = port_arena_base + phys;
    }
    gResolved[{gPhase, phys}] += p != nullptr ? 1 : 0;
    return p;
}

void PrintMtx(const char* tag, const f32* p, int n) {
    std::printf("%s", tag);
    for (int i = 0; i < n; i++) {
        std::printf("\t%.9g", p[i]);
    }
    std::printf("\n");
}

struct Brres {
    const char* name;
    u8* pData;
    u32 size;
    ResFile file;
};

Brres LoadBrres(ARCHandle* pHandle, const char* pPath) {
    ARCFileInfo info;
    if (!ARCOpen(pHandle, pPath, &info)) {
        Die("BRRES not found in the archive");
    }
    Brres b;
    b.name = pPath;
    b.pData = static_cast<u8*>(ARCGetStartAddrInMem(&info));
    b.size = ARCGetLength(&info);
    b.file = ResFile(b.pData);
    if (!b.file.CheckRevision()) {
        Die("BRRES revision check failed");
    }
    // RPGrpModelResManager::CreateData(Type_ResFile): Init, then Bind to itself
    b.file.Init();
    b.file.Bind(b.file);
    std::printf("BRRES\t%s\t%08x\t%u\n", pPath, port_vaddr(b.pData) & 0x3FFFFFFF, b.size);
    return b;
}

// RPGrpModelG3D: ScnMdl for a non-zero buffer option, ScnMdlSimple otherwise.
ScnMdlSimple* BuildModel(const char* pName, ResMdl mdl, u32 bufferOption) {
    u32 size;
    ScnMdlSimple* p;
    if (bufferOption == 0) {
        p = ScnMdlSimple::Construct(&sAllocator, &size, mdl, 1);
    } else {
        p = ScnMdl::Construct(&sAllocator, &size, mdl, bufferOption, 1);
    }
    if (p == nullptr) {
        Die("model construction failed");
    }
    // One allocation: the object and its buffers (copied materials, matrices)
    std::printf("MODELBUF\t%s\t%08x\t%u\n", pName, port_vaddr(p) & 0x3FFFFFFF, size);
    return p;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: %s <disc files dir> <out.rgba> <data dir> [--frames N] [--log-level N] "
                     "[--clear 0xRRGGBB] [--no-puck] [--no-xlu] [--puck-first] [--dump-stream FILE]\n",
                     argv[0]);
        return 2;
    }
    const char* outPath = argv[2];
    const char* dataDir = argv[3];
    int frames = 2;
    int logLevel = 2;
    u32 clearRgb = 0xFF00FF;
    bool puck = true;
    bool xlu = true;
    bool puckFirst = false;
    const char* dumpPath = nullptr;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "--log-level" && i + 1 < argc) {
            logLevel = std::atoi(argv[++i]);
        } else if (a == "--clear" && i + 1 < argc) {
            clearRgb = static_cast<u32>(std::strtoul(argv[++i], nullptr, 0));
        } else if (a == "--no-puck") {
            puck = false;
        } else if (a == "--no-xlu") {
            xlu = false;
        } else if (a == "--puck-first") {
            puckFirst = true;
        } else if (a == "--dump-stream" && i + 1 < argc) {
            dumpPath = argv[++i];
        } else {
            Die("unknown option");
        }
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    void* pHeapMem = PortArenaAlloc(kHeapSize, 32);
    MEMiHeapHead* heap = MEMCreateExpHeapEx(pHeapMem, kHeapSize, 0);
    MEMInitAllocatorForExpHeap(&sAllocator, heap, 32);

    // --- GX on Aurora (before anything records display lists) --------------------
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
    GXInit(PortArenaAlloc(kFifoSize, 32), kFifoSize);
    void* pXfb = PortArenaAlloc(kEfbWidth * kEfbHeight * 2, 32);

    // --- RPGrpRenderer::Initialize ---------------------------------------------
    EGG::GfxEngine::initialize(kEfbWidth, kEfbHeight, NULL);
    EGG::StateGX::setDefaultPixelFormat(GX_PF_RGBA6_Z24);
    EGG::StateGX::s_flag |= EGG::StateGX::cFlag_EnableCache;
    g3d::G3dInit(false);
    RPGrpCamera::Initialize();
    const u16 maxX[] = {608, 832};
    u16 maxY[] = {kEfbHeight, kEfbHeight};
    RPGrpScreen::Initialize(maxX, maxY);

    // --- RPGrpModelScene(0x28, 1 light) ---------------------------------------------
    u32 size;
    ScnRoot* pRoot = ScnRoot::Construct(&sAllocator, &size, 0x400, 0x400, 16, 8);
    math::MTX34 ident;
    PSMTXIdentity(ident);
    for (int i = 0; i < 32; i++) {
        pRoot->GetCamera(static_cast<u8>(i)).SetCameraMtxDirectly(ident);
    }
    EGG::LightManager* pLightMgr = new EGG::LightManager(16, 8, 1);
    pLightMgr->Reset();
    for (int i = 0; i < 8; i++) {
        LightSet l = pRoot->GetLightSet(i);
        if (!l.IsValid() || !l.SelectAmbLightObj(0)) {
            Die("light set");
        }
        for (int j = 0; j < pLightMgr->GetNum() && j < 8; j++) {
            if (!l.SelectLightObj(j, j)) {
                Die("light set object");
            }
        }
    }
    EGG::FogManager* pFogMgr = new EGG::FogManager();
    pFogMgr->Reset();
    EGG::ScnRenderer* pRenderer = new EGG::ScnRenderer(pRoot);
    pRenderer->configure();
    pRenderer->setLightManager(pLightMgr);
    pRenderer->setFogManager(pFogMgr);
    // pathFlags 0x28 (shadow volume, bloom) are not created: they need EFB copies.
    pRenderer->createPath(0, &sAllocator);
    u8 opaPrio = 0, xluPrio = 0;
    pRenderer->getTimingPriorityRange(EGG::ScnRenderer::cTiming_Opa4th, &opaPrio, NULL);
    pRenderer->getTimingPriorityRange(EGG::ScnRenderer::cTiming_Xlu1st, &xluPrio, NULL);

    // --- RPHkyGame: screen, camera, light ---------------------------------------------
    RPGrpScreen* pScreen = new RPGrpScreen(0.0f, 0.0f, EGG::Screen::GetSizeXMax(),
                                           EGG::Screen::GetSizeYMax(), NULL, EGG::Frustum::CANVAS_LU);
    pScreen->SetFovy(40.0f);
    pScreen->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    RPGrpCamera* pCamera = new RPGrpCamera();
    pCamera->SetTarget(0.0f, 0.0f, 0.0f);
    pCamera->SetDistance(-745.0f);
    pCamera->SetRotation(-90.0f, 0.0f, 0.0f);
    pCamera->SetCameraType(RPGrpCamera::ECameraType_Orbit);

    EGG::LightObject* pLight = pLightMgr->GetLightObj(0);
    pLight->SetPos(math::VEC3(0.0f, 745.0f, 0.0f));
    pLight->CalcDirDist();
    GXColor ambient = {100, 100, 100, 0};
    pLightMgr->GetAmbientColor(0) = ambient;

    // --- Resources ---------------------------------------------------------------------
    u32 arcSize = 0;
    s32 bufSize = 0;
    u8* pArcFile = disc_walk::LoadFromDVD(kArcPath, &bufSize, &arcSize);
    if (pArcFile == NULL) {
        Die("cannot load the scene archive");
    }
    s32 expandSize = 0;
    u8* pArc = disc_walk::ExpandCompressed(pArcFile, static_cast<s32>(arcSize), &expandSize);
    ARCHandle handle;
    if (pArc == NULL || !ARCInitHandle(pArc, &handle)) {
        Die("cannot mount the scene archive");
    }
    Brres field = LoadBrres(&handle, "G3D/Hck_Field_2.brres");
    Brres anim = LoadBrres(&handle, "G3D/Hck_anim.brres");
    Brres pack = LoadBrres(&handle, "G3D/Hck_pack.brres");

    struct Model {
        const char* name;
        ScnMdlSimple* pMdl;
    };
    std::vector<Model> models;

    ResMdl fieldMdl = field.file.GetResMdl("Hck_Field_2");
    if (!fieldMdl.IsValid()) {
        Die("Hck_Field_2 not found");
    }
    ScnMdlSimple* pField = BuildModel("Hck_Field_2", fieldMdl, 0x108);
    Model fieldModel = {"Hck_Field_2", pField};
    models.push_back(fieldModel);
    {
        static const char* const kClr[6] = {"line1", "line3", "line4", "line6", "goal_L", "goal_R"};
        AnmObjMatClrOverride* pOverride = AnmObjMatClrOverride::Construct(&sAllocator, &size, fieldMdl, 6);
        pOverride->Bind(fieldMdl);
        pField->SetAnmObj(pOverride);
        for (int i = 0; i < 6; i++) {
            ResAnmClr clr = anim.file.GetResAnmClr(kClr[i]);
            if (!clr.IsValid()) {
                Die("CLR0 not found");
            }
            AnmObjMatClrRes* pRes = AnmObjMatClrRes::Construct(&sAllocator, &size, clr, fieldMdl, false);
            pRes->Bind(fieldMdl);
            pRes->SetFrame(clr.GetNumFrame() - 1.0f);
            pRes->SetUpdateRate(0.0f);
            pOverride->Attach(i, pRes);
            std::printf("CLR\t%s\t%g\n", kClr[i], clr.GetNumFrame() - 1.0f);
        }
    }
    if (puck) {
        // RPHkyPuck::Load: model Hck_pack (RPSimpleModel::LoadModel, buffer option 0x108)
        ResMdl packMdl = pack.file.GetResMdl("Hck_pack");
        if (!packMdl.IsValid()) {
            Die("Hck_pack not found");
        }
        Model m = {"Hck_pack", BuildModel("Hck_pack", packMdl, 0x108)};
        models.push_back(m);
    }
    for (Model& m : models) {
        // RPSimpleModel::CalcModelMtx: identity rotation, position 0, scale 1
        m.pMdl->SetMtx(ScnObj::MTX_LOCAL, &ident);
        m.pMdl->SetPriorityDrawOpa(opaPrio);
        m.pMdl->SetPriorityDrawXlu(xluPrio);
    }

    // --- Frames --------------------------------------------------------------------------
    const GXColor clear = {static_cast<u8>(clearRgb >> 16), static_cast<u8>(clearRgb >> 8),
                           static_cast<u8>(clearRgb), 255};
    for (int frame = 0; frame < frames; frame++) {
        if (!wpgx_begin_frame()) {
            Die("frame skipped");
        }
        GXSetCopyClear(clear, 0xFFFFFF);
        // GfxEngine::beginDraw (BaseSystem's display clear colour is the copy clear)
        EGG::StateGX::frameInit();
        EGG::StateGX::initGXCache();
        EGG::StateGX::setEfbClearColor(clear);
        EGG::G3DUtility::reset();

        // PreCalculate / PostCalculate: the draw list, world matrices, lights, fog
        pRoot->Clear();
        // --puck-first: the puck is entered (and drawn) before the table (a depth check)
        for (size_t i = 0; i < models.size(); i++) {
            pRoot->PushBack(models[puckFirst ? models.size() - 1 - i : i].pMdl);
        }
        pRenderer->pushBackDrawPath();
        pRenderer->UpdateFrame();
        pRenderer->CalcWorld();
        pCamera->CalcMatrix();
        pCamera->SaveCameraMatrix();
        // PreDraw: ResetViews
        pRenderer->CalcVtx();
        pRenderer->CalcMaterial();

        // Rendering, view 0
        EGG::DrawGX::SetCameraMtx(pCamera->GetSavedCameraMatrix());
        EGG::StateGX::GXSetColorUpdate_(true);
        EGG::StateGX::GXSetAlphaUpdate_(false);
        RPGrpScreen clone(*pScreen);
        clone.SetProjectionType(EGG::Screen::PROJ_PERSP);
        pRenderer->SetCurrentCamera(0, pCamera->GetSavedCameraMatrix(), clone);
        pRenderer->CalcView();
        // PreConditionForRenderingGX
        pRenderer->getScreen().SetProjectionGX();
        pLightMgr->LoadAmbientColorGX();
        pLightMgr->LoadObjIndxGX();
        pFogMgr->GetFog(0)->SetGX();
        // Draw
        pRenderer->GatherDrawScnObj();
        pRenderer->ZSort();
        GXDrawDone();
        gPhase = "draw";
        pRenderer->DrawOpa();
        if (xlu) {
            pRenderer->DrawXlu();
        }
        GXDrawDone();
        gPhase = "setup";
        pRenderer->finishDraw();

        GXDrawDone();
        GXCopyDisp(pXfb, GX_TRUE);
    }

    // --- What the frame used --------------------------------------------------------------
    PrintMtx("CAMERA", &pCamera->GetSavedCameraMatrix()._00, 12);
    f32 proj[GX_PROJECTION_SZ];
    GXGetProjectionv(proj);
    PrintMtx("PROJ", proj, GX_PROJECTION_SZ);
    f32 vp[GX_VIEWPORT_SZ];
    GXGetViewportv(vp);
    PrintMtx("VIEWPORT", vp, GX_VIEWPORT_SZ);
    for (Model& m : models) {
        ResMdl mdl = m.pMdl->GetResMdl();
        const math::MTX34* pWorld = m.pMdl->GetWldMtxArray();
        const u32 n = mdl.GetResMdlInfo().GetNumViewMtx();
        for (u32 i = 0; i < n; i++) {
            char tag[128];
            std::snprintf(tag, sizeof tag, "WORLD\t%s\t%u", m.name, i);
            PrintMtx(tag, &pWorld[i]._00, 12);
        }
    }
    u32 scratchBase, scratchSize;
    PortGPScratchRange(&scratchBase, &scratchSize);
    std::printf("SCRATCH\t%08x\t%u\n", scratchBase & 0x3FFFFFFF, scratchSize);
    u32 staticAddrs[32], staticSizes[32];
    const u32 numStatic = PortArenaStaticList(staticAddrs, staticSizes, 32);
    for (u32 i = 0; i < numStatic; i++) {
        std::printf("STATIC\t%08x\t%u\n", staticAddrs[i] & 0x3FFFFFFF, staticSizes[i]);
    }
    for (const auto& r : gResolved) {
        std::printf("RESOLVED\t%s\t%08x\t%d\n", r.first.first.c_str(), r.first.second, r.second);
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
    std::printf("gx3d_harness: %ux%u after %d frames on %s; GX stream %llu bytes\n", w, h, frames,
                wpgx_adapter_name(), static_cast<unsigned long long>(PortGXStreamBytes()));
    PortGXSetSink(nullptr);
    wpgx_shutdown();
    return 0;
}
