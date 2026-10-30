#!/usr/bin/env python3
"""Compare coll_harness (the game's KCL and PMP loaders on the host) with
port/assets/tools/fmt_collision.py and an independent model of the loaders.

Runs port/host/build/*/coll_harness on the extracted disc and builds the same
lines (see the harness header) from the bytes:

  KH, KP   the header RPKcl holds and every prism with its position and
           normals, from fmt_collision.parse_kcl. sphereRadius is the word at
           0x38: the header's own field for 0x3C-byte headers, positions[0].x
           for 0x38-byte headers (ASSETS.md 2.8).
  KF       the file is unchanged and a second SetResource is a no-op.
  KQ, KS   the query point inside each prism (computed in double from the
           file values, like the harness), and the results of RPKcl::CheckPoint
           and RPKcl::SearchSphereOctree / CheckSphereTri, modelled here in
           single precision step by step (each float operation rounded to
           f32) over the octree read from the bytes.
  PH, PW   the PMP header and every run of bytes RPResFile::Relocate changed:
           flag bit 0, the section slots and the path slots of the entries
           the type-0 filter returns, as big-endian emulated addresses for a
           file at the address the harness reports (PB).
  PC, PN   CountEntriesOfType / FindNthEntryOfType over the filtered entries.
  PO, PR   RPPathObject: position, vec20, radius and parameters read from the
           entry, and the rotation (Matrix34f::calcRPY), compared within 1e-5.
  PS       the synthetic PMP with paths passed its checks.

    python3 test_collision.py --harness build/host64/coll_harness --disc DISC
"""
import argparse
import collections
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_collision  # noqa: E402
import wiiarc  # noqa: E402

_F = struct.Struct("<f")
COW_TYPES = range(0x70000, 0x7000B)
MAX_HITS = 8


def f(x):
    """Round a double to the nearest f32 (what each float operation does)."""
    try:
        return _F.unpack(_F.pack(x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


def bits(x):
    return "%08x" % struct.unpack(">I", struct.pack(">f", x))[0]


def be_f(data, off):
    return struct.unpack_from(">f", data, off)[0]


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def trunc_s32(x):
    """static_cast<s32>(f32) as an unsigned 32-bit pattern."""
    return int(x) & 0xFFFFFFFF


def dot3(a, b):
    return f(f(f(a[0] * b[0]) + f(a[1] * b[1])) + f(a[2] * b[2]))


# --------------------------------------------------------------------------
# KCL
# --------------------------------------------------------------------------
class Kcl:
    def __init__(self, data, parsed):
        h = parsed["header"]
        self.data = data
        self.blk = h["pBlocks"]
        self.thickness = h["thickness"]
        self.area = (h["areaMin"]["x"], h["areaMin"]["y"], h["areaMin"]["z"])
        self.mask = (h["maskX"], h["maskY"], h["maskZ"])
        self.shift, self.shift_y, self.shift_z = h["shift"], h["shiftY"], h["shiftZ"]
        self.pos = [(p["x"], p["y"], p["z"]) for p in parsed["sections"]["positions"]["entries"]]
        self.nrm = [(n["x"], n["y"], n["z"]) for n in parsed["sections"]["normals"]["entries"]]
        self.prisms = parsed["sections"]["prisms"]["entries"]

    def s32_at(self, off):
        return struct.unpack_from(">i", self.data, off)[0]

    def u16_at(self, off):
        return struct.unpack_from(">H", self.data, off)[0]

    def leaf(self, lx, ly, lz):
        """The octree walk of CheckPoint / SearchSphereOctree: (list offset, level)."""
        level = self.shift
        key = ((((lz >> level) << self.shift_z) | ((ly >> level) << self.shift_y)
                | (lx >> level)) * 4) & 0xFFFFFFFF
        node = self.blk
        while True:
            value = self.s32_at(node + key)
            if value < 0:
                break
            node += value
            level -= 1
            key = ((((lz >> level) & 1) << 2) | (((ly >> level) & 1) << 1)
                   | ((lx >> level) & 1)) * 4
        return node + (value & 0x7FFFFFFF), level

    def prism_list(self, lst):
        out, cur = [], lst + 2
        while True:
            idx = self.u16_at(cur)
            if idx == 0:
                return out
            out.append(idx)
            cur += 2

    def hit(self, prism, distance):
        p = self.pos[prism["positionIdx"]]
        n = self.nrm[prism["normal0Idx"]]
        return ",".join([bits(p[0]), bits(p[1]), bits(p[2]), bits(n[0]), bits(n[1]),
                         bits(n[2]), bits(distance), str(prism["attribute"])])

    def check_point(self, q):
        local = []
        for i in range(3):
            v = trunc_s32(f(q[i] - self.area[i]))
            if v & self.mask[i]:
                return []
            local.append(v)
        lst, _level = self.leaf(*local)
        hits = []
        for idx in self.prism_list(lst):
            pr = self.prisms[idx]
            p = self.pos[pr["positionIdx"]]
            off = (f(q[0] - p[0]), f(q[1] - p[1]), f(q[2] - p[2]))
            if dot3(off, self.nrm[pr["normal1Idx"]]) > 0.0:
                continue
            if dot3(off, self.nrm[pr["normal2Idx"]]) > 0.0:
                continue
            if dot3(off, self.nrm[pr["normal3Idx"]]) > pr["height"]:
                continue
            n = self.nrm[pr["normal0Idx"]]
            distance = f(f(f(-off[0] * n[0]) - f(off[1] * n[1])) - f(off[2] * n[2]))
            if distance < 0.0 or distance > self.thickness:
                continue
            hits.append(self.hit(pr, distance))
        return hits

    def sphere_tri(self, pr, c, r):
        """RPKcl::CheckSphereTri; returns the penetration or None."""
        r_sq = f(r * r)
        p = self.pos[pr["positionIdx"]]
        off = (f(c[0] - p[0]), f(c[1] - p[1]), f(c[2] - p[2]))
        n1, n2 = self.nrm[pr["normal1Idx"]], self.nrm[pr["normal2Idx"]]
        n3, n0 = self.nrm[pr["normal3Idx"]], self.nrm[pr["normal0Idx"]]
        d1 = dot3(off, n1)
        if d1 >= r:
            return None
        d2 = dot3(off, n2)
        if d2 >= r:
            return None
        d3 = f(dot3(off, n3) - pr["height"])
        if d3 >= r:
            return None
        d0 = dot3(off, n0)
        pen = f(r - d0)
        if pen < 0.0 or pen > self.thickness:
            return None

        def corner(dot, da, db, na, nb):
            # a = (dot * db - da) / (dot * dot - 1); b = db - a * dot; offset = a na + b nb
            a = f(f(f(dot * db) - da) / f(f(dot * dot) - 1.0)) if f(f(dot * dot) - 1.0) != 0.0 \
                else (math.nan if f(f(dot * db) - da) == 0.0
                      else math.copysign(math.inf, f(f(dot * db) - da)) * math.copysign(1.0, f(f(dot * dot) - 1.0)))
            b = f(db - f(a * dot))
            return tuple(f(f(a * na[i]) + f(b * nb[i])) for i in range(3))

        def length(o):
            l_sq = f(f(f(o[0] * o[0]) + f(o[1] * o[1])) + f(o[2] * o[2]))
            ln = f(math.sqrt(l_sq)) if l_sq >= 0.0 else math.nan
            if ln > d0 or ln >= r:
                return None
            return f(r_sq - l_sq)

        def face(d):
            if d > d0:
                return None
            return f(r_sq - f(d * d))

        def depth(sqrt_input):
            if sqrt_input is None:
                return None
            root = f(math.sqrt(sqrt_input)) if sqrt_input >= 0.0 else math.nan
            value = f(root - d0)
            if value < 0.0:
                return None
            return value

        if d1 > d2:
            edge = 1 if d1 > d3 else 3
        else:
            edge = 2 if d2 > d3 else 3

        if edge == 1:
            if d1 <= 0.0:
                return pen
            if d2 > d3:
                dot12 = dot3(n1, n2)
                if not (f(dot12 * d1) > d2):
                    return depth(length(corner(dot12, d1, d2, n1, n2)))
                return depth(face(d1))
            dot_o = dot3(n1, n3)
            if not (f(dot_o * d1) > d3):
                return depth(length(corner(dot_o, d3, d1, n3, n1)))
            return depth(face(d1))
        if edge == 2:
            if d2 <= 0.0:
                return pen
            if d3 > d1:
                dot_o = dot3(n2, n3)
                if not (f(dot_o * d2) > d3):
                    return depth(length(corner(dot_o, d2, d3, n2, n3)))
                return depth(face(d2))
            dot12 = dot3(n2, n1)
            if not (f(dot12 * d2) > d1):
                return depth(length(corner(dot12, d1, d2, n1, n2)))
            return depth(face(d2))
        if d3 <= 0.0:
            return pen
        if d1 > d2:
            dot_o = dot3(n3, n1)
            if not (f(dot_o * d3) > d1):
                return depth(length(corner(dot_o, d3, d1, n3, n1)))
            return depth(face(d3))
        dot_o = dot3(n3, n2)
        if not (f(dot_o * d3) > d2):
            return depth(length(corner(dot_o, d2, d3, n2, n3)))
        return depth(face(d3))

    def search_sphere(self, c, r):
        mn = [f(c[i] - r) for i in range(3)]
        mx = [f(c[i] + r) for i in range(3)]
        low = []
        for i in range(3):
            v = trunc_s32(f(mn[i] - self.area[i]))
            low.append(0 if s32(v) < 0 else v)
        high = []
        for i in range(3):
            v = trunc_s32(f(mx[i] - self.area[i]))
            limit = ~self.mask[i] & 0xFFFFFFFF
            high.append(limit if s32(v) > s32(limit) else v)
        if any(s32(low[i]) >= s32(high[i]) for i in range(3)):
            return 0, []
        count, hits = 0, []
        cand = prev = None
        z = low[2]
        while True:
            z_step = 1000000
            y = low[1]
            while True:
                y_step = 1000000
                longest_y = 0
                x = low[0]
                while True:
                    lst, level = self.leaf(x, y, z)
                    size = 1 << level
                    mask = size - 1
                    rem_z = size - (z & mask)
                    rem_x = size - (x & mask)
                    rem_y = size - (y & mask)
                    z_step = min(z_step, rem_z)
                    y_step = min(y_step, rem_y)
                    if rem_y > longest_y and self.u16_at(lst + 2) != 0:
                        longest_y = rem_y
                        cand = lst
                    if lst != prev:
                        for idx in self.prism_list(lst):
                            pr = self.prisms[idx]
                            pen = self.sphere_tri(pr, c, r)
                            if pen is not None:
                                count += 1
                                if len(hits) < MAX_HITS:
                                    hits.append(self.hit(pr, pen))
                    x = (x + rem_x) & 0xFFFFFFFF
                    if not x <= high[0]:
                        break
                y = (y + y_step) & 0xFFFFFFFF
                prev = cand
                if not y <= high[1]:
                    break
            z = (z + z_step) & 0xFFFFFFFF
            if not z <= high[2]:
                break
        return count, hits


def query_point(kcl, pr):
    """The harness's point inside the prism, in double, then f32."""
    a = kcl.pos[pr["positionIdx"]]
    n0, n1 = kcl.nrm[pr["normal0Idx"]], kcl.nrm[pr["normal1Idx"]]
    n2, n3 = kcl.nrm[pr["normal2Idx"]], kcl.nrm[pr["normal3Idx"]]

    def cross(u, v):
        return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    height = pr["height"]
    cross_a, cross_b = cross(n1, n0), cross(n2, n0)
    d_a, d_b = dot(cross_a, n3), dot(cross_b, n3)
    if d_a == 0.0 or d_b == 0.0:
        return None
    s_b, s_a = height / d_b, height / d_a
    b = tuple(a[i] + cross_b[i] * s_b for i in range(3))
    c = tuple(a[i] + cross_a[i] * s_a for i in range(3))
    half = kcl.thickness * 0.5
    g = tuple((a[i] + b[i] + c[i]) / 3.0 - n0[i] * half for i in range(3))
    ab = tuple(b[i] - a[i] for i in range(3))
    try:
        radius = math.sqrt(dot(ab, ab)) * 0.25
    except ValueError:
        return None
    if not all(math.isfinite(v) for v in g) or not math.isfinite(radius) or radius == 0.0:
        return None
    return tuple(f(v) for v in g), f(radius)


def kcl_lines(path, data):
    parsed = fmt_collision.parse_kcl(data)
    h = parsed["header"]
    lines = [
        "\t".join(["KH", path, str(h["pPositions"]), str(h["pNormals"]), str(h["pPrisms"]),
                   str(h["pBlocks"]), bits(h["thickness"]), bits(h["areaMin"]["x"]),
                   bits(h["areaMin"]["y"]), bits(h["areaMin"]["z"]), "%08x" % h["maskX"],
                   "%08x" % h["maskY"], "%08x" % h["maskZ"], str(h["shift"]), str(h["shiftY"]),
                   str(h["shiftZ"]), data[0x38:0x3C].hex()]),
        f"KF\t{path}\t1\t1",
    ]
    kcl = Kcl(data, parsed)
    for i, pr in enumerate(kcl.prisms):
        if i == 0:
            continue
        a = kcl.pos[pr["positionIdx"]]
        cols = ["KP", path, str(i), bits(pr["height"]), str(pr["positionIdx"]),
                str(pr["normal0Idx"]), str(pr["normal1Idx"]), str(pr["normal2Idx"]),
                str(pr["normal3Idx"]), str(pr["attribute"]), ",".join(bits(v) for v in a)]
        for key in ("normal0Idx", "normal1Idx", "normal2Idx", "normal3Idx"):
            cols.append(",".join(bits(v) for v in kcl.nrm[pr[key]]))
        lines.append("\t".join(cols))

        q = query_point(kcl, pr)
        if q is None:
            continue
        point, radius = q
        hits = kcl.check_point(point)
        lines.append("\t".join(["KQ", path, str(i), ",".join(bits(v) for v in point),
                                str(len(hits))] + hits[:MAX_HITS]))
        count, hits = kcl.search_sphere(point, radius)
        lines.append("\t".join(["KS", path, str(i), bits(radius), str(count)] + hits))
    return lines


# --------------------------------------------------------------------------
# PMP
# --------------------------------------------------------------------------
def runs(orig, now, tag, path):
    out, i = [], 0
    while i < len(orig):
        if orig[i] == now[i]:
            i += 1
            continue
        j = i
        while j < len(orig) and orig[j] != now[j]:
            j += 1
        out.append(f"{tag}\t{path}\t{i}\t{bytes(now[i:j]).hex()}")
        i = j
    return out


def calc_rpy(m):
    """EGG::Matrix34f::calcRPY (m is [row][col])."""
    m00, m10, m20, m21, m22 = m[0][0], m[1][0], m[2][0], m[2][1], m[2][2]
    a20 = abs(m20)
    if a20 > f(0.999999):
        return (0.0, (m20 / a20) * -(math.pi / 2), math.atan2(-m[0][1], -m20 * m[0][2]))
    return (math.atan2(m21, m22), math.asin(-m20), math.atan2(m10, m00))


def pmp_lines(path, data, base):
    parsed = fmt_collision.parse_pmp(data)
    h = parsed["header"]
    entries = parsed["sections"]["entries"]["entries"]
    n_entries, n_paths, n_points = h["numEntries"], h["numPaths"], h["numPoints"]
    flags = h["flags"] | 1
    paths_raw = (base + h["pPaths"]) if n_paths else 0
    points_raw = (base + h["pPoints"]) if n_points else 0
    lines = [f"PH\t{path}\t{n_entries}\t{n_paths}\t{n_points}\t{flags}\t"
             f"{h['pEntries'] if n_entries else -1}\t{paths_raw:08x}\t{points_raw:08x}"]

    now = bytearray(data)
    struct.pack_into(">H", now, 8, flags)
    struct.pack_into(">I", now, 0x40, (base + h["pEntries"]) if n_entries else 0)
    struct.pack_into(">I", now, 0x44, paths_raw)
    struct.pack_into(">I", now, 0x48, points_raw)
    # Relocate fixes the path slot of the entries RPResFile's filter (0)
    # returns: those with type byte 0.
    filtered = [i for i, e in enumerate(entries) if e["type"] == 0]
    for i in filtered:
        slot = h["pEntries"] + i * 0x58 + 0x44
        p = entries[i]["pPath"]
        struct.pack_into(">I", now, slot, 0 if p is None else base + h["pPaths"] + p * 0x20)
    for k, p in enumerate(parsed["sections"]["paths"]["entries"]):
        struct.pack_into(">I", now, h["pPaths"] + k * 0x20 + 0x18,
                         base + h["pPoints"] + p["pPoints"] * 0x14)
    lines += runs(data, now, "PW", path)

    rot = {}
    for t in COW_TYPES:
        found = [i for i in filtered if entries[i]["unk00"] == t]
        lines.append(f"PC\t{path}\t{t:x}\t{len(found)}")
        for n, i in enumerate(found):
            lines.append(f"PN\t{path}\t{t:x}\t{n}\t{i}")
            e = h["pEntries"] + i * 0x58
            fl = [be_f(data, e + 4 * k) for k in range(0x11)]
            params = []
            for v in entries[i]["params"]:
                v = v - 0x10000 if v & 0x8000 else v
                params.append(str(v if v != 0 else -12345))
            lines.append("\t".join(["PO", path, str(i), ",".join(bits(fl[k]) for k in (2, 3, 4)),
                                    ",".join(bits(fl[k]) for k in (8, 9, 10)),
                                    bits(f(1.0 * fl[5]))] + params))
            mtx = [[fl[0x0E], fl[0x0B], fl[0x08]], [fl[0x0F], fl[0x0C], fl[0x09]],
                   [fl[0x10], fl[0x0D], fl[0x0A]]]
            rot[(path, i)] = calc_rpy(mtx)
    return lines, rot


def angle_close(a, b, tol=1e-5):
    d = (a - b + math.pi) % (2 * math.pi) - math.pi
    return abs(d) <= tol


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    got = [line for line in run.stdout.splitlines() if not line.startswith(("PR\t", "PB\t"))]
    bases = {}
    got_rot = {}
    for line in run.stdout.splitlines():
        cols = line.split("\t")
        if cols[0] == "PB":
            bases[cols[1]] = int(cols[2], 16)
        elif cols[0] == "PR":
            got_rot[(cols[1], int(cols[2]))] = tuple(float(v) for v in cols[3:6])

    expected, exp_rot = [], {}
    counts = collections.Counter()
    kcl_sizes = collections.Counter()
    notes = []
    for path, data in wiiarc.walk(args.disc):
        if path.endswith(".kcl"):
            counts["KCL"] += 1
            kcl_sizes[struct.unpack_from(">I", data, 0)[0]] += 1
            expected += kcl_lines(path, data)
        elif path.endswith(".pmp"):
            counts["PMP"] += 1
            base = bases.get(path)
            if base is None or not (0x80000000 <= base < 0x81800000
                                    or 0x90000000 <= base < 0x94000000):
                notes.append(f"PMP {path}: base address {base} is not in MEM1/MEM2")
                continue
            lines, rot = pmp_lines(path, data, base)
            expected += lines
            exp_rot.update(rot)
        elif data[:4] == fmt_collision.PMP_MAGIC or fmt_collision._is_kcl(data):
            notes.append(f"{path}: KCL/PMP data without the extension")
    expected.append("PS\tsynthetic\t1")

    rot_bad = [k for k in exp_rot if k not in got_rot
               or not all(angle_close(a, b) for a, b in zip(exp_rot[k], got_rot[k]))]
    rot_bad += [k for k in got_rot if k not in exp_rot]

    kinds = ("KH", "KF", "KP", "KQ", "KS", "PH", "PW", "PC", "PN", "PO", "PS")
    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print(f"files: KCL {counts['KCL']} (header 0x38: {kcl_sizes[0x38]}, 0x3C: {kcl_sizes[0x3C]}),"
          f" PMP {counts['PMP']}")
    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}" for k in kinds)
          + f", PR {len(got_rot)}/{len(exp_rot)} (harness/python)")
    print(f"missing {len(missing)}, extra {len(extra)}, rotations off {len(rot_bad)}")
    for line in notes:
        print("  note:", line)
    for line in missing[:10]:
        print("  missing:", line[:300])
    for line in extra[:10]:
        print("  extra:  ", line[:300])
    for k in rot_bad[:5]:
        print("  rotation:", k, exp_rot.get(k), got_rot.get(k))

    ok = (run.returncode == 0 and not missing and not extra and not rot_bad and not notes
          and counts["KCL"] > 0 and counts["PMP"] > 0)
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
