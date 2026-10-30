// KCL collision (unit 0x801B7748-0x801B83A8; .sdata2 pool 0.0f, 1.0f).
#include <Pack/RPParty/RPPartyCommon/RPKcl.h>

#include <Pack/RPParty/RPPartyCommon/RPSimpleModel.h>

#include <egg/gfxe/eggDrawGX.h>

#include <revolution/GX.h>

#include <math.h>

extern "C" {
void RP_makeMtxFromRotPos(nw4r::math::MTX34* pMtx, const nw4r::math::VEC3* pPos,
                          const nw4r::math::MTX33* pRot);
void RP_drawShadowVolume(const nw4r::math::MTX34* pMtx);
}

void RPKcl::SetResource(void* pKclFile) {
    if (pKclFile == NULL) {
        return;
    }
    if (mpHeader != NULL) {
        return;
    }

    mpHeader = static_cast<RPKclHeader*>(pKclFile);
    mHeader = *mpHeader;
    mpHeader = &mHeader;
    RPKclHeader* pHeader = mpHeader;

    // The file's header holds the four sections as offsets from its start
    const RPKclHeader* pFile = static_cast<const RPKclHeader*>(pKclFile);
    pHeader->pPositions = reinterpret_cast<BE_MATH(VEC3)*>(
        static_cast<u8*>(pKclFile) + RES_PTR_RAW(pFile->pPositions));
    pHeader->pNormals = reinterpret_cast<BE_MATH(VEC3)*>(
        static_cast<u8*>(pKclFile) + RES_PTR_RAW(pFile->pNormals));
    pHeader->pPrisms = reinterpret_cast<RPKclPrism*>(
        static_cast<u8*>(pKclFile) + RES_PTR_RAW(pFile->pPrisms));
    pHeader->pBlocks = static_cast<u8*>(pKclFile) + RES_PTR_RAW(pFile->pBlocks);
}

s32 RPKcl::CheckPoint(RPKclHit* pHits, s32 maxHits, const nw4r::math::VEC3& rPos) const {
    RPKclHeader* pHeader = mpHeader;
    RPKclHit* pHit = pHits;
    s32 hitCount = 0;
    nw4r::math::VEC3 pos;
    pos.x = rPos.x;
    pos.y = rPos.y;
    pos.z = rPos.z;
    u32 localX = static_cast<s32>(pos.x - pHeader->areaMin.x);
    if ((localX & pHeader->maskX) != 0) {
        return 0;
    }
    u32 localY = static_cast<s32>(pos.y - pHeader->areaMin.y);
    if ((localY & pHeader->maskY) != 0) {
        return 0;
    }
    u32 localZ = static_cast<s32>(pos.z - pHeader->areaMin.z);
    if ((localZ & pHeader->maskZ) != 0) {
        return 0;
    }

    u32 level = pHeader->shift;
    u32 key = (((localZ >> level) << pHeader->shiftZ) |
              ((localY >> level) << pHeader->shiftY) | (localX >> level)) * 4;
    // The octree links nodes and prism lists by byte offsets (KCL format)
    u8* pNode = static_cast<u8*>(RES_PTR_GET(pHeader->pBlocks));
    s32 nodeValue;
    while ((nodeValue = *reinterpret_cast<be_s32*>(pNode + key)) >= 0) {
        pNode += nodeValue;
        --level;
        key = ((((localZ >> level) & 1) << 2) |
              (((localY >> level) & 1) << 1) | ((localX >> level) & 1)) * 4;
    }

    be_u16* pList = reinterpret_cast<be_u16*>(pNode + (nodeValue & 0x7fffffff));
    while (*++pList != 0) {
        const BE_MATH(VEC3)* pNormal;
        const BE_MATH(VEC3)* pPosition;
        RPKclPrism* pPrism = &pHeader->pPrisms[*pList];
        pPosition = &pHeader->pPositions[pPrism->positionIdx];
        nw4r::math::VEC3 offset;
        offset.x = pos.x - pPosition->x;
        offset.y = pos.y - pPosition->y;
        offset.z = pos.z - pPosition->z;
        pNormal = &pHeader->pNormals[pPrism->normal1Idx];
        if (offset.x * pNormal->x + offset.y * pNormal->y + offset.z * pNormal->z > 0.0f) {
            continue;
        }
        pNormal = &pHeader->pNormals[pPrism->normal2Idx];
        if (offset.x * pNormal->x + offset.y * pNormal->y + offset.z * pNormal->z > 0.0f) {
            continue;
        }
        pNormal = &pHeader->pNormals[pPrism->normal3Idx];
        if (offset.x * pNormal->x + offset.y * pNormal->y + offset.z * pNormal->z > pPrism->height) {
            continue;
        }
        pNormal = &pHeader->pNormals[pPrism->normal0Idx];
        f32 distance = -offset.x * pNormal->x - offset.y * pNormal->y - offset.z * pNormal->z;
        if (distance < 0.0f || distance > pHeader->thickness) {
            continue;
        }
        if (hitCount < maxHits) {
            pHit->position = *pPosition;
            pHit->normal = *pNormal;
            pHit->distance = distance;
            pHit->attribute = pPrism->attribute;
        }
        ++pHit;
        ++hitCount;
    }

    return hitCount;
}

s32 RPKcl::SearchSphereOctree(RPKclHit* pHits, s32 maxHits, const nw4r::math::VEC3& rCenter,
                                f32 radius) const {
    s32 hitCount;
    nw4r::math::VEC3 center;
    nw4r::math::VEC3 minimum;
    nw4r::math::VEC3 maximum;
    center.x = rCenter.x;
    center.y = rCenter.y;
    center.z = rCenter.z;
    minimum.x = center.x - radius;
    minimum.y = center.y - radius;
    minimum.z = center.z - radius;
    maximum.x = center.x + radius;
    maximum.y = center.y + radius;
    maximum.z = center.z + radius;
    RPKclHeader* pHeader = mpHeader;
    hitCount = 0;
    nw4r::math::VEC3* pCenter = &center;
    u32 highX;
    u32 x;
    u32 y;
    u32 z;
    s32 remainingX;
    s32 yStep;
    s32 zStep;
    s32 longestY;
    be_u16* pList;
    be_u16* pListCandidate = NULL;
    be_u16* pPreviousList = NULL;
    u32 lowX;
    lowX = static_cast<s32>(minimum.x - pHeader->areaMin.x);
    if (static_cast<s32>(lowX) < 0) {
        lowX = 0;
    }
    u32 lowY = static_cast<s32>(minimum.y - pHeader->areaMin.y);
    if (static_cast<s32>(lowY) < 0) {
        lowY = 0;
    }
    z = static_cast<s32>(minimum.z - pHeader->areaMin.z);
    if (static_cast<s32>(z) < 0) {
        z = 0;
    }
    highX = static_cast<s32>(maximum.x - pHeader->areaMin.x);
    if (static_cast<s32>(highX) > static_cast<s32>(~pHeader->maskX)) {
        highX = ~pHeader->maskX;
    }
    u32 highY = static_cast<s32>(maximum.y - pHeader->areaMin.y);
    if (static_cast<s32>(highY) > static_cast<s32>(~pHeader->maskY)) {
        highY = ~pHeader->maskY;
    }
    u32 highZ = static_cast<s32>(maximum.z - pHeader->areaMin.z);
    if (static_cast<s32>(highZ) > static_cast<s32>(~pHeader->maskZ)) {
        highZ = ~pHeader->maskZ;
    }
    if (static_cast<s32>(lowX) >= static_cast<s32>(highX) ||
        static_cast<s32>(lowY) >= static_cast<s32>(highY) ||
        static_cast<s32>(z) >= static_cast<s32>(highZ)) {
        return 0;
    }

    do {
        zStep = 1000000;
        y = lowY;
        do {
            yStep = 1000000;
            longestY = 0;
            x = lowX;
            do {
                // The octree links nodes and prism lists by byte offsets (KCL format)
                u8* pNode = static_cast<u8*>(RES_PTR_GET(pHeader->pBlocks));
                u32 level = pHeader->shift;
                u32 key = (((z >> level) << pHeader->shiftZ) |
                          ((y >> level) << pHeader->shiftY) | (x >> level)) * 4;
                s32 nodeValue;
                while ((nodeValue = *reinterpret_cast<be_s32*>(pNode + key)) >= 0) {
                    pNode += nodeValue;
                    --level;
                    key = ((((z >> level) & 1) << 2) |
                          (((y >> level) & 1) << 1) | ((x >> level) & 1)) * 4;
                }

                u32 cellSize = 1U << level;
                u32 cellMask = cellSize - 1;
                pList = reinterpret_cast<be_u16*>(
                    pNode + (nodeValue & 0x7fffffff));
                s32 remainingZ = cellSize - (z & cellMask);
                remainingX = cellSize - (x & cellMask);
                s32 remainingY = cellSize - (y & cellMask);
                if (remainingZ < zStep) {
                    zStep = remainingZ;
                }
                if (remainingY < yStep) {
                    yStep = remainingY;
                }

                if (remainingY > longestY && pList[1] != 0) {
                    longestY = remainingY;
                    pListCandidate = pList;
                }

                if (pList != pPreviousList) {
                    while (*++pList != 0) {
                        RPKclPrism* pPrism = &pHeader->pPrisms[*pList];
                        CheckSphereTri(pHits, &hitCount, maxHits, pPrism, *pCenter, radius);
                    }
                }
                x += remainingX;
            } while (x <= highX);
            y += yStep;
            pPreviousList = pListCandidate;
        } while (y <= highY);
        z += zStep;
    } while (z <= highZ);

    return hitCount;
}

BOOL RPKcl::CheckSphereTri(RPKclHit* pHits, s32* pHitCount, s32 maxHits, RPKclPrism* pPrism,
                           nw4r::math::VEC3& rCenter, f32 radius) const {
    nw4r::math::VEC3 offset;
    f32 distance[4];
    f32 radiusSq = radius * radius;
    RPKclHeader* pHeader = mpHeader;
    const BE_MATH(VEC3)& rPosition = pHeader->pPositions[pPrism->positionIdx];
    offset.x = rCenter.x - rPosition.x;
    offset.y = rCenter.y - rPosition.y;
    offset.z = rCenter.z - rPosition.z;
    const BE_MATH(VEC3)& rNormal1 = pHeader->pNormals[pPrism->normal1Idx];
    distance[1] = offset.x * rNormal1.x + offset.y * rNormal1.y + offset.z * rNormal1.z;
    if (distance[1] >= radius) {
        return FALSE;
    }
    const BE_MATH(VEC3)& rNormal2 = pHeader->pNormals[pPrism->normal2Idx];
    distance[2] = offset.x * rNormal2.x + offset.y * rNormal2.y + offset.z * rNormal2.z;
    if (distance[2] >= radius) {
        return FALSE;
    }
    const BE_MATH(VEC3)& rNormal3 = pHeader->pNormals[pPrism->normal3Idx];
    distance[3] = offset.x * rNormal3.x + offset.y * rNormal3.y + offset.z * rNormal3.z - pPrism->height;
    if (distance[3] >= radius) {
        return FALSE;
    }
    const BE_MATH(VEC3)& rNormal0 = pHeader->pNormals[pPrism->normal0Idx];
    distance[0] = offset.x * rNormal0.x + offset.y * rNormal0.y + offset.z * rNormal0.z;
    f32 penetration = radius - distance[0];
    if (penetration < 0.0f || penetration > pHeader->thickness) {
        return FALSE;
    }

    f32 dot12, dotOther, sqrtInput, lengthSq;
    if (distance[1] > distance[2]) {
        if (!(distance[1] > distance[3])) {
            goto edge3;
        }
        goto edge1;
    }
    if (!(distance[2] > distance[3])) {
        goto edge3;
    }
    goto edge2;

edge1:
    if (distance[1] <= 0.0f) {
        goto hit;
    }
    if (distance[2] > distance[3]) {
        dot12 = rNormal1.x * rNormal2.x + rNormal1.y * rNormal2.y + rNormal1.z * rNormal2.z;
        if (!(dot12 * distance[1] > distance[2])) {
            goto corner12;
        }
        goto face1;
    }
    dotOther = rNormal1.x * rNormal3.x + rNormal1.y * rNormal3.y + rNormal1.z * rNormal3.z;
    if (!(dotOther * distance[1] > distance[3])) {
        goto corner31;
    }
    goto face1;

edge2:
    if (distance[2] <= 0.0f) {
        goto hit;
    }
    if (distance[3] > distance[1]) {
        dotOther = rNormal2.x * rNormal3.x + rNormal2.y * rNormal3.y + rNormal2.z * rNormal3.z;
        if (!(dotOther * distance[2] > distance[3])) {
            goto corner23;
        }
        goto face2;
    }
    dot12 = rNormal2.x * rNormal1.x + rNormal2.y * rNormal1.y + rNormal2.z * rNormal1.z;
    if (!(dot12 * distance[2] > distance[1])) {
        goto corner12;
    }
    goto face2;

edge3:
    if (distance[3] <= 0.0f) {
        goto hit;
    }
    if (distance[1] > distance[2]) {
        dotOther = rNormal3.x * rNormal1.x + rNormal3.y * rNormal1.y + rNormal3.z * rNormal1.z;
        if (!(dotOther * distance[3] > distance[1])) {
            goto corner31;
        }
        goto face3;
    }
    dotOther = rNormal3.x * rNormal2.x + rNormal3.y * rNormal2.y + rNormal3.z * rNormal2.z;
    if (!(dotOther * distance[3] > distance[2])) {
        goto corner23;
    }
    goto face3;

face1:
    if (distance[1] > distance[0]) {
        return FALSE;
    }
    sqrtInput = radiusSq - distance[1] * distance[1];
    goto depth;
face2:
    if (distance[2] > distance[0]) {
        return FALSE;
    }
    sqrtInput = radiusSq - distance[2] * distance[2];
    goto depth;
face3:
    if (distance[3] > distance[0]) {
        return FALSE;
    }
    sqrtInput = radiusSq - distance[3] * distance[3];
    goto depth;

corner12:
    {
        f32 a = (dot12 * distance[2] - distance[1]) / (dot12 * dot12 - 1.0f);
        f32 b = distance[2] - a * dot12;
        offset.x = a * rNormal1.x + b * rNormal2.x;
        offset.y = a * rNormal1.y + b * rNormal2.y;
        offset.z = a * rNormal1.z + b * rNormal2.z;
    }
    goto length;
corner23:
    {
        f32 a = (dotOther * distance[3] - distance[2]) / (dotOther * dotOther - 1.0f);
        f32 b = distance[3] - a * dotOther;
        offset.x = a * rNormal2.x + b * rNormal3.x;
        offset.y = a * rNormal2.y + b * rNormal3.y;
        offset.z = a * rNormal2.z + b * rNormal3.z;
    }
    goto length;
corner31:
    {
        f32 a = (dotOther * distance[1] - distance[3]) / (dotOther * dotOther - 1.0f);
        f32 b = distance[1] - a * dotOther;
        offset.x = a * rNormal3.x + b * rNormal1.x;
        offset.y = a * rNormal3.y + b * rNormal1.y;
        offset.z = a * rNormal3.z + b * rNormal1.z;
    }
length:
    lengthSq = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    {
        f32 len = sqrt(lengthSq);
        if (len > distance[0] || len >= radius) {
            return FALSE;
        }
    }
    sqrtInput = radiusSq - lengthSq;
depth:
    penetration = static_cast<f32>(sqrt(sqrtInput)) - distance[0];
    if (penetration < 0.0f) {
        return FALSE;
    }
hit:
    if (*pHitCount < maxHits) {
        pHits[*pHitCount].position.x = rPosition.x;
        pHits[*pHitCount].position.y = rPosition.y;
        pHits[*pHitCount].position.z = rPosition.z;
        pHits[*pHitCount].normal.x = rNormal0.x;
        pHits[*pHitCount].normal.y = rNormal0.y;
        pHits[*pHitCount].normal.z = rNormal0.z;
        pHits[*pHitCount].distance = penetration;
        pHits[*pHitCount].attribute = pPrism->attribute;
    }
    ++*pHitCount;
    return TRUE;
}
