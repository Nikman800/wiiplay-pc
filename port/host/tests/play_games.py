"""The seven minigames after Shooting Range and Laser Hockey, played whole (port/docs/HOST_BUILD.md §33).

Each game runs from the party menu (tests/boot/menus_saved.txt, a save with every game open)
through its own script (tests/boot/<script>.txt: the icon, "A: Start", then `play 1`): the
game-side player in src/host_probe_games.cpp reads the game's own state and works the remote,
and on the results points at "Play a different game". The probe's FRAME field (`game=TAG:...`)
gives the game's own state; the checks below read only that, the scene trace, the saved
frames and the log. test_play.py runs each game twice (determinism).
"""

import test_boot as tb


def seq_of(values, k=0):
    """The values of field k with repeats removed."""
    out = []
    for _, g in values:
        if not out or out[-1] != g[k]:
            out.append(g[k])
    return out


def changes(values, k):
    """(previous, current) value pairs at every frame where field k changes."""
    return [(a, b) for (_, a), (_, b) in zip(values, values[1:]) if a[k] != b[k]]


# --- Find Mii (wly: state, stage, timer, targets, found, question type, penalty) ------------
WLY_STAGES = 5  # host_probe_games.cpp: stages played to the answer, then wrong picks


def check_findmii(vals, errs):
    states = seq_of(vals)
    clears = sum(1 for a, b in changes(vals, 0) if b[0] == 3)
    stage = max((g[1] for _, g in vals), default=0)
    # finds only from the search, never more than the targets (look-alikes resolve together)
    found_bad = [(a, b) for a, b in changes(vals, 4) if b[4] > a[4] and (b[4] > b[3] or a[0] != 2)]
    wrong = sum(1 for a, b in changes(vals, 6) if b[6] > a[6])
    cut = [(a, b) for a, b in changes(vals, 2) if a[0] == 2 and b[0] == 2 and b[2] < a[2] - 1]
    if states[:3] != [0, 1, 2] or states[-1] != 4 or 3 not in states:
        errs.append("find mii: states %s" % states)
    if clears < WLY_STAGES or stage < WLY_STAGES + 1:
        errs.append("find mii: %d stages cleared, reached stage %d" % (clears, stage))
    if found_bad:
        errs.append("find mii: a find outside the search or by more than one: %s" % found_bad[:2])
    # a wrong pick blocks picking (penalty) and takes time off (5 x 60 frames)
    if wrong < 1 or not cut:
        errs.append("find mii: %d wrong picks, %d time cuts" % (wrong, len(cut)))
    return "states %s; %d stages cleared (stage %d reached); %d wrong picks, each taking time off; time up" % (
        " -> ".join(str(s) for s in states[:8]) + (" ... " + str(states[-1]) if len(states) > 8 else ""),
        clears, stage, wrong)


# --- Pose Mii (bom: state, level, score, hits, combo, misses, miss max, level timer, borders) --
BOM_LEVELS = 1  # levels the player must clear (it stops matching at level 2; the misses come sooner)


def check_posemii(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 11
    level = max((g[1] for _, g in vals), default=0)
    hits = final[3]
    # the score moves with a match, or by the level bonus as a level ends (states 6, 7)
    score_bad = [(a, b) for a, b in changes(vals, 2) if b[3] == a[3] and b[0] not in (6, 7)]
    # the three miss-counter borders: 16 apart once dropped in (the 64-bit slot-walk bug put
    # the second and third at mOffsetZ[2] and a word of mNodeMtx)
    play = [g for _, g in vals if g[0] == 3]
    border_bad = [g for g in play if (g[8] - g[9], g[9] - g[10]) != (160, 160)]
    for want in (0, 1, 2, 3, 6, 7, 9, 11, 12):
        if want not in states:
            errs.append("pose mii: states %s (no %d)" % (states, want))
            break
    if level < BOM_LEVELS or hits < 50:
        errs.append("pose mii: level %d, %d bubbles matched" % (level, hits))
    if score_bad:
        errs.append("pose mii: the score changed without a match: %s" % score_bad[:2])
    if final[5] != final[6] or final[6] <= 0:
        errs.append("pose mii: %d of %d misses at the end" % (final[5], final[6]))
    if not play or border_bad:
        errs.append("pose mii: the borders' z in play %s" % (border_bad[:1],))
    return ("states %s; levels %d, %d bubbles matched, score %d, %d/%d misses; borders 16 apart in all %d "
            "frames of play" % (" -> ".join(str(s) for s in states[:12]) + " ...", level + 1, hits, final[2],
                                 final[5], final[6], len(play)))


# --- Table Tennis (pnp: state, ball state, hits, rallies, ball x y z, racket x z, hitter, goal) --
PNP_RALLIES = 20


def check_tabletennis(vals, errs):
    states = seq_of(vals)
    states = states[states.index(0):] if 0 in states else states  # (the game's state before its Reset)
    final = vals[-1][1] if vals else (0,) * 13
    hits_bad = [(a, b) for a, b in changes(vals, 2) if b[2] not in (0, a[2] + 1) or (b[2] > a[2] and b[0] != 3)]
    # player 0's returns: the ball hit by side 0
    returns = sum(1 for a, b in changes(vals, 9) if b[9] == 0)
    if states[:4] != [0, 1, 2, 3] or states[-1] != 5:
        errs.append("table tennis: states %s" % states)
    if final[3] < PNP_RALLIES:
        errs.append("table tennis: %d rallies" % final[3])
    if hits_bad:
        errs.append("table tennis: the hit count changed outside a rally or by more than one: %s" % hits_bad[:2])
    return "states %s; %d returns by player 0, %d rallies (final hit count %d)" % (
        " -> ".join(str(s) for s in states), returns, final[3], final[2])


# --- Billiards (bil: game state, player state, shots, fouls, balls off, cue x z, lowest) ------
def check_billiards(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 8
    off_bad = [(a, b) for a, b in changes(vals, 4) if b[4] < a[4] or a[1] not in (4, 6, 7)]
    lowest = [g[7] for _, g in vals if g[7] > 0]
    if states[:3] != [0, 1, 2] or states[-1] != 5 or 3 not in states:
        errs.append("billiards: states %s" % states)
    if final[4] != 9:
        errs.append("billiards: %d object balls off the table at the end" % final[4])
    if off_bad:
        errs.append("billiards: a ball left the table outside a shot: %s" % off_bad[:2])
    if lowest != sorted(lowest):
        errs.append("billiards: the lowest ball went down")
    return "states %s; %d shots, %d fouls, all 9 object balls off: %s" % (
        " -> ".join(str(s) for s in states), final[2], final[3], final[4] == 9)


# --- Fishing (fsh: start state, player state, timer, time limit, caught, points, hooked, result) --
def check_fishing(vals, errs):
    starts = [x for x in seq_of(vals) if x != 0]
    pstates = set(g[1] for _, g in vals)
    final = vals[-1][1] if vals else (0,) * 8
    points_bad = [(a, b) for a, b in changes(vals, 5) if b[4] == a[4]]
    caught = max((g[4] for _, g in vals), default=0)
    timer = max((g[2] for _, g in vals), default=0)
    if starts[:4] != [1, 2, 3, 4]:
        errs.append("fishing: start states %s" % starts)
    if not {1, 2, 3, 4} <= pstates:
        errs.append("fishing: player states %s" % sorted(pstates))
    if caught < 3 or final[5] <= 0:
        errs.append("fishing: %d fish caught, %d points" % (caught, final[5]))
    if points_bad:
        errs.append("fishing: the points changed without a catch: %s" % points_bad[:2])
    if timer < final[3] - 1 or final[7] < 5:
        errs.append("fishing: timer %d of %d, result state %d" % (timer, final[3], final[7]))
    return "start states %s; %d fish caught, %d points; the clock ran out (%d frames); result state %d" % (
        " -> ".join(str(s) for s in starts), caught, final[5], timer, final[7])


# --- Charge! (cow: state, score, bonus, time, x, z, groups knocked, speed, debug) --------------
def check_charge(vals, errs):
    states = seq_of(vals)
    play = [g for _, g in vals if g[0] in (7, 8)]
    score = max((g[1] for _, g in vals), default=0)
    # points from scarecrows in play; at the goal (9-14) the time bonus is counted into the score
    score_bad = [(a, b) for a, b in changes(vals, 1) if b[1] > a[1] and a[0] not in (7, 8, 9, 10, 11, 12, 13, 14)]
    run = (play[0][5] - min(g[5] for g in play)) if play else 0
    goal = any(9 <= s <= 14 for s in states)
    for want in (1, 4, 5, 6, 7, 8, 22, 24):
        if want not in states:
            errs.append("charge: states %s (no %d)" % (states, want))
            break
    if score <= 0 or score_bad:
        errs.append("charge: score %d, changes outside play %s" % (score, score_bad[:2]))
    if run < 3000 or not goal:
        errs.append("charge: the cow ran %d along the course, goal %s" % (run, goal))
    return "states %s; %d points from scarecrows, %d groups knocked; ran %d along the course; goal: %s" % (
        " -> ".join(str(s) for s in states), score, max(g[6] for _, g in vals), run, goal)


# --- Tanks! (tnk: state, stage, kills, lives, enemies alive, x, z, alive) ----------------------
def check_tanks(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 8
    kills_bad = [(a, b) for a, b in changes(vals, 2) if b[2] != a[2] + 1 or a[0] != 4]
    # lives: set at the start (3), then one fewer per tank lost (a stage bonus adds one)
    lives_bad = [(a, b) for a, b in changes(vals, 3) if a[3] != 0 and b[3] not in (a[3] - 1, a[3] + 1)]
    stage = max((g[1] for _, g in vals), default=0)
    # mission 1 cleared: its enemy destroyed in play, the clear, then stage 1 loaded
    m1 = any(a[1] == 0 and b[0] == 6 for a, b in changes(vals, 0))
    if not m1 or stage < 1:
        errs.append("tanks: mission 1 not cleared (states %s, stage %d)" % (states, stage))
    if 9 not in states or states[-1] != 12:
        errs.append("tanks: states %s" % states)
    if kills_bad or lives_bad:
        errs.append("tanks: kills %s / lives %s changed out of play" % (kills_bad[:2], lives_bad[:2]))
    return "states %s; missions cleared %d, %d enemy tanks destroyed, lives %d at the end" % (
        " -> ".join(str(s) for s in states[:14]) + " ...", stage, final[2], final[3])


# name, script, probe tag, scene, frames, saved screens (start, mid-play, a scoring moment,
# the results, back on the party menu), check
GAMES = [
    ("find mii", "findmii", "wly", "ESceneID_RPWlyScene", 8600,
     [(4150, "start"), (4650, "found"), (6650, "wrong_pick"), (8150, "results"), (8590, "back_to_menu")],
     check_findmii),
    ("tanks", "tanks", "tnk", "ESceneID_RPTnkScene", 8900,
     [(4150, "start"), (4400, "shot"), (4650, "mission_cleared"), (8150, "results"), (8890, "back_to_menu")],
     check_tanks),
    ("billiards", "billiards", "bil", "ESceneID_RPBilScene", 11100,
     [(4150, "start"), (4900, "rolling"), (9900, "score"), (10150, "results"), (11090, "back_to_menu")],
     check_billiards),
    ("fishing", "fishing", "fsh", "ESceneID_RPFshScene", 12900,
     [(4150, "start"), (5150, "catch"), (5400, "points"), (11900, "results"), (12890, "back_to_menu")],
     check_fishing),
    ("table tennis", "tabletennis", "pnp", "ESceneID_RPPnpScene", 7400,
     [(4150, "start"), (4650, "rally"), (5150, "rally_14"), (6650, "results"), (7390, "back_to_menu")],
     check_tabletennis),
    ("pose mii", "posemii", "bom", "ESceneID_RPBomScene", 8750,
     [(4150, "start"), (4650, "matching"), (6150, "level_bonus"), (7900, "results"), (8740, "back_to_menu")],
     check_posemii),
    ("charge", "charge", "cow", "ESceneID_RPCowScene", 10300,
     [(4400, "start"), (5150, "knocked"), (5650, "hurdles"), (9150, "time_bonus"), (9650, "results"),
      (10290, "back_to_menu")],
     check_charge),
]


def net_drawn(path):
    """Share of dark pixels (the net's mesh) in Table Tennis's net band, the table being light
    blue: about 0.45 in play, under 0.1 where the "Hold your paddle." window covers it (the
    layout's window, N_msgWidow_00 at y +20, sits over the net during the serve wait)."""
    data = tb.load_rgba(path)
    n = dark = 0
    for y in range(206, 226):
        for x in range(120, 520):
            i = (y * tb.W + x) * 4
            n += 1
            dark += 0.3 * data[i] + 0.59 * data[i + 1] + 0.11 * data[i + 2] < 90
    return dark / n


def screens_tabletennis(paths, errs):
    shares = [net_drawn(paths[n]) for n in ("rally", "rally_14")]
    if min(shares) < 0.3:
        errs.append("table tennis: the net's band in play has %s dark pixels (the net is not drawn)" % shares)
    return "the net drawn in play (dark share %s)" % ", ".join("%.2f" % x for x in shares)


# checks of the saved frames, by script name
SCREEN_CHECKS = {"tabletennis": screens_tabletennis}
