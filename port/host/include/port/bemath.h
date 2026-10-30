/**
 * Big-endian nw4r::math value types in file structs: host build.
 *
 * BE_MATH(T) names port::be_math::T, a struct with the layout of
 * nw4r::math::T whose members are be_f32. Reading a member gives the host
 * float; the struct converts to the runtime type (and its _VEC/_MTX base), so
 * `math::VEC3 v = file->pos;` and `*pHitPos = *pFilePos;` compile unchanged.
 * The Wii build's include/port/bemath.h makes BE_MATH(T) the plain type.
 */

#ifndef PORT_BEMATH_H
#define PORT_BEMATH_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <nw4r/math/math_types.h>
#include <port/be.h>

#include <type_traits>

#define PORT_BEMATH_WRAPPED 1

namespace port {
namespace be_math {

struct _VEC2 {
    be_f32 x, y;

    _VEC2& operator=(const nw4r::math::_VEC2& v) {
        x = v.x;
        y = v.y;
        return *this;
    }

    operator nw4r::math::VEC2() const { return nw4r::math::VEC2(x, y); }
    operator nw4r::math::_VEC2() const {
        nw4r::math::_VEC2 v;
        v.x = x;
        v.y = y;
        return v;
    }
};
struct VEC2 : _VEC2 {
    using _VEC2::operator=;
};

struct _VEC3 {
    be_f32 x, y, z;

    _VEC3& operator=(const nw4r::math::_VEC3& v) {
        x = v.x;
        y = v.y;
        z = v.z;
        return *this;
    }

    operator nw4r::math::VEC3() const { return nw4r::math::VEC3(x, y, z); }
    operator nw4r::math::_VEC3() const {
        nw4r::math::_VEC3 v;
        v.x = x;
        v.y = y;
        v.z = z;
        return v;
    }
};
struct VEC3 : _VEC3 {
    using _VEC3::operator=;
};

struct _MTX34 {
    be_f32 m[3][4];

    _MTX34& operator=(const nw4r::math::_MTX34& r) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                m[i][j] = r.m[i][j];
            }
        }
        return *this;
    }

    operator nw4r::math::MTX34() const {
        nw4r::math::MTX34 r;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                r.m[i][j] = m[i][j];
            }
        }
        return r;
    }
    operator nw4r::math::_MTX34() const {
        nw4r::math::_MTX34 r;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                r.m[i][j] = m[i][j];
            }
        }
        return r;
    }
};
struct MTX34 : _MTX34 {
    using _MTX34::operator=;
};

#define PORT_BEMATH_CHECK(T)                                                          \
    static_assert(sizeof(T) == sizeof(nw4r::math::T) && alignof(T) == 4 &&            \
                      std::is_trivially_copyable<T>::value &&                          \
                      std::is_standard_layout<T>::value,                               \
                  "be_math::" #T " must have the layout of nw4r::math::" #T)
PORT_BEMATH_CHECK(_VEC2);
PORT_BEMATH_CHECK(VEC2);
PORT_BEMATH_CHECK(_VEC3);
PORT_BEMATH_CHECK(VEC3);
PORT_BEMATH_CHECK(_MTX34);
PORT_BEMATH_CHECK(MTX34);
#undef PORT_BEMATH_CHECK

} // namespace be_math
} // namespace port

#define BE_MATH(T) port::be_math::T
#define BE_MATH_CAST(T, p) (p)

// The math functions the g3d code applies to matrices in file structs
// (BE_MATH_CAST sites): convert, call the runtime function, store back.
namespace nw4r {
namespace math {

inline MTX34* MTX34Copy(port::be_math::_MTX34* pOut, const MTX34* pIn) {
    *pOut = *pIn;
    return NULL;
}
inline MTX34* MTX34Copy(MTX34* pOut, const port::be_math::_MTX34* pIn) {
    *pOut = *pIn;
    return pOut;
}
inline MTX34* MTX34Copy(port::be_math::_MTX34* pOut, const port::be_math::_MTX34* pIn) {
    *pOut = *pIn;
    return NULL;
}
inline MTX34* MTX34Identity(port::be_math::_MTX34* pMtx) {
    MTX34 m;
    MTX34Identity(&m);
    *pMtx = m;
    return NULL;
}
inline MTX34* MTX34Mult(MTX34* pOut, const port::be_math::_MTX34* pA, const MTX34* pB) {
    MTX34 a = *pA;
    return MTX34Mult(pOut, &a, pB);
}
inline MTX34* MTX34Mult(MTX34* pOut, const MTX34* pA, const port::be_math::_MTX34* pB) {
    MTX34 b = *pB;
    return MTX34Mult(pOut, pA, &b);
}
inline MTX34* MTX34Mult(MTX34* pOut, const port::be_math::_MTX34* pA,
                        const port::be_math::_MTX34* pB) {
    MTX34 a = *pA, b = *pB;
    return MTX34Mult(pOut, &a, &b);
}

} // namespace math
} // namespace nw4r

#endif
