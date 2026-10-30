/**
 * g3d scene/model runtime harness (port/docs/HOST_BUILD.md §20).
 *
 * Walks the disc like anm_harness (disc_walk.h; every BRRES set up as
 * RPGrpModelResManager::InternalCreateData does), and for every model builds
 * the scene objects the way the game does, without drawing:
 *
 *   - RPGrpModelManager / RPSysScene: the scene heap's EGG::Allocator (align
 *     32) for the models and the scene root. EGG::Heap is the host's M0 stand-in
 *     (src/host_sdk.cpp), so the harness builds what EGG::ExpHeap::create and
 *     EGG::Allocator(heap) build underneath: a MEM expanded heap and
 *     MEMInitAllocatorForExpHeap(allocator, heap, 32);
 *   - RPGrpRenderer: g3d::G3dInit(false), so the locked cache stays off and
 *     ScnMdlSimple / CalcView take their main-memory paths (the harness calls
 *     the LC part, ut::LC::Disable(); the rest of G3dInit sets the render mode);
 *   - RPGrpModelScene: ScnRoot::Construct(allocator, 0x400, 0x400, 16, 8), the
 *     model pushed into it, camera 0 current;
 *   - RPGrpModelG3D (G3D_ConstructScnObj): ScnMdlSimple::Construct(allocator,
 *     mdl, 1) for bufferOption 0, and ScnMdl::Construct(allocator, mdl,
 *     ANM_MATCLR | ANM_TEXSRT | ANM_TEXPAT, 1) as a model with buffers;
 *   - RPGrpModelAnmG3D: CHR through an AnmObjChrBlend of 8 with the
 *     AnmObjChrRes attached at slot 0 (weight 1); CLR / SRT / PAT as
 *     AnmObj*Res set on the model directly (one animation of that kind);
 *     AnmObjChrRes / AnmObjMatClrRes / ... Construct(cache = false) + Bind;
 *   - per frame: AnmObj::SetFrame, then ScnRoot::CalcWorld, CalcMaterial,
 *     CalcVtx and CalcView (fixed camera), as RPGrpModelScene::Calc does.
 *
 * Every animation in the same BRRES, or in a BRRES of the same archive that has
 * no models (the game's animation files), whose Bind succeeds is applied, one at
 * a time, at a set of frames; with two or more CHR0 the first two are also
 * blended 0.5 / 0.5 (regression only). The plain ScnMdlSimple runs the CHR
 * animations and its world matrices are compared with the ScnMdl's; the
 * material animations run on the ScnMdl (copied buffers) and again on the
 * ScnMdlSimple (CalcMaterialDirectly writes into the resource, which is
 * restored afterwards), and the results must agree.
 *
 * Output (tab separated, floats as bit patterns):
 *   CAM   camera matrix (12)
 *   MDL   brres, model, nodes, posNrmMtx, viewMtx, materials
 *   RUN   brres, model, kind (BIND, CHR, BLEND, CLR, SRT, PAT), anim brres, anim,
 *         frame
 *   W     mtxID, attribute, world matrix (12)        all posNrm matrices
 *   V     mtxID, view position matrix (12)           view matrices
 *   N     mtxID, view normal matrix (9)              if the model needs them
 *   C     material, chan 0/1 mat + amb, tev colour 1-3, konst 0-3
 *   T     material, flag, mode, per texture Su Sv R Tu Tv
 *   P     material, per texture map: texture name, image0
 */
#include <nw4r/g3d.h>

#include <revolution/MEM.h>

#include <port/arena.h>
#include <port/be.h>

#include "disc_walk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nw4r;
using namespace nw4r::g3d;

namespace {

int sFiles = 0, sModels = 0, sRuns = 0, sAnims = 0;

MEMiHeapHead* spHeap = NULL;
MEMAllocator sAllocator;
MEMAllocator* const spAllocator = &sAllocator;
ScnRoot* spRoot = NULL;

struct Brres {
    std::string path;
    u8* pData;
    u32 size;
};

// Textures of the archive being tested (PAT results are printed by name).
struct TexKey {
    u32 image3;
    std::string name;
};
std::vector<TexKey> sTextures;

u32 Bits(f32 v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    return u;
}

std::string ResNameStr(s32 ofsName, const void* pBase) {
    return ofsName != 0 ? std::string(static_cast<const char*>(pBase) + ofsName) : "";
}

void PrintF(const f32* p, int n) {
    for (int i = 0; i < n; i++) {
        std::printf("\t%08x", Bits(p[i]));
    }
}

u32 ReadBE32(const void* p) {
    const u8* b = static_cast<const u8*>(p);
    return static_cast<u32>(b[0]) << 24 | static_cast<u32>(b[1]) << 16 |
           static_cast<u32>(b[2]) << 8 | b[3];
}

/** Frames each animation is applied at. */
std::vector<f32> Frames(u32 numFrame) {
    f32 n = static_cast<f32>(numFrame);
    std::vector<f32> frames = {0.0f, 1.5f};
    frames.push_back(static_cast<f32>(static_cast<int>(n / 3.0f)) + 0.25f);
    frames.push_back(static_cast<f32>(static_cast<int>(n * 2.0f / 3.0f)));
    if (n >= 1.0f) {
        frames.push_back(n - 1.0f);
    }
    return frames;
}

// --- Output ------------------------------------------------------------------------

void PrintMatrices(ScnMdlSimple* pMdl) {
    ResMdl mdl = pMdl->GetResMdl();
    u32 numMtx = mdl.GetResMdlInfo().GetNumPosNrmMtx();
    u32 numView = pMdl->GetNumViewMtx();
    const math::MTX34* pW = pMdl->GetWldMtxArray();
    const u32* pAttr = pMdl->GetWldMtxAttribArray();
    for (u32 i = 0; i < numMtx; i++) {
        std::printf("W\t%u\t%08x", i, pAttr[i]);
        PrintF(&pW[i]._00, 12);
        std::printf("\n");
    }
    const math::MTX34* pV = pMdl->GetViewPosMtxArray();
    const math::MTX33* pN = pMdl->GetViewNrmMtxArray();
    for (u32 i = 0; i < numView; i++) {
        std::printf("V\t%u", i);
        PrintF(&pV[i]._00, 12);
        std::printf("\n");
        if (pN != NULL) {
            std::printf("N\t%u", i);
            PrintF(&pN[i]._00, 9);
            std::printf("\n");
        }
    }
}

std::string ColorStr(ResMatChan chan, ResMatTevColor tev) {
    char buf[256];
    std::string s;
    for (int c = 0; c < 2; c++) {
        GXColor mat = {0, 0, 0, 0}, amb = {0, 0, 0, 0};
        if (chan.IsValid()) {
            chan.GXGetChanMatColor(static_cast<GXChannelID>(GX_COLOR0A0 + c), &mat);
            chan.GXGetChanAmbColor(static_cast<GXChannelID>(GX_COLOR0A0 + c), &amb);
        }
        std::snprintf(buf, sizeof buf, "\t%02x%02x%02x%02x\t%02x%02x%02x%02x", mat.r, mat.g,
                      mat.b, mat.a, amb.r, amb.g, amb.b, amb.a);
        s += buf;
    }
    for (int r = GX_TEVREG0; r <= GX_TEVREG2; r++) {
        GXColorS10 c = {0, 0, 0, 0};
        if (tev.IsValid()) {
            tev.GXGetTevColorS10(static_cast<GXTevRegID>(r), &c);
        }
        std::snprintf(buf, sizeof buf, "\t%04x%04x%04x%04x", c.r & 0xFFFF, c.g & 0xFFFF,
                      c.b & 0xFFFF, c.a & 0xFFFF);
        s += buf;
    }
    for (int k = GX_KCOLOR0; k <= GX_KCOLOR3; k++) {
        GXColor c = {0, 0, 0, 0};
        if (tev.IsValid()) {
            tev.GXGetTevKColor(static_cast<GXTevKColorID>(k), &c);
        }
        std::snprintf(buf, sizeof buf, "\t%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
        s += buf;
    }
    return s;
}

std::string SrtStr(ResTexSrt srt) {
    char buf[128];
    if (!srt.IsValid()) {
        return "\t-";
    }
    std::snprintf(buf, sizeof buf, "\t%08x\t%u", static_cast<u32>(srt.ref().flag),
                  static_cast<u32>(srt.GetTexMtxMode()));
    std::string s = buf;
    for (int i = 0; i < ResTexSrtData::NUM_OF_TEXTURE; i++) {
        if (!srt.IsExist(i)) {
            s += "\t-"; // CopyTo copies only the texture matrices that exist
            continue;
        }
        const TexSrt& t = srt.ref().texSrt[i];
        f32 v[5] = {t.Su, t.Sv, t.R, t.Tu, t.Tv};
        std::snprintf(buf, sizeof buf, "\t%08x,%08x,%08x,%08x,%08x", Bits(v[0]), Bits(v[1]),
                      Bits(v[2]), Bits(v[3]), Bits(v[4]));
        s += buf;
    }
    return s;
}

std::string TexStr(ResTexObj obj) {
    std::string s;
    char buf[64];
    for (int i = GX_TEXMAP0; i <= GX_TEXMAP7; i++) {
        if (!obj.IsValid() || !obj.IsValidTexObj(static_cast<GXTexMapID>(i))) {
            s += "\t-";
            continue;
        }
        const GXTexObj* p = obj.GetTexObj(static_cast<GXTexMapID>(i));
        u32 image0 = ReadBE32(reinterpret_cast<const u8*>(p) + 0x8);
        u32 image3 = ReadBE32(reinterpret_cast<const u8*>(p) + 0xC) & 0xFFFFFF;
        std::string name = "?";
        for (size_t t = 0; t < sTextures.size(); t++) {
            if (sTextures[t].image3 == image3) {
                name = sTextures[t].name;
                break;
            }
        }
        std::snprintf(buf, sizeof buf, ",%08x", image0);
        s += "\t" + name + buf;
    }
    return s;
}

/** Material results; Simple reads the resource, the ScnMdl its copied buffers. */
std::vector<std::string> MaterialLines(ScnMdlSimple* pSimple, ScnMdl* pMdl, char kind) {
    std::vector<std::string> lines;
    ResMdl mdl = pSimple->GetResMdl();
    for (u32 m = 0; m < mdl.GetResMatNumEntries(); m++) {
        ResMat mat = mdl.GetResMat(m);
        char head[32];
        std::snprintf(head, sizeof head, "%c\t%u", kind, m);
        std::string s = head;
        if (pMdl != NULL) {
            ScnMdl::CopiedMatAccess access(pMdl, m);
            if (kind == 'C') {
                ResMatChan chan = access.GetResMatChan(false);
                ResMatTevColor tev = access.GetResMatTevColor(false);
                s += ColorStr(chan.IsValid() ? chan : mat.GetResMatChan(),
                              tev.IsValid() ? tev : mat.GetResMatTevColor());
            } else if (kind == 'T') {
                ResTexSrt srt = access.GetResTexSrt(false);
                s += SrtStr(srt.IsValid() ? srt : mat.GetResTexSrt());
            } else {
                ResTexObj obj = access.GetResTexObj(false);
                s += TexStr(obj.IsValid() ? obj : mat.GetResTexObj());
            }
        } else if (kind == 'C') {
            s += ColorStr(mat.GetResMatChan(), mat.GetResMatTevColor());
        } else if (kind == 'T') {
            s += SrtStr(mat.GetResTexSrt());
        } else {
            s += TexStr(mat.GetResTexObj());
        }
        lines.push_back(s);
    }
    return lines;
}

// --- Scene -------------------------------------------------------------------------

void Calc() {
    // RPGrpModelScene::Calc order (no UpdateFrame: the frame is set directly).
    spRoot->CalcWorld();
    spRoot->CalcMaterial();
    spRoot->CalcVtx();
    spRoot->CalcView();
}

bool SameWorld(ScnMdlSimple* a, ScnMdlSimple* b) {
    u32 n = a->GetResMdl().GetResMdlInfo().GetNumPosNrmMtx();
    return std::memcmp(a->GetWldMtxArray(), b->GetWldMtxArray(), n * sizeof(math::MTX34)) == 0 &&
           std::memcmp(a->GetWldMtxAttribArray(), b->GetWldMtxAttribArray(), n * sizeof(u32)) == 0;
}

void RunHeader(const Brres& f, const std::string& mdlName, const char* kind,
               const std::string& src, const std::string& anm, f32 frame) {
    std::printf("RUN\t%s\t%s\t%s\t%s\t%s\t%08x\n", f.path.c_str(), mdlName.c_str(), kind,
                src.c_str(), anm.c_str(), Bits(frame));
    sRuns++;
}

void Error(const Brres& f, const std::string& what) {
    std::fprintf(stderr, "ERROR: %s: %s\n", f.path.c_str(), what.c_str());
    disc_walk::Errors()++;
}

/** Positions both models in the root; only one is in it at a time. */
void Swap(ScnObj* pOut, ScnObj* pIn) {
    if (pOut != NULL) {
        spRoot->Remove(pOut);
    }
    spRoot->PushBack(pIn);
}

/**
 * One model; `sources` are the BRRES whose animations are tried on it: its own,
 * then the archive's BRRES without models (the game's animation files).
 */
void TestModel(const Brres& f, ResMdl mdl, const std::vector<const Brres*>& sources) {
    std::string mdlName = ResNameStr(mdl.ref().name, &mdl.ref());
    u32 size;

    ScnMdlSimple* pSimple = ScnMdlSimple::Construct(spAllocator, &size, mdl, 1);
    ScnMdl* pBuf = ScnMdl::Construct(spAllocator, &size, mdl,
                                     ScnMdl::ANM_MATCLR | ScnMdl::ANM_TEXSRT |
                                         ScnMdl::ANM_TEXPAT,
                                     1);
    if (pSimple == NULL || pBuf == NULL) {
        Error(f, mdlName + ": Construct failed");
        return;
    }
    sModels++;

    std::printf("MDL\t%s\t%s\t%u\t%u\t%u\t%u\n", f.path.c_str(), mdlName.c_str(),
                mdl.GetResNodeNumEntries(), mdl.GetResMdlInfo().GetNumPosNrmMtx(),
                pSimple->GetNumViewMtx(), mdl.GetResMatNumEntries());

    // Bind pose.
    Swap(NULL, pBuf);
    Calc();
    Swap(pBuf, pSimple);
    Calc();
    RunHeader(f, mdlName, "BIND", "-", "-", 0.0f);
    PrintMatrices(pSimple);
    if (!SameWorld(pSimple, pBuf)) {
        Error(f, mdlName + ": ScnMdl and ScnMdlSimple world matrices differ (bind pose)");
    }

    // CHR: RPGrpModelAnmG3D's AnmObjChrBlend with the animation at slot 0.
    AnmObjChrBlend* pBlend = AnmObjChrBlend::Construct(spAllocator, &size, mdl, 8);
    AnmObjChrBlend* pBlendBuf = AnmObjChrBlend::Construct(spAllocator, &size, mdl, 8);
    std::vector<AnmObjChrRes*> chrs, chrsBuf;
    std::vector<std::string> chrNames, chrSrcs;
    std::vector<f32> chrFrames;
    for (const Brres* pSrc : sources) {
    ResFile file(pSrc->pData);
    for (u32 a = 0; a < file.GetResAnmChrNumEntries(); a++) {
        ResAnmChr chr = file.GetResAnmChr(a);
        AnmObjChrRes* pRes = AnmObjChrRes::Construct(spAllocator, &size, chr, mdl, false);
        AnmObjChrRes* pResBuf = AnmObjChrRes::Construct(spAllocator, &size, chr, mdl, false);
        if (!pRes->Bind(mdl)) {
            pRes->Destroy();
            pResBuf->Destroy();
            continue;
        }
        pResBuf->Bind(mdl);
        chrs.push_back(pRes);
        chrsBuf.push_back(pResBuf);
        chrNames.push_back(ResNameStr(chr.ref().name, &chr.ref()));
        chrFrames.push_back(chr.GetNumFrame());
        chrSrcs.push_back(pSrc->path);
        sAnims++;
    }
    }
    pBlend->Bind(mdl);
    pBlendBuf->Bind(mdl);
    for (size_t a = 0; a < chrs.size(); a++) {
        pBlend->Attach(0, chrs[a]);
        pBlend->SetWeight(0, 1.0f);
        pBlendBuf->Attach(0, chrsBuf[a]);
        pBlendBuf->SetWeight(0, 1.0f);
        pSimple->SetAnmObj(pBlend);
        pBuf->SetAnmObj(pBlendBuf);
        for (f32 frame : Frames(static_cast<u32>(chrFrames[a]))) {
            chrs[a]->SetFrame(frame);
            chrsBuf[a]->SetFrame(frame);
            Swap(pSimple, pBuf);
            Calc();
            Swap(pBuf, pSimple);
            Calc();
            RunHeader(f, mdlName, "CHR", chrSrcs[a], chrNames[a], frame);
            PrintMatrices(pSimple);
            if (!SameWorld(pSimple, pBuf)) {
                Error(f, mdlName + ": ScnMdl and ScnMdlSimple world matrices differ (" +
                             chrNames[a] + ")");
            }
        }
        pBlend->Detach(0);
        pBlendBuf->Detach(0);
    }
    if (chrs.size() >= 2) {
        pBlend->Attach(0, chrs[0]);
        pBlend->Attach(1, chrs[1]);
        pBlend->SetWeight(0, 0.5f);
        pBlend->SetWeight(1, 0.5f);
        chrs[0]->SetFrame(1.5f);
        chrs[1]->SetFrame(2.25f);
        Calc();
        RunHeader(f, mdlName, "BLEND", chrSrcs[0] + "+" + chrSrcs[1],
                  chrNames[0] + "+" + chrNames[1], 1.5f);
        PrintMatrices(pSimple);
        pBlend->DetachAll();
    }
    pSimple->RemoveAnmObj(pBlend);
    pBuf->RemoveAnmObj(pBlendBuf);
    for (size_t a = 0; a < chrs.size(); a++) {
        chrs[a]->Destroy();
        chrsBuf[a]->Destroy();
    }
    pBlend->Destroy();
    pBlendBuf->Destroy();

    // Material animations: the ScnMdl's copied buffers, then the resource itself.
    std::vector<u8> snapshot(f.pData, f.pData + f.size);
    struct MatKind {
        const char* kind;
        char line;
    } kinds[] = {{"CLR", 'C'}, {"SRT", 'T'}, {"PAT", 'P'}};
    for (const MatKind& k : kinds) {
        for (const Brres* pSrc : sources) {
        ResFile file(pSrc->pData);
        u32 num = k.line == 'C'   ? file.GetResAnmClrNumEntries()
                  : k.line == 'T' ? file.GetResAnmTexSrtNumEntries()
                                  : file.GetResAnmTexPatNumEntries();
        for (u32 a = 0; a < num; a++) {
            AnmObj* pObj[2] = {NULL, NULL};
            std::string name;
            f32 numFrame = 0.0f;
            for (int i = 0; i < 2; i++) {
                if (k.line == 'C') {
                    ResAnmClr anm = file.GetResAnmClr(a);
                    name = ResNameStr(anm.ref().name, &anm.ref());
                    numFrame = anm.GetNumFrame();
                    pObj[i] = AnmObjMatClrRes::Construct(spAllocator, &size, anm, mdl, false);
                } else if (k.line == 'T') {
                    ResAnmTexSrt anm = file.GetResAnmTexSrt(a);
                    name = ResNameStr(anm.ref().name, &anm.ref());
                    numFrame = anm.GetNumFrame();
                    pObj[i] = AnmObjTexSrtRes::Construct(spAllocator, &size, anm, mdl, false);
                } else {
                    ResAnmTexPat anm = file.GetResAnmTexPat(a);
                    name = ResNameStr(anm.ref().name, &anm.ref());
                    numFrame = anm.GetNumFrame();
                    pObj[i] = AnmObjTexPatRes::Construct(spAllocator, &size, anm, mdl, false);
                }
            }
            if (!pObj[0]->Bind(mdl)) {
                pObj[0]->Destroy();
                pObj[1]->Destroy();
                continue;
            }
            pObj[1]->Bind(mdl);
            sAnims++;
            pBuf->SetAnmObj(pObj[0]);
            pSimple->SetAnmObj(pObj[1]);
            for (f32 frame : Frames(static_cast<u32>(numFrame))) {
                pObj[0]->SetFrame(frame);
                pObj[1]->SetFrame(frame);
                Swap(pSimple, pBuf);
                Calc();
                std::vector<std::string> buf = MaterialLines(pSimple, pBuf, k.line);
                Swap(pBuf, pSimple);
                Calc();
                std::vector<std::string> direct = MaterialLines(pSimple, NULL, k.line);
                std::memcpy(f.pData, snapshot.data(), f.size);
                RunHeader(f, mdlName, k.kind, pSrc->path, name, frame);
                for (size_t m = 0; m < buf.size(); m++) {
                    std::printf("%s\n", buf[m].c_str());
                    if (buf[m] != direct[m]) {
                        Error(f, mdlName + ": " + name + ": material " + std::to_string(m) +
                                     ": ScnMdl buffer and CalcMaterialDirectly differ");
                    }
                }
            }
            pBuf->RemoveAnmObj(pObj[0]);
            pSimple->RemoveAnmObj(pObj[1]);
            pObj[0]->Destroy();
            pObj[1]->Destroy();
        }
        }
    }

    spRoot->Remove(pSimple);
    pSimple->Destroy();
    pBuf->Destroy();
}

void TestFile(const Brres& f, const std::vector<Brres>& files) {
    ResFile file(f.pData);
    std::vector<const Brres*> sources = {&f};
    for (const Brres& other : files) {
        if (&other != &f && ResFile(other.pData).GetResMdlNumEntries() == 0) {
            sources.push_back(&other);
        }
    }
    for (u32 m = 0; m < file.GetResMdlNumEntries(); m++) {
        TestModel(f, file.GetResMdl(m), sources);
    }
    sFiles++;
}

// --- Archive walk (as anm_harness) -----------------------------------------------

void Collect(ARCHandle* pHandle, const std::string& prefix, const std::string& dir,
             std::vector<Brres>* pOut) {
    ARCDir arcDir;
    if (!ARCOpenDir(pHandle, dir.c_str(), &arcDir)) {
        return;
    }
    ARCDirEntry entry;
    while (ARCReadDir(&arcDir, &entry)) {
        std::string rel = (dir == "/" ? std::string("") : dir.substr(1) + "/") + entry.name;
        if (entry.isDir) {
            Collect(pHandle, prefix, "/" + rel, pOut);
            continue;
        }
        ARCFileInfo info;
        if (ARCFastOpen(pHandle, entry.entryNum, &info)) {
            u8* p = static_cast<u8*>(ARCGetStartAddrInMem(&info));
            u32 size = ARCGetLength(&info);
            if (size >= 4 && std::memcmp(p, "bres", 4) == 0) {
                Brres b = {prefix + "/" + rel, p, size};
                pOut->push_back(b);
            }
            ARCClose(&info);
        }
    }
    ARCCloseDir(&arcDir);
}

void TestArchive(const std::string& path, const ARCHandle& handle) {
    std::vector<Brres> files;
    Collect(const_cast<ARCHandle*>(&handle), path, "/", &files);
    if (files.empty()) {
        return;
    }

    std::vector<int> bound(files.size());
    for (size_t i = 0; i < files.size(); i++) {
        ResFile file(files[i].pData);
        if (!file.CheckRevision()) {
            Error(files[i], "CheckRevision failed");
        }
        file.Init();
        bound[i] = file.Bind(file);
    }
    for (size_t i = 0; i < files.size(); i++) {
        ResFile file(files[i].pData);
        for (size_t j = 0; j < files.size() && !bound[i]; j++) {
            if (j != i) {
                file.Bind(ResFile(files[j].pData));
                bound[i] = file.Bind(file);
            }
        }
    }

    sTextures.clear();
    for (size_t i = 0; i < files.size(); i++) {
        ResFile file(files[i].pData);
        for (u32 t = 0; t < file.GetResTexNumEntries(); t++) {
            ResTex tex = file.GetResTex(t);
            GXTexObj obj;
            GXInitTexObj(&obj, const_cast<void*>(tex.GetTexData()), 8, 8, GX_TF_I8, GX_CLAMP,
                         GX_CLAMP, GX_FALSE);
            TexKey key = {ReadBE32(reinterpret_cast<const u8*>(&obj) + 0xC) & 0xFFFFFF,
                          ResNameStr(tex.ref().name, &tex.ref())};
            sTextures.push_back(key);
        }
    }

    for (size_t i = 0; i < files.size(); i++) {
        TestFile(files[i], files);
    }
}

void Leaf(const std::string& path, u8* p, u32 size) {
    if (size >= 4 && std::memcmp(p, "bres", 4) == 0 && path.find('/') == std::string::npos) {
        std::fprintf(stderr, "ERROR: %s: top-level BRRES not handled\n", path.c_str());
        disc_walk::Errors()++;
    }
}

const u32 kHeapSize = 32 * 1024 * 1024;

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> [path ...]\n", argv[0]);
        return 2;
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    ut::LC::Disable();

    void* pHeapMem = PortArenaAlloc(kHeapSize, 32);
    spHeap = MEMCreateExpHeapEx(pHeapMem, kHeapSize, 0);
    MEMInitAllocatorForExpHeap(spAllocator, spHeap, 32);
    u32 freeAtStart = MEMGetAllocatableSizeForExpHeap(spHeap);

    u32 size;
    spRoot = ScnRoot::Construct(spAllocator, &size, 0x400, 0x400, 16, 8);
    Camera cam = spRoot->GetCamera(0);
    cam.SetPosition(math::VEC3(30.0f, 120.0f, 400.0f));
    Camera::PostureInfo posture;
    posture.tp = Camera::POSTURE_LOOKAT;
    posture.cameraUp = math::VEC3(0.0f, 1.0f, 0.0f);
    posture.cameraTarget = math::VEC3(0.0f, 40.0f, 0.0f);
    posture.cameraRotate = math::VEC3(0.0f, 0.0f, 0.0f);
    posture.cameraTwist = 0.0f;
    cam.SetPosture(posture);
    spRoot->SetCurrentCamera(0);
    math::MTX34 camMtx;
    cam.GetCameraMtx(&camMtx);
    std::printf("CAM");
    PrintF(&camMtx._00, 12);
    std::printf("\n");

    std::vector<std::string> paths;
    if (argc > 2) {
        paths.assign(argv + 2, argv + argc);
    } else {
        disc_walk::ListDisc(argv[1], "", &paths);
    }

    disc_walk::Visitor visitor = {Leaf, TestArchive};
    disc_walk::WalkDisc(visitor, paths);

    spRoot->Destroy();
    if (MEMGetAllocatableSizeForExpHeap(spHeap) != freeAtStart) {
        std::fprintf(stderr, "ERROR: heap not empty after teardown (%u of %u free)\n",
                     MEMGetAllocatableSizeForExpHeap(spHeap), freeAtStart);
        disc_walk::Errors()++;
    }

    std::fprintf(stderr,
                 "mdl_harness: %d BRRES, %d models, %d animations, %d runs; %d errors "
                 "(pointer size %zu)\n",
                 sFiles, sModels, sAnims, sRuns, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
