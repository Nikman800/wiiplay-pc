/**
 * The live platform layer (include/port/live.h, port/docs/HOST_BUILD.md §30):
 * real-time pacing, audio output and window-system input for build/host64/boot.
 * Everything here goes through wpgx (SDL3 inside the Aurora library).
 *
 * Host code: allocates with malloc only (operator new is the game's EGG::Heap).
 */

#include <types.h>

#include <revolution/WPAD.h>

#include <port/host.h>
#include <port/input.h>
#include <port/live.h>
#include <port/perf.h>
#include <port/platform.h>
#include <port/record.h>
#include <port/speaker.h>

#include "wpgx.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

// --- Config ---------------------------------------------------------------------

// SDL_GamepadButton / SDL_GamepadAxis values (SDL3's stable enums)
enum {
    PAD_SOUTH, PAD_EAST, PAD_WEST, PAD_NORTH, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LEFT_STICK,
    PAD_RIGHT_STICK, PAD_LEFT_SHOULDER, PAD_RIGHT_SHOULDER, PAD_DPAD_UP, PAD_DPAD_DOWN, PAD_DPAD_LEFT,
    PAD_DPAD_RIGHT, PAD_MISC1, PAD_BUTTONS = 26
};
enum { AXIS_LEFTX, AXIS_LEFTY, AXIS_RIGHTX, AXIS_RIGHTY, AXIS_LEFT_TRIGGER, AXIS_RIGHT_TRIGGER, AXIS_COUNT };

struct PadName {
    const char* name;
    int code; // button, or 100 + trigger axis
};
const PadName kPadNames[] = {
    {"south", PAD_SOUTH}, {"a", PAD_SOUTH}, {"east", PAD_EAST}, {"b", PAD_EAST}, {"west", PAD_WEST},
    {"x", PAD_WEST}, {"north", PAD_NORTH}, {"y", PAD_NORTH}, {"back", PAD_BACK}, {"guide", PAD_GUIDE},
    {"start", PAD_START}, {"leftstick", PAD_LEFT_STICK}, {"rightstick", PAD_RIGHT_STICK},
    {"leftshoulder", PAD_LEFT_SHOULDER}, {"rightshoulder", PAD_RIGHT_SHOULDER}, {"dpup", PAD_DPAD_UP},
    {"dpdown", PAD_DPAD_DOWN}, {"dpleft", PAD_DPAD_LEFT}, {"dpright", PAD_DPAD_RIGHT}, {"misc1", PAD_MISC1},
    {"lefttrigger", 100 + AXIS_LEFT_TRIGGER}, {"righttrigger", 100 + AXIS_RIGHT_TRIGGER},
};

const int MAX_KEY_BINDS = 64;
// A binding gives remote buttons (WPAD_BUTTON_*, the low 16 bits) and motion
// actions (the gesture layer, HOST_BUILD.md §37)
enum : u32 {
    ACT_TWIST_LEFT = 1u << 16,
    ACT_TWIST_RIGHT = 1u << 17,
    ACT_TILT_FORWARD = 1u << 18,
    ACT_TILT_BACK = 1u << 19,
    ACT_TILT_LEFT = 1u << 20,
    ACT_TILT_RIGHT = 1u << 21,
    ACT_SHAKE = 1u << 22,
    ACT_PUSH = 1u << 23,
    ACT_PULL = 1u << 24,
    ACT_TWIST_RESET = 1u << 25,
};
struct ActionName {
    const char* name;
    u32 bit;
};
const ActionName kActions[] = {
    {"TWIST_LEFT", ACT_TWIST_LEFT}, {"TWIST_RIGHT", ACT_TWIST_RIGHT}, {"TILT_FORWARD", ACT_TILT_FORWARD},
    {"TILT_BACK", ACT_TILT_BACK},   {"TILT_LEFT", ACT_TILT_LEFT},     {"TILT_RIGHT", ACT_TILT_RIGHT},
    {"SHAKE", ACT_SHAKE},           {"PUSH", ACT_PUSH},               {"PULL", ACT_PULL},
    {"TWIST_RESET", ACT_TWIST_RESET},
};

struct KeyBind {
    char name[32]; // SDL key name or MouseN
    u32 button;
};
KeyBind sKeyBinds[MAX_KEY_BINDS];
int sKeyBindCount = 0;
u32 sPadBinds[PAD_BUTTONS + 2]; // buttons, then the two triggers

PortLiveConfig sConfig = {1.5f, 2.0f, 1, 1, 1.0f, 0, 0, 0.2f, 0,
                          /* leftStick */ 0, /* twistSpeed */ 120.0f, /* wheelTwist */ 10.0f,
                          /* tiltAngle */ 60.0f, /* tiltSpeed */ 360.0f, /* shakeStrength */ 2.5f,
                          /* shakeFrames */ 4, /* pullDistance */ 0.3f, /* pushDistance */ 0.3f,
                          /* speakerVolume */ 0.5f};

bool NameEq(const char* a, const char* b);

// A binding's name: a remote button, a motion action or "none"; false if unknown
bool BindFromName(const char* name, u32* out) {
    if (NameEq(name, "none")) {
        *out = 0;
        return true;
    }
    for (const ActionName& a : kActions) {
        if (NameEq(a.name, name)) {
            *out = a.bit;
            return true;
        }
    }
    *out = PortInputButtonFromName(name);
    return *out != 0;
}

bool NameEq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

void AddKeyBind(const char* name, u32 button) {
    for (int i = 0; i < sKeyBindCount; i++) {
        if (NameEq(sKeyBinds[i].name, name)) {
            sKeyBinds[i].button = button;
            return;
        }
    }
    if (sKeyBindCount < MAX_KEY_BINDS) {
        std::snprintf(sKeyBinds[sKeyBindCount].name, sizeof sKeyBinds[0].name, "%s", name);
        sKeyBinds[sKeyBindCount++].button = button;
    }
}

void DefaultBindings() {
    sKeyBindCount = 0;
    for (const PortInputBinding* b = PortInputDefaultBindings; b->name != NULL; b++) {
        AddKeyBind(b->name, b->button);
    }
    AddKeyBind("Return", WPAD_BUTTON_A);
    AddKeyBind("Backspace", WPAD_BUTTON_B);
    AddKeyBind("H", WPAD_BUTTON_HOME);
    // Motions (§37): twist, tilt, shake, pull back and thrust, push
    AddKeyBind("Q", ACT_TWIST_LEFT);
    AddKeyBind("E", ACT_TWIST_RIGHT);
    AddKeyBind("W", ACT_TILT_FORWARD);
    AddKeyBind("S", ACT_TILT_BACK);
    AddKeyBind("A", ACT_TILT_LEFT);
    AddKeyBind("D", ACT_TILT_RIGHT);
    AddKeyBind("F", ACT_SHAKE);
    AddKeyBind("R", ACT_PULL);
    AddKeyBind("G", ACT_PUSH);
    AddKeyBind("Mouse2", ACT_TWIST_RESET); // middle button
    AddKeyBind("Mouse4", ACT_SHAKE);       // side buttons
    AddKeyBind("Mouse5", ACT_PULL);
    std::memset(sPadBinds, 0, sizeof sPadBinds);
    sPadBinds[PAD_SOUTH] = WPAD_BUTTON_A;
    sPadBinds[PAD_EAST] = WPAD_BUTTON_B;
    sPadBinds[PAD_WEST] = WPAD_BUTTON_1;
    sPadBinds[PAD_NORTH] = WPAD_BUTTON_2;
    sPadBinds[PAD_START] = WPAD_BUTTON_PLUS;
    sPadBinds[PAD_BACK] = WPAD_BUTTON_MINUS;
    sPadBinds[PAD_GUIDE] = WPAD_BUTTON_HOME;
    sPadBinds[PAD_DPAD_UP] = WPAD_BUTTON_UP;
    sPadBinds[PAD_DPAD_DOWN] = WPAD_BUTTON_DOWN;
    sPadBinds[PAD_DPAD_LEFT] = WPAD_BUTTON_LEFT;
    sPadBinds[PAD_DPAD_RIGHT] = WPAD_BUTTON_RIGHT;
    // Wii Play reads neither Nunchuk button: the shoulders and the left trigger make motions
    sPadBinds[PAD_LEFT_SHOULDER] = ACT_PULL;
    sPadBinds[PAD_RIGHT_SHOULDER] = ACT_SHAKE;
    sPadBinds[PAD_LEFT_STICK] = ACT_TWIST_RESET;
    sPadBinds[PAD_BUTTONS + 0] = ACT_PUSH;      // left trigger
    sPadBinds[PAD_BUTTONS + 1] = WPAD_BUTTON_B; // right trigger
}

int PadIndex(const char* name) {
    for (const PadName& p : kPadNames) {
        if (NameEq(p.name, name)) {
            return p.code >= 100 ? PAD_BUTTONS + (p.code - 100 - AXIS_LEFT_TRIGGER) : p.code;
        }
    }
    return -1;
}

// Splits a line into at most `max` words (more are ignored). A word in double
// quotes may contain spaces: key names such as "Left Shift".
int Split(char* line, char** words, int max) {
    int n = 0;
    char* p = line;
    while (n < max) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == '"') {
            char* end = std::strchr(p + 1, '"');
            if (end == nullptr) {
                return -1;
            }
            *end = '\0';
            words[n++] = p + 1;
            p = end + 1;
            continue;
        }
        words[n++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
    return n;
}

// --- Pacing and audio ----------------------------------------------------------------

const u32 AUDIO_RATE = 32000;
const u32 AUDIO_TARGET = 1600;  // frames queued in steady state (50 ms)
const u32 AUDIO_PREFILL = 1600; // silence queued when the stream starts or runs dry
const u32 AUDIO_HIGH = 4800;    // above this (150 ms) an AX frame is skipped
const u32 AUDIO_LIMIT = 9600;   // above this (300 ms: the device stalled) the queue is dropped
const f64 AX_FRAME_SECONDS = 96.0 / AUDIO_RATE;
// The device steers the pace by at most this fraction of real time (0.5 %):
// a device clock's drift is far smaller; a device running slower than that
// costs skipped AX frames instead of slowing the game.
const f64 AUDIO_MAX_CORRECTION = 0.005;
// Seconds of correction per AX frame per second of queue error: 10 ms of error
// gives 0.33 % (time constant about 3 s); clamped to the bound above.
const f64 AUDIO_GAIN = 0.001;

bool sPacing = false;
bool sStarted = false;
f32 sSpeed = 1.0f;
Clock::time_point sAnchor;
long long sAnchorUnits = 0;
Clock::time_point sStart;
bool sAudio = false;
bool sAudioPrimed = false;
bool sFocused = true;
PortLiveStats sStats;

// The device paces the game: each AX frame nudges the anchor by a bounded
// fraction of the queue's distance from its target (a proportional
// controller).
void SteerByAudio(u32 queued) {
    f64 error = (static_cast<f64>(queued) - AUDIO_TARGET) / AUDIO_RATE; // seconds ahead
    f64 shift = error * AUDIO_GAIN;
    const f64 bound = AUDIO_MAX_CORRECTION * AX_FRAME_SECONDS;
    shift = shift > bound ? bound : (shift < -bound ? -bound : shift);
    sStats.correction = shift / AX_FRAME_SECONDS;
    sAnchor += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(shift));
}

void Pace(long long units, void*) {
    Clock::time_point now = Clock::now();
    if (!sStarted) {
        sStarted = true;
        sStart = now;
        sAnchor = now;
        sAnchorUnits = units;
        return;
    }
    double seconds = static_cast<double>(units - sAnchorUnits) / 60000.0 / sSpeed;
    Clock::time_point target =
        sAnchor + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    if (now > target + std::chrono::milliseconds(150)) {
        // Far behind (a load, a stall, a debugger): continue from now instead
        // of rushing to catch up.
        sAnchor = now;
        sAnchorUnits = units;
        sStats.lateResets++;
        return;
    }
    if (target > now) {
        u64 start = PortPerfNow();
        // port/platform.h: the OS sleep, then a short spin (Windows' sleeps are coarse)
        PortSleepUntilNs(static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(target.time_since_epoch()).count()));
        PortPerfAdd(PORT_PERF_SLEEP, PortPerfNow() - start);
    }
}

// --- Input -----------------------------------------------------------------------

struct PadState {
    bool present = false;
    bool buttons[PAD_BUTTONS] = {};
    u32 pressPoll[PAD_BUTTONS] = {};    // the poll that saw the press
    bool releaseNext[PAD_BUTTONS] = {}; // released in that poll: up at the next
    f32 axes[AXIS_COUNT] = {};
};
PadState sPads[WPAD_MAX_CONTROLLERS];
bool sInput = false;
bool sMerge = false; // live input over a test script's remotes
u16 sKeyButtons = 0;   // keyboard + mouse, remote 0
f32 sMouseX = 0.0f, sMouseY = 0.0f;
f32 sMouseWinX = 0.0f, sMouseWinY = 0.0f; // the last position in window points
bool sMouseSeen = false;
char sWindowInfo[640];
u32 sWindowInfoPoll = 0;
f32 sWheelY = 0.0f; // wheel steps since the last poll
f32 sPointerX[WPAD_MAX_CONTROLLERS], sPointerY[WPAD_MAX_CONTROLLERS];
Clock::time_point sLastPoll;
bool sPolled = false;
bool sMotor[WPAD_MAX_CONTROLLERS];

// Keys currently held, so a binding change or two keys on one button work.
// A key pressed and released between two polls (a quick click, or a remote
// desktop delivering both at once) stays held until the next poll, so the
// game sees at least one frame of it (3-4 WPAD reports).
const int MAX_HELD = 32;
struct Held {
    char name[32];
    u32 poll;         // the poll that saw the press
    bool releaseNext; // released in that same poll: drop at the next one
};
Held sHeld[MAX_HELD];
int sHeldCount = 0;
u32 sPollCount = 0;

u32 KeyButtons() {
    u32 b = 0;
    for (int i = 0; i < sHeldCount; i++) {
        for (int k = 0; k < sKeyBindCount; k++) {
            if (NameEq(sKeyBinds[k].name, sHeld[i].name)) {
                b |= sKeyBinds[k].button;
            }
        }
    }
    return b;
}

void DropHeld(int i) {
    std::memmove(&sHeld[i], &sHeld[i + 1], (sHeldCount - i - 1) * sizeof sHeld[0]);
    sHeldCount--;
}

void SetHeld(const char* name, bool down) {
    for (int i = 0; i < sHeldCount; i++) {
        if (NameEq(sHeld[i].name, name)) {
            if (down) {
                sHeld[i].releaseNext = false;
            } else if (sHeld[i].poll == sPollCount) {
                sHeld[i].releaseNext = true;
            } else {
                DropHeld(i);
            }
            return;
        }
    }
    if (down && sHeldCount < MAX_HELD) {
        std::snprintf(sHeld[sHeldCount].name, sizeof sHeld[0].name, "%s", name);
        sHeld[sHeldCount].poll = sPollCount;
        sHeld[sHeldCount].releaseNext = false;
        sHeldCount++;
    }
}

// Logs the window's geometry and the mouse mapping when it changed (stderr and
// the --perf summary)
bool NoteWindow(const char* why) {
    char info[sizeof sWindowInfo];
    wpgx_window_info(info, sizeof info);
    if (std::strcmp(info, sWindowInfo) == 0) {
        return false;
    }
    std::snprintf(sWindowInfo, sizeof sWindowInfo, "%s", info);
    std::fprintf(stderr, "boot: %s: %s\n", why, info);
    char line[sizeof sWindowInfo + 32];
    std::snprintf(line, sizeof line, "%s: %s", why, info);
    PortPerfNote(line);
    return true;
}

// The mouse's last window position on the picture (again after the window changed)
void MapMouse() {
    f32 u, v;
    wpgx_window_to_picture(sMouseWinX, sMouseWinY, &u, &v);
    PortInputPointerFromWindow(u, v, 1.0f, 1.0f, &sMouseX, &sMouseY);
    sPointerX[0] = sMouseX;
    sPointerY[0] = sMouseY;
}

// The releases deferred from the last poll
void ReleaseDeferred() {
    for (int i = sHeldCount - 1; i >= 0; i--) {
        if (sHeld[i].releaseNext) {
            DropHeld(i);
        }
    }
    for (PadState& p : sPads) {
        for (int b = 0; b < PAD_BUTTONS; b++) {
            if (p.releaseNext[b]) {
                p.buttons[b] = false;
                p.releaseNext[b] = false;
            }
        }
    }
}

f32 Deadzone(f32 v) {
    f32 dz = sConfig.stickDeadzone;
    if (std::fabs(v) <= dz) {
        return 0.0f;
    }
    return (v > 0 ? v - dz : v + dz) / (1.0f - dz);
}

void Motor(s32 chan, int on, void*) {
    if (chan < 0 || chan >= WPAD_MAX_CONTROLLERS) {
        return;
    }
    sMotor[chan] = on != 0;
    if (!sConfig.rumble || !sPads[chan].present) {
        return;
    }
    if (on) {
        sStats.rumbleOn++;
    }
    // The remote's motor is on or off; a long duration, renewed while on
    wpgx_gamepad_rumble(chan, on ? 0.5f : 0.0f, on ? 0.5f : 0.0f, on ? 2000 : 0);
}

f32 Clamp(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// --- Motions (HOST_BUILD.md §37) ---------------------------------------------------
// The gesture layer turns buttons and a stick into the remote motions the games
// detect: a twist (roll), a tilt (pitch, or roll for a remote held sideways), a
// shake (an acceleration pulse along the remote's up vector), a pull back and
// thrust (the distance from the sensor bar, with the thrust's acceleration) and
// a push (closer to the screen while held). It adds to the remote's state.

const f32 FRAME_SECONDS = 1001.0f / 60000.0f; // one poll per retrace: deterministic

struct Gesture {
    f32 twist = 0.0f;           // degrees, persistent (the keys turn it, the wheel steps it)
    f32 fwd = 0.0f, side = 0.0f; // the tilt keys' springs (-1..1)
    f32 stickX = 0.0f, stickY = 0.0f; // the left stick in a tilt game (this poll)
    int shakeLeft = 0;          // polls of the shake pulse left
    bool shakeHeld = false;
    f32 dist = 0.0f;            // metres added to the distance
    int pull = 0;               // 0 idle, 1 drawing back, 2 thrusting, 3 returning
    f32 fwdDeg = 0.0f, sideDeg = 0.0f; // the tilt this poll (degrees)
    f32 thrustSpeed = 0.0f, thrustFrac = 0.0f;
};
Gesture sGestures[WPAD_MAX_CONTROLLERS];

int Hold(int chan) {
    return PortInputHoldHook != nullptr ? PortInputHoldHook(chan) : 0;
}

f32 Approach(f32 v, f32 target, f32 step) {
    return v < target ? (v + step > target ? target : v + step) : (v - step < target ? target : v - step);
}

// The gesture state from this poll's actions (the motions are added to the
// remote's state by PortLiveApplyMotion, on the copy the WPAD backend gets: they
// never accumulate in a script's state)
void UpdateGestures(int chan, u32 actions, f32 wheel) {
    Gesture& g = sGestures[chan];
    const f32 dt = FRAME_SECONDS;
    // Twist
    if (actions & ACT_TWIST_RESET) {
        g.twist = 0.0f;
    }
    if (actions & ACT_TWIST_LEFT) {
        g.twist -= sConfig.twistSpeed * dt;
    }
    if (actions & ACT_TWIST_RIGHT) {
        g.twist += sConfig.twistSpeed * dt;
    }
    g.twist = Clamp(g.twist + wheel * sConfig.wheelTwist, -90.0f, 90.0f);
    // Tilt: springs to +-1 while held; the stick adds its deflection
    const f32 step = sConfig.tiltAngle > 0.0f ? sConfig.tiltSpeed * dt / sConfig.tiltAngle : 1.0f;
    g.fwd = Approach(g.fwd, ((actions & ACT_TILT_FORWARD) ? 1.0f : 0.0f) - ((actions & ACT_TILT_BACK) ? 1.0f : 0.0f), step);
    g.side = Approach(g.side, ((actions & ACT_TILT_RIGHT) ? 1.0f : 0.0f) - ((actions & ACT_TILT_LEFT) ? 1.0f : 0.0f), step);
    g.fwdDeg = Clamp(g.fwd + g.stickY, -1.0f, 1.0f) * sConfig.tiltAngle;
    g.sideDeg = Clamp(g.side + g.stickX, -1.0f, 1.0f) * sConfig.tiltAngle;
    g.stickX = g.stickY = 0.0f;
    // Shake: one pulse per press
    const bool shake = (actions & ACT_SHAKE) != 0;
    if (g.shakeLeft > 0) {
        g.shakeLeft--;
    }
    if (shake && !g.shakeHeld) {
        g.shakeLeft = sConfig.shakeFrames;
    }
    g.shakeHeld = shake;
    // Pull back (held), thrust forward (released): a cue stroke; push: closer while held
    const bool pull = (actions & ACT_PULL) != 0;
    switch (g.pull) {
    case 0:
    case 3:
        if (pull) {
            g.pull = 1;
            break;
        }
        g.dist = Approach(g.dist, (actions & ACT_PUSH) ? -sConfig.pushDistance : 0.0f, 0.6f * dt);
        if (g.pull == 3 && g.dist == 0.0f) {
            g.pull = 0;
        }
        break;
    case 1:
        g.dist = Approach(g.dist, sConfig.pullDistance, 0.6f * dt);
        if (!pull) {
            g.thrustFrac = sConfig.pullDistance > 0.0f ? Clamp(g.dist / sConfig.pullDistance, 0.0f, 1.0f) : 1.0f;
            g.thrustSpeed = 1.5f + 2.5f * g.thrustFrac;
            g.pull = 2;
        }
        break;
    case 2:
        g.dist -= g.thrustSpeed * dt;
        if (g.dist <= -0.1f) {
            g.pull = 3;
        }
        break;
    }
}

void ApplyPad(int chan, PortInputState* st, f32 dt, u32* actions) {
    const PadState& p = sPads[chan];
    u32 b = 0;
    for (int i = 0; i < PAD_BUTTONS; i++) {
        if (p.buttons[i]) {
            b |= sPadBinds[i];
        }
    }
    if (p.axes[AXIS_LEFT_TRIGGER] > 0.5f) {
        b |= sPadBinds[PAD_BUTTONS + 0];
    }
    if (p.axes[AXIS_RIGHT_TRIGGER] > 0.5f) {
        b |= sPadBinds[PAD_BUTTONS + 1];
    }
    f32 lx = Deadzone(p.axes[AXIS_LEFTX]);
    f32 ly = Deadzone(p.axes[AXIS_LEFTY]);
    if (st->nunchuk) {
        st->stickX = lx;
        st->stickY = -ly;
    } else if (sConfig.leftStick == 2 || (sConfig.leftStick == 0 && (Hold(chan) & PORT_HOLD_TILT_STICK))) {
        // A game that reads tilt or twist: the left stick tilts the remote (§37)
        sGestures[chan].stickX = lx;
        sGestures[chan].stickY = -ly;
    } else {
        // Without a Nunchuk the left stick is a second D-pad
        if (lx < -0.5f) b |= WPAD_BUTTON_LEFT;
        if (lx > 0.5f) b |= WPAD_BUTTON_RIGHT;
        if (ly < -0.5f) b |= WPAD_BUTTON_UP;
        if (ly > 0.5f) b |= WPAD_BUTTON_DOWN;
    }
    st->buttons |= static_cast<u16>(b);
    *actions |= b & 0xFFFF0000u;

    // Pointer: the right stick moves it; the gyro too
    f32 rx = Deadzone(p.axes[AXIS_RIGHTX]);
    f32 ry = Deadzone(p.axes[AXIS_RIGHTY]);
    sPointerX[chan] += rx * sConfig.pointerSpeed * 2.0f * dt;
    sPointerY[chan] += ry * sConfig.pointerSpeed * 2.0f * dt;
    float gyro[3] = {0, 0, 0}, accel[3] = {0, 0, 0};
    int have = wpgx_gamepad_sensors(chan, gyro, accel);
    if ((have & 1) && sConfig.gyroSensitivity > 0.0f) {
        // SDL: x pitch (nose up > 0), y yaw (left > 0), rad/s
        sPointerX[chan] -= gyro[1] * sConfig.gyroSensitivity * 2.0f * dt;
        sPointerY[chan] -= gyro[0] * sConfig.gyroSensitivity * 2.0f * dt;
    }
    sPointerX[chan] = Clamp(sPointerX[chan], -1.2f, 1.2f);
    sPointerY[chan] = Clamp(sPointerY[chan], -1.2f, 1.2f);
    if ((have & 2) && sConfig.tilt) {
        // Gravity in SDL's axes (+Y up out of a pad lying flat, +Z toward the
        // player): flat = the remote pointing at the screen.
        f32 g = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
        if (g > 1.0f) {
            st->roll = static_cast<f32>(std::atan2(-accel[0], accel[1]) * 180.0 / M_PI);
            st->pitch = static_cast<f32>(std::atan2(-accel[2], accel[1]) * 180.0 / M_PI);
        }
    }
}

// --- Synthetic events (tests) ------------------------------------------------------

enum EvOp { EV_KEY, EV_MOUSE, EV_CLICK, EV_WHEEL, EV_WINDOW, EV_PAD_ATTACH, EV_PAD_BUTTON, EV_PAD_AXIS, EV_PAD_ACCEL, EV_PAD_GYRO, EV_QUIT };
struct TestEvent {
    u32 retrace;
    EvOp op;
    int code, down;
    f32 v[3];
    char name[32];
};
TestEvent* sEvents = nullptr;
int sEventCount = 0;
int sEventNext = 0;

} // namespace

extern "C" {

int PortLiveLoadConfig(const char* path, char* err, int errLen) {
    DefaultBindings();
    if (path == NULL) {
        return 0;
    }
    FILE* f = std::fopen(path, "r");
    if (f == NULL) {
        std::snprintf(err, errLen, "cannot open %s", path);
        return -1;
    }
    char line[256];
    int lineNo = 0;
    while (std::fgets(line, sizeof line, f) != NULL) {
        lineNo++;
        char* hash = std::strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        line[std::strcspn(line, "\r\n")] = '\0';
        char* w[3];
        int n = Split(line, w, 3);
        if (n == 0) {
            continue;
        }
        auto bad = [&](const char* what) {
            std::snprintf(err, errLen, "%s:%d: %s", path, lineNo, what);
            std::fclose(f);
            return -1;
        };
        if (n < 0) {
            return bad("unterminated quote");
        }
        if (NameEq(w[0], "clear")) {
            sKeyBindCount = 0;
            std::memset(sPadBinds, 0, sizeof sPadBinds);
        } else if (NameEq(w[0], "key") || NameEq(w[0], "pad")) {
            // key "Left Shift" B   |   pad south A   (button "none" unbinds)
            if (n != 3) {
                return bad("expected: key|pad NAME BUTTON");
            }
            u32 button = 0;
            if (!BindFromName(w[2], &button)) {
                return bad("unknown remote button or motion (A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C, "
                           "TWIST_LEFT TWIST_RIGHT TWIST_RESET TILT_FORWARD TILT_BACK TILT_LEFT TILT_RIGHT SHAKE "
                           "PULL PUSH, none)");
            }
            if (NameEq(w[0], "key")) {
                AddKeyBind(w[1], button);
            } else {
                int idx = PadIndex(w[1]);
                if (idx < 0) {
                    return bad("unknown gamepad button");
                }
                sPadBinds[idx] = button;
            }
        } else if (n == 2) {
            f32 v = static_cast<f32>(std::atof(w[1]));
            if (NameEq(w[0], "pointer_speed")) {
                sConfig.pointerSpeed = v;
            } else if (NameEq(w[0], "gyro_sensitivity")) {
                sConfig.gyroSensitivity = v;
            } else if (NameEq(w[0], "tilt")) {
                sConfig.tilt = v != 0;
            } else if (NameEq(w[0], "rumble")) {
                sConfig.rumble = v != 0;
            } else if (NameEq(w[0], "volume")) {
                sConfig.volume = Clamp(v, 0.0f, 1.0f);
            } else if (NameEq(w[0], "mute_unfocused")) {
                sConfig.muteUnfocused = v != 0;
            } else if (NameEq(w[0], "nunchuk")) {
                sConfig.nunchuk = v != 0;
            } else if (NameEq(w[0], "stick_deadzone")) {
                sConfig.stickDeadzone = Clamp(v, 0.0f, 0.9f);
            } else if (NameEq(w[0], "pointer_stabilise")) {
                sConfig.pointerStabilise = v != 0;
            } else if (NameEq(w[0], "speaker_volume")) {
                sConfig.speakerVolume = Clamp(v, 0.0f, 2.0f);
            } else if (NameEq(w[0], "left_stick")) {
                // auto: tilt in the games that read tilt or twist, else the D-pad
                sConfig.leftStick = NameEq(w[1], "auto") ? 0 : NameEq(w[1], "dpad") ? 1 : NameEq(w[1], "tilt") ? 2 : -1;
                if (sConfig.leftStick < 0) {
                    return bad("left_stick: auto, dpad or tilt");
                }
            } else if (NameEq(w[0], "twist_speed")) {
                sConfig.twistSpeed = Clamp(v, 0.0f, 1000.0f);
            } else if (NameEq(w[0], "wheel_twist")) {
                sConfig.wheelTwist = Clamp(v, -90.0f, 90.0f);
            } else if (NameEq(w[0], "tilt_angle")) {
                sConfig.tiltAngle = Clamp(v, 0.0f, 90.0f);
            } else if (NameEq(w[0], "tilt_speed")) {
                sConfig.tiltSpeed = Clamp(v, 1.0f, 10000.0f);
            } else if (NameEq(w[0], "shake_strength")) {
                sConfig.shakeStrength = Clamp(v, 0.0f, 3.4f);
            } else if (NameEq(w[0], "shake_frames")) {
                sConfig.shakeFrames = static_cast<int>(Clamp(v, 1.0f, 30.0f));
            } else if (NameEq(w[0], "pull_distance")) {
                sConfig.pullDistance = Clamp(v, 0.0f, 1.0f);
            } else if (NameEq(w[0], "push_distance")) {
                sConfig.pushDistance = Clamp(v, 0.0f, 1.0f);
            } else {
                return bad("unknown setting");
            }
        } else {
            return bad("expected: SETTING VALUE, key NAME BUTTON, pad NAME BUTTON or clear");
        }
    }
    std::fclose(f);
    return 0;
}

const PortLiveConfig* PortLiveGetConfig(void) {
    return &sConfig;
}

void PortLiveStartPacing(f32 speed) {
    sPacing = true;
    sSpeed = speed > 0.0f ? speed : 1.0f;
    PortVISetPacer(Pace, NULL);
}

void SpeakerPacket(s32 chan, const void* data, u16 len, void*) {
    PortSpeakerPacket(chan, data, len);
}

void SpeakerControl(s32 chan, u32 command, void*) {
    if (command == WPAD_SPEAKER_ON || command == WPAD_SPEAKER_OFF) {
        PortSpeakerReset(chan);
    }
}

int PortLiveStartAudio(void) {
    if (wpgx_audio_open(AUDIO_RATE, 2) != 0) {
        return -1;
    }
    // The remote speakers' sound (§37) into the same output
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        PortSpeakerReset(chan);
    }
    PortWPADSetSpeakerCallback(SpeakerPacket, NULL);
    PortWPADSetSpeakerControlCallback(SpeakerControl, NULL);
    wpgx_audio_set_gain(sConfig.volume);
    sAudio = true;
    int rate = 0, channels = 0, buffer = 0;
    if (wpgx_audio_device_format(&rate, &channels, &buffer) == 0) {
        std::fprintf(stderr,
                     "audio: %s, device %d Hz %d ch, buffer %d frames (%.1f ms); stream s16 stereo %u Hz "
                     "(SDL resamples), queue target %.0f ms, pace correction <= %.1f %%\n",
                     wpgx_audio_driver(), rate, channels, buffer, rate > 0 ? 1000.0 * buffer / rate : 0.0,
                     AUDIO_RATE, 1000.0 * AUDIO_TARGET / AUDIO_RATE, 100.0 * AUDIO_MAX_CORRECTION);
    }
    return 0;
}

void PortLiveAudioFrame(const short* samples, u32 frames) {
    if (!sAudio) {
        return;
    }
    static short silence[AUDIO_PREFILL * 2];
    if (!sAudioPrimed) {
        wpgx_audio_queue(silence, AUDIO_PREFILL);
        sAudioPrimed = true;
    }
    u32 queued = wpgx_audio_queued_frames();
    if (queued == 0) {
        // The device ran dry (the game fell behind): it played silence; queue
        // the cushion again so that one late frame is not followed by more.
        sStats.underruns++;
        wpgx_audio_queue(silence, AUDIO_PREFILL);
        queued = AUDIO_PREFILL;
    } else if (queued > AUDIO_LIMIT) {
        // The device stalled: drop the backlog rather than play late.
        wpgx_audio_clear();
        sStats.overruns++;
        queued = 0;
    }
    // The remote speakers: the speaker volume the game set (HOME Menu settings, SC) times
    // speaker_volume (0 mutes)
    static short mixed[2 * 256];
    const u32 n = frames <= 256 ? frames : 256;
    std::memcpy(mixed, samples, n * 2 * sizeof(short));
    PortSpeakerMix(mixed, n, sConfig.speakerVolume * static_cast<f32>(WPADGetSpeakerVolume()) / 127.0f);
    if (queued > AUDIO_HIGH) {
        // Ahead of a device slower than the correction can follow: skip 3 ms.
        sStats.skippedFrames++;
    } else {
        wpgx_audio_queue(mixed, n);
    }
    if (sPacing && sSpeed == 1.0f) {
        SteerByAudio(queued);
    }
}

// KPAD's pointer stabilisation (HOST_BUILD.md §35): the game sets a play
// radius (0.06 of the half-width, sensitivity 1) every frame, under which KPAD
// moves the pointer by (distance / radius)^4 of the way per sample: a real
// remote's jitter is held still, but a mouse's small moves lag or stop short.
// With live input (and pointer_stabilise 0, the default) remote 0, which the
// mouse drives, gets radius 0: KPAD's position is the pointer's. The boot
// executable is linked with --wrap=KPADSetPosParam.
void __real_KPADSetPosParam(s32 chan, f32 playRadius, f32 sensitivity);
void __wrap_KPADSetPosParam(s32 chan, f32 playRadius, f32 sensitivity) {
    if (sInput && chan == 0 && !sConfig.pointerStabilise) {
        playRadius = 0.0f;
        sensitivity = 1.0f;
    }
    __real_KPADSetPosParam(chan, playRadius, sensitivity);
}

void PortLiveApplyMotion(int chan, PortInputState* st) {
    if (!sInput || chan < 0 || chan >= WPAD_MAX_CONTROLLERS) {
        return;
    }
    const Gesture& g = sGestures[chan];
    if (Hold(chan) & PORT_HOLD_SIDEWAYS) {
        // Held sideways (Charge!): tipping it forward is the remote's roll; tilting it to the
        // left raises its pitch (the game turns left)
        st->roll += g.fwdDeg + g.twist;
        st->pitch -= g.sideDeg;
    } else {
        // Pointing at the screen: forward is nose down; left / right is the roll
        st->pitch -= g.fwdDeg;
        st->roll += g.sideDeg + g.twist;
    }
    if (g.shakeLeft > 0) {
        // along the remote's up: what lifting it sharply reads
        const f32 r = st->roll * 0.017453292f, p = st->pitch * 0.017453292f;
        const f32 k = sConfig.shakeStrength;
        st->accel[0] += k * -std::cos(p) * std::sin(r);
        st->accel[1] += k * -std::cos(p) * std::cos(r);
        st->accel[2] += k * std::sin(p);
    }
    if (g.pull == 1) {
        st->accel[2] += 0.2f; // drawing the remote back
    } else if (g.pull == 2) {
        st->accel[2] -= 1.0f + 2.5f * g.thrustFrac; // the thrust toward the screen
    }
    st->distance += g.dist;
}

void PortLiveSetMerge(int merge) {
    sMerge = merge != 0;
}

void PortLiveTestKey(const char* name, int down) {
    wpgx_test_push_key(name, down);
}

int PortLiveStartInput(void) {
    if (wpgx_input_init() != 0) {
        return -1;
    }
    for (int i = 0; i < WPAD_MAX_CONTROLLERS; i++) {
        sPointerX[i] = 0.0f;
        sPointerY[i] = 0.0f;
    }
    PortWPADSetMotorCallback(Motor, NULL);
    wpgx_show_cursor(0);
    sInput = true;
    NoteWindow("window at start");
    return 0;
}

int PortLivePoll(PortInputState states[WPAD_MAX_CONTROLLERS]) {
    Clock::time_point now = Clock::now();
    f32 dt = sPolled ? std::chrono::duration<f32>(now - sLastPoll).count() : 0.0f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    sPolled = true;
    sLastPoll = now;

    u64 pollStart = PortPerfNow();
    sPollCount++;
    ReleaseDeferred();
    sWheelY = 0.0f;
    wpgx_pump_events();
    int quit = 0;
    wpgx_event e;
    while (wpgx_next_event(&e)) {
        sStats.events++;
        if (PortRecordActive() || e.type == WPGX_EV_KEY || e.type == WPGX_EV_MOUSE_BUTTON ||
            e.type == WPGX_EV_GAMEPAD_BUTTON) {
            char what[96];
            switch (e.type) {
            case WPGX_EV_KEY:
                std::snprintf(what, sizeof what, "key %s %s", e.name, e.down ? "down" : "up");
                break;
            case WPGX_EV_MOUSE_BUTTON:
                std::snprintf(what, sizeof what, "click %d %s", e.code, e.down ? "down" : "up");
                break;
            case WPGX_EV_GAMEPAD_BUTTON:
                std::snprintf(what, sizeof what, "pad %d button %d %s", e.pad, e.code, e.down ? "down" : "up");
                break;
            case WPGX_EV_MOUSE_MOTION:
                std::snprintf(what, sizeof what, "mouse %.1f %.1f", e.x, e.y);
                break;
            case WPGX_EV_GAMEPAD_AXIS:
                std::snprintf(what, sizeof what, "pad %d axis %d %.3f", e.pad, e.code, e.x);
                break;
            default:
                std::snprintf(what, sizeof what, "event %u", e.type);
                break;
            }
            PortRecordInput(what, (e.type == WPGX_EV_KEY || e.type == WPGX_EV_MOUSE_BUTTON ||
                                   e.type == WPGX_EV_GAMEPAD_BUTTON) && e.down);
        }
        switch (e.type) {
        case WPGX_EV_QUIT:
            quit = 1;
            break;
        case WPGX_EV_KEY:
            if (e.down && NameEq(e.name, "F3")) {
                PortPerfToggleOverlay();
                break;
            }
            if (e.down && (NameEq(e.name, "F11") || (NameEq(e.name, "Return") && sHeldCount > 0 &&
                                                     (NameEq(sHeld[0].name, "Left Alt") || NameEq(sHeld[0].name, "Right Alt"))))) {
                wpgx_set_fullscreen(!wpgx_is_fullscreen());
                break;
            }
            SetHeld(e.name, e.down != 0);
            break;
        case WPGX_EV_MOUSE_MOTION:
            sMouseWinX = e.x;
            sMouseWinY = e.y;
            sMouseSeen = true;
            MapMouse();
            break;
        case WPGX_EV_MOUSE_WHEEL:
            sWheelY += e.y;
            break;
        case WPGX_EV_RESIZED:
            if (NoteWindow("window changed") && sMouseSeen) {
                MapMouse();
            }
            break;
        case WPGX_EV_MOUSE_BUTTON: {
            char name[16];
            std::snprintf(name, sizeof name, "Mouse%d", e.code);
            SetHeld(name, e.down != 0);
            break;
        }
        case WPGX_EV_GAMEPAD_ADDED:
            if (e.pad >= 0 && e.pad < WPAD_MAX_CONTROLLERS) {
                sPads[e.pad] = PadState{};
                sPads[e.pad].present = true;
                std::fprintf(stderr, "boot: gamepad %d: %s (gyro %s, accelerometer %s)\n", e.pad, e.name,
                             (e.code & 1) ? "yes" : "no", (e.code & 2) ? "yes" : "no");
            }
            break;
        case WPGX_EV_GAMEPAD_REMOVED:
            if (e.pad >= 0 && e.pad < WPAD_MAX_CONTROLLERS) {
                sPads[e.pad] = PadState{};
            }
            break;
        case WPGX_EV_GAMEPAD_BUTTON:
            if (e.pad >= 0 && e.pad < WPAD_MAX_CONTROLLERS && e.code >= 0 && e.code < PAD_BUTTONS) {
                PadState& p = sPads[e.pad];
                if (e.down) {
                    p.buttons[e.code] = true;
                    p.pressPoll[e.code] = sPollCount;
                    p.releaseNext[e.code] = false;
                } else if (p.buttons[e.code] && p.pressPoll[e.code] == sPollCount) {
                    p.releaseNext[e.code] = true; // seen for one frame first
                } else {
                    p.buttons[e.code] = false;
                }
            }
            break;
        case WPGX_EV_GAMEPAD_AXIS:
            if (e.pad >= 0 && e.pad < WPAD_MAX_CONTROLLERS && e.code >= 0 && e.code < AXIS_COUNT) {
                sPads[e.pad].axes[e.code] = e.x;
            }
            break;
        case WPGX_EV_FOCUS:
            sFocused = e.down != 0;
            if (sAudio && sConfig.muteUnfocused) {
                wpgx_audio_set_gain(sFocused ? sConfig.volume : 0.0f);
            }
            if (!sFocused) {
                sHeldCount = 0; // no stuck keys after alt-tab
            }
            break;
        default:
            break;
        }
    }

    // The first frames present the picture (Aurora's viewport) and may resize;
    // then a change shows as an event. A cheap check every 2 s catches the rest.
    if (sPollCount - sWindowInfoPoll >= (sPollCount < 300 ? 30u : 120u)) {
        sWindowInfoPoll = sPollCount;
        if (NoteWindow("window") && sMouseSeen) {
            MapMouse();
        }
    }

    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        PortInputState* st = &states[chan];
        u32 keys = chan == 0 ? KeyButtons() : 0;
        u32 actions = keys & 0xFFFF0000u;
        if (sMerge) {
            // Over a test script's remote (§37): add the buttons and motions only
            st->buttons |= static_cast<u16>(keys);
            if (sPads[chan].present) {
                ApplyPad(chan, st, dt, &actions);
            }
            if (chan == 0 && sMouseSeen) {
                st->pointerValid = 1;
                st->pointerX = sPointerX[0];
                st->pointerY = sPointerY[0];
            }
            UpdateGestures(chan, actions, chan == 0 ? sWheelY : 0.0f);
            continue;
        }
        bool wanted = chan == 0 || sPads[chan].present;
        st->connected = wanted ? 1 : 0;
        st->nunchuk = sConfig.nunchuk;
        st->buttons = static_cast<u16>(keys);
        // The motions start from a level remote at the default distance each poll
        st->roll = st->pitch = 0.0f;
        st->accel[0] = st->accel[1] = st->accel[2] = 0.0f;
        st->distance = 2.0f;
        if (sPads[chan].present) {
            ApplyPad(chan, st, dt, &actions);
        }
        UpdateGestures(chan, actions, chan == 0 ? sWheelY : 0.0f);
        st->pointerValid = chan != 0 || sMouseSeen || sPads[0].present ? 1 : 0;
        if (Hold(chan) & PORT_HOLD_SIDEWAYS) {
            // Held on its side (Charge!) the remote cannot see the sensor bar: the game
            // waits for the pointer to leave the screen before the race starts
            st->pointerValid = 0;
        }
        st->pointerX = sPointerX[chan];
        st->pointerY = sPointerY[chan];
        if (sMotor[chan] && sConfig.rumble && sPads[chan].present) {
            wpgx_gamepad_rumble(chan, 0.5f, 0.5f, 2000);
        }
    }
    PortPerfAdd(PORT_PERF_INPUT, PortPerfNow() - pollStart);
    return quit;
}

int PortLiveLoadEvents(const char* path, char* err, int errLen) {
    FILE* f = std::fopen(path, "r");
    if (f == NULL) {
        std::snprintf(err, errLen, "cannot open %s", path);
        return -1;
    }
    int cap = 64;
    sEvents = static_cast<TestEvent*>(std::malloc(cap * sizeof(TestEvent)));
    char line[256];
    int lineNo = 0;
    while (std::fgets(line, sizeof line, f) != NULL) {
        lineNo++;
        char* hash = std::strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        line[std::strcspn(line, "\r\n")] = '\0';
        char* w[6];
        int n = Split(line, w, 6);
        if (n == 0) {
            continue;
        }
        if (sEventCount == cap) {
            cap *= 2;
            sEvents = static_cast<TestEvent*>(std::realloc(sEvents, cap * sizeof(TestEvent)));
        }
        TestEvent& t = sEvents[sEventCount];
        std::memset(&t, 0, sizeof t);
        t.retrace = static_cast<u32>(std::atoi(w[0]));
        bool ok = n >= 2;
        auto isDown = [](const char* s) { return NameEq(s, "down") ? 1 : 0; };
        if (ok && NameEq(w[1], "key") && n == 4) {
            t.op = EV_KEY;
            std::snprintf(t.name, sizeof t.name, "%s", w[2]);
            t.down = isDown(w[3]);
        } else if (ok && NameEq(w[1], "mouse") && n == 4) {
            t.op = EV_MOUSE;
            t.v[0] = static_cast<f32>(std::atof(w[2]));
            t.v[1] = static_cast<f32>(std::atof(w[3]));
        } else if (ok && NameEq(w[1], "click") && n == 4) {
            t.op = EV_CLICK;
            t.code = std::atoi(w[2]);
            t.down = isDown(w[3]);
        } else if (ok && NameEq(w[1], "pad") && n >= 3 && NameEq(w[2], "attach")) {
            t.op = EV_PAD_ATTACH;
        } else if (ok && NameEq(w[1], "pad") && n == 5 && NameEq(w[2], "button")) {
            t.op = EV_PAD_BUTTON;
            t.code = PadIndex(w[3]);
            t.down = isDown(w[4]);
            ok = t.code >= 0 && t.code < PAD_BUTTONS;
        } else if (ok && NameEq(w[1], "pad") && n == 5 && NameEq(w[2], "axis")) {
            t.op = EV_PAD_AXIS;
            t.code = std::atoi(w[3]);
            t.v[0] = static_cast<f32>(std::atof(w[4]));
        } else if (ok && NameEq(w[1], "pad") && n == 6 && (NameEq(w[2], "accel") || NameEq(w[2], "gyro"))) {
            // pad accel|gyro X Y Z
            t.op = NameEq(w[2], "accel") ? EV_PAD_ACCEL : EV_PAD_GYRO;
            t.v[0] = static_cast<f32>(std::atof(w[3]));
            t.v[1] = static_cast<f32>(std::atof(w[4]));
            t.v[2] = static_cast<f32>(std::atof(w[5]));
        } else if (ok && NameEq(w[1], "wheel") && n == 3) {
            // wheel STEPS (> 0 away from the user)
            t.op = EV_WHEEL;
            t.v[0] = static_cast<f32>(std::atof(w[2]));
        } else if (ok && NameEq(w[1], "window") && n == 6) {
            // window LW LH PW PH: a fake window geometry (points, pixels); 0 0 0 0 = the real one
            t.op = EV_WINDOW;
            t.v[0] = static_cast<f32>(std::atof(w[2]));
            t.v[1] = static_cast<f32>(std::atof(w[3]));
            t.v[2] = static_cast<f32>(std::atof(w[4]));
            t.code = std::atoi(w[5]);
        } else if (ok && NameEq(w[1], "quit")) {
            t.op = EV_QUIT;
        } else {
            ok = false;
        }
        if (!ok) {
            std::snprintf(err, errLen, "%s:%d: bad event", path, lineNo);
            std::fclose(f);
            return -1;
        }
        sEventCount++;
    }
    std::fclose(f);
    return 0;
}

void PortLivePushEvents(u32 retrace) {
    while (sEventNext < sEventCount && sEvents[sEventNext].retrace <= retrace) {
        const TestEvent& t = sEvents[sEventNext++];
        switch (t.op) {
        case EV_KEY:
            wpgx_test_push_key(t.name, t.down);
            break;
        case EV_MOUSE:
            wpgx_test_push_mouse_motion(t.v[0], t.v[1]);
            break;
        case EV_CLICK:
            wpgx_test_push_mouse_button(t.code, t.down);
            break;
        case EV_PAD_ATTACH:
            if (wpgx_test_gamepad_attach() != 0) {
                std::fprintf(stderr, "boot: cannot attach the virtual gamepad\n");
            }
            break;
        case EV_PAD_BUTTON:
            wpgx_test_gamepad_button(t.code, t.down);
            break;
        case EV_PAD_AXIS:
            wpgx_test_gamepad_axis(t.code, t.v[0]);
            break;
        case EV_PAD_ACCEL:
        case EV_PAD_GYRO:
            wpgx_test_gamepad_sensor(t.op == EV_PAD_ACCEL, t.v[0], t.v[1], t.v[2]);
            break;
        case EV_WHEEL:
            wpgx_test_push_mouse_wheel(0.0f, t.v[0]);
            break;
        case EV_WINDOW:
            wpgx_test_set_window_metrics(t.v[0], t.v[1], t.v[2], static_cast<f32>(t.code));
            break;
        case EV_QUIT:
            wpgx_test_push_quit();
            break;
        }
    }
}

void PortLiveGetStats(PortLiveStats* stats) {
    *stats = sStats;
    stats->wallSeconds = sStarted ? std::chrono::duration<double>(Clock::now() - sStart).count() : 0.0;
    stats->queuedFrames = sAudio ? wpgx_audio_queued_frames() : 0;
    stats->testRumbles = wpgx_test_gamepad_rumbles(NULL, NULL);
}

void PortLiveShutdown(void) {
    if (sAudio) {
        wpgx_audio_close();
        sAudio = false;
    }
    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        if (sPads[chan].present) {
            wpgx_gamepad_rumble(chan, 0, 0, 0);
        }
    }
}

} // extern "C"
