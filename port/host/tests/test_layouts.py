#!/usr/bin/env python3
"""Compare lyt_harness (decompiled TPL + nw4r::lyt on the host) with fmt_layout.py.

Runs port/host/build/*/lyt_harness on the extracted disc and builds the same
lines independently from port/assets/tools/fmt_layout.py (BRLYT, BRLAN and
TPL parsed from the file format) and wiiarc.py (archive walk):

  T/TD  every TPL palette and descriptor (header fields, data offsets)
  L     every layout: size, pane count, material count, group count
  P     every pane in tree order: kind, name, user data, flags, SRT, size,
        material, and per kind the text (length and hash), font, colours,
        vertex colours and window frames
  M     every material: TEV colours, texture SRT 0, textures (TPL size and
        format through the archive's timg/ directory, wrap modes)
  G     every group: pane names
  A/AC  every (BRLAN, BRLYT) pair from the same archive: frame size, loop,
        file count, contents, and which contents bound to a pane or material

The game's rules that decide what a line contains (block nesting, group depth,
the clamps on material counts, the text buffer length, name lookup) are
written out here from the format description, not taken from the harness.

    python3 test_layouts.py --harness build/host64/lyt_harness --disc DISC
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_layout  # noqa: E402  (read-only reference)
import wiiarc  # noqa: E402

GX_MAX_TEXMAP = 8
MAX_TEX_SRT = 10


def g(value):
    return "%.6g" % value


def vec(v, keys):
    return ",".join(g(v[k]) for k in keys)


def fixed(raw):
    end = raw.find(b"\0")
    return (raw if end < 0 else raw[:end]).decode("latin-1")


def fnv_text(units):
    h = 0xCBF29CE484222325
    for u in units:
        for b in (u >> 8, u & 0xFF):
            h ^= b
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def tpl_lines(path, data):
    tpl = fmt_layout.parse_tpl(data)
    lines = [f"T\t{path}\t{tpl['header']['numDescriptors']}"]
    for i, d in enumerate(tpl["descriptors"]):
        t = d["textureHeader"]
        if not d["textureHeaderOffset"]:
            lines.append(f"TD\t{path}\t{i}\t-")
            continue
        c = d["CLUTHeader"] if d["CLUTHeaderOffset"] else None
        clut = f"{c['numEntries']}\t{c['format']}\t{c['dataOffset']}" if c else "-\t-\t-"
        lines.append(
            "\t".join(
                str(x)
                for x in (
                    "TD", path, i, t["width"], t["height"], t["format"], t["dataOffset"],
                    t["wrapS"], t["wrapT"], t["minFilter"], t["magFilter"], g(t["LODBias"]),
                    t["edgeLODEnable"], t["minLOD"], t["maxLOD"], clut,
                )
            )
        )
    return lines


def tpl_first(data):
    tpl = fmt_layout.parse_tpl(data)
    t = tpl["descriptors"][0]["textureHeader"]
    return t["width"], t["height"], t["format"]


def material_line(path, data, mat, textures, files, root):
    """One M line for the res::Material at mat['offset']."""
    off = mat["offset"]
    n = mat["resNum"]
    tex_num = min(n["texMap"], GX_MAX_TEXMAP)
    srt_cap = min(n["texSRT"], MAX_TEX_SRT)
    tev = ";".join(",".join(str(v) for v in c) for c in mat["tevCols"])
    kcol = ",".join("%02x%02x%02x%02x" % (c["r"], c["g"], c["b"], c["a"]) for c in mat["tevKCols"])
    srt = "-"
    srt_off = off + 0x40 + 4 * n["texMap"]
    if srt_cap:
        tx, ty, rot, sx, sy = struct.unpack_from(">5f", data, srt_off)
        srt = ",".join(g(v) for v in (tx, ty, rot, sx, sy))
    tex = []
    for i in range(tex_num):
        tex_idx, wrap_s, wrap_t = struct.unpack_from(">HBB", data, off + 0x40 + 4 * i)
        w, h, fmt = tpl_first(files[root + "timg/" + textures[tex_idx]])
        tex.append(f"{w}x{h}:{fmt}:{wrap_s}:{wrap_t}")
    return (
        f"M\t{path}\t{mat['name']}\t{tex_num}\t{srt_cap}\t{tev}\t{kcol}\t{srt}\t"
        + (";".join(tex) if tex else "-")
    )


def list_names(data, section):
    """txl1/fnl1 names. nameStrOffset is relative to the first entry (the
    end of the 12-byte list header), as Material and TextBox resolve it.
    fmt_layout.parse_string_list resolves it from the block start instead,
    so its "name" fields are wrong; only its offsets are used here."""
    base = section["offset"] + 12
    names = []
    for e in section["entries"]:
        start = base + e["nameStrOffset"]
        names.append(data[start:data.index(b"\0", start)].decode("ascii"))
    return names


def layout_lines(path, data, files, root, anims):
    lyt = fmt_layout.parse_brlyt(data)
    if lyt["header"]["version"] != 8:
        return []

    textures, fonts, materials = [], [], []
    panes = []  # (depth, section)
    groups = []
    layout_size = None
    depth = 0
    group_depth = 0
    root_group_seen = False
    for s in lyt["sections"]:
        kind = s["kind"]
        if kind == "lyt1":
            layout_size = s["layoutSize"]
        elif kind == "txl1":
            textures = list_names(data, s)
        elif kind == "fnl1":
            fonts = list_names(data, s)
        elif kind == "mat1":
            materials = s["materials"]
        elif kind in ("pan1", "pic1", "txt1", "wnd1", "bnd1"):
            panes.append((depth, s))
        elif kind == "pas1":
            depth += 1
        elif kind == "pae1":
            depth -= 1
        elif kind == "grp1":
            if not root_group_seen:
                root_group_seen = True
            elif group_depth == 1:
                groups.append(s)
        elif kind == "grs1":
            group_depth += 1
        elif kind == "gre1":
            group_depth -= 1

    lines = []
    owned = []  # material names in tree order, for animation binding
    pane_names = []
    n_materials = 0
    for index, (d, s) in enumerate(panes):
        kind = s["kind"]
        off = s["offset"]
        name = s["name"]
        pane_names.append(name)
        user = data[off + 0x1C:off + 0x24].hex()
        visible = s["flag"] & 1
        bp = s["basePosition"]
        mats = []
        extra = "-"
        if kind == "pic1":
            mats = [materials[s["materialIdx"]]]
            extra = "vtx=" + ",".join("%08x" % c for c in s["vtxCols"])
        elif kind == "txt1":
            mats = [materials[s["materialIdx"]]]
            buf = s["textBufBytes"] // 2
            if buf > 0:
                buf -= 1
            units = []
            if s["textStrBytes"] >= 2 and buf > 0:
                res_len = s["textStrBytes"] // 2 - 1
                count = min(res_len, buf)
                units = list(struct.unpack_from(">%dH" % count, data, off + s["textStrOffset"]))
                if 0 in units:
                    units = units[:units.index(0)]
            font_name = fonts[s["fontIdx"]]
            has_font = "y" if (root + "font/" + font_name) in files else "n"
            fs = s["fontSize"]
            extra = (
                f"buf={buf} len={len(units)} hash=%016x font={has_font} size={g(fs['x'])},{g(fs['y'])}"
                f" space={g(s['charSpace'])},{g(s['lineSpace'])} cols=%08x,%08x"
            ) % (fnv_text(units), s["textCols"][0], s["textCols"][1])
        elif kind == "wnd1":
            content = off + s["contentOffset"]
            vtx = struct.unpack_from(">4I", data, content)
            (content_mat,) = struct.unpack_from(">H", data, content + 0x10)
            frames = [materials[f["materialIdx"]] for f in s["frames"]]
            mats = [materials[content_mat]] + frames
            extra = "vtx=" + ",".join("%08x" % c for c in vtx) + " frames=" + ",".join(
                m["name"] for m in frames
            )
        sz = s["size"]
        lines.append(
            "\t".join(
                [
                    "P", path, str(index), str(d), kind, name, user, str(visible),
                    str(s["alpha"]), f"{bp % 3},{bp // 3}",
                    vec(s["translate"], "xyz"), vec(s["rotate"], "xyz"),
                    vec(s["scale"], "xy"), f"{g(sz['x'])},{g(sz['y'])}",
                    mats[0]["name"] if mats else "-", extra,
                ]
            )
        )
        for m in mats:
            lines.append(material_line(path, data, m, textures, files, root))
            owned.append(m["name"])
        n_materials += len(mats)

    for grp in groups:
        lines.append(f"G\t{path}\t{grp['name']}\t" + (",".join(grp["paneNames"]) or "-"))

    lines.append(
        f"L\t{path}\t{g(layout_size['x'])}\t{g(layout_size['y'])}\t{len(panes)}\t{n_materials}\t{len(groups)}"
    )

    # Binding: a pane content binds to the first pane with that name
    # (16-character compare), a material content to the first material
    # (20-character compare) owned by a pane or window frame.
    pane_set = {n[:16] for n in pane_names}
    mat_set = {n[:20] for n in owned}
    for anm_path, anm in anims:
        pai = next(s for s in anm["sections"] if s["kind"] == "pai1")
        bound = 0
        for i, c in enumerate(pai["animationContents"]):
            b = (c["name"][:16] in pane_set) if c["type"] == 0 else (c["name"][:20] in mat_set)
            bound += b
            lines.append(f"AC\t{anm_path}\t{path}\t{i}\t{c['name']}\t{c['type']}\t{int(b)}")
        lines.append(
            f"A\t{anm_path}\t{path}\t{pai['frameSize']}\t{int(pai['loop'] != 0)}\t{pai['fileNum']}"
            f"\t{pai['animContNum']}\t{bound}"
        )
    return lines


def expected_lines(disc):
    # Layout archive directory (the archive root, or its arc/ directory for
    # the HOME Menu archives) -> {"blyt/x.brlyt": bytes, "timg/...", ...}
    dirs = collections.defaultdict(dict)
    lines = []
    counts = collections.Counter()
    for path, data in wiiarc.walk(disc):
        path = path.replace(os.sep, "/")
        if path.endswith(".tpl"):
            lines += tpl_lines(path, data)
            counts["TPL"] += 1
        parts = path.split("/")
        if len(parts) >= 3 and parts[-2] in ("blyt", "anim", "timg", "font"):
            dirs["/".join(parts[:-2])][parts[-2] + "/" + parts[-1]] = data

    for prefix, files in sorted(dirs.items()):
        anims = [
            (prefix + "/" + k, fmt_layout.parse_brlan(v))
            for k, v in sorted(files.items())
            if k.startswith("anim/") and k.endswith(".brlan")
        ]
        layouts = [k for k in sorted(files) if k.startswith("blyt/") and k.endswith(".brlyt")]
        for k in layouts:
            lines += layout_lines(prefix + "/" + k, files[k], files, "", anims)
            counts["BRLYT"] += 1
        if layouts:
            counts["BRLAN"] += len(anims)
    return lines, counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    got = run.stdout.splitlines()
    expected, counts = expected_lines(args.disc)

    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print(f"files: TPL {counts['TPL']}, BRLYT {counts['BRLYT']}, BRLAN {counts['BRLAN']}")
    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}"
                                for k in ("T", "TD", "L", "P", "M", "G", "A", "AC"))
          + " (harness/python)")
    print(f"missing {len(missing)}, extra {len(extra)}")
    for line in missing[:10]:
        print("  missing:", line)
    for line in extra[:10]:
        print("  extra:  ", line)

    ok = (run.returncode == 0 and not missing and not extra and counts["TPL"] > 0
          and counts["BRLYT"] > 0 and counts["BRLAN"] > 0)
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
