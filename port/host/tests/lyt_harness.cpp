/**
 * Layout harness: runs the decompiled TPL and nw4r::lyt resource code on the
 * host, without drawing.
 *
 * Walks the disc like arc_harness (disc_walk.h). For every mounted archive
 * that has a `blyt` directory at its root or under `arc/` (a layout archive)
 * it attaches a real lyt::ArcResourceAccessor to the archive, then for every
 * .brlyt in it:
 *
 *   Layout::Build                         panes, materials (TPLBind through
 *                                         TexMap::Set), text boxes (fonts
 *                                         through ResFont), windows, groups
 *   for every .brlan in the archive:
 *     Layout::CreateAnimTransform         AnimTransformBasic::SetResource
 *     Layout::BindAnimation               AnimTransformBasic::Bind
 *
 * and for every .tpl leaf file (standalone or inside an archive) it calls the
 * host TPLBind (unless a layout already bound it, which is the case lyt's
 * "already bound" test exists for) and reads every descriptor back through
 * TPLGet and lyt::TexMap::Set(TPLPalette*, id).
 *
 * Output: tab-separated lines (see port/host/tests/test_layouts.py, which
 * builds the same lines from port/assets/tools/fmt_layout.py):
 *
 *   T   tpl-path  numDescriptors
 *   TD  tpl-path  idx  w h fmt dataOff wrapS wrapT minF magF bias edge minLOD maxLOD
 *       clutEntries clutFmt clutOff
 *   L   brlyt-path  width height  panes  materials  groups
 *   P   brlyt-path  index depth kind name userData flags alpha basePos
 *       translate rotate scale size material extra
 *   M   brlyt-path  material-name texMapNum texSRTCap tevCols kCols srt0 textures
 *   G   brlyt-path  name  pane names
 *   A   brlan-path  brlyt-path  frameSize loop fileNum contents bound
 *   AC  brlan-path  brlyt-path  index name type bound
 *
 * Usage: lyt_harness <extracted-disc-files-dir> [path ...]
 */

#include <egg/core.h>
#include <nw4r/lyt.h>
#include <nw4r/ut.h>
#include <revolution/ARC.h>
#include <revolution/MEM.h>
#include <revolution/TPL.h>

#include <port/arena.h>
#include <port/host.h>

#include "disc_walk.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace nw4r;

namespace {

int sTpls = 0;
int sTplBoundByLayout = 0;
int sLayouts = 0;
int sPanes = 0;
int sAnims = 0;
int sBinds = 0;
int sBoundContents = 0;

// Expose the per-content animation links (protected in AnimTransformBasic).
struct AnimProbe : lyt::AnimTransformBasic {
    static const lyt::AnimationLink* Links(const lyt::AnimTransformBasic* p) {
        return static_cast<const AnimProbe*>(p)->mAnimLinkAry;
    }
};

std::string Hex(const void* p, u32 n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (u32 i = 0; i < n; i++) {
        u8 b = static_cast<const u8*>(p)[i];
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

std::string Fixed(const char* p, u32 n) {
    return std::string(p, strnlen(p, n));
}

/**************************************************************************
 * TPL
 **************************************************************************/
void TestTpl(const std::string& path, u8* pData, u32 size) {
    TPLPalette* pPal = reinterpret_cast<TPLPalette*>(pData);
    if (size < sizeof(TPLPalette) || pPal->versionNumber != 2142000) {
        std::fprintf(stderr, "ERROR: %s is not a TPL\n", path.c_str());
        disc_walk::Errors()++;
        return;
    }

    // The same "already bound" test lyt::TexMap::Set makes.
    if (PORT_PTR_TO_U32(pPal->descriptorArray) < 0x80000000) {
        TPLBind(pPal);
    } else {
        sTplBoundByLayout++;
    }

    const u32 base = port_vaddr(pData);
    std::printf("T\t%s\t%u\n", path.c_str(), static_cast<u32>(pPal->numDescriptors));

    for (u32 i = 0; i < pPal->numDescriptors; i++) {
        const TPLDescriptor* pDesc = TPLGet(pPal, i);
        const TPLHeader* pTex = pDesc->textureHeader;

        if (pTex == NULL) {
            std::printf("TD\t%s\t%u\t-\n", path.c_str(), i);
            continue;
        }

        std::string clut = "-\t-\t-";
        const TPLClutHeader* pClut = pDesc->CLUTHeader;
        if (pClut != NULL) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%u\t%u\t%u",
                          static_cast<u32>(pClut->numEntries),
                          static_cast<u32>(static_cast<GXTlutFmt>(pClut->format)),
                          RES_PTR_RAW(pClut->data) - base);
            clut = buf;
        }

        std::printf("TD\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%.6g\t%u\t%u\t%u\t%s\n",
                    path.c_str(), i, static_cast<u32>(pTex->width),
                    static_cast<u32>(pTex->height), static_cast<u32>(pTex->format),
                    RES_PTR_RAW(pTex->data) - base,
                    static_cast<u32>(static_cast<GXTexWrapMode>(pTex->wrapS)),
                    static_cast<u32>(static_cast<GXTexWrapMode>(pTex->wrapT)),
                    static_cast<u32>(static_cast<GXTexFilter>(pTex->minFilter)),
                    static_cast<u32>(static_cast<GXTexFilter>(pTex->magFilter)),
                    static_cast<f32>(pTex->LODBias), pTex->edgeLODEnable,
                    pTex->minLOD, pTex->maxLOD, clut.c_str());

        // The view lyt gets of the same descriptor.
        lyt::TexMap texMap;
        texMap.Set(pPal, i);
        if (texMap.GetWidth() != pTex->width || texMap.GetHeight() != pTex->height ||
            texMap.GetTexelFormat() != static_cast<GXTexFmt>(static_cast<u32>(pTex->format)) ||
            texMap.GetImage() != static_cast<void*>(pTex->data) ||
            texMap.GetPalette() != (pClut != NULL ? static_cast<void*>(pClut->data) : NULL) ||
            texMap.GetWrapModeS() != static_cast<GXTexWrapMode>(pTex->wrapS)) {
            std::fprintf(stderr, "ERROR: TexMap::Set disagrees with %s[%u]\n",
                         path.c_str(), i);
            disc_walk::Errors()++;
        }
    }
    sTpls++;
}

/**************************************************************************
 * Layouts
 **************************************************************************/
const char* PaneKind(lyt::Pane* p) {
    if (ut::DynamicCast<lyt::Picture*>(p) != NULL) {
        return "pic1";
    }
    if (ut::DynamicCast<lyt::TextBox*>(p) != NULL) {
        return "txt1";
    }
    if (ut::DynamicCast<lyt::Window*>(p) != NULL) {
        return "wnd1";
    }
    if (ut::DynamicCast<lyt::Bounding*>(p) != NULL) {
        return "bnd1";
    }
    return "pan1";
}

std::string Colors(lyt::Pane* p) {
    std::string s;
    for (u32 i = 0; i < 4; i++) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s%08x", i ? "," : "", p->GetVtxColor(i).ToU32());
        s += buf;
    }
    return s;
}

u64 Fnv1a64Wide(const wchar_t* s) {
    u64 h = 0xcbf29ce484222325ULL;
    for (; *s != 0; s++) {
        // Hash the code unit as UTF-16BE bytes (the file's encoding).
        h ^= static_cast<u16>(*s) >> 8;
        h *= 0x100000001b3ULL;
        h ^= static_cast<u16>(*s) & 0xFF;
        h *= 0x100000001b3ULL;
    }
    return h;
}

void PrintMaterial(const std::string& path, const lyt::Material* pMat) {
    std::string tev, kcol, tex;
    char buf[160];
    for (u32 i = 0; i < 3; i++) {
        GXColorS10 c = pMat->GetTevColor(i);
        std::snprintf(buf, sizeof(buf), "%s%d,%d,%d,%d", i ? ";" : "", c.r, c.g, c.b, c.a);
        tev += buf;
    }
    for (u32 i = 0; i < 4; i++) {
        std::snprintf(buf, sizeof(buf), "%s%08x", i ? "," : "", pMat->GetTevKColor(i).ToU32());
        kcol += buf;
    }
    std::string srt = "-";
    if (pMat->GetTexSRTCap() > 0) {
        const lyt::TexSRT& r = pMat->GetTexSRT(0);
        std::snprintf(buf, sizeof(buf), "%.6g,%.6g,%.6g,%.6g,%.6g", r.translate.x,
                      r.translate.y, r.rotate, r.scale.x, r.scale.y);
        srt = buf;
    }
    for (u8 i = 0; i < pMat->GetTextureNum(); i++) {
        const lyt::TexMap& m = pMat->GetTexture(i);
        std::snprintf(buf, sizeof(buf), "%s%ux%u:%u:%u:%u", i ? ";" : "", m.GetWidth(),
                      m.GetHeight(), static_cast<u32>(m.GetTexelFormat()),
                      static_cast<u32>(m.GetWrapModeS()), static_cast<u32>(m.GetWrapModeT()));
        tex += buf;
    }
    std::printf("M\t%s\t%s\t%u\t%u\t%s\t%s\t%s\t%s\n", path.c_str(),
                Fixed(pMat->GetName(), NW4R_LYT_MATERIAL_NAME_LEN).c_str(),
                pMat->GetTextureNum(), pMat->GetTexSRTCap(), tev.c_str(), kcol.c_str(),
                srt.c_str(), tex.empty() ? "-" : tex.c_str());
}

struct LayoutStats {
    int panes;
    int materials;
};

void PrintPane(const std::string& path, lyt::Pane* p, int depth, LayoutStats* pStats) {
    char buf[512];
    const math::VEC3& t = p->GetTranslate();
    const math::VEC3& r = p->GetRotate();
    const math::VEC2& s = p->GetScale();
    const lyt::Size& z = p->GetSize();
    const char* kind = PaneKind(p);

    std::string extra = "-";
    if (lyt::TextBox* pText = ut::DynamicCast<lyt::TextBox*>(p)) {
        const wchar_t* str = pText->GetString();
        std::snprintf(buf, sizeof(buf),
                      "buf=%u len=%zu hash=%016" PRIx64 " font=%s size=%.6g,%.6g space=%.6g,%.6g cols=%08x,%08x",
                      pText->GetStringBufferLength(), str ? wcslen(str) : 0,
                      str ? Fnv1a64Wide(str) : 0, pText->GetFont() != NULL ? "y" : "n",
                      pText->GetFontSize().width, pText->GetFontSize().height,
                      pText->GetCharSpace(), pText->GetLineSpace(),
                      pText->GetTextColor(0).ToU32(), pText->GetTextColor(1).ToU32());
        extra = buf;
    } else if (std::strcmp(kind, "pic1") == 0) {
        extra = "vtx=" + Colors(p);
    } else if (lyt::Window* pWnd = ut::DynamicCast<lyt::Window*>(p)) {
        extra = "vtx=" + Colors(p) + " frames=";
        for (u32 i = 0;; i++) {
            lyt::Material* pFrame = pWnd->GetFrameMaterial(i);
            if (pFrame == NULL) {
                break;
            }
            extra += (i ? "," : "") + Fixed(pFrame->GetName(), NW4R_LYT_MATERIAL_NAME_LEN);
        }
    }

    lyt::Material* pMat = p->GetMaterial();
    std::snprintf(buf, sizeof(buf), "%.6g,%.6g,%.6g\t%.6g,%.6g,%.6g\t%.6g,%.6g\t%.6g,%.6g",
                  t.x, t.y, t.z, r.x, r.y, r.z, s.x, s.y, z.width, z.height);
    std::printf("P\t%s\t%d\t%d\t%s\t%s\t%s\t%u\t%u\t%u,%u\t%s\t%s\t%s\n", path.c_str(),
                pStats->panes, depth, kind, Fixed(p->GetName(), NW4R_LYT_RES_NAME_LEN).c_str(),
                Hex(p->GetUserData(), NW4R_LYT_PANE_USERDATA_LEN).c_str(),
                p->IsVisible() ? 1u : 0u, p->GetAlpha(), p->GetBasePositionH(),
                p->GetBasePositionV(), buf,
                pMat != NULL ? Fixed(pMat->GetName(), NW4R_LYT_MATERIAL_NAME_LEN).c_str() : "-",
                extra.c_str());
    pStats->panes++;

    if (pMat != NULL) {
        PrintMaterial(path, pMat);
        pStats->materials++;
    }
    if (lyt::Window* pWnd = ut::DynamicCast<lyt::Window*>(p)) {
        for (u32 i = 0;; i++) {
            lyt::Material* pFrame = pWnd->GetFrameMaterial(i);
            if (pFrame == NULL) {
                break;
            }
            PrintMaterial(path, pFrame);
            pStats->materials++;
        }
    }

    NW4R_UT_LINKLIST_FOREACH (it, p->GetChildList(),
                              { PrintPane(path, &*it, depth + 1, pStats); })
}

std::vector<std::string> ListDir(ARCHandle* pHandle, const char* pDir, const char* pExt) {
    std::vector<std::string> names;
    ARCDir dir;
    if (!ARCOpenDir(pHandle, pDir, &dir)) {
        return names;
    }
    ARCDirEntry ent;
    while (ARCReadDir(&dir, &ent)) {
        std::string n = ent.name;
        if (!ent.isDir && n.size() > std::strlen(pExt) &&
            n.compare(n.size() - std::strlen(pExt), std::string::npos, pExt) == 0) {
            names.push_back(n);
        }
    }
    ARCCloseDir(&dir);
    return names;
}

void TestLayoutArchive(const std::string& path, const ARCHandle& rHandle) {
    // Game layout archives keep blyt/anim/timg/font at the root and are
    // attached with root "" (RPSysLytResAccessor::mountArchive); the HOME
    // Menu archives keep them under arc/.
    ARCHandle handle = rHandle;
    const char* pRoot;
    if (ARCConvertPathToEntrynum(&handle, "/blyt") != -1) {
        pRoot = "";
    } else if (ARCConvertPathToEntrynum(&handle, "/arc/blyt") != -1) {
        pRoot = "arc";
    } else {
        return;
    }
    const std::string dir = std::string("/") + pRoot + (*pRoot ? "/" : "");

    lyt::ArcResourceAccessor accessor;
    if (!accessor.Attach(handle.archiveStartAddr, pRoot)) {
        std::fprintf(stderr, "ERROR: Attach failed: %s\n", path.c_str());
        disc_walk::Errors()++;
        return;
    }

    std::vector<std::string> layouts = ListDir(&handle, (dir + "blyt").c_str(), ".brlyt");
    std::vector<std::string> anims = ListDir(&handle, (dir + "anim").c_str(), ".brlan");

    for (const std::string& name : layouts) {
        std::string lytPath = path + dir + "blyt/" + name;
        void* pBin = accessor.GetResource(lyt::ArcResourceAccessor::RES_TYPE_LAYOUT,
                                          name.c_str(), NULL);

        lyt::Layout layout;
        if (pBin == NULL || !layout.Build(pBin, &accessor) || layout.GetRootPane() == NULL) {
            std::fprintf(stderr, "ERROR: Layout::Build failed: %s (resource %p)\n",
                         lytPath.c_str(), pBin);
            disc_walk::Errors()++;
            continue;
        }

        LayoutStats stats = {0, 0};
        PrintPane(lytPath, layout.GetRootPane(), 0, &stats);

        int groups = 0;
        if (layout.GetGroupContainer() != NULL) {
            NW4R_UT_LINKLIST_FOREACH (it, layout.GetGroupContainer()->GetGroupList(), {
                std::string names;
                NW4R_UT_LINKLIST_FOREACH (pl, it->GetPaneList(), {
                    names += (names.empty() ? "" : ",") +
                             Fixed(pl->mTarget->GetName(), NW4R_LYT_RES_NAME_LEN);
                })
                std::printf("G\t%s\t%s\t%s\n", lytPath.c_str(),
                            Fixed(it->GetName(), NW4R_LYT_RES_NAME_LEN).c_str(),
                            names.empty() ? "-" : names.c_str());
                groups++;
            })
        }

        ut::Rect rect = layout.GetLayoutRect();
        std::printf("L\t%s\t%.6g\t%.6g\t%d\t%d\t%d\n", lytPath.c_str(), rect.right - rect.left,
                    rect.top - rect.bottom < 0 ? rect.bottom - rect.top : rect.top - rect.bottom,
                    stats.panes, stats.materials, groups);
        sLayouts++;
        sPanes += stats.panes;

        for (const std::string& anim : anims) {
            std::string anmPath = path + dir + "anim/" + anim;
            void* pAnm = accessor.GetResource(lyt::ArcResourceAccessor::RES_TYPE_ANIMATION,
                                              anim.c_str(), NULL);
            lyt::AnimTransform* pTrans =
                pAnm != NULL ? layout.CreateAnimTransform(pAnm, &accessor) : NULL;
            if (pTrans == NULL) {
                std::fprintf(stderr, "ERROR: CreateAnimTransform failed: %s\n", anmPath.c_str());
                disc_walk::Errors()++;
                continue;
            }
            layout.BindAnimation(pTrans);

            const lyt::res::AnimationBlock* pBlock = pTrans->GetAnimResource();
            const lyt::AnimationLink* pLinks =
                AnimProbe::Links(static_cast<lyt::AnimTransformBasic*>(pTrans));
            const be_u32* pOffs = lyt::detail::ConvertOffsToPtr<be_u32>(
                pBlock, pBlock->animContOffsetsOffset);

            int bound = 0;
            for (u16 i = 0; i < pBlock->animContNum; i++) {
                const lyt::res::AnimationContent* pContent =
                    lyt::detail::ConvertOffsToPtr<lyt::res::AnimationContent>(pBlock, pOffs[i]);
                bool b = pLinks != NULL && pLinks[i].GetAnimTransform() == pTrans;
                bound += b;
                std::printf("AC\t%s\t%s\t%u\t%s\t%u\t%d\n", anmPath.c_str(), lytPath.c_str(), i,
                            Fixed(pContent->name, lyt::res::AnimationContent::NAME_LEN).c_str(),
                            pContent->type, b ? 1 : 0);
            }
            std::printf("A\t%s\t%s\t%u\t%d\t%u\t%u\t%d\n", anmPath.c_str(), lytPath.c_str(),
                        pTrans->GetFrameSize(), pTrans->IsLoopData() ? 1 : 0,
                        static_cast<u32>(pBlock->fileNum), static_cast<u32>(pBlock->animContNum),
                        bound);
            sBinds++;
            sBoundContents += bound;
        }
    }
    sAnims += static_cast<int>(anims.size());
}

void Leaf(const std::string& path, u8* p, u32 size) {
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".tpl") == 0) {
        TestTpl(path, p, size);
    }
}

void* HostAlloc(MEMAllocator*, u32 size) {
    return PortArenaAlloc(size, 32);
}
void HostFree(MEMAllocator*, void* p) {
    PortArenaFree(p);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> [path ...]\n", argv[0]);
        return 2;
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    // What the game does at boot (RPSysLayoutManager / EGG): lyt allocates
    // through an MEMAllocator.
    static const MEMAllocatorFuncs sFuncs = {HostAlloc, HostFree};
    static MEMAllocator sAllocator = {&sFuncs, NULL, 0, 0};
    lyt::Layout::SetAllocator(&sAllocator);

    std::vector<std::string> paths;
    if (argc > 2) {
        paths.assign(argv + 2, argv + argc);
    } else {
        disc_walk::ListDisc(argv[1], "", &paths);
    }

    disc_walk::Visitor visitor = {Leaf, TestLayoutArchive};
    disc_walk::WalkDisc(visitor, paths);

    std::fprintf(stderr,
                 "lyt_harness: %d TPL files (%d already bound by a layout), %d layouts "
                 "(%d panes), %d animations (%d binds, %d bound contents), %d errors "
                 "(pointer size %zu)\n",
                 sTpls, sTplBoundByLayout, sLayouts, sPanes, sAnims, sBinds, sBoundContents,
                 disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
