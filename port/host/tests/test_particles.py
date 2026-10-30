#!/usr/bin/env python3
"""Check `ptcl_harness` (the real nw4r::ef runtime on the host) against independent models
where practical, and against itself otherwise (port/docs/HOST_BUILD.md §19).

The harness creates every emitter of every BREFF on the disc as an effect through
EffectSystem::CreateEffect (the RPSysEffectCreator set-up), steps it 60 frames with
EffectSystem::Calc and prints per-frame counts (F lines) and, at selected frames, every live
particle (P lines). This test reads the same BREFF files from the disc (read-only) with
port/assets/tools/fmt_effect.py plus its own EmitterDesc decode, and checks:

Modelled (independent of the C++):
  * coverage: one E line per emitter of every registered BREFF, the name resolved to the
    file's own EmitterResource (or to an earlier registered project with the same name), the
    group ID computed from the userdata flags as RPSysEffectCreator::createEffect does, and
    the memory pools empty after teardown (X lines);
  * emission timing and counts: for emitters whose emission is deterministic (no emission or
    interval randomness, no LOD, no emit-past, no equal-division flag, no emission curve), the
    emitter's tick and the particles it creates each frame, simulated from emitEmitStart,
    emitLife, emitEmitInterval and emitEmit with f32 accumulation (Emitter::CalcEmitter /
    CalcEmission / Emission);
  * lifetime: every particle's life from ptclLife / ptclLifeRandom (EmitterForm::CalcLife's
    range), tick in [1, life], tick advancing by one per frame with life and seed unchanged,
    and a particle retired exactly when its tick reaches its life (ParticleManager::Calc);
  * motion and fields: for particles of effects with one emitter, no emitter rotation or
    scale, and only gravity / speed fields (no TIMING flag) among the motion tracks, the
    step from each selected frame to the next: the gravity power from the field's curve (the
    f32 curve model of test_efcurves.py) times the decoded direction into velocity or
    position, the speed field's (speed - 1) * velocity, then velocity and position integrated
    with the particle's momentum, all in f32. Must match bit for bit;
  * seed independence: an emitter whose randomSeed is non-zero does not read the system
    random, so with a different system seed its effect (if it never spawns other emitters)
    must print exactly the same lines (particle object IDs aside: the pool slots depend on
    what earlier effects left behind); at least one other effect must change.

Regression only (no model here): initial positions and velocities (the emit forms), sizes,
colours, rotations and texture curves (their curves are modelled in test_efcurves.py, not
their application), random / spin / magnet fields, child emitters and particles.
Determinism: two runs with the same seed must be identical; with --compare, the other
build's harness (64- vs 32-bit) must print exactly the same output.

    test_particles.py --harness build/host64/ptcl_harness --disc ~/wii-play/extracted/files \\
        [--compare build/host32/ptcl_harness]
"""
import argparse
import collections
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
sys.path.insert(0, HERE)
import fmt_effect  # noqa: E402  (read-only reference)
from wiiarc import walk  # noqa: E402
from test_efcurves import Curve, exec_f32, f32, add, sub, mul  # noqa: E402  (f32 curve model)

OTHER_SEED = "0x9876"
FRAMES = 60
PAIRS = [(1, 2), (2, 3), (3, 4), (4, 5), (10, 11), (20, 21), (40, 41)]

# EmitterDesc flags (include/nw4r/ef/ef_res_emitter.h)
CMN_MAX_LIFE = 1 << 2
CMN_DISABLE_CALC = 1 << 9
EMIT_LOD = 1 << 8
EMIT_DIV = 1 << 17
# AnimCurveHeader
PROC_STOP = 1 << 3
PROC_TIMING = 1 << 4
AC_F32 = 3
KIND_GRAVITY, KIND_SPEED = 32, 33
MOTION_KINDS = range(32, 40)
EMIT_SCALE, EMIT_ROTATE, EMIT_EMISSION = 65, 66, 73
# RPSysEffectDefine::EUserFlag
USER_0, USER_15 = 1 << 0, 1 << 15


def desc_of(data, off):
    """EmitterDesc fields this test needs, from the file bytes."""
    d = {}
    (d["commonFlag"], d["emitFlag"], d["emitLife"], d["ptclLife"], d["ptclLifeRandom"], _,
     d["intervalRandom"], d["emitRandom"], d["emitEmit"], d["emitStart"], d["emitPast"],
     d["emitInterval"]) = struct.unpack_from(">IIHHbbbbfHHH", data, off)
    d["scale"] = struct.unpack_from(">3f", data, off + 0x60)
    d["rotate"] = struct.unpack_from(">3f", data, off + 0x6C)
    d["randomSeed"] = struct.unpack_from(">I", data, off + 0x88)[0]
    d["userdata"] = data[off + 0x8C:off + 0x94]
    return d


def group_id(scene, desc):
    u = desc["userdata"]
    flags = u[2] << 8 | u[3]
    g = scene << 7
    if flags & USER_15:
        g |= 0x30 if flags & USER_0 else 0x20
    elif not flags & USER_0:
        g |= 0x10
    return g


def run(harness, disc, *extra):
    r = subprocess.run([harness, disc, *extra], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True)
    return r.returncode, r.stdout, r.stderr


def parse_output(text):
    """[effect], effect = {path, index, name, res, group, F: {frame: ints}, P: {frame: [..]},
    X: ints, lines: [str]}"""
    effects = []
    cur = None
    for line in text.splitlines():
        f = line.split()
        if f[0] == "E":
            cur = {"path": f[1], "index": int(f[2]), "name": f[3], "lines": [line]}
            if f[4] == "FAIL":
                cur["fail"] = True
            else:
                cur["res"], cur["group"] = f[4], int(f[5], 16)
            cur["F"], cur["P"] = {}, collections.defaultdict(list)
            effects.append(cur)
            continue
        cur["lines"].append(line)
        if f[0] == "F":
            cur["F"][int(f[1])] = [int(x) for x in f[2:]]
        elif f[0] == "P":
            cur["P"][int(f[1])].append(f)
        elif f[0] == "X":
            cur["X"] = [int(x) for x in f[1:]]
    return effects


def normalized(lines):
    """Effect lines with particle object IDs replaced by their order of appearance (which pool
    slot a particle gets depends on what earlier effects left in the free list)."""
    ids, out = {}, []
    for line in lines:
        f = line.split()
        if f[0] == "P":
            f[2] = str(ids.setdefault(f[2], len(ids)))
        out.append(f)
    return out


def fl(h):
    return struct.unpack("<f", struct.pack("<I", int(h, 16)))[0]


def model_emission(desc, frames):
    """Per frame (emitter tick or None once retired, particles emitted)."""
    wait, tick, iw, cnt, first, alive = desc["emitStart"], 0, 0, 0.0, True, True
    ratio = f32(desc["emitEmit"])
    out = {}
    for frame in range(1, frames + 1):
        if alive and wait == 0:
            if not desc["commonFlag"] & CMN_MAX_LIFE and tick >= desc["emitLife"]:
                alive = False
        new = 0
        if alive:
            if wait > 0:
                wait -= 1
            else:
                if iw > 0:
                    iw -= 1
                else:
                    iw = desc["emitInterval"]
                    cnt = add(cnt, ratio)
                    if first and ratio != 0 and cnt < 1.0:
                        cnt = 1.0
                    if cnt >= 1.0:
                        new = int(cnt)
                        cnt = sub(cnt, float(new))
                    first = False
                tick += 1
        out[frame] = (tick if alive else None, new)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--compare", help="the other build's ptcl_harness (skipped if missing)")
    args = ap.parse_args()

    rc, out, err = run(args.harness, args.disc)
    sys.stderr.write(err)
    if rc != 0:
        print("ptcl_harness failed (exit %d)" % rc)
        return 1
    errors = 0

    def fail(msg):
        nonlocal errors
        errors += 1
        if errors <= 40:
            print("ERROR: " + msg)

    # Determinism.
    rc2, out2, _ = run(args.harness, args.disc)
    if rc2 != 0 or out2 != out:
        fail("two runs with the same seed differ")
    compared = "not run"
    if args.compare:
        if os.path.exists(args.compare):
            rc3, out3, err3 = run(args.compare, args.disc)
            sys.stderr.write(err3)
            if rc3 != 0 or out3 != out:
                fail("%s prints different output" % args.compare)
            compared = "identical"
        else:
            compared = "skipped (%s not built)" % args.compare
    rc4, out4, _ = run(args.harness, args.disc, OTHER_SEED)
    if rc4 != 0:
        fail("ptcl_harness with seed %s failed" % OTHER_SEED)

    effects = parse_output(out)
    other = {(e["path"], e["index"]): e for e in parse_output(out4)}

    files = {p: d for p, d in walk(os.path.join(args.disc, "Common"))}
    files = {"Common/" + p: d for p, d in files.items() if d[:4] == b"REFF"}
    parsed = {p: fmt_effect.parse(d) for p, d in files.items()}

    def resource(where):
        path, off = where.rsplit("+", 1)
        off = int(off, 16)
        for em in parsed[path]["emitters"]:
            if em["offset"] == off:
                return path, em
        return path, None

    descs = {}

    def desc(path, em):
        key = (path, em["offset"])
        if key not in descs:
            descs[key] = desc_of(files[path], em["emitterDescOffset"])
        return descs[key]

    # --- coverage --------------------------------------------------------------------------
    created = collections.Counter((e["path"], e["index"]) for e in effects)
    for path in sorted(set(e["path"] for e in effects)):
        n = len(parsed[path]["emitters"])
        for i in range(n):
            if created[(path, i)] != 1:
                fail("%s emitter %d created %d times" % (path, i, created[(path, i)]))
    missing = sorted(set(parsed) - set(e["path"] for e in effects))
    for path in missing:
        fail("%s: no emitter created" % path)
    elsewhere = 0
    for e in effects:
        if e.get("fail"):
            fail("%s %s: CreateEffect failed" % (e["path"], e["name"]))
            continue
        rpath, em = resource(e["res"])
        if em is None:
            fail("%s %s: resource %s is not an EmitterResource" % (e["path"], e["name"], e["res"]))
            continue
        if rpath == e["path"]:
            if parsed[rpath]["emitters"][e["index"]]["offset"] != em["offset"]:
                fail("%s %d: name resolved to another emitter of the same file" % (
                    e["path"], e["index"]))
        else:
            elsewhere += 1
        scene = 1 if e["path"].endswith("nw4r_defcursor_pack.breff") else \
            2 if e["path"] == "Common/PartyStatic/common.carc/basic_common.breff" else 0
        if e["group"] != group_id(scene, desc(rpath, em)):
            fail("%s %s: group %x, expected %x" % (e["path"], e["name"], e["group"],
                                                   group_id(scene, desc(rpath, em))))
        if e.get("X") != [0, 0, 0, 0]:
            fail("%s %s: pools after teardown %s" % (e["path"], e["name"], e.get("X")))
        e["rpath"], e["em"] = rpath, em

    # --- emission timing and counts -----------------------------------------------------------
    emission_checked = emitted = 0
    for e in effects:
        if "em" not in e:
            continue
        em, d = e["em"], desc(e["rpath"], e["em"])
        emit_tracks = em["tracks"][em["particleTrackCount"]:]
        if (d["emitRandom"] or d["intervalRandom"] or d["emitPast"] or d["emitFlag"] & EMIT_LOD
                or d["emitFlag"] & EMIT_DIV or d["commonFlag"] & CMN_DISABLE_CALC
                or any(t["kindType"] == EMIT_EMISSION for t in emit_tracks)):
            continue
        model = model_emission(d, FRAMES)
        emission_checked += 1
        for frame in range(1, FRAMES + 1):
            num_emitter, tick, _live, new = e["F"][frame][:4]
            want_tick, want_new = model[frame]
            if want_tick is not None and tick != want_tick:
                fail("%s %s frame %d: emitter tick %d, model %d" % (
                    e["path"], e["name"], frame, tick, want_tick))
                break
            if new != want_new:
                fail("%s %s frame %d: %d new particles, model %d" % (
                    e["path"], e["name"], frame, new, want_new))
                break
            emitted += new

    # --- lifetime -------------------------------------------------------------------------------
    particles = life_checked = steps = 0
    for e in effects:
        if "em" not in e:
            continue
        for frame, plist in e["P"].items():
            for p in plist:
                particles += 1
                tick, life = int(p[4]), int(p[5])
                rpath, em = resource(p[3])
                d = desc(rpath, em)
                base = max(1, d["ptclLife"])
                lo = base
                if d["ptclLifeRandom"] > 0:
                    lo = max(1, base - int(base * d["ptclLifeRandom"] / 100.0))
                if d["ptclLifeRandom"] >= 0:
                    life_checked += 1
                    if not lo <= life <= base:
                        fail("%s %s frame %d particle %s: life %d outside [%d, %d]" % (
                            e["path"], e["name"], frame, p[2], life, lo, base))
                if not 1 <= tick <= life:
                    fail("%s %s frame %d particle %s: tick %d, life %d" % (
                        e["path"], e["name"], frame, p[2], tick, life))
        for a, b in PAIRS:
            after = {p[2]: p for p in e["P"].get(b, [])}
            before = {p[2] for p in e["P"].get(a, [])}
            for p in e["P"].get(a, []):
                tick, life = int(p[4]), int(p[5])
                q = after.get(p[2])
                if tick >= life:
                    if q is not None:
                        fail("%s %s: particle %s alive at frame %d past its life" % (
                            e["path"], e["name"], p[2], b))
                    continue
                if q is None:
                    fail("%s %s: particle %s (tick %d, life %d) gone at frame %d" % (
                        e["path"], e["name"], p[2], tick, life, b))
                    continue
                steps += 1
                if (int(q[4]), q[5], q[6], q[3]) != (tick + 1, p[5], p[6], p[3]):
                    fail("%s %s: particle %s frame %d -> %d: tick/life/seed %s -> %s" % (
                        e["path"], e["name"], p[2], a, b, p[4:7], q[4:7]))
            for q in e["P"].get(b, []):
                if q[2] not in before and int(q[4]) != 1:
                    fail("%s %s: particle %s appears at frame %d with tick %s" % (
                        e["path"], e["name"], q[2], b, q[4]))
            new = sum(1 for q in e["P"].get(b, []) if q[2] not in before)
            if new != e["F"][b][5]:
                fail("%s %s frame %d: %d new P lines, F says %d" % (
                    e["path"], e["name"], b, new, e["F"][b][5]))

    # --- motion and fields ----------------------------------------------------------------------
    moved = gravity = speed = 0
    kinds_seen = collections.Counter()
    for e in effects:
        if "em" not in e or any(v[0] > 1 for v in e["F"].values()):
            continue
        em, d = e["em"], desc(e["rpath"], e["em"])
        if d["rotate"] != (0.0, 0.0, 0.0) or d["scale"] != (1.0, 1.0, 1.0):
            continue
        ptracks = em["tracks"][:em["particleTrackCount"]]
        etracks = em["tracks"][em["particleTrackCount"]:]
        if any(t["kindType"] in (EMIT_SCALE, EMIT_ROTATE) for t in etracks):
            continue
        fields = []
        ok = True
        for t in ptracks:
            if t["magic"] != fmt_effect.ANIM_CURVE_MAGIC or t["processFlag"] & PROC_STOP:
                continue
            if t["curveFlag"] != AC_F32 or t["kindType"] not in MOTION_KINDS:
                continue
            if t["kindType"] not in (KIND_GRAVITY, KIND_SPEED) or t["processFlag"] & PROC_TIMING:
                ok = False
                break
            fields.append((t, Curve(files[e["rpath"]], t["offset"])))
        if not ok:
            continue
        for a, b in PAIRS:
            after = {p[2]: p for p in e["P"].get(b, [])}
            for p in e["P"].get(a, []):
                q = after.get(p[2])
                if q is None or p[3] != e["res"]:
                    continue
                tick, life, seed = int(p[4]), int(p[5]), int(p[6])
                pos = [fl(x) for x in p[7:10]]
                vel = [fl(x) for x in p[10:13]]
                mom = fl(p[13])
                add_vel, add_pos = [0.0] * 3, [0.0] * 3
                skip = False
                for t, c in fields:
                    kinds_seen[t["kindType"]] += 1
                    if t["kindType"] == KIND_GRAVITY:
                        prm = t["fieldParam"]["values"]
                        power = exec_f32(c, tick, seed, life)[0]
                        if power is None:
                            power = f32(prm["power"])
                        v = [mul(f32(prm["dir." + k]), power) for k in "xyz"]
                        if prm["type"] == 0:
                            add_vel = [add(x, y) for x, y in zip(add_vel, v)]
                        elif prm["type"] == 1:
                            add_pos = [add(x, y) for x, y in zip(add_pos, v)]
                        gravity += 1
                    else:
                        s = exec_f32(c, tick, seed, life)[0]
                        if s is None:
                            skip = True
                            break
                        v = [mul(x, sub(s, 1.0)) for x in vel]
                        add_vel = [add(x, y) for x, y in zip(add_vel, v)]
                        speed += 1
                if skip:
                    continue
                nvel = [add(x, y) for x, y in zip(vel, add_vel)]
                npos = [add(x, mul(y, mom)) for x, y in zip(pos, add_pos)]
                npos = [add(x, mul(y, mom)) for x, y in zip(npos, nvel)]
                have_pos = [fl(x) for x in q[7:10]]
                have_vel = [fl(x) for x in q[10:13]]
                moved += 1
                if have_pos != npos or have_vel != nvel:
                    fail("%s %s particle %s frame %d -> %d: pos %s vel %s, model pos %s vel %s"
                         % (e["path"], e["name"], p[2], a, b, have_pos, have_vel, npos, nvel))

    # --- seed independence ----------------------------------------------------------------------
    same_seed = changed = 0
    for e in effects:
        o = other.get((e["path"], e["index"]))
        if o is None or "em" not in e:
            fail("%s %d missing from the seed %s run" % (e["path"], e["index"], OTHER_SEED))
            continue
        single = all(v[0] <= 1 for v in e["F"].values())
        if desc(e["rpath"], e["em"])["randomSeed"] != 0 and single:
            same_seed += 1
            if normalized(o["lines"]) != normalized(e["lines"]):
                fail("%s %s: randomSeed is set, but the system seed changed the output" % (
                    e["path"], e["name"]))
        elif o["lines"] != e["lines"]:
            changed += 1
    if changed == 0:
        fail("no effect depends on the system seed")

    print("test_particles: %d effects from %d BREFFs (%d names resolved to another project), "
          "%d particle lines" % (len(effects), len(parsed), elsewhere, particles))
    print("  modelled: emission of %d emitters (%d particles over %d frames); life of %d "
          "particle lines; %d one-frame steps (tick/life/seed); motion of %d particle steps "
          "(%d gravity, %d speed field evaluations); %d effects seed-independent, %d seed-"
          "dependent" % (emission_checked, emitted, FRAMES, life_checked, steps, moved, gravity,
                         speed, same_seed, changed))
    print("  determinism: same seed twice identical; other build: %s" % compared)
    if errors:
        print("test_particles: %d errors" % errors)
        return 1
    print("test_particles: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
