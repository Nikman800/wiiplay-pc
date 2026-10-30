/**
 * g3d resource harness (port/docs/HOST_BUILD.md §11, POINTERS.md step 5).
 *
 * Walks the disc like the other harnesses (disc_walk.h). For every mounted
 * archive it collects the BRRES files (magic "bres") and sets each one up as
 * RPGrpModelResManager::InternalCreateData does:
 *
 *   ResFile::CheckRevision, ResFile::Init, ResFile::Bind(itself), and
 *   ResAnmScn::Bind(itself) for each SCN0
 *
 * then binds the files whose self-bind left textures or palettes unresolved
 * to the other BRRES of the same archive (ResFile::Bind(other), the API the
 * game's model code offers for shared texture files).
 *
 * It prints one line per file, root group, model, texture, palette and
 * animation (read back through the decompiled ResFile/ResMdl/ResTex/...
 * accessors); tests/test_models.py compares them with
 * port/assets/tools/fmt_brres_model.py and its own animation reader.
 *
 * It also checks that each set-up file is the Wii's in-memory image:
 * - every byte that changed lies in a slot that set-up writes (shape
 *   pre-primitive array commands, material pTexData/pPlttData, GX texture and
 *   TLUT objects, PAT0 texture/palette arrays, SCN0 light-set ids);
 * - each shape's CP ARRAYBASE command holds the emulated physical address of
 *   its vertex array, big-endian, and ARRAYSTRIDE the array's stride;
 * - each bound material slot holds the emulated address of the ResTexData /
 *   ResPlttData, and the GXTexObj / GXTlutObj words hold the image / palette
 *   address, size and format as the Wii's GXInitTexObj / GXInitTlutObj store
 *   them (big-endian);
 * - each bound PAT0 slot holds the emulated address of the named texture.
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

int sFiles = 0, sModels = 0, sTextures = 0, sPalettes = 0, sAnims = 0;
int sSelfBound = 0, sCrossBound = 0, sUnbound = 0;
int sShapes = 0, sArrayCmds = 0, sTexInfos = 0, sTexObjs = 0, sTlutObjs = 0;
int sPatSlots = 0, sChangedBytes = 0;

struct Brres {
    std::string path;
    u8* pData;
    u32 size;
    std::vector<u8> orig;
};

void Error(const std::string& path, const char* what) {
    std::fprintf(stderr, "ERROR: %s: %s\n", path.c_str(), what);
    disc_walk::Errors()++;
}

u32 Load32(const void* p) {
    return port_load_be32(p);
}

u32 Phys(const void* p) {
    return port_vaddr(p) & 0x3FFFFFFF;
}

std::string DicName(const ResDicData* pDic, u32 i) {
    s32 ofs = pDic->data[i + 1].ofsString;
    return ofs != 0 ? std::string(reinterpret_cast<const char*>(pDic) + ofs) : "";
}

std::string ResNameStr(s32 ofsName, const void* pBase) {
    return ofsName != 0 ? std::string(static_cast<const char*>(pBase) + ofsName) : "";
}

void Mark(std::vector<bool>& allowed, const Brres& f, const void* p, u32 size) {
    u32 off = static_cast<u32>(static_cast<const u8*>(p) - f.pData);
    for (u32 i = 0; i < size && off + i < f.size; i++) {
        allowed[off + i] = true;
    }
}

// --- Byte-level checks of one set-up file -------------------------------

void CheckShape(const Brres& f, ResMdl mdl, ResShp shp, std::vector<bool>& allowed) {
    const u8* pArray = shp.GetResShpPrePrim().ref().dl.array[0];
    Mark(allowed, f, pArray, sizeof(shp.GetResShpPrePrim().ref().dl.array));
    sShapes++;

    const ResShpData& r = shp.ref();
    for (int attr = GX_VA_POS; attr < GX_POS_MTX_ARRAY; attr++) {
        const void* pBase = NULL;
        u8 stride = 0;
        int id;
        if (attr == GX_VA_POS) {
            id = r.idVtxPosition;
            if (id >= 0) mdl.GetResVtxPos(id).GetArray(&pBase, &stride);
        } else if (attr == GX_VA_NRM) {
            id = r.idVtxNormal;
            if (id >= 0) mdl.GetResVtxNrm(id).GetArray(&pBase, &stride);
        } else if (attr < GX_VA_TEX0) {
            id = r.idVtxColor[attr - GX_VA_CLR0];
            if (id >= 0) mdl.GetResVtxClr(id).GetArray(&pBase, &stride);
        } else {
            id = r.idVtxTexCoord[attr - GX_VA_TEX0];
            if (id >= 0) mdl.GetResVtxTexCoord(id).GetArray(&pBase, &stride);
        }
        if (id < 0) {
            continue;
        }
        const u8* pCmd = shp.GetResShpPrePrim().ref().dl.array[attr - GX_VA_POS];
        u32 cpAttr = attr - GX_VA_POS;
        // CP command: 0x08, register, value (ResWriteCPCmd).
        if (pCmd[0] != 0x08 || pCmd[1] != 0xA0 + cpAttr || Load32(pCmd + 2) != Phys(pBase) ||
            pCmd[6] != 0x08 || pCmd[7] != 0xB0 + cpAttr || Load32(pCmd + 8) != stride) {
            Error(f.path, "CP ARRAYBASE/ARRAYSTRIDE command does not hold the array address");
        }
        sArrayCmds++;
    }
}

void CheckMaterial(const Brres& f, ResMat mat, std::vector<bool>& allowed) {
    ResMatData& rMat = mat.ref();
    Mark(allowed, f, &rMat.texObjData, sizeof(rMat.texObjData));
    Mark(allowed, f, &rMat.tlutObjData, sizeof(rMat.tlutObjData));

    for (u32 i = 0; i < mat.GetNumResTexPlttInfo(); i++) {
        ResTexPlttInfo info = mat.GetResTexPlttInfo(i);
        ResTexPlttInfoData& r = info.ref();
        Mark(allowed, f, &r.pTexData, 8);
        sTexInfos++;

        if (r.pTexData != NULL) {
            const ResTexData* pTex = r.pTexData;
            const u8* pSlot = reinterpret_cast<const u8*>(&r.pTexData);
            if (Load32(pSlot) != port_vaddr(pTex) || std::memcmp(pTex->header.kind, "TEX0", 4) != 0) {
                Error(f.path, "pTexData slot is not the emulated address of a TEX0");
            }
            const u8* pObj = reinterpret_cast<const u8*>(
                mat.GetResTexObj().GetTexObj(static_cast<GXTexMapID>(r.mapID)));
            const u8* pImage = reinterpret_cast<const u8*>(pTex) + pTex->toTexData;
            u32 fmt = Load32(&pTex->fmt);
            u32 image0 = Load32(pObj + 0x8);
            if ((Load32(pObj + 0xC) & 0xFFFFFF) != Phys(pImage) >> 5 ||
                (image0 & 0x3FF) + 1 != pTex->width ||
                ((image0 >> 10) & 0x3FF) + 1 != pTex->height ||
                ((image0 >> 20) & 0xF) != (fmt & 0xF) || Load32(pObj + 0x14) != fmt ||
                Load32(pObj + 0x10) != 0 || (pObj[0x1F] & 1) != (pTex->mipmap_level > 1 ? 1 : 0)) {
                if (getenv("G3D_DEBUG")) {
                    for (int k = 0; k < 0x20; k += 4) std::fprintf(stderr, " %08x", Load32(pObj + k));
                    std::fprintf(stderr, " | phys %08x w %u h %u fmt %u mip %u\n", Phys(pImage),
                                 (u32)pTex->width, (u32)pTex->height, fmt, (u32)pTex->mipmap_level);
                }
                Error(f.path, "GXTexObj does not hold the texture's address, size and format");
            }
            if ((Load32(&mat.ref().texObjData.flagUsedTexMapID) & (1u << r.mapID)) == 0) {
                Error(f.path, "texture map not marked used");
            }
            sTexObjs++;
        }
        if (r.pPlttData != NULL) {
            const ResPlttData* pPltt = r.pPlttData;
            const u8* pSlot = reinterpret_cast<const u8*>(&r.pPlttData);
            const u8* pObj = reinterpret_cast<const u8*>(
                mat.GetResTlutObj().GetTlut(static_cast<GXTlut>(r.mapID)));
            const u8* pLut = reinterpret_cast<const u8*>(pPltt) + pPltt->toPlttData;
            if (Load32(pSlot) != port_vaddr(pPltt) || std::memcmp(pPltt->header.kind, "PLT0", 4) != 0 ||
                Load32(pObj) != (Load32(&pPltt->fmt) & 3) << 10 ||
                Load32(pObj + 4) != ((Phys(pLut) >> 5) | 0x64000000) ||
                port_load_be16(pObj + 8) != pPltt->numEntries) {
                Error(f.path, "GXTlutObj does not hold the palette's address, format and size");
            }
            sTlutObjs++;
        }
    }
}

void CheckFile(Brres& f) {
    ResFile file(f.pData);
    std::vector<bool> allowed(f.size, false);

    for (u32 m = 0; m < file.GetResMdlNumEntries(); m++) {
        ResMdl mdl = file.GetResMdl(m);
        for (u32 i = 0; i < mdl.GetResShpNumEntries(); i++) {
            CheckShape(f, mdl, mdl.GetResShp(i), allowed);
        }
        for (u32 i = 0; i < mdl.GetResMatNumEntries(); i++) {
            CheckMaterial(f, mdl.GetResMat(i), allowed);
        }
    }

    for (u32 a = 0; a < file.GetResAnmTexPatNumEntries(); a++) {
        ResAnmTexPat pat = file.GetResAnmTexPat(a);
        const ResAnmTexPatData& r = pat.ref();
        const u8* pBase = reinterpret_cast<const u8*>(&r);
        const be_s32* pTexNames = reinterpret_cast<const be_s32*>(pBase + r.toTexNameArray);
        const u8* pTexSlots = pBase + r.toResTexArray;
        const u8* pPlttSlots = pBase + r.toResPlttArray;
        Mark(allowed, f, pTexSlots, 4 * r.info.numTexture);
        Mark(allowed, f, pPlttSlots, 4 * r.info.numPalette);
        for (u32 i = 0; i < r.info.numTexture; i++) {
            u32 slot = Load32(pTexSlots + 4 * i);
            // The name offsets are relative to the start of the array; a
            // ResName starts 4 bytes before the string (NW4R_G3D_OFS_TO_RESNAME).
            ResName name(reinterpret_cast<const u8*>(pTexNames) + pTexNames[i] - 4);
            ResTex tex = file.GetResTex(name);
            if (slot != (tex.IsValid() ? port_vaddr(tex.ptr()) : 0)) {
                Error(f.path, "PAT0 texture slot does not hold the named texture's address");
            }
            sPatSlots++;
        }
        for (u32 i = 0; i < r.info.numPalette; i++) {
            sPatSlots++;
        }
    }

    for (u32 s = 0; s < file.GetResAnmScnNumEntries(); s++) {
        ResAnmScn scn = file.GetResAnmScn(s);
        for (u32 i = 0; i < scn.GetResLightSetNumEntries(); i++) {
            ResLightSetData& r = scn.GetResLightSet(i).ref();
            Mark(allowed, f, &r.ambLightId, sizeof(r.ambLightId));
            Mark(allowed, f, &r.lightId, sizeof(r.lightId));
        }
    }

    int bad = 0;
    for (u32 i = 0; i < f.size; i++) {
        if (f.pData[i] != f.orig[i]) {
            sChangedBytes++;
            if (!allowed[i] && bad++ == 0) {
                std::fprintf(stderr, "ERROR: %s: byte 0x%x changed outside a set-up slot\n",
                             f.path.c_str(), i);
                disc_walk::Errors()++;
            }
        }
    }
}

// --- Report lines ----------------------------------------------------------

void PrintFile(const Brres& f, bool rev, bool selfBind, bool finalBind) {
    ResFile file(f.pData);
    const ResDicData* pTop = &file.ref().dict.topLevel;
    std::printf("F\t%s\t%u\t%d\t%d\t%d\n", f.path.c_str(), static_cast<u32>(pTop->numData),
                rev ? 1 : 0, selfBind ? 1 : 0, finalBind ? 1 : 0);
    for (u32 g = 0; g < pTop->numData; g++) {
        const ResDicData* pGroup = static_cast<const ResDicData*>(ResDic(const_cast<ResDicData*>(pTop))[static_cast<int>(g)]);
        std::string names;
        for (u32 i = 0; i < pGroup->numData; i++) {
            names += (i ? "," : "") + DicName(pGroup, i);
        }
        std::printf("G\t%s\t%s\t%u\t%s\n", f.path.c_str(), DicName(pTop, g).c_str(),
                    static_cast<u32>(pGroup->numData), names.c_str());
    }

    for (u32 i = 0; i < file.GetResMdlNumEntries(); i++) {
        ResMdl mdl = file.GetResMdl(i);
        const ResMdlData& r = mdl.ref();
        std::printf("M\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n", f.path.c_str(),
                    ResNameStr(r.name, &r).c_str(), mdl.GetRevision(),
                    mdl.GetResMatNumEntries(), mdl.GetResShpNumEntries(), mdl.GetResNodeNumEntries(),
                    mdl.GetResVtxPosNumEntries(), mdl.GetResVtxNrmNumEntries(),
                    mdl.GetResVtxClrNumEntries(), mdl.GetResVtxTexCoordNumEntries(),
                    r.toResTevDic != 0 ? ResDic(const_cast<u8*>(reinterpret_cast<const u8*>(&r)) + r.toResTevDic).GetNumData() : 0,
                    mdl.GetResTexPlttInfoOffsetFromTexNameNumEntries(),
                    static_cast<u32>(r.info.numViewMtx), static_cast<u32>(r.info.scaling_rule));
        sModels++;
    }
    for (u32 i = 0; i < file.GetResTexNumEntries(); i++) {
        ResTex tex = file.GetResTex(i);
        const ResTexData& r = tex.ref();
        void* pImage;
        u16 w, h;
        GXTexFmt fmt;
        f32 minLod, maxLod;
        GXBool mip;
        tex.GetTexObjParam(&pImage, &w, &h, &fmt, &minLod, &maxLod, &mip);
        std::printf("T\t%s\t%s\t%u\t%u\t%u\t0x%x\t%u\t%d\t%u\t%g\t%g\n", f.path.c_str(),
                    ResNameStr(r.name, &r).c_str(), tex.GetRevision(), w, h,
                    static_cast<u32>(r.fmt), static_cast<u32>(r.mipmap_level),
                    tex.IsCIFmt() ? 1 : 0,
                    static_cast<u32>(static_cast<const u8*>(pImage) - reinterpret_cast<const u8*>(&r)),
                    minLod, maxLod);
        sTextures++;
    }
    for (u32 i = 0; i < file.GetResPlttNumEntries(); i++) {
        ResPltt pltt = file.GetResPltt(i);
        const ResPlttData& r = pltt.ref();
        std::printf("P\t%s\t%s\t%u\t0x%x\t%u\n", f.path.c_str(), ResNameStr(r.name, &r).c_str(),
                    pltt.GetRevision(), static_cast<u32>(pltt.GetFmt()), pltt.GetNumEntries());
        sPalettes++;
    }

#define ANIM(KIND, GETN, GET, DICFIELD, COUNT)                                       \
    for (u32 i = 0; i < file.GETN(); i++) {                                          \
        auto a = file.GET(i);                                                        \
        const auto& r = a.ref();                                                     \
        std::printf("A\t%s\t%s\t%s\t%u\t%d\t%u\t%d\t%u\n", f.path.c_str(), KIND,       \
                    ResNameStr(r.name, &r).c_str(), a.GetRevision(),                 \
                    static_cast<int>(r.info.numFrame), static_cast<u32>(r.info.policy), \
                    static_cast<int>(COUNT),                                         \
                    ResDic(const_cast<u8*>(reinterpret_cast<const u8*>(&r)) + r.DICFIELD).GetNumData()); \
        sAnims++;                                                                    \
    }
    ANIM("CHR0", GetResAnmChrNumEntries, GetResAnmChr, toChrDataDic, a.GetNumNode())
    ANIM("VIS0", GetResAnmVisNumEntries, GetResAnmVis, toVisDataDic, r.info.numNode)
    ANIM("CLR0", GetResAnmClrNumEntries, GetResAnmClr, toClrDataDic, a.GetNumMaterial())
    ANIM("PAT0", GetResAnmTexPatNumEntries, GetResAnmTexPat, toTexPatDataDic, a.GetNumMaterial())
    ANIM("SRT0", GetResAnmTexSrtNumEntries, GetResAnmTexSrt, toTexSrtDataDic, a.GetNumMaterial())
    ANIM("SHP0", GetResAnmShpNumEntries, GetResAnmShp, toShpDataDic, r.info.numVtxName)
    ANIM("SCN0", GetResAnmScnNumEntries, GetResAnmScn, toScnTopLevelDic,
         a.GetResLightSetNumEntries())
#undef ANIM
}

// --- Archive walk -------------------------------------------------------------

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
                Brres b = {prefix + "/" + rel, p, size, std::vector<u8>(p, p + size)};
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

    std::vector<int> rev(files.size()), selfBind(files.size()), finalBind(files.size());

    // RPGrpModelResManager::InternalCreateData, in archive order.
    for (size_t i = 0; i < files.size(); i++) {
        ResFile file(files[i].pData);
        rev[i] = file.CheckRevision();
        file.Init();
        selfBind[i] = file.Bind(file);
        for (int s = 0; s < static_cast<int>(file.GetResAnmScnNumEntries()); s++) {
            ResAnmScn scn = file.GetResAnmScn(s);
            scn.Bind(scn);
        }
        finalBind[i] = selfBind[i];
    }

    // Shared textures: bind what is still unresolved to the archive's other BRRES.
    for (size_t i = 0; i < files.size(); i++) {
        if (finalBind[i]) {
            sSelfBound++;
            continue;
        }
        ResFile file(files[i].pData);
        for (size_t j = 0; j < files.size() && !finalBind[i]; j++) {
            if (j != i) {
                file.Bind(ResFile(files[j].pData));
                finalBind[i] = file.Bind(file);  // true once every slot is bound
            }
        }
        if (finalBind[i]) {
            sCrossBound++;
        } else {
            sUnbound++;
        }
    }

    for (size_t i = 0; i < files.size(); i++) {
        CheckFile(files[i]);
        PrintFile(files[i], rev[i], selfBind[i], finalBind[i]);
        sFiles++;
    }
}

void Leaf(const std::string& path, u8* p, u32 size) {
    if (size >= 4 && std::memcmp(p, "bres", 4) == 0 && path.find('/') == std::string::npos) {
        Error(path, "top-level BRRES not handled");
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
                 "g3d_harness: %d BRRES (bound: %d by themselves, %d with the archive's other "
                 "BRRES, %d unresolved), %d models, %d textures, %d palettes, %d animations; "
                 "set-up checks: %d shapes (%d array commands), %d texture infos (%d tex objs, "
                 "%d tlut objs), %d PAT0 slots, %d bytes changed; %d errors (pointer size %zu)\n",
                 sFiles, sSelfBound, sCrossBound, sUnbound, sModels, sTextures, sPalettes, sAnims,
                 sShapes, sArrayCmds, sTexInfos, sTexObjs, sTlutObjs, sPatSlots, sChangedBytes,
                 disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
