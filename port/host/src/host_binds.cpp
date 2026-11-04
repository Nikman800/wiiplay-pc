/**
 * The live input layer's bindings and settings (src/host_binds.h; port/docs/HOST_BUILD.md
 * §30, §37, §44): the config file, its per-game sections, and the keys and gamepad
 * buttons held.
 *
 * The config file: lines before the first [SECTION] (or after [global]) are the global
 * set. [GAME] starts lines for one game (or the menus): its bindings are layered on top
 * of the global ones while that game's scene runs (`clear` in it starts that game from
 * none), and the settings that make sense per game may be changed for it.
 */

#include "host_binds.h"

#include <revolution/WPAD.h>

#include <port/input.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace PortBinds {

namespace {

struct PadName {
    const char* name;
    int slot;
};
const PadName kPadNames[] = {
    {"south", PAD_SOUTH}, {"a", PAD_SOUTH}, {"east", PAD_EAST}, {"b", PAD_EAST}, {"west", PAD_WEST},
    {"x", PAD_WEST}, {"north", PAD_NORTH}, {"y", PAD_NORTH}, {"back", PAD_BACK}, {"guide", PAD_GUIDE},
    {"start", PAD_START}, {"leftstick", PAD_LEFT_STICK}, {"rightstick", PAD_RIGHT_STICK},
    {"leftshoulder", PAD_LEFT_SHOULDER}, {"rightshoulder", PAD_RIGHT_SHOULDER}, {"dpup", PAD_DPAD_UP},
    {"dpdown", PAD_DPAD_DOWN}, {"dpleft", PAD_DPAD_LEFT}, {"dpright", PAD_DPAD_RIGHT}, {"misc1", PAD_MISC1},
    {"lefttrigger", PAD_BUTTONS + 0}, {"righttrigger", PAD_BUTTONS + 1},
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

// The sections' names (PORT_GAME_* order), then the aliases. Compared without case,
// spaces, '_', '-' or '!': "Laser Hockey", "find_mii" and "Tanks!" all work.
const char* const kGameNames[PORT_GAME_COUNT] = {
    "menu", "shooting", "findmii", "tabletennis", "posemii", "hockey", "billiards", "fishing", "charge", "tanks",
};
struct Alias {
    const char* name;
    int game;
};
const Alias kAliases[] = {
    {"menus", PORT_GAME_MENU},           {"shootingrange", PORT_GAME_SHOOTING},
    {"laserhockey", PORT_GAME_HOCKEY},   {"pingpong", PORT_GAME_TABLETENNIS},
};
const int SECTION_GLOBAL = -1;

const int MAX_KEY_BINDS = 96;
const int MAX_SETTINGS = 24;

struct KeyBind {
    char name[32]; // SDL key name or MouseN
    u32 button;
};

// A complete set: the global one, or the active one
struct BindSet {
    KeyBind keys[MAX_KEY_BINDS];
    int keyCount;
    u32 pad[PAD_SLOTS];
};

struct Setting {
    char name[24];
    char value[24];
};

// A game's lines: changes to the global set
struct Section {
    bool used;     // the file has the section (even empty)
    bool cleared;  // `clear`: start from no bindings
    KeyBind keys[MAX_KEY_BINDS];
    int keyCount;
    bool padSet[PAD_SLOTS];
    u32 pad[PAD_SLOTS];
    Setting settings[MAX_SETTINGS];
    int settingCount;
};

const PortLiveConfig kDefaultConfig = {1.5f, 2.0f, 1, 1, 1.0f, 0, 0, 0.2f, 0,
                                       /* leftStick */ 0, /* twistSpeed */ 120.0f, /* wheelTwist */ 10.0f,
                                       /* tiltAngle */ 60.0f, /* tiltSpeed */ 360.0f, /* shakeStrength */ 2.5f,
                                       /* shakeFrames */ 4, /* pullDistance */ 0.3f, /* pushDistance */ 0.3f,
                                       /* speakerVolume */ 0.5f};

BindSet sGlobal, sActive;
PortLiveConfig sGlobalConfig = kDefaultConfig, sActiveConfig = kDefaultConfig;
Section sSections[PORT_GAME_COUNT];
int sActiveGame = -1;
bool sSelected = false;

// Keys held, so a binding change or two keys on one button work. A key pressed and
// released between two polls (a quick click, or a remote desktop delivering both at
// once) stays held until the next poll, so the game sees at least one frame of it.
const int MAX_HELD = 32;
struct Held {
    char name[32];
    u32 poll;         // the poll that saw the press
    bool releaseNext; // released in that same poll: drop at the next one
    bool stale;       // held across a change of its binding: counts for nothing
};
Held sHeld[MAX_HELD];
int sHeldCount = 0;

// Gamepad slots seen down at the last PadButtons, and those held across a binding change
bool sPadDown[MAX_CHANS][PAD_SLOTS];
bool sPadStale[MAX_CHANS][PAD_SLOTS];

f32 Clamp(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

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

void AddKey(KeyBind* keys, int* count, const char* name, u32 button) {
    for (int i = 0; i < *count; i++) {
        if (NameEq(keys[i].name, name)) {
            keys[i].button = button;
            return;
        }
    }
    if (*count < MAX_KEY_BINDS) {
        std::snprintf(keys[*count].name, sizeof keys[0].name, "%s", name);
        keys[(*count)++].button = button;
    }
}

u32 Lookup(const BindSet& set, const char* name) {
    for (int i = 0; i < set.keyCount; i++) {
        if (NameEq(set.keys[i].name, name)) {
            return set.keys[i].button;
        }
    }
    return 0;
}

void DefaultBindings(BindSet* s) {
    s->keyCount = 0;
    for (const PortInputBinding* b = PortInputDefaultBindings; b->name != NULL; b++) {
        AddKey(s->keys, &s->keyCount, b->name, b->button);
    }
    AddKey(s->keys, &s->keyCount, "Return", WPAD_BUTTON_A);
    AddKey(s->keys, &s->keyCount, "Backspace", WPAD_BUTTON_B);
    AddKey(s->keys, &s->keyCount, "H", WPAD_BUTTON_HOME);
    // Motions (§37): twist, tilt, shake, pull back and thrust, push
    const KeyBind motions[] = {
        {"Q", ACT_TWIST_LEFT}, {"E", ACT_TWIST_RIGHT}, {"W", ACT_TILT_FORWARD}, {"S", ACT_TILT_BACK},
        {"A", ACT_TILT_LEFT},  {"D", ACT_TILT_RIGHT},  {"F", ACT_SHAKE},        {"R", ACT_PULL},
        {"G", ACT_PUSH},
        {"Mouse2", ACT_TWIST_RESET}, // middle button
        {"Mouse4", ACT_SHAKE},       // side buttons
        {"Mouse5", ACT_PULL},
    };
    for (const KeyBind& m : motions) {
        AddKey(s->keys, &s->keyCount, m.name, m.button);
    }
    std::memset(s->pad, 0, sizeof s->pad);
    s->pad[PAD_SOUTH] = WPAD_BUTTON_A;
    s->pad[PAD_EAST] = WPAD_BUTTON_B;
    s->pad[PAD_WEST] = WPAD_BUTTON_1;
    s->pad[PAD_NORTH] = WPAD_BUTTON_2;
    s->pad[PAD_START] = WPAD_BUTTON_PLUS;
    s->pad[PAD_BACK] = WPAD_BUTTON_MINUS;
    s->pad[PAD_GUIDE] = WPAD_BUTTON_HOME;
    s->pad[PAD_DPAD_UP] = WPAD_BUTTON_UP;
    s->pad[PAD_DPAD_DOWN] = WPAD_BUTTON_DOWN;
    s->pad[PAD_DPAD_LEFT] = WPAD_BUTTON_LEFT;
    s->pad[PAD_DPAD_RIGHT] = WPAD_BUTTON_RIGHT;
    // Wii Play reads neither Nunchuk button: the shoulders and the left trigger make motions
    s->pad[PAD_LEFT_SHOULDER] = ACT_PULL;
    s->pad[PAD_RIGHT_SHOULDER] = ACT_SHAKE;
    s->pad[PAD_LEFT_STICK] = ACT_TWIST_RESET;
    s->pad[PAD_BUTTONS + 0] = ACT_PUSH;      // left trigger
    s->pad[PAD_BUTTONS + 1] = WPAD_BUTTON_B; // right trigger
}

// The settings: those a section may change, then the global-only ones
enum SettingResult { SET_OK, SET_UNKNOWN, SET_BAD_VALUE };
const char* const kGlobalOnly[] = {"volume", "mute_unfocused", "rumble", "nunchuk", "speaker_volume"};

bool GlobalOnly(const char* name) {
    for (const char* g : kGlobalOnly) {
        if (NameEq(g, name)) {
            return true;
        }
    }
    return false;
}

SettingResult ApplySetting(PortLiveConfig* c, const char* name, const char* value, const char** why) {
    f32 v = static_cast<f32>(std::atof(value));
    if (NameEq(name, "pointer_speed")) {
        c->pointerSpeed = v;
    } else if (NameEq(name, "gyro_sensitivity")) {
        c->gyroSensitivity = v;
    } else if (NameEq(name, "tilt")) {
        c->tilt = v != 0;
    } else if (NameEq(name, "stick_deadzone")) {
        c->stickDeadzone = Clamp(v, 0.0f, 0.9f);
    } else if (NameEq(name, "pointer_stabilise")) {
        c->pointerStabilise = v != 0;
    } else if (NameEq(name, "left_stick")) {
        // auto: tilt in the games that read tilt or twist, else the D-pad
        int m = NameEq(value, "auto") ? 0 : NameEq(value, "dpad") ? 1 : NameEq(value, "tilt") ? 2 : -1;
        if (m < 0) {
            *why = "left_stick: auto, dpad or tilt";
            return SET_BAD_VALUE;
        }
        c->leftStick = m;
    } else if (NameEq(name, "twist_speed")) {
        c->twistSpeed = Clamp(v, 0.0f, 1000.0f);
    } else if (NameEq(name, "wheel_twist")) {
        c->wheelTwist = Clamp(v, -90.0f, 90.0f);
    } else if (NameEq(name, "tilt_angle")) {
        c->tiltAngle = Clamp(v, 0.0f, 90.0f);
    } else if (NameEq(name, "tilt_speed")) {
        c->tiltSpeed = Clamp(v, 1.0f, 10000.0f);
    } else if (NameEq(name, "shake_strength")) {
        c->shakeStrength = Clamp(v, 0.0f, 3.4f);
    } else if (NameEq(name, "shake_frames")) {
        c->shakeFrames = static_cast<int>(Clamp(v, 1.0f, 30.0f));
    } else if (NameEq(name, "pull_distance")) {
        c->pullDistance = Clamp(v, 0.0f, 1.0f);
    } else if (NameEq(name, "push_distance")) {
        c->pushDistance = Clamp(v, 0.0f, 1.0f);
    } else if (NameEq(name, "rumble")) {
        c->rumble = v != 0;
    } else if (NameEq(name, "volume")) {
        c->volume = Clamp(v, 0.0f, 1.0f);
    } else if (NameEq(name, "mute_unfocused")) {
        c->muteUnfocused = v != 0;
    } else if (NameEq(name, "nunchuk")) {
        c->nunchuk = v != 0;
    } else if (NameEq(name, "speaker_volume")) {
        c->speakerVolume = Clamp(v, 0.0f, 2.0f);
    } else {
        *why = "unknown setting";
        return SET_UNKNOWN;
    }
    return SET_OK;
}

// A section header's name -> PORT_GAME_*, SECTION_GLOBAL, or -2 if unknown
int SectionFromName(const char* name) {
    char key[64];
    int n = 0;
    for (const char* p = name; *p && n < static_cast<int>(sizeof key) - 1; p++) {
        if (*p == ' ' || *p == '\t' || *p == '_' || *p == '-' || *p == '!') {
            continue;
        }
        key[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
    key[n] = '\0';
    if (std::strcmp(key, "global") == 0) {
        return SECTION_GLOBAL;
    }
    for (int g = 0; g < PORT_GAME_COUNT; g++) {
        if (std::strcmp(key, kGameNames[g]) == 0) {
            return g;
        }
    }
    for (const Alias& a : kAliases) {
        if (std::strcmp(key, a.name) == 0) {
            return a.game;
        }
    }
    return -2;
}

// The set for GAME: the global one with the section's changes
void Build(int game, BindSet* set, PortLiveConfig* config) {
    *config = sGlobalConfig;
    if (game < 0 || game >= PORT_GAME_COUNT || !sSections[game].used) {
        *set = sGlobal;
        return;
    }
    const Section& s = sSections[game];
    if (s.cleared) {
        set->keyCount = 0;
        std::memset(set->pad, 0, sizeof set->pad);
    } else {
        *set = sGlobal;
    }
    for (int i = 0; i < s.keyCount; i++) {
        AddKey(set->keys, &set->keyCount, s.keys[i].name, s.keys[i].button);
    }
    for (int i = 0; i < PAD_SLOTS; i++) {
        if (s.padSet[i]) {
            set->pad[i] = s.pad[i];
        }
    }
    for (int i = 0; i < s.settingCount; i++) {
        const char* why;
        ApplySetting(config, s.settings[i].name, s.settings[i].value, &why); // checked by Load
    }
}

void DropHeld(int i) {
    std::memmove(&sHeld[i], &sHeld[i + 1], (sHeldCount - i - 1) * sizeof sHeld[0]);
    sHeldCount--;
}

} // namespace

bool NameEq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

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

int PadIndex(const char* name) {
    for (const PadName& p : kPadNames) {
        if (NameEq(p.name, name)) {
            return p.slot;
        }
    }
    return -1;
}

const char* SectionName(int game) {
    return game >= 0 && game < PORT_GAME_COUNT ? kGameNames[game] : "global";
}

int Load(const char* path, char* err, int errLen) {
    DefaultBindings(&sGlobal);
    sGlobalConfig = kDefaultConfig;
    std::memset(sSections, 0, sizeof sSections);
    sSelected = false;
    int result = 0;
    if (path != NULL) {
        FILE* f = std::fopen(path, "r");
        if (f == NULL) {
            std::snprintf(err, errLen, "cannot open %s", path);
            result = -1;
        } else {
            char line[256];
            int lineNo = 0;
            int section = SECTION_GLOBAL;
            while (result == 0 && std::fgets(line, sizeof line, f) != NULL) {
                lineNo++;
                char* hash = std::strchr(line, '#');
                if (hash != NULL) {
                    *hash = '\0';
                }
                line[std::strcspn(line, "\r\n")] = '\0';
                auto bad = [&](const char* what) {
                    if (section == SECTION_GLOBAL) {
                        std::snprintf(err, errLen, "%s:%d: %s", path, lineNo, what);
                    } else {
                        std::snprintf(err, errLen, "%s:%d: [%s] %s", path, lineNo, kGameNames[section], what);
                    }
                    result = -1;
                };
                char* start = line + std::strspn(line, " \t");
                if (*start == '[') {
                    // [NAME]: the lines up to the next header are that game's
                    char* end = std::strchr(start, ']');
                    if (end == NULL || end[1 + std::strspn(end + 1, " \t")] != '\0') {
                        section = SECTION_GLOBAL; // the message names the line, not the last section
                        bad("expected: [SECTION] alone on its line");
                        continue;
                    }
                    *end = '\0';
                    int s = SectionFromName(start + 1);
                    if (s == -2) {
                        char what[192];
                        std::snprintf(what, sizeof what,
                                      "unknown section [%s] (shooting findmii tabletennis posemii hockey billiards "
                                      "fishing charge tanks menu, or global)",
                                      start + 1);
                        section = SECTION_GLOBAL; // the message names the line, not the last section
                        bad(what);
                        continue;
                    }
                    section = s;
                    if (s != SECTION_GLOBAL) {
                        sSections[s].used = true;
                    }
                    continue;
                }
                char* w[3];
                int n = Split(line, w, 3);
                if (n == 0) {
                    continue;
                }
                if (n < 0) {
                    bad("unterminated quote");
                    continue;
                }
                Section* sec = section == SECTION_GLOBAL ? nullptr : &sSections[section];
                if (NameEq(w[0], "clear")) {
                    if (sec == nullptr) {
                        sGlobal.keyCount = 0;
                        std::memset(sGlobal.pad, 0, sizeof sGlobal.pad);
                    } else {
                        // this game starts from no bindings (not even the global ones)
                        sec->cleared = true;
                        sec->keyCount = 0;
                        std::memset(sec->padSet, 0, sizeof sec->padSet);
                    }
                } else if (NameEq(w[0], "key") || NameEq(w[0], "pad")) {
                    // key "Left Shift" B   |   pad south A   (button "none" unbinds)
                    if (n != 3) {
                        bad("expected: key|pad NAME BUTTON");
                        continue;
                    }
                    u32 button = 0;
                    if (!BindFromName(w[2], &button)) {
                        bad("unknown remote button or motion (A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C, "
                            "TWIST_LEFT TWIST_RIGHT TWIST_RESET TILT_FORWARD TILT_BACK TILT_LEFT TILT_RIGHT SHAKE "
                            "PULL PUSH, none)");
                        continue;
                    }
                    if (NameEq(w[0], "key")) {
                        if (sec == nullptr) {
                            AddKey(sGlobal.keys, &sGlobal.keyCount, w[1], button);
                        } else {
                            AddKey(sec->keys, &sec->keyCount, w[1], button);
                        }
                    } else {
                        int idx = PadIndex(w[1]);
                        if (idx < 0) {
                            bad("unknown gamepad button");
                            continue;
                        }
                        if (sec == nullptr) {
                            sGlobal.pad[idx] = button;
                        } else {
                            sec->padSet[idx] = true;
                            sec->pad[idx] = button;
                        }
                    }
                } else if (n == 2) {
                    const char* why = "";
                    if (sec != nullptr && GlobalOnly(w[0])) {
                        char what[160];
                        std::snprintf(what, sizeof what,
                                      "%s is a global setting: put it before the first [section] (or under "
                                      "[global])",
                                      w[0]);
                        bad(what);
                        continue;
                    }
                    PortLiveConfig scratch = sGlobalConfig;
                    if (ApplySetting(sec == nullptr ? &sGlobalConfig : &scratch, w[0], w[1], &why) != SET_OK) {
                        bad(why);
                        continue;
                    }
                    if (sec != nullptr) {
                        if (sec->settingCount == MAX_SETTINGS) {
                            bad("too many settings in this section");
                            continue;
                        }
                        Setting& st = sec->settings[sec->settingCount++];
                        std::snprintf(st.name, sizeof st.name, "%s", w[0]);
                        std::snprintf(st.value, sizeof st.value, "%s", w[1]);
                    }
                } else {
                    bad("expected: SETTING VALUE, key NAME BUTTON, pad NAME BUTTON, clear or [SECTION]");
                }
            }
            std::fclose(f);
        }
    }
    sActive = sGlobal;
    sActiveConfig = sGlobalConfig;
    sActiveGame = -1;
    return result;
}

int Select(int game) {
    if (game < 0 || game >= PORT_GAME_COUNT) {
        game = -1;
    }
    if (sSelected && game == sActiveGame) {
        return 0;
    }
    const int old = sSelected ? sActiveGame : -1;
    sSelected = true;
    sActiveGame = game;
    static BindSet next; // large: not on the stack
    Build(game, &next, &sActiveConfig);
    // Held across the change: a key or button whose binding changed counts for nothing
    // until it is released (no stuck button, no surprise press)
    for (int i = 0; i < sHeldCount; i++) {
        if (Lookup(sActive, sHeld[i].name) != Lookup(next, sHeld[i].name)) {
            sHeld[i].stale = true;
        }
    }
    for (int c = 0; c < MAX_CHANS; c++) {
        for (int i = 0; i < PAD_SLOTS; i++) {
            if (sPadDown[c][i] && sActive.pad[i] != next.pad[i]) {
                sPadStale[c][i] = true;
            }
        }
    }
    sActive = next;
    const bool oldUsed = old >= 0 && sSections[old].used;
    const bool newUsed = game >= 0 && sSections[game].used;
    return newUsed ? 2 : oldUsed ? 1 : 0;
}

int Active() {
    return sActiveGame;
}

const PortLiveConfig& Config() {
    return sActiveConfig;
}

void KeyEvent(const char* name, bool down, u32 poll) {
    for (int i = 0; i < sHeldCount; i++) {
        if (NameEq(sHeld[i].name, name)) {
            if (down) {
                sHeld[i].releaseNext = false;
            } else if (sHeld[i].poll == poll && !sHeld[i].stale) {
                sHeld[i].releaseNext = true;
            } else {
                DropHeld(i);
            }
            return;
        }
    }
    if (down && sHeldCount < MAX_HELD) {
        Held& h = sHeld[sHeldCount++];
        std::snprintf(h.name, sizeof h.name, "%s", name);
        h.poll = poll;
        h.releaseNext = false;
        h.stale = false;
    }
}

void ReleaseDeferred() {
    for (int i = sHeldCount - 1; i >= 0; i--) {
        if (sHeld[i].releaseNext) {
            DropHeld(i);
        }
    }
}

void ClearHeld() {
    sHeldCount = 0;
}

int HeldCount() {
    return sHeldCount;
}

const char* HeldName(int i) {
    return i >= 0 && i < sHeldCount ? sHeld[i].name : "";
}

u32 KeyButtons() {
    u32 b = 0;
    for (int i = 0; i < sHeldCount; i++) {
        if (!sHeld[i].stale) {
            b |= Lookup(sActive, sHeld[i].name);
        }
    }
    return b;
}

u32 KeyBinding(const char* name) {
    return Lookup(sActive, name);
}

u32 PadButtons(int chan, const bool* buttons, bool leftTrigger, bool rightTrigger) {
    if (chan < 0 || chan >= MAX_CHANS) {
        return 0;
    }
    u32 b = 0;
    for (int i = 0; i < PAD_SLOTS; i++) {
        bool down = i < PAD_BUTTONS ? buttons[i] : (i == PAD_BUTTONS ? leftTrigger : rightTrigger);
        sPadDown[chan][i] = down;
        if (!down) {
            sPadStale[chan][i] = false;
        } else if (!sPadStale[chan][i]) {
            b |= sActive.pad[i];
        }
    }
    return b;
}

u32 PadBinding(int slot) {
    return slot >= 0 && slot < PAD_SLOTS ? sActive.pad[slot] : 0;
}

} // namespace PortBinds

extern "C" int PortLiveLoadConfig(const char* path, char* err, int errLen) {
    return PortBinds::Load(path, err, errLen);
}
