/**
 * The live input layer's config file and per-game bindings (src/host_binds.cpp;
 * port/docs/HOST_BUILD.md §30, §37, §44), without a window system: config files
 * written into the directory given on the command line, loaded, then games selected
 * and keys and gamepad buttons pressed as src/host_live.cpp does.
 *
 *   test_binds DIR
 */
#include "../src/host_binds.h"

#include <revolution/WPAD.h>

#include <port/input.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace PortBinds;

namespace {

int sErrors = 0, sChecks = 0;
std::string sDir;
u32 sPoll = 0;

void Check(bool ok, const char* what, int line) {
    sChecks++;
    if (!ok) {
        sErrors++;
        std::fprintf(stderr, "test_binds:%d: FAILED: %s\n", line, what);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

std::string Write(const char* name, const char* text) {
    std::string path = sDir + "/" + name;
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        std::fprintf(stderr, "test_binds: cannot write %s\n", path.c_str());
        sErrors++;
        return path;
    }
    std::fputs(text, f);
    std::fclose(f);
    return path;
}

// Loads TEXT as a config file; err gets the message
int LoadText(const char* name, const char* text, char* err, int len) {
    err[0] = '\0';
    return Load(Write(name, text).c_str(), err, len);
}

bool LoadOk(const char* name, const char* text) {
    char err[512];
    int r = LoadText(name, text, err, sizeof err);
    if (r != 0) {
        std::fprintf(stderr, "test_binds: %s: %s\n", name, err);
    }
    return r == 0;
}

// The error a config gives (empty if none)
std::string LoadErr(const char* name, const char* text) {
    char err[512];
    if (LoadText(name, text, err, sizeof err) == 0) {
        return "";
    }
    return err;
}

bool Has(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

// One poll: the deferred releases, then the key events
void Poll() {
    sPoll++;
    ReleaseDeferred();
}
void Key(const char* name, bool down) {
    KeyEvent(name, down, sPoll);
}

const u32 TILT_FORWARD = ACT_TILT_FORWARD, TILT_LEFT = ACT_TILT_LEFT, TILT_BACK = ACT_TILT_BACK,
          TILT_RIGHT = ACT_TILT_RIGHT;

// The defaults with no file, and an old (global-only) config: as before sections
void TestGlobal() {
    char err[256];
    CHECK(Load(nullptr, err, sizeof err) == 0);
    for (int game = -1; game < PORT_GAME_COUNT; game++) {
        Select(game);
        CHECK(KeyBinding("W") == TILT_FORWARD);
        CHECK(KeyBinding("Space") == WPAD_BUTTON_A);
        CHECK(KeyBinding("Up") == WPAD_BUTTON_UP);
        CHECK(PadBinding(PAD_SOUTH) == WPAD_BUTTON_A);
        CHECK(PadBinding(PAD_BUTTONS + 0) == ACT_PUSH);
        CHECK(Config().tiltAngle == 60.0f && Config().volume == 1.0f && Config().leftStick == 0);
    }
    // RUNNING.md's sample, global lines only
    CHECK(LoadOk("global.cfg",
                 "# keyboard and mouse\n"
                 "key Return A\n"
                 "key \"Left Shift\" B\n"
                 "key Mouse2 HOME          # middle mouse button\n"
                 "key Escape none          # unbind\n"
                 "pad rightshoulder 1\n"
                 "pointer_speed 1.5\n"
                 "gyro_sensitivity 2.0\n"
                 "tilt 1\nrumble 1\nvolume 0.5\nmute_unfocused 1\nnunchuk 0\nstick_deadzone 0.25\n"
                 "pointer_stabilise 0\n"
                 "key Q TWIST_LEFT\npad rightshoulder SHAKE\nleft_stick dpad\ntwist_speed 90\n"
                 "wheel_twist 10\ntilt_angle 45\ntilt_speed 360\nshake_strength 2.5\nshake_frames 4\n"
                 "pull_distance 0.3\npush_distance 0.3\nspeaker_volume 0.5\n"
                 "key W UP\n"));
    for (int game = -1; game < PORT_GAME_COUNT; game++) {
        CHECK(Select(game) == 0); // no sections: never a switch to report
        CHECK(KeyBinding("W") == WPAD_BUTTON_UP);
        CHECK(KeyBinding("Escape") == 0);
        CHECK(KeyBinding("Mouse2") == WPAD_BUTTON_HOME);
        CHECK(PadBinding(PAD_RIGHT_SHOULDER) == ACT_SHAKE);
        CHECK(Config().tiltAngle == 45.0f && Config().volume == 0.5f && Config().leftStick == 1 &&
              Config().muteUnfocused == 1 && Config().twistSpeed == 90.0f && Config().stickDeadzone == 0.25f);
    }
    // `clear` (global): no bindings at all, then its own
    CHECK(LoadOk("clear.cfg", "clear\nkey Space A\n"));
    Select(PORT_GAME_TANKS);
    CHECK(KeyBinding("Space") == WPAD_BUTTON_A && KeyBinding("W") == 0 && KeyBinding("Return") == 0);
    CHECK(PadBinding(PAD_SOUTH) == 0);
}

const char* const kTanksWasd = "# Tanks!: WASD drives the tank\n"
                               "[tanks]\n"
                               "key W UP\n"
                               "key A LEFT\n"
                               "key S DOWN\n"
                               "key D RIGHT\n";

// [tanks] WASD: only while Tanks! runs; Charge! and the menus keep the tilt keys
void TestTanks() {
    CHECK(LoadOk("tanks.cfg", kTanksWasd));
    CHECK(Active() == -1);
    CHECK(KeyBinding("W") == TILT_FORWARD); // before the first Select: the global set
    CHECK(Select(PORT_GAME_MENU) == 0);     // no [menu] section
    CHECK(KeyBinding("W") == TILT_FORWARD);
    CHECK(Select(PORT_GAME_TANKS) == 2);
    CHECK(Active() == PORT_GAME_TANKS && std::strcmp(SectionName(Active()), "tanks") == 0);
    CHECK(KeyBinding("W") == WPAD_BUTTON_UP && KeyBinding("A") == WPAD_BUTTON_LEFT &&
          KeyBinding("S") == WPAD_BUTTON_DOWN && KeyBinding("D") == WPAD_BUTTON_RIGHT);
    // layered on the global set: the rest stays
    CHECK(KeyBinding("Q") == ACT_TWIST_LEFT && KeyBinding("Space") == WPAD_BUTTON_A &&
          KeyBinding("Up") == WPAD_BUTTON_UP && PadBinding(PAD_SOUTH) == WPAD_BUTTON_A);
    CHECK(Select(PORT_GAME_TANKS) == 0); // the same game: nothing to do
    CHECK(Select(PORT_GAME_MENU) == 1);  // back to the global set
    CHECK(KeyBinding("W") == TILT_FORWARD);
    CHECK(Select(PORT_GAME_CHARGE) == 0);
    CHECK(KeyBinding("W") == TILT_FORWARD && KeyBinding("A") == TILT_LEFT && KeyBinding("S") == TILT_BACK &&
          KeyBinding("D") == TILT_RIGHT);
    CHECK(Select(-1) == 0); // the hook absent: global
    CHECK(KeyBinding("W") == TILT_FORWARD);
    // the section order does not matter: global lines after a [global] header still
    // apply under [tanks] (the section is layered on the final global set)
    CHECK(LoadOk("order.cfg", "[tanks]\nkey W UP\n[global]\nkey Q B\nkey W 1\n"));
    Select(PORT_GAME_TANKS);
    CHECK(KeyBinding("W") == WPAD_BUTTON_UP && KeyBinding("Q") == WPAD_BUTTON_B);
    Select(PORT_GAME_MENU);
    CHECK(KeyBinding("W") == WPAD_BUTTON_1);
}

// `none` and `clear` in a section, gamepad bindings, repeated sections, the names
void TestNoneClear() {
    CHECK(LoadOk("noneclear.cfg",
                 "key Z B\n"
                 "[tanks]\n"
                 "key Q none\n"
                 "pad south none\n"
                 "pad lefttrigger B\n"
                 "[ Laser Hockey ]\n"
                 "key E A          # dropped by the clear below\n"
                 "clear\n"
                 "key Space A\n"
                 "pad east B\n"
                 "[tanks]\n"
                 "key W UP\n"));
    Select(PORT_GAME_TANKS);
    CHECK(KeyBinding("Q") == 0 && KeyBinding("E") == ACT_TWIST_RIGHT && KeyBinding("W") == WPAD_BUTTON_UP);
    CHECK(KeyBinding("Z") == WPAD_BUTTON_B);
    CHECK(PadBinding(PAD_SOUTH) == 0 && PadBinding(PAD_BUTTONS + 0) == WPAD_BUTTON_B &&
          PadBinding(PAD_EAST) == WPAD_BUTTON_B);
    Select(PORT_GAME_HOCKEY);
    CHECK(KeyBinding("Space") == WPAD_BUTTON_A && KeyBinding("E") == 0 && KeyBinding("Q") == 0 &&
          KeyBinding("Return") == 0 && KeyBinding("Z") == 0 && KeyBinding("Escape") == 0);
    CHECK(PadBinding(PAD_EAST) == WPAD_BUTTON_B && PadBinding(PAD_SOUTH) == 0 && PadBinding(PAD_GUIDE) == 0);
    Select(PORT_GAME_MENU);
    CHECK(KeyBinding("Q") == ACT_TWIST_LEFT && KeyBinding("Return") == WPAD_BUTTON_A &&
          PadBinding(PAD_SOUTH) == WPAD_BUTTON_A);
    // every name and alias
    struct {
        const char* header;
        int game;
    } names[] = {
        {"menu", PORT_GAME_MENU},           {"Menus", PORT_GAME_MENU},
        {"shooting", PORT_GAME_SHOOTING},   {"Shooting Range", PORT_GAME_SHOOTING},
        {"findmii", PORT_GAME_FINDMII},     {"Find Mii", PORT_GAME_FINDMII},
        {"tabletennis", PORT_GAME_TABLETENNIS}, {"Table Tennis", PORT_GAME_TABLETENNIS},
        {"posemii", PORT_GAME_POSEMII},     {"pose_mii", PORT_GAME_POSEMII},
        {"hockey", PORT_GAME_HOCKEY},       {"laser-hockey", PORT_GAME_HOCKEY},
        {"billiards", PORT_GAME_BILLIARDS}, {"FISHING", PORT_GAME_FISHING},
        {"Charge!", PORT_GAME_CHARGE},      {"Tanks!", PORT_GAME_TANKS},
    };
    for (const auto& n : names) {
        char text[128];
        std::snprintf(text, sizeof text, "[%s]\nkey F1 PLUS\n", n.header);
        CHECK(LoadOk("name.cfg", text));
        for (int game = 0; game < PORT_GAME_COUNT; game++) {
            Select(game);
            if ((KeyBinding("F1") == WPAD_BUTTON_PLUS) != (game == n.game)) {
                std::fprintf(stderr, "test_binds: [%s] applies to game %d\n", n.header, game);
                sErrors++;
            }
        }
    }
}

// Settings per game; the global-only ones are errors in a section
void TestSettings() {
    CHECK(LoadOk("settings.cfg",
                 "tilt_angle 50\n"
                 "volume 0.8\n"
                 "[charge]\n"
                 "tilt_angle 30\n"
                 "tilt_speed 720\n"
                 "[tanks]\n"
                 "left_stick dpad\n"
                 "pointer_speed 3\n"
                 "[hockey]\n"
                 "twist_speed 240\nwheel_twist 5\ngyro_sensitivity 0\nstick_deadzone 0.1\npointer_stabilise 1\n"
                 "shake_strength 1\nshake_frames 8\npull_distance 0.5\npush_distance 0.1\ntilt 0\n"));
    Select(PORT_GAME_CHARGE);
    CHECK(Config().tiltAngle == 30.0f && Config().tiltSpeed == 720.0f && Config().volume == 0.8f);
    CHECK(Config().leftStick == 0 && Config().pointerSpeed == 1.5f);
    Select(PORT_GAME_TANKS);
    CHECK(Config().tiltAngle == 50.0f && Config().leftStick == 1 && Config().pointerSpeed == 3.0f);
    Select(PORT_GAME_HOCKEY);
    CHECK(Config().twistSpeed == 240.0f && Config().wheelTwist == 5.0f && Config().gyroSensitivity == 0.0f &&
          Config().pointerStabilise == 1 && Config().shakeFrames == 8 && Config().tilt == 0 &&
          std::fabs(Config().stickDeadzone - 0.1f) < 1e-6f && std::fabs(Config().pullDistance - 0.5f) < 1e-6f);
    Select(PORT_GAME_MENU);
    CHECK(Config().tiltAngle == 50.0f && Config().tiltSpeed == 360.0f && Config().leftStick == 0 &&
          Config().twistSpeed == 120.0f && Config().pointerStabilise == 0 && Config().volume == 0.8f);
    // A section with only settings switches too
    CHECK(Select(PORT_GAME_CHARGE) == 2);

    for (const char* g : {"volume 0.5", "mute_unfocused 1", "rumble 0", "nunchuk 1", "speaker_volume 1"}) {
        char text[128];
        std::snprintf(text, sizeof text, "\n[tanks]\n%s\n", g);
        std::string e = LoadErr("globalonly.cfg", text);
        CHECK(Has(e, "globalonly.cfg:3: [tanks] ") && Has(e, "is a global setting"));
    }
}

// Bad lines: the file, the line and the section in the message
void TestErrors() {
    std::string e = LoadErr("unknown.cfg", "key W UP\n\n[tank]\nkey W UP\n");
    CHECK(Has(e, "unknown.cfg:3: unknown section [tank]") && Has(e, "tanks") && Has(e, "findmii") &&
          Has(e, "menu") && Has(e, "global"));
    e = LoadErr("badbutton.cfg", "[charge]\n# a comment\nkey W FORWARD\n");
    CHECK(Has(e, "badbutton.cfg:3: [charge] unknown remote button or motion"));
    e = LoadErr("badpad.cfg", "[Find Mii]\npad trigger A\n");
    CHECK(Has(e, "badpad.cfg:2: [findmii] unknown gamepad button"));
    e = LoadErr("badsetting.cfg", "[fishing]\ntilt_angel 30\n");
    CHECK(Has(e, "badsetting.cfg:2: [fishing] unknown setting"));
    e = LoadErr("badstick.cfg", "[menu]\nleft_stick sideways\n");
    CHECK(Has(e, "badstick.cfg:2: [menu] left_stick: auto, dpad or tilt"));
    e = LoadErr("badheader.cfg", "[tanks\n");
    CHECK(Has(e, "badheader.cfg:1: expected: [SECTION] alone on its line"));
    e = LoadErr("badheader2.cfg", "[tanks] key W UP\n");
    CHECK(Has(e, "badheader2.cfg:1: expected: [SECTION]"));
    e = LoadErr("global.cfg", "key W\n");
    CHECK(Has(e, "global.cfg:1: expected: key|pad NAME BUTTON") && !Has(e, "["));
    e = LoadErr("quote.cfg", "[billiards]\nkey \"Left Shift B\n");
    CHECK(Has(e, "quote.cfg:2: [billiards] unterminated quote"));
}

// Keys and gamepad buttons held while the game changes
void TestHeld() {
    CHECK(LoadOk("held.cfg", "[tanks]\nkey W UP\nkey A LEFT\npad south B\n"));
    Select(PORT_GAME_CHARGE);
    Poll();
    Key("W", true);
    Key("Space", true);
    CHECK(KeyButtons() == (TILT_FORWARD | WPAD_BUTTON_A));
    Poll();
    CHECK(KeyButtons() == (TILT_FORWARD | WPAD_BUTTON_A));
    // Charge! -> Tanks! with W and Space down: W's binding changed (it stops tilting and does
    // not press UP); Space's did not (A stays)
    Select(PORT_GAME_TANKS);
    Poll();
    CHECK(KeyButtons() == WPAD_BUTTON_A);
    Key("A", true); // pressed in Tanks!: the new binding
    CHECK(KeyButtons() == (WPAD_BUTTON_A | WPAD_BUTTON_LEFT));
    Key("W", false);
    Poll();
    CHECK(KeyButtons() == (WPAD_BUTTON_A | WPAD_BUTTON_LEFT));
    Key("W", true); // pressed again: UP
    Poll();
    CHECK(KeyButtons() == (WPAD_BUTTON_A | WPAD_BUTTON_LEFT | WPAD_BUTTON_UP));
    // Tanks! -> the menus with W and A down: both stop, nothing stuck
    Select(PORT_GAME_MENU);
    Poll();
    CHECK(KeyButtons() == WPAD_BUTTON_A);
    Key("W", false);
    Key("A", false);
    Key("Space", false);
    Poll();
    CHECK(KeyButtons() == 0 && HeldCount() == 0);
    // a stale key released and pressed again within one poll: the new binding, for a frame
    Select(PORT_GAME_TANKS);
    Poll();
    Key("W", true);
    Poll();
    Select(PORT_GAME_CHARGE);
    Poll();
    CHECK(KeyButtons() == 0);
    Key("W", false);
    Key("W", true);
    Key("W", false);
    CHECK(KeyButtons() == TILT_FORWARD);
    Poll();
    CHECK(KeyButtons() == 0 && HeldCount() == 0);
    // a click within one poll still shows for one frame (as before sections)
    Key("Mouse1", true);
    Key("Mouse1", false);
    CHECK(KeyButtons() == WPAD_BUTTON_A);
    Poll();
    CHECK(KeyButtons() == 0);

    // the gamepad: south held from the menus (A) into Tanks! (B): nothing until released
    bool buttons[PAD_BUTTONS] = {};
    Select(PORT_GAME_MENU);
    buttons[PAD_SOUTH] = true;
    buttons[PAD_EAST] = true;
    CHECK(PadButtons(1, buttons, false, true) == (WPAD_BUTTON_A | WPAD_BUTTON_B));
    Select(PORT_GAME_TANKS);
    CHECK(PadButtons(1, buttons, false, true) == WPAD_BUTTON_B); // east and the right trigger: B
    buttons[PAD_SOUTH] = false;
    CHECK(PadButtons(1, buttons, false, true) == WPAD_BUTTON_B);
    buttons[PAD_SOUTH] = true;
    CHECK(PadButtons(1, buttons, false, true) == WPAD_BUTTON_B); // south is B in Tanks!
    buttons[PAD_EAST] = false;
    CHECK(PadButtons(1, buttons, false, false) == WPAD_BUTTON_B);
    Select(PORT_GAME_MENU);
    CHECK(PadButtons(1, buttons, false, false) == 0);
    buttons[PAD_SOUTH] = false;
    CHECK(PadButtons(1, buttons, false, false) == 0);
    buttons[PAD_SOUTH] = true;
    CHECK(PadButtons(1, buttons, false, false) == WPAD_BUTTON_A);
    // the focus lost: nothing held
    Key("W", true);
    ClearHeld();
    CHECK(KeyButtons() == 0 && HeldCount() == 0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: test_binds DIR\n");
        return 2;
    }
    sDir = argv[1];
    TestGlobal();
    TestTanks();
    TestNoneClear();
    TestSettings();
    TestErrors();
    TestHeld();
    std::printf("test_binds: %d checks, %d errors\n", sChecks, sErrors);
    return sErrors == 0 ? 0 : 1;
}
