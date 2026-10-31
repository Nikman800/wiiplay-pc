#!/usr/bin/env python3
"""The game's own boot and frame loop, headless, to the title screen (port/docs/HOST_BUILD.md §29).

Runs build/host64/boot (the decomp's main: RPSysSystem::initialize / create / setup /
mainLoop, the scene manager and creator, the boot and title scenes, on the host platform
layers, Aurora headless on lavapipe) three times:

  run A   tests/boot/title.txt (remote 0 connects, A pressed at retrace 200): the health and
          safety screen, then the title; frame hashes, two frames saved, the audio output;
  run B   the same again: every frame hash must be identical (determinism);
  run C   no input: the boot scene must stay (no A press, its 1200-frame timeout not reached);
  run D   real time with synthetic SDL events (§30);
  run M   the menus from a fresh NAND to the party menu (tests/boot/menus.txt), then a short
          Shooting Range (tests/boot/shooting_smoke.txt: round 1 until hits score, pause, Quit);
  run H   a NAND with a save that has every game open (written here), the menus
          (tests/boot/menus_saved.txt), then a short Laser Hockey (tests/boot/hockey_smoke.txt).

The full games (two runs each, for determinism) are tests/test_play.py (`make playtest`).

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
import concurrent.futures
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


# The menu run (tests/boot/menus.txt): frames saved, and the scene each shows
MENU_SHOTS = [(650, "save_check"), (1990, "save_created"), (2390, "select_players"),
              (2890, "select_mii"), (3100, "guest_warning"), (3500, "play_with_mii"),
              (4200, "welcome"), (4890, "party_menu")]
MENU_SCENES = [("save_check", "ESceneID_RPPartyPackTitleScene"), ("save_created", "ESceneID_RPPartyPackTitleScene"),
               ("select_players", "ESceneID_RPSysPlayerSelectScene"),
               ("select_mii", "ESceneID_RPSysPlayerSelectScene"),
               ("guest_warning", "ESceneID_RPSysPlayerSelectScene"),
               ("play_with_mii", "ESceneID_RPSysPlayerSelectScene"),
               ("welcome", "ESceneID_RPPartyMainScene"), ("party_menu", "ESceneID_RPPartyMainScene")]
# RPDucGame's states (RPDucGame.h): 1 intro, 2 round start, 3 round play, 4 round end,
# 5 waiting for the score, 6 medal, 7 results menu; five rounds
SHOOT_STATES = [(0, 0), (1, 1)] + [x for r in range(1, 6) for x in ((2, r), (3, r), (4, r))] \
    + [(5, 5), (6, 5), (7, 5)]
# The short Shooting Range of run M (tests/boot/shooting_smoke.txt): round 1, paused, Quit
SMOKE_FRAMES = 6450
SHOOT_SMOKE_SHOTS = [(5100, "explanation"), (5480, "round_start"), (5904, "hit"), (6200, "paused"),
                     (6440, "back_to_menu")]
SHOOT_SMOKE_SCENES = {"explanation": "ESceneID_RPPartyMainScene", "back_to_menu": "ESceneID_RPPartyMainScene"}
SHOOT_SMOKE_STATES = [(0, 0), (1, 1), (2, 1), (3, 1)]

# Laser Hockey (§32): RPHkyGame's states 0 intro, 1 play, 2 goal, 3 time up, 4 wait,
# 5 result. The probe: hky:STATE,TIMER,SCORE0,SCORE1,PUCKX,PUCKZ,DIST (the puck's distance
# from player 0's paddle). Contact: the paddle's centre within this of the puck's.
HOCKEY_CONTACT = 40
HOCKEY_SMOKE_FRAMES = 4600
HOCKEY_SMOKE_SHOTS = [(3700, "explanation"), (3950, "countdown"), (4200, "play"), (4380, "paused"),
                      (4590, "back_to_menu")]
HOCKEY_SMOKE_SCENES = {"explanation": "ESceneID_RPPartyMainScene", "back_to_menu": "ESceneID_RPPartyMainScene"}

def party_save(nand, games=0x1FF):
    """RPParty.dat in the game's format (RPSysSaveData::write: the pack and version
    signatures, RPPartySystemData, empty player lists, the 16-bit sum / inverted-sum CRC
    over the written part) with the given games open, and the banner file, whose contents
    the game never reads back (it only checks that it exists). games=1 gives the bytes the
    game itself writes for a fresh save."""
    d = os.path.join(nand, "title", "00010000", "52484145", "data")
    os.makedirs(d, exist_ok=True)
    buf = bytearray(32 * 1024)
    buf[0:8] = b"RPPT0000"
    for i in range(3):  # mOldIndex[i] = -1, then six address bytes
        buf[8 + i * 7] = 0xFF
    struct.pack_into(">II", buf, 0x20, games, 1)  # mGameFlags, mMsgFlags
    end = 0x50A0  # the end of the player lists
    total = inverted = 0
    for k in range(0, end, 2):
        w = struct.unpack_from(">H", buf, k)[0]
        total = (total + w) & 0xFFFF
        inverted = (inverted + (~w & 0xFFFF)) & 0xFFFF
    struct.pack_into(">I", buf, end, total << 16 | inverted)
    with open(os.path.join(d, "RPParty.dat"), "wb") as f:
        f.write(buf)
    with open(os.path.join(d, "banner.bin"), "wb") as f:
        f.write(bytes(29344))


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


def script(scratch, name, parts):
    """Concatenate input scripts (each without its `end`) into one."""
    path = os.path.join(scratch, name)
    with open(path, "w") as out:
        for part in parts:
            with open(os.path.join(HERE, "boot", part)) as f:
                out.writelines(l for l in f if not l.startswith("end"))
        out.write("end 100000\n")
    return path


def run(boot, disc, scratch, name, env, extra, saved=False, timeout=2400):
    d = os.path.join(scratch, name)
    for sub in ("nand", "data"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    if saved:
        party_save(os.path.join(d, "nand"))
    cmd = [boot, "--disc", disc, "--nand", os.path.join(d, "nand"), "--data-dir",
           os.path.join(d, "data"), "--trace", "--hash", "--log-level", "0"] + extra
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit("FAIL: boot (%s) exited %d" % (name, p.returncode))
    frames, scenes, end = [], [], None
    for line in p.stdout.splitlines():
        f = line.split("\t")
        kv = dict(x.split("=", 1) for x in f[1:] if "=" in x)
        if f[0] == "FRAME":
            frames.append((int(f[1]), int(kv["retrace"]), int(kv["time"]), int(kv["scene"]),
                           int(kv["audio"]), kv.get("hash"), kv.get("game")))
        elif f[0] == "SCENE":
            scenes.append((int(kv["retrace"]), int(kv["scene"])))
        elif f[0] == "END":
            end = kv
    return d, frames, scenes, end, p.stderr


def run_live(boot, disc, scratch, env):
    """Run D: real time, the dummy audio driver, synthetic SDL events (§30)."""
    d = os.path.join(scratch, "d")
    for sub in ("nand", "data"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    env = dict(env)
    for var in ("DISPLAY", "WAYLAND_DISPLAY"):  # never a window on the user's desktop
        env.pop(var, None)
    env["SDL_AUDIO_DRIVER"] = "dummy"
    env["SDL_VIDEO_DRIVER"] = "dummy"
    cmd = [boot, "--disc", disc, "--nand", os.path.join(d, "nand"), "--data-dir", os.path.join(d, "data"),
           "--headless", "--realtime", "--audio-out", "--events", os.path.join(HERE, "boot", "live.txt"),
           "--frames", "2000", "--trace", "--log-level", "2"]
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=600)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit("FAIL: boot (live) exited %d" % p.returncode)
    inputs, scenes, end, live = [], [], None, None
    for line in p.stdout.splitlines():
        f = line.split("\t")
        kv = dict(x.split("=", 1) for x in f[1:] if "=" in x)
        if f[0] == "INPUT":
            px, py = (float(v) for v in kv["pointer"].split(","))
            inputs.append((int(kv["retrace"]), int(kv["buttons"], 16), px, py, float(kv["roll"])))
        elif f[0] == "SCENE":
            scenes.append((int(kv["retrace"]), int(kv["scene"])))
        elif f[0] == "END":
            end = kv
        elif f[0] == "LIVE":
            live = kv
    return inputs, scenes, end, live, p.stderr


def check_live(inputs, scenes, end, live, stderr, title_id, errs):
    if end is None or live is None:
        errs.append("live: no END / LIVE line")
        return None
    retraces = int(end["retrace"])
    # the quit event at retrace 715 ends the run there
    if not 715 <= retraces <= 717:
        errs.append("live: ended at retrace %d, expected the quit at 715" % retraces)
    want = retraces * RETRACE_UNITS / 60000.0
    wall = float(live["wall"])
    # Never faster than real time. Slower only when the machine cannot keep up
    # (a loaded host: the pacer then re-anchors, counted in "late").
    if wall < 0.97 * want or (int(live["late"]) == 0 and wall > 1.08 * want):
        errs.append("live: %.3f s of wall time for %.3f s of game time" % (wall, want))
    if live["audiodriver"] != "dummy" or int(live["overruns"]) != 0 or int(live["underruns"]) > 2:
        errs.append("live: audio %s" % live)

    def state_at(r):
        cur = None
        for x in inputs:
            if x[0] <= r:
                cur = x
        return cur
    key = state_at(182)
    if key is None or key[1] != 0x0800:
        errs.append("live: the Space key did not press A (%s)" % (key,))
    click = state_at(202)
    if click is None or click[1] != 0x0800 or state_at(208)[1] != 0:
        errs.append("live: the left mouse button did not press A")
    p = state_at(100)
    if p is None or abs(p[2]) > 1e-3 or abs(p[3]) > 1e-3:
        errs.append("live: mouse at the window centre gave pointer %s" % (p,))
    tilt = state_at(250)
    if tilt is None or abs(tilt[4] - 30.0) > 0.2 or abs(state_at(265)[4]) > 0.2:
        errs.append("live: accelerometer roll %s, expected 30" % (tilt,))
    moved = state_at(285)
    # 1.5 screens per second at full deflection: about 10 frames of it
    if moved is None or not 0.35 < moved[2] < 0.65:
        errs.append("live: right stick moved the pointer to %s" % (moved,))
    ab = state_at(522)
    if ab is None or ab[1] != 0x0C00:
        errs.append("live: the gamepad's south + east buttons gave %s, expected A+B" % (ab,))
    yes = state_at(650)
    if yes is None or abs(yes[2] - (195 / 320.0 - 1)) > 1e-3 or abs(yes[3] - (348 / 228.0 - 1)) > 1e-3:
        errs.append("live: mouse at (195, 348) gave pointer %s" % (yes,))
    if int(live["testrumble"]) == 0 or int(live["rumble"]) == 0:
        errs.append("live: no rumble reached the virtual gamepad (%s)" % live)
    if title_id not in [s for _, s in scenes]:
        errs.append("live: scenes %s, the title never came" % scenes)
    if "gamepad 0: wiiplay test gamepad (gyro yes, accelerometer yes)" not in stderr:
        errs.append("live: the virtual gamepad was not reported")
    return wall, want, live


def game_values(fm, tag):
    """(frame, values) of the FRAME lines whose game= probe is `tag:`."""
    return [(f[0], tuple(int(v) for v in f[6].split(":")[1].split(","))) for f in fm
            if f[6] is not None and f[6].startswith(tag + ":")]


def check_log(stderr, errs, what):
    """Aurora's log (only documented warnings and unhandled registers) and no boot: message."""
    log_errs = []
    test_gx3d.ALLOWED_LOG, saved_log = ALLOWED_LOG, test_gx3d.ALLOWED_LOG
    test_gx3d.ALLOWED_UNHANDLED, saved_unh = ALLOWED_UNHANDLED, test_gx3d.ALLOWED_UNHANDLED
    try:
        allowed, unhandled = test_gx3d.check_log(stderr, log_errs)
    finally:
        test_gx3d.ALLOWED_LOG, test_gx3d.ALLOWED_UNHANDLED = saved_log, saved_unh
    for line in stderr.splitlines():
        if line.startswith("boot:") or "host_thread:" in line or "port arena:" in line:
            log_errs.append(line)
    errs += ["%s: %s" % (what, e) for e in log_errs]
    return sum(allowed.values()), len(unhandled)


def check_screens(fm, shots, spec, scenes, scene_id, ids, errs, what):
    """Each saved frame in its scene, all distinct."""
    for (f, name), (_, path) in zip(spec, shots):
        want = ids[scenes[name]] if name in scenes else scene_id
        if fm[f][3] != want:
            errs.append("%s: frame %d (%s) in scene %d, expected %d" % (what, f, name, fm[f][3], want))
    distinct = len(set(load_rgba(p) for _, p in shots))
    if distinct != len(shots):
        errs.append("%s: %d distinct screens of %d" % (what, distinct, len(shots)))
    return distinct


def check_shooting(fm, stderr, shots, audio_path, ids, errs, spec, scenes, states, rounds_want):
    """Shooting Range from the party menu (§31, §32): the game's own state through the run."""
    duc_id = ids["ESceneID_RPDucScene"]
    games = game_values(fm, "duc")
    seq = []
    for _, g in games:
        if not seq or seq[-1] != g[:2]:
            seq.append(g[:2])
    if seq != states:
        errs.append("shooting: (state, round) sequence %s, expected %s" % (seq, states))
    # score changes: only while shooting (state 3) or at a round's end (bonuses, state 4)
    changes, rounds = 0, set()
    for (_, a), (_, b) in zip(games, games[1:]):
        if b[2] != a[2]:
            changes += 1
            if b[0] not in (3, 4):
                errs.append("shooting: score changed in state %d" % b[0])
            if b[2] > a[2] and b[0] == 3:
                rounds.add(b[1])
    final = games[-1][1] if games else (0, 0, 0, 0)
    if final[2] <= 0 or final[3] <= 0 or rounds != rounds_want:
        errs.append("shooting: final score %d, hits %d, rounds scoring %s" % (final[2], final[3], sorted(rounds)))
    distinct = check_screens(fm, shots, spec, scenes, duc_id, ids, errs, "shooting")
    # sound while playing (round 1: BGM, shots, hits)
    play = [f for f, g in games if g[0] == 3 and g[1] == 1]
    with open(audio_path, "rb") as f:
        pcm = f.read()
    a0, a1 = (fm[play[0]][4] * 96 * 4, fm[play[-1]][4] * 96 * 4) if play else (0, 0)
    part = struct.unpack("<%dh" % ((a1 - a0) // 2), pcm[a0:a1]) if a1 > a0 else ()
    rms = (sum(v * v for v in part) / max(1, len(part))) ** 0.5
    if rms < 100:
        errs.append("shooting: no sound during round 1 (rms %.1f)" % rms)
    allowed, unhandled = check_log(stderr, errs, "shooting")
    return (" -> ".join("%d/%d" % x for x in seq[1:]), final[2], final[3], changes,
            rounds == rounds_want, rms, distinct, allowed, unhandled)


def check_hockey(fm, stderr, shots, ids, errs, spec, scenes, full):
    """Laser Hockey from the party menu (§32): the game's own state through the run."""
    games = game_values(fm, "hky")
    seq = []
    for _, g in games:
        if not seq or seq[-1] != g[0]:
            seq.append(g[0])
    play = [g for _, g in games if g[0] == 1]
    # the clock: in play it counts down a frame at a time (or stands, paused)
    clock_ok = all(b[1] in (a[1], a[1] - 1) for a, b in zip(play, play[1:]) if b[0] == 1)
    contacts = sum(1 for (_, a), (_, b) in zip(games, games[1:])
                   if b[0] == 1 and b[6] <= HOCKEY_CONTACT < a[6])
    travel = max((abs(g[4]) for g in play), default=0)
    # goals: a score changes only as a goal starts (state 2)
    goals, bad = [0, 0], []
    for (_, a), (_, b) in zip(games, games[1:]):
        for k in (0, 1):
            if b[2 + k] != a[2 + k]:
                if b[2 + k] != a[2 + k] + 1 or b[0] != 2:
                    bad.append((a, b))
                goals[k] += 1
    final = games[-1][1] if games else (0,) * 7
    if full:
        if seq[:2] != [0, 1] or 2 not in seq or seq[-1] != 5 or 3 not in seq:
            errs.append("hockey: states %s (expected intro, play, goals, time up, result)" % seq)
        if goals[0] < 1 or (final[2], final[3]) != tuple(goals):
            errs.append("hockey: goals %s, final score %d-%d" % (goals, final[2], final[3]))
    else:
        if seq != [0, 1]:
            errs.append("hockey: states %s, expected [0, 1]" % seq)
    if bad:
        errs.append("hockey: a score changed outside a goal: %s" % (bad[:2],))
    if not clock_ok or not play or play[0][1] != 7200:
        errs.append("hockey: the clock (%s at the start of play)" % (play[0][1] if play else None))
    # the puck leaves the centre line (the short run plays about 200 frames)
    if contacts < 1 or travel < (100 if full else 30):
        errs.append("hockey: %d contacts of paddle and puck, the puck at most %d from the centre line"
                    % (contacts, travel))
    distinct = check_screens(fm, shots, spec, scenes, ids["ESceneID_RPHkyScene"], ids, errs, "hockey")
    allowed, unhandled = check_log(stderr, errs, "hockey")
    return seq, len(play), contacts, travel, goals, final, distinct, allowed, unhandled


def hockey_summary(r):
    seq, play, contacts, travel, goals, final, distinct, allowed, unhandled = r
    return ("states %s; %d frames in play, the clock counting down from 7200; %d paddle-puck contacts, "
            "the puck up to %d from the centre line; goals %d-%d (final %d-%d); %d distinct screens; log: %d "
            "allowed warnings, %d unhandled registers, all documented"
            % (" -> ".join(str(x) for x in seq), play, contacts, travel, goals[0], goals[1], final[2], final[3],
               distinct, allowed, unhandled))


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
    script_a = os.path.join(HERE, "boot", "title.txt")
    errs = []

    # --- run D first, alone (real time: the other runs would load the machine) -----------
    inputs, sd, endd, lived, errd = run_live(args.boot, args.disc, scratch, env)
    live_result = check_live(inputs, sd, endd, lived, errd, title_id, errs)

    # --- the deterministic runs, concurrently ---------------------------------------------
    # M: the menus from a fresh NAND, then a short Shooting Range; H: a save with every game
    # open, the menus, then a short Laser Hockey
    menus = script(scratch, "menus_shooting_smoke.txt", ["menus.txt", "shooting_smoke.txt"])
    hockey = script(scratch, "menus_hockey_smoke.txt", ["menus_saved.txt", "hockey_smoke.txt"])
    shots = [(f, os.path.join(scratch, "menu_%s.rgba" % name)) for f, name in MENU_SHOTS]
    shoot_shots = [(f, os.path.join(scratch, "shoot_%s.rgba" % name)) for f, name in SHOOT_SMOKE_SHOTS]
    hockey_shots = [(f, os.path.join(scratch, "hockey_%s.rgba" % name)) for f, name in HOCKEY_SMOKE_SHOTS]
    shoot_audio = os.path.join(scratch, "shoot_audio.raw")
    save_args, hockey_args = [], []
    for f, path in shots + shoot_shots:
        save_args += ["--save", "%d:%s" % (f, path)]
    for f, path in hockey_shots:
        hockey_args += ["--save", "%d:%s" % (f, path)]
    health_path = os.path.join(scratch, "health.rgba")
    title_path = os.path.join(scratch, "title.rgba")
    audio_path = os.path.join(scratch, "audio.raw")
    jobs = {
        "a": ["--frames", str(FRAMES), "--input", script_a, "--save", "%d:%s" % (HEALTH_FRAME, health_path),
              "--save", "%d:%s" % (FRAMES - 1, title_path), "--audio", audio_path],
        "b": ["--frames", str(FRAMES), "--input", script_a],
        "c": ["--frames", "300"],
        "m": ["--frames", str(SMOKE_FRAMES), "--input", menus, "--audio", shoot_audio] + save_args,
        "h": ["--frames", str(HOCKEY_SMOKE_FRAMES), "--input", hockey] + hockey_args,
    }
    # Three at a time, each with few lavapipe threads: oversubscribed software
    # rendering slows every run down far more than it gains.
    penv = dict(env)
    penv.setdefault("LP_NUM_THREADS", "4")
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        # the long runs first
        order = ["m", "h", "a", "b", "c"]
        futures = {k: pool.submit(run, args.boot, args.disc, scratch, k, penv, jobs[k], k == "h")
                   for k in order}
        results = {k: f.result() for k, f in futures.items()}

    # --- run A: to the title ------------------------------------------------------------
    _, fa, sa, enda, erra = results["a"]
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
    for i, (idx, retrace, time, _, audio, _, _) in enumerate(fa):
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
    _, fb, sb, _, _ = results["b"]
    same = [x[5] for x in fa] == [x[5] for x in fb] and sa == sb
    if not same:
        n = next((i for i, (x, y) in enumerate(zip(fa, fb)) if x[5] != y[5]), None)
        errs.append("run B differs from run A (first frame %s)" % n)

    # --- run C: no input --------------------------------------------------------------------
    _, fc, sc, _, _ = results["c"]
    if [s for _, s in sc] != [-1, boot_id] or fc[-1][3] != boot_id:
        errs.append("without input: scenes %s" % [s for _, s in sc])

    # --- run M: the menus (save check, Select Players, Select Mii, party menu) --------------
    dm, fm, sm, endm, errm = results["m"]
    menu_seq = [s for _, s in sm]
    duc_id, party_id = ids["ESceneID_RPDucScene"], ids["ESceneID_RPPartyMainScene"]
    want_seq = [-1, boot_id, title_id, ids["ESceneID_RPSysPlayerSelectScene"], party_id, duc_id, party_id]
    if menu_seq != want_seq:
        errs.append("menus: scenes %s, expected %s" % (menu_seq, want_seq))
    if endm is None or len(fm) != SMOKE_FRAMES:
        errs.append("menus: %d frames, END %s" % (len(fm), "present" if endm else "missing"))
    for f, name in MENU_SHOTS:
        want_scene = dict(MENU_SCENES)[name]
        if f < len(fm) and fm[f][3] != ids[want_scene]:
            errs.append("menus: frame %d (%s) in scene %d, expected %s" % (f, name, fm[f][3], want_scene))
    save = os.path.join(dm, "nand", "title", "00010000", "52484145", "data", "RPParty.dat")
    if not os.path.isfile(save):
        errs.append("menus: no save file %s" % save)
    else:
        # the game's own fresh save is what party_save writes with only the first game open
        fresh = os.path.join(scratch, "fresh_nand")
        party_save(fresh, 1)
        with open(save, "rb") as f1, open(os.path.join(fresh, "title", "00010000", "52484145", "data",
                                                       "RPParty.dat"), "rb") as f2:
            if f1.read() != f2.read():
                errs.append("menus: the game's save differs from party_save's (format)")
    distinct = len(set(load_rgba(p) for _, p in shots))
    if distinct != len(shots):
        errs.append("menus: %d distinct screens of %d" % (distinct, len(shots)))
    if args.png:
        for (f, name), (_, path) in zip(MENU_SHOTS, shots):
            gxtex.write_png(os.path.join(args.png, "menu_%s.png" % name), W, H, opaque(load_rgba(path)))

    # --- Shooting Range, short (run M, from frame 4800) -------------------------------------
    shoot = check_shooting(fm, errm, shoot_shots, shoot_audio, ids, errs, SHOOT_SMOKE_SHOTS,
                           SHOOT_SMOKE_SCENES, SHOOT_SMOKE_STATES, {1})
    # --- run H: Laser Hockey, short -----------------------------------------------------------
    _, fh, sh, endh, errh = results["h"]
    hky_id = ids["ESceneID_RPHkyScene"]
    want_h = [-1, boot_id, title_id, ids["ESceneID_RPSysPlayerSelectScene"], party_id, hky_id, party_id]
    if [s for _, s in sh] != want_h:
        errs.append("hockey: scenes %s, expected %s" % ([s for _, s in sh], want_h))
    if endh is None or len(fh) != HOCKEY_SMOKE_FRAMES:
        errs.append("hockey: %d frames, END %s" % (len(fh), "present" if endh else "missing"))
    hky = check_hockey(fh, errh, hockey_shots, ids, errs, HOCKEY_SMOKE_SHOTS, HOCKEY_SMOKE_SCENES, False)
    if args.png:
        for (f, name), (_, path) in zip(SHOOT_SMOKE_SHOTS, shoot_shots):
            gxtex.write_png(os.path.join(args.png, "shoot_%s.png" % name), W, H, opaque(load_rgba(path)))
        for (f, name), (_, path) in zip(HOCKEY_SMOKE_SHOTS, hockey_shots):
            gxtex.write_png(os.path.join(args.png, "hockey_%s.png" % name), W, H, opaque(load_rgba(path)))

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
    print("  menus: scenes %s over %d frames; %d screens (%s); save file written (the format party_save "
          "writes)" % (" -> ".join(str(x) for x in menu_seq), len(fm), len(shots), ", ".join(n for _, n in MENU_SHOTS)))
    print("  shooting range (short): states %s; score %d (%d hits) over %d score changes in play, round 1 "
          "scoring: %s; sound in play: rms %.1f; %d distinct screens; log: %d allowed warnings, %d unhandled "
          "registers, all documented" % shoot)
    print("  laser hockey (short, a save with every game open): scenes %s; %s" % (
        " -> ".join(str(x) for _, x in sh), hockey_summary(hky)))
    if live_result:
        wall, want, lv = live_result
        print("  live (headless, dummy audio, synthetic SDL events): %.3f s wall for %.3f s game time; audio "
              "underruns %s, overruns %s, %s frames queued, %s re-anchors; %s events; rumble %s on the virtual gamepad"
              % (wall, want, lv["underruns"], lv["overruns"], lv["queued"], lv["late"], lv["events"],
                 lv["testrumble"]))
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
