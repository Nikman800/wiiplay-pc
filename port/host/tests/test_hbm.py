#!/usr/bin/env python3
"""Compare hbm_harness (the HOME Menu files through the real homebuttonMiniLib
HBMCreate on the host) with the bytes.

Runs port/host/build/*/hbm_harness on the extracted disc and builds the same
lines (see the harness header):

  HC  per layout archive (US/PartyStatic/local.carc/HomeMenu/homeBtn*.arc):
      config.txt as HomeButton::set_config reads it (layout and animation
      names, button count, animation count = buttons x 3, dialog flags)
  HT  home_nosave.csv (UTF-16, big-endian, with a byte-order mark) split as
      HomeButton::set_text does: every opening quote starts message
      [language][index], languages first
  HB  the three text boxes init_msg fills for the archive's region
  HS  every SpeakerSe.arc .bwav (big-endian 16-bit PCM) as the Wii Remote
      speaker receives it through the WPAD backend: RemoteSpk's UpdateSpeaker
      makes 40-sample packets, each sample times the volume / 10, the last
      packet padded with zeros, and encodes them (WENC, 4-bit ADPCM, 20 bytes
      a packet; the first packet of each sound from the initial state, as
      after RemoteSpk::Connect)

It also checks that every file in a HomeMenu directory on the disc is covered
here or by another harness (HomeButtonSe.brsar: test_sound.py, homeBtnIcon.bti:
test_bti.py, the layout archives' contents: test_layouts.py).

    python3 test_hbm.py --harness build/host64/hbm_harness --disc DISC
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

CONFIG_SIZE = 0x23        # RPSysHomeMenuMgr::HBM_CONFIG_SIZE
ANIM_PER_BUTTON = 3       # homebutton::res::eAnim_Max
TEXT_PANES = ("T_msg_00", "T_msg_01", "T_Dialog")
SAMPLES_PER_PACKET = 40   # nw4r::snd::RemoteSpeaker::SAMPLES_PER_AUDIO_PACKET
VOLUME = 7
# RPSysHomeMenuMgr::initHomeButtonInfo: archive -> SC_LANG region
REGIONS = {"homeBtn.arc": 0, "homeBtn_ENG.arc": 1, "homeBtn_GER.arc": 2, "homeBtn_FRA.arc": 3,
           "homeBtn_SPA.arc": 4, "homeBtn_ITA.arc": 5, "homeBtn_NED.arc": 6}
COVERED_ELSEWHERE = ("HomeButtonSe.brsar", "homeBtnIcon.bti")


def fnv(data, h=0x811C9DC5):
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def config_line(arc, config):
    buf = config[:CONFIG_SIZE] + b"\0"
    layout_len = buf.index(b",")
    layout = buf[:layout_len].decode()
    rest = buf[layout_len + 1:]
    anm_len = rest.index(b",")
    anm = rest[:anm_len].decode()
    p = layout_len + 1 + anm_len
    flags = []
    i = 0
    while buf[p + i] != 0 and p + i < CONFIG_SIZE:
        if buf[p + i] == ord(","):
            flags.append(1 if buf[p + i + 1] == ord("1") else 0)
        i += 1
    shown = (flags + [0, 0, 0, 0])[:4]
    return (f"HC\t{arc}\t{layout}\t{anm}\t{len(flags)}\t{len(flags) * ANIM_PER_BUTTON}\t"
            + ",".join(str(f) for f in shown))


def split_text(csv):
    """HomeButton::set_text: message[lang][index] = the units after each opening quote."""
    units = list(struct.unpack(f">{len(csv) // 2}H", csv[:len(csv) // 2 * 2])) + [0]
    texts = {}
    inside = False
    j = k = 0
    for i, u in enumerate(units):
        if u == 0:
            break
        if u == 0x22:
            if not inside:
                inside = True
                texts[(j, k)] = i + 1
                j += 1
                if j == 7:
                    j, k = 0, k + 1
            else:
                inside = False
    out = {}
    for key, start in texts.items():
        end = start
        while units[end] not in (0x22, 0):
            end += 1
        out[key] = "".join(f"{u:04x}" for u in units[start:end])
    return out


WENC_TABLE = (0.89843750, 0.89843750, 0.89843750, 0.89843750,
              1.19921875, 1.59765625, 2.00000000, 2.39843750)


def wenc_encode(pcm, state):
    """WENCGetEncodeData (src/revolution/WENC/wenc.c): the Wii Remote speaker's
    4-bit ADPCM, two samples a byte, the first in the high nibble. `state` is
    [xn, dl], None for the first packet (WENC_FLAG_USER_INFO clear)."""
    xn, dl = state if state is not None else (0, 127)
    out = bytearray((len(pcm) + 1) // 2)
    for i, da in enumerate(pcm):
        l3 = 1 if da < xn else 0
        dn = abs(da - xn)
        l2 = l1 = l0 = 0
        if dn >= dl:
            l2, dn = 1, dn - dl
        dlh = dl // 2
        if dn >= dlh:
            l1, dn = 1, dn - dlh
        dlq = dlh // 2
        if dn >= dlq:
            l0, dn = 1, dn - dlq
        qn = (1 - l3 * 2) * (dl * l2 + dlh * l1 + dlq * l0 + dlq // 2)
        qn = max(-0x10000, min(0xFFFF, qn))
        xn = max(-0x8000, min(0x7FFF, xn + qn))
        out[i // 2] |= (l3 * 8 + l2 * 4 + l1 * 2 + l0) << (4 if i % 2 == 0 else 0)
        dl = int(dl * WENC_TABLE[l2 * 4 + l1 * 2 + l0])
        dl = max(127, min(0x6000, dl))
    return bytes(out), [xn, dl]


def speaker_line(name, data):
    samples = list(struct.unpack(f">{len(data) // 2}h", data[:len(data) // 2 * 2]))
    out = []
    length = len(data)
    pos = 0
    while length > 0:
        size = length // 2
        for n in range(min(size, SAMPLES_PER_PACKET)):
            v = samples[pos + n] * VOLUME
            q = abs(v) // 10
            out.append(((-q if v < 0 else q) + 0x8000 & 0xFFFF) - 0x8000)
        if size <= SAMPLES_PER_PACKET:
            out += [0] * (SAMPLES_PER_PACKET - size)
        pos += SAMPLES_PER_PACKET
        length -= SAMPLES_PER_PACKET * 2
    encoded = bytearray()
    state = None
    for i in range(0, len(out), SAMPLES_PER_PACKET):
        packet, state = wenc_encode(out[i:i + SAMPLES_PER_PACKET], state)
        encoded += packet
    packets = len(out) // SAMPLES_PER_PACKET
    return f"HS\t{name}\t{VOLUME}\t{packets}\t{len(encoded)}\t{fnv(bytes(encoded)):08x}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    got = run.stdout.splitlines()

    config = csv = None
    layouts = set()
    bwavs = {}
    problems = []
    for path, data in wiiarc.walk(args.disc):
        parts = path.split("/")
        if "HomeMenu" not in parts:
            continue
        rel = parts[parts.index("HomeMenu") + 1:]
        if path.startswith("Common/PartyStatic/common.carc/HomeMenu/"):
            if rel == ["config.txt"]:
                config = data
            elif rel == ["home_nosave.csv"]:
                csv = data
            elif len(rel) == 2 and rel[0] == "SpeakerSe.arc" and rel[1].endswith(".bwav"):
                bwavs[rel[1]] = data
            elif rel[-1] not in COVERED_ELSEWHERE:
                problems.append(f"{path}: not covered")
        elif path.startswith("US/PartyStatic/local.carc/HomeMenu/") and rel[0] in REGIONS:
            layouts.add(rel[0])
        else:
            problems.append(f"{path}: not covered")
    if config is None or csv is None or not layouts or not bwavs:
        problems.append("HOME Menu files missing on the disc")
        expected = []
    else:
        if csv[:2] != b"\xfe\xff":
            problems.append("home_nosave.csv has no big-endian byte-order mark")
        texts = split_text(csv)
        expected = []
        for arc in sorted(layouts):
            expected.append(config_line(arc, config))
            for (lang, msg), text in sorted(texts.items()):
                expected.append(f"HT\t{arc}\t{lang}\t{msg}\t{text}")
            region = REGIONS[arc]
            for i, pane in enumerate(TEXT_PANES):
                expected.append(f"HB\t{arc}\t{region}\t{pane}\t{texts.get((region, i), '')}")
        for name, data in bwavs.items():
            expected.append(speaker_line(name, data))

    kinds = ("HC", "HT", "HB", "HS")
    kinds_got = collections.Counter(line.split("\t", 1)[0] for line in got)
    kinds_exp = collections.Counter(line.split("\t", 1)[0] for line in expected)
    cg, ce = collections.Counter(got), collections.Counter(expected)
    missing = sorted((ce - cg).elements())
    extra = sorted((cg - ce).elements())

    print(f"files: layout archives {len(layouts)}, .bwav {len(bwavs)}, config.txt, "
          f"home_nosave.csv")
    print("lines: " + ", ".join(f"{k} {kinds_got[k]}/{kinds_exp[k]}" for k in kinds)
          + " (harness/python)")
    print(f"missing {len(missing)}, extra {len(extra)}")
    for line in problems:
        print("  problem:", line)
    for line in missing[:10]:
        print("  missing:", line[:200])
    for line in extra[:10]:
        print("  extra:  ", line[:200])
    ok = run.returncode == 0 and not missing and not extra and not problems
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
