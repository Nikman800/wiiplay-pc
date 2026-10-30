/**
 * Big-endian nw4r::math value types in file structs: Wii (matching) build.
 *
 * File structs that hold vectors or matrices declare those members as
 * BE_MATH(VEC3), BE_MATH(_MTX34) and so on. On the Wii this is the plain
 * nw4r::math type, so the code compiles to exactly what it did before. The host
 * build finds port/host/include/port/bemath.h first, where they are structs of
 * be_f32 that convert to the runtime type (port/docs/HOST_BUILD.md).
 */

#ifndef PORT_BEMATH_H
#define PORT_BEMATH_H

#if defined(PORT_HOST)
#error "host builds must find port/host/include/port/bemath.h first"
#endif

#define PORT_BEMATH_WRAPPED 0

#define BE_MATH(T) nw4r::math::T

// A pointer to a BE_MATH member passed to the math functions, which take the
// runtime type: the original upcast. On the host the pointer stays the
// big-endian type and port/host/include/port/bemath.h overloads the functions.
#define BE_MATH_CAST(T, p) static_cast<T>(p)

#endif
