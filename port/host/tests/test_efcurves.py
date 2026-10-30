#!/usr/bin/env python3
"""Compare `ef_harness --curves` (the real ef_animcurve.cpp on the host) with an independent
evaluation of every BREFF animation curve on the disc (port/docs/HOST_BUILD.md §17).

The harness registers every BREFF/BREFT pair in the game's order (as for test_effects.py) and,
right after each registration, runs every particle and emit track through
AnimCurveExecuteU8 / F32 / Rotate / Texture / Child at a set of ticks, seeds and lifetimes.
This test locates the same tracks with port/assets/tools/fmt_effect.py (read-only) and evaluates
them from the file bytes, following ef_animcurve.cpp: CalcTime / CalcFrameStep (loop, turn and
fitting modes), the key searches, the per-key random seeds (folded byte by byte in the Wii's
big-endian byte order) and ranges, the linear / smooth / eased / step interpolations of bytes
and floats, the random rotation sign, texture keys with random reverse bits, and the child
curve's creation requests. Every f32 operation is rounded as the host build computes it (SSE,
no fused multiply-add). Texture and child names resolve to whatever the registered projects
(boot pairs plus the scene pair) provide, as RelocateCommand does.

Results must match bit for bit.

    test_efcurves.py --harness build/host64/ef_harness --disc ~/wii-play/extracted/files
"""
import argparse
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_effect  # noqa: E402  (read-only reference)
from wiiarc import walk  # noqa: E402

PROJECT_OFFSET = 0x18  # FileHeader (0x10) + BlockHeader (8)
HEADER = 0x20  # sizeof(AnimCurveHeader)
INFLOOP, TURN, FITTING, SYNC = 1 << 5, 1 << 6, 1 << 7, 1 << 2
FLT_EPSILON = struct.unpack("<f", struct.pack("<f", 1.192092895e-7))[0]
M32 = 0xFFFFFFFF


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


def add(a, b):
    return f32(a + b)


def sub(a, b):
    return f32(a - b)


def mul(a, b):
    return f32(a * b)


def div(a, b):
    if b == 0:
        return math.nan if a == 0 else math.copysign(math.inf, a) * math.copysign(1, b)
    return f32(a / b)


def to_int(x):
    """float -> integer conversion (truncation toward zero)."""
    return int(x)


def cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# --- file reads -------------------------------------------------------------------------------

class Curve:
    def __init__(self, d, h):
        self.d, self.h = d, h
        (self.magic, self.kind_type, self.curve_flag, self.kind_enable, self.flag, self.loop_count,
         self.random_seed, self.fl) = struct.unpack_from(">6BHH", d, h)
        self.key_table, self.range_table, self.random_table = struct.unpack_from(">III", d, h + 0xC)
        self.key_tbl = h + HEADER
        self.range_tbl = self.key_tbl + self.key_table
        self.random_tbl = self.range_tbl + self.range_table
        self.name_tbl = self.random_tbl + self.random_table

    def u8(self, o):
        return self.d[o]

    def u16(self, o):
        return struct.unpack_from(">H", self.d, o)[0]

    def s16(self, o):
        return struct.unpack_from(">h", self.d, o)[0]

    def f(self, o):
        return struct.unpack_from(">f", self.d, o)[0]

    def names(self):
        """AnimCurveNameTable: u16 count, pad, u32 work[count], then (u16 len, name) entries."""
        o = self.name_tbl
        count = self.u16(o)
        p = o + 4 + 4 * count
        out = []
        for _ in range(count):
            n = self.u16(p)
            out.append(bytes(self.d[p + 2:p + 2 + n]).split(b"\0", 1)[0].decode("latin-1"))
            p += 2 + n
        return out


# --- timing (ef_animcurve.cpp) ------------------------------------------------------------------

def calc_fitting_loop_time(c, tick, last_tick):
    fl1 = float(c.fl - 1)
    scale = mul(float(c.loop_count), div(fl1, f32(float(last_tick))))
    ts = mul(f32(float(tick)), scale)
    loop = to_int(div(ts, fl1)) & M32
    time = sub(ts, f32(float((loop * (c.fl - 1)) & M32)))
    if not c.flag & TURN or loop % 2 == 0:
        return time, loop
    return sub(fl1, time), loop


def calc_time(c, tick, life):
    """CalcTime -> (time, frame, loop)."""
    fl = c.fl
    loop = 0
    if fl <= 1:
        return 0.0, 0, tick
    flag = c.flag
    if not flag & INFLOOP and c.loop_count <= 1:
        if not flag & FITTING:
            frame = tick & 0xFFFF
            if tick >= fl - 1:
                frame = fl - 1
            time = float(frame)
        else:
            time = mul(f32(float(tick)), div(float(fl - 1), f32(float((life - 1) & M32))))
            if time > float(fl - 1):
                time = float(fl - 1)
            frame = to_int(time) & 0xFFFF
    elif not flag & FITTING:
        loop = tick // (fl - 1)
        if not flag & TURN:
            if not flag & INFLOOP and loop >= c.loop_count:
                frame = fl - 1
                loop = (c.loop_count - 1) & 0xFF
            else:
                frame = (tick - loop * (fl - 1)) & 0xFFFF
        else:
            if not flag & INFLOOP and loop >= c.loop_count:
                frame = 0 if c.loop_count % 2 == 0 else fl - 1
                loop = (c.loop_count - 1) & 0xFF
            elif loop % 2 == 0:
                frame = (tick - loop * (fl - 1)) & 0xFFFF
            else:
                frame = ((fl - 1) * (loop + 1) - tick) & 0xFFFF
        time = float(frame)
    else:
        last_tick = (life - 1) & M32
        if tick >= last_tick:
            frame = (fl - 1) & 0xFF if (not flag & TURN or c.loop_count % 2 != 0) else 0
            time = float(frame)
            loop = (c.loop_count - 1) & 0xFF
        else:
            time, loop = calc_fitting_loop_time(c, tick, last_tick)
            frame = to_int(time) & 0xFFFF
    return time, frame, loop


def calc_frame_step(c, tick, life):
    """CalcFrameStep -> (frame, loop, end)."""
    fl, flag, lc = c.fl, c.flag, c.loop_count
    if not flag & INFLOOP and lc <= 1:
        if not flag & FITTING:
            return (fl if tick >= fl else tick & 0xFFFF), 0, False
        return (((fl * tick) & M32) // life) & 0xFFFF, 0, False
    if not flag & FITTING:
        if fl <= 1 or not flag & TURN:
            loop = tick // fl
            if not flag & INFLOOP and loop >= lc:
                return fl, (lc - 1) & 0xFF, False
            return (tick - loop * fl) & 0xFFFF, loop, False
        loop = tick // (fl - 1)
        if not flag & INFLOOP and loop >= lc:
            end = False
            if lc % 2 == 0:
                if loop == lc and tick - loop * (fl - 1) == 0:
                    end = True
                frame = 0
            else:
                frame = (fl - 1) & 0xFF
            return frame, (lc - 1) & 0xFF, end
        if loop % 2 == 0:
            return (tick - loop * (fl - 1)) & 0xFFFF, loop, False
        return ((fl - 1) - (tick - loop * (fl - 1))) & 0xFFFF, loop, False
    if tick >= life:
        frame = fl if (not flag & TURN or lc % 2 != 0) else 0
        return frame, (lc - 1) & 0xFF, False
    if fl <= 1 or not flag & TURN:
        scale = mul(float(lc), div(float(fl), f32(float(life))))
        ts = mul(f32(float(tick)), scale)
        loop = to_int(div(ts, float(fl))) & M32
        frame = to_int(sub(ts, f32(float((loop * fl) & M32)))) & 0xFFFF
        return frame, loop, False
    scale = div(add(1.0, mul(float(fl - 1), float(lc))), f32(float(life)))
    pos = to_int(mul(f32(float(tick)), scale)) & M32
    loop = pos // (fl - 1)
    if loop >= lc:
        end = False
        if lc % 2 == 0:
            if tick == life - 1:
                end = True
            frame = 0
        else:
            frame = (fl - 1) & 0xFF
        return frame, (lc - 1) & 0xFF, end
    if loop % 2 == 0:
        return (pos - loop * (fl - 1)) & 0xFFFF, loop, False
    return ((fl - 1) * (loop + 1) - pos) & 0xFFFF, loop, False


# --- key search and randoms -----------------------------------------------------------------------

def search_key_stride(c, key_size, frame, time):
    """SearchKeyStride -> (exact, idx, prevFrame, nextFrame)."""
    keys = c.key_tbl + 4

    def kf(i):
        return c.u16(keys + i * key_size)

    lo, hi = 0, c.u16(c.key_tbl) - 1
    mid = cdiv(hi, 2)
    exact = abs(sub(float(frame), time)) < FLT_EPSILON
    prev, nxt = kf(0), None
    if frame < kf(0):
        return True, 0, prev, nxt
    if frame == kf(0):
        if hi == 0:
            return True, 0, prev, nxt
        if not exact:
            nxt = kf(1)
        return exact, 0, prev, nxt
    nxt = kf(hi)
    if nxt <= frame:
        return True, hi, prev, nxt
    mid_f = kf(mid)
    while lo < mid:
        if frame == mid_f:
            if not exact:
                prev, nxt = mid_f, kf(mid + 1)
            return exact, mid, prev, nxt
        if mid_f < frame:
            lo, prev = mid, mid_f
        else:
            hi, nxt = mid, mid_f
        mid = (lo + hi) // 2
        mid_f = kf(mid)
    return False, lo, prev, nxt


def search_key_step(c, stride, frame, lower):
    """SearchKeyStep over keys whose u16 frame is at offset 0."""
    keys = c.key_tbl + 4
    n = c.u16(c.key_tbl)

    def kf(i):
        return c.u16(keys + i * stride)

    hi, lo = n - 1, 0
    mid = cdiv(hi, 2)
    first = kf(0)
    if frame < first or (frame == first and lower):
        return 0
    if kf(hi) < frame or (frame == kf(hi) and not lower):
        return hi
    mid_f = kf(mid)
    while lo < mid:
        if frame == mid_f:
            if lower:
                hi = mid
            else:
                lo = mid
        else:
            if mid_f < frame:
                lo = mid
            if mid_f >= frame:
                hi = mid
        mid = (lo + hi) // 2
        mid_f = kf(mid)
    return lo


def mix(rnd):
    return (rnd * 0x343FD + 0x269EC3) & M32


def random_seed(c, seed, loop, range_idx):
    """CalcRandomSeed: the sum, then bytes[2] ^= bytes[3] ... in the Wii's byte order."""
    v = (seed * 0x3F81F635 + c.random_seed * 0x30A74193 + range_idx * 0x371097E7 +
         loop * 0x7B929 + 0x4BF53) & M32
    b = [v >> 24 & 0xFF, v >> 16 & 0xFF, v >> 8 & 0xFF, v & 0xFF]
    b[2] ^= b[3]
    b[1] ^= b[2]
    b[0] ^= b[1]
    return b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]


def random_range(c, range_size, random, range_idx, loop, seed):
    """GetRandomRangeU8 -> (rnd, range offset)."""
    rnd = random_seed(c, seed, loop, range_idx)
    if not random & 2:
        return rnd, c.range_tbl + 4 + range_idx * range_size
    rng = c.random_tbl + 4 + ((rnd >> 16) % c.u16(c.random_tbl)) * range_size
    return mix(rnd), rng


def update(c, rnd):
    return rnd if c.flag & SYNC else mix(rnd)


def adjust_turn_loop(c, idx, loop):
    loop0 = loop1 = loop
    if c.flag & TURN and (c.flag & INFLOOP or c.loop_count > 1):
        if not loop & 1 and idx + 1 >= c.u16(c.key_tbl) - 1:
            if c.flag & INFLOOP or loop < ((c.loop_count - 1) & M32):
                loop1 = loop + 1
        if loop & 1 and idx == 0 and loop != 0:
            loop0 = loop + 1
    return loop0, loop1


def s16_of(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def random_u8(c, rng, rnd):
    return c.u8(rng) + cdiv(c.u8(rng + 1) * s16_of(rnd >> 16), 32768)


def clamp_u8(v):
    return 0 if v < 0 else 255 if v > 255 else v


def random_f32(c, rng, rnd):
    """CalcRandomValue(base, width, rnd)."""
    return add(c.f(rng), mul(c.f(rng + 4), float(rnd >> 16)))


# --- interpolation ------------------------------------------------------------------------------

def ease(interp):
    return (1.5 if interp >> 2 & 1 else 0.0), (1.5 if interp >> 3 & 1 else 0.0)


def ease_factor(t, ease_in, ease_out):
    return add(ease_out, add(mul(t, mul(t, sub(add(ease_out, ease_in), 2.0))),
                             mul(t, add(3.0, sub(mul(-2.0, ease_out), ease_in)))))


def interp_u8(interp, t, v0, v1):
    k = interp & 3
    if k == 0:
        return (v0 + (((t * ((v1 - v0) & M32)) & M32) >> 16)) & 0xFF
    if k == 1:
        ft = div(f32(float(t)), 65536.0)
        if interp == 1:
            v = add(float(v0), mul(ft, mul(ft, mul(sub(3.0, mul(2.0, ft)), float(v1 - v0)))))
            return to_int(v) & 0xFF
        ease_in, ease_out = ease(interp)
        v = add(float(v0), mul(mul(ft, float(v1 - v0)), ease_factor(ft, ease_in, ease_out)))
        if v < 0.0:
            return 0
        if v > 255.0:
            return 255
        return to_int(v) & 0xFF
    if k == 2:
        return v0
    return 0


def interp_f32(interp, t, v0, v1):
    k = interp & 3
    if k == 0:
        return add(v0, mul(t, sub(v1, v0)))
    if k == 1:
        if interp == 1:
            return add(v0, mul(t, mul(t, mul(sub(3.0, mul(2.0, t)), sub(v1, v0)))))
        ease_in, ease_out = ease(interp)
        return add(v0, mul(mul(t, sub(v1, v0)), ease_factor(t, ease_in, ease_out)))
    if k == 2:
        return v0
    return 0.0


def enable_mask(c):
    num, mask = 8, 0x80
    while mask:
        if c.kind_enable & mask:
            break
        num -= 1
        mask >>= 1
    return num, mask


# --- AnimCurveExecute* -------------------------------------------------------------------------

def exec_u8(c, tick, seed, life):
    target = [0xEE] * 8
    enable = c.kind_enable
    num, mask = enable_mask(c)
    if num == 0:
        return target
    key_size = (num + 13) & ~1
    range_size = num * 2
    time, frame, loop = calc_time(c, tick, life)
    keys = c.key_tbl + 4
    exact, idx, prev, nxt = search_key_stride(c, key_size, frame, time)
    pos = 0
    if exact:
        key = keys + idx * key_size
        random = c.u8(key + 2)
        pv = key + 12
        if random == 0:
            while mask:
                if enable & mask:
                    target[pos] = c.u8(pv)
                    pv += 1
                pos += 1
                mask >>= 1
        else:
            rnd, rng = random_range(c, range_size, random, c.u16(pv), loop, seed)
            while mask:
                if enable & mask:
                    v = clamp_u8(random_u8(c, rng, rnd))
                    rng += 2
                    rnd = update(c, rnd)
                    target[pos] = v
                pos += 1
                mask >>= 1
        return target
    loop0, loop1 = adjust_turn_loop(c, idx, loop)
    t = (to_int(mul(65536.0, sub(time, float(prev)))) & M32) // ((nxt - prev) & M32)
    k0, k1 = keys + idx * key_size, keys + (idx + 1) * key_size
    pv0, pv1, pi = k0 + 12, k1 + 12, k0 + 4
    r0, r1 = c.u8(k0 + 2), c.u8(k1 + 2)
    if not r0 and not r1:
        while mask:
            if enable & mask:
                v0, v1 = c.u8(pv0), c.u8(pv1)
                target[pos] = v0 if v0 == v1 else interp_u8(c.u8(pi), t, v0, v1)
                pv0 += 1
                pv1 += 1
            pi += 1
            pos += 1
            mask >>= 1
    elif r0 and not r1:
        rnd, rng = random_range(c, range_size, r0, c.u16(pv0), loop0, seed)
        while mask:
            if enable & mask:
                v0 = clamp_u8(random_u8(c, rng, rnd))
                rng += 2
                rnd = update(c, rnd)
                target[pos] = interp_u8(c.u8(pi), t, v0, c.u8(pv1))
                pv1 += 1
            pi += 1
            pos += 1
            mask >>= 1
    elif not r0 and r1:
        rnd, rng = random_range(c, range_size, r1, c.u16(pv1), loop1, seed)
        while mask:
            if enable & mask:
                v1 = clamp_u8(random_u8(c, rng, rnd))
                rng += 2
                rnd = update(c, rnd)
                target[pos] = interp_u8(c.u8(pi), t, c.u8(pv0), v1)
                pv0 += 1
            pi += 1
            pos += 1
            mask >>= 1
    else:
        rnd0, rng0 = random_range(c, range_size, r0, c.u16(pv0), loop0, seed)
        rnd1, rng1 = random_range(c, range_size, r1, c.u16(pv1), loop1, seed)
        while mask:
            if enable & mask:
                v0 = clamp_u8(random_u8(c, rng0, rnd0))
                rng0 += 2
                rnd0 = update(c, rnd0)
                v1 = clamp_u8(random_u8(c, rng1, rnd1))
                rng1 += 2
                rnd1 = update(c, rnd1)
                target[pos] = interp_u8(c.u8(pi), t, v0, v1)
            pi += 1
            pos += 1
            mask >>= 1
    return target


EE = struct.unpack("<f", b"\xee\xee\xee\xee")[0]


def exec_f32(c, tick, seed, life):
    target = [None] * 8
    enable = c.kind_enable
    time, frame, loop = calc_time(c, tick, life)
    num, mask = enable_mask(c)
    if num == 0:
        return target
    key_size = num * 4 + 12
    range_size = num * 8
    keys = c.key_tbl + 4
    exact, idx, prev, nxt = search_key_stride(c, key_size, frame, time)
    pos = 0
    if exact:
        key = keys + idx * key_size
        random = c.u8(key + 2)
        pv = key + 12
        if random == 0:
            while mask:
                if enable & mask:
                    target[pos] = c.f(pv)
                pv += 4
                pos += 1
                mask >>= 1
        else:
            rnd, rng = random_range(c, range_size, random, c.u16(pv), loop, seed)
            while mask:
                if enable & mask:
                    target[pos] = random_f32(c, rng, rnd)
                    rng += 8
                    rnd = update(c, rnd)
                pos += 1
                mask >>= 1
        return target
    loop0, loop1 = adjust_turn_loop(c, idx, loop)
    t = div(sub(time, float(prev)), float(nxt - prev))
    k0, k1 = keys + idx * key_size, keys + (idx + 1) * key_size
    pv0, pv1, pi = k0 + 12, k1 + 12, k0 + 4
    r0, r1 = c.u8(k0 + 2), c.u8(k1 + 2)
    if not r0 and not r1:
        while mask:
            if enable & mask:
                v0, v1 = c.f(pv0), c.f(pv1)
                target[pos] = v0 if v0 == v1 else interp_f32(c.u8(pi), t, v0, v1)
                pv0 += 4
                pv1 += 4
            pi += 1
            pos += 1
            mask >>= 1
    elif r0 and not r1:
        rnd, rng = random_range(c, range_size, r0, c.u16(pv0), loop0, seed)
        while mask:
            if enable & mask:
                v0 = random_f32(c, rng, rnd)
                rng += 8
                rnd = update(c, rnd)
                target[pos] = interp_f32(c.u8(pi), t, v0, c.f(pv1))
                pv1 += 4
            pi += 1
            pos += 1
            mask >>= 1
    elif not r0 and r1:
        rnd, rng = random_range(c, range_size, r1, c.u16(pv1), loop1, seed)
        while mask:
            if enable & mask:
                v1 = random_f32(c, rng, rnd)
                rng += 8
                rnd = update(c, rnd)
                target[pos] = interp_f32(c.u8(pi), t, c.f(pv0), v1)
                pv0 += 4
            pi += 1
            pos += 1
            mask >>= 1
    else:
        rnd0, rng0 = random_range(c, range_size, r0, c.u16(pv0), loop0, seed)
        rnd1, rng1 = random_range(c, range_size, r1, c.u16(pv1), loop1, seed)
        while mask:
            if enable & mask:
                v0 = random_f32(c, rng0, rnd0)
                rng0 += 8
                rnd0 = update(c, rnd0)
                v1 = random_f32(c, rng1, rnd1)
                rng1 += 8
                rnd1 = update(c, rnd1)
                target[pos] = interp_f32(c.u8(pi), t, v0, v1)
            pi += 1
            pos += 1
            mask >>= 1
    return target


def random_sign(c, rng, rnd):
    """GetRandomSign -> (negate, rnd)."""
    if c.u8(rng + 24):
        return (not rnd & 0x10000), mix(rnd)
    return False, rnd


def exec_rotate(c, tick, seed, life):
    target = [None] * 8
    time, frame, loop = calc_time(c, tick, life)
    keys = c.key_tbl + 4
    exact, idx, prev, nxt = search_key_stride(c, 24, frame, time)

    def rv(rng, i, rnd, negate):
        v = random_f32(c, rng + 8 * i, rnd)
        return -v if negate else v

    if exact:
        key = keys + idx * 24
        random = c.u8(key + 2)
        pv = key + 12
        if random == 0:
            target[:3] = [c.f(pv + 4 * i) for i in range(3)]
        else:
            rnd, rng = random_range(c, 28, random, c.u16(pv), loop, seed)
            negate, rnd = random_sign(c, rng, rnd)
            for i in range(3):
                target[i] = rv(rng, i, rnd, negate)
                if i < 2:
                    rnd = update(c, rnd)
        return target
    loop0, loop1 = adjust_turn_loop(c, idx, loop)
    t = div(sub(time, float(prev)), float(nxt - prev))
    k0, k1 = keys + idx * 24, keys + (idx + 1) * 24
    pv0, pv1, pi = k0 + 12, k1 + 12, k0 + 4
    r0, r1 = c.u8(k0 + 2), c.u8(k1 + 2)
    if not r0 and not r1:
        for i in range(3):
            v0, v1 = c.f(pv0 + 4 * i), c.f(pv1 + 4 * i)
            target[i] = v0 if v0 == v1 else interp_f32(c.u8(pi + i), t, v0, v1)
    elif r0 and not r1:
        rnd, rng = random_range(c, 28, r0, c.u16(pv0), loop0, seed)
        negate, rnd = random_sign(c, rng, rnd)
        for i in range(3):
            target[i] = interp_f32(c.u8(pi + i), t, rv(rng, i, rnd, negate), c.f(pv1 + 4 * i))
            rnd = update(c, rnd)
    elif not r0 and r1:
        rnd, rng = random_range(c, 28, r1, c.u16(pv1), loop1, seed)
        negate, rnd = random_sign(c, rng, rnd)
        for i in range(3):
            target[i] = interp_f32(c.u8(pi + i), t, c.f(pv0 + 4 * i), rv(rng, i, rnd, negate))
            rnd = update(c, rnd)
    else:
        rnd0, rng0 = random_range(c, 28, r0, c.u16(pv0), loop0, seed)
        rnd1, rng1 = random_range(c, 28, r1, c.u16(pv1), loop1, seed)
        negate0, rnd0 = random_sign(c, rng0, rnd0)
        negate1, rnd1 = random_sign(c, rng1, rnd1)
        for i in range(3):
            target[i] = interp_f32(c.u8(pi + i), t, rv(rng0, i, rnd0, negate0),
                                   rv(rng1, i, rnd1, negate1))
            rnd0, rnd1 = update(c, rnd0), update(c, rnd1)
    return target


def exec_texture(c, tick, seed, life, resolve):
    frame, loop, _end = calc_frame_step(c, tick, life)
    textures = ["-"] * 3
    layer = {14: 0, 15: 1, 16: 2}.get(c.kind_type)
    if layer is None:
        return textures + ["0", "0"]
    key = c.key_tbl + 4 + 16 * search_key_step(c, 16, frame, False)
    random = c.u8(key + 2)
    if random == 0:
        name, wrap, reverse = c.u16(key + 0xE), c.u8(key + 0xC), c.u8(key + 0xD)
    else:
        range_idx = c.u16(key + 0xC)
        rnd = random_seed(c, seed, loop, range_idx)
        if not random & 2:
            r = c.range_tbl + 4 + 8 * range_idx
            name, wrap, reverse = c.u16(r + 2), c.u8(r), c.u8(r + 1)
            mode = c.u8(r + 4)
            if mode == 1:
                reverse = (reverse & 2) | ((rnd >> 16) & 1)
            elif mode == 2:
                reverse = (reverse & 1) | ((rnd >> 16) & 2)
            elif mode == 3:
                reverse = (rnd >> 16) & 3
        else:
            r = c.random_tbl + 4 + 4 * ((rnd >> 16) % c.u16(c.random_tbl))
            name, wrap, reverse = c.u16(r + 2), c.u8(r), c.u8(r + 1)
    textures[layer] = resolve(c.names()[name])
    return textures + ["%x" % (((wrap & 0xF) << layer * 4) & 0xFFFF),
                       "%x" % (((reverse & 3) << layer * 2) & 0xFF)]


CHILD_KEY = 22  # sizeof(AnimCurveKeyChild)
CHILD = 10  # sizeof(AnimCurveChild)


def exec_child(c, tick, seed, life, resolve):
    out = []
    names = c.names()

    def create(key, loop):
        if c.u8(key + 2) == 0:
            child = key + 0xC
        else:
            rnd = random_seed(c, seed, loop, c.u16(key + 0xC))
            if not c.u8(key + 2) & 2:
                raise ValueError("random child key without a table (pChild is not set)")
            count = c.u16(c.random_tbl)
            if count == 0:
                return
            child = c.random_tbl + 4 + CHILD * ((rnd >> 16) % count)
        name = resolve(names[c.u16(child + 8)])
        if name == "-":
            return
        setting = bytes(c.d[child:child + 8])
        out.append("%d,%s,%d,%s,%u" % (0 if setting[6] == 0 else 1, setting.hex(), c.s16(child),
                                       name, 3))

    keys = c.key_tbl + 4
    n = c.u16(c.key_tbl)

    def kf(i):
        return c.u16(keys + CHILD_KEY * i)

    def key(i):
        return keys + CHILD_KEY * i

    frame0, loop0, end = calc_frame_step(c, tick, life)
    frame1, loop1, _end1 = calc_frame_step(c, tick + 1, life)
    if loop0 == loop1:
        if frame0 == frame1:
            if not end:
                return out
            frame1 = 1
        if frame0 < frame1:
            i = search_key_step(c, CHILD_KEY, frame0, True)
            while i < n:
                if kf(i) >= frame0:
                    if frame1 <= kf(i):
                        return out
                    create(key(i), loop0)
                i += 1
        else:
            i = search_key_step(c, CHILD_KEY, frame0, False)
            while i >= 0:
                if frame0 >= kf(i):
                    if kf(i) <= frame1:
                        return out
                    create(key(i), loop0)
                i -= 1
        return out
    if c.flag & TURN:
        if not loop0 & 1:
            i = search_key_step(c, CHILD_KEY, frame0, True)
            while i < n:
                if kf(i) >= frame0:
                    if kf(i) == c.fl - 1:
                        break
                    create(key(i), loop0)
                i += 1
        else:
            i = search_key_step(c, CHILD_KEY, frame0, False)
            while i >= 0:
                if frame0 >= kf(i):
                    if kf(i) == 0:
                        break
                    create(key(i), loop0)
                i -= 1
        for _loop in range(loop0 + 1, loop1):
            if not loop0 & 1:
                for i in range(n):
                    if kf(i) == c.fl - 1:
                        break
                    create(key(i), loop0)
            else:
                for i in range(n - 1, -1, -1):
                    if kf(i) == 0:
                        break
                    create(key(i), loop0)
        if not loop1 & 1:
            for i in range(n):
                if frame1 <= kf(i):
                    return out
                create(key(i), loop0)
        else:
            for i in range(n - 1, -1, -1):
                if frame1 >= kf(i):
                    return out
                create(key(i), loop0)
    else:
        i = search_key_step(c, CHILD_KEY, frame0, True)
        while i < n:
            if kf(i) >= frame0:
                create(key(i), loop0)
            i += 1
        for _loop in range(loop0 + 1, loop1):
            for i in range(n):
                create(key(i), loop0)
        for i in range(n):
            if frame1 <= kf(i):
                return out
            create(key(i), loop0)
    return out


# --- driver -------------------------------------------------------------------------------------

def project_names(d):
    """Names in a BREFF/BREFT project's NameTable."""
    table = PROJECT_OFFSET + struct.unpack_from(">I", d, PROJECT_OFFSET)[0]
    out, p = [], table + 8
    for _ in range(struct.unpack_from(">H", d, table + 4)[0]):
        n = struct.unpack_from(">H", d, p)[0]
        out.append(bytes(d[p + 2:p + 2 + n]).split(b"\0", 1)[0].decode("latin-1"))
        p += 2 + n + 8
    return out


def ticks(life):
    out = list(range(0, life + 2, 1 if life < 40 else 3))
    return out + [250, 1000]


def expected(c, seed, life, tick, resolve_tex, resolve_emitter):
    if c.curve_flag == 0:
        return ["%02x" % v for v in exec_u8(c, tick, seed, life)]
    if c.curve_flag in (3, 6):
        vals = exec_f32(c, tick, seed, life) if c.curve_flag == 3 else exec_rotate(c, tick, seed, life)
        return ["eeeeeeee" if v is None else "%08x" % bits(v) for v in vals]
    if c.curve_flag == 4:
        return exec_texture(c, tick, seed, life, resolve_tex)
    if c.curve_flag == 5:
        return exec_child(c, tick, seed, life, resolve_emitter)
    return ["?"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc, "--curves"], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        print("ef_harness failed (exit %d)" % run.returncode)
        return 1

    files = {p: d for p, d in walk(args.disc) if d[:4] in (b"REFF", b"REFT")}
    names = {p: set(project_names(d)) for p, d in files.items()}
    parsed = {}

    registered = []  # (breff, breft) pairs in registration order
    errors = 0
    seen_tracks = set()
    curves = {}  # (path, emitter, track) -> Curve located by fmt_effect
    evals = children = 0
    kinds = {}
    for line in run.stdout.splitlines():
        if line.startswith("EF "):
            breff = line.split()[1]
            registered.append((breff, breff[:-len(".breff")] + ".breft"))
            continue
        if line.startswith("STAGE reset:"):
            stem = line.split()[1][len("reset:"):]
            registered = [p for p in registered if os.path.basename(p[0]) != stem + ".breff"]
            continue
        if line.startswith("K "):
            f = line.split()
            path, emitter, track, off = f[1], int(f[2]), f[3], int(f[4], 16)
            if path not in parsed:
                parsed[path] = fmt_effect.parse(files[path])
            em = parsed[path]["emitters"][emitter]
            j = int(track[1:])
            index = j if track[0] == "p" else em["particleTrackCount"] + j
            if em["tracks"][index]["offset"] != off:
                print("ERROR: %s emitter %d track %s at %x, fmt_effect says %x" % (
                    path, emitter, track, off, em["tracks"][index]["offset"]))
                errors += 1
            if em["tracks"][index]["magic"] == fmt_effect.ANIM_CURVE_MAGIC:
                seen_tracks.add((path, emitter, track))
            curves[(path, emitter, track)] = Curve(files[path], em["tracks"][index]["offset"])
            continue
        if not line.startswith("C "):
            continue
        head, _, rest = line.partition(" :")
        _, path, emitter, track, seed, life, tick = head.split()
        have = rest.split()
        tex_names = set().union(*(names[t] for _, t in registered))
        emitter_names = set().union(*(names[e] for e, _ in registered))
        c = curves[(path, int(emitter), track)]
        try:
            want = expected(c, int(seed, 16), int(life), int(tick),
                            lambda n: n if n in tex_names else "-",
                            lambda n: n if n in emitter_names else "-")
        except (IndexError, ValueError, struct.error) as exc:
            want = ["exception: %s" % exc]
        seen_tracks.discard((path, int(emitter), track))
        evals += 1
        flag = c.curve_flag
        kinds[flag] = kinds.get(flag, 0) + 1
        if flag == 5:
            children += len(have)
        if want != have:
            errors += 1
            if errors <= 20:
                print("ERROR: %s\n  want %s" % (line, " ".join(want)))
    for t in sorted(seen_tracks):
        print("ERROR: curve %s emitter %d track %s was not evaluated" % t)
        errors += 1
    names_k = {0: "U8", 3: "F32", 6: "Rotate", 4: "Texture", 5: "Child"}
    print("test_efcurves: %d evaluations (%s), %d child requests; %d errors" % (
        evals, ", ".join("%s %d" % (names_k.get(k, k), v) for k, v in sorted(kinds.items())),
        children, errors))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
