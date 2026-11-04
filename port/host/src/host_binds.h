/**
 * The live input layer's bindings and settings (port/docs/HOST_BUILD.md §30, §37, §44):
 * the config file's parser, the global and per-game binding sets, and the keys and
 * gamepad buttons held, which the active set turns into remote buttons and motions.
 * No window system here (src/host_live.cpp feeds it): tests/test_binds.cpp tests it
 * alone.
 *
 * Host code: no allocation.
 */

#ifndef PORT_HOST_BINDS_H
#define PORT_HOST_BINDS_H

#include <types.h>

#include <port/live.h>

namespace PortBinds {

// SDL_GamepadButton / SDL_GamepadAxis values (SDL3's stable enums)
enum {
    PAD_SOUTH, PAD_EAST, PAD_WEST, PAD_NORTH, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LEFT_STICK,
    PAD_RIGHT_STICK, PAD_LEFT_SHOULDER, PAD_RIGHT_SHOULDER, PAD_DPAD_UP, PAD_DPAD_DOWN, PAD_DPAD_LEFT,
    PAD_DPAD_RIGHT, PAD_MISC1, PAD_BUTTONS = 26
};
enum { AXIS_LEFTX, AXIS_LEFTY, AXIS_RIGHTX, AXIS_RIGHTY, AXIS_LEFT_TRIGGER, AXIS_RIGHT_TRIGGER, AXIS_COUNT };
// A gamepad binding slot: a button, or PAD_BUTTONS + 0 / 1 for the left / right trigger
const int PAD_SLOTS = PAD_BUTTONS + 2;

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

const int MAX_CHANS = 4; // WPAD_MAX_CONTROLLERS

bool NameEq(const char* a, const char* b);
// Splits a line into at most `max` words (more are ignored). A word in double
// quotes may contain spaces: key names such as "Left Shift". -1: an unterminated quote.
int Split(char* line, char** words, int max);
// A gamepad button or trigger's name -> its slot; -1 if unknown
int PadIndex(const char* name);

// The config file (NULL: the defaults). Returns 0, or -1 with "FILE:LINE: [SECTION] what"
// in err. Selects the global set until the next Select.
int Load(const char* path, char* err, int errLen);

// The game running (PORT_GAME_*, port/input.h; < 0: unknown, the global set): its section
// becomes the active set. Keys and buttons held whose binding changes stop counting until
// they are released (pressed again they give the new binding). Returns 2 when the game
// changed to one the config has a section for, 1 when it changed from such a game to one
// without (the global set), else 0.
int Select(int game);
int Active();                     // the game Select last chose (-1 before the first)
const char* SectionName(int game); // "tanks", "menu", ...; "global" for < 0
const PortLiveConfig& Config();   // the active settings

// Keyboard and mouse (remote 0). Poll: the caller's poll counter: a key pressed and
// released within one poll stays held until ReleaseDeferred at the next.
void KeyEvent(const char* name, bool down, u32 poll);
void ReleaseDeferred();
void ClearHeld();                 // nothing held (the window lost focus)
int HeldCount();
const char* HeldName(int i);      // in the order pressed
u32 KeyButtons();                 // remote buttons and ACT_* bits of the keys held
u32 KeyBinding(const char* name); // the active binding of one key (0 if none)

// Gamepad CHAN: the buttons held (PAD_BUTTONS of them) and the triggers past half way
// -> remote buttons and ACT_* bits
u32 PadButtons(int chan, const bool* buttons, bool leftTrigger, bool rightTrigger);
u32 PadBinding(int slot);

} // namespace PortBinds

#endif
