#!/usr/bin/env python3
"""Compare tests/anm_harness (the real nw4r::g3d animation readers on the host) with an
independent f32 evaluation of every animation on the disc (port/docs/HOST_BUILD.md §17).

The animation blocks and their records are located with port/assets/tools/fmt_brres_anim.py
(read-only). The values are evaluated here from the file bytes with the formulas of the
decompiled readers (src/nw4r/g3d/res/g3d_resanm*.cpp, g3d_resanm.h), every operation rounded to
f32 as the host build computes it (SSE, no fused multiply-add):

* keyframe tracks (ResKeyFrameAnmData): estimate-then-walk search and Hermite interpolation
  with FInv as Broadway computes it (fres's table, one Newton-Raphson step; §42);
* CHR0 frame values FVS32 / FVS48 / FVS96 (quantised frames, 12-bit and 16-bit values, S10.5 /
  S7.8 slopes) and CV8 / CV16 / CV32 tracks; the rotation matrix of MTX34RotXYZDeg with
  SinCosFIdx's table (read from src/nw4r/math/math_triangular.cpp);
* colour tracks (fixed-point lerp of RGBA bytes), the light enable bit tracks, PAT0 frame
  search, SRT0 matrices, SCN0 lights, ambient lights and cameras.

The fast-cast stores (psq_st: F32ToU16, F32ToU8_0, F32ToS10_5) clamp and truncate.
Every result must match bit for bit. On the Wii g3d_resanmchr.cpp is built with fp_contract
(fmadds), so the console's floats can differ from these in the last bit or so; the host build
does not try to reproduce that (§42).

    test_anims.py --harness build/host64/anm_harness --disc ~/wii-play/extracted/files
"""
import argparse
import collections
import math
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "port", "assets", "tools"))
import fmt_brres_anim as A  # noqa: E402  (read-only reference)
from wiiarc import walk  # noqa: E402


# --- f32 arithmetic ---------------------------------------------------------------------------

def f32(x):
    if x != x or math.isinf(x):
        return x
    try:
        return struct.unpack("<f", struct.pack("<f", x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def from_bits(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def add(a, b):
    return f32(a + b)


def sub(a, b):
    return f32(a - b)


def mul(a, b):
    return f32(a * b)


def div(a, b):
    if b == 0:
        if a == 0 or a != a:
            return math.nan
        return math.copysign(math.inf, a) * (1 if math.copysign(1, b) > 0 else -1)
    return f32(a / b)


def fmaf(a, b, c):
    """Single-precision fused multiply-add, rounded once (port_fmaf): a*b is exact in double;
    the sum is rounded to odd in double, which then rounds correctly to single."""
    p = a * b
    s = p + c
    if math.isinf(s) or math.isnan(s) or p == 0 or c == 0:
        return f32(s)
    bb = s - p
    err = (p - (s - bb)) + (c - bb)
    if err != 0.0:
        m = struct.unpack("<Q", struct.pack("<d", s))[0]
        if m & 1 == 0:
            s = math.nextafter(s, math.inf if err > 0 else -math.inf)  # toward the exact sum
    return f32(s)


FRES_BASE = [0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200, 0x579000, 0x520800,
             0x4cc800, 0x47ca00, 0x430800, 0x3e8000, 0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
             0x2aa800, 0x272c00, 0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
             0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000, 0x041c00, 0x020c00]
FRES_DEC = [0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0, 0x27f, 0x261, 0x245, 0x22a,
            0x212, 0x1fb, 0x1e5, 0x1d1, 0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e, 0x15b, 0x15b,
            0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106]


def fres(x):
    """Broadway's fres (Dolphin's table; port/ps.h port_fres)."""
    u = struct.unpack("<Q", struct.pack("<d", x))[0]
    mant, sign, exp = u & ((1 << 52) - 1), u & (1 << 63), u & (0x7FF << 52)
    if mant == 0 and exp == 0:
        return math.copysign(math.inf, x)
    if exp == 0x7FF << 52:
        return math.copysign(0.0, x) if mant == 0 else math.nan
    if exp < 895 << 52:
        return math.copysign(3.4028234663852886e38, x)
    if exp >= 1149 << 52:
        return math.copysign(0.0, x)
    exp = (0x7FD << 52) - exp
    i = mant >> 37
    m = FRES_BASE[i // 1024] - (FRES_DEC[i // 1024] * (i % 1024) + 1) // 2
    return f32(struct.unpack("<d", struct.pack("<Q", sign | exp | (m << 29)))[0])


def finv(x):
    """math::FInv: fres refined by one Newton-Raphson step (compat.h, port_fres_nr):
    e = fres(x); -(x * (e * e) - (e + e)), fused."""
    e = fres(x)
    return -fmaf(x, mul(e, e), -add(e, e))


def quantize(v, scale, lo, hi):
    """psq_st to an integer type: scale, clamp, truncate (port_psq_quantize)."""
    s = mul(v, float(1 << scale))
    if s != s:
        return 0
    if s <= lo:
        return lo
    if s >= hi:
        return hi
    return int(s)


def f32_to_u16(v):
    return quantize(v, 0, 0, 0xFFFF)


def clip_frame(num_frame, frame):
    if frame <= 0.0:
        return 0.0
    if num_frame <= frame:
        return float(num_frame)
    return frame


# --- file reads -------------------------------------------------------------------------------

class Data:
    def __init__(self, data):
        self.d = data

    def u8(self, o):
        return self.d[o]

    def u16(self, o):
        return struct.unpack_from(">H", self.d, o)[0]

    def s16(self, o):
        return struct.unpack_from(">h", self.d, o)[0]

    def u32(self, o):
        return struct.unpack_from(">I", self.d, o)[0]

    def s32(self, o):
        return struct.unpack_from(">i", self.d, o)[0]

    def f(self, o):
        return struct.unpack_from(">f", self.d, o)[0]


# --- shared tracks (g3d_resanm.cpp, g3d_resanm.h) ---------------------------------------------

def hermite(v0, t0, v1, t1, p, d):
    """g3d_resanm.cpp HermiteInterpolation."""
    invd = finv(d)
    s = mul(p, invd)
    s_1 = sub(s, 1.0)
    a = mul(s, mul(s, mul(sub(mul(2.0, s), 3.0), sub(v0, v1))))
    b = mul(mul(p, s_1), add(mul(s_1, t0), mul(s, t1)))
    return add(add(v0, a), b)


def keyframe_result(r, o, frame):
    """GetResKeyFrameAnmResult."""
    n = r.u16(o)
    inv = r.f(o + 4)

    def key(i):
        k = o + 8 + 12 * i
        return r.f(k), r.f(k + 4), r.f(k + 8)

    first, last = key(0), key(n - 1)
    if frame <= first[0]:
        return first[1]
    if last[0] <= frame:
        return last[1]
    est = mul(mul(sub(frame, first[0]), float(n)), inv)
    i = f32_to_u16(est)
    if frame < key(i)[0]:
        i -= 1
        while frame < key(i)[0]:
            i -= 1
    else:
        i += 1
        while key(i)[0] <= frame:
            i += 1
        i -= 1
    left = key(i)
    if left[0] == frame:
        return left[1]
    right = key(i + 1)
    return hermite(left[1], left[2], right[1], right[2], sub(frame, left[0]), sub(right[0], left[0]))


def anm_result(r, word, frame, constant):
    """detail::GetResAnmResult (ResAnmData)."""
    if constant:
        return r.f(word)
    return keyframe_result(r, word + r.s32(word), frame)


def color_lerp(a, b, ratio):
    return (a + (((b - a) * ratio) >> 15)) & 0xFF


def color_result(r, word, frame, constant):
    """detail::GetResColorAnmResult (ResColorAnmData)."""
    if constant:
        return r.u32(word)
    arr = word + r.s32(word)
    frac = math.modf(frame)[0]
    i = int(math.modf(frame)[1])
    if frac == 0.0:
        return r.u32(arr + 4 * i)
    left, right = r.u32(arr + 4 * i), r.u32(arr + 4 * i + 4)
    ratio = quantize(mul(32768.0, frac), 0, -0x8000, 0x7FFF)
    out = 0
    for shift in (24, 16, 8, 0):
        out |= color_lerp(left >> shift & 0xFF, right >> shift & 0xFF, ratio) << shift
    return out


def bool_result(r, word, frame):
    """detail::GetResBoolAnmResult."""
    arr = word + r.s32(word)
    idx = frame & 0xFFFFFFFF
    return (r.u32(arr + 4 * (idx // 32)) & ((1 << 31) >> (idx % 32))) != 0


# --- CHR0 (g3d_resanmchr.cpp) ------------------------------------------------------------------

def sincos_table():
    src = open(os.path.join(REPO, "src", "nw4r", "math", "math_triangular.cpp")).read()
    body = src[src.index("sSinCosTbl[] = {"):]
    body = body[:body.index("};")]
    rows = re.findall(r"\{\s*([-0-9.e]+)f,\s*([-0-9.e]+)f,\s*([-0-9.e]+)f,\s*([-0-9.e]+)f\}", body)
    return [tuple(f32(float(v)) for v in row) for row in rows]


SINCOS = sincos_table()
DEG_TO_FIDX = f32(256.0 / 360.0)


def sincos_fidx(fidx):
    """math::SinCosFIdx."""
    a = abs(fidx)
    while a >= 65536.0:
        a = sub(a, 65536.0)
    whole = f32_to_u16(a)
    frac = sub(a, float(whole))
    s_val, c_val, s_d, c_d = SINCOS[whole & 255]
    s = add(s_val, mul(frac, s_d))
    c = add(c_val, mul(frac, c_d))
    return (-s if fidx < 0.0 else s), c


def rot_xyz_deg(x, y, z):
    """MTX34RotXYZDeg -> MTX34RotXYZFIdx (math_types.cpp), row-major 3x4."""
    sx, cx = sincos_fidx(mul(x, DEG_TO_FIDX))
    sy, cy = sincos_fidx(mul(y, DEG_TO_FIDX))
    sz, cz = sincos_fidx(mul(z, DEG_TO_FIDX))
    m = [[0.0] * 4 for _ in range(3)]
    m[2][0] = -sy
    m[0][0] = mul(cz, cy)
    m[1][0] = mul(sz, cy)
    m[2][1] = mul(cy, sx)
    m[2][2] = mul(cy, cx)
    cx_sz, sx_cz = mul(cx, sz), mul(sx, cz)
    m[0][1] = sub(mul(sx_cz, sy), cx_sz)
    m[1][2] = sub(mul(cx_sz, sy), sx_cz)
    sx_sz, cx_cz = mul(sx, sz), mul(cx, cz)
    m[0][2] = add(mul(cx_cz, sy), sx_sz)
    m[1][1] = add(mul(sx_sz, sy), cx_cz)
    return m


def hermite_chr(v0, t0, v1, t1, s, d):
    """g3d_resanmchr.cpp HermiteInterpolation (s already divided)."""
    s_1 = sub(s, 1.0)
    a = mul(s, mul(s, mul(sub(mul(2.0, s), 3.0), sub(v0, v1))))
    b = mul(mul(d, s_1), add(mul(s_1, t0), mul(s, t1)))
    return add(add(v0, a), b)


class Fvs:
    """CResAnmChrFrm<ResAnmChrFrm{32,48,96}Data> + CAnmFmtTraits."""

    def __init__(self, r, o, fmt):
        self.r, self.o, self.fmt = r, o, fmt
        self.n = r.u16(o)
        self.inv = r.f(o + 4)
        if fmt == "FVS96":
            self.base, self.stride = o + 8, 12
        else:
            self.scale, self.offset = r.f(o + 8), r.f(o + 12)
            self.base, self.stride = o + 16, 4 if fmt == "FVS32" else 6

    def frame(self, i):  # GetFrame (the quantised type)
        k = self.base + self.stride * i
        if self.fmt == "FVS32":
            return self.r.u8(k)
        if self.fmt == "FVS48":
            return self.r.s16(k)
        return self.r.f(k)

    def frame_f32(self, i):  # GetFrameF32
        k = self.base + self.stride * i
        if self.fmt == "FVS32":
            return float(self.r.u8(k))
        if self.fmt == "FVS48":
            return self.r.s16(k) / 32.0
        return self.r.f(k)

    def value(self, i):
        k = self.base + self.stride * i
        if self.fmt == "FVS32":
            raw = (self.r.u32(k) & 0x00FFF000) >> 12
            return add(mul(self.scale, float(raw)), self.offset)
        if self.fmt == "FVS48":
            return add(mul(self.scale, float(self.r.u16(k + 2))), self.offset)
        return self.r.f(k + 4)

    def slope(self, i):
        k = self.base + self.stride * i
        if self.fmt == "FVS32":
            raw = self.r.u32(k) & 0xFFF
            if raw & 0x800:
                raw -= 0x1000
            return raw / 32.0
        if self.fmt == "FVS48":
            return self.r.s16(k + 4) / 256.0
        return self.r.f(k + 8)

    def quantize_frame(self, frame):
        if self.fmt == "FVS32":
            return quantize(frame, 0, 0, 0xFF)
        if self.fmt == "FVS48":
            return quantize(frame, 5, -0x8000, 0x7FFF)
        return frame

    def calc(self, frame):
        """CalcAnimationFVS."""
        n = self.n
        if frame <= self.frame_f32(0):
            return self.value(0)
        if self.frame_f32(n - 1) <= frame:
            return self.value(n - 1)
        est = mul(mul(sub(frame, self.frame_f32(0)), float(n)), self.inv)
        i = f32_to_u16(est)
        q = self.quantize_frame(frame)
        if q < self.frame(i):
            i -= 1
            while q < self.frame(i):
                i -= 1
        else:
            i += 1
            while self.frame(i) <= q:
                i += 1
            i -= 1
        if frame == self.frame_f32(i):
            return self.value(i)
        f0, f1 = self.frame_f32(i), self.frame_f32(i + 1)
        delta = sub(frame, f0)
        inv = finv(sub(f1, f0))
        return hermite_chr(self.value(i), self.slope(i), self.value(i + 1), self.slope(i + 1),
                           mul(delta, inv), delta)


def cv_calc(r, o, fmt, num_frame, frame):
    """CalcAnimationCV."""
    def at(i):
        if fmt == "CV8":
            return add(mul(float(r.u8(o + 8 + i)), r.f(o)), r.f(o + 4))
        if fmt == "CV16":
            return add(mul(float(r.u16(o + 8 + 2 * i)), r.f(o)), r.f(o + 4))
        return r.f(o + 4 * i)

    fi = f32_to_u16(frame)
    if frame <= 0.0:
        return at(0)
    if num_frame <= fi:
        return at(num_frame)
    rr = sub(frame, float(fi))
    v0 = at(fi)
    if rr == 0.0:
        return v0
    v1 = at(fi + 1)
    return add(mul(rr, sub(v1, v0)), v0)


CHR_FMT = {1: "FVS32", 2: "FVS48", 3: "FVS96", 4: "CV8", 5: "CV16", 6: "CV32"}


def chr_component(r, node, word, fmt, constant, num_frame, frame):
    if constant:
        return r.f(word)
    data = node + r.s32(word)
    if fmt.startswith("FVS"):
        return Fvs(r, data, fmt).calc(frame)
    return cv_calc(r, data, fmt, num_frame, frame)


def chr_group(r, node, word, code, const_bits, count, num_frame, frame, allow_cv):
    """GetAnmScale / GetAnmRotation / GetAnmTranslation: values and the next word."""
    flags = r.u32(node + 4)
    if code == 0:
        vals = [r.f(word + 4 * k) for k in range(count)]
        return vals, word + 4 * count
    fmt = CHR_FMT.get(code)
    if fmt is None or (fmt.startswith("CV") and not allow_cv):
        return [0.0] * count, word
    vals = []
    for k in range(count):
        vals.append(chr_component(r, node, word, fmt, flags & const_bits[k], num_frame, frame))
        word += 4
    return vals, word


def chr_result(r, block, node, frame):
    """ResAnmChr::GetAnmResult -> flags, s[3], rawR[3], rt[12]."""
    num_frame = r.u16(block + 0x1C)
    flags = r.u32(node + 4)
    out_flags = flags & 0x1FFF
    s = [1.0, 1.0, 1.0]
    raw = [0.0, 0.0, 0.0]
    m = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]
    word = node + 8
    if flags & A.CHR_HAS_SCALE:
        uniform = flags & A.CHR_SCALE_UNIFORM
        vals, word = chr_group(r, node, word, (flags >> 25) & 3, A.CHR_SCALE_CONST,
                               1 if uniform else 3, num_frame, frame, False)
        s = vals * 3 if uniform else vals
    if flags & A.CHR_HAS_ROT:
        raw, word = chr_group(r, node, word, (flags >> 27) & 7, A.CHR_ROT_CONST, 3, num_frame,
                              frame, True)
        m = rot_xyz_deg(*raw)
        out_flags |= 1 << 31
    if flags & A.CHR_HAS_TRANS:
        t, word = chr_group(r, node, word, (flags >> 30) & 3, A.CHR_TRANS_CONST, 3, num_frame,
                            frame, False)
        m[0][3], m[1][3], m[2][3] = t
    else:
        m[0][3] = m[1][3] = m[2][3] = 0.0
    return ["%08x" % out_flags] + ["%08x" % bits(v) for v in s + raw + m[0] + m[1] + m[2]]


# --- CLR0 / SRT0 / PAT0 -------------------------------------------------------------------------

def clr_result(r, block, mat, frame):
    num_frame = r.u16(block + 0x1C)
    flags = r.u32(mat + 4)
    exist = 0
    rgba = [0] * 11
    mask = [0] * 11
    entry = mat + 8
    if flags:
        clipped = clip_frame(num_frame, frame)
        for i in range(11):
            b = flags >> (2 * i)
            if b & 1:
                exist |= 1 << i
                mask[i] = r.u32(entry)
                rgba[i] = color_result(r, entry + 4, clipped, b & 2)
                entry += 8
    return ["%08x" % exist] + ["%08x:%08x" % (rgba[i], mask[i]) for i in range(11)]


def srt_make(r, tex, frame):
    flags = r.u32(tex)
    word = tex + 4
    su = sv = 1.0
    rot = 0.0
    tu = tv = 0.0
    if not flags & A.SRT_SCALE_ONE:
        su = anm_result(r, word, frame, flags & A.SRT_SU_CONST)
        word += 4
        if flags & A.SRT_SCALE_UNIFORM:
            sv = su
        else:
            sv = anm_result(r, word, frame, flags & A.SRT_SV_CONST)
            word += 4
    if not flags & A.SRT_ROT_ZERO:
        rot = anm_result(r, word, frame, flags & A.SRT_R_CONST)
        word += 4
    if not flags & A.SRT_TRANS_ZERO:
        tu = anm_result(r, word, frame, flags & A.SRT_TU_CONST)
        tv = anm_result(r, word + 4, frame, flags & A.SRT_TV_CONST)
    return flags & 0xF, (su, sv, rot, tu, tv)


def srt_result(r, block, mat, frame):
    flags, ind_flags = r.u32(mat + 4), r.u32(mat + 8)
    srt = [(0.0,) * 5] * 11
    out = [0, 0]
    to_tex = mat + 12
    for group, (fl, base) in enumerate(((flags, 0), (ind_flags, 8))):
        index = 0
        while fl:
            if fl & 1:
                f, vals = srt_make(r, mat + r.s32(to_tex), frame)
                to_tex += 4
                srt[base + index] = vals
                out[group] |= f << (index * 4)
            fl >>= 1
            index += 1
    return (["%08x" % out[0], "%08x" % out[1], "%u" % r.u32(block + 0x20)] +
            [",".join("%08x" % bits(v) for v in t) for t in srt])


def pat_search(r, o, frame):
    n = r.u16(o)
    inv = r.f(o + 4)

    def key(i):
        return struct.unpack_from(">fHH", r.d, o + 8 + 8 * i)

    if frame <= key(0)[0]:
        return key(0)
    if key(n - 1)[0] <= frame:
        return key(n - 1)
    est = mul(mul(sub(frame, key(0)[0]), float(n)), inv)
    i = f32_to_u16(est)
    if frame < key(i)[0]:
        i -= 1
        while frame < key(i)[0]:
            i -= 1
    else:
        i += 1
        while key(i)[0] <= frame:
            i += 1
        i -= 1
    return key(i)


def pat_result(r, block, mat, frame, textures, palettes):
    flags = r.u32(mat + 4)
    word = mat + 8
    btex = bpltt = 0
    slots = [["-", "-"] for _ in range(8)]
    for i in range(8):
        b = flags >> (4 * i)
        if not b & 1:
            continue
        if b & 2:
            tex, pltt = struct.unpack_from(">HH", r.d, word)
        else:
            _f, tex, pltt = pat_search(r, mat + r.s32(word), frame)
        if b & 4 and textures[tex] is not None:
            slots[i][0] = textures[tex]
            btex |= 1 << i
        if b & 8 and palettes[pltt] is not None:
            slots[i][1] = palettes[pltt]
            bpltt |= 1 << i
        word += 4
    return ["%02x" % btex, "%02x" % bpltt] + [",".join(s) for s in slots]


# --- SCN0 (g3d_resanmamblight.cpp, g3d_resanmlight.cpp, g3d_resanmcamera.cpp) -------------------

def amb_result(r, rec, num_frame, frame):
    flags = r.u32(rec + 0x14)
    color = color_result(r, rec + 0x18, clip_frame(num_frame, frame), flags & (1 << 31))
    return ["%08x" % (flags & 3), "%08x" % color]


def light_result(r, rec, num_frame, frame):
    flags = r.u32(rec + 0x1C)
    clipped = clip_frame(num_frame, frame)
    out_flags = flags & 0x3F
    spec_idx = 0
    pos = [0.0] * 3
    aim = [0.0] * 3
    color = spec_color = 0
    dist = spot = 0
    ref_dist = ref_bright = cutoff = shin = 0.0

    def val(off, bit):
        return anm_result(r, rec + off, frame, flags & (1 << bit))

    if not flags & (1 << 23):
        if bool_result(r, rec + 0x20, int(math.floor(clipped))):
            out_flags |= 4
    if out_flags & 4:
        kind = flags & 3
        spec = flags & 8
        pos = [val(0x24, 19), val(0x28, 20), val(0x2C, 21)]
        color = color_result(r, rec + 0x30, clipped, flags & (1 << 22))
        if kind != 0 or spec:
            aim = [val(0x34, 24), val(0x38, 25), val(0x3C, 26)]
        if kind == 0:
            dist = r.u32(rec + 0x40)
            ref_dist, ref_bright = val(0x44, 28), val(0x48, 29)
        elif kind == 2:
            dist, spot = r.u32(rec + 0x40), r.u32(rec + 0x4C)
            cutoff = val(0x50, 27)
            ref_dist, ref_bright = val(0x44, 28), val(0x48, 29)
        if spec:
            spec_color = color_result(r, rec + 0x54, clipped, flags & (1 << 30))
            shin = val(0x58, 31)
            spec_idx = r.u32(rec + 0x14)
    h = lambda v: "%08x" % bits(v)  # noqa: E731
    return (["%08x" % out_flags, "%08x" % spec_idx] + [h(v) for v in pos + aim] +
            ["%08x" % color, "%u" % dist, h(ref_dist), h(ref_bright), "%u" % spot, h(cutoff),
             "%08x" % spec_color, h(shin)])


def camera_result(r, rec, frame):
    proj, flags = r.u32(rec + 0x14), r.u32(rec + 0x18)

    def val(off, bit):
        return anm_result(r, rec + off, frame, flags & (1 << bit))

    pos = [val(0x20, 17), val(0x24, 18), val(0x28, 19)]
    apn = [val(0x2C, 20), val(0x30, 21), val(0x34, 22)]
    union = [0.0] * 4
    if (flags & 1) == 0:
        union[:3] = [val(0x38, 29), val(0x3C, 30), val(0x40, 31)]
    else:
        union = [val(0x44, 25), val(0x48, 26), val(0x4C, 27), val(0x50, 28)]
    fov = 0.0
    if proj == 0:
        fov = val(0x54, 23)
    elif proj == 1:
        fov = val(0x58, 24)
    return (["%08x" % (flags & 3), "%u" % proj] +
            ["%08x" % bits(v) for v in pos + apn + union + [fov]])


# --- driver -------------------------------------------------------------------------------------

def frames(num_frame):
    """anm_harness Frames()."""
    n = float(num_frame)
    out = [-1.5, 0.0, 0.25, 1.0, 1.5]
    for k in range(1, 7):
        q = div(mul(n, float(k)), 7.0)
        out.append(add(q, 0.375))
        out.append(float(int(q)))
    out += [sub(n, 1.0), sub(n, 0.5), n, add(n, 2.5)]
    return out


def expected_lines(path, data, tex_names):
    """Every line anm_harness prints for one BRRES, in its order."""
    r = Data(data)
    reader = A.Reader(data)
    blocks = collections.defaultdict(list)
    for kind, name, block in A.animation_blocks(reader):
        blocks[kind].append((name, block))
    out = []
    for name, block in blocks["CHR0"]:
        nodes = A.record_dictionary(reader, block, "CHR0", 0x10, r.u16(block + 0x1E))
        for frame in frames(r.u16(block + 0x1C)):
            for i, (_n, node) in enumerate(nodes):
                out.append(["CHR", path, name, str(i), "%08x" % bits(frame)] +
                           chr_result(r, block, node, frame))
    for name, block in blocks["CLR0"]:
        mats = A.record_dictionary(reader, block, "CLR0", 0x10, r.u16(block + 0x1E))
        for frame in frames(r.u16(block + 0x1C)):
            for i, (_n, mat) in enumerate(mats):
                out.append(["CLR", path, name, str(i), "%08x" % bits(frame)] +
                           clr_result(r, block, mat, frame))
    for name, block in blocks["SRT0"]:
        mats = A.record_dictionary(reader, block, "SRT0", 0x10, r.u16(block + 0x1E))
        for frame in frames(r.u16(block + 0x1C)):
            for i, (_n, mat) in enumerate(mats):
                out.append(["SRT", path, name, str(i), "%08x" % bits(frame)] +
                           srt_result(r, block, mat, frame))
    for name, block in blocks["PAT0"]:
        parsed = A.parse_block(reader, "PAT0", name, block)
        textures = [t if t in tex_names else None for t in parsed["textures"]]
        palettes = [None for _ in parsed["palettes"]]  # no PLT0 is named by a PAT0 on the disc
        mats = A.record_dictionary(reader, block, "PAT0", 0x10, r.u16(block + 0x2E))
        for frame in frames(r.u16(block + 0x2C)):
            for i, (_n, mat) in enumerate(mats):
                out.append(["PAT", path, name, str(i), "%08x" % bits(frame)] +
                           pat_result(r, block, mat, frame, textures, palettes))
    for name, block in blocks["SCN0"]:
        num_frame = r.u16(block + 0x30)
        counts = struct.unpack_from(">HHHHH", data, block + 0x38)
        arrays = {k: block + r.s32(block + f) for k, f in
                  (("amb", 0x18), ("light", 0x1C), ("cam", 0x24))}
        for frame in frames(num_frame):
            fb = "%08x" % bits(frame)
            for i in range(counts[1]):
                rec = arrays["amb"] + 0x1C * i
                out.append(["AMB", path, name, str(i), fb] + amb_result(r, rec, num_frame, frame))
            for i in range(counts[2]):
                rec = arrays["light"] + 0x5C * i
                out.append(["LIT", path, name, str(i), fb] + light_result(r, rec, num_frame, frame))
            for i in range(counts[4]):
                rec = arrays["cam"] + 0x5C * i
                out.append(["CAM", path, name, str(i), fb] + camera_result(r, rec, frame))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        print("anm_harness failed (exit %d)" % run.returncode)
        return 1
    got = collections.defaultdict(list)
    for line in run.stdout.splitlines():
        fields = line.split("\t")
        got[fields[1]].append(fields)

    # Texture names each archive's BRRES provide (PAT0 slots bind by name within the file or,
    # failing that, the archive: g3d_harness / HOST_BUILD.md §11).
    files = [(p, d) for p, d in walk(args.disc) if d[:4] == b"bres"]
    archive_tex = collections.defaultdict(set)
    file_tex = {}
    for path, data in files:
        reader = A.Reader(data)
        names = set()
        header_size = reader.unpack(0, "4sHHIHH", "BRRES header")[4]
        for group, off in reader.dictionary(header_size + 8, "BRRES root"):
            if group == "Textures(NW4R)":
                names |= {n for n, _ in reader.dictionary(off, group)}
        file_tex[path] = names
        archive_tex[path[:path.rindex(".carc/") + 5] if ".carc/" in path else ""] |= names

    errors = 0
    counts = collections.Counter()
    seen = 0
    for path, data in files:
        arc = path[:path.rindex(".carc/") + 5] if ".carc/" in path else ""
        tex_names = file_tex[path] | archive_tex[arc]
        want = expected_lines(path, data, tex_names)
        have = got.pop(path, [])
        seen += 1
        if len(want) != len(have):
            print("ERROR: %s: harness printed %d results, expected %d" % (path, len(have), len(want)))
            errors += 1
        for w, h in zip(want, have):
            counts[w[0]] += 1
            if w != h:
                errors += 1
                if errors <= 20:
                    print("ERROR: %s\n  want %s\n  have %s" % (path, "\t".join(w), "\t".join(h)))
    for path in got:
        print("ERROR: harness printed %s, not a BRRES on the disc" % path)
        errors += 1
    print("test_anims: %d BRRES; %s; %d errors" % (
        seen, ", ".join("%s %d" % kv for kv in sorted(counts.items())), errors))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
