#!/usr/bin/env python3
"""Check the OS / storage platform layer (port/docs/HOST_BUILD.md §22).

Runs tests/sys_harness.cpp (the game's EGG heaps, dvdfs.c, RPSysResourceManager,
RPSysSaveDataMgr and the SC getters on the host platform layer) and checks its
output against models written from the formats:

  heap  arena bounds, the heap tree RPSysSystem builds (sizes, nesting, kinds),
        alignment and free checks (the harness's own CHECKs)
  dvd   the disc's sys/fst.bin parsed here: every path's entry number, every
        file's size and FNV-1a hash read from the extracted tree; the FST
        built from the tree when fst.bin is ignored must give the same
        numbers; async callbacks, cancellation, a read from another thread
  res   every file the resource manager holds: the cached .carc files as read
        from the disc (rounded up to 32 bytes, zero padded), the expanded
        archives as wiiarc.py decompresses them, and each archive's files as
        wiiarc.py lists them
  save  the save files the game's save code writes to the scratch NAND:
        RPParty.dat decoded here (signature, version, system and player data
        against the values the harness set, the checksum recomputed) and
        banner.bin (magic, flags, icon speed, the title and subtitle against
        party_message.bmg's messages 1001 / 1002, the banner and icon images
        against banner.tpl); a second run finds the save and loads it back
  sc    the defaults and a config file's values, RPSysSystem::setupLocalSettings

With --compare, the other build's harness (64- / 32-bit) must print the same
lines (heap addresses aside) and write byte-identical save files.

    test_sys.py --harness BUILD/sys_harness --disc ~/wii-play/extracted/files \
                --scratch DIR [--compare OTHER_HARNESS]

Everything the test writes (path list, NAND directories, SC configs) goes under
--scratch, which is wiped first.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import wiiarc  # noqa: E402
import fmt_text_font  # noqa: E402

MEM1_ARENA = (0x80500000, 0x81800000)
MEM2_ARENA = (0x90000800, 0x933E0000)

errors = []


def err(msg):
    if len(errors) < 50:
        print("ERROR: " + msg)
    errors.append(msg)


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def round32(n):
    return (n + 31) & ~31


def run(harness, args, expect_rc=0):
    r = subprocess.run([harness] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True)
    if r.returncode != expect_rc:
        err("%s %s: exit %d, expected %d\n%s" % (os.path.basename(harness), " ".join(args),
                                                 r.returncode, expect_rc, r.stderr[-2000:]))
    lines = r.stdout.splitlines()
    for line in lines:
        if line.startswith("ERROR"):
            err("harness: " + line)
    return lines


# --- FST ---------------------------------------------------------------------

def parse_fst(data):
    """[(path, is_dir, a, b)] in entry order; entry 0 is the root."""
    count = struct.unpack(">I", data[8:12])[0]
    names = count * 12
    entries = [("", True, 0, count)]
    ends = [(count, "")]
    for i in range(1, count):
        while ends and i >= ends[-1][0]:
            ends.pop()
        w0, a, b = struct.unpack(">III", data[i * 12:i * 12 + 12])
        off = names + (w0 & 0xFFFFFF)
        name = data[off:data.index(b"\0", off)].decode("ascii")
        prefix = ends[-1][1] if ends else ""
        path = (prefix + "/" + name).lstrip("/")
        is_dir = (w0 >> 24) != 0
        entries.append((path, is_dir, a, b))
        if is_dir:
            ends.append((b, path))
    return entries


# --- heap --------------------------------------------------------------------

def check_heap(lines, bits):
    heaps = {}
    for line in lines:
        f = line.split()
        if f[0] == "A":
            lo1, hi1, lo2, hi2 = (int(x, 16) for x in (f[2], f[3], f[5], f[6]))
            if lo1 != hi1 or lo2 != hi2:
                err("arenas not handed over to the heaps: %s" % line)
            if not (MEM1_ARENA[0] < hi1 <= MEM1_ARENA[1] and hi2 == MEM2_ARENA[1]):
                err("arena bounds: %s" % line)
        elif f[0] == "H" and f[2] != "none:":
            h = dict(zip(f[2::2], f[3::2]))
            heaps[f[1]] = {k: int(v, 16) if k != "kind" else int(v) for k, v in h.items()}
        elif f[0] == "H":
            err("the assert heap was not created: %s" % line)
    want = {"root1": (1, None, "MEM1"), "root2": (1, None, "MEM2"), "system": (1, 0x600000, "root1"),
            "resource": (2, 0x1800000, "root2"), "reserve": (1, 0x80000, "root1"),
            # PORT_HOST_SIZE(0x100, 0x400): the Wii's 0x100 bytes do not hold 64-bit heap headers.
            "assert": (1, 0x400, "root2")}
    for name, (kind, size, parent) in want.items():
        h = heaps.get(name)
        if h is None:
            err("heap %s missing" % name)
            continue
        if h["kind"] != kind:
            err("heap %s kind %d" % (name, h["kind"]))
        if parent == "MEM1":
            if not (MEM1_ARENA[0] <= h["start"] and h["end"] <= MEM1_ARENA[1]) or h["parent"]:
                err("heap %s not the MEM1 root: %r" % (name, h))
        elif parent == "MEM2":
            if not (MEM2_ARENA[0] <= h["start"] and h["end"] <= MEM2_ARENA[1]) or h["parent"]:
                err("heap %s not the MEM2 root: %r" % (name, h))
        else:
            p = heaps[parent]
            if h["parent"] != p["start"] or not (p["start"] <= h["self"] < h["end"] <= p["end"]):
                err("heap %s not inside %s" % (name, parent))
            # The child's block is the requested size; the EGG heap object and
            # the MEM header come first.
            if h["end"] - h["self"] != size:
                err("heap %s block size %X, expected %X" % (name, h["end"] - h["self"], size))
    cur = [line for line in lines if line.startswith("C current")]
    if not cur or int(cur[0].split()[2], 16) != heaps.get("root2", {}).get("self"):
        err("the current heap is not the MEM2 root heap")
    if "X disposed 1" not in lines:
        err("disposer did not run")
    return len(heaps)


# --- dvd ---------------------------------------------------------------------

def check_dvd(lines, disc, entries, from_tree):
    index = {}
    for i, (path, is_dir, a, b) in enumerate(entries):
        index[path.lower() + ("/" if is_dir and path else "")] = (i, is_dir, b)
    seen = 0
    for line in lines:
        f = line.split(" ")
        if f[0] == "D" and f[1] == "fst":
            src = "built from the tree" if from_tree else "sys/fst.bin"
            if src not in line:
                err("FST source: %s" % line)
        elif f[0] == "D" and f[1] == "cwd":
            if f[2] != "/":
                err("current directory %s" % f[2])
        elif f[0] == "E":
            seen += 1
            path = f[1]
            want = index.get(path.lower())
            if want is None:
                err("%s is not in the FST" % path)
                continue
            if int(f[2]) != want[0]:
                err("%s: entry %s, FST %d" % (path, f[2], want[0]))
            if path.endswith("/"):
                if f[3] != "0":
                    err("DVDFastOpen opened directory %s" % path)
                continue
            data = open(os.path.join(disc, path), "rb").read()
            if int(f[3]) != len(data) or int(f[4], 16) != fnv1a64(data):
                err("%s: size/hash %s %s, file %d %016X" % (path, f[3], f[4], len(data),
                                                          fnv1a64(data)))
    files = sum(1 for e in entries[1:] if not e[1])
    dirs = len(entries) - 1 - files
    if seen != len(entries) - 1:
        err("checked %d paths, the FST has %d entries" % (seen, len(entries) - 1))

    kokeshi = index["common/kokeshi/common.carc"][0]
    want_l = "L -1 %d %d %d" % (kokeshi, kokeshi, kokeshi)
    if want_l not in lines:
        err("lookups: %s, expected %s" % ([l for l in lines if l.startswith("L ")], want_l))

    cfg = open(os.path.join(disc, "gameConfig.ini"), "rb").read()
    # Cancel calls its callback at once (order 1); the two reads complete in
    # issue order when the main thread idles (orders 2, 3).
    want_q = "Q status-before 2 calls-before 0 | 1 32 2 | 1 32 3 | 1 -3 1 | 0"
    if want_q not in lines:
        err("async: %s, expected %s" % ([l for l in lines if l.startswith("Q s")], want_q))
    want_b = "Q bytes %016X %016X" % (fnv1a64(cfg[0:32]), fnv1a64(cfg[32:64]))
    if want_b not in lines:
        err("async bytes: %s" % [l for l in lines if l.startswith("Q b")])
    want_w = "W result 32 hash %016X" % fnv1a64(cfg[0:32])
    if want_w not in lines:
        err("blocking wait: %s" % [l for l in lines if l.startswith("W ")])
    tpl = open(os.path.join(disc, "banner.tpl"), "rb").read()
    want_t = "T banner.tpl %d %016X done 1" % (round32(len(tpl)), fnv1a64(tpl))
    if want_t not in lines:
        err("thread read: %s, expected %s" % ([l for l in lines if l.startswith("T ")], want_t))
    return files, dirs


# --- res ---------------------------------------------------------------------

def check_res(lines, disc):
    fc = fl = arc_files = 0
    current = None
    listed = {}
    for line in lines:
        f = line.split(" ")
        if f[0] in ("FC", "FL"):
            path, size, h = f[1], int(f[2]), int(f[3], 16)
            raw = open(os.path.join(disc, path), "rb").read()
            if f[0] == "FC":
                fc += 1
                want = raw + bytes(round32(len(raw)) - len(raw))
            else:
                fl += 1
                padded = raw + bytes(round32(len(raw)) - len(raw))
                want = wiiarc.yaz0(raw) if raw[:4] == b"Yaz0" else padded
                current = path
                listed[path] = [(p, len(b), fnv1a64(b)) for p, b in wiiarc.u8(want)]
                listed[path].reverse()
            if size != len(want) or h != fnv1a64(want):
                err("%s %s: %d %016X, expected %d %016X" % (f[0], path, size, h, len(want),
                                                            fnv1a64(want)))
        elif f[0] == "R":
            arc_files += 1
            path, inner, size, h = f[1], f[2], int(f[3]), int(f[4], 16)
            want = listed.get(path)
            got = (inner, size, h)
            if not want:
                err("archive file %s/%s not in wiiarc's listing" % (path, inner))
                continue
            exp = want.pop()
            if exp != got:
                err("%s: %r, wiiarc %r" % (path, got, exp))
    for path, rest in listed.items():
        if rest:
            err("%s: %d files not listed by the harness" % (path, len(rest)))
    msg = open(os.path.join(disc, "US/Message/message.carc"), "rb").read()
    bmg = dict(wiiarc.u8(wiiarc.unpack(msg)))["party_message.bmg"]
    if "G party_message.bmg %d %016X" % (len(bmg), fnv1a64(bmg)) not in lines:
        err("GetMessageResource: %s" % [l for l in lines if l.startswith("G ")])
    if "X exists 1 0" not in lines:
        err("IsExist")
    scenes = [l for l in lines if l.startswith("S ")]
    if len(scenes) != 11:
        err("%d scenes loaded" % len(scenes))
    return fc, fl, arc_files


# --- save --------------------------------------------------------------------

def bmg_text(bmg, group):
    for r in fmt_text_font.bmg_messages(bmg):
        if r["group"] == group and r["index"] == 0:
            return "".join(chr(u) for u in r["codeUnits"])
    raise KeyError(group)


def tpl_images(tpl):
    """[(width, height, format, data offset)] of a TPL."""
    magic, count, table = struct.unpack(">III", tpl[:12])
    out = []
    for i in range(count):
        img, _pal = struct.unpack(">II", tpl[table + i * 8:table + i * 8 + 8])
        h, w, fmt, data = struct.unpack(">HHII", tpl[img:img + 12])
        out.append((w, h, fmt, data))
    return out


def decode_save(data):
    """The stream RPSysSaveData::write produces, read back here (big-endian)."""
    pos = 0

    def take(fmt):
        nonlocal pos
        v = struct.unpack_from(">" + fmt, data, pos)
        pos += struct.calcsize(">" + fmt)
        return v

    sig, ver = take("4s4s")
    old = [take("b6B") for _ in range(3)]
    today, date, games, msgs = take("BHII")
    players = []
    for _ in range(100):
        flags, = take("I")
        cid = take("8B")
        debut, = take("I")
        records = []
        for _g in range(9):
            rec = take("5i")
            medal, = take("B")
            records.append((rec, medal))
        demo, = take("B")
        players.append({"flags": flags, "id": bytes(cid), "debut": debut, "records": records})
    end = pos
    crc, = take("I")
    s = isum = 0
    for i in range(0, end - end % 2, 2):
        v = struct.unpack_from(">H", data, i)[0]
        s = (s + v) & 0xFFFF
        isum = (isum + (~v & 0xFFFF)) & 0xFFFF
    return {"sig": sig, "ver": ver, "today": today, "date": date, "games": games,
            "players": players, "crc": crc, "crc_model": (s << 16) | isum, "length": end + 4}


def save_values(lines, tag):
    """The P lines of one PrintSave: (today, date, open flags), [players]."""
    sysv = None
    players = []
    for line in lines:
        f = line.split()
        if f[0] != tag:
            continue
        if f[1] == "sys":
            sysv = (int(f[3]), int(f[5]), [int(x) for x in f[7:16]], int(f[17]))
        else:
            cid = bytes.fromhex(f[7][2:])
            recs = [tuple(int(x) for x in r.split(":")) for r in f[8:17]]
            players.append((int(f[4]), int(f[6]), cid, recs))
    return sysv, players


def check_save_files(nand, lines, disc, tag):
    home = os.path.join(nand, "title", "00010000", "52484145", "data")
    data = open(os.path.join(home, "RPParty.dat"), "rb").read()
    banner = open(os.path.join(home, "banner.bin"), "rb").read()
    if len(data) != 32768:
        err("RPParty.dat is %d bytes" % len(data))
    if len(banner) != 0x60A0 + 0x1200:
        err("banner.bin is %d bytes" % len(banner))
    if os.listdir(os.path.join(nand, "tmp")):
        err("/tmp not empty after the banner move: %s" % os.listdir(os.path.join(nand, "tmp")))
    s = decode_save(data)
    if s["sig"] != b"RPPT" or s["ver"] != b"0000":
        err("save signature %r %r" % (s["sig"], s["ver"]))
    if s["crc"] != s["crc_model"]:
        err("save checksum %08X, model %08X" % (s["crc"], s["crc_model"]))
    sysv, players = save_values(lines, tag)
    if sysv is None or len(players) != 4:
        err("no %s lines" % tag)
        return s
    today, date, opened, _broken = sysv
    if (s["today"], s["date"]) != (today, date):
        err("system data %r, printed %r" % ((s["today"], s["date"]), (today, date)))
    if [(s["games"] >> g) & 1 for g in range(9)] != opened:
        err("game flags %08X, printed %r" % (s["games"], opened))
    for i, (reg, debut, cid, recs) in enumerate(players):
        p = s["players"][i]
        if (p["debut"], p["id"]) != (debut, cid):
            err("player %d: %r, printed %r" % (i, (p["debut"], p["id"]), (debut, cid)))
        if [(r[0][0], r[1]) for r in p["records"]] != recs:
            err("player %d records differ" % i)
        if ((p["flags"] >> 1) & 1) != reg:
            err("player %d registered flag %X" % (i, p["flags"]))

    magic, flags, speed = struct.unpack(">IIH", banner[:10])
    if magic != 0x5749424E or flags != 0 or speed != 2:
        err("banner header %08X %X %X" % (magic, flags, speed))
    msg = open(os.path.join(disc, "US/Message/message.carc"), "rb").read()
    bmg = dict(wiiarc.u8(wiiarc.unpack(msg)))["party_message.bmg"]
    for off, group in ((0x20, 1001), (0x60, 1002)):
        text = banner[off:off + 64].decode("utf-16-be").split("\0")[0]
        if text != bmg_text(bmg, group)[:32]:
            err("banner text at %X: %r, message %d %r" % (off, text, group, bmg_text(bmg, group)))
    tpl = open(os.path.join(disc, "banner.tpl"), "rb").read()
    imgs = tpl_images(tpl)
    if banner[0xA0:0xA0 + 0x6000] != tpl[imgs[0][3]:imgs[0][3] + 0x6000]:
        err("banner texture differs from banner.tpl image 0")
    if banner[0x60A0:0x60A0 + 0x1200] != tpl[imgs[1][3]:imgs[1][3] + 0x1200]:
        err("banner icon differs from banner.tpl image 1")
    return s


def check_save_flow(lines, first):
    want = ["exist", "memory", "create", "load", "save", "reload"] if first else \
           ["exist", "load", "save", "reload"]
    got = [l.split()[1] for l in lines if l.startswith("N ")]
    if got != want:
        err("save steps %r, expected %r" % (got, want))
    for l in lines:
        f = l.split()
        if f[0] == "N" and (f[5] != "0" or f[3] == "100"):
            err("save step failed: %s" % l)
    p1 = save_values(lines, "P1")
    p2 = save_values(lines, "P2")
    pr = save_values(lines, "PR")
    if p1 != p2:
        err("reloaded data differs from what was saved")
    if pr == p1:
        err("the reset before reloading did not change the data")
    return save_values(lines, "P0")


# --- main --------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True, help="the extracted disc's files/ directory")
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--compare", help="the other build's sys_harness (64/32-bit)")
    args = ap.parse_args()

    disc = os.path.abspath(args.disc)
    scratch = os.path.abspath(args.scratch)
    repo = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    for root, what in ((disc, "the disc"), (repo, "the repository")):
        if os.path.commonpath([scratch, root]) == root:
            sys.exit("--scratch must not be inside %s (save files hold disc data)" % what)
    # Only a directory this test made (it has the marker) is wiped.
    marker = os.path.join(scratch, ".test_sys_scratch")
    if os.path.isdir(scratch) and os.listdir(scratch) and not os.path.exists(marker):
        sys.exit("--scratch %s exists and was not made by this test; not wiping it" % scratch)
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    open(marker, "w").close()

    fst_path = os.path.join(disc, "..", "sys", "fst.bin")
    if not os.path.exists(fst_path):
        sys.exit("no sys/fst.bin next to %s" % disc)
    entries = parse_fst(open(fst_path, "rb").read())

    # Every path of the extracted tree (directories with a trailing '/').
    paths = []
    for dirpath, dirs, names in os.walk(disc):
        dirs.sort()
        rel = os.path.relpath(dirpath, disc)
        if rel != ".":
            paths.append(rel.replace(os.sep, "/") + "/")
        for n in sorted(names):
            paths.append(os.path.relpath(os.path.join(dirpath, n), disc).replace(os.sep, "/"))
    list_path = os.path.join(scratch, "paths.txt")
    with open(list_path, "w") as f:
        f.write("\n".join(paths) + "\n")

    bits = 0
    outputs = {}

    def harness_run(h, name, extra, nand, expect_rc=0):
        lines = run(h, ["--disc", disc, "--nand", os.path.join(scratch, nand), "--list", list_path]
                    + extra, expect_rc)
        outputs.setdefault(h, {})[name] = lines
        return lines

    h = args.harness
    main_lines = harness_run(h, "main", ["--sections", "heap,dvd,res,sc"], "nand-main")
    tail = [l for l in main_lines if l.startswith("sys_harness:")]
    bits = 32 if tail and "pointer size 4" in tail[0] else 64
    nheaps = check_heap(main_lines, bits)
    files, dirs = check_dvd(main_lines, disc, entries, False)
    fc, fl, arc_files = check_res(main_lines, disc)

    # The FST built from the tree must number the paths as the disc does.
    tree_lines = harness_run(h, "tree", ["--sections", "dvd", "--fst-tree"], "nand-tree")
    check_dvd(tree_lines, disc, entries, True)

    # SC: defaults, then a config file.
    want_sc = "SC lang 1 aspect 0 prog 0 eurgb60 1 sound 1 bar 0 dpd 3 motor 1 spk 85 saver 1 offset 0"
    if want_sc not in main_lines or "SC locale 7 language 7 dir US/" not in main_lines:
        err("SC defaults: %s" % [l for l in main_lines if l.startswith("SC")])
    cfg = os.path.join(scratch, "sc.ini")
    with open(cfg, "w") as f:
        f.write("# test settings\nlanguage = fr\naspect = 16:9\nprogressive = 1\n"
                "eurgb60 = 0\nsound = surround  # Dolby Pro Logic II\nsensorbar = top\n"
                "sensitivity = 5\nrumble = 0\nspeaker = 40\nscreensaver = 0\noffset = -8\n")
    sc_lines = harness_run(h, "sc", ["--sections", "sc", "--sc", cfg], "nand-sc")
    want = ["SC lang 3 aspect 1 prog 1 eurgb60 0 sound 2 bar 1 dpd 5 motor 0 spk 40 saver 0 offset -8",
            "SC locale 7 language 1 dir US/"]
    for w in want:
        if w not in sc_lines:
            err("SC config: %s, expected %s" % ([l for l in sc_lines if l.startswith("SC")], w))
    bad = os.path.join(scratch, "bad.ini")
    with open(bad, "w") as f:
        f.write("aspect = 21:9\n")
    run(h, ["--disc", disc, "--nand", os.path.join(scratch, "nand-sc"), "--sections", "sc",
            "--sc", bad], expect_rc=2)

    # Save: a fresh NAND (create, save, load), then the same NAND again.
    s1 = harness_run(h, "save1", ["--sections", "save", "--save-values", "7"], "nand-save")
    check_save_flow(s1, True)
    check_save_files(os.path.join(scratch, "nand-save"), s1, disc, "P2")
    first_files = {n: open(os.path.join(scratch, "nand-save/title/00010000/52484145/data", n),
                           "rb").read() for n in ("RPParty.dat", "banner.bin")}
    s2 = harness_run(h, "save2", ["--sections", "save", "--save-values", "9"], "nand-save")
    p0 = check_save_flow(s2, False)
    if p0 != save_values(s1, "P2"):
        err("the second run did not load the first run's save")
    check_save_files(os.path.join(scratch, "nand-save"), s2, disc, "P2")

    compared = ""
    if args.compare and os.path.exists(args.compare):
        o = args.compare
        other = {}
        other["main"] = run(o, ["--disc", disc, "--nand", os.path.join(scratch, "nand-o"),
                                "--list", list_path, "--sections", "heap,dvd,res,sc"])
        other["save1"] = run(o, ["--disc", disc, "--nand", os.path.join(scratch, "nand-osave"),
                                 "--list", list_path, "--sections", "save", "--save-values", "7"])

        def strip(lines):
            # Heap addresses depend on the pointer size (EGG / MEM headers).
            return [l for l in lines if l.split(" ")[0] not in ("A", "H", "C", "X", "F")
                    and not l.startswith("sys_harness:")]
        for name in ("main", "save1"):
            a, b = strip(outputs[h][name]), strip(other[name])
            if a != b:
                diff = [(x, y) for x, y in zip(a, b) if x != y][:3]
                err("%s: the other build prints differently (%d / %d lines): %r" % (
                    name, len(a), len(b), diff))
        for n, data in first_files.items():
            o_data = open(os.path.join(scratch, "nand-osave/title/00010000/52484145/data", n),
                          "rb").read()
            if o_data != data:
                err("%s differs between the 64- and 32-bit builds" % n)
        compared = ", compared with %s" % os.path.basename(os.path.dirname(o))

    print("sys_harness: %d heaps; FST %d files %d directories (sys/fst.bin and the tree agree); "
          "resources: %d cached, %d expanded, %d archive files; save written, reloaded and "
          "reloaded by a second run; SC defaults and config%s" % (
              nheaps, files, dirs, fc, fl, arc_files, compared))
    print("errors: %d" % len(errors))
    print("OK" if not errors else "FAILED")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
