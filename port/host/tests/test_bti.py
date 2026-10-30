#!/usr/bin/env python3
"""Compare bti_harness (every .bti through the game's RPGrpTexture /
EGG::GXUtility::getTexObj on the host) with the bytes.

Runs port/host/build/*/bti_harness on the extracted disc and builds the same
lines (see the harness header):

  BT  every .bti (fmt_text_font.parse_bti): the image offset and the GXTexObj
      words that GXInitTexObj / GXInitTexObjLOD store for the header's values,
      as getTexObj passes them (LODs / 8, bias / 100)
  BF  RPTnkFootmarkTex::Load's 11 textures, in the game's order

Every image is also decoded with port/assets/tools/gxtex.py at the offset,
size and format the harness's GXTexObj holds.

The GXTexObj model (gx_tex_obj) follows src/revolution/GX/GXTexture.c, as
port/host/src/host_gxtexobj.cpp does; test_rfl.py uses it too.

    python3 test_bti.py --harness build/host64/bti_harness --disc DISC
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
import fmt_text_font  # noqa: E402
import gxtex  # noqa: E402
import wiiarc  # noqa: E402

GX2HW_FILT = (0, 4, 1, 5, 2, 6, 0, 0)
CI_FORMATS = (8, 9, 10)
TNK_NAMES = ["tnk_footmark/tnk_mark_1p.bti", "tnk_footmark/tnk_mark_2p.bti"] + [
    f"tnk_footmark/tnk_mark_e_{n}.bti" for n in range(1, 10)]


def f32(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


def to_u8_wii(f):
    """static_cast<u8>(static_cast<s32>(f)): truncate, then keep the low byte."""
    return int(f) & 0xFF


def bitset(field, pos, size, value):
    shift = 32 - pos - size
    mask = ((1 << size) - 1) << shift
    return (field & ~mask & 0xFFFFFFFF) | ((value << shift) & mask)


def gx_tex_obj(width, height, fmt, wrap_s, wrap_t, mipmap, min_filt, mag_filt, min_lod,
               max_lod, lod_bias, bias_clamp, edge_lod, aniso):
    """GXInitTexObj then GXInitTexObjLOD: (mode0, mode1, image0, fmt, loadCnt, loadFmt, flags)."""
    mode0 = bitset(0, 30, 2, wrap_s)
    mode0 = bitset(mode0, 28, 2, wrap_t)
    mode0 = bitset(mode0, 27, 1, 1)
    mode1 = 0
    flags = 0
    if mipmap:
        flags |= 1
        mode0 = bitset(mode0, 24, 3, 5 if fmt in CI_FORMATS else 6)
        mode1 = bitset(mode1, 16, 8, to_u8_wii(16.0 * int(math.log2(max(width, height)))))
    else:
        mode0 = bitset(mode0, 24, 3, 4)
    image0 = bitset(0, 22, 10, width - 1)
    image0 = bitset(image0, 12, 10, height - 1)
    image0 = bitset(image0, 8, 4, fmt & 0xF)
    load_fmt, row_t, col_t = {
        0: (1, 3, 3), 8: (1, 3, 3),
        1: (2, 3, 2), 2: (2, 3, 2), 9: (2, 3, 2),
        3: (2, 2, 2), 10: (2, 2, 2), 4: (2, 2, 2), 5: (2, 2, 2),
        6: (3, 2, 2), 14: (0, 3, 3),
    }.get(fmt & 0xF, (2, 2, 2))
    load_cnt = (((width + (1 << row_t) - 1) >> row_t) * ((height + (1 << col_t) - 1) >> col_t)) & 0x7FFF
    flags |= 2

    lod_bias = f32(lod_bias)
    if lod_bias < -4.0:
        lod_bias = -4.0
    elif lod_bias >= 4.0:
        lod_bias = f32(3.99)
    mode0 = bitset(mode0, 15, 8, to_u8_wii(32.0 * lod_bias))
    mode0 = bitset(mode0, 27, 1, 1 if mag_filt == 1 else 0)
    mode0 = bitset(mode0, 24, 3, GX2HW_FILT[min_filt])
    mode0 = bitset(mode0, 23, 1, 0 if edge_lod else 1)
    mode0 = bitset(mode0, 14, 1, 0)
    mode0 = bitset(mode0, 13, 1, 0)
    mode0 = bitset(mode0, 11, 2, aniso)
    mode0 = bitset(mode0, 10, 1, bias_clamp)
    min_lod = min(max(min_lod, 0.0), 10.0)
    max_lod = min(max(max_lod, 0.0), 10.0)
    mode1 = bitset(mode1, 24, 8, to_u8_wii(16.0 * min_lod))
    mode1 = bitset(mode1, 16, 8, to_u8_wii(16.0 * max_lod))
    return mode0, mode1, image0, fmt, load_cnt, load_fmt, flags


def tex_obj_cols(words):
    mode0, mode1, image0, fmt, load_cnt, load_fmt, flags = words
    return f"{mode0:08x}\t{mode1:08x}\t{image0:08x}\t{fmt:08x}\t{load_cnt:04x}\t{load_fmt:02x}\t{flags:02x}"


def bti_words(h):
    """EGG::GXUtility::getTexObj's calls for a BTI header."""
    return gx_tex_obj(h["width"], h["height"], h["format"], h["wrapS"], h["wrapT"], h["mipMap"] > 0,
                      h["minFilter"], h["magFilter"], h["minLOD"] / 8.0, h["maxLOD"] / 8.0,
                      f32(h["lodbias"] / 100.0), h["biasClampEnable"], h["edgeLODEnable"],
                      h["anisotropy"])


def decode_check(data, offset, words):
    """Decode the image the GXTexObj describes; returns the RGBA byte count."""
    image0 = words[2]
    width, height = (image0 & 0x3FF) + 1, ((image0 >> 10) & 0x3FF) + 1
    fmt = (image0 >> 20) & 0xF
    size = gxtex.image_size(gxtex._normalize_format(fmt, gxtex.FORMATS, "format"), width, height)
    rgba = gxtex.decode(fmt, data[offset:offset + size], width, height)
    if len(rgba) != width * height * 4:
        raise ValueError("decoded size")
    return len(rgba)


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
    decoded = 0
    tnk = {}
    problems = []
    for path, data in wiiarc.walk(args.disc):
        if not path.endswith(".bti"):
            continue
        bti = fmt_text_font.parse_bti(data)
        words = bti_words(bti["header"])
        expected.append(f"BT\t{path}\t{bti['imageOffsetResolved']:x}\t{tex_obj_cols(words)}")
        try:
            decoded += decode_check(data, bti["imageOffsetResolved"], words)
        except ValueError as e:
            problems.append(f"{path}: decode failed: {e}")
        if path.startswith("Common/RPTnkScene/common.carc/"):
            tnk[path.split("/", 3)[3]] = words[2]
    for i, name in enumerate(TNK_NAMES):
        if name in tnk:
            expected.append(f"BF\t{i}\t{name}\t{tnk[name]:08x}")
        else:
            problems.append(f"{name} not on the disc")

    kinds = ("BT", "BF")
    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}" for k in kinds)
          + " (harness/python)")
    print(f"decoded {decoded} RGBA bytes; missing {len(missing)}, extra {len(extra)}")
    for line in problems:
        print("  problem:", line)
    for line in missing[:10]:
        print("  missing:", line)
    for line in extra[:10]:
        print("  extra:  ", line)
    ok = run.returncode == 0 and not missing and not extra and not problems and kinds_exp["BT"] > 0
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
