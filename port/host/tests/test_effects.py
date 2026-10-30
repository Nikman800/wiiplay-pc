#!/usr/bin/env python3
"""Compare ef_harness (decompiled nw4r::ef::Resource on the host) with
port/assets/tools/fmt_effect.py and an independent model of the relocation.

Runs port/host/build/*/ef_harness on the extracted disc and builds the same
lines (see the comment at the top of tests/ef_harness.cpp):

  EF/E/ED/PD/K   every BREFF, emitter, emitter and particle descriptor field,
                 particle and emit track (fmt_effect.parse, read-only)
  TF/T           every BREFT and texture
  STAGE          RelocateCommand's error count, emitter and texture totals and
                 the two project lists after every set/reset
  FILE           every registered file (the emulated base address is taken
                 from the harness and only checked for range and alignment)
  W              every run of bytes that differs from the disc file

The W lines come from a model written from the file format and the game's
call sequence, not from the decompiled code: Resource::Add/AddTexture link the
project (+0x4 prev, +0x8 next, big-endian emulated addresses of the
neighbouring project headers), write each emitter's name slot and track pointer
table and each texture's name/texture/tlut slots; RelocateCommand resolves
every particle texture name and every texture/child curve name by searching
the registered projects from the last one back; RemoveEffectProject /
RemoveTextureProject unlink. setCaptureEfb (CAP) writes format, width, height
and the buffer address into dummy_tex. A file whose W lines match is
byte-identical to the Wii's in-memory copy at the same emulated address.

fmt_effect.py reads the curve name table header as (padding, count); the
count is the first u16 (AnimCurveNameTable, ef_res_animcurve.h), so its
'nameTable' entries are always empty. The name tables are read here.

    python3 test_effects.py --harness build/host64/ef_harness --disc DISC
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_effect  # noqa: E402
from wiiarc import walk  # noqa: E402

STATIC_ARC = "Common/PartyStatic/common.carc"
# RPSysEffectMgr (Party Pack): cursor creator, then static creator.
BOOT = [("cursor", "nw4r_defcursor_pack"), ("static", "basic_common")]
# The pairs the scenes pass to RPSysEffectCreator::setEffectResource
# (RPFshScene.cpp:120, RPBilScene.cpp:170, RPTnkScene.cpp:293,
# RPPnpScene.cpp:64, RPCowGame.cpp:525, RPHkyScene.cpp:49, RPDucScene.cpp:206,
# RPBomScene.cpp:227, RPWlyScene.cpp:81, RPAvatarScene.cpp:98,
# RPMenuScene.cpp:192).
GAME_SCENES = {"basic_fishing", "basic_billiards", "basic_tank", "basic_pingpong",
               "basic_cow", "basic_hokey", "basic_shooting", "basic_bomb",
               "basic_wanted", "basic_selectmenu", "pack_sequence"}
PROJECT_OFFSET = 0x18  # FileHeader (0x10) + BlockHeader (8)
ARENA = [(0x80003100, 0x81800000), (0x90000000, 0x94000000)]
CAPTURE = (4, 304, 228)  # format, width, height the harness passes


def u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def cstr(d, o, n):
    s = bytes(d[o:o + n])
    return s.split(b"\0", 1)[0].decode("latin-1")


def name_table(d):
    """[(name, name offset, entry offset)] of the project's NameTable."""
    table = PROJECT_OFFSET + u32(d, PROJECT_OFFSET)
    out, p = [], table + 8
    for _ in range(u16(d, table + 4)):
        n = u16(d, p)
        out.append((cstr(d, p + 2, n), p + 2, table + u32(d, p + 2 + n)))
        p += 2 + n + 8
    return out


def curve_names(d, curve):
    """(work slot offsets, names) of a curve's AnimCurveNameTable."""
    o = curve["nameTable"]["offset"]
    count = u16(d, o)
    p = o + 4 + 4 * count
    slots, names = [], []
    for k in range(count):
        n = u16(d, p)
        slots.append(o + 4 + 4 * k)
        names.append(cstr(d, p + 2, n))
        p += 2 + n
    return slots, names


class Project:
    def __init__(self, path, data):
        self.path = path
        self.orig = bytes(data)
        self.data = bytearray(data)
        self.parsed = fmt_effect.parse(data)
        self.effect = self.parsed["format"] == "BREFF"
        self.names = name_table(self.orig)
        self.base = None
        self.link = None

    def addr(self, off):
        return self.base + off

    def put(self, off, value):
        self.data[off:off + 4] = struct.pack(">I", value)


class Model:
    def __init__(self):
        self.breff, self.breft = [], []
        self.num_emitter = self.num_texture = 0

    @staticmethod
    def _link(lst, p):
        # ut::List_Append on a link at +0x4 holding (prev, next) object addresses.
        p.put(PROJECT_OFFSET + 4, 0)
        p.put(PROJECT_OFFSET + 8, 0)
        if lst:
            tail = lst[-1]
            p.put(PROJECT_OFFSET + 4, tail.addr(PROJECT_OFFSET))
            tail.put(PROJECT_OFFSET + 8, p.addr(PROJECT_OFFSET))
        lst.append(p)

    @staticmethod
    def _unlink(lst, p):
        i = lst.index(p)
        prev = lst[i - 1] if i > 0 else None
        nxt = lst[i + 1] if i + 1 < len(lst) else None
        if prev:
            prev.put(PROJECT_OFFSET + 8, nxt.addr(PROJECT_OFFSET) if nxt else 0)
        if nxt:
            nxt.put(PROJECT_OFFSET + 4, prev.addr(PROJECT_OFFSET) if prev else 0)
        p.put(PROJECT_OFFSET + 4, 0)
        p.put(PROJECT_OFFSET + 8, 0)
        lst.remove(p)

    def add(self, p):
        self._link(self.breff, p)
        self.num_emitter += len(p.names)
        for (name, name_off, _), em in zip(p.names, p.parsed["emitters"]):
            p.put(em["namePointerOffset"], p.addr(name_off))
            for t in em["tracks"]:
                p.put(t["pointerOffset"], p.addr(t["offset"]))

    def add_texture(self, p):
        self._link(self.breft, p)
        self.num_texture += len(p.names)
        for (name, name_off, _), tx in zip(p.names, p.parsed["textures"]):
            p.put(tx["namePointerOffset"], p.addr(name_off))
            p.put(tx["texturePointerOffset"], p.addr(tx["textureDataOffset"]))
            p.put(tx["tlutPointerOffset"], p.addr(tx["tlutDataOffset"]))

    def remove(self, eff, tex):
        self._unlink(self.breff, eff)
        self.num_emitter -= len(eff.names)
        self._unlink(self.breft, tex)
        self.num_texture -= len(tex.names)

    @staticmethod
    def _find(projects, name, first=None):
        order = ([first] if first else []) + list(reversed(projects))
        for p in order:
            for n, _, off in p.names:
                if n == name:
                    return p, off
        return None

    def find_texture(self, name, first=None):
        return None if name == "" else self._find(self.breft, name, first)

    def find_emitter(self, name):
        return self._find(self.breff, name)

    def relocate(self):
        errors = 0
        for p in self.breff:
            for em in p.parsed["emitters"]:
                desc = em["particleParameterDesc"]
                for slot, tn in zip(desc["texturePointerOffsets"], desc["textureNames"]):
                    name = cstr(p.orig, tn["nameOffset"], tn["length"])
                    hit = self.find_texture(name)
                    p.put(slot, hit[0].addr(hit[1]) if hit else 0)
                    if name and not hit:
                        errors += 1
                for t in em["tracks"][:em["particleTrackCount"]]:
                    if t["magic"] != fmt_effect.ANIM_CURVE_MAGIC or t["kindEnable"] != 1:
                        continue
                    if t["curveFlag"] == 4:  # AC_TYPE_PARTICLE_TEXTURE
                        find, need = self.find_texture, (lambda n: n != "")
                    elif t["curveFlag"] == 5:  # AC_TYPE_CHILD
                        find, need = self.find_emitter, (lambda n: True)
                    else:
                        continue
                    for slot, name in zip(*curve_names(p.orig, t)):
                        hit = find(name)
                        p.put(slot, hit[0].addr(hit[1]) if hit else 0)
                        if need(name) and not hit:
                            errors += 1
        return errors


def runs(p):
    out, i, n = [], 0, len(p.orig)
    while i < n:
        if p.data[i] == p.orig[i]:
            i += 1
            continue
        start = i
        while i < n and p.data[i] != p.orig[i]:
            i += 1
        out.append((start, p.data[start:i].hex()))
    return out


def f32hex(d, o):
    return "%x" % u32(d, o)


def structure_lines(p):
    d, path, lines = p.orig, p.path, []
    if p.effect:
        lines.append("EF %s %d %d" % (path, len(p.names), u32(d, PROJECT_OFFSET)))
        names = [n for n, _, _ in p.names]
        for i, ((name, name_off, off), em) in enumerate(zip(p.names, p.parsed["emitters"])):
            assert off == em["offset"]
            desc = em["particleParameterDesc"]
            lines.append("E %s %d %x %s %x %d %d %d %d %d %x %x" % (
                path, i, off, name, name_off, names.index(name), em["particleTrackCount"],
                em["particleInitTrackCount"], em["emitTrackCount"], em["emitInitTrackCount"],
                em["emitterDescOffset"], desc["offset"]))
            e = em["emitterDescOffset"]
            ds = e + 0x94
            lines.append("ED %s %d %x %x %d %d %s %d %d %d %s %s %s %d %d %x %x %s %s %s %d %d" % (
                path, i, u32(d, e), u32(d, e + 4), u16(d, e + 8), u16(d, e + 0xA),
                f32hex(d, e + 0x10), u16(d, e + 0x14), u16(d, e + 0x16), u16(d, e + 0x18),
                f32hex(d, e + 0x1C), f32hex(d, e + 0x3C), f32hex(d, e + 0x50), u16(d, e + 0x34),
                u32(d, e + 0x88), u16(d, ds), u16(d, ds + 0x66), f32hex(d, ds + 0x74),
                f32hex(d, ds + 0x98), f32hex(d, ds + 0xA8), u32(d, e + 4) & 0xFF, d[ds + 0xA0]))
            q = desc["offset"]
            tex = " ".join(cstr(d, t["nameOffset"], t["length"]) or "-" for t in desc["textureNames"])
            lines.append("PD %s %d %s %x %s %s %s" % (
                path, i, tex, u16(d, q + 0x74), f32hex(d, q + 0x4C), f32hex(d, q + 0x7C),
                f32hex(d, q + 0x84)))
            for k, t in enumerate(em["tracks"]):
                kind, j = ("p", k) if k < em["particleTrackCount"] else ("e", k - em["particleTrackCount"])
                lines.append("K %s %d %s%d %x %02x %d %d %d %d %d %d %x %x %x %x %x" % (
                    path, i, kind, j, t["offset"], t["magic"], t["kindType"], t["curveFlag"],
                    t["kindEnable"], t["processFlag"], t["loopCount"], t["frameLength"],
                    t["keyTableSize"], t["randomTableSize"], t["randomRangeTableSize"],
                    t["nameTableSize"], t["infoTableSize"]))
    else:
        lines.append("TF %s %d %d" % (path, len(p.names), u32(d, PROJECT_OFFSET)))
        by_name = {}
        for (name, _, off), tx in zip(p.names, p.parsed["textures"]):
            by_name.setdefault(name, (off, tx))
        for i, (name, name_off, _) in enumerate(p.names):
            off, tx = by_name[name]  # FindTexture: first entry with the name
            first_name_off = next(no for n, no, _ in p.names if n == name)
            lines.append("T %s %d %s %x %x %d %d %d %d %d %d %d %d %d %d %x %x" % (
                path, i, name, off, first_name_off, tx["width"], tx["height"], tx["format"],
                tx["tlutFormat"], tx["tlutEntries"], tx["dataSize"], tx["tlutSize"], tx["mipmap"],
                tx["min_filt"], tx["mag_filt"], tx["textureDataOffset"], tx["tlutDataOffset"]))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    r = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE, text=True)
    if r.returncode != 0:
        print("ef_harness failed (exit %d)" % r.returncode)
        return 1
    got = r.stdout.splitlines()

    files = {}
    for path, blob in walk(args.disc):
        if blob[:4] in (b"REFF", b"REFT"):
            files[path] = blob

    # Bases (arena addresses) as the harness reports them.
    bases = {}
    for line in got:
        if line.startswith("FILE "):
            _, _, path, base, size = line.split()
            base = int(base, 16)
            if bases.setdefault(path, base) != base:
                print("ERROR: %s moved" % path)
                return 1
            if not any(lo <= base and base + int(size) <= hi for lo, hi in ARENA) or base % 4:
                print("ERROR: %s base %08x is not an arena address" % (path, base))
                return 1
    captures = [line.split() for line in got if line.startswith("CAP ")]
    cap_buf = int(captures[0][3], 16) if captures else None

    # Scene pairs: every X.breff / X.breft at the root of an archive other than
    # the static one, in disc order.
    pairs = collections.OrderedDict()
    for path in sorted(files, key=lambda p: p.split("/")):
        arc, _, name = path.rpartition("/")
        stem, ext = os.path.splitext(name)
        if arc != STATIC_ARC and ext == ".breff" and arc + "/" + stem + ".breft" in files:
            pairs[stem] = arc
    errors = 0
    if set(pairs) != GAME_SCENES:
        print("ERROR: scene pairs on the disc %s != the game's %s" % (sorted(pairs), sorted(GAME_SCENES)))
        errors += 1

    model = Model()
    registered = []
    want = []

    def project(path):
        p = Project(path, files[path])
        p.base = bases.get(path)
        if p.base is None:
            raise SystemExit("ERROR: harness did not register %s" % path)
        return p

    def state(stage, reloc_errors):
        want.append("STAGE %s %d %d %d %s %s" % (
            stage, reloc_errors, model.num_emitter, model.num_texture,
            ",".join(p.path for p in model.breff), ",".join(p.path for p in model.breft)))
        for p in registered:
            want.append("FILE %s %s %08x %d" % (stage, p.path, p.base, len(p.orig)))
        for p in registered:
            for off, hexbytes in runs(p):
                want.append("W %s %s %x %s" % (stage, p.path, off, hexbytes))

    def set_pair(stage, arc, stem):
        eff, tex = project(arc + "/" + stem + ".breff"), project(arc + "/" + stem + ".breft")
        model.add(eff)
        model.add_texture(tex)
        registered.extend([eff, tex])
        n = model.relocate()
        want.extend(structure_lines(eff))
        want.extend(structure_lines(tex))
        state(stage, n)
        return eff, tex

    creators = []
    for stage, stem in BOOT:
        creators.append(set_pair(stage, STATIC_ARC, stem))
    for stem, arc in pairs.items():
        eff, tex = set_pair("set:" + stem, arc, stem)
        for _, ctex in creators + [(eff, tex)]:
            hit = model.find_texture("dummy_tex", ctex)
            if hit:
                tp, off = hit
                tp.data[off + 0xC] = CAPTURE[0]
                tp.data[off + 4:off + 8] = struct.pack(">HH", CAPTURE[1], CAPTURE[2])
                tp.put(off + 0x18, cap_buf)
                want.append("CAP %s %s+%x %08x %08x" % (ctex.path, tp.path, off, cap_buf, cap_buf))
        model.remove(eff, tex)
        n = model.relocate()
        state("reset:" + stem, n)
        registered.remove(eff)
        registered.remove(tex)

    gc, wc = collections.Counter(got), collections.Counter(want)
    missing, extra = wc - gc, gc - wc
    for line in sorted(missing)[:20]:
        print("missing:", line)
    for line in sorted(extra)[:20]:
        print("extra:  ", line)

    kinds = collections.OrderedDict()
    for line in want:
        k = line.split(" ", 1)[0]
        kinds[k] = kinds.get(k, 0) + 1
    gk = collections.Counter(line.split(" ", 1)[0] for line in got)
    print("files: BREFF %d, BREFT %d (scene pairs %d)" % (
        sum(1 for p in files if files[p][:4] == b"REFF"),
        sum(1 for p in files if files[p][:4] == b"REFT"), len(pairs)))
    registered_paths = {l.split()[1] for l in want if l.startswith(("EF ", "TF "))}
    if registered_paths != set(files):
        print("ERROR: not every BREFF/BREFT was registered: %s" % sorted(set(files) - registered_paths))
        errors += 1
    print("lines: " + ", ".join("%s %d/%d" % (k, gk.get(k, 0), v) for k, v in kinds.items()) +
          " (harness/python)")
    print("missing %d, extra %d" % (sum(missing.values()), sum(extra.values())))
    if missing or extra or errors:
        print("FAILED")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
