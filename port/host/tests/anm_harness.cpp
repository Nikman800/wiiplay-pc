/**
 * g3d animation harness (port/docs/HOST_BUILD.md §17).
 *
 * Walks the disc like g3d_harness (disc_walk.h), sets every BRRES up as
 * RPGrpModelResManager::InternalCreateData does (CheckRevision, Init,
 * Bind(itself), ResAnmScn::Bind(itself); then the archive's other BRRES for
 * files that stay unresolved, as g3d_harness does), and evaluates every
 * animation through the real decompiled readers at a set of frames:
 *
 *   CHR  ResAnmChr::GetAnmResult(ChrAnmResult*, node, frame)
 *   CLR  ResAnmClr::GetAnmResult(ClrAnmResult*, material, frame)
 *   SRT  ResAnmTexSrt::GetAnmResult(TexSrtAnmResult*, material, frame)
 *   PAT  ResAnmTexPat::GetAnmResult(TexPatAnmResult*, material, frame)
 *   AMB  ResAnmScn::GetResAnmAmbLightByRefNumber(i).GetAnmResult(...)
 *   LIT  ResAnmScn::GetResAnmLightByRefNumber(i).GetAnmResult(...)
 *   CAM  ResAnmScn::GetResAnmCameraByRefNumber(i).GetAnmResult(...)
 *
 * (VIS0 and SHP0 are not on the disc; ResAnmFog has no reader in Wii Play.)
 * Each result struct is zeroed before the call and printed whole: floats as
 * their bit patterns, PAT0 textures as the bound ResTex's name.
 * tests/test_anims.py evaluates the same from the file bytes with its own
 * f32 model of the formulas (located with port/assets/tools/fmt_brres_anim.py).
 */
#include <nw4r/g3d.h>

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

int sFiles = 0, sAnims = 0, sResults = 0;

struct Brres {
    std::string path;
    u8* pData;
    u32 size;
};

u32 Bits(f32 v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    return u;
}

std::string ResNameStr(s32 ofsName, const void* pBase) {
    return ofsName != 0 ? std::string(static_cast<const char*>(pBase) + ofsName) : "";
}

/** Frames each animation is evaluated at (before, at and after its range). */
std::vector<f32> Frames(u32 numFrame) {
    f32 n = static_cast<f32>(numFrame);
    std::vector<f32> frames = {-1.5f, 0.0f, 0.25f, 1.0f, 1.5f};
    for (int k = 1; k <= 6; k++) {
        frames.push_back(n * k / 7.0f + 0.375f);
        frames.push_back(static_cast<f32>(static_cast<int>(n * k / 7.0f)));
    }
    frames.push_back(n - 1.0f);
    frames.push_back(n - 0.5f);
    frames.push_back(n);
    frames.push_back(n + 2.5f);
    return frames;
}

void PrintF(const f32* p, int n) {
    for (int i = 0; i < n; i++) {
        std::printf("\t%08x", Bits(p[i]));
    }
}

void EvalFile(const Brres& f) {
    ResFile file(f.pData);

    for (u32 a = 0; a < file.GetResAnmChrNumEntries(); a++) {
        ResAnmChr anm = file.GetResAnmChr(a);
        std::string name = ResNameStr(anm.ref().name, &anm.ref());
        for (f32 frame : Frames(anm.ref().info.numFrame)) {
            for (int i = 0; i < anm.GetNumNode(); i++) {
                ChrAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                anm.GetAnmResult(&r, static_cast<u32>(i), frame);
                std::printf("CHR\t%s\t%s\t%d\t%08x\t%08x", f.path.c_str(), name.c_str(), i,
                            Bits(frame), r.flags);
                PrintF(&r.s.x, 3);
                PrintF(&r.rawR.x, 3);
                PrintF(&r.rt.m[0][0], 12);
                std::printf("\n");
                sResults++;
            }
        }
        sAnims++;
    }

    for (u32 a = 0; a < file.GetResAnmClrNumEntries(); a++) {
        ResAnmClr anm = file.GetResAnmClr(a);
        std::string name = ResNameStr(anm.ref().name, &anm.ref());
        for (f32 frame : Frames(anm.ref().info.numFrame)) {
            for (int i = 0; i < anm.GetNumMaterial(); i++) {
                ClrAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                anm.GetAnmResult(&r, static_cast<u32>(i), frame);
                std::printf("CLR\t%s\t%s\t%d\t%08x\t%08x", f.path.c_str(), name.c_str(), i,
                            Bits(frame), r.bRgbaExist);
                for (int k = 0; k < ClrAnmResult::CLA_MAX; k++) {
                    std::printf("\t%08x:%08x", r.rgba[k], r.rgbaMask[k]);
                }
                std::printf("\n");
                sResults++;
            }
        }
        sAnims++;
    }

    for (u32 a = 0; a < file.GetResAnmTexSrtNumEntries(); a++) {
        ResAnmTexSrt anm = file.GetResAnmTexSrt(a);
        std::string name = ResNameStr(anm.ref().name, &anm.ref());
        for (f32 frame : Frames(anm.ref().info.numFrame)) {
            for (int i = 0; i < anm.GetNumMaterial(); i++) {
                TexSrtAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                anm.GetAnmResult(&r, static_cast<u32>(i), frame);
                std::printf("SRT\t%s\t%s\t%d\t%08x\t%08x\t%08x\t%u", f.path.c_str(),
                            name.c_str(), i, Bits(frame), r.flags, r.indFlags,
                            static_cast<u32>(r.texMtxMode));
                for (int k = 0; k < TexSrtAnmResult::NUM_OF_TEX_MTX; k++) {
                    const TexSrt& t = r.srt[k];
                    std::printf("\t%08x,%08x,%08x,%08x,%08x", Bits(t.Su), Bits(t.Sv), Bits(t.R),
                                Bits(t.Tu), Bits(t.Tv));
                }
                std::printf("\n");
                sResults++;
            }
        }
        sAnims++;
    }

    for (u32 a = 0; a < file.GetResAnmTexPatNumEntries(); a++) {
        ResAnmTexPat anm = file.GetResAnmTexPat(a);
        std::string name = ResNameStr(anm.ref().name, &anm.ref());
        for (f32 frame : Frames(anm.ref().info.numFrame)) {
            for (int i = 0; i < anm.GetNumMaterial(); i++) {
                TexPatAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                anm.GetAnmResult(&r, static_cast<u32>(i), frame);
                std::printf("PAT\t%s\t%s\t%d\t%08x\t%02x\t%02x", f.path.c_str(), name.c_str(), i,
                            Bits(frame), r.bTexExist, r.bPlttExist);
                for (int k = 0; k < TexPatAnmResult::NUM_OF_ANMS; k++) {
                    std::string tex = "-", pltt = "-";
                    if (r.tex[k].IsValid()) {
                        tex = ResNameStr(r.tex[k].ref().name, &r.tex[k].ref());
                    }
                    if (r.pltt[k].IsValid()) {
                        pltt = ResNameStr(r.pltt[k].ref().name, &r.pltt[k].ref());
                    }
                    std::printf("\t%s,%s", tex.c_str(), pltt.c_str());
                }
                std::printf("\n");
                sResults++;
            }
        }
        sAnims++;
    }

    for (u32 a = 0; a < file.GetResAnmScnNumEntries(); a++) {
        ResAnmScn scn = file.GetResAnmScn(a);
        std::string name = ResNameStr(scn.ref().name, &scn.ref());
        for (f32 frame : Frames(scn.ref().info.numFrame)) {
            for (u32 i = 0; i < scn.GetResAnmAmbLightMaxRefNumber(); i++) {
                ResAnmAmbLight amb = scn.GetResAnmAmbLightByRefNumber(i);
                if (!amb.IsValid()) {
                    std::printf("AMB\t%s\t%s\t%u\t%08x\tinvalid\n", f.path.c_str(), name.c_str(),
                                i, Bits(frame));
                    continue;
                }
                AmbLightAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                amb.GetAnmResult(&r, frame);
                std::printf("AMB\t%s\t%s\t%u\t%08x\t%08x\t%08x\n", f.path.c_str(), name.c_str(), i,
                            Bits(frame), r.flags, r.color);
                sResults++;
            }
            for (u32 i = 0; i < scn.GetResAnmLightMaxRefNumber(); i++) {
                ResAnmLight light = scn.GetResAnmLightByRefNumber(i);
                if (!light.IsValid()) {
                    std::printf("LIT\t%s\t%s\t%u\t%08x\tinvalid\n", f.path.c_str(), name.c_str(),
                                i, Bits(frame));
                    continue;
                }
                LightAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                light.GetAnmResult(&r, frame);
                std::printf("LIT\t%s\t%s\t%u\t%08x\t%08x\t%08x", f.path.c_str(), name.c_str(), i,
                            Bits(frame), r.flags, r.specIdx);
                PrintF(&r.pos.x, 3);
                PrintF(&r.aim.x, 3);
                std::printf("\t%08x\t%u", static_cast<u32>(r.color),
                            static_cast<u32>(r.distFunc));
                PrintF(&r.refDistance, 2);
                std::printf("\t%u", static_cast<u32>(r.spotFunc));
                PrintF(&r.cutoff, 1);
                std::printf("\t%08x", static_cast<u32>(r.specColor));
                PrintF(&r.shininess, 1);
                std::printf("\n");
                sResults++;
            }
            for (u32 i = 0; i < scn.GetResAnmCameraMaxRefNumber(); i++) {
                ResAnmCamera cam = scn.GetResAnmCameraByRefNumber(i);
                if (!cam.IsValid()) {
                    std::printf("CAM\t%s\t%s\t%u\t%08x\tinvalid\n", f.path.c_str(), name.c_str(),
                                i, Bits(frame));
                    continue;
                }
                CameraAnmResult r;
                std::memset(static_cast<void*>(&r), 0, sizeof r);
                cam.GetAnmResult(&r, frame);
                std::printf("CAM\t%s\t%s\t%u\t%08x\t%08x\t%u", f.path.c_str(), name.c_str(), i,
                            Bits(frame), r.flags, static_cast<u32>(r.projType));
                PrintF(&r.pos.x, 3);
                PrintF(&r.aspect, 3);
                PrintF(&r.aim.aim.x, 4);
                PrintF(&r.perspFovy, 1);
                std::printf("\n");
                sResults++;
            }
        }
        sAnims++;
    }
}

// --- Archive walk (as g3d_harness) ---------------------------------------------

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
            std::fprintf(stderr, "ERROR: %s: CheckRevision failed\n", files[i].path.c_str());
            disc_walk::Errors()++;
        }
        file.Init();
        bound[i] = file.Bind(file);
        for (int s = 0; s < static_cast<int>(file.GetResAnmScnNumEntries()); s++) {
            ResAnmScn scn = file.GetResAnmScn(s);
            scn.Bind(scn);
        }
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

    for (size_t i = 0; i < files.size(); i++) {
        EvalFile(files[i]);
        sFiles++;
    }
}

void Leaf(const std::string& path, u8* p, u32 size) {
    if (size >= 4 && std::memcmp(p, "bres", 4) == 0 && path.find('/') == std::string::npos) {
        std::fprintf(stderr, "ERROR: %s: top-level BRRES not handled\n", path.c_str());
        disc_walk::Errors()++;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> [path ...]\n", argv[0]);
        return 2;
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    std::vector<std::string> paths;
    if (argc > 2) {
        paths.assign(argv + 2, argv + argc);
    } else {
        disc_walk::ListDisc(argv[1], "", &paths);
    }

    disc_walk::Visitor visitor = {Leaf, TestArchive};
    disc_walk::WalkDisc(visitor, paths);

    std::fprintf(stderr,
                 "anm_harness: %d BRRES, %d animations, %d results; %d errors (pointer size "
                 "%zu)\n",
                 sFiles, sAnims, sResults, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
