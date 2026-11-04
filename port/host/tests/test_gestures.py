"""Motion gestures through the live input layer (port/docs/HOST_BUILD.md §37).

Each run plays a minigame from the party menu (tests/boot/menus_saved.txt, a save with every
game open) with the live input layer on top of the script (--live-input: merge mode). The
game-side players run in `play 2`: they navigate and aim as in the playtests but leave the
motions to the live layer, pressing its default motion keys through SDL's queue (Q / E twist,
W / A / D tilt, F shake, R pull and thrust, G push). Laser Hockey uses an events file
(keys, the mouse wheel, the middle button, a virtual gamepad's left stick). Every run has a
config file with a [tanks] section (§44: W / A / S / D drive the tank there); Tanks! runs
without its player, driven by an events file's keys. The checks read
the FRAME lines' `motion=` probe (host_probe_games.cpp: what each game's own detector made of
remote 0) and `game=`: the twist turns Laser Hockey's paddle and Pose Mii's cursor, the
tilt makes the bull run and turn, the shake makes it jump and hooks a fish, the pull and
thrust shoots in Billiards, the push moves Find Mii's distance ratio; in Tanks! (`game=`)
D and W move the tank, D held from the menu into the game only after it is pressed again,
and Charge! still tilts with W / A / D.
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_boot as tb  # noqa: E402
import test_gxlyt  # noqa: E402

PLAY_AT = 3850  # the scripts' `play` frame (after "A: Start")

# game: (script, frames, events file or None)
RUNS = {
    "hockey": ("hockey_smoke.txt", 4300, "gestures_hockey.txt"),
    "posemii": ("posemii.txt", PLAY_AT + 2400, None),
    "charge": ("charge.txt", PLAY_AT + 2400, None),
    "fishing": ("fishing.txt", PLAY_AT + 3600, None),
    "billiards": ("billiards.txt", PLAY_AT + 2400, None),
    "findmii": ("findmii.txt", PLAY_AT + 1200, None),
    "tanks": ("tanks.txt", 4500, "gestures_tanks.txt"),
}
# Runs without the game-side player (the events file plays)
NO_PLAYER = {"tanks"}

# Per-game bindings (§44): the tilt keys drive the tank in Tanks! only
CONFIG = """# tests/test_gestures.py
[tanks]
key W UP
key A LEFT
key S DOWN
key D RIGHT
"""


def make_script(scratch, game, part, frames):
    path = os.path.join(scratch, "gest_%s.txt" % game)
    with open(path, "w") as out:
        for name in ("menus_saved.txt", part):
            with open(os.path.join(HERE, "boot", name)) as f:
                for line in f:
                    if line.startswith("end"):
                        continue
                    w = line.split()
                    if len(w) == 3 and w[1] == "play" and w[2] == "1":
                        if game in NO_PLAYER:
                            continue
                        line = "%s play 2\n" % w[0]
                    out.write(line)
        out.write("end 100000\n")
    return path


def run(boot, disc, scratch, game, env):
    part, frames, events = RUNS[game]
    d = os.path.join(scratch, game)
    for sub in ("nand", "data"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    tb.party_save(os.path.join(d, "nand"))
    cmd = [boot, "--disc", disc, "--nand", os.path.join(d, "nand"), "--data-dir", os.path.join(d, "data"),
           "--frames", str(frames), "--input", make_script(scratch, game, part, frames), "--live-input",
           "--log-level", "0", "--config", os.path.join(scratch, "input.cfg")]
    if events:
        cmd += ["--events", os.path.join(HERE, "boot", events)]
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=3600)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-3000:])
        return game, None
    switches = [l for l in p.stderr.splitlines() if l.startswith("boot: input:")]
    vals = []
    for line in p.stdout.splitlines():
        f = line.split("\t")
        if f[0] != "FRAME":
            continue
        kv = dict(x.split("=", 1) for x in f[1:] if "=" in x)
        if game == "tanks" and kv.get("game", "").startswith("tnk:"):
            vals.append((int(f[1]), "tnk", [int(x) for x in kv["game"][4:].split(",")], kv["game"]))
        if "motion" in kv:
            tag, v = kv["motion"].split(":", 1)
            vals.append((int(f[1]), tag, [int(x) for x in v.split(",")], kv.get("game", "")))
    return game, (vals, switches)


def of(vals, tag):
    return [(f, v, g) for f, t, v, g in vals if t == tag]


def at(vals, frame):
    """The values at the first frame >= frame."""
    for f, v, g in vals:
        if f >= frame:
            return v
    return None


def check_hockey(vals, errs):
    v = of(vals, "hky")
    # tests/boot/gestures_hockey.txt: E held 15 frames (+30 degrees at 120 degrees a second),
    # the wheel 3 steps back (-30), Q 15 frames (-30), the middle button (reset), the left
    # stick at half (+30: tilt_angle 60)
    want = [(4085, 300), (4125, 0), (4165, -300), (4205, 0), (4255, 300)]
    out = []
    for frame, roll in want:
        x = at(v, frame)
        # the paddle's twist follows KPAD's roll (rate limit 8 degrees a frame), KPAD's within
        # a degree or so of the remote's
        if x is None or abs(x[1] - roll) > 25 or abs(x[0] - x[1]) > 25:
            errs.append("hockey: at frame %d paddle twist %s, KPAD roll %s, expected %.1f degrees"
                        % (frame, x and x[0] / 10, x and x[1] / 10, roll / 10))
        out.append("%+.0f" % (x[0] / 10 if x else 0))
    return "paddle twist " + " / ".join(out) + " degrees (E, wheel, Q, middle button, left stick)"


def check_posemii(vals, errs):
    v = of(vals, "bom")
    games = [g for _, _, g in v if g.startswith("bom:")]
    hits = max((int(g[4:].split(",")[3]) for g in games), default=0)
    rolls = [x[1] for _, x, _ in v]
    turned = max(rolls, default=0) - min(rolls, default=0)
    if turned < 300 or hits < 10:
        errs.append("pose mii: KPAD roll range %.1f degrees, %d bubbles matched" % (turned / 10, hits))
    return "twist keys: KPAD roll over %.0f degrees; %d bubbles matched" % (turned / 10, hits)


def check_charge(vals, errs):
    v = of(vals, "cow")
    speed = max((x[1] for _, x, _ in v), default=0)
    yaws = [x[0] for _, x, _ in v]
    jumps = sum(1 for (_, a, _), (_, b, _) in zip(v, v[1:]) if a[2] <= 0 < b[2])
    rolls = [x[3] for _, x, _ in v]
    if speed < 50 or max(yaws, default=0) - min(yaws, default=0) < 200 or jumps < 2 or max(rolls, default=0) < 500:
        errs.append("charge: top speed %d, yaw range %d mrad, %d jumps, KPAD roll max %.1f"
                    % (speed, max(yaws, default=0) - min(yaws, default=0), jumps, max(rolls, default=0) / 10))
    return "W: KPAD roll %.0f degrees, top speed %.2f; A / D: yaw over %.2f rad; F: %d jumps" % (
        max(rolls, default=0) / 10, speed / 100, (max(yaws, default=0) - min(yaws, default=0)) / 1000, jumps)


def check_fishing(vals, errs):
    v = of(vals, "fsh")
    hooks = sum(1 for (_, a, _), (_, b, _) in zip(v, v[1:]) if a[1] != 0 and b[1] == 0)
    caught = max((x[2] for _, x, _ in v), default=0)
    if hooks < 1 or caught < 1:
        errs.append("fishing: %d lifts caught by the game's detector, %d fish caught" % (hooks, caught))
    return "F on a bite: %d fish hooked by the lift, %d caught" % (hooks, caught)


def check_billiards(vals, errs):
    v = of(vals, "bil")
    shots = max((x[1] for _, x, _ in v), default=0)
    if shots < 2:
        errs.append("billiards: %d shots from the pull and thrust" % shots)
    return "R held and released (B held): %d shots" % shots


def check_findmii(vals, errs):
    v = of(vals, "wly")
    ratios = [x[0] for _, x, _ in v]
    if not ratios or max(ratios) < 900 or min(ratios) > 100:
        errs.append("find mii: distance ratio %s..%s" % (min(ratios, default=None), max(ratios, default=None)))
    return "G held: the cursor's distance ratio %.2f..%.2f" % (min(ratios, default=0) / 1000,
                                                              max(ratios, default=0) / 1000)


def check_tanks(vals, errs, switches):
    v = of(vals, "tnk")  # state, stage, kills, lives, enemies, x, z, alive

    def pos(frame):
        x = at(v, frame)
        return (x[5], x[6]) if x else (0, 0)
    start, held, right, up = pos(4100), pos(4295), pos(4385), pos(4485)
    if held != start:
        errs.append("tanks: D held from the menu moved the tank %s -> %s" % (start, held))
    if right[0] - held[0] < 50 or abs(right[1] - held[1]) > 5:
        errs.append("tanks: D (RIGHT in [tanks]) moved the tank %s -> %s" % (held, right))
    if right[1] - up[1] < 50 or abs(up[0] - right[0]) > 5:
        errs.append("tanks: W (UP in [tanks]) moved the tank %s -> %s" % (right, up))
    if switches != ["boot: input: the [tanks] bindings"]:
        errs.append("tanks: binding switches %s" % switches)
    return "[tanks] WASD: D held from the menu: still at %s; D: x %+d; W: z %+d" % (
        held, right[0] - held[0], up[1] - right[1])


CHECKS = {"hockey": check_hockey, "posemii": check_posemii, "charge": check_charge,
          "fishing": check_fishing, "billiards": check_billiards, "findmii": check_findmii, "tanks": check_tanks}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--only", default=None)
    args = ap.parse_args()
    env = dict(os.environ)
    for var in ("DISPLAY", "WAYLAND_DISPLAY"):  # never a window on the user's desktop
        env.pop(var, None)
    env["SDL_VIDEO_DRIVER"] = "dummy"
    env["SDL_AUDIO_DRIVER"] = "dummy"
    env["VK_ICD_FILENAMES"] = test_gxlyt.lavapipe_icd()
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    env.setdefault("LP_NUM_THREADS", "4")
    scratch = os.path.join(args.scratch, "gestures")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    with open(os.path.join(scratch, "input.cfg"), "w") as f:
        f.write(CONFIG)
    games = args.only.split(",") if args.only else list(RUNS)
    errs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(lambda g: run(args.boot, args.disc, scratch, g, env), games))
    for game, res in results:
        if res is None:
            errs.append("%s: the boot failed" % game)
            continue
        vals, switches = res
        if game == "tanks":
            print("  %-10s %s" % (game, check_tanks(vals, errs, switches)))
            continue
        if switches:  # the [tanks] section never applies in the other games
            errs.append("%s: binding switches %s" % (game, switches))
        print("  %-10s %s" % (game, CHECKS[game](vals, errs)))
    for e in errs:
        print("  ERROR: " + e)
    print("gestures: %d errors" % len(errs))
    sys.exit(1 if errs else 0)


if __name__ == "__main__":
    main()
