#ifndef RP_PARTY_COMMON_KCL_H
#define RP_PARTY_COMMON_KCL_H
#include <Pack/types_pack.h>

#include <nw4r/math.h>

#include <port/be.h>
#include <port/bemath.h>
#include <port/resptr.h>

//! @addtogroup rp_party
//! @{

/**
 * @brief One KCL prism (triangle extruded along normal0), 0x10 bytes
 * @note Field names follow the common KCL format.
 */
struct RPKclPrism {
    be_f32 height;      // at 0x0
    be_u16 positionIdx; // at 0x4
    be_u16 normal0Idx;  // at 0x6 face normal
    be_u16 normal1Idx;  // at 0x8 edge normals
    be_u16 normal2Idx;  // at 0xA
    be_u16 normal3Idx;  // at 0xC
    be_u16 attribute;   // at 0xE
};

/**
 * @brief KCL collision file header (offsets relocated to pointers in RPKcl's copy)
 * @note Field names follow the common KCL format; Wii Play's own names are unknown.
 */
struct RPKclHeader {
    RES_PTR(BE_MATH(VEC3)) pPositions; // at 0x0
    RES_PTR(BE_MATH(VEC3)) pNormals;   // at 0x4
    RES_PTR(RPKclPrism) pPrisms;       // at 0x8
    RES_PTR(void) pBlocks;             // at 0xC octree
    be_f32 thickness;                  // at 0x10
    BE_MATH(VEC3) areaMin;             // at 0x14
    be_u32 maskX;                      // at 0x20
    be_u32 maskY;                      // at 0x24
    be_u32 maskZ;                      // at 0x28
    be_u32 shift;                      // at 0x2C
    be_u32 shiftY;                     // at 0x30
    be_u32 shiftZ;                     // at 0x34
    be_f32 sphereRadius;               // at 0x38
};

/**
 * @brief One collision result of RPKcl::CheckPoint/SearchSphereOctree, 0x20 bytes
 */
struct RPKclHit {
    nw4r::math::VEC3 position; // at 0x0 prism vertex
    nw4r::math::VEC3 normal;   // at 0xC push-out direction
    f32 distance;              // at 0x18 push-out depth
    u16 attribute;             // at 0x1C prism attribute
};

/**
 * @brief KCL triangle-prism collision data
 * @details No vtable. Size 0x40 [H] (first base of RPCollisionModel, which starts with it).
 * SetResource copies the file header into mHeader, relocates the four section offsets and
 * points mpHeader at mHeader; it does nothing when already set (mpHeader != NULL).
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPKcl {
public:
    RPKcl() : mpHeader(NULL) {}

    /**
     * @brief Copies the file header into mHeader and relocates its section offsets [H]
     */
    void SetResource(void* pKclFile);
    /**
     * @brief Collects up to maxHits prisms under rPos [M]
     * @return The number of hits found (may exceed maxHits)
     */
    s32 CheckPoint(RPKclHit* pHits, s32 maxHits, const nw4r::math::VEC3& rPos) const;
    /**
     * @brief Collects up to maxHits prisms within radius of rCenter, walking the octree [M]
     * @return The number of hits found (may exceed maxHits)
     */
    s32 SearchSphereOctree(RPKclHit* pHits, s32 maxHits, const nw4r::math::VEC3& rCenter,
                           f32 radius) const;
    /**
     * @brief Tests one prism against the sphere, appends a hit when it overlaps [M]
     */
    BOOL CheckSphereTri(RPKclHit* pHits, s32* pHitCount, s32 maxHits, RPKclPrism* pPrism,
                        nw4r::math::VEC3& rCenter, f32 radius) const;

protected:
    RPKclHeader* mpHeader; // at 0x0
    RPKclHeader mHeader;   // at 0x4
};

//! @}

#endif
