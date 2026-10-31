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
struct KeyBind {
    char name[32]; // SDL key name or MouseN
    u16 button;
};
KeyBind sKeyBinds[MAX_KEY_BINDS];
int sKeyBindCount = 0;
u16 sPadBinds[PAD_BUTTONS + 2]; // buttons, then the two triggers

PortLiveConfig sConfig = {1.5f, 2.0f, 1, 1, 1.0f, 0, 0, 0.2f};

bool NameEq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

void AddKeyBind(const char* name, u16 button) {
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
    sPadBinds[PAD_LEFT_SHOULDER] = WPAD_BUTTON_FS_C;
    sPadBinds[PAD_RIGHT_SHOULDER] = WPAD_BUTTON_B;
    sPadBinds[PAD_BUTTONS + 0] = WPAD_BUTTON_FS_Z; // left trigger
    sPadBinds[PAD_BUTTONS + 1] = WPAD_BUTTON_B;    // right trigger
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
const u32 AUDIO_PREFILL = 1600; // silence queued when the stream starts
const u32 AUDIO_LIMIT = 9600;   // above this (300 ms) the queue is dropped

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

// The device paces the game: each AX frame nudges the anchor by a fraction of
// the queue's distance from its target (a proportional controller, time
// constant about a third of a second).
void SteerByAudio() {
    u32 queued = wpgx_audio_queued_frames();
    double error = (static_cast<double>(queued) - AUDIO_TARGET) / AUDIO_RATE; // seconds ahead
    sAnchor += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(error * 0.01));
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
        std::this_thread::sleep_until(target);
    }
}

// --- Input -----------------------------------------------------------------------

struct PadState {
    bool present = false;
    bool buttons[PAD_BUTTONS] = {};
    f32 axes[AXIS_COUNT] = {};
};
PadState sPads[WPAD_MAX_CONTROLLERS];
bool sInput = false;
u16 sKeyButtons = 0;   // keyboard + mouse, remote 0
f32 sMouseX = 0.0f, sMouseY = 0.0f;
bool sMouseSeen = false;
f32 sPointerX[WPAD_MAX_CONTROLLERS], sPointerY[WPAD_MAX_CONTROLLERS];
Clock::time_point sLastPoll;
bool sPolled = false;
bool sMotor[WPAD_MAX_CONTROLLERS];

// Keys currently held, so a binding change or two keys on one button work
const int MAX_HELD = 32;
char sHeld[MAX_HELD][32];
int sHeldCount = 0;

u16 KeyButtons() {
    u16 b = 0;
    for (int i = 0; i < sHeldCount; i++) {
        for (int k = 0; k < sKeyBindCount; k++) {
            if (NameEq(sKeyBinds[k].name, sHeld[i])) {
                b |= sKeyBinds[k].button;
            }
        }
    }
    return b;
}

void SetHeld(const char* name, bool down) {
    for (int i = 0; i < sHeldCount; i++) {
        if (NameEq(sHeld[i], name)) {
            if (!down) {
                std::memmove(sHeld[i], sHeld[i + 1], (sHeldCount - i - 1) * sizeof sHeld[0]);
                sHeldCount--;
            }
            return;
        }
    }
    if (down && sHeldCount < MAX_HELD) {
        std::snprintf(sHeld[sHeldCount++], sizeof sHeld[0], "%s", name);
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

void ApplyPad(int chan, PortInputState* st, f32 dt) {
    const PadState& p = sPads[chan];
    u16 b = 0;
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
    } else {
        // Without a Nunchuk the left stick is a second D-pad
        if (lx < -0.5f) b |= WPAD_BUTTON_LEFT;
        if (lx > 0.5f) b |= WPAD_BUTTON_RIGHT;
        if (ly < -0.5f) b |= WPAD_BUTTON_UP;
        if (ly > 0.5f) b |= WPAD_BUTTON_DOWN;
    }
    st->buttons |= b;

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

enum EvOp { EV_KEY, EV_MOUSE, EV_CLICK, EV_PAD_ATTACH, EV_PAD_BUTTON, EV_PAD_AXIS, EV_PAD_ACCEL, EV_PAD_GYRO, EV_QUIT };
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
            u16 button = NameEq(w[2], "none") ? 0 : PortInputButtonFromName(w[2]);
            if (button == 0 && !NameEq(w[2], "none")) {
                return bad("unknown remote button (A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C none)");
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

int PortLiveStartAudio(void) {
    if (wpgx_audio_open(AUDIO_RATE, 2) != 0) {
        return -1;
    }
    wpgx_audio_set_gain(sConfig.volume);
    sAudio = true;
    return 0;
}

void PortLiveAudioFrame(const short* samples, u32 frames) {
    if (!sAudio) {
        return;
    }
    if (!sAudioPrimed) {
        static short silence[AUDIO_PREFILL * 2];
        wpgx_audio_queue(silence, AUDIO_PREFILL);
        sAudioPrimed = true;
    }
    u32 queued = wpgx_audio_queued_frames();
    if (queued == 0) {
        sStats.underruns++;
    } else if (queued > AUDIO_LIMIT) {
        // The game ran ahead of the device (a stall in the device or pacing
        // off): drop the backlog rather than play late.
        wpgx_audio_clear();
        sStats.overruns++;
    }
    wpgx_audio_queue(samples, frames);
    if (sPacing && sSpeed == 1.0f) {
        SteerByAudio();
    }
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

    wpgx_pump_events();
    int quit = 0;
    wpgx_event e;
    while (wpgx_next_event(&e)) {
        sStats.events++;
        switch (e.type) {
        case WPGX_EV_QUIT:
            quit = 1;
            break;
        case WPGX_EV_KEY:
            if (e.down && (NameEq(e.name, "F11") || (NameEq(e.name, "Return") && sHeldCount > 0 &&
                                                     (NameEq(sHeld[0], "Left Alt") || NameEq(sHeld[0], "Right Alt"))))) {
                wpgx_set_fullscreen(!wpgx_is_fullscreen());
                break;
            }
            SetHeld(e.name, e.down != 0);
            break;
        case WPGX_EV_MOUSE_MOTION: {
            f32 u, v;
            wpgx_window_to_picture(e.x, e.y, &u, &v);
            PortInputPointerFromWindow(u, v, 1.0f, 1.0f, &sMouseX, &sMouseY);
            sPointerX[0] = sMouseX;
            sPointerY[0] = sMouseY;
            sMouseSeen = true;
            break;
        }
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
                sPads[e.pad].buttons[e.code] = e.down != 0;
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

    for (int chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        PortInputState* st = &states[chan];
        bool wanted = chan == 0 || sPads[chan].present;
        st->connected = wanted ? 1 : 0;
        st->nunchuk = sConfig.nunchuk;
        st->buttons = chan == 0 ? KeyButtons() : 0;
        if (sPads[chan].present) {
            ApplyPad(chan, st, dt);
        }
        st->pointerValid = chan != 0 || sMouseSeen || sPads[0].present ? 1 : 0;
        st->pointerX = sPointerX[chan];
        st->pointerY = sPointerY[chan];
        if (sMotor[chan] && sConfig.rumble && sPads[chan].present) {
            wpgx_gamepad_rumble(chan, 0.5f, 0.5f, 2000);
        }
    }
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
