#!/usr/bin/env python3
"""Compare rfl_harness (RFL_Res.dat through the real RVLFaceLib on the host)
with the bytes.

Runs port/host/build/*/rfl_harness on the extracted disc and builds the same
lines from RFL_Res.dat (see the harness header for the line kinds):

  RI, RA  the loader state after RFLInitRes: version and, per part archive,
          numFiles, biggestSize and the offset of its file table
  RF      every file of every archive: size and hash, from the offset tables
  RT      textures: the GXTexObj RFLiInitRFLTexture makes (test_bti.gx_tex_obj)
  RX      shape textures: RFLiInitTexRes's GXTexObj and image copy
  RS      shapes: RFLiInitShapeRes's vertex counts, the position / normal /
          texcoord arrays it writes (big-endian GX data) in four modes, and the
          faceline's three Vecs
  RE      the resource is unchanged

    python3 test_rfl.py --harness build/host64/rfl_harness --disc DISC
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
sys.path.insert(0, HERE)
import wiiarc  # noqa: E402
from test_bti import f32, gx_tex_obj, tex_obj_cols  # noqa: E402

# RFLiArcID order (include/RVLFaceLib/RFLi_Types.h)
ARC = {name: i for i, name in enumerate(
    "Beard Eye Eyebrow Faceline FaceTex ForeHead Glass GlassTex Hair Mask Mole Mouth Mustache "
    "Nose Nline NlineTex Cap CapTex".split())}
TEX_ARC = [ARC[n] for n in ("Eye", "Eyebrow", "Mouth", "Mustache", "Mole")]
SHPTEX_ARC = [ARC[n] for n in ("FaceTex", "CapTex", "NlineTex", "GlassTex")]
SHP_ARC = [ARC[n] for n in ("Nose", "ForeHead", "Faceline", "Hair", "Cap", "Beard", "Nline",
                            "Mask", "Glass")]
SHP_FACELINE = 2
SHP_SKIP_TXC = (0, 1, 3, 5)  # nose, forehead, hair, beard
POS_SCALE = 0.75
POS_TRANS = (1.5, -2.25, 3.0)


def fnv(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def s16(x):
    return ((x + 0x8000) & 0xFFFF) - 0x8000


def pack16(values):
    return struct.pack(f">{len(values)}h", *values)


def parse_res(data):
    version = struct.unpack_from(">H", data, 2)[0]
    arcs = []
    for i in range(len(ARC)):
        off = struct.unpack_from(">I", data, (i + 1) * 4)[0]
        num, biggest = struct.unpack_from(">HH", data, off)
        table = off + 4
        offs = struct.unpack_from(f">{num + 1}I", data, table)
        base = table + num * 4 + 4
        files = [data[base + offs[j]:base + offs[j + 1]] for j in range(num)]
        arcs.append((num, biggest, table, files))
    return version, arcs


def tex_header(tex):
    (fmt, _alpha, w, h, wrap_s, wrap_t, _idx, _cfmt, _ncol, _pal, _lod, edge, clamp, aniso,
     min_f, mag_f, min_lod, max_lod, _mip, _res, bias, image) = struct.unpack_from(
        ">BBHHBBBBHIBBBBBBbbBbhI", tex, 0)
    return dict(fmt=fmt, w=w, h=h, wrap_s=wrap_s, wrap_t=wrap_t, edge=edge, clamp=clamp,
                aniso=aniso, min_f=min_f, mag_f=mag_f, min_lod=min_lod, max_lod=max_lod,
                bias=bias, image=image)


def shape_lines(part, file, data):
    lines = []
    p = 4
    vecs = None
    if part == SHP_FACELINE:
        vecs = struct.unpack_from(">9I", data, p)
        p += 36
    num_pos = struct.unpack_from(">H", data, p)[0]
    if num_pos == 0:
        pos = nrm = txc = []
        num_nrm = num_txc = 0
    else:
        p += 2
        pos = list(struct.unpack_from(f">{num_pos * 3}h", data, p))
        p += num_pos * 6
        num_nrm = struct.unpack_from(">H", data, p)[0]
        p += 2
        nrm = list(struct.unpack_from(f">{num_nrm * 3}h", data, p))
        p += num_nrm * 6
        if part in SHP_SKIP_TXC:
            num_txc, txc = 0, []
        else:
            num_txc = struct.unpack_from(">H", data, p)[0]
            p += 2
            txc = list(struct.unpack_from(f">{num_txc * 2}h", data, p))
    s = int(f32(256.0 * f32(POS_SCALE)))
    tx, ty, tz = (int(f32(256.0 * f32(t))) for t in POS_TRANS)
    for mode in range(4):
        transform, flip = mode >= 2, mode & 1
        out_pos = []
        for i in range(0, len(pos), 3):
            x, y, z = pos[i:i + 3]
            if transform:
                x = tx + (((-x if flip else x) * s) >> 8)
                y = ty + ((y * s) >> 8)
                z = tz + ((z * s) >> 8)
            elif flip:
                x = -x
            out_pos += [s16(x), s16(y), s16(z)]
        out_nrm = []
        for i in range(0, len(nrm), 3):
            x, y, z = nrm[i:i + 3]
            out_nrm += [s16(-x if flip else x), y, z]
        line = (f"RS\t{part}\t{file}\t{mode}\t{num_pos:x}\t{num_nrm:x}\t{num_txc:x}\t"
                f"{fnv(pack16(out_pos)):08x}\t{fnv(pack16(out_nrm)):08x}\t{fnv(pack16(txc)):08x}")
        if vecs is not None:
            line += "".join(f"\t{v:08x}" for v in vecs)
        lines.append(line)
    return lines


def expected_lines(data):
    version, arcs = parse_res(data)
    lines = [f"RI\t0\t0\t{version:x}\t1"]
    for i, (num, biggest, table, _files) in enumerate(arcs):
        lines.append(f"RA\t{i}\t{num:x}\t{biggest:x}\t{table:x}")
    for part, arc in enumerate(TEX_ARC):
        for j, f in enumerate(arcs[arc][3]):
            lines.append(f"RF\tT\t{part}\t{j}\t{len(f):x}\t{fnv(f):08x}")
            h = tex_header(f)
            words = gx_tex_obj(h["w"], h["h"], h["fmt"], 0, 0, False, h["min_f"], h["mag_f"],
                               float(h["min_lod"]), float(h["max_lod"]), float(h["bias"]),
                               h["clamp"], h["edge"], h["aniso"])
            lines.append(f"RT\t{part}\t{j}\t{h['image']:x}\t{tex_obj_cols(words)}")
    for part, arc in enumerate(SHPTEX_ARC):
        for j, f in enumerate(arcs[arc][3]):
            lines.append(f"RF\tS\t{part}\t{j}\t{len(f):x}\t{fnv(f):08x}")
            h = tex_header(f)
            size = {0: h["w"] * h["h"] * 2, 3: h["w"] * h["h"]}.get(part, h["w"] * h["h"] // 2)
            words = gx_tex_obj(h["w"], h["h"], h["fmt"], h["wrap_s"], h["wrap_t"], False, 1, 1,
                               0.0, 0.0, 0.0, 0, 0, 0)
            image = f[h["image"]:h["image"] + size]
            lines.append(f"RX\t{part}\t{j}\t0\t{tex_obj_cols(words)}\t{fnv(image):08x}")
    for part, arc in enumerate(SHP_ARC):
        for j, f in enumerate(arcs[arc][3]):
            lines.append(f"RF\tP\t{part}\t{j}\t{len(f):x}\t{fnv(f):08x}")
            lines += shape_lines(part, j, f)
    lines.append("RE\t1")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    got = run.stdout.splitlines()

    expected = []
    found = 0
    for path, data in wiiarc.walk(args.disc):
        if path.endswith("RFL_Res.dat"):
            found += 1
            expected += expected_lines(data)

    kinds = ("RI", "RA", "RF", "RT", "RX", "RS", "RE")
    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print(f"files: RFL_Res.dat {found}")
    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}" for k in kinds)
          + " (harness/python)")
    print(f"missing {len(missing)}, extra {len(extra)}")
    for line in missing[:10]:
        print("  missing:", line)
    for line in extra[:10]:
        print("  extra:  ", line)
    ok = run.returncode == 0 and found == 1 and not missing and not extra
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
