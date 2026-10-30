#!/usr/bin/env python3
"""Compare data_harness (EGG binary settings and game tables through the game's
code on the host) with port/assets/tools/fmt_game_data.py and the bytes.

Runs port/host/build/*/data_harness on the extracted disc and builds the same
lines (see the harness header):

  LO, LD, LA, LT  every .plight (fmt_game_data.parse_lght): the loaded lights
                  (count capped at the manager's 16) as LightObject::SetBinaryInner
                  version 1 stores them and GetBinaryInner writes them back, the
                  runtime intensity and target, the 8 ambient colours and the
                  light-texture colour
  MT, ML          every .plmap (parse_lmap): each texture's name, drawType,
                  lightMask and its lights (capped at the texture's light count)
  BL              every .pblm (parse_pblm): all BinData fields; version 0
                  files get zero gather scales
  TM              the 120 RPTnkMapData maps (parse_tnk_map header, cells from
                  the bytes)
  BQ, BW          RPBomQuestionParam: BuildLevelTable's question ranges from
                  the patterns' mQuestion, and every record word (the header
                  also against parse_bom_question_param)
  WL, PL          RPWlyLevelData and RPPnpLevelTable records
                  (parse_wly_leveltable, parse_leveltable)

Every value is compared as the hex of its 32-bit pattern (floats as bits), so a
field read in the wrong byte order or at the wrong offset shows up as a
mismatch.

    python3 test_data.py --harness build/host64/data_harness --disc DISC
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_game_data  # noqa: E402
import wiiarc  # noqa: E402

LIGHT_NUM = 16      # LightManager(16, 8, 1)
AMBIENT_NUM = 8
LIGHT_OBJ_MAX = 32  # LightTextureManager::LIGHT_OBJ_MAX, each LightTexture's mLightNum


def fb(x):
    return "%08x" % struct.unpack(">I", struct.pack(">f", x))[0]


def ib(x):
    return "%08x" % (x & 0xFFFFFFFF)


def col(c):
    return "%02x%02x%02x%02x" % tuple(c)


def words(data, off, n):
    return ["%08x" % w for w in struct.unpack_from(">%dI" % n, data, off)]


def light_lines(path, p):
    lines = []
    if p["header"]["version"] != 1:
        raise ValueError(f"{path}: only version 1 .plight files are modelled")
    for i, l in enumerate(p["lights"][:LIGHT_NUM]):
        lines.append("\t".join([
            "LO", path, str(i), str(l["BYTE_0x13"]), str(l["spotFn"]), str(l["distAttnFn"]),
            str(l["ambLightIdx"]), "%04x" % l["flags"], str(l["BYTE_0x12"]), fb(l["intensity"]),
            ",".join(fb(l["pos"][k]) for k in "xyz"), ",".join(fb(l["target"][k]) for k in "xyz"),
            col(l["color"]), col(l["COLOR_0x38"]), fb(l["spotArg"]), fb(l["distAttnArg0"]),
            fb(l["distAttnArg1"])]))
        lines.append("\t".join(["LD", path, str(i), fb(l["intensity"]),
                                ",".join(fb(l["target"][k]) for k in "xyz")]))
    for j, a in enumerate(p["ambient"][:AMBIENT_NUM]):
        lines.append(f"LA\t{path}\t{j}\t{col(a['color'])}")
    lines.append(f"LT\t{path}\t{col(p['lightTexColor'])}")
    return lines


def lmap_lines(path, p):
    lines = []
    for t in p["textures"]:
        lines.append(f"MT\t{path}\t{t['name']}\t{t['drawType']}\t{t['lightMask']:08x}")
        for k, l in enumerate(t["lights"][:LIGHT_OBJ_MAX]):
            lines.append(f"ML\t{path}\t{t['name']}\t{k}\t{fb(l['effectScale'])}\t{l['pttrn']}")
    return lines


def bloom_lines(path, p):
    blur = [f"{fb(p[k]['mRadius'])},{fb(p[k]['mIntensity'])}" for k in ("mBlur0", "mBlur1", "mBlur2")]
    return ["\t".join(["BL", path, fb(p["mThreshold"]), col(p["mMaskColor"]), col(p["mColor"]),
                       "%04x" % p["mFlags"], "%04x" % p["unkE"]] + blur
                      + [str(p["mBlendModeIdx"]), str(p["mNumBlur"]), fb(p["mGatherScale0"]),
                         fb(p["mGatherScale1"])])]


def tnk_line(path, data, p):
    h = p["header"]
    cells = struct.unpack_from(">%di" % p["cellCount"], data, 0x10)
    return "\t".join(["TM", path, str(p["width"]), "%08x" % h["unk4"], str(h["unk8"]),
                      str(h["unkC"]), ",".join(str(c) for c in cells)])


def bom_lines(path, data, p):
    lines = []
    # BuildLevelTable over the 250 patterns' mQuestion.
    start, num = [0] * 128, [0] * 128
    last = 0
    for i in range(250):
        q = struct.unpack_from(">i", data, 0x1004 + i * 0x38 + 0x18)[0]
        if q <= last:
            num[last] += 1
        else:
            last = q
            start[q] = i
            num[q] = 1
    lines.append("\t".join(["BQ", path, str(last)]
                           + [f"{q}:{start[q]}:{num[q]}" for q in range(128) if start[q] or num[q]]))
    header = words(data, 0, 15)
    named = [fb(v) if isinstance(v, float) else ib(v) for v in p["header"].values()]
    if named != header:
        raise ValueError(f"{path}: header words differ from parse_bom_question_param")
    lines.append("\t".join(["BW", path, "header", "0"] + header))
    for kind, off, size, count in (("question", 0x3C, 0x28, 101), ("pattern", 0x1004, 0x38, 250),
                                   ("level", 0x46B4, 0x44, 16)):
        for i in range(count):
            lines.append("\t".join(["BW", path, kind, str(i)] + words(data, off + i * size, size // 4)))
    lines.append("\t".join(["BW", path, "combo", "0"] + words(data, 0x4AF4, 10)))
    return lines


def record_lines(tag, path, kind, p):
    out = []
    for i, rec in enumerate(p["records"]):
        out.append("\t".join([tag, path, kind, str(i)]
                             + [fb(v) if isinstance(v, float) else ib(v) for v in rec.values()]))
    return out


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
    kinds = collections.Counter()
    versions = collections.defaultdict(collections.Counter)
    uncovered = collections.Counter()
    for path, data in wiiarc.walk(args.disc):
        kind = fmt_game_data.classify(path, data)
        if kind is None:
            continue
        p = fmt_game_data.parse(data, path=path)
        kinds[kind] += 1
        if kind == "LGHT":
            versions[kind][p["header"]["version"]] += 1
            expected += light_lines(path, p)
        elif kind == "LMAP":
            versions[kind][p["header"]["version"]] += 1
            expected += lmap_lines(path, p)
        elif kind == "PBLM":
            versions[kind][p["header"]["version"]] += 1
            expected += bloom_lines(path, p)
        elif kind == "TnkMapData":
            expected.append(tnk_line(path, data, p))
        elif kind == "BomQuestionParam":
            expected += bom_lines(path, data, p)
        elif kind.startswith("wanted_"):
            expected += record_lines("WL", path, "level", p)
        elif kind == "leveltable":
            expected += record_lines("PL", path, "level", p)
        elif kind.startswith("rallyleveluptable"):
            expected += record_lines("PL", path, "rally", p)
        else:
            uncovered[kind] += 1

    tags = ("LO", "LD", "LA", "LT", "MT", "ML", "BL", "TM", "BQ", "BW", "WL", "PL")
    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print("files: " + ", ".join(
        f"{k} {kinds[k]}" + (" (versions " + ",".join(f"{v}:{n}" for v, n in sorted(versions[k].items())) + ")"
                             if k in versions else "")
        for k in ("LGHT", "LMAP", "PBLM", "TnkMapData", "BomQuestionParam")))
    print("tables: " + ", ".join(f"{k} {n}" for k, n in sorted(kinds.items())
                                 if k not in ("LGHT", "LMAP", "PBLM", "TnkMapData", "BomQuestionParam")))
    print("not loaded by the harness: " + (", ".join(f"{k} {n}" for k, n in sorted(uncovered.items())) or "none"))
    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}" for k in tags) + " (harness/python)")
    print(f"missing {len(missing)}, extra {len(extra)}")
    for line in missing[:10]:
        print("  missing:", line[:300])
    for line in extra[:10]:
        print("  extra:  ", line[:300])

    ok = run.returncode == 0 and not missing and not extra and kinds["LGHT"] > 0
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
