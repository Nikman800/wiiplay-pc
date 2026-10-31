#!/usr/bin/env python3
"""The game's own boot and frame loop, headless, to the title screen (port/docs/HOST_BUILD.md §29).

Runs build/host64/boot (the decomp's main: RPSysSystem::initialize / create / setup /
mainLoop, the scene manager and creator, the boot and title scenes, on the host platform
layers, Aurora headless on lavapipe) three times:

  run A   tests/boot/title.txt (remote 0 connects, A pressed at retrace 200): the health and
          safety screen, then the title; frame hashes, two frames saved, the audio output;
  run B   the same again: every frame hash must be identical (determinism);
  run C   no input: the boot scene must stay (no A press, its 1200-frame timeout not reached).

Checked independently of the game's code:

  scenes    the scene IDs RPSysSceneMgr reports, against RPSysSceneCreator.h's enum read here:
            -1 (none), the boot scene, then the Party Pack title scene, which comes after
            the A press and within the boot scene's fade (30 frames) plus a few frames;
  health    a displayed frame of the health screen against test_gxlyt's model of the strap
            layout (independent: main.dol .data6 -> Yaz0 -> U8 -> BRLYT/TPL, rasterised
            here), scaled from the layout's 608 units to the 640-pixel EFB the boot's 2D
            view covers: within 3 per channel everywhere;
  frames    FRAME lines consecutive, retrace count and game time increasing (1001 units a
            retrace), one display copy per game frame;
  audio     AX frames every 3 ms of game time (180 units), the raw output's size, and sound
            in it (not silence) once the title is up;
  log       no Aurora warning but the documented ones, every unhandled register documented
            (test_gx3d's list plus the draw-sync token registers), nothing unresolved, the
            process ends with its END line (no aborting stand-in reached).

Visual only (saved as PNGs with --png): the title screen's look. Its numeric checks are
properties: it differs from the health screen and shows the orange "Play" logo.

Nothing from the disc goes into the repository: frames, NAND and caches go to --scratch.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import gxtex  # noqa: E402
import test_gx3d  # noqa: E402
import test_gxlyt  # noqa: E402

W, H = 640, 456
FRAMES = 420
HEALTH_FRAME = 150   # displayed frame on the health screen (the A press is at retrace 200)
PRESS_RETRACE = 200
FADE_FRAMES = 30     # RPSysBootScene::Reset: setFadeFrame(30)
TOLERANCE = 3
RETRACE_UNITS = 1001
AX_UNITS = 180

ALLOWED_LOG = dict(test_gx3d.ALLOWED_LOG)
ALLOWED_LOG["Buffer mapping Aborted: Buffer was destroyed before mapping was resolved."] = \
    "a frame readback still pending when wpgx_shutdown destroys the device"
ALLOWED_UNHANDLED = dict(test_gx3d.ALLOWED_UNHANDLED)
ALLOWED_UNHANDLED[("BP", 0x47)] = "draw sync token (GXSetDrawSync is synchronous on the host)"
ALLOWED_UNHANDLED[("BP", 0x48)] = "draw sync token interrupt (synchronous on the host)"


def scene_ids(repo):
    """ESceneID values from RPSysSceneCreator.h (an enum counted from 0)."""
    with open(os.path.join(repo, "include", "Pack", "RPSystem", "RPSysSceneCreator.h")) as f:
        text = f.read()
    body = text[text.index("enum ESceneID"):]
    body = body[body.index("{") + 1:body.index("}")]
    ids, n = {}, 0
    active = []  # #if blocks: the host build defines PACK_PARTY, not PACK_SPORTS
    for line in body.splitlines():
        line = line.strip()
        if line.startswith("#if"):
            active.append("PACK_PARTY" in line)
            continue
        if line.startswith("#endif"):
            active.pop()
            continue
        if not all(active):
            continue
        m = re.match(r"\s*(ESceneID_\w+)\s*(?:=\s*(\w+))?\s*,?", line)
        if not m:
            continue
        if m.group(2) is not None:
            n = int(m.group(2), 0)
        ids[m.group(1)] = n
        n += 1
    return ids


def run(boot, disc, scratch, name, env, extra):
    d = os.path.join(scratch, name)
    for sub in ("nand", "data"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    cmd = [boot, "--disc", disc, "--nand", os.path.join(d, "nand"), "--data-dir",
           os.path.join(d, "data"), "--trace", "--hash", "--log-level", "0"] + extra
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=600)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit("FAIL: boot (%s) exited %d" % (name, p.returncode))
    frames, scenes, end = [], [], None
    for line in p.stdout.splitlines():
        f = line.split("\t")
        kv = dict(x.split("=", 1) for x in f[1:] if "=" in x)
        if f[0] == "FRAME":
            frames.append((int(f[1]), int(kv["retrace"]), int(kv["time"]), int(kv["scene"]),
                           int(kv["audio"]), kv.get("hash")))
        elif f[0] == "SCENE":
            scenes.append((int(kv["retrace"]), int(kv["scene"])))
        elif f[0] == "END":
            end = kv
    return d, frames, scenes, end, p.stderr


def load_rgba(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != W * H * 4:
        raise SystemExit("FAIL: %s: frame size" % path)
    return data


def opaque(rgba):
    out = bytearray(rgba)
    out[3::4] = b"\xff" * (len(out) // 4)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot", required=True)
    ap.add_argument("--disc", required=True, help="extracted disc files directory")
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--gpu", action="store_true", help="use the real Vulkan driver, not lavapipe")
    ap.add_argument("--png", default=None, help="directory for PNGs of the health and title frames")
    args = ap.parse_args()

    env = dict(os.environ)
    if not args.gpu:
        icd = test_gxlyt.lavapipe_icd()
        if icd is None:
            raise SystemExit("FAIL: lavapipe (mesa-vulkan-drivers) not found")
        env["VK_ICD_FILENAMES"] = icd
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    # A fresh NAND for every run of the test (the boot writes save data)
    scratch = os.path.join(args.scratch, "boot")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch, exist_ok=True)
    repo = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    ids = scene_ids(repo)
    boot_id = ids["ESceneID_RPSysBootScene"]
    title_id = ids["ESceneID_RPPartyPackTitleScene"]
    script = os.path.join(HERE, "boot", "title.txt")
    errs = []

    # --- run A: to the title ------------------------------------------------------------
    health_path = os.path.join(scratch, "health.rgba")
    title_path = os.path.join(scratch, "title.rgba")
    audio_path = os.path.join(scratch, "audio.raw")
    _, fa, sa, enda, erra = run(args.boot, args.disc, scratch, "a", env,
                                ["--frames", str(FRAMES), "--input", script,
                                 "--save", "%d:%s" % (HEALTH_FRAME, health_path),
                                 "--save", "%d:%s" % (FRAMES - 1, title_path),
                                 "--audio", audio_path])
    if enda is None or len(fa) != FRAMES:
        errs.append("run A: %d frames, END line %s" % (len(fa), "present" if enda else "missing"))

    # scenes
    seq = [s for _, s in sa]
    if seq != [-1, boot_id, title_id]:
        errs.append("scenes %s, expected [-1, %d, %d]" % (seq, boot_id, title_id))
    title_at = next((r for r, s in sa if s == title_id), None)
    if title_at is None or not PRESS_RETRACE < title_at <= PRESS_RETRACE + FADE_FRAMES + 8:
        errs.append("title scene at retrace %s, expected within the fade after %d" % (title_at, PRESS_RETRACE))
    if fa[HEALTH_FRAME][3] != boot_id or fa[-1][3] != title_id:
        errs.append("frame %d scene %d, last frame scene %d" % (HEALTH_FRAME, fa[HEALTH_FRAME][3], fa[-1][3]))

    # frames, time, audio frames
    for i, (idx, retrace, time, _, audio, _) in enumerate(fa):
        if idx != i:
            errs.append("frame %d numbered %d" % (i, idx))
            break
        if time != retrace * RETRACE_UNITS:
            errs.append("frame %d: time %d at retrace %d" % (i, time, retrace))
            break
        if i and retrace <= fa[i - 1][1]:
            errs.append("frame %d: retrace %d not after %d" % (i, retrace, fa[i - 1][1]))
            break
    last = fa[-1]
    # AX starts with the first AX frame after AXInit (a few retraces into the boot)
    if not 0 <= last[2] // AX_UNITS - last[4] <= 10 * RETRACE_UNITS // AX_UNITS:
        errs.append("%d audio frames after %d units of game time" % (last[4], last[2]))
    audio_frames = int(enda["audio"]) if enda else 0
    size = os.path.getsize(audio_path)
    if size != audio_frames * 96 * 4:
        errs.append("audio file %d bytes for %d AX frames" % (size, audio_frames))
    with open(audio_path, "rb") as f:
        pcm = f.read()
    title_start = title_at * RETRACE_UNITS // AX_UNITS * 96 * 4 if title_at else 0
    tail = struct.unpack("<%dh" % ((len(pcm) - title_start) // 2), pcm[title_start:len(pcm) - (len(pcm) - title_start) % 2])
    peak = max((abs(v) for v in tail), default=0)
    rms = (sum(v * v for v in tail) / max(1, len(tail))) ** 0.5
    if peak == 0:
        errs.append("no sound after the title scene starts")

    # log
    allowed, unhandled = {}, []
    log_errs = []
    test_gx3d.ALLOWED_LOG, saved_log = ALLOWED_LOG, test_gx3d.ALLOWED_LOG
    test_gx3d.ALLOWED_UNHANDLED, saved_unh = ALLOWED_UNHANDLED, test_gx3d.ALLOWED_UNHANDLED
    try:
        allowed, unhandled = test_gx3d.check_log(erra, log_errs)
    finally:
        test_gx3d.ALLOWED_LOG, test_gx3d.ALLOWED_UNHANDLED = saved_log, saved_unh
    errs += log_errs
    for line in erra.splitlines():
        if line.startswith("boot:") or "host_thread:" in line or "port arena:" in line:
            errs.append(line)

    # health screen against the layout model
    files = test_gxlyt.strap_files(os.path.join(args.disc, "..", "sys", "main.dol"))
    want, drawn = test_gxlyt.expected_frame(files, (255, 255, 255), W, H, W / test_gxlyt.W)
    health = load_rgba(health_path)
    diffs = [max(abs(health[i * 4 + c] - px[c]) for c in range(3)) for i, px in enumerate(want)]
    over = sum(1 for d in diffs if d > TOLERANCE)
    if over:
        errs.append("health screen: %d pixels over %d (max %d)" % (over, TOLERANCE, max(diffs)))

    # title: properties only
    title = load_rgba(title_path)
    differ = sum(1 for i in range(0, len(title), 4) if title[i:i + 3] != health[i:i + 3])
    orange = sum(1 for i in range(0, len(title), 4)
                 if title[i] > 230 and 90 < title[i + 1] < 170 and title[i + 2] < 60)
    if differ < W * H // 4 or orange < 2000:
        errs.append("title frame: %d pixels differ from the health screen, %d orange" % (differ, orange))
    if args.png:
        os.makedirs(args.png, exist_ok=True)
        gxtex.write_png(os.path.join(args.png, "health.png"), W, H, opaque(health))
        gxtex.write_png(os.path.join(args.png, "title.png"), W, H, opaque(title))

    # --- run B: determinism ---------------------------------------------------------------
    _, fb, sb, _, _ = run(args.boot, args.disc, scratch, "b", env,
                          ["--frames", str(FRAMES), "--input", script])
    same = [x[5] for x in fa] == [x[5] for x in fb] and sa == sb
    if not same:
        n = next((i for i, (x, y) in enumerate(zip(fa, fb)) if x[5] != y[5]), None)
        errs.append("run B differs from run A (first frame %s)" % n)

    # --- run C: no input --------------------------------------------------------------------
    _, fc, sc, _, _ = run(args.boot, args.disc, scratch, "c", env, ["--frames", "300"])
    if [s for _, s in sc] != [-1, boot_id] or fc[-1][3] != boot_id:
        errs.append("without input: scenes %s" % [s for _, s in sc])

    print("test_boot: %d frames on %s; scenes -1 -> %d (boot) -> %d (title) at retrace %s, A pressed at %d"
          % (len(fa), "lavapipe" if not args.gpu else "the GPU", boot_id, title_id, title_at, PRESS_RETRACE))
    print("  health screen (frame %d) vs the strap layout model at 640/608: max diff %d, mean %.3f, %d over %d"
          % (HEALTH_FRAME, max(diffs), sum(diffs) / len(diffs), over, TOLERANCE))
    print("  title (frame %d): %d pixels differ from the health screen, %d orange logo pixels (visual check: title.png)"
          % (FRAMES - 1, differ, orange))
    print("  time: %d retraces, %d AX frames; audio after the title starts: peak %d, rms %.1f"
          % (last[1], audio_frames, peak, rms))
    print("  log: %d allowed warnings (%s); %d unhandled registers, all documented"
          % (sum(allowed.values()), ", ".join(sorted(allowed)), len(unhandled)))
    print("  determinism: %d frame hashes identical over two runs: %s; without input the boot scene stays: %s"
          % (len(fa), same, [s for _, s in sc] == [-1, boot_id]))
    for e in errs:
        print("  ERROR: " + e)
    print("errors: %d" % len(errs))
    print("OK" if not errs else "FAIL")
    return 0 if not errs else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except test_gxlyt.ModelError as e:
        raise SystemExit("FAIL: model: %s" % e)
