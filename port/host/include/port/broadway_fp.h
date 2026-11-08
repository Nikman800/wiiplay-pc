// SPDX-License-Identifier: GPL-2.0-or-later
//
// Broadway's floating-point estimate instructions, bit for bit (host build only).
//
// Unlike the rest of this repository (CC0), this file is GPL-2.0-or-later: the lookup
// tables and the interpolation are taken from the Dolphin emulator,
// Source/Core/Common/FloatUtils.cpp (ApproximateReciprocal,
// ApproximateReciprocalSquareRoot), and port_force25 is Dolphin's Force25Bit
// (Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h).
//   Copyright 2008 Dolphin Emulator Project; https://dolphin-emu.org,
//   https://github.com/dolphin-emu/dolphin
// Dolphin's developers measured the tables on a console. Translated to C here.
// A program built with this file is therefore under GPL-2.0-or-later as a whole
// (LICENSES/GPL-2.0-or-later.txt; port/docs/HOST_BUILD.md §42).
//
// This program is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version. It is
// distributed WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
// License for more details.

#ifndef PORT_BROADWAY_FP_H
#define PORT_BROADWAY_FP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Broadway's estimate instructions, bit for bit: the lookup tables and the
 * interpolation Dolphin uses (Source/Core/Common/FloatUtils.cpp,
 * ApproximateReciprocal / ApproximateReciprocalSquareRoot; Dolphin measured
 * them on a console). HOST_BUILD.md §42. */
static inline unsigned long long port_dbits(double d) {
    unsigned long long u;
    __builtin_memcpy(&u, &d, 8);
    return u;
}
static inline double port_bitsd(unsigned long long u) {
    double d;
    __builtin_memcpy(&d, &u, 8);
    return d;
}

/* fres (and ps_res): about 12 bits; the result is a single (23 fraction bits). */
static inline float port_fres(float x) {
    static const int base[32] = {
        0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200, 0x579000, 0x520800,
        0x4cc800, 0x47ca00, 0x430800, 0x3e8000, 0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
        0x2aa800, 0x272c00, 0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
        0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000, 0x041c00, 0x020c00};
    static const int dec[32] = {
        0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0, 0x27f, 0x261, 0x245,
        0x22a, 0x212, 0x1fb, 0x1e5, 0x1d1, 0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e,
        0x15b, 0x15b, 0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106};
    unsigned long long u = port_dbits((double)x);
    unsigned long long mantissa = u & ((1ULL << 52) - 1), sign = u & (1ULL << 63);
    long long exponent = (long long)(u & (0x7FFULL << 52));
    if (mantissa == 0 && exponent == 0) {
        return __builtin_copysignf(__builtin_inff(), x);
    }
    if (exponent == (long long)(0x7FFULL << 52)) {
        if (mantissa == 0) {
            return __builtin_copysignf(0.0f, x);
        }
        return x + x; /* quiet NaN */
    }
    if (exponent < (895LL << 52)) {
        return __builtin_copysignf(3.402823466e+38f, x);
    }
    if (exponent >= (1149LL << 52)) {
        return __builtin_copysignf(0.0f, x);
    }
    exponent = (0x7FDLL << 52) - exponent;
    {
        int i = (int)(mantissa >> 37);
        long long m = base[i / 1024] - (dec[i / 1024] * (i % 1024) + 1) / 2;
        return (float)port_bitsd(sign | (unsigned long long)exponent | ((unsigned long long)m << 29));
    }
}

/* frsqrte: about 12 bits, a double with up to 26 fraction bits (not always a
 * single: a single-precision instruction that takes it as its C operand rounds
 * it first, port_force25). Inputs here are singles (no denormal doubles). */
static inline double port_frsqrte(double x) {
    static const int base[32] = {
        0x1a7e800, 0x17cb800, 0x1552800, 0x130c000, 0x10f2000, 0x0eff000, 0x0d2e000, 0x0b7c000,
        0x09e5000, 0x0867000, 0x06ff000, 0x05ab800, 0x046a000, 0x0339800, 0x0218800, 0x0105800,
        0x3ffa000, 0x3c29000, 0x38aa000, 0x3572000, 0x3279000, 0x2fb7000, 0x2d26000, 0x2ac0000,
        0x2881000, 0x2665000, 0x2468000, 0x2287000, 0x20c1000, 0x1f12000, 0x1d79000, 0x1bf4000};
    static const int dec[32] = {
        -0x568, -0x4f3, -0x48d, -0x435, -0x3e7, -0x3a2, -0x365, -0x32e, -0x2fc, -0x2d0, -0x2a8,
        -0x283, -0x261, -0x243, -0x226, -0x20b, -0x7a4, -0x700, -0x670, -0x5f2, -0x584, -0x524,
        -0x4cc, -0x47e, -0x43a, -0x3fa, -0x3c2, -0x38e, -0x35e, -0x332, -0x30a, -0x2e6};
    unsigned long long u = port_dbits(x);
    long long mantissa = (long long)(u & ((1ULL << 52) - 1));
    unsigned long long sign = u & (1ULL << 63);
    long long exponent = (long long)(u & (0x7FFULL << 52));
    if (mantissa == 0 && exponent == 0) {
        return sign ? -__builtin_inf() : __builtin_inf();
    }
    if (exponent == (long long)(0x7FFULL << 52)) {
        if (mantissa == 0) {
            return sign ? __builtin_nan("") : 0.0;
        }
        return x + x;
    }
    if (sign) {
        return __builtin_nan("");
    }
    if (!exponent) {
        do {
            exponent -= 1LL << 52;
            mantissa <<= 1;
        } while (!(mantissa & (1LL << 52)));
        mantissa &= (1LL << 52) - 1;
        exponent += 1LL << 52;
    }
    {
        long long exponent_lsb = exponent & (1LL << 52);
        int i;
        exponent = ((0x3FFLL << 52) - ((exponent - (0x3FELL << 52)) / 2)) & (0x7FFLL << 52);
        i = (int)((exponent_lsb | mantissa) >> 37);
        return port_bitsd(sign | (unsigned long long)exponent |
                          ((unsigned long long)(base[i / 2048] + dec[i / 2048] * (i % 2048)) << 26));
    }
}

/* The C operand of a single-precision multiply (fmuls, fmadds, ps_mul, ps_muls0,
 * ...) is rounded to 25 fraction bits first (Dolphin's Force25Bit). A no-op on a
 * single; it matters only for a double operand, here frsqrte's estimate and
 * PSQUATNormalize's double-precision scale. Normal values only. */
static inline double port_force25(double c) {
    unsigned long long u = port_dbits(c);
    return port_bitsd((u & 0xFFFFFFFFF8000000ULL) + (u & 0x8000000ULL));
}
#ifdef __cplusplus
}
#endif

#endif
