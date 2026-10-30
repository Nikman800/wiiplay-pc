#!/usr/bin/env python3
"""Compare g3d_harness (decompiled nw4r::g3d resource code on the host) with
port/assets/tools/fmt_brres_model.py and a minimal animation reader.

Runs port/host/build/*/g3d_harness on the extracted disc and builds the same
lines independently:

  F  every BRRES: root group count, CheckRevision, whether ResFile::Bind on
     itself succeeded, and whether everything was bound in the end (after
     binding to the other BRRES of the same archive)
  G  every root group: name, entry count, entry names (dictionary order)
  M  every MDL0: name, revision, material, shape, node, vertex position,
     normal, colour, texture coordinate and TEV counts, texture-name info
     count, numViewMtx, scaling rule
  T  every TEX0: name, revision, width, height, format, mipmap levels,
     CI flag, image offset, min/max LOD
  P  every PLT0: name, revision, format, entry count
  A  every animation (CHR0, VIS0, CLR0, PAT0, SRT0, SHP0, SCN0): kind, name,
     revision, frame count, policy, node/material/vertex-name/light-set
     count, and the entry count of its data dictionary

Models, textures and palettes come from fmt_brres_model.parse (read-only).
Animations are read here from the header layouts (the animation parser in
port/assets/tools is not finished). The bind expectations follow the rules of
ResTexPlttInfo::Bind and ResAnmTexPat::Bind written out from the format:
a model binds when every name in its texture/palette name dictionaries is a
texture/palette of the file, and ResFile::Bind returns the result of the last
PAT0 when there is one (its documented @bug).

    python3 test_models.py --harness build/host64/g3d_harness --disc DISC
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_brres_model  # noqa: E402  (read-only reference)
import wiiarc  # noqa: E402

REVISIONS = {
    "MDL0": 9, "TEX0": 1, "PLT0": 1, "CHR0": 4, "VIS0": 3, "CLR0": 3,
    "PAT0": 3, "SRT0": 4, "SHP0": 3, "SCN0": 4,
}

# Per animation kind: (offset of the data dictionary offset, of the name
# offset, of numFrame, of the count printed, of the policy). From the
# Res*Data structs in include/nw4r/g3d/res.
ANIM_LAYOUT = {
    "CHR0": (0x10, 0x14, 0x1C, 0x1E, 0x20),
    "VIS0": (0x10, 0x14, 0x1C, 0x1E, 0x20),
    "CLR0": (0x10, 0x14, 0x1C, 0x1E, 0x20),
    "SRT0": (0x10, 0x14, 0x1C, 0x1E, 0x24),
    "PAT0": (0x10, 0x24, 0x2C, 0x2E, 0x34),
    "SHP0": (0x10, 0x18, 0x20, 0x22, 0x24),
    "SCN0": (0x10, 0x28, 0x30, None, 0x34),
}


def u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def s32(d, o):
    return struct.unpack_from(">i", d, o)[0]


def cstr(d, o):
    return d[o:d.index(b"\0", o)].decode("ascii")


def dic_names(d, o):
    """Names of a ResDic at o (entries 1..numData, in order)."""
    n = u32(d, o + 4)
    names = []
    for i in range(1, n + 1):
        ofs = s32(d, o + 8 + 16 * i + 8)
        names.append(cstr(d, o + ofs) if ofs else "")
    return names


def dic_entry(d, o, name):
    n = u32(d, o + 4)
    for i in range(1, n + 1):
        e = o + 8 + 16 * i
        if s32(d, e + 8) and cstr(d, o + s32(d, e + 8)) == name:
            return o + s32(d, e + 12)
    return None


def walk(root):
    """(path, bytes, archive path) for every non-archive file, as wiiarc.walk."""
    def expand(path, data, archive):
        data = wiiarc.unpack(data)
        if data[:4] == wiiarc.U8_MAGIC:
            for inner, blob in wiiarc.u8(data):
                yield from expand(path + "/" + inner, blob, path)
        else:
            yield path, data, archive

    for dirpath, _dirs, names in os.walk(root):
        for n in sorted(names):
            full = os.path.join(dirpath, n)
            with open(full, "rb") as f:
                yield from expand(os.path.relpath(full, root), f.read(), None)


def g(x):
    s = "%g" % x
    return s


def anim_line(path, kind, d, off):
    dic, name, frame, count, policy = ANIM_LAYOUT[kind]
    dic_off = off + s32(d, off + dic)
    if kind == "SCN0":
        ls = dic_entry(d, dic_off, "LightSet(NW4R)")
        cnt = u32(d, ls + 4) if ls is not None else 0
    else:
        cnt = u16(d, off + count)
    return "A\t%s\t%s\t%s\t%u\t%d\t%u\t%d\t%u" % (
        path, kind, cstr(d, off + s32(d, off + name)), u32(d, off + 8),
        u16(d, off + frame), u32(d, off + policy), cnt, u32(d, dic_off + 4))


def pat0_names(d, off):
    """Texture and palette names of a PAT0 (offsets relative to each array)."""
    out = []
    for arr, num in ((0x14, 0x30), (0x18, 0x32)):
        base = off + s32(d, off + arr)
        out.append([cstr(d, base + s32(d, base + 4 * i)) for i in range(u16(d, off + num))])
    return out


def expected_lines(disc):
    lines = []
    files = []  # (path, archive, parsed, data)
    for path, data, archive in walk(disc):
        if data[:4] != fmt_brres_model.BRRES_MAGIC:
            continue
        files.append((path, archive, fmt_brres_model.parse(data), data))

    tex_by_archive = collections.defaultdict(set)
    pltt_by_archive = collections.defaultdict(set)
    for path, archive, parsed, data in files:
        for sub in parsed["subfiles"]:
            if sub["dictionaryName"] == "Textures(NW4R)":
                tex_by_archive[archive].add(sub["resourceName"])
            elif sub["dictionaryName"] == "Palettes(NW4R)":
                pltt_by_archive[archive].add(sub["resourceName"])

    for path, archive, parsed, data in files:
        root = parsed["root"]
        groups = root["dictionary"]["data"][1:]
        tex = set()
        pltt = set()
        for node, group in zip(groups, root["groups"]):
            names = [n["name"] for n in group["data"][1:]]
            lines.append("G\t%s\t%s\t%u\t%s" % (path, node["name"], len(names), ",".join(names)))
            if node["name"] == "Textures(NW4R)":
                tex = set(names)
            elif node["name"] == "Palettes(NW4R)":
                pltt = set(names)

        rev_ok = True
        self_ok = final_ok = True
        pat_result = None
        for sub in parsed["subfiles"]:
            kind = sub["header"]["kind"].decode("ascii")
            off = sub["offset"]
            if u32(data, off + 8) != REVISIONS[kind]:
                rev_ok = False
            if kind == "MDL0":
                sec = sub["sections"]

                def cnt(key):
                    return sec[key]["numData"] if sec[key] is not None else 0

                def names(key):
                    return [n["name"] for n in sec[key]["data"][1:]] if sec[key] else []

                lines.append("M\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u" % (
                    path, sub["name"], sub["revision"], cnt("toResMatDic"), cnt("toResShpDic"),
                    cnt("toResNodeDic"), cnt("toResVtxPosDic"), cnt("toResVtxNrmDic"),
                    cnt("toResVtxClrDic"), cnt("toResVtxTexCoordDic"), cnt("toResTevDic"),
                    cnt("toResTexNameToTexPlttInfoDic"), sub["info"]["numViewMtx"],
                    sub["info"]["scaling_rule"]))
                tn, pn = names("toResTexNameToTexPlttInfoDic"), names("toResPlttNameToTexPlttInfoDic")
                if not (set(tn) <= tex and set(pn) <= pltt):
                    self_ok = False
                if not (set(tn) <= tex_by_archive[archive] and set(pn) <= pltt_by_archive[archive]):
                    final_ok = False
            elif kind == "TEX0":
                lines.append("T\t%s\t%s\t%u\t%u\t%u\t0x%x\t%u\t%d\t%u\t%s\t%s" % (
                    path, sub["name"], sub["revision"], sub["width"], sub["height"], sub["fmt"],
                    sub["mipmap_level"], sub["flag"] & 1, sub["toTexData"],
                    g(sub["min_lod"]), g(struct.unpack_from(">f", data, off + 0x2C)[0])))
                # fmt_brres_model.py unpacks TEX0 with ">IiiIIHHIIfFii": the "F"
                # (a complex number) makes its max_lod, original_path and
                # toResUserData wrong, so max_lod is read here from +0x2C.
            elif kind == "PLT0":
                lines.append("P\t%s\t%s\t%u\t0x%x\t%u" % (
                    path, sub["name"], sub["revision"], sub["fmt"], sub["numEntries"]))
            elif kind in ANIM_LAYOUT:
                lines.append(anim_line(path, kind, data, off))
                if kind == "PAT0":
                    tn, pn = pat0_names(data, off)
                    pat_result = set(tn) <= tex and set(pn) <= pltt
                    if not (set(tn) <= tex_by_archive[archive] and
                            set(pn) <= pltt_by_archive[archive]):
                        final_ok = False
            else:
                raise ValueError(f"{path}: unexpected subfile {kind}")
        if pat_result is not None:
            self_ok = pat_result  # ResFile::Bind: the last PAT0 overwrites the result
        lines.append("F\t%s\t%u\t%d\t%d\t%d" % (path, len(groups), rev_ok, self_ok, final_ok))
    return lines, len(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    got = run.stdout.splitlines()
    expected, nfiles = expected_lines(args.disc)

    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    anims_exp = collections.Counter(line.split("\t")[2] for line in expected if line[0] == "A")
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print(f"files: BRRES {nfiles}; animations: "
          + ", ".join(f"{k} {anims_exp[k]}" for k in sorted(anims_exp)))
    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}"
                                for k in ("F", "G", "M", "T", "P", "A"))
          + " (harness/python)")
    print(f"missing {len(missing)}, extra {len(extra)}")
    for line in missing[:10]:
        print("  missing:", line)
    for line in extra[:10]:
        print("  extra:  ", line)

    ok = run.returncode == 0 and not missing and not extra and nfiles > 0
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
