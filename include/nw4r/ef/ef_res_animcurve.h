#ifndef NW4R_EF_RES_ANIM_CURVE_H
#define NW4R_EF_RES_ANIM_CURVE_H
#include <nw4r/types_nw4r.h>

#include <nw4r/math.h>

#include <port/be.h>
#include <port/bemath.h>

#define NW4R_EF_MAGIC_ANIMCURVE 0xAC
#define NW4R_EF_MAGIC_ANIMBAKED 0xAB

namespace nw4r {
namespace ef {

enum AnimCurveType {
    AC_TYPE_PARTICLE_U8 = 0,
    AC_TYPE_PARTICLE_F32 = 3,
    AC_TYPE_PARTICLE_ROTATE = 6,
    AC_TYPE_PARTICLE_TEXTURE = 4,
    AC_TYPE_CHILD = 5
};

enum AnimCurveTarget {
    // AC_TYPE_PARTICLE_U8
    AC_TARGET_COLOR0PRI,
    AC_TARGET_ALPHA0PRI,
    AC_TARGET_COLOR0SEC,
    AC_TARGET_ALPHA0SEC,
    AC_TARGET_COLOR1PRI,
    AC_TARGET_ALPHA1PRI,
    AC_TARGET_COLOR1SEC,
    AC_TARGET_ALPHA1SEC,
    AC_TARGET_ACMPREF0,
    AC_TARGET_ACMPREF1,

    // AC_TYPE_PARTICLE_F32
    AC_TARGET_SIZE,
    AC_TARGET_SCALE,
    AC_TARGET_12,
    AC_TARGET_TEXTURE1SCALE = 17,
    AC_TARGET_TEXTURE1ROTATE,
    AC_TARGET_TEXTURE1TRANSLATE,
    AC_TARGET_TEXTURE2SCALE,
    AC_TARGET_TEXTURE2ROTATE,
    AC_TARGET_TEXTURE2TRANSLATE,
    AC_TARGET_TEXTUREINDSCALE,
    AC_TARGET_TEXTUREINDROTATE,
    AC_TARGET_TEXTUREINDTRANSLATE,

    AC_TARGET_UNK32 = 32,
    AC_TARGET_FIELD_SPEED,
    AC_TARGET_UNK34,
    AC_TARGET_UNK35,
    AC_TARGET_UNK36,
    AC_TARGET_UNK38 = 38, // FieldSpin?
    AC_TARGET_UNK39,
    AC_TARGET_EMIT_COMMONPARAM = 64,
    AC_TARGET_EMIT_SCALE,
    AC_TARGET_EMIT_ROTATE,
    AC_TARGET_EMIT_TRANSLATE,
    AC_TARGET_EMIT_SPEED_ORIG,
    AC_TARGET_EMIT_SPEED_YAXIS,
    AC_TARGET_EMIT_SPEED_RANDOM,
    AC_TARGET_EMIT_SPEED_NORMAL,
    AC_TARGET_EMIT_SPEED_SPECDIR,
    AC_TARGET_EMIT_EMISSION,

    // AC_TYPE_PARTICLE_ROTATE
    AC_TARGET_ROTATE = 12,

    // Texture target
    // AC_TARGET_TEXTURE1 = 23,
    // AC_TARGET_TEXTURE2 = 24,
    // AC_TARGET_TEXTUREIND = 25,

    // Child target
    // AC_TARGET_CHILD = 26,

    // Field target
    // AC_TARGET_FIELD_GRAVITY = 27,
    // AC_TARGET_FIELD_SPEED = 28,
    // AC_TARGET_FIELD_MAGNET = 29,
    // AC_TARGET_FIELD_NEWTON = 30,
    // AC_TARGET_FIELD_VORTEX = 31,
    // AC_TARGET_FIELD_SPIN = 32,
    // AC_TARGET_FIELD_RANDOM = 33,
    // AC_TARGET_FIELD_TAIL = 34,

    // Postfield target
    // AC_TARGET_POSTFIELD_SIZE = 35,
    // AC_TARGET_POSTFIELD_ROTATE = 36,
    // AC_TARGET_POSTFIELD_TRANSLATE = 37,
};

struct AnimCurveHeader {
    enum ProcessFlag {
        PROC_FLAG_SYNC = (1 << 2),
        PROC_FLAG_STOP = (1 << 3),
        PROC_FLAG_TIMING = (1 << 4),
        PROC_FLAG_INFLOOP = (1 << 5),
        PROC_FLAG_TURN = (1 << 6),
        PROC_FLAG_FITTING = (1 << 7),
    };

    u8 magic;        // at 0x0
    u8 kindType;     // at 0x1
    u8 curveFlag;    // at 0x2
    u8 kindEnable;   // at 0x3
    u8 processFlag;  // at 0x4
    u8 loopCount;    // at 0x5
    be_u16 randomSeed;  // at 0x6
    be_u16 frameLength; // at 0x8
    be_u16 PADDING_0xA; // at 0xA
    be_u32 keyTable;    // at 0xC
    be_u32 rangeTable;  // at 0x10
    be_u32 randomTable; // at 0x14
    be_u32 nameTable;   // at 0x18
    be_u32 infoTable;   // at 0x1C
};

struct AnimCurveKey {
    be_u16 count;       // at 0x0
    be_u16 PADDING_0x2; // at 0x2
    u8 datas[1];     // at 0x4
};

struct AnimCurveRandom {
    be_u16 count;       // at 0x0
    be_u16 PADDING_0x2; // at 0x2
    u8 datas[1];     // at 0x4
};

struct AnimCurveRandomTable {
    be_u16 count;       // at 0x0
    be_u16 PADDING_0x2; // at 0x2
    u8 datas[1];     // at 0x4
};

struct AnimCurveNameTable {
    struct Name {
        be_u32 work; // at 0x0
    };

    be_u16 count;       // at 0x0
    be_u16 PADDING_0x2; // at 0x2
    Name datas[1];   // at 0x4
};

/******************************************************************************
 *
 * Field parameter blocks
 *
 * A particle track whose kind is a field (gravity, random, spin, magnet,
 * newton, vortex) carries one of these in its info table, right after the
 * name table. ParticleManager::Calc finds it at
 * header + sizeof(AnimCurveHeader) + keyTable + rangeTable + randomTable +
 * nameTable (src/nw4r/ef/ef_particlemanager.cpp).
 *
 ******************************************************************************/
struct FieldGravityParam {
    be_f32 power;          // at 0x0
    BE_MATH(VEC3) dir;     // at 0x4
    u8 space;              // at 0x10
    u8 type;               // at 0x11
};

struct FieldRandomParam {
    be_f32 power;    // at 0x0
    be_f32 diffuse;  // at 0x4
    be_u16 interval; // at 0x8
    u8 flags;        // at 0xA
    u8 type;         // at 0xB
};

struct FieldSpinParam {
    be_f32 speed;         // at 0x0
    BE_MATH(VEC3) rot;    // at 0x4
    u8 space;             // at 0x10
    u8 type;              // at 0x11
    u8 reserved0;         // at 0x12
    u8 reserved1;         // at 0x13
};

struct FieldMagnetParam {
    be_f32 power;         // at 0x0
    BE_MATH(VEC3) pos;    // at 0x4
    u8 space;             // at 0x10
    u8 type;              // at 0x11
    u8 reserved0;         // at 0x12
    u8 reserved1;         // at 0x13
};

struct FieldNewtonParam {
    be_f32 power;         // at 0x0
    be_f32 range;         // at 0x4
    BE_MATH(VEC3) pos;    // at 0x8
    u8 space;             // at 0x14
    u8 type;              // at 0x15
    u8 reserved0;         // at 0x16
    u8 reserved1;         // at 0x17
};

struct FieldVortexParam {
    be_f32 innerSpeed;    // at 0x0
    be_f32 outerSpeed;    // at 0x4
    be_f32 radius;        // at 0x8
    BE_MATH(VEC3) rot;    // at 0xC
    u8 space;             // at 0x18
    u8 type;              // at 0x19
    u8 reserved0;         // at 0x1A
    u8 reserved1;         // at 0x1B
};

#if PORT_BE_WRAPPED
// ParticleManager::Calc copies the spin, magnet, newton and vortex blocks into
// a local and lets the curve overwrite one member as a host float
// (AnimCurveExecuteF32(..., &param.speed, ...)). On the host the local is this
// host-order copy; on the Wii it is the block's own type.
struct FieldSpinParamValue {
    f32 speed;
    math::VEC3 rot;
    u8 space, type, reserved0, reserved1;
    FieldSpinParamValue(const FieldSpinParam& r)
        : speed(r.speed), rot(r.rot), space(r.space), type(r.type),
          reserved0(r.reserved0), reserved1(r.reserved1) {}
};
struct FieldMagnetParamValue {
    f32 power;
    math::VEC3 pos;
    u8 space, type, reserved0, reserved1;
    FieldMagnetParamValue(const FieldMagnetParam& r)
        : power(r.power), pos(r.pos), space(r.space), type(r.type),
          reserved0(r.reserved0), reserved1(r.reserved1) {}
};
struct FieldNewtonParamValue {
    f32 power, range;
    math::VEC3 pos;
    u8 space, type, reserved0, reserved1;
    FieldNewtonParamValue(const FieldNewtonParam& r)
        : power(r.power), range(r.range), pos(r.pos), space(r.space), type(r.type),
          reserved0(r.reserved0), reserved1(r.reserved1) {}
};
struct FieldVortexParamValue {
    f32 innerSpeed, outerSpeed, radius;
    math::VEC3 rot;
    u8 space, type, reserved0, reserved1;
    FieldVortexParamValue(const FieldVortexParam& r)
        : innerSpeed(r.innerSpeed), outerSpeed(r.outerSpeed), radius(r.radius),
          rot(r.rot), space(r.space), type(r.type), reserved0(r.reserved0),
          reserved1(r.reserved1) {}
};
#define NW4R_EF_FIELD_VALUE(T) T##Value
#else
#define NW4R_EF_FIELD_VALUE(T) T
#endif

class ResAnimCurve {
private:
    u8* mAnimCurveData; // at 0x0

public:
    explicit ResAnimCurve(u8* pData = NULL) : mAnimCurveData(pData) {}
    ResAnimCurve(const ResAnimCurve& rOther)
        : mAnimCurveData(rOther.mAnimCurveData) {}

    bool IsValid() const {
        return mAnimCurveData != NULL;
    }

    AnimCurveHeader* ptr() {
        return reinterpret_cast<AnimCurveHeader*>(mAnimCurveData);
    }

    void SetStop(bool stop);
};

} // namespace ef
} // namespace nw4r

#endif
