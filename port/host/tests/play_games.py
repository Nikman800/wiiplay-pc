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
BOM_LEVELS = 2  # levels the player must clear (it stops matching at level 3: the misses end the game)


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
TNK_MISSIONS = 3  # missions cleared at least (the player goes on until its lives run out)


def check_tanks(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 8
    kills_bad = [(a, b) for a, b in changes(vals, 2) if b[2] != a[2] + 1 or a[0] != 4]
    # lives: set at the start (3), then one fewer per tank lost (a stage bonus adds one)
    lives_bad = [(a, b) for a, b in changes(vals, 3) if a[3] != 0 and b[3] not in (a[3] - 1, a[3] + 1)]
    stage = max((g[1] for _, g in vals), default=0)
    # mission 1 cleared: its enemy destroyed in play, the clear, then stage 1 loaded
    m1 = any(a[1] == 0 and b[0] == 6 for a, b in changes(vals, 0))
    if not m1 or stage < TNK_MISSIONS:
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
    ("tanks", "tanks", "tnk", "ESceneID_RPTnkScene", 9900,
     [(4150, "start"), (4400, "shot"), (4650, "mission_cleared"), (9100, "results"), (9890, "back_to_menu")],
     check_tanks),
    # the game-side player clears the table in 21 shots (7 fouls) since §42's rounding (10 before)
    ("billiards", "billiards", "bil", "ESceneID_RPBilScene", 17200,
     [(4150, "start"), (4900, "rolling"), (16100, "score"), (16500, "results"), (17190, "back_to_menu")],
     check_billiards),
    ("fishing", "fishing", "fsh", "ESceneID_RPFshScene", 12900,
     [(4150, "start"), (5150, "catch"), (5400, "points"), (11900, "results"), (12890, "back_to_menu")],
     check_fishing),
    ("table tennis", "tabletennis", "pnp", "ESceneID_RPPnpScene", 7400,
     [(4150, "start"), (4650, "rally"), (5150, "rally_14"), (6650, "results"), (7390, "back_to_menu")],
     check_tabletennis),
    ("pose mii", "posemii", "bom", "ESceneID_RPBomScene", 11400,
     [(4150, "start"), (4650, "matching"), (6150, "level_bonus"), (10500, "results"), (11390, "back_to_menu")],
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


# === Two players (port/docs/HOST_BUILD.md §36) ===============================================
# Each game from the two-player party menu (tests/boot/menus_saved2.txt: a second remote, "2"
# on Select Players, Guest A and Guest B), then the game's script and tests/boot/play2.txt
# (remote 2's game-side player plays player 2), or for Shooting Range and Laser Hockey their
# own two-player scripts (the `aim` command on both remotes). The probes append player 2's
# fields (host_probe.cpp, host_probe_games.cpp) when two players play.

def first_last(vals, k):
    return (vals[0][1][k], vals[-1][1][k]) if vals else (0, 0)


def rising(vals, k, states=None):
    """Increases of field k (only in the given states of field 0, when given)."""
    return [(a, b) for a, b in changes(vals, k) if b[k] > a[k] and (states is None or a[0] in states or b[0] in states)]


# --- Shooting Range (duc: state, round, score 1, hits 1, score 2, hits 2) ----------------------
def check_shooting2(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 6
    bad = [(a, b) for k in (2, 4) for a, b in changes(vals, k) if a[0] not in (3, 4) and b[0] not in (3, 4)]
    rounds = sorted(set(g[1] for _, g in vals if g[0] == 3))
    if rounds != [1, 2, 3, 4, 5] or states[-1] != 7:
        errs.append("shooting (2P): rounds played %s, states %s" % (rounds, states))
    if final[2] <= 0 or final[4] <= 0 or final[3] <= 0 or final[5] <= 0:
        errs.append("shooting (2P): scores %d / %d, hits %d / %d" % (final[2], final[4], final[3], final[5]))
    for k, who in ((2, 1), (4, 2)):
        per_round = set(a[1] for a, b in rising(vals, k, (3,)))
        if per_round != {1, 2, 3, 4, 5}:
            errs.append("shooting (2P): player %d scored in rounds %s" % (who, sorted(per_round)))
    if bad:
        errs.append("shooting (2P): a score changed outside play: %s" % bad[:2])
    return "states %s; scores %d (%d hits) / %d (%d hits), both scoring in every round" % (
        " -> ".join(str(x) for x in states), final[2], final[3], final[4], final[5])


# --- Laser Hockey (hky: state, timer, goals 1, goals 2, puck x z, dist 1, paddle 2 x z, dist 2) --
def check_hockey2(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 10
    goals = [(a, b) for k in (2, 3) for a, b in changes(vals, k)]
    goal_bad = [(a, b) for a, b in goals if b[2] + b[3] != a[2] + a[3] + 1 or b[0] != 2]
    contacts = [sum(1 for (_, a), (_, b) in zip(vals, vals[1:]) if b[0] == 1 and b[k] <= 40 < a[k]) for k in (6, 9)]
    clock = set(g[1] for _, g in vals if g[0] == 1)
    for want in (0, 1, 2, 3, 4, 5):
        if want not in states:
            errs.append("hockey (2P): states %s (no %d)" % (states, want))
            break
    if max(final[2], final[3]) != 8 or min(final[2], final[3]) < 1:
        errs.append("hockey (2P): final %d-%d (first to 8, both scoring)" % (final[2], final[3]))
    if goal_bad:
        errs.append("hockey (2P): a score changed outside a goal or by more than one: %s" % goal_bad[:2])
    if min(contacts) < 3:
        errs.append("hockey (2P): paddle-puck contacts %s" % contacts)
    if not clock <= {7200, -1}:
        errs.append("hockey (2P): the clock ran (%s): no time limit with two players" % sorted(clock)[:3])
    return "states %s; goals %d-%d; paddle-puck contacts %d / %d; no clock" % (
        " -> ".join(str(x) for x in states), final[2], final[3], contacts[0], contacts[1])


# --- Find Mii (wly: ..., score 1, score 2, penalty 2) -----------------------------------------
def check_findmii2(vals, errs):
    states = seq_of(vals)
    clears = sum(1 for a, b in changes(vals, 0) if b[0] == 3)
    ups = [len(rising(vals, k, (2, 3))) for k in (7, 8)]
    best = [max(g[k] for _, g in vals) for k in (7, 8)] if vals else [0, 0]
    pen2 = sum(1 for a, b in changes(vals, 9) if b[9] > a[9])
    start = [g[2] for _, g in vals if g[0] == 2][:1]
    if states[:3] != [0, 1, 2] or states[-1] != 4 or start != [7200]:
        errs.append("find mii (2P): states %s, clock from %s" % (states[:6], start))
    if clears < WLY_STAGES or min(ups) < 3:
        errs.append("find mii (2P): %d stages cleared, finds credited %s" % (clears, ups))
    if pen2 < 1:
        errs.append("find mii (2P): no wrong pick by player 2")
    return "states %s ... %d; %d stages cleared; finds credited to player 1 / 2: %d / %d (best scores %d / %d); %d " \
           "wrong picks by player 2; time up" % (" -> ".join(str(x) for x in states[:5]), states[-1], clears, ups[0],
                                                 ups[1], best[0], best[1], pen2)


# --- Pose Mii (bom: ..., score 2, hits 2) -----------------------------------------------------
def check_posemii2(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 13
    level = max((g[1] for _, g in vals), default=0)
    play = [g for _, g in vals if g[0] == 3]
    border_bad = [g for g in play if (g[8] - g[9], g[9] - g[10]) != (160, 160)]
    for want in (0, 1, 2, 3, 6, 7, 9, 11, 12):
        if want not in states:
            errs.append("pose mii (2P): states %s (no %d)" % (states, want))
            break
    if level < BOM_LEVELS or final[3] < 30 or final[12] < 30:
        errs.append("pose mii (2P): level %d, bubbles matched %d / %d" % (level, final[3], final[12]))
    if final[5] != final[6]:
        errs.append("pose mii (2P): %d of %d misses at the end" % (final[5], final[6]))
    if not play or border_bad:
        errs.append("pose mii (2P): the borders' z in play %s" % (border_bad[:1],))
    return "states %s ...; levels %d; matched %d / %d, scores %d / %d; %d/%d misses" % (
        " -> ".join(str(x) for x in states[:10]), level + 1, final[3], final[12], final[2], final[11], final[5],
        final[6])


# --- Table Tennis (pnp: ..., points 1, points 2, racket 2 x z) --------------------------------
def check_tabletennis2(vals, errs):
    states = seq_of(vals)
    states = states[states.index(0):] if 0 in states else states
    final = vals[-1][1] if vals else (0,) * 17
    pts = [(a, b) for k in (13, 14) for a, b in changes(vals, k)]
    pts_bad = [(a, b) for a, b in pts if b[13] + b[14] != a[13] + a[14] + 1]
    returns = [sum(1 for a, b in changes(vals, 9) if b[9] == side) for side in (0, 1)]
    if states[:4] != [0, 1, 2, 3] or states[-1] != 5:
        errs.append("table tennis (2P): states %s" % states[:8])
    if max(final[13], final[14]) != 11 or min(final[13], final[14]) < 1:
        errs.append("table tennis (2P): final points %d-%d (to 11, both scoring)" % (final[13], final[14]))
    if pts_bad:
        errs.append("table tennis (2P): points changed by more than one: %s" % pts_bad[:2])
    if min(returns) < 10:
        errs.append("table tennis (2P): strokes by player 1 / 2: %s" % returns)
    return "states %s; points %d-%d; strokes by player 1 / 2: %d / %d" % (
        " -> ".join(str(x) for x in states[:6]) + " ...", final[13], final[14], returns[0], returns[1])


# --- Billiards (bil: ..., turn, player 2 state, shots 2, fouls 2, score 1, score 2) -------------
def check_billiards2(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 14
    turns = len(changes(vals, 8))
    off_bad = [(a, b) for a, b in changes(vals, 4) if b[4] < a[4] or (a[1] not in (4, 6, 7) and a[9] not in (4, 6, 7))]
    if states[:3] != [0, 1, 2] or states[-1] != 5:
        errs.append("billiards (2P): states %s" % states[:8])
    if final[4] != 9 or turns < 2 or final[2] < 1 or final[10] < 1:
        errs.append("billiards (2P): %d balls off, %d turn changes, shots %d / %d" % (final[4], turns, final[2],
                                                                                      final[10]))
    if final[12] <= 0 or final[13] <= 0:
        errs.append("billiards (2P): scores %d / %d" % (final[12], final[13]))
    if off_bad:
        errs.append("billiards (2P): a ball left the table outside a shot: %s" % off_bad[:2])
    return "states %s ...; %d turn changes; shots %d / %d, fouls %d / %d, scores %d / %d; all 9 object balls off: %s" % (
        " -> ".join(str(x) for x in states[:8]), turns, final[2], final[10], final[3], final[11], final[12],
        final[13], final[4] == 9)


# --- Fishing (fsh: ..., player 2 state, caught 2, points 2) -----------------------------------
def check_fishing2(vals, errs):
    final = vals[-1][1] if vals else (0,) * 11
    caught = [max(g[k] for _, g in vals) if vals else 0 for k in (4, 9)]
    p2states = set(g[8] for _, g in vals)
    timer = max((g[2] for _, g in vals), default=0)
    if min(caught) < 2 or final[5] <= 0 or final[10] <= 0:
        errs.append("fishing (2P): caught %s, points %d / %d" % (caught, final[5], final[10]))
    if not {1, 2, 3, 4} <= p2states:
        errs.append("fishing (2P): player 2 states %s" % sorted(p2states))
    if timer < final[3] - 1 or final[7] < 5:
        errs.append("fishing (2P): timer %d of %d, result state %d" % (timer, final[3], final[7]))
    return "caught %d / %d fish, %d / %d points; the clock ran out; result state %d" % (
        caught[0], caught[1], final[5], final[10], final[7])


# --- Charge! (cow: ..., score 2, time 2, z 2, winner) -----------------------------------------
def check_charge2(vals, errs):
    states = seq_of(vals)
    play = [g for _, g in vals if g[0] in (7, 8)]
    final = vals[-1][1] if vals else (0,) * 12
    runs = [(play[0][k] - min(g[k] for g in play)) if play else 0 for k in (5, 10)]
    for want in (1, 4, 5, 6, 7, 8, 19, 20, 23, 24):
        if want not in states:
            errs.append("charge (2P): states %s (no %d)" % (states, want))
            break
    if final[1] <= 0 or final[8] <= 0 or min(runs) < 3000:
        errs.append("charge (2P): scores %d / %d, ran %s" % (final[1], final[8], runs))
    return "states %s; scores %d / %d; ran %d / %d along the course; winner player %d" % (
        " -> ".join(str(x) for x in states), final[1], final[8], runs[0], runs[1], final[11] + 1)


# --- Tanks! (tnk: ..., kills 2, lives 2, alive 2) ---------------------------------------------
def check_tanks2(vals, errs):
    states = seq_of(vals)
    final = vals[-1][1] if vals else (0,) * 11
    stage = max((g[1] for _, g in vals), default=0)
    kills_bad = [(a, b) for k in (2, 8) for a, b in changes(vals, k) if b[k] != a[k] + 1 or a[0] != 4]
    lives_bad = [(a, b) for a, b in changes(vals, 3) if b[3] != b[9]]
    if stage < 2 or 9 not in states or states[-1] != 12:
        errs.append("tanks (2P): states %s, missions cleared %d" % (states, stage))
    if final[2] < 1 or final[8] < 1:
        errs.append("tanks (2P): kills %d / %d" % (final[2], final[8]))
    if kills_bad or lives_bad:
        errs.append("tanks (2P): kills %s / shared lives %s" % (kills_bad[:2], lives_bad[:2]))
    return "states %s ...; missions cleared %d; kills %d / %d" % (" -> ".join(str(x) for x in states[:12]), stage,
                                                                 final[2], final[8])


# name, script parts, probe tag, scene, frames, saved screens, check, runs (2: determinism)
GAMES2 = [
    ("shooting range (2P)", ["menus_saved2.txt", "shooting2.txt"], "duc", "ESceneID_RPDucScene", 14800,
     [(4300, "start"), (5000, "both"), (13700, "finished"), (14100, "results"), (14790, "back_to_menu")],
     check_shooting2, 2),
    ("laser hockey (2P)", ["menus_saved2.txt", "hockey2.txt"], "hky", "ESceneID_RPHkyScene", 9300,
     [(4300, "start"), (4600, "rally"), (5000, "goal"), (8700, "results"), (9290, "back_to_menu")],
     check_hockey2, 2),
    ("find mii (2P)", ["menus_saved2.txt", "findmii.txt", "play2.txt"], "wly", "ESceneID_RPWlyScene", 14400,
     [(4100, "start"), (4500, "found"), (8000, "wrong_pick"), (13500, "results"), (14390, "back_to_menu")],
     check_findmii2, 2),
    ("pose mii (2P)", ["menus_saved2.txt", "posemii.txt", "play2.txt"], "bom", "ESceneID_RPBomScene", 11300,
     [(4300, "start"), (5000, "matching"), (8000, "level_2"), (10700, "results"), (11290, "back_to_menu")],
     check_posemii2, 2),
    ("table tennis (2P)", ["menus_saved2.txt", "tabletennis.txt", "play2.txt"], "pnp", "ESceneID_RPPnpScene", 12700,
     [(4300, "start"), (5000, "rally"), (8000, "swapped"), (12000, "results"), (12690, "back_to_menu")],
     check_tabletennis2, 2),
    # the game ends at frame 11622 and the menu is back at 12121 since §42's rounding (before 12100)
    ("billiards (2P)", ["menus_saved2.txt", "billiards.txt", "play2.txt"], "bil", "ESceneID_RPBilScene", 12400,
     [(4300, "start"), (5000, "rolling"), (8000, "player_2"), (11900, "results"), (12390, "back_to_menu")],
     check_billiards2, 2),
    ("fishing (2P)", ["menus_saved2.txt", "fishing.txt", "play2.txt"], "fsh", "ESceneID_RPFshScene", 13500,
     [(4300, "start"), (5000, "fishing"), (8000, "catch"), (12500, "results"), (13490, "back_to_menu")],
     check_fishing2, 2),
    ("charge (2P)", ["menus_saved2.txt", "charge.txt", "play2.txt"], "cow", "ESceneID_RPCowScene", 9300,
     [(4300, "start"), (5000, "race"), (6500, "knocked"), (8400, "results"), (9290, "back_to_menu")],
     check_charge2, 2),
    ("tanks (2P)", ["menus_saved2.txt", "tanks.txt", "play2.txt"], "tnk", "ESceneID_RPTnkScene", 8600,
     [(4150, "start"), (4400, "shot"), (4650, "mission_cleared"), (7700, "results"), (8590, "back_to_menu")],
     check_tanks2, 2),
]


# --- later levels, one player (the players' `goal`; one run each) -------------------------------
def check_findmii_long(vals, errs):
    out = check_findmii(vals, errs)
    stage = max((g[1] for _, g in vals), default=0)
    if stage < 13:
        errs.append("find mii (long): stage %d reached" % stage)
    return out


def check_posemii_long(vals, errs):
    out = check_posemii(vals, errs)
    level = max((g[1] for _, g in vals), default=0)
    if level < 4:
        errs.append("pose mii (long): level %d reached" % (level + 1))
    return out


def check_tabletennis_long(vals, errs):
    out = check_tabletennis(vals, errs)
    final = vals[-1][1] if vals else (0,) * 13
    if final[3] != 100:
        errs.append("table tennis (long): %d rallies (the game ends at 100)" % final[3])
    return out


# name, script parts, extra script lines, probe tag, scene, frames, saved screens, check, runs
LONG = [
    ("find mii (12 stages)", ["menus_saved.txt", "findmii.txt"], ["3850 goal 12"], "wly", "ESceneID_RPWlyScene",
     14900, [(4150, "start"), (9000, "stage_11"), (11500, "wrong_pick"), (14000, "results"), (14890, "back_to_menu")],
     check_findmii_long, 1),
    ("pose mii (4 levels)", ["menus_saved.txt", "posemii.txt"], ["3850 goal 4"], "bom", "ESceneID_RPBomScene", 15700,
     [(4150, "start"), (9000, "level_3"), (12000, "level_4"), (14800, "results"), (15690, "back_to_menu")],
     check_posemii_long, 1),
    ("table tennis (100 rallies)", ["menus_saved.txt", "tabletennis.txt"], ["3850 goal 100"], "pnp",
     "ESceneID_RPPnpScene", 11200,
     [(4150, "start"), (7000, "rally_50"), (9700, "rally_99"), (10300, "results"), (11190, "back_to_menu")],
     check_tabletennis_long, 1),
]
