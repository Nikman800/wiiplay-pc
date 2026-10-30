/**
 * EGG binary settings and game tables harness (port/docs/HOST_BUILD.md §14).
 *
 * Walks the disc like the other harnesses (disc_walk.h). Settings files are
 * loaded, in place inside their mounted archive, by the real EGG objects the
 * game loads them into:
 *
 *   .plight  EGG::LightManager(16, 8, 1) (as RPGrpModelScene creates it),
 *            IBinary<LightManager>::SetBinary -> LightManager::SetBinaryInner
 *            -> LightObject::SetBinary for every light
 *   .plmap   the LightManager's LightTextureManager: createTexture for every
 *            texture the file names, then SetBinary (LightTextureManager and
 *            LightTexture::SetBinaryInner)
 *   .pblm    EGG::DrawPathBloom, SetBinary
 *
 * Each object is then written back with the real GetBinary (GetBinaryInner),
 * and the harness prints that image read through the file structs, plus a few
 * runtime getters. The game tables are loaded by their real loaders where
 * those build on the host (RPTnkMapData, RPBomQuestionParam::Load); the
 * level tables (RPWlyLevelTable, RPPnpLevelTable), whose units do not build
 * on the host yet, are copied as their constructors do and read through the
 * real record structs.
 *
 * Lines (tab separated; 32-bit values as hex, floats as their bits):
 *   LO  .plight light i (< the file's count) from LightManager::GetBinary:
 *       BYTE_0x13 (type), spotFn, distAttnFn, ambLightIdx, flags, BYTE_0x12,
 *       intensity, pos, target, color, COLOR_0x38, spotArg, distAttnArg0/1
 *   LD  the same light through the runtime getters: intensity, at
 *   LA  ambient colour j, LT the light-texture colour
 *   MT  .plmap texture from LightTextureManager::GetBinary: name, drawType,
 *       lightMask; ML its light i (< the file's count): effectScale, pttrn
 *   BL  .pblm DrawPathBloom::GetBinary: every BinData field
 *   TM  RPTnkMapData map (tv mode, players, stage): file path, width, unk4,
 *       GetUnk08, GetUnk0C, and every cell through RP_TnkMapData_getCell
 *   BQ  RPBomQuestionParam::Load: mQuestionNum, then every non-empty
 *       (mPatternStart, mPatternNum) of BuildLevelTable
 *   BW  RPBomQuestionParamFile through its struct: one line per record kind
 *       and index with the words in member order
 *   WL  RPWlyLevelData records, PL RPPnpLevelTable LevelEntry/RallyEntry
 *
 * tests/test_data.py builds the same lines from
 * port/assets/tools/fmt_game_data.py and the bytes.
 */
#include <egg/gfxe/eggDrawPathBloom.h>
#include <egg/gfxe/eggLightManager.h>
#include <egg/gfxe/eggLightTexture.h>
#include <egg/gfxe/eggLightTextureManager.h>
#include <egg/gfxe/eggScreen.h>

#include <Pack/RPParty/RPBomScene/RPBomQuestionParam.h>
#include <Pack/RPParty/RPPartyCommon/RPTnkMapData.h>
#include <Pack/RPParty/RPPnpScene/RPPnpLevelTable.h>
#include <Pack/RPParty/RPWlyScene/RPWlyLevelTable.h>
#include <Pack/RPKernel/RP_DEBUG_STUB_1.h>
#include <Pack/RPSystem/RPSysResourceManager.h>

#include <port/arena.h>
#include <port/be.h>

#include "disc_walk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" s32 RP_TnkMapData_getCell(RPTnkMapData* pMapData, s32 stage, s32 x, s32 y,
                                     s32 playerIdx);

namespace {

int sLight = 0, sLightObjs = 0, sLmap = 0, sLtex = 0, sBloom = 0;
int sTnkMaps = 0, sBom = 0, sWly = 0, sPnp = 0;

u32 W(f32 v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    return u;
}
u32 W(s32 v) {
    return static_cast<u32>(v);
}
u32 W(u32 v) {
    return v;
}
template <typename T, typename S> u32 W(const port::be<T, S>& v) {
    return W(static_cast<T>(v));
}

u32 Color(const GXColor& c) {
    return (static_cast<u32>(c.r) << 24) | (c.g << 16) | (c.b << 8) | c.a;
}

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::vector<u8>& Scratch() {
    static std::vector<u8> sBuf;
    return sBuf;
}

/******************************************************************************
 * EGG binary settings
 ******************************************************************************/

void TestLight(const std::string& path, u8* pData) {
    sLight++;
    EGG::LightManager* pMgr = new EGG::LightManager(16, 8, 1);
    pMgr->SetBinary(pData);

    typedef EGG::IBinary<EGG::LightObject>::Bin LightBin;
    u16 fileNum = port_load_be16(pData + 0x10);
    u16 loaded = fileNum < pMgr->GetNum() ? fileNum : pMgr->GetNum();

    u8* pOut = static_cast<u8*>(std::calloc(1, pMgr->GetBinarySize()));
    pMgr->GetBinary(pOut);
    const EGG::LightManager::Bin* pBin = reinterpret_cast<const EGG::LightManager::Bin*>(pOut);
    for (u16 i = 0; i < loaded; i++) {
        sLightObjs++;
        const LightBin& l = pBin->data.lights[i];
        std::printf("LO\t%s\t%u\t%u\t%u\t%u\t%u\t%04x\t%u\t%08x\t%08x,%08x,%08x\t%08x,%08x,%08x\t%08x"
                    "\t%08x\t%08x\t%08x\t%08x\n",
                    path.c_str(), i, l.data.BYTE_0x13, l.data.spotFn, l.data.distAttnFn,
                    static_cast<u16>(l.data.ambLightIdx), static_cast<u16>(l.data.flags),
                    l.data.BYTE_0x12, W(l.data.intensity), W(l.data.pos.x), W(l.data.pos.y),
                    W(l.data.pos.z), W(l.data.target.x), W(l.data.target.y), W(l.data.target.z),
                    Color(l.data.color), Color(l.data.COLOR_0x38), W(l.data.spotArg),
                    W(l.data.distAttnArg0), W(l.data.distAttnArg1));
        const EGG::LightObject* pObj = pMgr->GetLightObj(i);
        std::printf("LD\t%s\t%u\t%08x\t%08x,%08x,%08x\n", path.c_str(), i,
                    W(pObj->GetIntensity()), W(pObj->GetAt().x), W(pObj->GetAt().y),
                    W(pObj->GetAt().z));
    }
    // The ambient colours follow the lights in the written image.
    const u8* pAmb = reinterpret_cast<const u8*>(&pBin->data.lights[pBin->data.lightNum]);
    for (int j = 0; j < pBin->data.ambientNum; j++) {
        std::printf("LA\t%s\t%d\t%08x\n", path.c_str(), j, port_load_be32(pAmb + j * 8));
    }
    std::printf("LT\t%s\t%08x\n", path.c_str(), Color(pBin->data.lightTexColor));
    std::free(pOut);
}

void TestLightMap(const std::string& path, u8* pData) {
    sLmap++;
    typedef EGG::IBinary<EGG::LightTexture>::Bin TexBin;
    EGG::LightManager* pMgr = new EGG::LightManager(16, 8, 1);
    EGG::LightTextureManager* pTexMgr = pMgr->GetLightTextureManager();

    // The game creates its light textures from its scene's models; here the
    // textures are the ones the file names (read through the file struct).
    u16 num = port_load_be16(pData + 0x10);
    std::map<std::string, u16> fileLights;
    const u8* p = pData + 0x20;
    for (u16 i = 0; i < num; i++) {
        const TexBin* pTex = reinterpret_cast<const TexBin*>(p);
        pTexMgr->createTexture(pTex->data.name);
        fileLights[pTex->data.name] = pTex->data.lightNum;
        p += pTex->header.size;
    }
    pTexMgr->SetBinary(pData);

    std::vector<u8> out(0x10000);
    pTexMgr->GetBinary(out.data());
    u16 outNum = port_load_be16(out.data() + 0x10);
    p = out.data() + 0x20;
    for (u16 i = 0; i < outNum; i++) {
        sLtex++;
        const TexBin* pTex = reinterpret_cast<const TexBin*>(p);
        std::printf("MT\t%s\t%s\t%u\t%08x\n", path.c_str(), pTex->data.name, pTex->data.drawType,
                    static_cast<u32>(pTex->data.lightMask));
        u16 n = fileLights[pTex->data.name];
        if (n > pTex->data.lightNum) {
            n = pTex->data.lightNum;
        }
        for (u16 k = 0; k < n; k++) {
            std::printf("ML\t%s\t%s\t%u\t%08x\t%u\n", path.c_str(), pTex->data.name, k,
                        W(pTex->data.lights[k].effectScale), pTex->data.lights[k].pttrn);
        }
        p += pTex->header.size;
    }
}

void TestBloom(const std::string& path, u8* pData) {
    sBloom++;
    EGG::DrawPathBloom* pBloom = new EGG::DrawPathBloom();
    pBloom->SetBinary(pData);

    EGG::DrawPathBloom::Bin out;
    std::memset(&out, 0, sizeof(out));
    pBloom->GetBinary(&out);
    const EGG::DrawPathBloom::BinData& d = out.data;
    std::printf("BL\t%s\t%08x\t%08x\t%08x\t%04x\t%04x\t%08x,%08x\t%08x,%08x\t%08x,%08x\t%u\t%u"
                "\t%08x\t%08x\n",
                path.c_str(), W(d.mThreshold), Color(d.mMaskColor), Color(d.mColor),
                static_cast<u16>(d.mFlags), static_cast<u16>(d.unkE), W(d.mBlur0.mRadius),
                W(d.mBlur0.mIntensity), W(d.mBlur1.mRadius), W(d.mBlur1.mIntensity),
                W(d.mBlur2.mRadius), W(d.mBlur2.mIntensity), d.mBlendModeIdx, d.mNumBlur,
                W(d.mGatherScale0), W(d.mGatherScale1));
}

/******************************************************************************
 * Game tables
 ******************************************************************************/
// The stand-in resource manager reads from the archive being visited.
const ARCHandle* spArchive = NULL;
std::string sArchivePath;
std::map<std::string, u32> sOpened; // file name -> size, for the TM lines

bool Has(const ARCHandle& handle, const char* pName) {
    return ARCConvertPathToEntrynum(const_cast<ARCHandle*>(&handle), pName) >= 0;
}

// The decomp's own non-polymorphic view of RPTnkMapData (RPTnkMapData.cpp).
struct TnkMapDataView {
    void* vtable;
    RPTnkMapData::Map* mpMaps[2][2][30];
};

void TestTnkMaps() {
    RPTnkMapData* pMaps = new RPTnkMapData;
    const TnkMapDataView* pView = reinterpret_cast<const TnkMapDataView*>(pMaps);
    for (int tv = 0; tv < 2; tv++) {
        EGG::Screen::SetTVMode(static_cast<EGG::Screen::TVMode>(tv));
        for (int player = 0; player < 2; player++) {
            for (int stage = 0; stage < 30; stage++) {
                sTnkMaps++;
                char name[64];
                std::snprintf(name, sizeof(name), "MapData/TnkMapData_P%d_%02d_%d.bin", player + 1,
                              stage, tv);
                const RPTnkMapData::Map* pMap = pView->mpMaps[tv][player][stage];
                s32 width = pMap->width;
                s32 cells = (static_cast<s32>(sOpened[name]) - 0x10) / 4;
                std::printf("TM\t%s/%s\t%d\t%08x\t%d\t%d\t", sArchivePath.c_str(), name, width,
                            static_cast<u32>(pMap->unk4), pMaps->GetUnk08(stage, player),
                            pMaps->GetUnk0C(stage, player));
                for (s32 c = 0; width > 0 && c < cells; c++) {
                    std::printf(c == 0 ? "%d" : ",%d",
                                RP_TnkMapData_getCell(pMaps, stage, c % width, c / width, player));
                }
                std::printf("\n");
            }
        }
    }
    EGG::Screen::SetTVMode(EGG::Screen::TV_MODE_STD);
}

template <typename T> void Words(const char* tag, const std::string& path, const char* kind,
                                 int idx, const T* pWords, int n) {
    std::printf("%s\t%s\t%s\t%d", tag, path.c_str(), kind, idx);
    for (int i = 0; i < n; i++) {
        std::printf("\t%08x", pWords[i]);
    }
    std::printf("\n");
}

void TestBom() {
    sBom++;
    std::string path = sArchivePath + "/param/BomQuestionParam.bin";
    RPBomQuestionParam* pParam = new RPBomQuestionParam();
    pParam->Load();
    std::printf("BQ\t%s\t%d", path.c_str(), pParam->mQuestionNum);
    for (int q = 0; q < 128; q++) {
        if (pParam->mPatternStart[q] != 0 || pParam->mPatternNum[q] != 0) {
            std::printf("\t%d:%d:%d", q, pParam->mPatternStart[q], pParam->mPatternNum[q]);
        }
    }
    std::printf("\n");

    const RPBomQuestionParamFile* f = pParam->mpFile;
    u32 w[17];
    int n = 0;
    w[n++] = W(f->mMissZ), w[n++] = W(f->mHitZoneFar), w[n++] = W(f->mHitZoneNear);
    w[n++] = W(f->mTwoPlayerCostScale), w[n++] = W(f->unk10), w[n++] = W(f->mSpeedChangeFrames);
    w[n++] = W(f->mBubbleScale169), w[n++] = W(f->mBubbleScale43), w[n++] = W(f->unk20[0]);
    w[n++] = W(f->unk20[1]), w[n++] = W(f->mMissWait), w[n++] = W(f->mMissMax);
    w[n++] = W(f->mSpawnXMargin), w[n++] = W(f->mSpawnZ), w[n++] = W(f->mBubbleWait);
    Words("BW", path, "header", 0, w, n);
    for (int i = 0; i < 101; i++) {
        const RPBomQuestionData& q = f->mQuestions[i];
        n = 0;
        w[n++] = W(q.mMissNext), w[n++] = W(q.mCost);
        for (int k = 0; k < 3; k++) {
            w[n++] = W(q.mPoseEnabled[k]);
        }
        w[n++] = W(q.mWaveInterval), w[n++] = W(q.mWaveNum[0]), w[n++] = W(q.mWaveNum[1]);
        w[n++] = W(q.mSpeed[0]), w[n++] = W(q.mSpeed[1]);
        Words("BW", path, "question", i, w, n);
    }
    for (int i = 0; i < 250; i++) {
        const RPBomPatternData& p = f->mPatterns[i];
        n = 0;
        w[n++] = W(p.mSegmentsMax), w[n++] = W(p.mSegmentsMin), w[n++] = W(p.mRadiusMax);
        w[n++] = W(p.mRadiusMin), w[n++] = W(p.unk10), w[n++] = W(p.unk14);
        w[n++] = W(p.mQuestion), w[n++] = W(p.mRotDirMode), w[n++] = W(p.mRotSpeedMax);
        w[n++] = W(p.mRotSpeedMin), w[n++] = W(p.mAngleMin), w[n++] = W(p.mAngleMax);
        w[n++] = W(p.mSpawnZMax), w[n++] = W(p.mSpawnZMin);
        Words("BW", path, "pattern", i, w, n);
    }
    for (int i = 0; i < 16; i++) {
        const RPBomLevelData& l = f->mLevels[i];
        n = 0;
        w[n++] = W(l.unk0);
        for (int k = 0; k < 11; k++) {
            w[n++] = W(l.mSpecialChance[k]);
        }
        w[n++] = W(l.mWaveIntervalScale), w[n++] = W(l.mTime), w[n++] = W(l.mStartQuestion);
        w[n++] = W(l.mBgFrame), w[n++] = W(l.mSpecialGap);
        Words("BW", path, "level", i, w, n);
    }
    n = 0;
    for (int k = 0; k < 10; k++) {
        w[n++] = W(f->mComboPoints[k]);
    }
    Words("BW", path, "combo", 0, w, n);
}

void *Open(const char* pName, u32* pSize);

void TestWly() {
    // RPWlyLevelTable::RPWlyLevelTable: memcpy, count = size / sizeof(RPWlyLevelData).
    const char* names[2] = {"wanted_1p_leveltable.bin", "wanted_2p_leveltable.bin"};
    for (int i = 0; i < 2; i++) {
        u32 size;
        void* pFile = Open(names[i], &size);
        RPWlyLevelData* pLevels = reinterpret_cast<RPWlyLevelData*>(new u8[size]);
        std::memcpy(pLevels, pFile, size);
        int num = size / sizeof(RPWlyLevelData);
        std::string path = sArchivePath + "/" + names[i];
        for (int j = 0; j < num; j++) {
            sWly++;
            const RPWlyLevelData& d = pLevels[j];
            u32 w[16] = {W(d.mAvatarNum),  W(d.unk4),        W(d.mQuestionType), W(d.mFieldType),
                         W(d.unk10),       W(d.unk14),       W(d.mCameraTarget), W(d.mSpeed),
                         W(d.unk20),       W(d.mSpotlight),  W(d.mFoundScale),   W(d.mSpotPos.x),
                         W(d.mSpotPos.y),  W(d.mSpotPos.z),  W(d.mPlaneY),       W(d.mPlaneZ)};
            Words("WL", path, "level", j, w, 16);
        }
        delete[] reinterpret_cast<u8*>(pLevels);
    }
}

void TestPnp() {
    // RPPnpLevelTable::RPPnpLevelTable: memcpy, count = size / sizeof(entry).
    u32 size;
    void* pFile = Open("leveltable.bin", &size);
    RPPnpLevelTable::LevelEntry* pLevels =
        reinterpret_cast<RPPnpLevelTable::LevelEntry*>(new u8[size]);
    std::memcpy(pLevels, pFile, size);
    for (u32 j = 0; j < size / sizeof(RPPnpLevelTable::LevelEntry); j++) {
        sPnp++;
        u32 w[3] = {W(pLevels[j].unk0), W(pLevels[j].unk4), W(pLevels[j].unk8)};
        Words("PL", sArchivePath + "/leveltable.bin", "level", j, w, 3);
    }
    const char* names[2] = {"rallyleveluptable.bin", "rallyleveluptable2.bin"};
    for (int i = 0; i < 2; i++) {
        pFile = Open(names[i], &size);
        RPPnpLevelTable::RallyEntry* pRally =
            reinterpret_cast<RPPnpLevelTable::RallyEntry*>(new u8[size]);
        std::memcpy(pRally, pFile, size);
        for (u32 j = 0; j < size / sizeof(RPPnpLevelTable::RallyEntry); j++) {
            sPnp++;
            u32 w[3] = {W(pRally[j].mRally), W(pRally[j].mCpuLevel), W(pRally[j].mBallSpeed)};
            Words("PL", sArchivePath + "/" + names[i], "rally", j, w, 3);
        }
    }
}

void *Open(const char* pName, u32* pSize) {
    ARCFileInfo info;
    if (spArchive == NULL || !ARCOpen(const_cast<ARCHandle*>(spArchive), pName, &info)) {
        std::fprintf(stderr, "ERROR: cannot open %s in %s\n", pName, sArchivePath.c_str());
        disc_walk::Errors()++;
        *pSize = 0;
        static u32 sEmpty[8];
        return sEmpty;
    }
    void* p = ARCGetStartAddrInMem(&info);
    *pSize = ARCGetLength(&info);
    ARCClose(&info);
    sOpened[pName] = *pSize;
    return p;
}

void Archive(const std::string& path, const ARCHandle& handle) {
    spArchive = &handle;
    sArchivePath = path;
    if (Has(handle, "MapData/TnkMapData_P1_00_0.bin")) {
        TestTnkMaps();
    }
    if (Has(handle, "param/BomQuestionParam.bin")) {
        TestBom();
    }
    if (Has(handle, "wanted_1p_leveltable.bin")) {
        TestWly();
    }
    if (Has(handle, "rallyleveluptable.bin")) {
        TestPnp();
    }
    spArchive = NULL;
}

void Leaf(const std::string& path, u8* pData, u32 size) {
    (void)size;
    if (EndsWith(path, ".plight")) {
        TestLight(path, pData);
    } else if (EndsWith(path, ".plmap")) {
        TestLightMap(path, pData);
    } else if (EndsWith(path, ".pblm")) {
        TestBloom(path, pData);
    }
}

} // namespace

/******************************************************************************
 * Stand-ins (not decompiled code)
 ******************************************************************************/
// src/Pack/RPKernel/RP_DEBUG_STUB_1.cpp (RPSceneObj's base), whose
// umbrella include (Pack/RPKernel.h) does not build on the host yet; the same
// empty bodies.
RP_DEBUG_STUB_1::RP_DEBUG_STUB_1() {}
RP_DEBUG_STUB_1::~RP_DEBUG_STUB_1() {}

EGG::Archive* RPSysResourceManager::LoadGameCommonArchive(s32, EGG::Heap*) {
    return NULL;
}

void* RPSysResourceManager::GetFileFromArchive(EGG::Archive*, const char* pName, u32* pSize) {
    u32 size;
    void* p = Open(pName, &size);
    if (pSize != NULL) {
        *pSize = size;
    }
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

    std::fprintf(stderr,
                 "data_harness: %d .plight (%d lights), %d .plmap (%d light textures), %d .pblm; "
                 "tables: %d Tnk maps, %d BomQuestionParam, %d Wly levels, %d Pnp records; "
                 "%d errors (pointer size %zu)\n",
                 sLight, sLightObjs, sLmap, sLtex, sBloom, sTnkMaps, sBom, sWly, sPnp,
                 disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
