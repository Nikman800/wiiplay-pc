// wpgx: window, input devices and audio output for the live game (wpgx.h,
// port/docs/HOST_BUILD.md §30). SDL3 is linked statically into this library
// and hidden, so these functions are the port's only way to reach it.
#include "wpgx.h"

#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>

#define WPGX_API extern "C" __attribute__((visibility("default")))

namespace {

constexpr int kMaxPads = 4;

bool gHeadless = true;
float gEfbWidth = 640.f;
float gEfbHeight = 456.f;
float gAspect = 0.f;
std::deque<wpgx_event> gEvents;

struct Pad {
    SDL_JoystickID id = 0;
    SDL_Gamepad* pad = nullptr;
    int sensors = 0;
};
Pad gPads[kMaxPads];

SDL_AudioStream* gAudio = nullptr;
int gAudioChannels = 2;

// Virtual test gamepad
SDL_JoystickID gVirtualId = 0;
SDL_Joystick* gVirtual = nullptr;
uint32_t gRumbles = 0;
float gRumbleLow = 0.f;
float gRumbleHigh = 0.f;

SDL_Window* Window() { return gHeadless ? nullptr : aurora_get_window(); }

int SlotOf(SDL_JoystickID id) {
    for (int i = 0; i < kMaxPads; i++) {
        if (gPads[i].pad != nullptr && gPads[i].id == id) {
            return i;
        }
    }
    return -1;
}

void Push(const wpgx_event& e) { gEvents.push_back(e); }

void AddPad(SDL_JoystickID id) {
    if (SlotOf(id) >= 0) {
        return;
    }
    for (int i = 0; i < kMaxPads; i++) {
        if (gPads[i].pad != nullptr) {
            continue;
        }
        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (pad == nullptr) {
            return;
        }
        gPads[i].id = id;
        gPads[i].pad = pad;
        gPads[i].sensors = 0;
        if (SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true)) {
            gPads[i].sensors |= 1;
        }
        if (SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true)) {
            gPads[i].sensors |= 2;
        }
        wpgx_event e{};
        e.type = WPGX_EV_GAMEPAD_ADDED;
        e.pad = i;
        e.code = gPads[i].sensors;
        const char* name = SDL_GetGamepadName(pad);
        std::snprintf(e.name, sizeof e.name, "%s", name != nullptr ? name : "gamepad");
        Push(e);
        return;
    }
}

void RemovePad(SDL_JoystickID id) {
    const int slot = SlotOf(id);
    if (slot < 0) {
        return;
    }
    SDL_CloseGamepad(gPads[slot].pad);
    gPads[slot] = Pad{};
    wpgx_event e{};
    e.type = WPGX_EV_GAMEPAD_REMOVED;
    e.pad = slot;
    Push(e);
}

void Translate(const SDL_Event& ev) {
    wpgx_event e{};
    switch (ev.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (ev.key.repeat) {
            return;
        }
        e.type = WPGX_EV_KEY;
        e.down = ev.type == SDL_EVENT_KEY_DOWN;
        std::snprintf(e.name, sizeof e.name, "%s", SDL_GetKeyName(ev.key.key));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        e.type = WPGX_EV_MOUSE_MOTION;
        e.x = ev.motion.x;
        e.y = ev.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        e.type = WPGX_EV_MOUSE_BUTTON;
        e.down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.code = ev.button.button;
        e.x = ev.button.x;
        e.y = ev.button.y;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        AddPad(ev.gdevice.which);
        return;
    case SDL_EVENT_GAMEPAD_REMOVED:
        RemovePad(ev.gdevice.which);
        return;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        e.pad = SlotOf(ev.gbutton.which);
        if (e.pad < 0) {
            return;
        }
        e.type = WPGX_EV_GAMEPAD_BUTTON;
        e.down = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.code = ev.gbutton.button;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        e.pad = SlotOf(ev.gaxis.which);
        if (e.pad < 0) {
            return;
        }
        e.type = WPGX_EV_GAMEPAD_AXIS;
        e.code = ev.gaxis.axis;
        e.x = std::clamp(ev.gaxis.value / 32767.f, -1.f, 1.f);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        e.type = WPGX_EV_FOCUS;
        e.down = ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        e.type = WPGX_EV_RESIZED;
        e.x = static_cast<float>(ev.window.data1);
        e.y = static_cast<float>(ev.window.data2);
        break;
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        e.type = WPGX_EV_QUIT;
        break;
    default:
        return;
    }
    Push(e);
}

bool SDLCALL VirtualRumble(void*, Uint16 low, Uint16 high) {
    gRumbles++;
    gRumbleLow = low / 65535.f;
    gRumbleHigh = high / 65535.f;
    return true;
}

} // namespace

void wpgx_platform_init(bool headless, uint32_t width, uint32_t height) {
    gHeadless = headless;
    gEfbWidth = static_cast<float>(width);
    gEfbHeight = static_cast<float>(height);
}

void wpgx_platform_take_events(const AuroraEvent* events) {
    for (const AuroraEvent* ev = events; ev != nullptr && ev->type != AURORA_NONE; ++ev) {
        if (ev->type == AURORA_SDL_EVENT) {
            Translate(ev->sdl);
        } else if (ev->type == AURORA_EXIT) {
            wpgx_event e{};
            e.type = WPGX_EV_QUIT;
            Push(e);
        }
    }
}

void wpgx_platform_shutdown() {
    wpgx_audio_close();
    for (Pad& p : gPads) {
        if (p.pad != nullptr) {
            SDL_CloseGamepad(p.pad);
        }
        p = Pad{};
    }
    if (gVirtual != nullptr) {
        SDL_CloseJoystick(gVirtual);
        SDL_DetachVirtualJoystick(gVirtualId);
        gVirtual = nullptr;
    }
}

// --- Window --------------------------------------------------------------------

WPGX_API void wpgx_set_display_aspect(float aspect) {
    gAspect = aspect > 0.f ? aspect : 0.f;
    aurora_set_present_aspect(gAspect);
}

WPGX_API float wpgx_display_aspect(void) { return gAspect; }

WPGX_API void wpgx_window_size(float* width, float* height) {
    int w = 0;
    int h = 0;
    SDL_Window* window = Window();
    if (window == nullptr || !SDL_GetWindowSize(window, &w, &h) || w <= 0 || h <= 0) {
        *width = gEfbWidth;
        *height = gEfbHeight;
        return;
    }
    *width = static_cast<float>(w);
    *height = static_cast<float>(h);
}

WPGX_API void wpgx_window_to_picture(float x, float y, float* u, float* v) {
    // The letterbox Aurora presents with (webgpu::calculate_present_viewport)
    float w = 0.f;
    float h = 0.f;
    wpgx_window_size(&w, &h);
    const float aspect = gAspect > 0.f ? gAspect : gEfbWidth / gEfbHeight;
    float pw = w;
    float ph = w / aspect;
    if (ph > h) {
        ph = h;
        pw = h * aspect;
    }
    const float left = (w - pw) / 2.f;
    const float top = (h - ph) / 2.f;
    *u = (x - left) / pw;
    *v = (y - top) / ph;
}

WPGX_API void wpgx_set_fullscreen(int fullscreen) {
    if (SDL_Window* window = Window()) {
        SDL_SetWindowFullscreen(window, fullscreen != 0);
    }
}

WPGX_API int wpgx_is_fullscreen(void) {
    SDL_Window* window = Window();
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
}

WPGX_API void wpgx_show_cursor(int show) {
    if (Window() == nullptr) {
        return;
    }
    if (show) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

// --- Input -----------------------------------------------------------------------

WPGX_API int wpgx_input_init(void) {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR | SDL_INIT_HAPTIC)) {
        std::fprintf(stderr, "wpgx: SDL input: %s\n", SDL_GetError());
        return -1;
    }
    // Gamepads present from the start arrive as GAMEPAD_ADDED events.
    return 0;
}

WPGX_API void wpgx_pump_events(void) { wpgx_platform_take_events(aurora_update()); }

WPGX_API int wpgx_next_event(wpgx_event* event) {
    if (gEvents.empty()) {
        return 0;
    }
    *event = gEvents.front();
    gEvents.pop_front();
    return 1;
}

WPGX_API int wpgx_gamepad_sensors(int pad, float gyro[3], float accel[3]) {
    if (pad < 0 || pad >= kMaxPads || gPads[pad].pad == nullptr) {
        return 0;
    }
    int have = 0;
    if ((gPads[pad].sensors & 1) && SDL_GetGamepadSensorData(gPads[pad].pad, SDL_SENSOR_GYRO, gyro, 3)) {
        have |= 1;
    }
    if ((gPads[pad].sensors & 2) && SDL_GetGamepadSensorData(gPads[pad].pad, SDL_SENSOR_ACCEL, accel, 3)) {
        have |= 2;
    }
    return have;
}

WPGX_API void wpgx_gamepad_rumble(int pad, float low, float high, uint32_t duration_ms) {
    if (pad < 0 || pad >= kMaxPads || gPads[pad].pad == nullptr) {
        return;
    }
    const auto strength = [](float f) { return static_cast<Uint16>(std::clamp(f, 0.f, 1.f) * 65535.f); };
    SDL_RumbleGamepad(gPads[pad].pad, strength(low), strength(high), duration_ms);
}

// --- Audio -----------------------------------------------------------------------

WPGX_API int wpgx_audio_open(int rate, int channels) {
    if (gAudio != nullptr) {
        return 0;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "wpgx: SDL audio: %s\n", SDL_GetError());
        return -1;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, rate};
    gAudio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (gAudio == nullptr) {
        std::fprintf(stderr, "wpgx: audio device: %s\n", SDL_GetError());
        return -1;
    }
    gAudioChannels = channels;
    SDL_ResumeAudioStreamDevice(gAudio);
    return 0;
}

WPGX_API void wpgx_audio_close(void) {
    if (gAudio != nullptr) {
        SDL_DestroyAudioStream(gAudio);
        gAudio = nullptr;
    }
}

WPGX_API void wpgx_audio_queue(const int16_t* samples, uint32_t frames) {
    if (gAudio != nullptr) {
        SDL_PutAudioStreamData(gAudio, samples, static_cast<int>(frames * gAudioChannels * sizeof(int16_t)));
    }
}

WPGX_API uint32_t wpgx_audio_queued_frames(void) {
    if (gAudio == nullptr) {
        return 0;
    }
    const int bytes = SDL_GetAudioStreamQueued(gAudio);
    return bytes > 0 ? static_cast<uint32_t>(bytes) / (gAudioChannels * sizeof(int16_t)) : 0;
}

WPGX_API void wpgx_audio_clear(void) {
    if (gAudio != nullptr) {
        SDL_ClearAudioStream(gAudio);
    }
}

WPGX_API void wpgx_audio_set_gain(float gain) {
    if (gAudio != nullptr) {
        SDL_SetAudioStreamGain(gAudio, gain);
    }
}

WPGX_API int wpgx_audio_device_format(int* rate, int* channels, int* buffer_frames) {
    if (gAudio == nullptr) {
        return -1;
    }
    SDL_AudioSpec spec{};
    int frames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(gAudio), &spec, &frames)) {
        return -1;
    }
    *rate = spec.freq;
    *channels = spec.channels;
    *buffer_frames = frames;
    return 0;
}

WPGX_API const char* wpgx_audio_driver(void) {
    const char* name = SDL_GetCurrentAudioDriver();
    return name != nullptr ? name : "none";
}

// --- Test helpers ------------------------------------------------------------------

WPGX_API void wpgx_test_push_key(const char* name, int down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.key = SDL_GetKeyFromName(name);
    ev.key.down = down != 0;
    SDL_PushEvent(&ev);
}

WPGX_API void wpgx_test_push_mouse_motion(float x, float y) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

WPGX_API void wpgx_test_push_mouse_button(int button, int down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = static_cast<Uint8>(button);
    ev.button.down = down != 0;
    SDL_PushEvent(&ev);
}

WPGX_API void wpgx_test_push_quit(void) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&ev);
}

WPGX_API int wpgx_test_gamepad_attach(void) {
    if (gVirtual != nullptr) {
        return 0;
    }
    static const SDL_VirtualJoystickSensorDesc kSensors[] = {{SDL_SENSOR_GYRO, 200.f}, {SDL_SENSOR_ACCEL, 200.f}};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.nsensors = 2;
    desc.sensors = kSensors;
    desc.name = "wiiplay test gamepad";
    desc.Rumble = VirtualRumble;
    gVirtualId = SDL_AttachVirtualJoystick(&desc);
    if (gVirtualId == 0) {
        std::fprintf(stderr, "wpgx: virtual gamepad: %s\n", SDL_GetError());
        return -1;
    }
    gVirtual = SDL_OpenJoystick(gVirtualId);
    return gVirtual != nullptr ? 0 : -1;
}

WPGX_API void wpgx_test_gamepad_button(int button, int down) {
    if (gVirtual != nullptr) {
        SDL_SetJoystickVirtualButton(gVirtual, button, down != 0);
    }
}

WPGX_API void wpgx_test_gamepad_axis(int axis, float value) {
    if (gVirtual != nullptr) {
        SDL_SetJoystickVirtualAxis(gVirtual, axis, static_cast<Sint16>(std::clamp(value, -1.f, 1.f) * 32767.f));
    }
}

WPGX_API void wpgx_test_gamepad_sensor(int accel, float x, float y, float z) {
    if (gVirtual != nullptr) {
        const float data[3] = {x, y, z};
        SDL_SendJoystickVirtualSensorData(gVirtual, accel ? SDL_SENSOR_ACCEL : SDL_SENSOR_GYRO, SDL_GetTicksNS(),
                                          data, 3);
    }
}

WPGX_API uint32_t wpgx_test_gamepad_rumbles(float* low, float* high) {
    if (low != nullptr) {
        *low = gRumbleLow;
    }
    if (high != nullptr) {
        *high = gRumbleHigh;
    }
    return gRumbles;
}
