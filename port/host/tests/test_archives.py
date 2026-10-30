#!/usr/bin/env python3
"""Compare the host loader harness (real decompiled ARC/EGG code) with wiiarc.py.

Runs arc_harness over the extracted disc and walks the same disc with
port/assets/tools/wiiarc.py, then checks that both produce the same set of
leaf paths with the same sizes and bytes (FNV-1a 64 of the contents), and that
the nw4r BinaryFileHeader fields the harness read through the big-endian
wrapper match a plain big-endian struct.unpack of the same bytes.

    python3 port/host/tests/test_archives.py --harness BUILD/arc_harness \
        --disc ~/wii-play/extracted/files

Exit status 0 when everything matches. Nothing is written to disk.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import wiiarc  # noqa: E402

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK = (1 << 64) - 1


def fnv1a64(data):
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK
    return h


def nw4r_header(data):
    """Same summary the harness prints, from struct.unpack."""
    if len(data) < 16 or data[4:6] != b"\xfe\xff":
        return "-"
    sig, bom, ver, size, hsize, blocks = struct.unpack(">4sHHIHH", data[:16])
    valid = size >= 16 + 8 * 1 and blocks >= 1
    return "%s v%04x size=%d hdr=%d blocks=%d valid=%d" % (
        sig.decode("latin-1"), ver, size, hsize, blocks, 1 if valid else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    proc = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        print("FAIL: harness exited with %d" % proc.returncode)
        return 1

    got = {}
    got_arcs = {}
    for line in proc.stdout.splitlines():
        if line.startswith("#archive\t"):
            _tag, path, nodes = line.split("\t")
            got_arcs[path] = int(nodes)
            continue
        path, size, h, hdr = line.split("\t")
        if path in got:
            print("FAIL: duplicate path from harness: %s" % path)
            return 1
        got[path] = (int(size), int(h, 16), hdr)

    want = {}
    for path, data in wiiarc.walk(args.disc):
        want[path] = (len(data), fnv1a64(data), nw4r_header(data))

    # Every archive (disc-level and nested), with its node count, by the same
    # rule as wiiarc._expand: unpack, then U8 means archive.
    want_arcs = {}

    def find_arcs(path, data):
        data = wiiarc.unpack(data)
        if data[:4] != wiiarc.U8_MAGIC:
            return
        root = struct.unpack(">I", data[4:8])[0]
        want_arcs[path] = struct.unpack(">I", data[root + 8:root + 12])[0]
        for inner, blob in wiiarc.u8(data):
            find_arcs(path + "/" + inner, blob)

    disc_files = 0
    carcs = 0
    for dirpath, _dirs, names in os.walk(args.disc):
        for n in names:
            full = os.path.join(dirpath, n)
            disc_files += 1
            carcs += n.endswith(".carc")
            with open(full, "rb") as f:
                find_arcs(os.path.relpath(full, args.disc), f.read())
    top_arcs = [p for p in want_arcs if os.path.isfile(os.path.join(args.disc, p))]
    carcs_opened = sum(1 for p in top_arcs if p.endswith(".carc") and p in got_arcs)

    missing = sorted(set(want) - set(got))
    extra = sorted(set(got) - set(want))
    bad = []
    for p in sorted(set(want) & set(got)):
        w, g = want[p], got[p]
        if w[0] != g[0]:
            bad.append("%s: size %d != %d" % (p, g[0], w[0]))
        elif w[1] != g[1]:
            bad.append("%s: contents differ" % p)
        elif w[2] != g[2]:
            bad.append("%s: header '%s' != '%s'" % (p, g[2], w[2]))

    for p in sorted(set(want_arcs) | set(got_arcs)):
        if want_arcs.get(p) != got_arcs.get(p):
            bad.append("archive %s: nodes %s != %s" % (p, got_arcs.get(p), want_arcs.get(p)))

    nw4r = sum(1 for v in got.values() if v[2] != "-")
    print("disc files: %d (.carc: %d, opened by the harness: %d)" % (disc_files, carcs, carcs_opened))
    print("archives mounted: harness %d, wiiarc %d (disc-level %d, nested %d)" % (
        len(got_arcs), len(want_arcs), len(top_arcs), len(want_arcs) - len(top_arcs)))
    print("leaf files: harness %d, wiiarc %d" % (len(got), len(want)))
    print("nw4r headers checked: %d" % nw4r)
    print("missing %d, extra %d, mismatched %d" % (len(missing), len(extra), len(bad)))
    for p in missing[:20]:
        print("  missing: " + p)
    for p in extra[:20]:
        print("  extra: " + p)
    for b in bad[:20]:
        print("  mismatch: " + b)

    ok = not missing and not extra and not bad and carcs_opened == carcs
    print("OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
