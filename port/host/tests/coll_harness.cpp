/**
 * KCL and PMP harness (port/docs/HOST_BUILD.md §14, POINTERS.md §3.5-3.6).
 *
 * Walks the disc like the other harnesses (disc_walk.h) and hands every
 * .kcl and .pmp file, in place inside its mounted archive, to the game's own
 * loaders:
 *
 *   KCL  RPKcl::SetResource (src/Pack/RPParty/RPPartyCommon/RPKcl.cpp), as
 *        RPBilTable / RPFshStage / RPCowGame do, then the collision queries
 *        RPKcl::CheckPoint and RPKcl::SearchSphereOctree (CheckSphereTri) at
 *        one point inside every prism.
 *   PMP  RPResTable::RPResTable (RPResFile_4.cpp: RPResFile::Create and
 *        Relocate, RPResFile_3.cpp) as RPCowGame's LOAD_FIELD does, then
 *        CountEntriesOfType / FindNthEntryOfType for every Cow object type and
 *        RPPathObject (RPPathFollower_2.cpp) on every entry they return.
 *
 * Lines (tab separated; floats as the hex of their bits):
 *   KH  KCL header as RPKcl holds it (mHeader): the four section slots as file
 *       offsets, thickness, areaMin, masks, shifts, sphereRadius
 *   KF  the file is unchanged after SetResource, and a second SetResource is
 *       a no-op
 *   KP  every prism (from 1; prism 0 is the format's dummy) read through
 *       mHeader: height, indices, attribute, its position and four normals
 *   KQ  CheckPoint at a point inside the prism: the point, the hit count and
 *       up to 8 hits (position, normal, distance, attribute)
 *   KS  SearchSphereOctree with a sphere at that point: radius, count, hits
 *   PB  PMP: emulated address of the file
 *   PH  PMP header after Relocate (through RPResData): counts, flags, slots
 *   PW  every run of bytes that differs from the disc file after Relocate
 *   PC  CountEntriesOfType(type)
 *   PN  FindNthEntryOfType(type, n): entry index
 *   PO  RPPathObject on that entry: position, vec20, radius, GetParam(0..7)
 *   PR  RPPathObject rotation (calcRPY; compared with a tolerance)
 *   PS  synthetic PMP with paths and points (not on the disc): slot checks
 *
 * tests/test_collision.py builds the same lines from the bytes and from
 * port/assets/tools/fmt_collision.py.
 */
#include <Pack/RPParty/RPPartyCommon/RPKcl.h>
#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>
#include <Pack/RPParty/RPPartyCommon/RPResFile_2.h>
#include <Pack/RPSystem/RPSysResourceManager.h>

#include <port/arena.h>
#include <port/be.h>
#include <port/resptr.h>

#include "disc_walk.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int sKcl = 0, sPrisms = 0, sPointHits = 0, sSphereHits = 0, sQueries = 0;
int sPmp = 0, sEntries = 0, sObjects = 0, sChanged = 0;

u32 Bits(f32 f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/******************************************************************************
 * KCL
 ******************************************************************************/
class HostKcl : public RPKcl {
public:
    const RPKclHeader* Header() const { return mpHeader; }
    const RPKclHeader* Copy() const { return &mHeader; }
};

struct D3 {
    double x, y, z;
};
D3 ToD3(const nw4r::math::VEC3& v) {
    D3 d = {v.x, v.y, v.z};
    return d;
}
D3 Cross(const D3& a, const D3& b) {
    D3 r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return r;
}
double Dot(const D3& a, const D3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void PrintHits(const RPKclHit* pHits, s32 count, s32 max) {
    for (s32 i = 0; i < count && i < max; i++) {
        const RPKclHit& h = pHits[i];
        std::printf("\t%08x,%08x,%08x,%08x,%08x,%08x,%08x,%u", Bits(h.position.x),
                    Bits(h.position.y), Bits(h.position.z), Bits(h.normal.x), Bits(h.normal.y),
                    Bits(h.normal.z), Bits(h.distance), h.attribute);
    }
    std::printf("\n");
}

void TestKcl(const std::string& path, u8* pData, u32 size) {
    sKcl++;
    std::vector<u8> orig(pData, pData + size);

    HostKcl kcl;
    kcl.SetResource(pData);
    const RPKclHeader* h = kcl.Header();
    if (h != kcl.Copy()) {
        std::fprintf(stderr, "ERROR: %s: mpHeader does not point at the copy\n", path.c_str());
        disc_walk::Errors()++;
        return;
    }

    const u8* pPos = reinterpret_cast<const u8*>(RES_PTR_GET(h->pPositions));
    const u8* pNrm = reinterpret_cast<const u8*>(RES_PTR_GET(h->pNormals));
    const u8* pPrs = reinterpret_cast<const u8*>(RES_PTR_GET(h->pPrisms));
    const u8* pBlk = static_cast<const u8*>(RES_PTR_GET(h->pBlocks));
    std::printf("KH\t%s\t%ld\t%ld\t%ld\t%ld\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%u\t%u\t%u\t%08x\n",
                path.c_str(), static_cast<long>(pPos - pData), static_cast<long>(pNrm - pData),
                static_cast<long>(pPrs - pData), static_cast<long>(pBlk - pData),
                Bits(h->thickness), Bits(h->areaMin.x), Bits(h->areaMin.y), Bits(h->areaMin.z),
                static_cast<u32>(h->maskX), static_cast<u32>(h->maskY), static_cast<u32>(h->maskZ),
                static_cast<u32>(h->shift), static_cast<u32>(h->shiftY),
                static_cast<u32>(h->shiftZ), Bits(h->sphereRadius));

    // SetResource again: mpHeader != NULL, nothing happens.
    RPKclHeader before = *h;
    kcl.SetResource(pData + 4);
    bool noop = std::memcmp(&before, h, sizeof(before)) == 0 && kcl.Header() == kcl.Copy();
    bool unchanged = std::memcmp(orig.data(), pData, size) == 0;
    std::printf("KF\t%s\t%d\t%d\n", path.c_str(), unchanged ? 1 : 0, noop ? 1 : 0);

    s32 prismCount = static_cast<s32>((pBlk - pPrs) / sizeof(RPKclPrism));
    const f32 thickness = h->thickness;
    for (s32 i = 1; i < prismCount; i++) {
        RPKclPrism* pPrism = &h->pPrisms[i];
        const BE_MATH(VEC3)& a = h->pPositions[pPrism->positionIdx];
        const BE_MATH(VEC3)* n[4] = {&h->pNormals[pPrism->normal0Idx],
                                     &h->pNormals[pPrism->normal1Idx],
                                     &h->pNormals[pPrism->normal2Idx],
                                     &h->pNormals[pPrism->normal3Idx]};
        sPrisms++;
        std::printf("KP\t%s\t%d\t%08x\t%u\t%u\t%u\t%u\t%u\t%u\t%08x,%08x,%08x", path.c_str(), i,
                    Bits(pPrism->height), static_cast<u16>(pPrism->positionIdx),
                    static_cast<u16>(pPrism->normal0Idx), static_cast<u16>(pPrism->normal1Idx),
                    static_cast<u16>(pPrism->normal2Idx), static_cast<u16>(pPrism->normal3Idx),
                    static_cast<u16>(pPrism->attribute), Bits(a.x), Bits(a.y), Bits(a.z));
        for (int k = 0; k < 4; k++) {
            std::printf("\t%08x,%08x,%08x", Bits(n[k]->x), Bits(n[k]->y), Bits(n[k]->z));
        }
        std::printf("\n");

        // A point inside the prism: the triangle's centroid, half the thickness
        // below the face. The vertices follow the KCL format (position, then
        // the edge normals crossed with the face normal, scaled by height).
        D3 A = ToD3(a), N0 = ToD3(*n[0]), N1 = ToD3(*n[1]), N2 = ToD3(*n[2]), N3 = ToD3(*n[3]);
        double height = pPrism->height;
        D3 crossA = Cross(N1, N0), crossB = Cross(N2, N0);
        double dA = Dot(crossA, N3), dB = Dot(crossB, N3);
        if (dA == 0.0 || dB == 0.0) {
            continue;
        }
        double sB = height / dB, sA = height / dA;
        D3 B = {A.x + crossB.x * sB, A.y + crossB.y * sB, A.z + crossB.z * sB};
        D3 C = {A.x + crossA.x * sA, A.y + crossA.y * sA, A.z + crossA.z * sA};
        double half = static_cast<double>(thickness) * 0.5;
        D3 G = {(A.x + B.x + C.x) / 3.0 - N0.x * half, (A.y + B.y + C.y) / 3.0 - N0.y * half,
                (A.z + B.z + C.z) / 3.0 - N0.z * half};
        D3 AB = {B.x - A.x, B.y - A.y, B.z - A.z};
        double radius = std::sqrt(Dot(AB, AB)) * 0.25;
        if (!std::isfinite(G.x) || !std::isfinite(G.y) || !std::isfinite(G.z) ||
            !std::isfinite(radius) || radius == 0.0) {
            continue;
        }
        nw4r::math::VEC3 q(static_cast<f32>(G.x), static_cast<f32>(G.y), static_cast<f32>(G.z));
        f32 r = static_cast<f32>(radius);
        sQueries++;

        RPKclHit hits[8];
        std::memset(hits, 0, sizeof(hits));
        s32 count = kcl.CheckPoint(hits, 8, q);
        sPointHits += count;
        std::printf("KQ\t%s\t%d\t%08x,%08x,%08x\t%d", path.c_str(), i, Bits(q.x), Bits(q.y),
                    Bits(q.z), count);
        PrintHits(hits, count, 8);

        std::memset(hits, 0, sizeof(hits));
        count = kcl.SearchSphereOctree(hits, 8, q, r);
        sSphereHits += count;
        std::printf("KS\t%s\t%d\t%08x\t%d", path.c_str(), i, Bits(r), count);
        PrintHits(hits, count, 8);
    }
}

/******************************************************************************
 * PMP
 ******************************************************************************/
const std::string* spCurrentName = NULL;
u8* spCurrentData = NULL;
u32 sCurrentSize = 0;

void PrintChanges(const char* tag, const std::string& path, const u8* pOrig, const u8* pNow,
                  u32 size) {
    for (u32 i = 0; i < size;) {
        if (pOrig[i] == pNow[i]) {
            i++;
            continue;
        }
        u32 j = i;
        while (j < size && pOrig[j] != pNow[j]) {
            j++;
        }
        std::printf("%s\t%s\t%u\t", tag, path.c_str(), i);
        for (u32 k = i; k < j; k++) {
            std::printf("%02x", pNow[k]);
        }
        std::printf("\n");
        sChanged++;
        i = j;
    }
}

void TestPmp(const std::string& path, u8* pData, u32 size) {
    sPmp++;
    std::vector<u8> orig(pData, pData + size);

    // RPCowGame: mpResTable = new RPResTable(pmp) on the scene's common
    // archive; the stand-in resource manager below returns this file.
    std::string name = path.substr(path.rfind('/') + 1);
    spCurrentName = &name;
    spCurrentData = pData;
    sCurrentSize = size;
    RPResTable* pTable = new RPResTable(name.c_str());
    spCurrentName = NULL;

    const RPResData* pRes = reinterpret_cast<const RPResData*>(pData);
    std::printf("PB\t%s\t%08x\n", path.c_str(), port_vaddr(pData));
    long entries = pRes->pEntries == NULL
                       ? -1
                       : static_cast<long>(reinterpret_cast<const u8*>(
                                               RES_PTR_GET(pRes->pEntries)) -
                                           pData);
    std::printf("PH\t%s\t%u\t%u\t%u\t%u\t%ld\t%08x\t%08x\n", path.c_str(),
                static_cast<u16>(pRes->numEntries), static_cast<u16>(pRes->numPaths),
                static_cast<u16>(pRes->numPoints), static_cast<u16>(pRes->flags), entries,
                RES_PTR_RAW(pRes->pPaths), RES_PTR_RAW(pRes->pPoints));
    PrintChanges("PW", path, orig.data(), pData, size);

    for (u32 type = 0x70000; type <= 0x7000A; type++) {
        u16 count = pTable->CountEntriesOfType(type);
        std::printf("PC\t%s\t%x\t%u\n", path.c_str(), type, count);
        for (u16 n = 0; n < count; n++) {
            const RPResEntry* pEntry =
                static_cast<const RPResEntry*>(pTable->FindNthEntryOfType(type, n));
            long index = pEntry == NULL ? -1 : static_cast<long>(pEntry - &pRes->pEntries[0]);
            std::printf("PN\t%s\t%x\t%u\t%ld\n", path.c_str(), type, n, index);
            if (pEntry == NULL) {
                continue;
            }
            sEntries++;

            // RPCowFieldObj & co.: new RPPathObject(0.1f, 1.0f, entry)
            RPPathObject* pObj = new RPPathObject(0.1f, 1.0f, pEntry);
            sObjects++;
            std::printf("PO\t%s\t%ld\t%08x,%08x,%08x\t%08x,%08x,%08x\t%08x", path.c_str(), index,
                        Bits(pObj->GetPos().x), Bits(pObj->GetPos().y), Bits(pObj->GetPos().z),
                        Bits(pObj->GetVec20().x), Bits(pObj->GetVec20().y),
                        Bits(pObj->GetVec20().z), Bits(pObj->GetRadius()));
            for (u16 k = 0; k < 8; k++) {
                std::printf("\t%d", pObj->GetParam(k, -12345));
            }
            std::printf("\n");
            std::printf("PR\t%s\t%ld\t%.9g\t%.9g\t%.9g\n", path.c_str(), index, pObj->GetRot().x,
                        pObj->GetRot().y, pObj->GetRot().z);
            delete pObj;
        }
    }
}

/**
 * A PMP with paths and points, built here (the disc's PMP files have none):
 * 3 entries (paths 1, none, 0), 2 paths (first points 2 and 0), 4 points.
 * RPResFile::Create relocates it; every slot must hold the big-endian
 * emulated address the Wii would store, and RP_getPathPoint must find the
 * points.
 */
void TestSyntheticPmp() {
    const u32 entries = 0x80, paths = entries + 3 * 0x58, points = paths + 2 * 0x20;
    const u32 size = points + 4 * 0x14;
    u8* p = new u8[size];
    std::memset(p, 0, size);
    std::memcpy(p, "PMPF", 4);
    port_store_be16(p + 0x10, 3);
    port_store_be16(p + 0x12, 2);
    port_store_be16(p + 0x14, 4);
    port_store_be32(p + 0x40, entries);
    port_store_be32(p + 0x44, paths);
    port_store_be32(p + 0x48, points);
    const u32 entryPath[3] = {1, 0xFFFFFFFF, 0};
    for (int i = 0; i < 3; i++) {
        port_store_be32(p + entries + i * 0x58, 0x70001);
        port_store_be32(p + entries + i * 0x58 + 0x44, entryPath[i]);
    }
    const u32 firstPoint[2] = {2, 0};
    for (int i = 0; i < 2; i++) {
        port_store_be16(p + paths + i * 0x20, 2);
        port_store_be32(p + paths + i * 0x20 + 0x18, firstPoint[i]);
    }
    for (int i = 0; i < 4; i++) {
        port_store_be16(p + points + i * 0x14 + 0xC, static_cast<u16>(100 + i));
    }

    RPResFile::Create(p, NULL);
    const RPResData* pRes = reinterpret_cast<const RPResData*>(p);
    const u32 base = port_vaddr(p);
    bool ok = port_load_be16(p + 8) == 1 && port_load_be32(p + 0x40) == base + entries &&
              port_load_be32(p + 0x44) == base + paths &&
              port_load_be32(p + 0x48) == base + points;
    for (int i = 0; i < 3; i++) {
        u32 want = entryPath[i] == 0xFFFFFFFF ? 0 : base + paths + entryPath[i] * 0x20;
        ok = ok && port_load_be32(p + entries + i * 0x58 + 0x44) == want;
    }
    for (int i = 0; i < 2; i++) {
        ok = ok && port_load_be32(p + paths + i * 0x20 + 0x18) ==
                       base + points + firstPoint[i] * 0x14;
        const RPPathRes* pPath = &pRes->pPaths[i];
        const RPPathPoint* pPoint = RP_getPathPoint(pPath, 1);
        ok = ok && pPoint == reinterpret_cast<const RPPathPoint*>(
                                 p + points + (firstPoint[i] + 1) * 0x14) &&
             pPoint->param0C == 100 + firstPoint[i] + 1;
    }
    ok = ok && pRes->pEntries[0].pPath == &pRes->pPaths[1] && pRes->pEntries[1].pPath == NULL;

    // Relocate runs once (flag bit 0).
    std::vector<u8> once(p, p + size);
    RPResFile::Create(p, NULL);
    ok = ok && std::memcmp(once.data(), p, size) == 0;

    std::printf("PS\tsynthetic\t%d\n", ok ? 1 : 0);
    if (!ok) {
        disc_walk::Errors()++;
    }
    delete[] p;
}

void Leaf(const std::string& path, u8* pData, u32 size) {
    if (EndsWith(path, ".kcl")) {
        TestKcl(path, pData, size);
    } else if (EndsWith(path, ".pmp")) {
        TestPmp(path, pData, size);
    }
}

} // namespace

/******************************************************************************
 * Stand-ins (not decompiled code)
 ******************************************************************************/

// RPResTable's lookups: the scene's common archive is the one the disc walk
// has mounted; the file is the one being tested.
EGG::Archive* RPSysResourceManager::LoadGameCommonArchive(s32, EGG::Heap*) {
    return NULL;
}

void* RPSysResourceManager::GetFileFromArchive(EGG::Archive*, const char* pName, u32* pSize) {
    if (spCurrentName == NULL || *spCurrentName != pName) {
        std::fprintf(stderr, "ERROR: unexpected GetFileFromArchive(%s)\n", pName);
        disc_walk::Errors()++;
        return NULL;
    }
    if (pSize != NULL) {
        *pSize = sCurrentSize;
    }
    return spCurrentData;
}

// RPPathFollower_2.cpp calls Matrix34f::calcRPY by its CodeWarrior name with
// the struct return passed as the first argument (CodeWarrior's ABI).
extern "C" void calcRPY__Q23EGG9Matrix34fCFv(EGG::Vector3f* pOut, const EGG::Matrix34f* pMtx) {
    *pOut = pMtx->calcRPY();
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

    disc_walk::Visitor visitor = {Leaf, NULL};
    disc_walk::WalkDisc(visitor, paths);
    TestSyntheticPmp();

    std::fprintf(stderr,
                 "coll_harness: %d KCL (%d prisms, %d point queries: %d point hits, %d sphere "
                 "hits), %d PMP (%d entries found by type, %d path objects, %d changed runs); "
                 "%d errors (pointer size %zu)\n",
                 sKcl, sPrisms, sQueries, sPointHits, sSphereHits, sPmp, sEntries, sObjects,
                 sChanged, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
