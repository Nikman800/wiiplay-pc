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
  WL              RPWlyLevelTable's records (parse_wly_leveltable) after its
                  constructor in both TV modes: mSpeed * 1.3684211f on 16:9,
                  unk20 = 6.25f on field type 3; and with sUseDefaultLevel set
  PL              RPPnpLevelTable's records (parse_leveltable)
  DP, DN          the six Duc parameter files (parse_duc_param) in both TV
                  modes, with LoadParams' changes: frame counts tripled
                  (CircleMato, Duck, Ufo), Duck's jump and forward x ranges
                  * 0.75f on 4:3; DN: mBaseRadius and the patterns' hit scales
  TP              TnkGameParam (parse_tnk_game_param): the variant
                  PickStageVariant picks for each stage in both modes, from the
                  stage records' range and the Tnk random generator

Every value is compared as the hex of its 32-bit pattern (floats as bits), so a
field read in the wrong byte order or at the wrong offset shows up as a
mismatch.

    python3 test_data.py --harness build/host64/data_harness --disc DISC
"""
import argparse
import collections
import os
import re
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


def f32(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


WLY_WIDE_SPEED = f32(1.3684211)  # RPWlyLevelTable::RPWlyLevelTable
WLY_FIELD3_UNK20 = 6.25          # Wly_fieldType3Unk20 (RPWlyAvatarMgr_2)
WLY_DEFAULT = dict(zip(          # RPWlyLevelTable::sDefaultLevel
    ("mAvatarNum", "unk4", "mQuestionType", "mFieldType", "unk10", "unk14", "mCameraTarget",
     "mSpeed", "unk20", "mSpotlight", "mFoundScale", "mSpotPos.x", "mSpotPos.y", "mSpotPos.z",
     "mPlaneY", "mPlaneZ"),
    (8, 0, 1, 0, -130.0, -130.0, -27.0, 8.0, 15.0, 0.0, 1.5, 1.0, 1.0, 1.0, 0.0, 0.0)))


def wly_lines(path, p):
    out = []
    for default in (0, 1):
        for tv in (0, 1):
            kind = ("default-" if default else "") + f"tv{tv}"
            for i, rec in enumerate(p["records"]):
                rec = dict(WLY_DEFAULT if default else rec)
                if tv == 1:  # EGG::Screen::TV_MODE_WIDE
                    rec["mSpeed"] = f32(rec["mSpeed"] * WLY_WIDE_SPEED)
                if rec["mFieldType"] == 3:
                    rec["unk20"] = WLY_FIELD3_UNK20
                out.append("\t".join(["WL", path, kind, str(i)]
                                      + [fb(v) if isinstance(v, float) else ib(v) for v in rec.values()]))
    return out


# Members RPDuc*::LoadParams multiplies by 3 (frame counts; three sub-steps per frame).
DUC_TRIPLED = {
    "DucCircleMatoParam": r"mLayout\.mPatterns\[\d\]\.(mGrowFrames|mStandFrames\[\d\])",
    "DucDuckParam": r"mPatterns\[\d\]\.mBounceFrames|mDamageFrames",
    "DucUfoParam": r"mPatterns\[\d\]\.(mWait2Frames|mWaitFrames|mWanderFrames|mCircleTimeMin"
                   r"|mCircleTimeMax|mCircleFrames|mAbductFrames|mLiftFrames[01]|mHoldFrames)",
}
# RPDucDuck::LoadParams on 4:3 (EGG::Screen TV mode 0): * 0.75f (lbl_8043FE04).
DUC_STD_SCALED = {"DucDuckParam": r"mPatterns\[\d\]\.mJumpRangeX|mForwardRangeX"}


def duc_lines(path, name, p):
    out = []
    types = dict(fmt_game_data.DUC_PARAM_FIELDS[name])
    for tv in (0, 1):
        fields = dict(p["fields"])
        for field in fields:
            if name in DUC_TRIPLED and re.fullmatch(DUC_TRIPLED[name], field):
                fields[field] = (fields[field] * 3 + 2**31) % 2**32 - 2**31
            if tv == 0 and name in DUC_STD_SCALED and re.fullmatch(DUC_STD_SCALED[name], field):
                fields[field] = f32(fields[field] * f32(0.75))
        out.append("\t".join(["DP", path, str(tv)]
                              + [fb(v) if types[k] == "f32" else ib(v) for k, v in fields.items()]))
        if name == "DucCircleMatoParam":
            scales = [fields[f"mLayout.mPatterns[{i}].mHitScale[{k}]"] for i in range(8) for k in range(2)]
        else:
            scales = [fields[f"mPatterns[{i}].mHitScale"] for i in range(8)]
        out.append("\t".join(["DN", path, str(tv), fb(fields["mBaseRadius"]),
                               ",".join(fb(v) for v in scales)]))
    return out


def tnk_param_lines(path, p):
    # RPTnkGameManager::PickStageVariant with TnkGameRandom; the harness seeds
    # RP_randSeedX/Y and sets mModeIdx and unk14 (-1) as ResetForMode does.
    out = []
    for players in (1, 2):
        mode = players - 1
        x, y = 0x12345678, 0x9ABCDEF1
        last = -1
        picks = []
        for stage in p["stages"]:
            first, final = stage[f"variants[{mode}][0]"], stage[f"variants[{mode}][1]"]
            if first == final:
                pick = first
            else:
                while True:
                    x = (x * 0x41C64E6D + 12345) & 0xFFFFFFFF
                    if y & 1:
                        y ^= 0x11020
                    y >>= 1
                    pick = ((x ^ y) % ((final - first + 1) & 0xFFFFFFFF)) + first
                    if pick != last:
                        break
            last = pick
            picks.append(str(pick))
        out.append("\t".join(["TP", path, str(players), ",".join(picks)]))
    return out


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
            expected += wly_lines(path, p)
        elif kind.startswith("Duc"):
            expected += duc_lines(path, kind, p)
        elif kind == "TnkGameParam":
            expected += tnk_param_lines(path, p)
        elif kind == "leveltable":
            expected += record_lines("PL", path, "level", p)
        elif kind.startswith("rallyleveluptable"):
            expected += record_lines("PL", path, "rally", p)
        else:
            uncovered[kind] += 1

    tags = ("LO", "LD", "LA", "LT", "MT", "ML", "BL", "TM", "BQ", "BW", "WL", "PL", "DP", "DN", "TP")
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
