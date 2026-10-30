#!/usr/bin/env python3
"""Compare snd_harness (decompiled nw4r::snd sound-archive code on the host)
with a reading of the file bytes, cross-checked against
port/assets/tools/fmt_sound.py.

Runs port/host/build/*/snd_harness on the extracted disc and builds the same
lines from the bytes of every .brsar (disc-level ones opened by the harness
through DvdSoundArchive, the HOME Menu one inside an archive through
MemorySoundArchive):

  A   archive: player count, GetGroupCount() (the INFO group table minus its
      last entry), the SoundArchivePlayerInfo counts
  S   every sound: label, file, player, priority, volume, remote filter, pan
      mode and curve (the version < 1.2 defaults), type, user parameter, the
      SEQ / WAVE / STRM info and the 3D parameters
  B   every bank (name through its stringId), P every player, G every group
      with GI items, F every file with its positions (groupId:index)
  L   every group the game can load (0 .. GetGroupCount() - 1); empty ones
      are not loaded
  X   every file as SoundArchivePlayer::detail_GetFileAddress finds it after
      the groups are loaded (first position in a loaded group for a DVD
      archive; position 0 in place for a memory archive)
  Q   RSEQ: version, SeqFileReader::GetBaseAddress offset
  K   RBNK: version, instrument and wave counts; KI every program x key with
      velocity runs (BankFileReader::ReadInstInfo's region walk: range and
      index tables, key then velocity); KW every wave (ReadWaveParam)
  W   RWSD: wave sound and wave counts; WS / WN / WW the wave sound info,
      note infos and waves
  R   the head of each file's wave data, and whether it is an RWAR

The RSAR INFO and SYMB blocks and the embedded files are read here from the
format (struct layouts as documented in port/assets/formats/sound.md and the
nw4r::snd headers; the reading rules -- version defaults, region walk, sample
arithmetic -- are written out, not translated). fmt_sound.py (read-only) is a
second opinion: every archive and embedded file it parses must agree on the
fields both readers have; when it raises (the parser is being extended) that
is reported as a note, not a failure.

    python3 test_sound.py --harness build/host64/snd_harness --disc DISC
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import wiiarc  # noqa: E402
try:
    import fmt_sound  # noqa: E402  (read-only second opinion)
except Exception as error:  # pragma: no cover - depends on the worker's file
    fmt_sound = None
    FMT_IMPORT_ERROR = str(error)

NOTES = []


def u8(d, o):
    return d[o]


def u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def s16(d, o):
    return struct.unpack_from(">h", d, o)[0]


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def s32(d, o):
    return struct.unpack_from(">i", d, o)[0]


def f32(d, o):
    return struct.unpack_from(">f", d, o)[0]


def g(f):
    return "%.9g" % f


def ref(d, o, base):
    """Util::DataRef at o: the target offset, or None (address 0)."""
    ref_type, _data_type, _res, value = struct.unpack_from(">BBHI", d, o)
    if ref_type == 1:
        return base + value
    if ref_type == 0 and value == 0:
        return None
    raise ValueError("DataRef at 0x%x: refType %d value 0x%x" % (o, ref_type, value))


def ref_type_of(d, o):
    return d[o + 1]


def table(d, o, base):
    """Util::Table<DataRef<...>>: list of target offsets (None for NULL)."""
    return [ref(d, o + 4 + 8 * i, base) for i in range(u32(d, o))]


def cstr(d, o):
    return d[o:d.index(b"\0", o)].decode("ascii")


# --- RSAR -------------------------------------------------------------------

def read_rsar(d):
    version = u16(d, 6)
    symb, _ssize, info, _isize = struct.unpack_from(">IIII", d, 0x10)
    sb = symb + 8
    tab, *trees = struct.unpack_from(">5I", d, sb)
    strings = [cstr(d, sb + u32(d, sb + tab + 4 + 4 * i)) for i in range(u32(d, sb + tab))]

    def tree(off):
        names = {}
        if off == 0:
            return names
        t = sb + off
        for i in range(u32(d, t + 4)):
            flags, _bit, _l, _r, str_idx, node_id = struct.unpack_from(">HHIIII", d, t + 8 + 20 * i)
            if flags & 1:
                names[node_id] = strings[str_idx]
        return names

    sound_names, player_names, group_names, bank_names = (tree(t) for t in trees)
    ib = info + 8

    def string(sid):
        return None if sid == 0xFFFFFFFF else strings[sid]

    r = {"version": version, "strings": strings, "names": {
        "sound": sound_names, "player": player_names, "group": group_names, "bank": bank_names}}

    sounds = []
    for o in table(d, ref(d, ib, ib), ib):
        sid, fid, pid = struct.unpack_from(">III", d, o)
        p3 = ref(d, o + 0xC, ib)
        s = {"label": string(sid), "fileId": fid, "playerId": pid,
             "volume": d[o + 0x14], "playerPriority": d[o + 0x15], "soundType": d[o + 0x16],
             "remoteFilter": d[o + 0x17], "info": ref(d, o + 0x18, ib),
             "user": u32(d, o + 0x20), "panMode": d[o + 0x28], "panCurve": d[o + 0x29],
             "p3d": None if p3 is None else (u32(d, p3), d[p3 + 4], d[p3 + 5])}
        sounds.append(s)
    r["sounds"] = sounds
    r["banks"] = [(string(u32(d, o)), u32(d, o + 4)) for o in table(d, ref(d, ib + 8, ib), ib)]
    r["players"] = [(u32(d, o), d[o + 4], u32(d, o + 8)) for o in table(d, ref(d, ib + 16, ib), ib)]
    files = []
    for o in table(d, ref(d, ib + 24, ib), ib):
        ext = ref(d, o + 0xC, ib)
        pos = []
        for p in table(d, ref(d, o + 0x14, ib), ib):
            pos.append(None if p is None else (u32(d, p), u32(d, p + 4)))
        files.append({"size": u32(d, o), "wsize": u32(d, o + 4),
                      "ext": None if ext is None else cstr(d, ext), "pos": pos})
    r["files"] = files
    groups = []
    for o in table(d, ref(d, ib + 32, ib), ib):
        ext = ref(d, o + 8, ib)
        items = [struct.unpack_from(">5I", d, it) for it in table(d, ref(d, o + 0x20, ib), ib)]
        groups.append({"stringId": u32(d, o), "ext": None if ext is None else cstr(d, ext),
                       "off": u32(d, o + 0x10), "size": u32(d, o + 0x14),
                       "woff": u32(d, o + 0x18), "wsize": u32(d, o + 0x1C), "items": items})
    r["groups"] = groups
    r["playerInfo"] = struct.unpack_from(">7H", d, ref(d, ib + 40, ib))
    return r


def sound_line(arc, r, d, i, s):
    ver = r["version"]
    pan = (s["panMode"], s["panCurve"]) if ver >= 0x0102 else (1, 0)  # BALANCE, SQRT
    stype = s["soundType"] if ver >= 0x0101 else None
    stype = stype if stype in (1, 2, 3) else 0
    line = ("S %s %d %s file=%d player=%d prio=%d vol=%d rf=%d pan=%d,%d type=%d user=%d"
            % (arc, i, s["label"] or "-", s["fileId"], s["playerId"], s["playerPriority"],
               s["volume"], s["remoteFilter"], pan[0], pan[1], stype, s["user"]))
    o = s["info"]
    if stype == 1:
        rel = d[o + 0xD] if ver >= 0x0103 else 0
        line += " seq=%d,%d,%d,%d,%d" % (u32(d, o), u32(d, o + 4), u32(d, o + 8), d[o + 0xC],
                                         1 if rel else 0)
    elif stype == 3:
        rel = d[o + 9] if ver >= 0x0103 else 0
        line += " wsd=%d,%d,%d" % (s32(d, o), d[o + 8], 1 if rel else 0)
    elif stype == 2:
        line += " strm=%d" % (1 if o is not None else 0)
    if s["p3d"] is not None:
        line += " 3d=%d,%d,%d" % s["p3d"]
    return line


# --- embedded files -----------------------------------------------------------

def sample_by_dsp_address(addr, fmt):
    """AxVoice::GetSampleByDspAddress(NULL, addr, fmt) for a file format."""
    if fmt == 2:  # ADPCM: 16 nibbles (8 bytes) per frame of 14 samples, 2 header nibbles
        s = addr
        return ((s % 16) + (s // 16) * 14 - 2) & 0xFFFFFFFF
    return addr & 0xFFFFFFFF


def wave_param(d, w, tag, arc, fid, idx):
    """WaveFileReader::ReadWaveParam on the WaveInfo at w (offsets into d)."""
    fmt, loop, ch, rate24 = d[w], d[w + 1], d[w + 2], d[w + 3]
    rate = (rate24 << 16) + u16(d, w + 4)
    ls, le, ctab, doff = u32(d, w + 8), u32(d, w + 12), u32(d, w + 16), u32(d, w + 20)
    line = "%s %s %d %d 0 fmt=%d loop=%d ch=%d rate=%d ls=%d le=%d" % (
        tag, arc, fid, idx, fmt, loop, ch, rate, sample_by_dsp_address(ls, fmt),
        (sample_by_dsp_address(le, fmt) + 1) & 0xFFFFFFFF)
    for c in range(ch):
        ci = w + u32(d, w + ctab + 4 * c)  # the channel offsets are read from +0, no count
        cdo, aoff, vfl, vfr, vrl, vrr = struct.unpack_from(">6I", d, ci)
        line += " [%d %d %d %d %d" % (doff + cdo, vfl, vfr, vrl, vrr)
        if fmt == 2:
            if aoff:
                a = w + aoff
                line += " c0=%d gain=%d ps=%d" % (s16(d, a), u16(d, a + 0x20), u16(d, a + 0x22))
            else:
                line += " c0=0 gain=0 ps=0"
        line += "]"
    return line


def bank_lines(arc, fid, d):
    lines = []
    ver = u16(d, 6)
    data_off, _ds, wave_off, _ws = struct.unpack_from(">IIII", d, 0x10)
    ib = data_off + 8  # &instTable: the DataRef base
    wb = wave_off + 8
    ninst, nwave = u32(d, ib), u32(d, wb)
    lines.append("K %s %d ver=%04x insts=%d waves=%d" % (arc, fid, ver, ninst, nwave))

    def region(o):
        return (d[o], d[o + 1], u32(d, o + 4))  # refType, dataType, value

    def sub(o, split):
        """GetReferenceToSubRegion: the DataRegion offset for split, or None."""
        ref_type, dtype, value = region(o)
        if dtype == 0:
            return None
        if dtype == 1:
            return o
        if ref_type != 1:
            if value == 0:
                return None
            raise ValueError("address region")
        t = ib + value
        if dtype == 2:
            n = d[t]
            i = 0
            while split > d[t + 1 + i]:
                i += 1
                if i >= n:
                    return None
            return t + i * 8 + ((n + 1 + 3) & ~3)
        if dtype == 3:
            lo, hi = d[t], d[t + 1]
            if split < lo or split > hi:
                return None
            return t + 4 + (split - lo) * 8
        return None

    cache = {}

    def param(o):
        if o is None:
            return "none"
        if o in cache:
            return cache[o]
        ref_type, dtype, value = region(o)
        result = "none"
        if dtype == 1 and ref_type == 1:
            p = ib + value
            wi = s32(d, p)
            if wi >= 0:
                vol, tune = (d[p + 0xD], f32(d, p + 0x10)) if ver >= 0x0101 else (127, 1.0)
                result = "w=%d adsr=%d,%d,%d,%d key=%d pan=%d vol=%d tune=%s" % (
                    wi, d[p + 4], d[p + 5], d[p + 6], d[p + 7], d[p + 0xC], d[p + 0xE], vol, g(tune))
        cache[o] = result
        return result

    vel_cache = {}
    for prg in range(ninst):
        top = ib + 4 + prg * 8
        for key in range(128):
            r = top
            dtype = d[r + 1]
            if dtype == 4:
                continue
            if dtype != 1:
                r = sub(r, key)
                if r is None or d[r + 1] == 4:
                    continue
            if d[r + 1] == 1:
                results = [param(r)] * 128
            else:
                if r not in vel_cache:
                    vel_cache[r] = [param(x) if x is not None and d[x + 1] == 1 else "none"
                                    for x in (sub(r, v) for v in range(128))]
                results = vel_cache[r]
            start = 0
            for v in range(1, 129):
                if v == 128 or results[v] != results[start]:
                    if results[start] != "none":
                        lines.append("KI %s %d %d %d %d-%d %s" % (arc, fid, prg, key, start, v - 1,
                                                                  results[start]))
                    start = v
    for w in range(nwave):
        lines.append(wave_param(d, ref(d, wb + 4 + w * 8, wb), "KW", arc, fid, w))
    return lines, {"insts": ninst, "waves": nwave}


def wsd_lines(arc, fid, d):
    lines = []
    ver = u16(d, 6)
    data_off, _ds, wave_off, _ws = struct.unpack_from(">IIII", d, 0x10)
    base = data_off + 8  # &wsdCount
    nwsd = u32(d, base)
    nwave = u32(d, wave_off + 8)
    lines.append("W %s %d ver=%04x wsds=%d waves=%d" % (arc, fid, ver, nwsd, nwave))
    notes_all = []
    for i in range(nwsd):
        wsd = ref(d, base + 4 + 8 * i, base)
        info = ref(d, wsd, base)
        notes = table(d, ref(d, wsd + 16, base), base)
        if ver == 0x0102:
            v = (g(f32(d, info)), d[info + 4], d[info + 5], d[info + 6], d[info + 7], d[info + 8],
                 d[info + 9])
        elif ver == 0x0101:
            v = (g(f32(d, info)), d[info + 4], d[info + 5], 0, 0, 0, 127)
        else:
            v = ("1", 64, 0, 0, 0, 0, 127)
        lines.append("WS %s %d %d pitch=%s pan=%d span=%d fx=%d,%d,%d main=%d notes=%d"
                     % ((arc, fid, i) + v + (len(notes),)))
        for n, o in enumerate(notes):
            pan, span, pitch = ((d[o + 0xE], d[o + 0xF], g(f32(d, o + 0x10))) if ver >= 0x0101
                                else (64, 0, "1"))
            lines.append("WN %s %d %d %d w=%d adsr=%d,%d,%d,%d key=%d vol=%d pan=%d span=%d pitch=%s"
                         % (arc, fid, i, n, s32(d, o), d[o + 4], d[o + 5], d[o + 6], d[o + 7],
                            d[o + 0xC], d[o + 0xD], pan, span, pitch))
            notes_all.append((s32(d, o), d[o + 4], d[o + 5], d[o + 6], d[o + 7], d[o + 0xC],
                              d[o + 0xD], d[o + 0xE], d[o + 0xF]))
    for w in range(nwave):
        if ver == 0x0100:
            wi = wave_off + u32(d, wave_off + 8 + 4 * w)
        else:
            wi = wave_off + u32(d, wave_off + 12 + 4 * w)
        lines.append(wave_param(d, wi, "WW", arc, fid, w))
    return lines, {"wsds": nwsd, "waves": nwave, "notes": notes_all}


# --- one archive ----------------------------------------------------------------

def archive_lines(arc, d, memory):
    r = read_rsar(d)
    out = []
    groups, files = r["groups"], r["files"]
    pi = r["playerInfo"]
    out.append("A %s players=%d groups=%d seq=%d,%d strm=%d,%d,%d wave=%d,%d"
               % ((arc, len(r["players"]), len(groups) - 1) + tuple(pi)))
    for i, s in enumerate(r["sounds"]):
        out.append(sound_line(arc, r, d, i, s))
    for i, (name, fid) in enumerate(r["banks"]):
        out.append("B %s %d %s file=%d" % (arc, i, name or "-", fid))
    for i, (_sid, count, heap) in enumerate(r["players"]):
        out.append("P %s %d %s sounds=%d heap=%d" % (arc, i, r["names"]["player"].get(i, "-"),
                                                     count, heap))
    for i, gr in enumerate(groups):
        out.append("G %s %d %s items=%d ext=%s off=%d size=%d woff=%d wsize=%d"
                   % (arc, i, r["names"]["group"].get(i, "-"), len(gr["items"]), gr["ext"] or "-",
                      gr["off"], gr["size"], gr["woff"], gr["wsize"]))
        for j, it in enumerate(gr["items"]):
            out.append("GI %s %d %d file=%d off=%d size=%d woff=%d wsize=%d" % ((arc, i, j) + it))
    for i, f in enumerate(files):
        pos = " ".join("-" if p is None else "%d:%d" % p for p in f["pos"])
        out.append(("F %s %d size=%d wsize=%d ext=%s pos=%d" % (
            arc, i, f["size"], f["wsize"], f["ext"] or "-", len(f["pos"]))) + (" " + pos if pos else ""))

    loaded = set()
    for i in range(len(groups) - 1):
        if groups[i]["size"] == 0 and groups[i]["wsize"] == 0:
            out.append("L %s %d empty" % (arc, i))
        else:
            out.append("L %s %d ok=1" % (arc, i))
            loaded.add(i)

    embedded = {}
    for fid, f in enumerate(files):
        where = None
        candidates = f["pos"][:1] if memory else f["pos"]
        for p in candidates:
            if p is None or p[0] >= len(groups) or p[1] >= len(groups[p[0]]["items"]):
                continue
            gr = groups[p[0]]
            if gr["ext"] is not None or (not memory and p[0] not in loaded):
                continue
            it = gr["items"][p[1]]
            wave = None
            if memory or gr["wsize"] != 0:
                wave = gr["woff"] + it[3]
            where = (gr["off"] + it[1], wave)
            break
        if where is None:
            out.append("X %s %d none" % (arc, fid))
            continue
        fo, wo = where
        blob = d[fo:fo + u32(d, fo + 8)]
        wave = d[wo:] if wo is not None else None
        sig = blob[:4].decode("ascii")
        out.append("X %s %d %s size=%d wave=%s" % (arc, fid, sig, u32(d, fo + 8),
                                                     "yes" if wave is not None else "no"))
        if sig == "RSEQ":
            out.append("Q %s %d ver=%04x base=%d" % (arc, fid, u16(blob, 6),
                                                     u32(blob, 0x10) + u32(blob, u32(blob, 0x10) + 8)))
            embedded[fid] = ("RSEQ", blob, {"base": u32(blob, u32(blob, 0x10) + 8)})
        elif sig == "RBNK":
            lines, facts = bank_lines(arc, fid, blob)
            out += lines
            embedded[fid] = ("RBNK", blob, facts)
        elif sig == "RWSD":
            lines, facts = wsd_lines(arc, fid, blob)
            out += lines
            embedded[fid] = ("RWSD", blob, facts)
        if wave is not None and f["wsize"] != 0:
            h = wave[:16]
            rwar = (h[:4] == b"RWAR" and u16(h, 4) == 0xFEFF and u16(h, 6) == 0x0100
                    and u32(h, 8) >= 16 + 8 and u16(h, 14) >= 1)
            out.append("R %s %d rwar=%d head=%s" % (arc, fid, 1 if rwar else 0, h[:8].hex()))
    return out, r, embedded


# --- fmt_sound.py as a second opinion --------------------------------------------

def cross_check(arc, d, r, embedded, errors):
    if fmt_sound is None:
        NOTES.append("fmt_sound.py not importable: " + FMT_IMPORT_ERROR)
        return 0
    checks = 0
    try:
        p = fmt_sound.parse(d)
    except Exception as error:
        NOTES.append("fmt_sound.py could not parse %s (%s); own reader only" % (arc, error))
        return 0
    info = p["infoBlock"]

    def same(what, a, b):
        nonlocal checks
        checks += 1
        if a != b:
            errors.append("fmt_sound.py disagrees on %s %s: %r vs %r" % (arc, what, a, b))

    same("sound count", len(info["sounds"]), len(r["sounds"]))
    for i, (a, b) in enumerate(zip(info["sounds"], r["sounds"])):
        same("sound %d" % i, (a["fileId"], a["playerId"], a["volume"], a["playerPriority"],
                              a["soundType"], a["remoteFilter"], a["userParam"][0],
                              a["panMode"], a["panCurve"]),
             (b["fileId"], b["playerId"], b["volume"], b["playerPriority"], b["soundType"],
              b["remoteFilter"], b["user"], b["panMode"], b["panCurve"]))
        # Not compared: fmt_sound.py's typeInfo (SEQ dataOffset, WAVE subNo) is
        # read at SoundCommonInfo + 0x30 instead of through soundInfoRef, so it
        # is wrong for every sound (port/docs/HOST_BUILD.md §13).
    same("banks", [x["fileId"] for x in info["banks"]], [x[1] for x in r["banks"]])
    same("players", [(x["playableSoundCount"], x["heapSize"]) for x in info["players"]],
         [(x[1], x[2]) for x in r["players"]])
    same("files", [(x["fileSize"], x["waveDataSize"]) for x in info["files"]],
         [(x["size"], x["wsize"]) for x in r["files"]])
    same("file positions",
         [[None if q is None else (q["groupId"], q["index"]) for q in x["positions"]]
          for x in info["files"]], [x["pos"] for x in r["files"]])
    same("groups", [(x["offset"], x["size"], x["waveDataOffset"], x["waveDataSize"],
                     [tuple(it[k] for k in ("fileId", "offset", "size", "waveDataOffset",
                                            "waveDataSize")) for it in x["items"]])
                    for x in info["groups"]],
         [(x["off"], x["size"], x["woff"], x["wsize"], [tuple(it) for it in x["items"]])
          for x in r["groups"]])
    same("player info", tuple(info["soundArchivePlayerInfo"][k] for k in (
        "seqSoundCount", "seqTrackCount", "strmSoundCount", "strmTrackCount", "strmChannelCount",
        "waveSoundCount", "waveTrackCount")), tuple(r["playerInfo"]))
    same("string count", p["symbolBlock"]["stringCount"], len(r["strings"]))
    for kind in ("sound", "player", "group", "bank"):
        tree = p["symbolBlock"][kind + "Tree"]["nodeTable"]["items"]
        same(kind + " tree ids", sorted(n["id"] for n in tree if n["flags"] & 1),
             sorted(r["names"][kind]))

    for fid, (kind, blob, facts) in sorted(embedded.items()):
        try:
            e = fmt_sound.parse(blob)
        except Exception as error:
            NOTES.append("fmt_sound.py could not parse %s file %d %s (%s)" % (arc, fid, kind, error))
            continue
        if kind == "RSEQ":
            same("file %d RSEQ baseOffset" % fid, e["dataBlock"]["baseOffset"], facts["base"])
        elif kind == "RBNK":
            same("file %d RBNK counts" % fid, (e["dataBlock"]["count"], e["waveBlock"]["count"]),
                 (facts["insts"], facts["waves"]))
        elif kind == "RWSD":
            same("file %d RWSD counts" % fid, (e["dataBlock"]["wsdCount"], e["waveBlock"]["waveCount"]),
                 (facts["wsds"], facts["waves"]))
            notes = [(n["waveIndex"], n["attack"], n["decay"], n["sustain"], n["release"],
                      n["originalKey"], n["volume"], n["pan"], n["surroundPan"])
                     for w in e["dataBlock"]["wsds"] for n in w["notes"]]
            same("file %d RWSD notes" % fid, notes, facts["notes"])
    return checks


def expected_lines(disc):
    lines, errors = [], []
    narch = nchecks = 0
    for path, blob in wiiarc.walk(disc):
        if blob[:4] != b"RSAR":
            continue
        memory = ".carc/" in path
        out, r, embedded = archive_lines(path, blob, memory)
        lines += out
        narch += 1
        nchecks += cross_check(path, blob, r, embedded, errors)
    return lines, narch, nchecks, errors


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    got = run.stdout.splitlines()
    expected, narch, nchecks, errors = expected_lines(args.disc)

    def kind(line):
        return line.split(" ", 1)[0]

    kinds = ("A", "S", "B", "P", "G", "GI", "F", "L", "X", "Q", "K", "KI", "KW", "W", "WS",
             "WN", "WW", "R")
    kg = collections.Counter(kind(x) for x in got)
    ke = collections.Counter(kind(x) for x in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print("archives: %d (RSAR); fmt_sound.py cross-checks: %d, disagreements %d"
          % (narch, nchecks, len(errors)))
    print("lines: " + ", ".join("%s %d/%d" % (k, kg[k], ke[k]) for k in kinds)
          + " (harness/python)")
    print("missing %d, extra %d" % (len(missing), len(extra)))
    for line in missing[:10]:
        print("  missing:", line)
    for line in extra[:10]:
        print("  extra:  ", line)
    for e in errors[:10]:
        print("  " + e)
    # fmt_sound.py parse failures, grouped by reason (the parser is in flux).
    import re
    reasons = collections.Counter(re.sub(r"\d+", "N", n.split(" (", 1)[-1]) for n in NOTES)
    for reason, count in sorted(reasons.items()):
        print("  note: fmt_sound.py could not parse %d file(s): (%s" % (count, reason))

    ok = run.returncode == 0 and not missing and not extra and not errors and narch > 0
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
