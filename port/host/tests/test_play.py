#!/usr/bin/env python3
"""Whole minigames from the party menu, headless, twice each (port/docs/HOST_BUILD.md §31-§33).

`make playtest` (64-bit): the long runs that `make test` leaves out, to run before committing
a game milestone. Runs of build/host64/boot, four at a time:

  P1, P2  the menus from a fresh NAND (tests/boot/menus.txt), then Shooting Range
          (tests/boot/shooting.txt): five rounds, the results, back to the party menu;
  Q1, Q2  a NAND with a save that has every game open (test_boot.party_save), the menus
          (tests/boot/menus_saved.txt), then Laser Hockey (tests/boot/hockey.txt): the
          two-minute game against the CPU, the results, back to the party menu.

Checks (the game's own state, read by host_probe.cpp; test_boot.py has the helpers):
Shooting Range as in §31 (states, score rising every round, sound, screens, log); Laser
Hockey: the states (intro, play, goals, time up, result), the clock, paddle-puck contacts,
goals scored by player 0, scores changing only at goals, screens, log; every frame hash and
the scene trace identical over each pair of runs.

  then the other seven games (play_games.py: Find Mii, Tanks!, Billiards, Fishing, Table
  Tennis, Pose Mii, Charge!), each twice from the all-games save: menus_saved.txt, then the
  game's script; the game-side player (host_probe_games.cpp) plays to the results and back.
  Checks per game in play_games.py (the game's own state), plus the scenes, five screens
  (in the game's scene, distinct), the log and determinism.

`--only NAME,...` runs a subset (shooting, hockey, or a game's script name).

`--set 2` (`make playtest2`, §36) runs the two-player games instead (play_games.GAMES2: all
nine, each twice from the two-player party menu, both remotes played) and the later-level
runs (play_games.LONG: Find Mii to stage 13, Pose Mii to level 5, Table Tennis to its 100
rallies; once each). `--only` then takes their keys (shooting2, hockey2, findmii2, ...,
findmii_long, ...).
"""

import argparse
import concurrent.futures
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_boot as tb  # noqa: E402
import play_games  # noqa: E402

PLAY_FRAMES = 16200
SHOOT_SHOTS = [(5100, "explanation"), (5480, "round_start"), (5904, "hit"), (7500, "targets"),
               (14950, "finished"), (15300, "medal"), (15740, "results_menu"), (16190, "back_to_menu")]
SHOOT_SCENES = {"explanation": "ESceneID_RPPartyMainScene", "back_to_menu": "ESceneID_RPPartyMainScene"}
HOCKEY_FRAMES = 15000
HOCKEY_SHOTS = [(3950, "start"), (4400, "rally"), (5250, "goal"), (13850, "game_over"), (14350, "results"),
                (14550, "results_menu"), (14990, "back_to_menu")]
HOCKEY_SCENES = {"back_to_menu": "ESceneID_RPPartyMainScene"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot", required=True)
    ap.add_argument("--disc", required=True, help="extracted disc files directory")
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--gpu", action="store_true", help="use the real Vulkan driver, not lavapipe")
    ap.add_argument("--png", default=None, help="directory for PNGs of the screens checked")
    ap.add_argument("--only", default=None, help="comma-separated subset to run")
    ap.add_argument("--jobs", type=int, default=4, help="runs at once")
    ap.add_argument("--set", default="1", choices=("1", "2"), help="1: one player; 2: two players and later levels")
    args = ap.parse_args()
    only = set(args.only.split(",")) if args.only else None

    env = dict(os.environ)
    if not args.gpu:
        icd = tb.test_gxlyt.lavapipe_icd()
        if icd is None:
            raise SystemExit("FAIL: lavapipe (mesa-vulkan-drivers) not found")
        env["VK_ICD_FILENAMES"] = icd
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    env.setdefault("LP_NUM_THREADS", "3")
    scratch = os.path.join(args.scratch, "play")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch, exist_ok=True)
    repo = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    ids = tb.scene_ids(repo)
    errs = []

    shooting = tb.script(scratch, "menus_shooting.txt", ["menus.txt", "shooting.txt"])
    hockey = tb.script(scratch, "menus_hockey.txt", ["menus_saved.txt", "hockey.txt"])
    shoot_shots = [(f, os.path.join(scratch, "shoot_%s.rgba" % n)) for f, n in SHOOT_SHOTS]
    hockey_shots = [(f, os.path.join(scratch, "hockey_%s.rgba" % n)) for f, n in HOCKEY_SHOTS]
    shoot_audio = os.path.join(scratch, "shoot_audio.raw")
    jobs = {}
    if args.set == "2":
        return run_set2(args, env, scratch, ids, only)
    if only is None or "shooting" in only:
        jobs["p1"] = (["--frames", str(PLAY_FRAMES), "--input", shooting, "--audio", shoot_audio]
                      + [x for f, p in shoot_shots for x in ("--save", "%d:%s" % (f, p))], False)
        jobs["p2"] = (["--frames", str(PLAY_FRAMES), "--input", shooting], False)
    if only is None or "hockey" in only:
        jobs["q1"] = (["--frames", str(HOCKEY_FRAMES), "--input", hockey]
                      + [x for f, p in hockey_shots for x in ("--save", "%d:%s" % (f, p))], True)
        jobs["q2"] = (["--frames", str(HOCKEY_FRAMES), "--input", hockey], True)
    games = [g for g in play_games.GAMES if only is None or g[1] in only]
    game_shots = {}
    for name, scr, tag, scene, frames, spec, check in games:
        path = tb.script(scratch, "menus_%s.txt" % scr, ["menus_saved.txt", "%s.txt" % scr])
        game_shots[scr] = [(f, os.path.join(scratch, "%s_%s.rgba" % (scr, n))) for f, n in spec]
        jobs[scr + "1"] = (["--frames", str(frames), "--input", path]
                           + [x for f, p in game_shots[scr] for x in ("--save", "%d:%s" % (f, p))], True)
        jobs[scr + "2"] = (["--frames", str(frames), "--input", path], True)
    # the longest first
    order = sorted(jobs, key=lambda k: -int(jobs[k][0][1]))
    start = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        # four runs at once go at about 5 frames a second each on 12 cores
        futures = {k: pool.submit(tb.run, args.boot, args.disc, scratch, k, env, jobs[k][0], jobs[k][1], 7200)
                   for k in order}
        results = {k: f.result() for k, f in futures.items()}
    wall = time.time() - start

    def same(a, b, what):
        fa, sa, fb, sb = results[a][1], results[a][2], results[b][1], results[b][2]
        ok = [x[5] for x in fa] == [x[5] for x in fb] and sa == sb and len(fa) == len(fb)
        if not ok:
            n = next((i for i, (x, y) in enumerate(zip(fa, fb)) if x[5] != y[5]), None)
            errs.append("%s: run %s differs from run %s (first frame %s)" % (what, b.upper(), a.upper(), n))
        return ok

    party_id = ids["ESceneID_RPPartyMainScene"]
    common = [-1, ids["ESceneID_RPSysBootScene"], ids["ESceneID_RPPartyPackTitleScene"],
              ids["ESceneID_RPSysPlayerSelectScene"], party_id]

    lines = []
    if "p1" in results:
        # --- Shooting Range ----------------------------------------------------------------------
        _, fp, sp, endp, errp = results["p1"]
        want = common + [ids["ESceneID_RPDucScene"], party_id]
        if [s for _, s in sp] != want or endp is None or len(fp) != PLAY_FRAMES:
            errs.append("shooting: scenes %s, %d frames, END %s" % ([s for _, s in sp], len(fp), endp is not None))
        shoot = tb.check_shooting(fp, errp, shoot_shots, shoot_audio, ids, errs, SHOOT_SHOTS, SHOOT_SCENES,
                                  tb.SHOOT_STATES, {1, 2, 3, 4, 5})
        shoot_same = same("p1", "p2", "shooting")

        lines.append("  shooting range: scenes %s over %d frames; states %s; score %d (%d hits) over %d score changes "
                     "in play, every round scoring: %s; sound in play: rms %.1f; %d distinct screens; log: %d allowed "
                     "warnings, %d unhandled registers, all documented"
                     % ((" -> ".join(str(s) for _, s in sp), len(fp)) + shoot))
        lines.append("    hashes and scenes identical over two runs: %s" % shoot_same)
    if "q1" in results:
        # --- Laser Hockey ------------------------------------------------------------------------
        _, fq, sq, endq, errq = results["q1"]
        want = common + [ids["ESceneID_RPHkyScene"], party_id]
        if [s for _, s in sq] != want or endq is None or len(fq) != HOCKEY_FRAMES:
            errs.append("hockey: scenes %s, %d frames, END %s" % ([s for _, s in sq], len(fq), endq is not None))
        hky = tb.check_hockey(fq, errq, hockey_shots, ids, errs, HOCKEY_SHOTS, HOCKEY_SCENES, True)
        hockey_same = same("q1", "q2", "hockey")
        lines.append("  laser hockey: scenes %s over %d frames; %s" % (" -> ".join(str(s) for _, s in sq), len(fq),
                                                                   tb.hockey_summary(hky)))
        lines.append("    hashes and scenes identical over two runs: %s" % hockey_same)
    for name, scr, tag, scene, frames, spec, check in games:
        _, fg, sg, endg, errg = results[scr + "1"]
        want = common + [ids[scene], party_id]
        if [s for _, s in sg] != want or endg is None or len(fg) != frames:
            errs.append("%s: scenes %s, %d frames, END %s" % (name, [s for _, s in sg], len(fg), endg is not None))
        vals = tb.game_values(fg, tag)
        if not vals:
            errs.append("%s: no %s: probe values" % (name, tag))
            summary = "no probe values"
        else:
            summary = check(vals, errs)
        distinct = tb.check_screens(fg, game_shots[scr], spec, {"back_to_menu": "ESceneID_RPPartyMainScene"},
                                    ids[scene], ids, errs, name)
        if scr in play_games.SCREEN_CHECKS:
            summary += "; " + play_games.SCREEN_CHECKS[scr](
                {n: p for (f, n), (_, p) in zip(spec, game_shots[scr])}, errs)
        allowed, unhandled = tb.check_log(errg, errs, name)
        lines.append("  %s: scenes %s over %d frames; %s; %d distinct screens; log: %d allowed warnings, %d "
                     "unhandled registers, all documented" % (name, " -> ".join(str(s) for _, s in sg), len(fg),
                                                             summary, distinct, allowed, unhandled))
        lines.append("    hashes and scenes identical over two runs: %s" % same(scr + "1", scr + "2", name))
        if args.png:
            os.makedirs(args.png, exist_ok=True)
            for (f, n), (_, p) in zip(spec, game_shots[scr]):
                tb.gxtex.write_png(os.path.join(args.png, "%s_%s.png" % (scr, n)), tb.W, tb.H,
                                   tb.opaque(tb.load_rgba(p)))

    if args.png and "p1" in results:
        os.makedirs(args.png, exist_ok=True)
        for (f, n), (_, p) in zip(SHOOT_SHOTS, shoot_shots):
            tb.gxtex.write_png(os.path.join(args.png, "shoot_%s.png" % n), tb.W, tb.H, tb.opaque(tb.load_rgba(p)))
    if args.png and "q1" in results:
        os.makedirs(args.png, exist_ok=True)
        for (f, n), (_, p) in zip(HOCKEY_SHOTS, hockey_shots):
            tb.gxtex.write_png(os.path.join(args.png, "hockey_%s.png" % n), tb.W, tb.H, tb.opaque(tb.load_rgba(p)))

    print("test_play: %d runs on %s in %.0f s wall" % (len(jobs), "lavapipe" if not args.gpu else "the GPU", wall))
    for line in lines:
        print(line)
    for e in errs:
        print("  ERROR: " + e)
    print("errors: %d" % len(errs))
    print("OK" if not errs else "FAIL")
    return 0 if not errs else 1


def build_script(scratch, name, parts, extra):
    """The parts (tests/boot/*.txt, each without its `end`) and extra lines, in frame order."""
    path = os.path.join(scratch, name)
    lines = []
    for part in parts:
        with open(os.path.join(HERE, "boot", part)) as f:
            lines += [l for l in f if not l.startswith("end")]
    lines += [l + "\n" for l in extra]
    with open(path, "w") as out:
        out.writelines(lines)
        out.write("end 100000\n")
    return path


def run_set2(args, env, scratch, ids, only):
    """Two players (play_games.GAMES2) and the later levels (play_games.LONG), §36."""
    entries = []
    for name, parts, tag, scene, frames, spec, check, runs in play_games.GAMES2:
        key = parts[1][:-4] if parts[1].endswith("2.txt") else parts[1][:-4] + "2"
        entries.append((key, name, parts, [], tag, scene, frames, spec, check, runs))
    for name, parts, extra, tag, scene, frames, spec, check, runs in play_games.LONG:
        entries.append((parts[1][:-4] + "_long", name, parts, extra, tag, scene, frames, spec, check, runs))
    entries = [e for e in entries if only is None or e[0] in only]
    errs, jobs, shots = [], {}, {}
    for key, name, parts, extra, tag, scene, frames, spec, check, runs in entries:
        path = build_script(scratch, "%s.txt" % key, parts, extra)
        shots[key] = [(f, os.path.join(scratch, "%s_%s.rgba" % (key, n))) for f, n in spec]
        jobs[key + "_1"] = (["--frames", str(frames), "--input", path]
                            + [x for f, p in shots[key] for x in ("--save", "%d:%s" % (f, p))], True)
        if runs > 1:
            jobs[key + "_2"] = (["--frames", str(frames), "--input", path], True)
    order = sorted(jobs, key=lambda k: -int(jobs[k][0][1]))
    start = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {k: pool.submit(tb.run, args.boot, args.disc, scratch, k, env, jobs[k][0], jobs[k][1], 14400)
                   for k in order}
        results = {k: f.result() for k, f in futures.items()}
    wall = time.time() - start
    party_id = ids["ESceneID_RPPartyMainScene"]
    common = [-1, ids["ESceneID_RPSysBootScene"], ids["ESceneID_RPPartyPackTitleScene"],
              ids["ESceneID_RPSysPlayerSelectScene"], party_id]
    lines = []
    for key, name, parts, extra, tag, scene, frames, spec, check, runs in entries:
        _, fg, sg, endg, errg = results[key + "_1"]
        want = common + [ids[scene], party_id]
        if [x for _, x in sg] != want or endg is None or len(fg) != frames:
            errs.append("%s: scenes %s, %d frames, END %s" % (name, [x for _, x in sg], len(fg), endg is not None))
        vals = tb.game_values(fg, tag)
        summary = check(vals, errs) if vals else "no probe values"
        if not vals:
            errs.append("%s: no %s: probe values" % (name, tag))
        distinct = tb.check_screens(fg, shots[key], spec, {"back_to_menu": "ESceneID_RPPartyMainScene"},
                                    ids[scene], ids, errs, name)
        allowed, unhandled = tb.check_log(errg, errs, name)
        lines.append("  %s: scenes %s over %d frames; %s; %d distinct screens; log: %d allowed warnings, %d "
                     "unhandled registers, all documented" % (name, " -> ".join(str(x) for _, x in sg), len(fg),
                                                             summary, distinct, allowed, unhandled))
        if runs > 1:
            _, fb, sb, _, _ = results[key + "_2"]
            ok = [x[5] for x in fg] == [x[5] for x in fb] and sg == sb and len(fg) == len(fb)
            if not ok:
                n = next((i for i, (x, y) in enumerate(zip(fg, fb)) if x[5] != y[5]), None)
                errs.append("%s: the second run differs (first frame %s)" % (name, n))
            lines.append("    hashes and scenes identical over two runs: %s" % ok)
        if args.png:
            os.makedirs(args.png, exist_ok=True)
            for (f, n), (_, p) in zip(spec, shots[key]):
                tb.gxtex.write_png(os.path.join(args.png, "%s_%s.png" % (key, n)), tb.W, tb.H,
                                   tb.opaque(tb.load_rgba(p)))
    print("test_play (two players, later levels): %d runs on %s in %.0f s wall" % (
        len(jobs), "lavapipe" if not args.gpu else "the GPU", wall))
    for line in lines:
        print(line)
    for e in errs:
        print("  ERROR: " + e)
    print("errors: %d" % len(errs))
    print("OK" if not errs else "FAIL")
    return 0 if not errs else 1


if __name__ == "__main__":
    sys.exit(main())
