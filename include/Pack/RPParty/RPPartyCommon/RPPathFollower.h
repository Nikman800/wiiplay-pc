#ifndef RP_PARTY_COMMON_PATH_FOLLOWER_H
#define RP_PARTY_COMMON_PATH_FOLLOWER_H
#include <Pack/types_pack.h>

#include <egg/math.h>

#include <port/be.h>
#include <port/resptr.h>

//! @addtogroup rp_party
//! @{

// Forward declarations
class IRPPathSegmentArray;

/**
 * @brief One point of a path resource
 * @details Size 0x14 [H] (stride of RP_getPathPoint). The s16 parameters are read by
 * RPPathFollower::ApplyPointParams.
 */
struct RPPathPoint {
    EGG::Vector3f pos; // at 0x0
    be_s16 param0C;    // at 0xC
    be_s16 param0E;    // at 0xE
    be_s16 param10;    // at 0x10
    be_s16 param12;    // at 0x12
};

/**
 * @brief Path resource header [M]
 * @details Size 0x20 [H] (stride in RPResFile::Relocate).
 */
struct RPPathRes {
    be_u16 numPoints;              // at 0x0
    be_u16 unk02;                  // at 0x2
    u8 type;                       // at 0x4 1: Bezier, otherwise linear
    u8 unk05;                      // at 0x5
    be_u16 flags;                  // at 0x6 bit 0: closed loop
    u8 unk08[0x18 - 0x8];          // at 0x8
    RES_PTR(RPPathPoint) pPoints;  // at 0x18 point index until relocated
    u8 unk1C[0x20 - 0x1C];
};

/**
 * @brief One entry of an RPResFile's data
 * @details Size 0x58 (array stride in RPResFile::CountFilteredEntries). Path objects
 * (RPPathObject) read their placement and parameters from it.
 */
struct RPResEntry {
    be_u32 unk00;            // at 0x0
    u8 type;                 // at 0x4
    u8 unk05[0x44 - 0x5];
    RES_PTR(RPPathRes) pPath; // at 0x44 path index (-1: none) until relocated
    be_s16 params[8];        // at 0x48 object parameters (RPPathObject::GetParam)
};

// Ledger name; defined in RPResFile_3: &pRes->pPoints[idx]
extern "C" const RPPathPoint* RP_getPathPoint(const RPPathRes* pRes, u16 idx);

/**
 * @brief Position along a path resource made of linear or Bezier segments
 * @details No vtable. Size 0x2C [H]. The constructor (0x801C5770, which returns this and is
 * called first by RPPathFollower's constructor) builds an RPLinearSegmentArray or
 * RPBezierSegmentArray from the resource's points; Advance walks mDistance across segments,
 * flipping mReverse at open ends, and evaluates mPos/mTangent.
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPPathWalker {
public:
    RPPathWalker(const void* pPathRes);

    const RPPathPoint* Advance(f32 delta);
    void SetSegment(s32 idx);
    const RPPathPoint* GetPointAhead(s32 idx);
    const RPPathPoint* GetPointBehind(s32 idx);

private:
    // Inlined into the constructor (builds mpSegments from the resource's points)
    inline void InitSegments(const void* pPathRes);

public:

protected:
    const RPPathRes* mpRes;          // at 0x0
    IRPPathSegmentArray* mpSegments; // at 0x4
    s32 mSegment;                    // at 0x8
    f32 mDistance;                   // at 0xC within the current segment
    bool mStopped;                   // at 0x10 [M]
    bool mReverse;                   // at 0x11
    EGG::Vector3f mPos;              // at 0x14
    EGG::Vector3f mTangent;          // at 0x20
};

/**
 * @brief Moves along a path resource made of linear or Bezier segments
 * @details No vtable. Size 0x68, verified (RPPathObject allocates it). Per-point parameters
 * (RPPathPoint) are blended into the floats from 0x2C by ApplyPointParams.
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPPathFollower : public RPPathWalker {
public:
    RPPathFollower(f32 speedPercent, f32 scalePercent, const void* pPathRes);

    void ApplyPointParams();

private:
    f32 unk2C;                      // at 0x2C [L]
    f32 unk30;                      // at 0x30 [L]
    f32 unk34;                      // at 0x34 [L]
    f32 unk38;                      // at 0x38 [L]
    f32 mScale;                     // at 0x3C scalePercent / 100
    f32 unk40;                      // at 0x40 [L]
    f32 unk44;                      // at 0x44 [L]
    f32 unk48;                      // at 0x48 [L]
    f32 unk4C;                      // at 0x4C [L]
    f32 unk50;                      // at 0x50 [L]
    f32 unk54;                      // at 0x54 [L]
    f32 unk58;                      // at 0x58 [L]
    f32 mSpeed;                     // at 0x5C speedPercent / 100
    s32 unk60;                      // at 0x60 [L]
    f32 unk64;                      // at 0x64 [L]
};

/**
 * @brief Placement read from a path/object resource, optionally moving along a path
 * @details No vtable. Size 0x34 [M] (Init writes every member; embedded in Cow objects).
 * Init copies position, rotation (RPY of the resource matrix) and scale and creates an
 * RPPathFollower when the resource has a path (res+0x44).
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPPathObject {
public:
    /**
     * @brief Ledger RP_PathObject_init: zeroes the vectors and reads the resource (the Cow
     * objects allocate it with new, 0x34 bytes) [H]
     */
    RPPathObject(f32 speedPercent, f32 scalePercent, const void* pObjRes);
    void Init(f32 speedPercent, f32 scalePercent, const void* pObjRes);

    const EGG::Vector3f& GetPos() const {
        return mPos;
    }
    const EGG::Vector3f& GetRot() const {
        return mRot;
    }
    const EGG::Vector3f& GetVec20() const {
        return mVec20;
    }
    f32 GetRadius() const {
        return mRadius;
    }
    /**
     * @brief Ledger RP_getResShortOrDefault: s16 parameter idx of the resource entry (at
     * +0x48), def when it is 0; 0 without a resource [H]
     */
    s32 GetParam(u16 idx, s32 def) const;

private:
    const void* mpRes;           // at 0x0
    RPPathFollower* mpFollower;  // at 0x4
    EGG::Vector3f mPos;          // at 0x8
    EGG::Vector3f mRot;          // at 0x14
    EGG::Vector3f mVec20;        // at 0x20 [L] from res+0x20
    f32 mSpeedPercent;           // at 0x2C
    f32 mRadius;                 // at 0x30 [L] scalePercent * res+0x14
};

//! @}

#endif
