/**
 * RFL_Res.dat harness (RVLFaceLib, port/docs/HOST_BUILD.md §16).
 *
 * Walks the disc (disc_walk.h) and hands Common/Kokeshi/common.carc's
 * RFL_Res.dat, in place inside its mounted archive, to the real RVLFaceLib
 * (src/RVLFaceLib, all of it built for the host):
 *
 *   RFLInitRes(work, res, size, FALSE), as RPSysKokeshiManager does. There is
 *   no NAND on the host (tests/rfl_host.c): the Mii database open reports
 *   NAND_RESULT_NOEXISTS, which RFLiBootLoadDatabaseAsync treats as an empty
 *   database, and the boot load goes on to RFLiLoadResourceHeaderAsync, which
 *   reads the cached resource's header (parseOnmemoryRes_).
 *
 * Then every file of every part archive is read through the library:
 *
 *   textures (eye, eyebrow, mouth, mustache, mole)  RFLiGetTexSize,
 *       RFLiLoadTexture, then RFLiInitRFLTexture (RFL_MakeTex.c) into a GXTexObj
 *   shape textures (face, cap, nose line, glasses)  RFLiGetShpTexSize,
 *       RFLiLoadShpTexture, then RFLiInitTexRes (RFL_Model.c): the image copy
 *       and the GXTexObj
 *   shapes (nose ... glasses)  RFLiGetShapeSize, RFLiLoadShape, then
 *       RFLiInitShapeRes (RFL_Model.c) in four modes: plain, flipped,
 *       transformed (scale and offset, as the hair, cap and beard are) and
 *       transformed + flipped. Its vertex arrays are GX data (big-endian).
 *
 * Lines (tab separated, hex; FNV-1a 32-bit hashes of byte ranges):
 *   RI  RFLInitRes result, RFLGetAsyncStatus, loader version, cached flag
 *   RA  archive index, numFiles, biggestSize, offset (RFLiLoader)
 *   RF  kind (T, S, P), part, file, size, hash of the loaded bytes
 *   RT  part, file, image offset in the loaded texture, GXTexObj words
 *   RX  part, file, GXTexObj words (image offset in the buffer), image hash
 *   RS  part, file, mode, numVtxPos, numVtxNrm, numVtxTxc, hashes of the
 *       position, normal and texcoord arrays as written, and for the faceline
 *       the three Vecs (f32 bits)
 *   RE  the resource bytes are unchanged
 *
 * The display lists RFLiInitShapeRes writes go to the GX FIFO, which the host
 * does not have (Phase 7); their bytes are not checked here.
 *
 * tests/test_rfl.py builds the same lines from the bytes.
 */
#include <RVLFaceLib/RVLFaceLibInternal.h>

#include <revolution/OS/OSAddress.h>

#include <port/arena.h>
#include <port/be.h>

#include "disc_walk.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int sRes = 0, sFiles = 0, sTex = 0, sShpTex = 0, sShapes = 0;

const RFLiArcID scTexArc[RFLiPartsTex_Max] = {RFLiArcID_Eye, RFLiArcID_Eyebrow,
                                              RFLiArcID_Mouth, RFLiArcID_Mustache,
                                              RFLiArcID_Mole};
const RFLiArcID scShpTexArc[RFLiPartsShpTex_Max] = {RFLiArcID_FaceTex, RFLiArcID_CapTex,
                                                    RFLiArcID_NlineTex, RFLiArcID_GlassTex};
const RFLiArcID scShpArc[RFLiPartsShp_Max] = {
    RFLiArcID_Nose,  RFLiArcID_ForeHead, RFLiArcID_Faceline, RFLiArcID_Hair, RFLiArcID_Cap,
    RFLiArcID_Beard, RFLiArcID_Nline,    RFLiArcID_Mask,     RFLiArcID_Glass};

void Error(const std::string& path, const char* pMsg) {
    std::fprintf(stderr, "ERROR: %s: %s\n", path.c_str(), pMsg);
    disc_walk::Errors()++;
}

u32 Fnv(const void* p, u32 size) {
    const u8* b = static_cast<const u8*>(p);
    u32 h = 0x811C9DC5;
    for (u32 i = 0; i < size; i++) {
        h = (h ^ b[i]) * 0x01000193;
    }
    return h;
}

u32 Bits(f32 f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::string TexObjCols(const GXTexObj& rObj) {
    const u8* o = reinterpret_cast<const u8*>(&rObj);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%08x\t%08x\t%08x\t%08x\t%04x\t%02x\t%02x",
                  port_load_be32(o + 0x0), port_load_be32(o + 0x4), port_load_be32(o + 0x8),
                  port_load_be32(o + 0x14), port_load_be16(o + 0x1C), o[0x1E], o[0x1F]);
    return buf;
}

s64 ImageOffset(const GXTexObj& rObj, const void* pBase) {
    s64 image = static_cast<s64>(port_load_be32(reinterpret_cast<const u8*>(&rObj) + 0xC) &
                                 0xFFFFFF)
                << 5;
    return image - static_cast<s64>(OSCachedToPhysical(pBase));
}

u8* Alloc(u32 size) {
    return static_cast<u8*>(RFLiAlloc32(size));
}

void TestTextures(u16 numFiles[RFLiArcID_Max]) {
    for (int part = 0; part < RFLiPartsTex_Max; part++) {
        for (u16 file = 0; file < numFiles[scTexArc[part]]; file++) {
            u32 size = RFLiGetTexSize(static_cast<RFLiPartsTex>(part), file);
            u8* pBuf = Alloc(size);
            RFLiTexture* pTex = RFLiLoadTexture(static_cast<RFLiPartsTex>(part), file, pBuf);
            std::printf("RF\tT\t%d\t%d\t%x\t%08x\n", part, file, size, Fnv(pBuf, size));
            sFiles++;

            if (pTex != reinterpret_cast<RFLiTexture*>(pBuf)) {
                Error("RFL_Res.dat", "RFLiLoadTexture did not return its buffer");
            } else {
                RFLiTexObj tobj;
                tobj.texture = pTex;
                tobj.image = NULL;
                RFLiInitRFLTexture(&tobj);
                std::printf("RT\t%d\t%d\t%llx\t%s\n", part, file,
                            static_cast<unsigned long long>(ImageOffset(tobj.tobj, pBuf)),
                            TexObjCols(tobj.tobj).c_str());
                sTex++;
            }
            RFLiFree(pBuf);
        }
    }
}

void TestShapeTextures(u16 numFiles[RFLiArcID_Max]) {
    // In the emulated arena: the GXTexObj holds the image's emulated address.
    const u32 kImageSize = 0x8000;
    u8* sImage = static_cast<u8*>(PortArenaAlloc(kImageSize, 32));
    for (int part = 0; part < RFLiPartsShpTex_Max; part++) {
        for (u16 file = 0; file < numFiles[scShpTexArc[part]]; file++) {
            RFLiPartsShpTex p = static_cast<RFLiPartsShpTex>(part);
            u32 size = RFLiGetShpTexSize(p, file);
            u8* pBuf = Alloc(size);
            RFLiLoadShpTexture(p, file, pBuf);
            std::printf("RF\tS\t%d\t%d\t%x\t%08x\n", part, file, size, Fnv(pBuf, size));
            sFiles++;

            // RFLiInitTexRes copies width x height x bpp into the buffer.
            const RFLiTexture* pTex = reinterpret_cast<const RFLiTexture*>(pBuf);
            u32 w = BE_GET(pTex->width), h = BE_GET(pTex->height);
            u32 texSize = part == RFLiPartsShpTex_Face  ? w * h * 2
                          : part == RFLiPartsShpTex_Glass ? w * h
                                                          : w * h / 2;
            RFLiFree(pBuf);
            if (texSize > kImageSize) {
                Error("RFL_Res.dat", "shape texture larger than the test buffer");
                continue;
            }

            std::memset(sImage, 0xCD, kImageSize);
            GXTexObj obj;
            RFLiInitTexRes(&obj, p, file, sImage);
            std::printf("RX\t%d\t%d\t%llx\t%s\t%08x\n", part, file,
                        static_cast<unsigned long long>(ImageOffset(obj, sImage)),
                        TexObjCols(obj).c_str(), Fnv(sImage, texSize));
            sShpTex++;
        }
    }
    PortArenaFree(sImage);
}

void TestShapes(u16 numFiles[RFLiArcID_Max]) {
    static be_s16 sPos[0x4000], sNrm[0x4000], sTxc[0x4000];
    static u8 sDl[0x4000] __attribute__((aligned(32)));
    static const Vec scTrans = {1.5f, -2.25f, 3.0f};

    for (int part = 0; part < RFLiPartsShp_Max; part++) {
        for (u16 file = 0; file < numFiles[scShpArc[part]]; file++) {
            RFLiPartsShp p = static_cast<RFLiPartsShp>(part);
            u32 size = RFLiGetShapeSize(p, file);
            u8* pBuf = Alloc(size);
            RFLiLoadShape(p, file, pBuf);
            std::printf("RF\tP\t%d\t%d\t%x\t%08x\n", part, file, size, Fnv(pBuf, size));
            RFLiFree(pBuf);
            sFiles++;

            for (int mode = 0; mode < 4; mode++) {
                Vec posTrans = scTrans, noseTrans, beardTrans, hairTrans;
                std::memset(&noseTrans, 0, sizeof(Vec));
                std::memset(&beardTrans, 0, sizeof(Vec));
                std::memset(&hairTrans, 0, sizeof(Vec));
                std::memset(sPos, 0, sizeof(sPos));
                std::memset(sNrm, 0, sizeof(sNrm));
                std::memset(sTxc, 0, sizeof(sTxc));

                RFLiShapeRes arg;
                std::memset(&arg, 0, sizeof(arg));
                arg.part = p;
                arg.file = file;
                arg.transform = mode >= 2;
                arg.flipX = mode & 1;
                arg.vtxPosBuf = sPos;
                arg.vtxNrmBuf = sNrm;
                arg.vtxTxcBuf = sTxc;
                arg.dlBuf = sDl;
                arg.vtxPosBufSize = 0x4000 / 3;
                arg.vtxNrmBufSize = 0x4000 / 3;
                arg.vtxTxcBufSize = 0x4000 / 2;
                arg.dlBufSize = sizeof(sDl);
                arg.posScale = 0.75f;
                arg.posTrans = &posTrans;
                arg.noseTrans = &noseTrans;
                arg.beardTrans = &beardTrans;
                arg.hairTrans = &hairTrans;
                RFLiInitShapeRes(&arg);

                std::printf("RS\t%d\t%d\t%d\t%x\t%x\t%x\t%08x\t%08x\t%08x", part, file, mode,
                            arg.numVtxPos, arg.numVtxNrm, arg.numVtxTxc,
                            Fnv(sPos, arg.numVtxPos * 6), Fnv(sNrm, arg.numVtxNrm * 6),
                            Fnv(sTxc, arg.numVtxTxc * 4));
                if (p == RFLiPartsShp_Faceline) {
                    const Vec* vecs[3] = {&noseTrans, &beardTrans, &hairTrans};
                    for (int v = 0; v < 3; v++) {
                        std::printf("\t%08x\t%08x\t%08x", Bits(vecs[v]->x), Bits(vecs[v]->y),
                                    Bits(vecs[v]->z));
                    }
                }
                std::printf("\n");
                sShapes++;
            }
        }
    }
}

void TestRfl(const std::string& path, u8* pData, u32 size) {
    sRes++;
    u32 before = Fnv(pData, size);

    u32 workSize = RFLGetWorkSize(FALSE);
    u8* pWork = new (static_cast<EGG::Heap*>(NULL), 32) u8[workSize];

    RFLErrcode err = RFLInitRes(pWork, pData, size, FALSE);
    RFLiLoader* pLoader = RFLiGetLoader();
    if (pLoader == NULL) {
        Error(path, "RFLInitRes failed");
        delete[] pWork;
        return;
    }
    std::printf("RI\t%d\t%d\t%x\t%d\n", err, RFLGetAsyncStatus(), pLoader->version,
                RFLIsResourceCached());

    u16 numFiles[RFLiArcID_Max];
    for (int i = 0; i < RFLiArcID_Max; i++) {
        const RFLiArchive& rArc = pLoader->archives[i];
        numFiles[i] = rArc.numFiles;
        std::printf("RA\t%d\t%x\t%x\t%x\n", i, rArc.numFiles, rArc.biggestSize, rArc.offset);
    }

    TestTextures(numFiles);
    TestShapeTextures(numFiles);
    TestShapes(numFiles);

    std::printf("RE\t%d\n", Fnv(pData, size) == before);
    RFLExit();
    delete[] pWork;
}

void Leaf(const std::string& path, u8* pData, u32 size) {
    if (path.size() >= 11 && path.compare(path.size() - 11, 11, "RFL_Res.dat") == 0) {
        TestRfl(path, pData, size);
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

    disc_walk::Visitor visitor = {Leaf, NULL};
    disc_walk::WalkDisc(visitor, paths);

    std::fprintf(stderr,
                 "rfl_harness: %d RFL_Res.dat, %d part files (%d textures, %d shape textures, "
                 "%d shape loads); %d errors (pointer size %zu)\n",
                 sRes, sFiles, sTex, sShpTex, sShapes, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 && sRes == 1 ? 0 : 1;
}
