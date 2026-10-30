#!/usr/bin/env python3
"""Compare the host resource harness (real decompiled ResFont / RPSysMessage)
with an independent Python decoding (port/assets/tools/fmt_text_font.py).

Runs res_harness over the extracted disc, walks the same disc with wiiarc.py,
and for every BRFNT and BMG file (including those inside nested archives)
builds the lines the harness should print (see tests/res_harness.cpp):

    F  font metrics and the relocated FINF/TGLP slots, as file offsets
    C  the CMAP chain (pMap, pNext...) as payload offsets, in chain order
    W  the CWDH chain (pWidth, pNext...)
    G  one line per code point 0..0xFFFF that FindGlyphIndex finds: index,
       widths, glyph sheet offset, cell and texture fields
    A  how many code points are not found, and the alternate glyph they get
    M  one line per MID1 entry: id split, INF1 entry, attributes, text offset,
       code units, tags, a hash of the text and tags, wcslen

and checks that the two sets of lines are identical.

    python3 port/host/tests/test_resources.py --harness BUILD/res_harness \
        --disc ~/wii-play/extracted/files

Exit status 0 when everything matches. Nothing is written to disk.
"""
import argparse
import collections
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_text_font  # noqa: E402
import wiiarc  # noqa: E402

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK = (1 << 64) - 1


def hash_tokens(tokens):
    h = FNV_OFFSET
    for token in tokens:
        for i in range(4):
            h = ((h ^ ((token >> (8 * i)) & 0xFF)) * FNV_PRIME) & MASK
    return h


def font_lines(path, data):
    decoded = fmt_text_font.parse_brfnt(data)
    font = fmt_text_font.BrfntFont(decoded)
    finf, tglp = font.finf, font.tglp
    d = finf["defaultWidth"]
    lines = [
        "\t".join(
            str(v)
            for v in (
                "F", path, finf["encoding"], finf["fontType"], finf["width"],
                finf["height"], finf["ascent"], finf["height"] - finf["ascent"],
                tglp["baselinePos"], tglp["cellWidth"], tglp["cellHeight"],
                tglp["maxCharWidth"], tglp["sheetFormat"], finf["linefeed"],
                finf["alterCharIndex"], d["left"], d["glyphWidth"], d["charWidth"],
                finf["pGlyph"], finf["pWidth"], finf["pMap"], tglp["sheetImage"],
            )
        ),
        "\t".join(["C", path] + [str(m["offset"] + 8) for m in font.maps]),
        "\t".join(["W", path] + [str(w["offset"] + 8) for w in font.widths]),
    ]
    not_found = 0
    for ch in range(0x10000):
        index = font.find_glyph_index(ch)
        if index == fmt_text_font.GLYPH_INDEX_NOT_FOUND:
            not_found += 1
            continue
        w = font.char_widths(index)
        g = font.glyph(index)
        lines.append(
            "\t".join(
                str(v)
                for v in (
                    "G", path, ch, index, *w, g["texOffset"], g["cellX"], g["cellY"],
                    g["height"], g["texFormat"], g["texWidth"], g["texHeight"],
                )
            )
        )
    if not_found:
        alt = finf["alterCharIndex"]
        w = font.char_widths(alt)
        g = font.glyph(alt)
        lines.append(
            "\t".join(
                str(v)
                for v in ("A", path, not_found, *w, g["texOffset"], g["cellX"], g["cellY"])
            )
        )
    return lines


def bmg_lines(path, data):
    lines = []
    for i, m in enumerate(fmt_text_font.bmg_messages(data)):
        tokens = []
        for unit in m["sequence"]:
            if unit >= 0:
                tokens.append(unit)
            else:
                length, tag_id = m["tags"][-unit - 1]
                tokens += [0xFFFF0000 | length, tag_id]
        lines.append(
            "\t".join(
                str(v)
                for v in (
                    "M", path, i, m["messageID"], m["group"], m["index"],
                    m["entryIndex"], m["attribute"], m["scale"], m["charSpace"],
                    m["textOffset"], len(m["codeUnits"]), len(m["tags"]),
                    f"{hash_tokens(tokens):016x}", m["wcslen"],
                )
            )
        )
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run(
        [args.harness, args.disc], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True,
    )
    sys.stderr.write(run.stderr)
    got = run.stdout.splitlines()

    expected = []
    files = collections.Counter()
    for path, data in wiiarc.walk(args.disc):
        path = path.replace(os.sep, "/")
        if data[:4] == b"RFNT":
            expected += font_lines(path, data)
            files["BRFNT"] += 1
        elif data[:4] == b"MESG":
            expected += bmg_lines(path, data)
            files["BMG"] += 1

    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    missing = sorted(set(expected) - set(got))
    extra = sorted(set(got) - set(expected))

    print(f"files: BRFNT {files['BRFNT']}, BMG {files['BMG']}")
    print(
        "lines: "
        + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}" for k in "FCWGAM")
        + " (harness/python)"
    )
    print(f"missing {len(missing)}, extra {len(extra)}")
    for line in missing[:10]:
        print("  missing:", line)
    for line in extra[:10]:
        print("  extra:  ", line)

    ok = (
        run.returncode == 0
        and not missing
        and not extra
        and len(got) == len(expected)
        and files["BRFNT"] > 0
        and files["BMG"] > 0
    )
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
