#!/usr/bin/env python3
"""Checks audio_harness: the nw4r::snd runtime on the host AX mixer
(port/docs/HOST_BUILD.md §21).

Modelled, bit for bit, from the archive bytes (port/assets/tools/fmt_sound.py,
dspadpcm.py) and an independent model of the DSP:

  * Voice identity: every note a voice starts (an N line) begins at sample 0
    of a wave of the file the sound plays (its RWSD, or its bank's RBNK), in
    that wave's format.
  * Decoding: for every voice run in the taps, the accelerator output (the
    decoded samples, in read order) equals dspadpcm.py's decode of that wave
    channel (DSP-ADPCM / PCM16 / PCM8), the loop section repeated for looping
    waves, followed by zeros in the frame where a one-shot wave ends.
  * Sample-rate conversion: the 4-tap polyphase filter over those samples,
    with each frame's ratio and coefficient table (gen_ax_coefs.py: the
    decomp's 12 kHz table, the generated 8 / 16 kHz ones), from the PB's
    history and fraction.
  * Volume envelope, LPF, and the mix into the main L/R buses through the
    master volume, for every frame of the single-wave-sound renders (without
    effects).
  * With the SRC switched off (--src-none), a voice's output is the decode
    times its envelope: sat((x * v) >> 15).

Regression only: which voices start when (N/E/C lines), the voice counts and
the rendered output's hash for a set of sequence and wave-sound scenarios
(tests/audio_expected.txt; --update rewrites it). Determinism: every scenario
twice (output and taps identical), and the other build's harness (--compare)
must print the same events and render the same samples (only the physical
addresses of the I lines may differ: the heap layout does).

    python3 test_audio.py --harness build/host64/audio_harness --disc DISC
        [--compare build/host32/audio_harness] [--wav-dir DIR] [--update]

--wav-dir writes each scenario's render as a WAV there (outside the repo).
"""

import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "port", "assets", "tools"))
sys.path.insert(0, os.path.join(REPO, "port", "host", "tools"))

import dspadpcm  # noqa: E402
import fmt_sound  # noqa: E402
import gen_ax_coefs  # noqa: E402

EXPECTED = os.path.join(HERE, "audio_expected.txt")
FRAME = 96
PB_SIZE = 0x140

# Scenarios: name -> (archive, sounds (label or id, @game frame), AX frames, flags)
BIL = "Sound/RPBilScene/rp_Bil_sound.brsar"
BOM = "Sound/RPBomScene/rp_Bom_sound.brsar"
COW = "Sound/RPCowScene/rp_Cow_sound.brsar"
COMMON = "Sound/RPPartyCommon/rp_Common_Party.brsar"
DUC = "Sound/RPDucScene/rp_Duc_sound.brsar"
SCENARIOS = [
    # One wave sound at a time, tapped: the decode / SRC / envelope / mix models.
    ("bil-wave-native", BIL, ["RP_BIL_SE_TURN_DECIDE"], 220, ["tap"]),
    ("bil-wave-pitch", BIL, ["RP_BIL_SE_WINDOWCHANGE"], 120, ["tap"]),
    ("bom-wave-pan", BOM, ["RP_BOM_SE_GENTEN_1"], 140, ["tap"]),
    ("bil-wave-nosrc", BIL, ["RP_BIL_SE_TURN_DECIDE"], 220, ["tap", "--src-none"]),
    ("cow-wave-loop", COW, ["148"], 3300, ["tap"]),
    # Several wave sounds, staggered.
    ("bil-waves", BIL, ["0", "1@10", "2@20", "5@30", "3@45", "4@60"], 600, ["tap"]),
    # Sequences (bank instruments: PCM16 and DSP-ADPCM waves, envelopes, pitch).
    ("common-seqs", COMMON, ["RP_COMMON_BGM_PARTY_MENU", "RP_COMMON_SE_WIN_OPEN_01@30",
                             "RP_COMMON_SE_MESS_10@60", "RP_COMMON_SE_KEY_A_01@90",
                             "RP_COMMON_SE_LOGO_01@120"], 2000, ["tap"]),
    # With the game's default effects (--fx: reverb / chorus / delay on aux A / B / C,
    # the decomp's AXFX on the aux buses).
    ("duc-seqs-fx", DUC, ["RP_DUC_BGM_START", "RP_DUC_BGM_COMBO_HIT@200",
                          "RP_DUC_BGM_FANFARE_WIN@400"], 3000, ["tap", "--fx"]),
    ("common-sleep-fx", COMMON, ["RP_COMMON_BGM_PARTY_MENU_SLEEP"], 3000, ["tap", "--fx"]),
    # Long renders, untapped: regression and determinism only.
    ("common-bgm-fx", COMMON, ["RP_COMMON_BGM_PARTY_MENU"], 6000, ["--fx"]),
    ("common-wave-bgm-fx", COMMON, ["RP_COMMON_BGM_PARTY_01"], 3000, ["--fx"]),
]

# AXPB field offsets (include/revolution/AX/AXPB.h), host byte order.
PB = {
    "srcSelect": 0x08, "coefSelect": 0x0A, "mixerCtrl": 0x0C, "state": 0x10, "type": 0x12,
    "mix": 0x14, "dpop": 0x52, "ve": 0x6A, "addr": 0x6E, "adpcm": 0x7E, "src": 0xA6,
    "adpcmLoop": 0xB4, "lpf": 0xBA, "biquad": 0xC2,
}

errors = []


def fail(msg):
    errors.append(msg)
    if len(errors) <= 30:
        print("ERROR: " + msg)


def sat16(v):
    return 32767 if v > 32767 else (-32768 if v < -32768 else v)


def u16(raw, off):
    return struct.unpack_from("<H", raw, off)[0]


def s16(raw, off):
    return struct.unpack_from("<h", raw, off)[0]


def pb_info(raw):
    a = PB["addr"]
    s = PB["src"]
    return {
        "srcSelect": u16(raw, PB["srcSelect"]),
        "coefSelect": u16(raw, PB["coefSelect"]),
        "mixerCtrl": struct.unpack_from("<I", raw, PB["mixerCtrl"])[0],
        "state": u16(raw, PB["state"]),
        "type": u16(raw, PB["type"]),
        "mix": [u16(raw, PB["mix"] + 2 * i) for i in range(24)],
        "ve": (u16(raw, PB["ve"]), s16(raw, PB["ve"] + 2)),
        "loopFlag": u16(raw, a),
        "format": u16(raw, a + 2),
        "loop": u16(raw, a + 4) << 16 | u16(raw, a + 6),
        "end": u16(raw, a + 8) << 16 | u16(raw, a + 10),
        "cur": u16(raw, a + 12) << 16 | u16(raw, a + 14),
        "ratio": u16(raw, s) << 16 | u16(raw, s + 2),
        "frac": u16(raw, s + 4),
        "last": [s16(raw, s + 6 + 2 * i) for i in range(4)],
        "lpf": [u16(raw, PB["lpf"] + 2 * i) for i in range(4)],
        "biquadOn": u16(raw, PB["biquad"]),
    }


def read_tap(path):
    data = open(path, "rb").read()
    recs = []
    p = 0
    while p < len(data):
        magic, frame, voice, n = struct.unpack_from("<4I", data, p)
        if magic != 0x50415456:
            raise ValueError("bad tap record at %d" % p)
        p += 16
        before = pb_info(data[p:p + PB_SIZE])
        after = pb_info(data[p + PB_SIZE:p + 2 * PB_SIZE])
        p += 2 * PB_SIZE
        inp = struct.unpack_from("<%dh" % n, data, p)
        p += 2 * n
        src = struct.unpack_from("<96h", data, p)
        p += 192
        out = struct.unpack_from("<96h", data, p)
        p += 192
        recs.append({"frame": frame, "voice": voice, "before": before, "after": after,
                     "input": inp, "src": src, "out": out})
    return recs


def run(harness, disc, archive, sounds, frames, flags, tmp, wav_dir, name):
    cmd = [harness, "--disc", disc, "--frames", str(frames)]
    tap = None
    if "tap" in flags:
        tap = os.path.join(tmp, name + ".tap")
        cmd += ["--tap", tap]
    wav = os.path.join(wav_dir if wav_dir else tmp, name + ".wav")
    cmd += ["--wav", wav]
    cmd += [f for f in flags if f.startswith("--")]
    cmd += [archive] + sounds
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return p.returncode, p.stdout, p.stderr, tap, wav


def read_wav(path):
    data = open(path, "rb").read()
    assert data[:4] == b"RIFF" and data[36:40] == b"data"
    n = struct.unpack_from("<I", data, 40)[0] // 2
    return struct.unpack_from("<%dh" % n, data, 44)


# --------------------------------------------------------------- archive side

class Archive:
    """The waves of an archive's files, from fmt_sound.py / dspadpcm.py."""

    def __init__(self, disc, rel):
        self.data = open(os.path.join(disc, rel), "rb").read()
        parsed = fmt_sound.parse(self.data)
        diag_errors = []
        self.decoded = {w["name"]: w for w in dspadpcm.iter_brsar(self.data, diag_errors, rel)}
        if diag_errors:
            fail("%s: dspadpcm: %s" % (rel, diag_errors[0]))
        self.files = {}
        for emb in parsed["embedded"]:
            sub = emb["parsed"]
            if sub is None or sub["format"] not in ("RBNK", "RWSD"):
                continue
            if emb["fileId"] in self.files:
                continue
            kind = sub["format"]
            waves = sub["waveBlock"]["items"] if kind == "RBNK" else sub["waveBlock"]["waves"]
            self.files[emb["fileId"]] = (kind, waves, sub)

    def wave_starts(self, file_id, phys):
        """DSP address of sample 0 of each channel of each wave -> (wave name, channel, format)."""
        out = {}
        if file_id not in self.files:
            return out
        kind, waves, _ = self.files[file_id]
        for index, w in enumerate(waves):
            for ch, c in enumerate(w["channels"]):
                byte = phys + w["dataOffset"] + c["channelDataOffset"]
                fmt = w["format"]
                if fmt == dspadpcm.DSP_ADPCM:
                    addr, axfmt = byte * 2 + 2, 0
                elif fmt == dspadpcm.PCM16:
                    addr, axfmt = byte // 2, 10
                else:
                    addr, axfmt = byte, 25
                out[addr] = ("%s-file%d-wave%d" % (kind, file_id, index), ch, axfmt)
        return out


def expected_stream(wave, ch, length):
    """What the accelerator reads for this channel: the wave, its loop repeated."""
    samples = list(wave["channelSamples"][ch])
    if not wave["loop"]:
        return samples, len(samples)
    loop = samples[wave["loopStart"]:]
    out = list(samples)
    while len(out) < length:
        out += loop
    return out, None


# ------------------------------------------------------------------ the model

def model_src(coefs, runs_recs, stream, end_len):
    """Polyphase / none SRC over the expected stream, frame by frame; returns
    (mismatching frames, frames checked)."""
    bad = 0
    pos_in = 0
    for rec in runs_recs:
        b = rec["before"]
        ratio = b["ratio"]
        pos = b["frac"]
        ring = list(b["last"])  # the PB's history (the DSP wrote it back last frame)
        out = []
        select = rec["srcSelectUsed"]
        if select == 2:
            for i in range(FRAME):
                out.append(stream[pos_in] if (end_len is None or pos_in < end_len) else 0)
                pos_in += 1
        else:
            table = coefs[b["coefSelect"]]
            for i in range(FRAME):
                pos += ratio
                while pos >= 0x10000:
                    x = stream[pos_in] if (end_len is None or pos_in < end_len) else 0
                    ring = ring[1:] + [x]
                    pos_in += 1
                    pos -= 0x10000
                c = table[(pos & 0xFFFF) >> 9]
                out.append(sat16(sum(ring[k] * c[k] for k in range(4)) >> 15))
        if tuple(out) != tuple(rec["src"]):
            bad += 1
    return bad


def model_out(rec, src):
    vol, delta = rec["before"]["ve"]
    out = []
    for x in src:
        out.append(sat16((x * vol) >> 15))
        vol = (vol + delta) & 0xFFFF
    lpf = rec["before"]["lpf"]
    if lpf[0]:
        y = s16(struct.pack("<H", lpf[1]), 0)
        a0, b0 = lpf[2], lpf[3]
        for i in range(FRAME):
            y = sat16((a0 * out[i] + b0 * y) >> 15)
            out[i] = y
    return out


def mix_contrib(rec):
    """The voice's main L / R contributions (MixBus), or None if it uses S."""
    b = rec["before"]
    ctrl = b["mixerCtrl"]
    if ctrl & (1 << 3) or b["biquadOn"]:
        return None
    m = b["mix"]
    res = []
    for on, vol, delta in ((1, m[0], m[1]), (2, m[2], m[3])):
        ramp = ctrl & 4
        col = [0] * FRAME
        if ctrl & on:
            v = vol
            d = delta if ramp else 0
            for i in range(FRAME):
                col[i] = sat16((rec["out"][i] * v) >> 15)
                v = (v + d) & 0xFFFF
        res.append(col)
    return res


def check_voices(name, archive, lines, recs, coefs, src_none):
    """Voice identity, decode, SRC and envelope for every tapped voice run."""
    starts = {}
    for f in lines["I"]:
        starts.update(archive.wave_starts(int(f[2]), int(f[3], 16)))
    # Runs: consecutive frames of one voice, from an N line to the next N / E.
    by_voice = {}
    for rec in recs:
        rec["srcSelectUsed"] = 2 if src_none else rec["before"]["srcSelect"]
        by_voice.setdefault(rec["voice"], []).append(rec)
    runs = []
    for voice, vrecs in by_voice.items():
        cur = None
        prev_frame = None
        prev_addr = None
        for rec in vrecs:
            b = rec["before"]
            if cur is None or rec["frame"] != prev_frame + 1 or b["cur"] != prev_addr:
                cur = []
                runs.append(cur)
            cur.append(rec)
            prev_frame = rec["frame"]
            prev_addr = rec["after"]["cur"]
    stats = {"runs": len(runs), "identified": 0, "decode": 0, "src": 0, "out": 0, "samples": 0}
    for run_recs in runs:
        b = run_recs[0]["before"]
        ident = starts.get(b["cur"])
        if ident is None:
            fail("%s: voice %d at frame %d starts at 0x%08x, not at a wave of the sound's file"
                 % (name, run_recs[0]["voice"], run_recs[0]["frame"], b["cur"]))
            continue
        wave_name, ch, axfmt = ident
        if b["format"] != axfmt:
            fail("%s: %s voice format %d, wave format %d" % (name, wave_name, b["format"], axfmt))
            continue
        stats["identified"] += 1
        wave = archive.decoded[wave_name]
        if wave["loop"] and (wave["diag"]["loopHistMismatch"] or wave["diag"]["loopPsMismatch"]):
            fail("%s: %s loop context disagrees with the data" % (name, wave_name))
        got = [x for rec in run_recs for x in rec["input"]]
        stream, end_len = expected_stream(wave, ch, len(got))
        exp = list(stream[:len(got)])
        if end_len is not None and len(got) > end_len:
            exp = list(stream[:end_len]) + [0] * (len(got) - end_len)
            # Reads stop in the frame the wave ends; it must be the last frame.
            last_start = sum(len(r["input"]) for r in run_recs[:-1])
            if last_start > end_len:
                fail("%s: %s voice kept running after its end" % (name, wave_name))
            if run_recs[-1]["after"]["state"] != 0:
                fail("%s: %s voice still running after its end" % (name, wave_name))
        if got != exp:
            first = next(i for i in range(min(len(got), len(exp))) if got[i] != exp[i]) \
                if any(got[i] != exp[i] for i in range(min(len(got), len(exp)))) else min(len(got), len(exp))
            fail("%s: %s ch%d decode differs at sample %d (%s vs %s)"
                 % (name, wave_name, ch, first, got[first:first + 4], exp[first:first + 4]))
        else:
            stats["decode"] += 1
            stats["samples"] += len(got)
        bad = model_src(coefs, run_recs, stream, end_len)
        if bad:
            fail("%s: %s SRC differs in %d of %d frames" % (name, wave_name, bad, len(run_recs)))
        else:
            stats["src"] += 1
        bad_out = 0
        for rec in run_recs:
            # With the SRC off, rec["src"] has already been checked against
            # the decode itself, so this is "decode times envelope".
            if model_out(rec, rec["src"]) != list(rec["out"]):
                bad_out += 1
        if bad_out:
            fail("%s: %s envelope differs in %d frames" % (name, wave_name, bad_out))
        else:
            stats["out"] += 1
    return stats


def check_mix(name, recs, wav, frames):
    """The rendered output from the tapped voices (main L/R, master volume 1.0)."""
    by_frame = {}
    for rec in recs:
        by_frame.setdefault(rec["frame"], []).append(rec)
    bad = 0
    checked = 0
    for f in range(frames):
        lr = [[0] * FRAME, [0] * FRAME]
        ok = True
        for rec in by_frame.get(f, []):
            c = mix_contrib(rec)
            if c is None:
                ok = False
                break
            for k in range(2):
                for i in range(FRAME):
                    lr[k][i] += c[k][i]
        if not ok:
            continue
        exp = []
        for i in range(FRAME):
            for k in range(2):
                v = lr[k][i]
                exp.append(32767 if v > 32767 else (-32767 if v < -32767 else v))
        got = list(wav[f * FRAME * 2:(f + 1) * FRAME * 2])
        checked += 1
        if got != exp:
            bad += 1
            if bad == 1:
                fail("%s: mixed output differs at AX frame %d" % (name, f))
    return checked, bad


def parse_lines(out):
    lines = {"S": [], "I": [], "N": [], "E": [], "C": [], "R": []}
    for ln in out.splitlines():
        f = ln.split()
        if f and f[0] in lines:
            lines[f[0]].append(f)
    return lines


def masked(out):
    return "\n".join(" ".join(ln.split()[:3]) if ln.startswith("I ") else ln
                     for ln in out.splitlines())


def summary(lines):
    events = "\n".join(" ".join(f) for k in ("N", "E", "C") for f in lines[k])
    maxv = max([int(f[2]) for f in lines["C"]] or [0])
    r = lines["R"][0]
    return "notes=%d maxvoices=%d events=%s out=%s" % (
        len(lines["N"]), maxv, hashlib.sha1(events.encode()).hexdigest()[:16], r[6])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--compare", help="the other build's audio_harness (skipped if missing)")
    ap.add_argument("--wav-dir", help="write each render there (outside the repository)")
    ap.add_argument("--update", action="store_true", help="rewrite tests/audio_expected.txt")
    args = ap.parse_args()

    if args.wav_dir:
        wav_dir = os.path.abspath(args.wav_dir)
        if wav_dir == REPO or wav_dir.startswith(REPO + os.sep):
            print("test_audio: --wav-dir must be outside the repository")
            return 1
        os.makedirs(wav_dir, exist_ok=True)
    else:
        wav_dir = None

    coefs = gen_ax_coefs.tables(REPO)
    hdr = open(os.path.join(REPO, "port", "host", "src", "host_ax_coefs.h")).read()
    if hdr != gen_ax_coefs.render():
        fail("src/host_ax_coefs.h is out of date (run tools/gen_ax_coefs.py)")

    expected = {}
    if os.path.exists(EXPECTED):
        for ln in open(EXPECTED):
            if ln.strip() and not ln.startswith("#"):
                k, v = ln.rstrip("\n").split(" ", 1)
                expected[k] = v
    results = {}
    archives = {}
    totals = {"runs": 0, "identified": 0, "decode": 0, "src": 0, "out": 0, "samples": 0}
    mix_checked = mix_bad = 0
    compared = "not run"
    other = args.compare if args.compare and os.path.exists(args.compare) else None
    if args.compare and other is None:
        compared = "skipped (%s not built)" % args.compare

    with tempfile.TemporaryDirectory() as tmp:
        for name, arc, sounds, frames, flags in SCENARIOS:
            rc, out, err, tap, wav = run(args.harness, args.disc, arc, sounds, frames, flags, tmp,
                                         wav_dir, name)
            if rc != 0:
                fail("%s: harness exit %d: %s" % (name, rc, err.strip()[-300:]))
                continue
            lines = parse_lines(out)
            if not lines["R"]:
                fail("%s: no R line" % name)
                continue
            for s in lines["S"]:
                if s[3] != "1":
                    fail("%s: sound %s did not start" % (name, s[5]))
            # Determinism: the same run again, and the other build.
            rc2, out2, _, tap2, wav2b = run(args.harness, args.disc, arc, sounds, frames,
                                            flags, tmp, None, name + "-again")
            if out2 != out:
                fail("%s: a second run prints different output" % name)
            if tap and open(tap, "rb").read() != open(tap2, "rb").read():
                fail("%s: a second run taps different voices" % name)
            if other:
                rc3, out3, _, tap3, _ = run(other, args.disc, arc, sounds, frames, flags, tmp, None,
                                            name + "-other")
                # The I lines' physical addresses differ (the heap layout
                # does); every other line, the event addresses included
                # (printed relative to those), must not.
                if masked(out3) != masked(out):
                    fail("%s: %s prints different output" % (name, args.compare))
                if tap3:
                    os.remove(tap3)
                compared = "identical"
            results[name] = summary(lines)
            r = lines["R"][0]
            print("  %-16s %5s AX frames, %3d notes, max %2s voices, rms %9s, peak L/R %5s %5s"
                  % (name, r[1], len(lines["N"]),
                     max([int(f[2]) for f in lines["C"]] or [0]), r[3], r[4], r[5]))
            if tap:
                if arc not in archives:
                    archives[arc] = Archive(args.disc, arc)
                recs = read_tap(tap)
                st = check_voices(name, archives[arc], lines, recs, coefs, "--src-none" in flags)
                for k in totals:
                    totals[k] += st[k]
                if len(lines["S"]) == 1 and lines["S"][0][4] == "WAVE" and "--fx" not in flags:
                    c, b = check_mix(name, recs, read_wav(wav), frames)
                    mix_checked += c
                    mix_bad += b
                os.remove(tap)
                os.remove(tap2)

    if args.update:
        with open(EXPECTED, "w") as f:
            f.write("# test_audio.py regression record: scenario, note count, max voices,\n"
                    "# sha1 of the N/E/C lines (16 hex), FNV-1a of the rendered samples.\n"
                    "# Rewrite with --update after an intended change.\n")
            for name, _, _, _, _ in SCENARIOS:
                if name in results:
                    f.write("%s %s\n" % (name, results[name]))
        print("test_audio: wrote %s" % EXPECTED)
    else:
        for name, _, _, _, _ in SCENARIOS:
            if name not in results:
                continue
            if name not in expected:
                fail("%s: not in audio_expected.txt (run with --update)" % name)
            elif expected[name] != results[name]:
                fail("%s: regression: %s, expected %s" % (name, results[name], expected[name]))

    print("test_audio: %d scenarios; voice runs %d (identified %d): decode exact %d (%d samples), "
          "SRC exact %d, envelope exact %d; mixed output exact in %d of %d frames"
          % (len(results), totals["runs"], totals["identified"], totals["decode"],
             totals["samples"], totals["src"], totals["out"], mix_checked - mix_bad, mix_checked))
    print("  determinism: every scenario twice identical; other build: %s" % compared)
    if errors:
        print("test_audio: %d errors" % len(errors))
        return 1
    print("test_audio: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
