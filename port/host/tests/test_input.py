#!/usr/bin/env python3
"""Input platform layer test (docs/HOST_BUILD.md §23).

Runs input_harness over each script in tests/input/ in both modes (the game's
controller code, and KPAD alone) and checks its per-frame KPAD status against
a model computed here from the script:

  pointer   KPAD's pos (and the game's cursor) against the window position the
            script gives, through the sensor-bar dots and the real KPAD math;
            dpd_valid_fg against which dots the camera sees; horizon and
            distance against the roll and distance
  tilt      acc against the counts the mapping must produce (exactly) and
            against the gravity model (within a count); acc_vertical after it
            settles
  buttons   hold / trig / release (and the repeat flag) from a model of KPAD's
            read_kpad_button / calc_button_repeat and EGG's stick buttons
  nunchuk   stick and acc; Z / C; the extension's device type
  rumble    the motor changes after a connection, from a model of
            EGG::ControllerRumbleUnit and the pattern RPSysCoreController uses
  events    connect / disconnect calls into the sound and player managers
  WPAD      the harness's self-checks (info, speaker, gravity units, probe)
  64 / 32   --compare: the other build prints the same, line for line
"""

import argparse
import math
import os
import struct
import subprocess
import sys

POS_TOL = 0.0045       # KPAD pos units (2 = the screen width): 1.4 pixels at 608
PLAY_RADIUS = 0.06     # RPSysCoreController's pos play radius
KPAD_FRAME_X = 0.95    # KPAD's error frame (kp_err_outside_frame 0.05)
KPAD_FRAME_Y = 0.75 - 0.05
FRAME_MARGIN = 0.008   # about 2 camera pixels either side of KPAD's frame edge
SETTLE = 40            # frames after a roll / pitch / distance / connection change
CORE_G = 636 - 530
FS_G = 716 - 512
STICK_MIN, STICK_MAX = 15, 71
VV1 = None             # set from f32(0.2 / 0.383864)

errors = []
stats = {}
counts = {}


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def lround(x):
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


def err(msg):
    errors.append(msg)


def count(name):
    counts[name] = counts.get(name, 0) + 1


def stat(name, value):
    stats[name] = max(stats.get(name, 0.0), value)


BUTTONS = {'A': 1 << 11, 'B': 1 << 10, '1': 1 << 9, '2': 1 << 8, 'PLUS': 1 << 4,
           'MINUS': 1 << 12, 'HOME': 1 << 15, 'UP': 1 << 3, 'DOWN': 1 << 2,
           'LEFT': 1 << 0, 'RIGHT': 1 << 1, 'Z': 1 << 13, 'C': 1 << 14}
ZC = (1 << 13) | (1 << 14)


class State:
    def __init__(self):
        self.connected = False
        self.nunchuk = False
        self.battery = 4
        self.buttons = 0
        self.pointer = None    # (x, y) in KPAD units, None: pointing away
        self.mouse = None      # window pixels, when set by `mouse`
        self.dist = 2.0
        self.roll = 0.0
        self.pitch = 0.0
        self.accel = (0.0, 0.0, 0.0)
        self.stick = (0.0, 0.0)
        self.fsroll = 0.0
        self.fspitch = 0.0
        self.fsaccel = (0.0, 0.0, 0.0)

    def copy(self):
        s = State()
        s.__dict__.update(self.__dict__)
        return s


def parse_script(path):
    """Per-frame states of the four channels, plus the window size."""
    window = (608.0, 456.0)
    end = None
    cmds = []
    chan = 0
    last = 0
    with open(path) as f:
        for line in f:
            line = line.split('#', 1)[0].split()
            if not line:
                continue
            if line[0] == 'window':
                window = (float(line[1]), float(line[2]))
                continue
            if line[0] == 'end':
                end = int(line[1])
                continue
            frame = int(float(line[0]))
            last = frame
            if line[1] == 'chan':
                chan = int(float(line[2]))
            cmds.append((frame, chan, line[1], line[2:]))
    if end is None:
        end = last + 1
    states = [State() for _ in range(4)]
    out = []
    i = 0
    for frame in range(end):
        while i < len(cmds) and cmds[i][0] <= frame:
            _, ch, op, args = cmds[i]
            i += 1
            s = states[ch]
            nums = [float(a) for a in args] if op not in ('press', 'release') else []
            if op == 'connect':
                s.connected = True
            elif op == 'disconnect':
                s.connected = False
            elif op == 'nunchuk':
                s.nunchuk = nums[0] != 0
            elif op == 'battery':
                s.battery = int(nums[0])
            elif op == 'mouse':
                s.mouse = (nums[0], nums[1])
                s.pointer = (f32(f32(f32(2.0 * f32(nums[0])) / f32(window[0])) - 1.0),
                             f32(f32(f32(2.0 * f32(nums[1])) / f32(window[1])) - 1.0))
            elif op == 'pointer':
                s.mouse = None
                s.pointer = (nums[0], nums[1])
            elif op == 'offscreen':
                s.pointer = None
            elif op == 'dist':
                s.dist = nums[0]
            elif op == 'press':
                for a in args:
                    s.buttons |= BUTTONS[a]
            elif op == 'release':
                for a in args:
                    s.buttons &= ~BUTTONS[a]
            elif op == 'roll':
                s.roll = nums[0]
            elif op == 'pitch':
                s.pitch = nums[0]
            elif op == 'accel':
                s.accel = tuple(nums)
            elif op == 'stick':
                s.stick = tuple(nums)
            elif op == 'fsroll':
                s.fsroll = nums[0]
            elif op == 'fspitch':
                s.fspitch = nums[0]
            elif op == 'fsaccel':
                s.fsaccel = tuple(nums)
        out.append([st.copy() for st in states])
    return window, out


# --- the model -------------------------------------------------------------

def gravity(roll, pitch, extra):
    r, p = math.radians(roll), math.radians(pitch)
    return (-math.cos(p) * math.sin(r) + extra[0], -math.cos(p) * math.cos(r) + extra[1],
            math.sin(p) + extra[2])


def acc_count(g, zero, one, even):
    c = g * (one - zero)
    n = 2 * lround(c / 2.0) if even else lround(c)
    hi = 1022 if even else 1023
    return max(-zero, min(hi - zero, n))


def kpad_acc(roll, pitch, extra, zero, one, even_yz, clamp):
    """(counts X, Y, Z), KPAD's acc from them (f32), and the unquantized model."""
    a = gravity(roll, pitch, extra)
    cx = acc_count(-a[0], zero, one, False)
    cy = acc_count(a[2], zero, one, even_yz)
    cz = acc_count(-a[1], zero, one, even_yz)
    scale = f32(1.0 / (one - zero))

    def k(c):
        v = f32(f32(float(c)) * scale)
        return max(-clamp, min(clamp, v))
    kp = (k(-cx), k(-cz), k(cy))
    return kp, a


def stick_value(v):
    a = min(abs(v), 1.0)
    n = lround(a * (STICK_MAX - STICK_MIN))
    if n == 0:
        return 0
    return -(n + STICK_MIN) if v < 0 else n + STICK_MIN


def kpad_stick(sx, sy):
    def axis(c):
        t = abs(c)
        if t <= STICK_MIN:
            v = 0.0
        elif t >= STICK_MAX:
            v = 1.0
        else:
            v = f32(float(t - STICK_MIN) / float(STICK_MAX - STICK_MIN))
        return -v if c < 0 else v
    x, y = axis(stick_value(sx)), axis(stick_value(sy))
    ln = f32(x * x + y * y)
    if ln > 1.0:
        ln = math.sqrt(ln)
        x, y = x / ln, y / ln
    return x, y


def dot_model(level, pointer, roll, dist):
    """The two exact dots (KPAD's centre coordinates) and their spacing in pixels."""
    sy = 0.75 - abs(level)
    k = 1.25 / min(1.0, sy)
    r = math.radians(roll)
    c, s = math.cos(r), math.sin(r)
    vx = 0.0 - pointer[0] / k
    vy = -level - pointer[1] / k
    mx, my = c * vx + s * vy, -s * vx + c * vy
    half = VV1 / dist * 0.5
    nx, ny = c, -s
    return (mx - half * nx, my - half * ny), (mx + half * nx, my + half * ny), 2 * half * 512


def dot_class(d):
    ax, ay = abs(d[0]), abs(d[1])
    if ax < KPAD_FRAME_X - FRAME_MARGIN and ay < KPAD_FRAME_Y - FRAME_MARGIN:
        return 'in'
    if ax > KPAD_FRAME_X + FRAME_MARGIN or ay > KPAD_FRAME_Y + FRAME_MARGIN:
        return 'out'
    return 'edge'


# --- the harness output ----------------------------------------------------

def parse_output(text):
    head = None
    ks = {}
    motors = []
    events = []
    selfchecks = []
    for line in text.splitlines():
        if line.startswith('H '):
            head = dict(kv.split('=', 1) for kv in line.split()[1:])
        elif line.startswith('K '):
            d = dict(kv.split('=', 1) for kv in line.split()[1:])
            ks[(int(d['f']), int(d['ch']))] = d
        elif line.startswith('M '):
            d = dict(kv.split('=', 1) for kv in line.split()[1:])
            motors.append((int(d['f']), int(d['ch']), int(d['on'])))
        elif line.startswith('E '):
            parts = line.split()
            events.append((int(parts[1][2:]), ' '.join(parts[2:])))
        elif line.startswith('S '):
            selfchecks.append(line[2:])
    return head, ks, motors, events, selfchecks


def vec(s):
    return tuple(float(v) for v in s.split(','))


def run(harness, script, mode):
    p = subprocess.run([harness, '--script', script, '--mode', mode], capture_output=True,
                       text=True)
    if p.returncode != 0:
        err(f'{os.path.basename(script)} {mode}: exit {p.returncode}: {p.stderr.strip()[-300:]}')
    return p.stdout


# --- checks ----------------------------------------------------------------

def check_run(name, mode, window, frames, out):
    head, ks, motors, events, selfchecks = parse_output(out)
    if head is None:
        err(f'{name} {mode}: no header')
        return 0
    level = float(head['level'])
    if abs(level - (-0.35)) > 1e-6:
        err(f'{name} {mode}: sensor level {level} (the game uses -0.35 with the bar below)')
    game = mode == 'game'
    W, H = window
    checked = 0

    # Repeat parameters: RPSysCoreController::initialize (1 / 1.5 s, 1 / 12 s), or
    # KPAD's defaults (no repeat).
    delay = int(f32(f32(1.0 / 1.5) * 200.0 + 0.5)) if game else 40000
    pulse = int(f32(f32(1.0 / 12.0) * 200.0 + 0.5)) if game else 0

    for chan in range(4):
        btn = {'hold': 0, 'time': 0, 'next': delay}
        stick_hold = 0
        last_change = -1000
        tilt_change = -1000
        fs_change = -1000
        anchored = False
        prev = None
        for f in range(len(frames)):
            st = frames[f][chan]
            if prev is None or st.connected != prev.connected or st.nunchuk != prev.nunchuk:
                last_change = f
                tilt_change = f
                fs_change = f
                anchored = False
            elif (st.roll, st.dist) != (prev.roll, prev.dist):
                last_change = f
                anchored = False
            if prev is not None and (st.roll, st.pitch, st.accel) != (prev.roll, prev.pitch, prev.accel):
                tilt_change = f
            if prev is not None and st.pointer != prev.pointer:
                if st.pointer is not None and prev.pointer is not None and \
                        math.hypot(st.pointer[0] - prev.pointer[0], st.pointer[1] - prev.pointer[1]) > PLAY_RADIUS + 0.01:
                    anchored = True
                elif st.pointer is not None and prev.pointer is None:
                    anchored = True
                else:
                    anchored = False
            prev = st
            k = ks.get((f, chan))
            if not st.connected:
                if k is not None:
                    err(f'{name} {mode} f={f} ch={chan}: printed while disconnected')
                continue
            if k is None:
                err(f'{name} {mode} f={f} ch={chan}: no status')
                continue
            n = int(k['n'])
            expect_n = (200 * 1001 * (f + 1)) // 60000 - (200 * 1001 * f) // 60000
            if n != expect_n:
                err(f'{name} {mode} f={f}: {n} reports, expected {expect_n}')
            reads = int(k['reads'])
            if reads == 0:
                continue
            checked += 1
            settled = f - last_change >= SETTLE
            dpd = int(k['dpd'])
            pos = vec(k['pos'])
            where = f'{name} {mode} f={f} ch={chan}'

            # Device type: the extension a few reports after it changes.
            if f - fs_change >= 2:
                want_dev = 1 if st.nunchuk else 0
                if int(k['dev']) != want_dev:
                    err(f'{where}: dev {k["dev"]}, expected {want_dev}')

            # Pointer.
            pitch_ok = abs(st.pitch) <= 40 and f - tilt_change >= SETTLE and st.accel == (0.0, 0.0, 0.0)
            if st.pointer is None:
                count('pointing away')
                if dpd != 0:
                    err(f'{where}: pointing away but dpd {dpd}')
            else:
                d0, d1, lpx = dot_model(level, st.pointer, st.roll, st.dist)
                c0, c1 = dot_class(d0), dot_class(d1)
                if c0 == 'out' and c1 == 'out':
                    count('both dots out of view')
                if (c0 == 'out') != (c1 == 'out') and 'edge' not in (c0, c1):
                    count('one dot out of view')
                if c0 == 'out' and c1 == 'out' and dpd != 0:
                    err(f'{where}: both dots outside the camera but dpd {dpd}')
                if (c0 == 'out') != (c1 == 'out') and 'edge' not in (c0, c1) and abs(dpd) == 2:
                    err(f'{where}: one dot outside but dpd {dpd}')
                if c0 == 'in' and c1 == 'in':
                    if settled and pitch_ok and dpd != 2:
                        err(f'{where}: both dots in view, settled, but dpd {dpd}')
                    if dpd == 2:
                        e = math.hypot(pos[0] - st.pointer[0], pos[1] - st.pointer[1])
                        if not game or anchored:
                            stat('pos error (exact)', e)
                            count('pos exact')
                            if e > POS_TOL:
                                err(f'{where}: pos {pos} vs {st.pointer}: {e:.5f} > {POS_TOL}')
                        else:
                            stat('pos error (play radius)', e)
                            count('pos within the play radius')
                            if e > PLAY_RADIUS + 2 * POS_TOL:
                                err(f'{where}: pos {pos} vs {st.pointer}: {e:.5f} beyond the play radius')
                        if settled:
                            hori = vec(k['hori'])
                            r = math.radians(st.roll)
                            he = math.hypot(hori[0] - math.cos(r), hori[1] - math.sin(r))
                            stat('horizon error x spacing (px)', he * lpx)
                            if he > 4.3 / lpx:
                                err(f'{where}: horizon {hori} vs roll {st.roll}: {he:.4f}')
                            de = abs(float(k['dist']) / st.dist - 1.0)
                            stat('distance error x spacing (px)', de * lpx)
                            if de > 4.5 / lpx:
                                err(f'{where}: dist {k["dist"]} vs {st.dist}')
                # The game's cursor (RPSysCursorDrawMgr::preUpdate).
                if game:
                    if dpd > 0:
                        if 'cursor' not in k:
                            err(f'{where}: no cursor')
                        else:
                            cx, cy = vec(k['cursor'])
                            size = vec(k['size'])
                            px = max(-1.0, min(1.0, pos[0]))
                            py = max(-1.0, min(1.0, pos[1]))
                            if abs(cx - px * size[0] / 2) > 1e-3 or abs(cy + py * size[1] / 2) > 1e-3:
                                err(f'{where}: cursor {cx},{cy} is not pos {pos} on the screen')
                            if size != (W, H):
                                err(f'{where}: cursor screen {size}, window {W}x{H}')
                            if st.mouse is not None and dpd == 2 and c0 == 'in' and c1 == 'in' and anchored:
                                mx = max(0.0, min(W, st.mouse[0]))
                                my = max(0.0, min(H, st.mouse[1]))
                                e = math.hypot(cx + W / 2 - mx, H / 2 - cy - my)
                                stat('cursor error (px)', e)
                                count('cursor vs mouse')
                                if e > POS_TOL * W / 2:
                                    err(f'{where}: cursor at {cx + W / 2:.2f},{H / 2 - cy:.2f}, mouse at {mx},{my}')
                    elif 'cursor' in k:
                        err(f'{where}: cursor without the pointer')

            # Accelerometer: exactly the counts, within a count of the model.
            acc = vec(k['acc'])
            want, model = kpad_acc(st.roll, st.pitch, st.accel, 530, 636, True, 3.4)
            for i in range(3):
                if abs(acc[i] - want[i]) > 1e-6:
                    err(f'{where}: acc {acc} vs counts {want}')
                    break
            for i in range(3):
                mv = max(-3.4, min(3.4, model[i]))
                if abs(model[i]) < 3.3 and abs(acc[i] - mv) > 1.0 / CORE_G + 1e-6:
                    err(f'{where}: acc {acc} vs model {model}')
                    break
                if abs(model[i]) < 3.3:
                    stat('acc error (g)', abs(acc[i] - mv))
            if f - tilt_change >= SETTLE and st.accel == (0.0, 0.0, 0.0):
                accv = vec(k['accv'])
                p = math.radians(st.pitch)
                e = math.hypot(accv[0] - math.cos(p), accv[1] + math.sin(p))
                stat('acc_vertical error', e)
                count('acc_vertical')
                if e > 0.02:
                    err(f'{where}: acc_vertical {accv} vs pitch {st.pitch}')
            if pitch_ok is False and abs(st.pitch) >= 50 and f - tilt_change >= SETTLE:
                count('pitched away')
            if pitch_ok is False and abs(st.pitch) >= 50 and f - tilt_change >= SETTLE and dpd != 0:
                err(f'{where}: pitched {st.pitch} but dpd {dpd}')

            # Nunchuk.
            sbits = 0
            if st.nunchuk and f - fs_change >= 2:
                stick = vec(k['fsstick'])
                want_s = kpad_stick(*st.stick)
                count('Nunchuk')
                if abs(stick[0] - want_s[0]) > 1e-5 or abs(stick[1] - want_s[1]) > 1e-5:
                    err(f'{where}: stick {stick} vs {want_s}')
                fsacc = vec(k['fsacc'])
                want_a, model_a = kpad_acc(st.fsroll, st.fspitch, st.fsaccel, 512, 716, False, 2.1)
                for i in range(3):
                    if abs(fsacc[i] - want_a[i]) > 1e-6 or \
                            (abs(model_a[i]) < 2.0 and abs(fsacc[i] - model_a[i]) > 0.5 / FS_G + 1e-6):
                        err(f'{where}: Nunchuk acc {fsacc} vs {want_a} / {model_a}')
                        break
                if game:
                    sx, sy = stick
                    if not (-0.25 < sx < 0.25):
                        if sx <= -0.5:
                            sbits = 1 << 18
                        elif sx >= 0.5:
                            sbits = 1 << 19
                    if not (-0.25 < sy < 0.25):
                        if sy <= -0.5:
                            sbits = (sbits & ~(1 << 16)) | (1 << 17)
                        elif sy >= 0.5:
                            sbits = (sbits & ~(1 << 17)) | (1 << 16)

            # Buttons: read_kpad_button / calc_button_repeat, then EGG's stick bits.
            if f - fs_change < 2:
                # The extension's reports take a sample or two to start; resync.
                btn['hold'] = int(k['hold'], 16) & 0xFFFF
                stick_hold = int(k['hold'], 16) & 0xF0000 if game else 0
                continue
            cur = st.buttons & (0xFFFF if st.nunchuk else 0xFFFF & ~ZC)
            old = btn['hold'] & 0xFFFF
            change = cur ^ old
            trig = change & cur
            rel = change & old
            hold = cur
            if trig or rel:
                btn['time'] = 0
                btn['next'] = delay
                if trig and pulse:
                    hold |= 0x80000000
            elif hold:
                btn['time'] = (btn['time'] + reads) & 0xFFFF
                if btn['time'] >= 40000:
                    btn['time'] -= 40000
                if btn['time'] >= btn['next']:
                    hold |= 0x80000000
                    btn['next'] = (btn['next'] + pulse) & 0xFFFF
                    if btn['time'] >= 20000:
                        btn['time'] -= 20000
                        btn['next'] = (btn['next'] - 20000) & 0xFFFF
            btn['hold'] = hold
            if game:
                st_trig = sbits & ~stick_hold
                st_rel = stick_hold & ~sbits
                stick_hold = sbits
                hold |= sbits
                trig |= st_trig
                rel |= st_rel
            got = (int(k['hold'], 16), int(k['trig'], 16), int(k['rel'], 16))
            count('buttons')
            if trig or rel:
                count('button edges')
            if hold & 0x80000000:
                count('repeat flags')
            if got != (hold, trig, rel):
                err(f'{where}: hold/trig/rel {got[0]:08x}/{got[1]:08x}/{got[2]:08x}, '
                    f'expected {hold:08x}/{trig:08x}/{rel:08x}')

    # Rumble and events (the game's controller code only).
    want_m, want_e = [], []
    if game:
        pattern = '***-***---*'
        for chan in range(4):
            motor = 0
            unit = None
            for f in range(len(frames)):
                st = frames[f][chan]
                was = frames[f - 1][chan].connected if f > 0 else False
                if st.connected and not was:
                    want_e.append((f, f'speaker_on ch={chan} result=0'))
                    want_e.append((f, f'se=0 player={chan}'))
                    unit = {'ptr': 0, 'frame': len(pattern)}
                if was and not st.connected:
                    want_e.append((f, f'speaker_off ch={chan} result=-1'))
                    want_e.append((f, 'reset_old_data'))
                    if motor:
                        want_m.append((f, chan, 0))
                    motor = 0
                    unit = None
                    continue
                if not st.connected or unit is None:
                    continue
                # ControllerRumbleUnit::calc (pattern, loop with timer).
                unit['ptr'] += 1
                ch = pattern[unit['ptr']] if unit['ptr'] < len(pattern) else '\0'
                power = 1.0 if ch == '*' else 0.0
                if ch == '\0':
                    unit['ptr'] = 0
                unit['frame'] -= 1
                done = unit['frame'] <= 0
                on = 1 if power >= 1.0 else 0
                if on != motor:
                    want_m.append((f, chan, on))
                    motor = on
                if done:
                    unit = None
                    # The next frame's calc removes the unit and stops the motor.
                    if motor:
                        want_m.append((f + 1, chan, 0))
                        motor = 0
    got_m = sorted(motors)
    counts['motor changes'] = counts.get('motor changes', 0) + len(want_m)
    counts['events'] = counts.get('events', 0) + len(want_e)
    if got_m != sorted(want_m):
        err(f'{name} {mode}: motor changes {got_m[:12]}, expected {sorted(want_m)[:12]}')
    if sorted(events) != sorted(want_e):
        err(f'{name} {mode}: events {events[:8]}, expected {want_e[:8]}')

    # WPAD self-checks, on the last connected channel.
    last = frames[-1]
    chans = [c for c in range(4) if last[c].connected]
    if chans:
        c = chans[-1]
        st = last[c]
        packet = bytes((i * 7 + 1) & 0xFF for i in range(20))
        s = 0
        for b in packet:
            s = (s * 31 + b) & 0xFFFFFFFF
        want_s = [
            'info queued=0 callbacks_before=0',
            f'info callbacks=1 result=0 battery={st.battery} lowBat={int(st.battery <= 1)} dpd=1 '
            f'attach={int(st.nunchuk)} led={1 << c}',
            'speaker before=0 send=-2',
            'speaker enabled=1 can=1',
            f'speaker data ch={c} len=20 sum={s:08x}',
            'speaker send=0',
            'speaker off can=0',
            f'gravity core=106,106,106 fs={"204,204,204" if st.nunchuk else "0,0,0"} sensorbar=0 dpdsens=3',
            'probe=0 probe3=-1 type3=253',
        ]
        if selfchecks != want_s:
            err(f'{name} {mode}: self-checks {selfchecks}, expected {want_s}')
    return checked


def main():
    global VV1
    ap = argparse.ArgumentParser()
    ap.add_argument('--harness', required=True)
    ap.add_argument('--scripts', required=True)
    ap.add_argument('--compare', required=False)
    a = ap.parse_args()
    VV1 = f32(f32(0.2) / f32(0.383864))

    scripts = sorted(p for p in os.listdir(a.scripts) if p.endswith('.txt'))
    compare = a.compare if a.compare and os.path.exists(a.compare) else None
    total = 0
    compared = 0
    for s in scripts:
        path = os.path.join(a.scripts, s)
        window, frames = parse_script(path)
        for mode in ('game', 'kpad'):
            out = run(a.harness, path, mode)
            total += check_run(s, mode, window, frames, out)
            if compare:
                other = run(compare, path, mode)
                mine = [l for l in out.splitlines() if not l.startswith('input_harness:')]
                theirs = [l for l in other.splitlines() if not l.startswith('input_harness:')]
                if mine != theirs:
                    diff = next((i for i in range(min(len(mine), len(theirs))) if mine[i] != theirs[i]),
                                min(len(mine), len(theirs)))
                    err(f'{s} {mode}: differs from {compare} at line {diff + 1}')
                else:
                    compared += len(mine)

    print(f'test_input: {len(scripts)} scripts x 2 modes, {total} statuses checked')
    print('  checked: ' + ', '.join(f'{k} {counts[k]}' for k in sorted(counts)))
    for k in sorted(stats):
        print(f'  max {k}: {stats[k]:.5f}')
    if compare:
        print(f'  compared with {os.path.basename(os.path.dirname(compare))}: {compared} lines identical')
    else:
        print('  (no other build to compare with)')
    for e in errors[:40]:
        print('ERROR', e)
    print(f'errors: {len(errors)}')
    print('OK' if not errors else 'FAILED')
    return 0 if not errors else 1


if __name__ == '__main__':
    sys.exit(main())
