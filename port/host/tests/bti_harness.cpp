/**
 * BTI harness (port/docs/HOST_BUILD.md §16).
 *
 * Walks the disc (disc_walk.h) and hands every .bti file, in place inside its
 * mounted archive, to the game's code that reads it:
 *
 *   RPGrpTexture::RPGrpTexture(const EGG::ResTIMG*) (RPGrpTexture.cpp), as
 *   RPSysKokeshiIcon::SetupModel does for lm_0 / lm_1, which calls
 *   EGG::GXUtility::getTexObj (eggGXUtility.cpp): the ResTIMG header is read
 *   through its be_* fields into a GXTexObj (host GXInitTexObj /
 *   GXInitTexObjLOD, src/host_gxtexobj.cpp: the Wii's words, big-endian).
 *   EGG::DrawGX::LoadTexture (the Tanks tread marks' draw path) makes the same
 *   call and is run too.
 *
 * For Common/RPTnkScene/common.carc the real RPTnkFootmarkTex::Load
 * (RPTnkTask_3.cpp) runs while the archive is mounted, and each texture it
 * returns goes through EGG::DrawGX::LoadTexture's getTexObj.
 *
 * Lines (tab separated, hex):
 *   BT  path, image offset in the file (from the GXTexObj's image address),
 *       mode0, mode1, image0, fmt, loadCnt, loadFmt, flags of the GXTexObj
 *   BF  RPTnkFootmarkTex texture index, the archive path it points at, and the
 *       GXTexObj image0 word
 *
 * tests/test_bti.py builds the same lines from the bytes
 * (fmt_text_font.parse_bti and a model of GXInitTexObj / GXInitTexObjLOD) and
 * decodes every image with port/assets/tools/gxtex.py.
 */
#include <Pack/RPGraphics/RPGrpTexture.h>
#include <Pack/RPKernel/RP_DEBUG_STUB_1.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTask.h>
#include <Pack/RPSystem/RPSysResourceManager.h>

#include <egg/gfxe.h>

#include <revolution/ARC.h>
#include <revolution/OS/OSAddress.h>

#include <port/be.h>

#include "disc_walk.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int sBti = 0, sTnk = 0;
const ARCHandle* spHandle = NULL;

bool EndsWith(const std::string& s, const char* pSuffix) {
    size_t n = std::strlen(pSuffix);
    return s.size() >= n && s.compare(s.size() - n, n, pSuffix) == 0;
}

void Error(const std::string& path, const char* pMsg) {
    std::fprintf(stderr, "ERROR: %s: %s\n", path.c_str(), pMsg);
    disc_walk::Errors()++;
}

u32 Load32(const void* p) {
    return port_load_be32(p);
}

// The image offset the GXTexObj's address (image3, physical >> 5) points at.
s64 ImageOffset(const GXTexObj& rObj, const void* pFile) {
    const u8* o = reinterpret_cast<const u8*>(&rObj);
    s64 image = static_cast<s64>(Load32(o + 0xC) & 0xFFFFFF) << 5;
    return image - static_cast<s64>(OSCachedToPhysical(pFile));
}

void PrintTexObj(const char* pKind, const std::string& path, const GXTexObj& rObj,
                 const void* pFile) {
    const u8* o = reinterpret_cast<const u8*>(&rObj);
    std::printf("%s\t%s\t%llx\t%08x\t%08x\t%08x\t%08x\t%04x\t%02x\t%02x\n", pKind, path.c_str(),
                static_cast<unsigned long long>(ImageOffset(rObj, pFile)), Load32(o + 0x0),
                Load32(o + 0x4), Load32(o + 0x8), Load32(o + 0x14), port_load_be16(o + 0x1C),
                o[0x1E], o[0x1F]);
}

void TestBti(const std::string& path, u8* pData, u32 size) {
    sBti++;
    if (size < sizeof(EGG::ResTIMG) || (OSCachedToPhysical(pData) & 31) != 0) {
        Error(path, "short or unaligned BTI");
        return;
    }

    u8 before[sizeof(EGG::ResTIMG)];
    std::memcpy(before, pData, sizeof(before));

    const EGG::ResTIMG* pRes = reinterpret_cast<const EGG::ResTIMG*>(pData);
    RPGrpTexture tex(pRes);
    PrintTexObj("BT", path, tex.GetTexObj(), pData);

    // DrawGX::LoadTexture: the same getTexObj (GXLoadTexObj is a host no-op).
    EGG::DrawGX::LoadTexture(pRes, GX_TEXMAP0);
    GXTexObj direct;
    EGG::GXUtility::getTexObj(&direct, *pRes);
    if (std::memcmp(&direct, &tex.GetTexObj(), sizeof(direct)) != 0) {
        Error(path, "GXUtility::getTexObj and RPGrpTexture disagree");
    }

    if (std::memcmp(before, pData, sizeof(before)) != 0) {
        Error(path, "header changed");
    }
}

void Leaf(const std::string& path, u8* pData, u32 size) {
    if (EndsWith(path, ".bti")) {
        TestBti(path, pData, size);
    }
}

std::string sArchivePath;

// The Tanks scene's common archive: run the game's RPTnkFootmarkTex::Load.
void Archive(const std::string& path, const ARCHandle& rHandle) {
    if (path != "Common/RPTnkScene/common.carc") {
        return;
    }
    spHandle = &rHandle;
    sArchivePath = path;

    RPTnkFootmarkTex::CreateInstance();
    RPTnkFootmarkTex::sInstance->Load();

    for (s32 i = 0; i < 11; i++) {
        const void* pTex = RPTnkFootmarkTex::sInstance->GetTexture(i);
        if (pTex == NULL) {
            Error(path, "RPTnkFootmarkTex texture missing");
            continue;
        }

        // Which archive entry the pointer is.
        std::string name = "?";
        ARCDir dir;
        ARCDirEntry entry;
        ARCHandle handle = rHandle;
        if (ARCOpenDir(&handle, "/tnk_footmark", &dir)) {
            while (ARCReadDir(&dir, &entry)) {
                ARCFileInfo info;
                if (!entry.isDir && ARCFastOpen(&handle, entry.entryNum, &info)) {
                    if (ARCGetStartAddrInMem(&info) == pTex) {
                        name = std::string("tnk_footmark/") + entry.name;
                    }
                    ARCClose(&info);
                }
            }
            ARCCloseDir(&dir);
        }

        GXTexObj obj;
        EGG::GXUtility::getTexObj(&obj, *static_cast<const EGG::ResTIMG*>(pTex));
        std::printf("BF\t%d\t%s\t%08x\n", i, name.c_str(),
                    Load32(reinterpret_cast<const u8*>(&obj) + 0x8));
        sTnk++;
    }

    RPTnkFootmarkTex::DestroyInstance();
    spHandle = NULL;
}

} // namespace

/******************************************************************************
 * Stand-ins (not decompiled code)
 ******************************************************************************/

// src/Pack/RPKernel/RP_DEBUG_STUB_1.cpp (RPSceneObj's base): the same empty bodies.
RP_DEBUG_STUB_1::RP_DEBUG_STUB_1() {}
RP_DEBUG_STUB_1::~RP_DEBUG_STUB_1() {}

// RPTnkFootmarkTex::Load's lookups: the archive the disc walk has mounted.
EGG::Archive* RPSysResourceManager::LoadGameCommonArchive(s32, EGG::Heap*) {
    return NULL;
}

void* RPSysResourceManager::GetFileFromArchive(EGG::Archive*, const char* pName, u32* pSize) {
    if (spHandle == NULL) {
        std::fprintf(stderr, "ERROR: unexpected GetFileFromArchive(%s)\n", pName);
        disc_walk::Errors()++;
        return NULL;
    }
    ARCHandle handle = *spHandle;
    ARCFileInfo info;
    if (!ARCOpen(&handle, pName, &info)) {
        std::fprintf(stderr, "ERROR: %s: no %s\n", sArchivePath.c_str(), pName);
        disc_walk::Errors()++;
        return NULL;
    }
    void* p = ARCGetStartAddrInMem(&info);
    if (pSize != NULL) {
        *pSize = ARCGetLength(&info);
    }
    ARCClose(&info);
    return p;
}

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

    disc_walk::Visitor visitor = {Leaf, Archive};
    disc_walk::WalkDisc(visitor, paths);

    std::fprintf(stderr, "bti_harness: %d BTI, %d RPTnkFootmarkTex textures; %d errors (pointer size %zu)\n",
                 sBti, sTnk, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
