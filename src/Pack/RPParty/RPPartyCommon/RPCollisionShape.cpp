#include <Pack/RPParty/RPPartyCommon/RPCollisionShape.h>
#include <egg/gfxe/eggDrawGX.h>
#include <math.h>

// The address of a temporary (CodeWarrior accepts `&T(...)`; the host form is in
// port/host/include/decomp/compat.h)
#if !defined(PORT_TEMP_PTR)
#define PORT_TEMP_PTR(x) (&(x))
#endif

extern "C" void RP_BBox_reset2(RPBBox*);
extern "C" EGG::Vector3f calcRPY__Q23EGG9Matrix34fCFv(const EGG::Matrix34f*);
extern "C" EGG::Vector3f cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f*, const EGG::Vector3f&);
extern "C" EGG::Vector3f __mi__Q23EGG8Vector3fCFv(const EGG::Vector3f*);
extern "C" EGG::Vector3f __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f*, const EGG::Vector3f&);
extern "C" EGG::Vector3f __ml__Q23EGG8Vector3fCFf(const EGG::Vector3f*, f32);

extern const f32 RP_epsilon;
const f32 RP_epsilon = 0.01f;
extern const EGG::Vector3f RP_eggVecZero, RP_eggVecOne, RP_eggVecEx, RP_eggVecEy, RP_eggVecEz, RP_eggVecMin, RP_eggVecMax;
extern EGG::Matrix34f RP_eggMtxZero, RP_eggMtxIdentity;

// Pointer-to-member access keeps the shared header's declarations and layout intact.
struct RPCollisionShapeAccess : RPCollisionShape {
    static s32 Type(const RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mType); }
    static u32 Mode(const RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::unk14); }
    static EGG::Vector3f& Pos(RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mPos); }
    static EGG::Vector3f& Up(RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mUp); }
    static EGG::Vector3f& Forward(RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mForward); }
    static const EGG::Vector3f& Angles(const RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mVec40); }
    static f32 Radius(const RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mRadius); }
    static RPBBox& PosOfs(RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mPosOfs); }
    static RPBBox& RotOfs(RPCollisionShape* p) { return p->*(&RPCollisionShapeAccess::mRotOfs); }
    static void CopyUp(EGG::Matrix34f* out, RPCollisionShape* p) {
        out->_01 = Up(p).x;
        out->_11 = Up(p).y;
        out->_21 = Up(p).z;
    }
    static void CopyForward(EGG::Matrix34f* out, RPCollisionShape* p) {
        out->_02 = Forward(p).x;
        out->_12 = Forward(p).y;
        out->_22 = Forward(p).z;
    }
    static void SetUp(RPCollisionShape* p, const EGG::Vector3f& v) {
        Up(p).x = v.x;
        Up(p).y = v.y;
        Up(p).z = v.z;
    }
    static void SetForward(RPCollisionShape* p, const EGG::Vector3f& v) {
        Forward(p).x = v.x;
        Forward(p).y = v.y;
        Forward(p).z = v.z;
    }
};

inline f32 ShapeAbs(f32 x) { return x > 0.0f ? x : -x; }
inline f32 ShapeMin(f32 x, f32 y) { return x < y ? x : y; }
inline f32 ShapeMax(f32 x, f32 y) { return x > y ? x : y; }
inline bool ShapeNan(f32 x) { return isnan(x); }
inline bool ShapeNanXY(const EGG::Vector3f& v) { return ShapeNan(v.x) || ShapeNan(v.y); }
inline bool ShapeNanVec(const EGG::Vector3f& v) { return ShapeNanXY(v) || ShapeNan(v.z); }
inline f32 ShapeIdentity(f32 x) { return x; }
extern "C" nw4r::math::MTX34 s_cameraMtx__Q23EGG6DrawGX;
inline void ShapeDrawWorld(EGG::DrawGX::DL dl, const nw4r::math::MTX34& m, const GXColor& color) {
    nw4r::math::MTX34 view;
    nw4r::math::MTX34Mult(&view, &s_cameraMtx__Q23EGG6DrawGX, &m);
    EGG::DrawGX::DrawDL(dl, view, color);
}
static const GXColor sHitColor = {255, 0, 0, 128};
static const GXColor sFreeColor = {255, 192, 192, 128};
static const GXColor sArrowColor = {255, 0, 0, 128};
extern const f32 RP_CollisionShape_arrowLength;

const EGG::Vector3f RP_eggVecZero(0.0f, 0.0f, 0.0f);
const EGG::Vector3f RP_eggVecOne(1.0f, 1.0f, 1.0f);
const EGG::Vector3f RP_eggVecEx(1.0f, 0.0f, 0.0f);
const EGG::Vector3f RP_eggVecEy(0.0f, 1.0f, 0.0f);
const EGG::Vector3f RP_eggVecEz(0.0f, 0.0f, 1.0f);
const EGG::Vector3f RP_eggVecMin(-3.402823466e38f, -3.402823466e38f, -3.402823466e38f);
const EGG::Vector3f RP_eggVecMax(3.402823466e38f, 3.402823466e38f, 3.402823466e38f);
EGG::Matrix34f RP_eggMtxZero(0.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 0.0f);
EGG::Matrix34f RP_eggMtxIdentity(1.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 1.0f, 0.0f);

f32 RPCollisionShape::GetRadius() const { return mRadius; }
EGG::Vector3f& RPCollisionShape::GetRot() { return mRot; }
EGG::Vector3f& RPCollisionShape::GetPos() { return mPos; }

extern "C" EGG::Vector3f RP_Mtx34_getColumn(const EGG::Matrix34f&, int);

RPCollisionShape::RPCollisionShape() {
    RP_BBox_reset2(&mPosOfs);
    RP_BBox_reset2(&mRotOfs);
    mPos = mPrevPos = RP_Mtx34_getColumn(RP_eggMtxIdentity, 3);
    mRot = RP_eggVecZero;
    EGG::Vector3f angles = calcRPY__Q23EGG9Matrix34fCFv(&RP_eggMtxIdentity);
    mVec40 = angles;
    mVec4C = RP_eggVecZero;
    unk14 = 0;
    mType = 1;
    mRadius = 0.0f;
    unk74 = 0.0f;
    mPosOfs.Reset();
    mRotOfs.Reset();
}

void RPCollisionShape::Reset() {
    mPos = mPrevPos = RP_Mtx34_getColumn(RP_eggMtxIdentity, 3);
    mRot = RP_eggVecZero;
    EGG::Vector3f angles = calcRPY__Q23EGG9Matrix34fCFv(&RP_eggMtxIdentity);
    mVec40 = angles;
    mVec4C = RP_eggVecZero;
    unk14 = 0;
    mType = 1;
    mRadius = 0.0f;
    unk74 = 0.0f;
    mPosOfs.Reset();
    mRotOfs.Reset();
}

extern "C" EGG::Vector3f RP_Mtx34_getColumn(const EGG::Matrix34f& m, int col) {
    f32 x = m(0, col);
    f32 y = m(1, col);
    f32 z = m(2, col);
    return EGG::Vector3f(x, y, z);
}

// The original reloads mUp/mForward after each vector operation instead of keeping a common
// pointer to them (per-function pragma, LEVERS section 10).
#pragma push
#pragma opt_common_subs off
extern "C" void RP_CollisionShape_getMtx(EGG::Matrix34f* out, RPCollisionShape* p) {
    EGG::Vector3f right;
    switch (RPCollisionShapeAccess::Mode(p)) {
    case 0:
        out->makeRT(RPCollisionShapeAccess::Angles(p), RPCollisionShapeAccess::Pos(p));
        break;
    case 1:
        out->setBase(0, cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&RPCollisionShapeAccess::Up(p), RPCollisionShapeAccess::Forward(p)));
        RPCollisionShapeAccess::CopyUp(out, p);
        RPCollisionShapeAccess::CopyForward(out, p);
        out->setBase(3, RPCollisionShapeAccess::Pos(p));
        break;
    case 2:
        right = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&RPCollisionShapeAccess::Up(p), RPCollisionShapeAccess::Forward(p));
        right.normalise();
        RPCollisionShapeAccess::SetForward(p, cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&right, RPCollisionShapeAccess::Up(p)));
        out->setBase(0, right);
        RPCollisionShapeAccess::CopyUp(out, p);
        RPCollisionShapeAccess::CopyForward(out, p);
        out->setBase(3, RPCollisionShapeAccess::Pos(p));
        break;
    case 3:
        right = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&RPCollisionShapeAccess::Up(p), RPCollisionShapeAccess::Forward(p));
        right.normalise();
        RPCollisionShapeAccess::SetUp(p, cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&RPCollisionShapeAccess::Forward(p), right));
        out->setBase(0, right);
        RPCollisionShapeAccess::CopyUp(out, p);
        RPCollisionShapeAccess::CopyForward(out, p);
        out->setBase(3, RPCollisionShapeAccess::Pos(p));
        break;
    }
}
#pragma pop

void RPCollisionShape::ApplyOffsets() {
    mPos += mPosOfs.GetSum();
    mRot += mRotOfs.GetSum();
    mPrevPos = mPos;
    mPosOfs.Reset();
    mRotOfs.Reset();
}

extern "C" void RP_CollisionShape_accumPosOffset(RPCollisionShape* p, const EGG::Vector3f& v) {
    RPCollisionShapeAccess::PosOfs(p).Extend(v);
}
extern "C" void RP_CollisionShape_accumRotOffset(RPCollisionShape* p, const EGG::Vector3f& v) {
    RPCollisionShapeAccess::RotOfs(p).Extend(v);
}
extern "C" void RP_dampAndApply(f32* pos, f32* vel, f32 damp) {
    *vel *= damp;
    *pos += *vel;
}
extern "C" void RP_dampVec3AndApply(EGG::Vector3f* pos, EGG::Vector3f* vel, f32 damp) {
    *vel *= damp;
    *pos += *vel;
}

extern "C" void RP_accelerateToward(f32* pos, f32* vel, f32 target, f32 limit, f32 accel) {
    if (target > *pos) {
        if (target > *pos + (*vel * *vel / (2.0f * accel)) * (*vel < 0.0f ? -1 : 1)) {
            *vel = ShapeMin(*vel + accel, limit);
        } else {
            *vel = *pos + *vel - accel < target ? *vel - accel : target - *pos;
        }
    } else if (target < *pos) {
        if (target < *pos + (*vel * *vel / (2.0f * accel)) * (*vel < 0.0f ? -1 : 1)) {
            *vel = ShapeMax(*vel - accel, -limit);
        } else {
            *vel = *pos + *vel + accel > target ? *vel + accel : target - *pos;
        }
    } else {
        *vel = 0.0f;
    }
    *pos += *vel;
}
extern "C" void RP_approachStep(f32* pos, f32* vel, f32 target, f32 rate) {
    *vel = (target - *pos) * rate;
    *pos += *vel;
}
extern "C" void RP_springStep(f32* pos, f32* vel, f32 target, f32 rate) {
    *vel += ((target - *pos) - *vel) * rate;
    *pos += *vel;
}
extern "C" void RP_wrapAngleNear(f32* angle, f32 target) {
    while (target - *angle > 3.1415927f) *angle += 6.2831855f;
    while (target - *angle < -3.1415927f) *angle -= 6.2831855f;
}
extern "C" bool RP_isWithinRange(f32 a, f32 b, f32 range) {
    return -range <= a - b && a - b <= range;
}
extern "C" bool RP_isVec3DiffWithinAbs(const EGG::Vector3f* a, const EGG::Vector3f* b, f32 range) {
    f32 lo = -range;
    return lo <= a->x - b->x && a->x - b->x <= range &&
           lo <= a->y - b->y && a->y - b->y <= range &&
           lo <= a->z - b->z && a->z - b->z <= range;
}
extern "C" bool RP_isVec3WithinAbs(const EGG::Vector3f* a, f32 range) {
    f32 lo = -range;
    return lo <= a->x && a->x <= range && lo <= a->y && a->y <= range && lo <= a->z && a->z <= range;
}
inline f32 ShapeClamp(f32 value, f32 range) {
    f32 result;
    if (value > range) result = range;
    else if (value < -range) result = -range;
    else result = value;
    return result;
}
extern "C" void RP_clampVec3Abs(EGG::Vector3f* a, f32 range) {
    a->x = ShapeClamp(a->x, range);
    a->y = ShapeClamp(a->y, range);
    a->z = ShapeClamp(a->z, range);
}

extern "C" bool RP_CollisionShape_checkSphereVsPoint(RPCollisionShape*, const EGG::Vector3f& delta,
        const EGG::Vector3f&, EGG::Vector3f* posOfs, EGG::Vector3f* rotOfs, u16* flags, f32 radius) {
    f32 dist = delta.squaredLength();
    if (dist < radius * radius) {
        if (posOfs != NULL) {
            *posOfs = delta;
            if (dist < 0.01f || posOfs->normalise() == ShapeIdentity(0.0f)) *posOfs = RP_eggVecEx;
            if (rotOfs != NULL) *rotOfs = *posOfs;
            *posOfs *= radius - EGG::Math<f32>::sqrt(dist);
        } else if (rotOfs != NULL) {
            *rotOfs = delta;
            if (dist < 0.01f || rotOfs->normalise() == ShapeIdentity(0.0f)) *rotOfs = RP_eggVecEx;
        }
        if (flags != NULL) *flags = 0;
        return true;
    }
    return false;
}

extern "C" bool RP_CollisionShape_checkSphereVsBox(RPCollisionShape* p, const EGG::Vector3f& delta,
        const EGG::Vector3f& prev, EGG::Vector3f* posOfs, EGG::Vector3f* rotOfs, u16* flags, f32 radius) {
    f32 x = ShapeAbs(delta.x);
    f32 y = ShapeAbs(delta.y);
    f32 z = ShapeAbs(delta.z);
    if (x < radius && y < radius && z < radius) {
        int count = x > RPCollisionShapeAccess::Radius(p) ? 1 : 0;
        if (y > RPCollisionShapeAccess::Radius(p)) count++;
        if (z > RPCollisionShapeAccess::Radius(p)) count++;
        if (count >= 2) {
            EGG::Vector3f v;
            v.x = delta.x > 0.0f ? delta.x - RPCollisionShapeAccess::Radius(p) : delta.x + RPCollisionShapeAccess::Radius(p);
            v.y = delta.y > 0.0f ? delta.y - RPCollisionShapeAccess::Radius(p) : delta.y + RPCollisionShapeAccess::Radius(p);
            v.z = delta.z > 0.0f ? delta.z - RPCollisionShapeAccess::Radius(p) : delta.z + RPCollisionShapeAccess::Radius(p);
            if (count >= 3) return RP_CollisionShape_checkSphereVsPoint(p, v, prev, posOfs, rotOfs, flags, radius - RPCollisionShapeAccess::Radius(p));
            if (x <= y && x <= z) {
                v.x = RPCollisionShapeAccess::Pos(p).x;
                return RP_CollisionShape_checkSphereVsPoint(p, v, prev, posOfs, rotOfs, flags, radius - RPCollisionShapeAccess::Radius(p));
            } else if (y <= z) {
                v.y = RPCollisionShapeAccess::Pos(p).y;
                return RP_CollisionShape_checkSphereVsPoint(p, v, prev, posOfs, rotOfs, flags, radius - RPCollisionShapeAccess::Radius(p));
            } else {
                v.z = RPCollisionShapeAccess::Pos(p).z;
                return RP_CollisionShape_checkSphereVsPoint(p, v, prev, posOfs, rotOfs, flags, radius - RPCollisionShapeAccess::Radius(p));
            }
        }
        if (x >= y && x >= z) {
            if (posOfs != NULL) {
                *posOfs = delta.x > 0.0f ? RP_eggVecEx : __mi__Q23EGG8Vector3fCFv(&RP_eggVecEx);
                *posOfs *= radius - x;
            }
            if (rotOfs != NULL) *rotOfs = delta.x > 0.0f ? RP_eggVecEx : __mi__Q23EGG8Vector3fCFv(&RP_eggVecEx);
        } else if (y >= z) {
            if (posOfs != NULL) {
                *posOfs = delta.y > 0.0f ? RP_eggVecEy : __mi__Q23EGG8Vector3fCFv(&RP_eggVecEy);
                *posOfs *= radius - y;
            }
            if (rotOfs != NULL) *rotOfs = delta.y > 0.0f ? RP_eggVecEy : __mi__Q23EGG8Vector3fCFv(&RP_eggVecEy);
        } else {
            if (posOfs != NULL) {
                *posOfs = delta.z > 0.0f ? RP_eggVecEz : __mi__Q23EGG8Vector3fCFv(&RP_eggVecEz);
                *posOfs *= radius - z;
            }
            if (rotOfs != NULL) *rotOfs = delta.z > 0.0f ? RP_eggVecEz : __mi__Q23EGG8Vector3fCFv(&RP_eggVecEz);
        }
        if (flags != NULL) *flags = 0;
        return true;
    }
    return false;
}

bool RPCollisionShape::CheckHit(f32 radius, const EGG::Vector3f& pos, const EGG::Vector3f& prev,
        EGG::Vector3f* posOfs, EGG::Vector3f* rotOfs, u16* flags) {
    f32 x = ShapeAbs(pos.x - mPos.x);
    f32 y = ShapeAbs(pos.y - mPos.y);
    f32 z = ShapeAbs(pos.z - mPos.z);
    if (x < radius + mRadius && y < radius + mRadius && z < radius + mRadius) {
        switch (mType) {
        case 0: return false;
        case 1: return RP_CollisionShape_checkSphereVsPoint(this, __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&pos, mPos), prev, posOfs, rotOfs, flags, radius + mRadius);
        case 2: return RP_CollisionShape_checkSphereVsBox(this, __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&pos, mPos), prev, posOfs, rotOfs, flags, radius + mRadius);
        }
    }
    return false;
}

void RPCollisionShape::Draw() {
    if (mRadius > 0.0f) {
        EGG::Matrix34f mtx;
        switch (mType) {
        case 1:
            mtx.makeST(EGG::Vector3f(mRadius, mRadius, mRadius), mPos);
            EGG::DrawGX::BeginDrawSphere();
            if (mpOwner != NULL) ShapeDrawWorld(EGG::DrawGX::DL_SPHERE_4X8, mtx, sHitColor);
            else ShapeDrawWorld(EGG::DrawGX::DL_SPHERE_4X8, mtx, sFreeColor);
            break;
        case 2:
            mtx.makeST(EGG::Vector3f(2.0f * mRadius, 2.0f * mRadius, 2.0f * mRadius), mPos);
            EGG::DrawGX::BeginDrawCube();
            if (mpOwner != NULL) ShapeDrawWorld(EGG::DrawGX::DL_CUBE, mtx, sHitColor);
            else ShapeDrawWorld(EGG::DrawGX::DL_CUBE, mtx, sFreeColor);
            break;
        }
        EGG::Vector3f dir = mRot;
        if (!ShapeNanVec(dir) && dir.normalise() != ShapeIdentity(0.0f)) {
            EGG::Matrix34f arrow;
            arrow.setBase(0, __ml__Q23EGG8Vector3fCFf(PORT_TEMP_PTR(EGG::Vector3f(dir.y, -dir.x, dir.z)), 2.5f));
            arrow.setBase(1, __ml__Q23EGG8Vector3fCFf(&mRot, RP_CollisionShape_arrowLength));
            arrow.setBase(2, __ml__Q23EGG8Vector3fCFf(PORT_TEMP_PTR(EGG::Vector3f(-dir.z, -dir.x, dir.y)), 2.5f));
            arrow.setBase(3, mPos);
            EGG::DrawGX::BeginDrawCornY();
            GXColor color = sArrowColor;
            EGG::DrawGX::DrawCornY(EGG::DrawGX::CIRCLE_16, arrow, color);
        }
    }
}

// Defined after Draw so the value is loaded from the object rather than folded.
const f32 RP_CollisionShape_arrowLength = 20.0f;
