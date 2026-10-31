/**
 * Input mapping layer (docs/HOST_BUILD.md §23): host input state -> the raw
 * state of a virtual remote (port/input.h), and the scripted input reader the
 * tests use.
 *
 * Pointer. KPAD (KPAD.c: get_kobj, calc_obj_horizon, calc_dpd_variable) turns
 * the two sensor-bar dots p0, p1 (camera coordinates, -1..1 across the
 * 1024-pixel width) into
 *
 *     n    = (p1 - p0) / |p1 - p0|            horizon = (n.x, -n.y)
 *     dist = dist_vv1 / |p1 - p0|             dist_vv1 = objInterval / 0.383864
 *     pos  = (center_org - R(roll) * (p0 + p1) / 2) * dpd2pos_scale
 *
 * with center_org = (0, -sensorLevel), R the rotation that undoes the roll
 * read from n, and dpd2pos_scale = 1.25 / min(1, 0.75 - |sensorLevel|) (its
 * calc_dpd2pos_scale). Inverting it: for a wanted pos P, roll r and distance D
 * the dots are m -/+ (L/2) n with
 *
 *     n = (cos r, -sin r),  L = dist_vv1 / D,
 *     m = R(-r) * (center_org - P / dpd2pos_scale).
 *
 * The camera reports integer pixels: x = c.x * 512 + 511.5, y = c.y * 512 +
 * 383.5 (KPAD_DPD_SCALE, KPAD_DPD_CX / CY); the pixels are chosen so that
 * KPAD's math maps them closest to the wanted position (below, in
 * PortInputToRemote; kpadFromDots evaluates KPAD's formulas). A dot
 * outside the camera's frame (0..1022 x 0..766; the HID parser drops 1023 /
 * 767) is not reported, as on the Wii: KPAD then tracks one dot or loses the
 * pointer.
 *
 * Accelerometer. At rest the sensor reads the "up" vector (minus gravity) in
 * the remote's axes. KPAD's acc axes (read_kpad_acc: acc = (-accX, -accZ,
 * accY) / counts per g) give, for roll r and pitch p,
 *
 *     acc = (-cos p sin r, -cos p cos r, sin p) + extra
 *
 * so a level remote reads (0, -1, 0), KPAD's acc_horizon tends to
 * (cos r, sin r) (the dots' horizon) and acc_vertical to (cos p, -sin p).
 * Counts per g are the SDK's default calibration (106 for the remote, 204 for
 * the Nunchuk); the remote's Y and Z have 9 significant bits (the HID parser
 * keeps bit 1 of the low bits only), so their counts are even.
 *
 * Nunchuk stick. KPAD (clamp_stick_cross) maps counts 15..71 to 0..1 per
 * axis; a stick value v becomes 15 + round(|v| * 56) counts (0 for v = 0), so
 * KPAD reads v back to 1/56.
 */

#include <port/input.h>

#include <revolution/SC.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const double PI = 3.14159265358979323846;

// KPAD.c constants (KPAD_DPD_RESO_WX / WY, kp_fs_fstick_min / max).
const double DPD_RESO_WX = 1024.0;
const double DPD_RESO_WY = 768.0;
const int FS_STICK_MIN = 15;
const int FS_STICK_MAX = 71;
const u16 DOT_SIZE = 12;

double dpd2posScale(double level) {
    // calc_dpd2pos_scale with center_org = (0, -level).
    double sx = 1.0;
    double sy = DPD_RESO_WY / DPD_RESO_WX;
    double scale = std::sqrt(sx * sx + sy * sy);
    double cy = -level;
    sy = cy < 0.0 ? sy + cy : sy - cy;
    return scale / (sx < sy ? sx : sy);
}

s16 clampCount(long v, long lo, long hi) {
    return static_cast<s16>(v < lo ? lo : v > hi ? hi : v);
}

// Counts for an acceleration in g: raw (zero + counts) stays in 10 bits; an
// even axis keeps raw even (zero is even for both calibrations).
s16 accCount(double g, int zero, int one, bool even) {
    double c = g * (one - zero);
    long n = even ? 2 * std::lround(c / 2.0) : std::lround(c);
    long hi = even ? 1022 : 1023;
    return clampCount(n, -zero, hi - zero);
}

void kpadAcc(f32 roll, f32 pitch, const f32 extra[3], double out[3]) {
    double r = roll * PI / 180.0;
    double p = pitch * PI / 180.0;
    out[0] = -std::cos(p) * std::sin(r) + extra[0];
    out[1] = -std::cos(p) * std::cos(r) + extra[1];
    out[2] = std::sin(p) + extra[2];
}

// KPAD's position, horizon and distance for two dots (camera pixels), in
// double precision (calc_obj_horizon / calc_dpd_variable without smoothing).
void kpadFromDots(long x0, long y0, long x1, long y1, double level, double k, double distVV1,
                  double* px, double* py, double* hx, double* hy, double* dist) {
    double c0x = x0 * (2.0 / DPD_RESO_WX) - (DPD_RESO_WX - 1) / DPD_RESO_WX;
    double c0y = y0 * (2.0 / DPD_RESO_WX) - (DPD_RESO_WY - 1) / DPD_RESO_WX;
    double c1x = x1 * (2.0 / DPD_RESO_WX) - (DPD_RESO_WX - 1) / DPD_RESO_WX;
    double c1y = y1 * (2.0 / DPD_RESO_WX) - (DPD_RESO_WY - 1) / DPD_RESO_WX;
    double vx = c1x - c0x, vy = c1y - c0y;
    double len = std::sqrt(vx * vx + vy * vy);
    if (len == 0.0) {
        *px = *py = 1e9;
        *hx = *hy = 0.0;
        *dist = 0.0;
        return;
    }
    double nx = vx / len, ny = vy / len;
    double mx = (c0x + c1x) * 0.5, my = (c0y + c1y) * 0.5;
    double rx = nx * mx + ny * my;
    double ry = -ny * mx + nx * my;
    *px = (0.0 - rx) * k;
    *py = (-level - ry) * k;
    *hx = nx;
    *hy = -ny;
    *dist = distVV1 / len;
}

s8 stickCount(f32 v) {
    double a = std::fabs(v);
    if (a > 1.0) {
        a = 1.0;
    }
    long n = std::lround(a * (FS_STICK_MAX - FS_STICK_MIN));
    if (n == 0) {
        return 0;
    }
    n += FS_STICK_MIN;
    return static_cast<s8>(v < 0 ? -n : n);
}

struct ButtonName {
    const char* name;
    u16 bit;
};

const ButtonName sButtonNames[] = {
    {"A", WPAD_BUTTON_A},       {"B", WPAD_BUTTON_B},         {"1", WPAD_BUTTON_1},
    {"2", WPAD_BUTTON_2},       {"PLUS", WPAD_BUTTON_PLUS},   {"MINUS", WPAD_BUTTON_MINUS},
    {"HOME", WPAD_BUTTON_HOME}, {"UP", WPAD_BUTTON_UP},       {"DOWN", WPAD_BUTTON_DOWN},
    {"LEFT", WPAD_BUTTON_LEFT}, {"RIGHT", WPAD_BUTTON_RIGHT}, {"Z", WPAD_BUTTON_FS_Z},
    {"C", WPAD_BUTTON_FS_C},
};

} // namespace

extern "C" {

const PortInputBinding PortInputDefaultBindings[] = {
    {"Mouse1", WPAD_BUTTON_A},      // left button
    {"Mouse3", WPAD_BUTTON_B},      // right button
    {"Space", WPAD_BUTTON_A},
    {"Left Shift", WPAD_BUTTON_B},
    {"1", WPAD_BUTTON_1},
    {"2", WPAD_BUTTON_2},
    {"=", WPAD_BUTTON_PLUS},
    {"Keypad +", WPAD_BUTTON_PLUS},
    {"-", WPAD_BUTTON_MINUS},
    {"Keypad -", WPAD_BUTTON_MINUS},
    {"Escape", WPAD_BUTTON_HOME},
    {"Up", WPAD_BUTTON_UP},
    {"Down", WPAD_BUTTON_DOWN},
    {"Left", WPAD_BUTTON_LEFT},
    {"Right", WPAD_BUTTON_RIGHT},
    {"Z", WPAD_BUTTON_FS_Z},
    {"C", WPAD_BUTTON_FS_C},
    {NULL, 0},
};

void PortInputDefaultConfig(PortInputConfig* config) {
    // RPSysCoreController::applySetting's sensor heights.
    config->sensorLevel = SCGetWpadSensorBarPosition() == SC_SENSOR_BAR_TOP ? 0.15f : -0.35f;
    config->objInterval = 0.2f;
}

void PortInputDefaultState(PortInputState* state) {
    std::memset(state, 0, sizeof(*state));
    state->battery = 4;
    state->distance = 2.0f;
}

void PortInputPointerFromWindow(f32 px, f32 py, f32 width, f32 height, f32* x, f32* y) {
    *x = 2.0f * px / width - 1.0f;
    *y = 2.0f * py / height - 1.0f;
}

u16 PortInputButtonFromName(const char* name) {
    for (size_t i = 0; i < sizeof(sButtonNames) / sizeof(sButtonNames[0]); i++) {
        if (std::strcmp(sButtonNames[i].name, name) == 0) {
            return sButtonNames[i].bit;
        }
    }
    return 0;
}

void PortInputToRemote(const PortInputConfig* config, const PortInputState* state,
                       PortWPADRemote* remote) {
    std::memset(remote, 0, sizeof(*remote));
    remote->connected = state->connected;
    remote->dev = state->nunchuk ? WPAD_DEV_FREESTYLE : WPAD_DEV_CORE;
    remote->battery = static_cast<u8>(state->battery < 0 ? 0 : state->battery > 4 ? 4 : state->battery);
    remote->button = state->buttons;
    if (!state->nunchuk) {
        remote->button &= static_cast<u16>(~(WPAD_BUTTON_FS_Z | WPAD_BUTTON_FS_C));
    }
    for (int i = 0; i < WPAD_MAX_DPD_OBJECTS; i++) {
        remote->obj[i].x = 0;
        remote->obj[i].y = WPAD_MAX_DPD_Y;
        remote->obj[i].traceId = static_cast<u8>(i);
    }

    // Accelerometer.
    double acc[3];
    kpadAcc(state->roll, state->pitch, state->accel, acc);
    remote->acc[0] = accCount(-acc[0], PORT_WPAD_CORE_ACC_ZERO, PORT_WPAD_CORE_ACC_1G, false);
    remote->acc[1] = accCount(acc[2], PORT_WPAD_CORE_ACC_ZERO, PORT_WPAD_CORE_ACC_1G, true);
    remote->acc[2] = accCount(-acc[1], PORT_WPAD_CORE_ACC_ZERO, PORT_WPAD_CORE_ACC_1G, true);

    if (state->nunchuk) {
        kpadAcc(state->fsRoll, state->fsPitch, state->fsAccel, acc);
        remote->fsAcc[0] = accCount(-acc[0], PORT_WPAD_FS_ACC_ZERO, PORT_WPAD_FS_ACC_1G, false);
        remote->fsAcc[1] = accCount(acc[2], PORT_WPAD_FS_ACC_ZERO, PORT_WPAD_FS_ACC_1G, false);
        remote->fsAcc[2] = accCount(-acc[1], PORT_WPAD_FS_ACC_ZERO, PORT_WPAD_FS_ACC_1G, false);
        remote->fsStick[0] = stickCount(state->stickX);
        remote->fsStick[1] = stickCount(state->stickY);
    }

    // Sensor bar dots.
    if (!state->pointerValid || state->distance <= 0.0f) {
        return;
    }
    double k = dpd2posScale(config->sensorLevel);
    double r = state->roll * PI / 180.0;
    double c = std::cos(r), s = std::sin(r);
    double vx = 0.0 - state->pointerX / k;
    double vy = -config->sensorLevel - state->pointerY / k;
    double mx = c * vx + s * vy;
    double my = -s * vx + c * vy;
    double distVV1 = static_cast<f32>(config->objInterval / 0.383864f);
    double half = distVV1 / state->distance * 0.5;
    double nx = c, ny = -s;

    // The camera's pixels. The pair's offset (dot 1 - dot 0) depends on the
    // roll and distance only: KPAD drops a pair whose distance moved more
    // than 0.04 since the last report (kp_err_dist_speed), and one pixel of
    // spacing is more than that beyond 3 m. Offsets within two pixels of the
    // rounded one whose length is within 0.3 pixels of it are allowed (a
    // tilt by a pixel or two hardly changes the length); of those and of the
    // positions within three pixels of the exact dot 0, the pair for which
    // KPAD's math gives the position closest to the wanted one is reported
    // (then the closest horizon). Plain rounding is off by up to half a pixel
    // in each coordinate, about 0.003 of KPAD's position; the tilt helps
    // because KPAD's roll correction turns it into a small shift.
    double x0 = (mx - half * nx) * 512.0 + 511.5;
    double y0 = (my - half * ny) * 512.0 + 383.5;
    long DX = std::lround(2.0 * half * nx * 512.0), DY = std::lround(2.0 * half * ny * 512.0);
    double refLen = std::sqrt(static_cast<double>(DX * DX + DY * DY));
    long X0 = std::lround(x0), Y0 = std::lround(y0);
    double best = -1.0;
    long bx0 = X0, by0 = Y0, bdx = DX, bdy = DY;
    for (int j = 0; j < 25; j++) {
        long dx = DX + j % 5 - 2, dy = DY + j / 5 - 2;
        double len = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        if (std::fabs(len - refLen) > 0.3) {
            continue;
        }
        for (int i = 0; i < 49; i++) {
            long cx0 = X0 + i % 7 - 3, cy0 = Y0 + i / 7 - 3;
            double ox, oy, hx, hy, d;
            kpadFromDots(cx0, cy0, cx0 + dx, cy0 + dy, config->sensorLevel, k, distVV1, &ox, &oy,
                         &hx, &hy, &d);
            double ex = ox - state->pointerX, ey = oy - state->pointerY;
            double e = ex * ex + ey * ey + 1e-4 * ((hx - c) * (hx - c) + (hy - s) * (hy - s));
            if (best < 0.0 || e < best) {
                best = e;
                bx0 = cx0, by0 = cy0, bdx = dx, bdy = dy;
            }
        }
    }
    X0 = bx0, Y0 = by0;
    long X1 = bx0 + bdx, Y1 = by0 + bdy;

    const long xs[2] = {X0, X1};
    const long ys[2] = {Y0, Y1};
    for (int i = 0; i < 2; i++) {
        if (xs[i] < 0 || xs[i] > WPAD_MAX_DPD_X - 1 || ys[i] < 0 || ys[i] > WPAD_MAX_DPD_Y - 1) {
            continue;
        }
        remote->obj[i].x = static_cast<s16>(xs[i]);
        remote->obj[i].y = static_cast<s16>(ys[i]);
        remote->obj[i].size = DOT_SIZE;
    }
}

/* ------------------------------------------------------------------------
 * Scripted input
 * ---------------------------------------------------------------------- */

enum ScriptOp {
    OP_CHAN,
    OP_CONNECT,
    OP_DISCONNECT,
    OP_NUNCHUK,
    OP_BATTERY,
    OP_MOUSE,
    OP_POINTER,
    OP_OFFSCREEN,
    OP_DIST,
    OP_PRESS,
    OP_RELEASE,
    OP_ROLL,
    OP_PITCH,
    OP_ACCEL,
    OP_STICK,
    OP_FSROLL,
    OP_FSPITCH,
    OP_FSACCEL,
    OP_AIM,
    OP_FIRE,
    OP_PLAY,
    OP_SHAKE,
};

struct ScriptCmd {
    int frame;
    int chan;
    ScriptOp op;
    f32 args[4];
    u16 buttons;
};

struct PortInputScript {
    ScriptCmd* cmds;
    int count;
    int next;
    int end;
    f32 width, height;
    int aim[WPAD_MAX_CONTROLLERS];
    int firePeriod[WPAD_MAX_CONTROLLERS], fireStart[WPAD_MAX_CONTROLLERS];
    int play[WPAD_MAX_CONTROLLERS];
    int shakeEnd[WPAD_MAX_CONTROLLERS];
    f32 shakeAcc[WPAD_MAX_CONTROLLERS][3];
};

namespace {

struct OpName {
    const char* name;
    ScriptOp op;
    int nargs;  // numbers; -1: button names
};

const OpName sOps[] = {
    {"chan", OP_CHAN, 1},       {"connect", OP_CONNECT, 0}, {"disconnect", OP_DISCONNECT, 0},
    {"nunchuk", OP_NUNCHUK, 1}, {"battery", OP_BATTERY, 1}, {"mouse", OP_MOUSE, 2},
    {"pointer", OP_POINTER, 2}, {"offscreen", OP_OFFSCREEN, 0}, {"dist", OP_DIST, 1},
    {"press", OP_PRESS, -1},    {"release", OP_RELEASE, -1}, {"roll", OP_ROLL, 1},
    {"pitch", OP_PITCH, 1},     {"accel", OP_ACCEL, 3},     {"stick", OP_STICK, 2},
    {"fsroll", OP_FSROLL, 1},   {"fspitch", OP_FSPITCH, 1}, {"fsaccel", OP_FSACCEL, 3},
    {"aim", OP_AIM, 1},         {"fire", OP_FIRE, 1},       {"play", OP_PLAY, 1},
    {"shake", OP_SHAKE, 4},
};

int tokenize(char* line, char** tok, int max) {
    int n = 0;
    for (char* p = std::strtok(line, " \t\r\n"); p != NULL && n < max; p = std::strtok(NULL, " \t\r\n")) {
        tok[n++] = p;
    }
    return n;
}

bool parseNumber(const char* s, f32* out) {
    char* end;
    double v = std::strtod(s, &end);
    if (end == s || *end != '\0') {
        return false;
    }
    *out = static_cast<f32>(v);
    return true;
}

} // namespace

PortInputScript* PortInputScriptLoad(const char* path, char* err, int errLen) {
    FILE* fp = std::fopen(path, "r");
    if (fp == NULL) {
        std::snprintf(err, errLen, "cannot open %s", path);
        return NULL;
    }
    PortInputScript* s = static_cast<PortInputScript*>(std::calloc(1, sizeof(PortInputScript)));
    s->width = 608.0f;
    s->height = 456.0f;
    s->end = -1;
    int cap = 0;
    int chan = 0;
    int lastFrame = 0;
    char line[512];
    int lineNo = 0;
    bool ok = true;

    while (ok && std::fgets(line, sizeof(line), fp) != NULL) {
        lineNo++;
        char* hash = std::strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char* tok[16];
        int n = tokenize(line, tok, 16);
        if (n == 0) {
            continue;
        }
        f32 a, b;
        if (std::strcmp(tok[0], "window") == 0) {
            if (n != 3 || !parseNumber(tok[1], &a) || !parseNumber(tok[2], &b) || a <= 0 || b <= 0) {
                std::snprintf(err, errLen, "%s:%d: window W H", path, lineNo);
                ok = false;
            }
            s->width = a;
            s->height = b;
            continue;
        }
        if (std::strcmp(tok[0], "end") == 0) {
            if (n != 2 || !parseNumber(tok[1], &a) || a < lastFrame) {
                std::snprintf(err, errLen, "%s:%d: end FRAME", path, lineNo);
                ok = false;
            }
            s->end = static_cast<int>(a);
            continue;
        }

        f32 frame;
        if (n < 2 || !parseNumber(tok[0], &frame) || frame < lastFrame) {
            std::snprintf(err, errLen, "%s:%d: FRAME COMMAND [ARGS] (frames in order)", path, lineNo);
            ok = false;
            break;
        }
        lastFrame = static_cast<int>(frame);
        const OpName* op = NULL;
        for (size_t i = 0; i < sizeof(sOps) / sizeof(sOps[0]); i++) {
            if (std::strcmp(sOps[i].name, tok[1]) == 0) {
                op = &sOps[i];
            }
        }
        if (op == NULL) {
            std::snprintf(err, errLen, "%s:%d: unknown command %s", path, lineNo, tok[1]);
            ok = false;
            break;
        }
        ScriptCmd cmd;
        std::memset(&cmd, 0, sizeof(cmd));
        cmd.frame = lastFrame;
        cmd.op = op->op;
        if (op->nargs >= 0) {
            if (n - 2 != op->nargs) {
                std::snprintf(err, errLen, "%s:%d: %s takes %d numbers", path, lineNo, op->name, op->nargs);
                ok = false;
                break;
            }
            for (int i = 0; i < op->nargs; i++) {
                if (!parseNumber(tok[2 + i], &cmd.args[i])) {
                    std::snprintf(err, errLen, "%s:%d: bad number %s", path, lineNo, tok[2 + i]);
                    ok = false;
                }
            }
        } else {
            for (int i = 2; i < n; i++) {
                u16 bit = PortInputButtonFromName(tok[i]);
                if (bit == 0) {
                    std::snprintf(err, errLen, "%s:%d: unknown button %s", path, lineNo, tok[i]);
                    ok = false;
                }
                cmd.buttons |= bit;
            }
        }
        if (cmd.op == OP_CHAN) {
            if (cmd.args[0] < 0 || cmd.args[0] >= WPAD_MAX_CONTROLLERS) {
                std::snprintf(err, errLen, "%s:%d: chan 0-3", path, lineNo);
                ok = false;
            }
            chan = static_cast<int>(cmd.args[0]);
        }
        cmd.chan = chan;
        if (s->count == cap) {
            cap = cap ? cap * 2 : 64;
            s->cmds = static_cast<ScriptCmd*>(std::realloc(s->cmds, sizeof(ScriptCmd) * cap));
        }
        s->cmds[s->count++] = cmd;
    }
    std::fclose(fp);
    if (ok && s->end < 0) {
        s->end = lastFrame + 1;
    }
    if (!ok) {
        PortInputScriptFree(s);
        return NULL;
    }
    return s;
}

void PortInputScriptFree(PortInputScript* script) {
    if (script != NULL) {
        std::free(script->cmds);
        std::free(script);
    }
}

int PortInputScriptFrames(const PortInputScript* script) {
    return script->end;
}

void PortInputScriptWindow(const PortInputScript* script, f32* width, f32* height) {
    *width = script->width;
    *height = script->height;
}

void PortInputScriptApply(PortInputScript* script, int frame, PortInputState states[4]) {
    while (script->next < script->count && script->cmds[script->next].frame <= frame) {
        const ScriptCmd& c = script->cmds[script->next++];
        PortInputState& st = states[c.chan];
        switch (c.op) {
        case OP_CHAN:
            break;
        case OP_CONNECT:
            st.connected = 1;
            break;
        case OP_DISCONNECT:
            st.connected = 0;
            break;
        case OP_NUNCHUK:
            st.nunchuk = c.args[0] != 0.0f;
            break;
        case OP_BATTERY:
            st.battery = static_cast<int>(c.args[0]);
            break;
        case OP_MOUSE:
            st.pointerValid = 1;
            PortInputPointerFromWindow(c.args[0], c.args[1], script->width, script->height,
                                       &st.pointerX, &st.pointerY);
            break;
        case OP_POINTER:
            st.pointerValid = 1;
            st.pointerX = c.args[0];
            st.pointerY = c.args[1];
            break;
        case OP_OFFSCREEN:
            st.pointerValid = 0;
            break;
        case OP_DIST:
            st.distance = c.args[0];
            break;
        case OP_PRESS:
            st.buttons |= c.buttons;
            break;
        case OP_RELEASE:
            st.buttons &= static_cast<u16>(~c.buttons);
            break;
        case OP_ROLL:
            st.roll = c.args[0];
            break;
        case OP_PITCH:
            st.pitch = c.args[0];
            break;
        case OP_ACCEL:
            std::memcpy(st.accel, c.args, sizeof(st.accel));
            break;
        case OP_STICK:
            st.stickX = c.args[0];
            st.stickY = c.args[1];
            break;
        case OP_FSROLL:
            st.fsRoll = c.args[0];
            break;
        case OP_FSPITCH:
            st.fsPitch = c.args[0];
            break;
        case OP_FSACCEL:
            std::memcpy(st.fsAccel, c.args, sizeof(st.fsAccel));
            break;
        case OP_AIM:
            script->aim[c.chan] = c.args[0] != 0.0f;
            break;
        case OP_PLAY:
            script->play[c.chan] = c.args[0] != 0.0f;
            break;
        case OP_SHAKE:
            // An acceleration pulse on top of `accel` for N frames (a flick or a jerk)
            if (script->shakeEnd[c.chan] > frame) {
                for (int i = 0; i < 3; i++) {
                    st.accel[i] -= script->shakeAcc[c.chan][i];
                }
            }
            for (int i = 0; i < 3; i++) {
                script->shakeAcc[c.chan][i] = c.args[i];
                st.accel[i] += c.args[i];
            }
            script->shakeEnd[c.chan] = c.frame + static_cast<int>(c.args[3]);
            break;
        case OP_FIRE:
            script->firePeriod[c.chan] = static_cast<int>(c.args[0]);
            script->fireStart[c.chan] = c.frame;
            if (script->firePeriod[c.chan] <= 0) {
                st.buttons &= static_cast<u16>(~WPAD_BUTTON_B);
            }
            break;
        }
    }
    // The end of a shake pulse
    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        if (script->shakeEnd[chan] != 0 && frame >= script->shakeEnd[chan]) {
            for (int i = 0; i < 3; i++) {
                states[chan].accel[i] -= script->shakeAcc[chan][i];
                script->shakeAcc[chan][i] = 0.0f;
            }
            script->shakeEnd[chan] = 0;
        }
    }
    // Rapid fire: B pressed for the first half of every period
    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        int period = script->firePeriod[chan];
        if (period >= 2) {
            int phase = (frame - script->fireStart[chan]) % period;
            if (phase == 0) {
                states[chan].buttons |= WPAD_BUTTON_B;
            } else if (phase == period / 2) {
                states[chan].buttons &= static_cast<u16>(~WPAD_BUTTON_B);
            }
        }
    }
    // Aiming: the game-side probe (if linked) places the pointer on a target
    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        f32 x, y;
        if (script->aim[chan] && PortInputAimHook != NULL && PortInputAimHook(chan, &x, &y)) {
            states[chan].pointerValid = 1;
            states[chan].pointerX = x;
            states[chan].pointerY = y;
        }
    }
    // Playing: the game-side probe (if linked) sets the whole input state
    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        if (script->play[chan] && PortInputPlayHook != NULL) {
            PortInputPlayHook(chan, frame, &states[chan]);
        }
    }
}

} // extern "C"
