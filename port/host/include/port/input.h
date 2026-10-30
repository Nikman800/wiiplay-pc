/**
 * Input platform layer (Phase 7, docs/HOST_BUILD.md §23).
 *
 * Two parts:
 *
 *  - The host WPAD backend (src/host_wpad.cpp): the WPAD library the game's
 *    KPAD (src/revolution/KPAD/KPAD.c, linked unmodified) and its controller
 *    code call, over up to four virtual remotes. A remote's raw state is what
 *    WPADRead would return after the SDK's HID parser: calibrated
 *    accelerometer counts, the camera's dot objects, buttons, the Nunchuk's
 *    stick and accelerometer. PortWPADRunFrame produces the 200 Hz reports of
 *    one video frame and calls the sampling callbacks (KPAD's), as the
 *    Bluetooth stack does on the Wii.
 *
 *  - The mapping layer (src/host_input.cpp): turns a host input state (a
 *    pointer position on the screen, buttons, a tilt, a stick) into that raw
 *    state. The pointer becomes the two dots the sensor bar would give, chosen
 *    by inverting KPAD's dot -> position math, so the real KPAD computes the
 *    requested position. A script reader feeds it from a text file (tests);
 *    a window backend (SDL) would fill PortInputState from its events instead.
 */

#ifndef PORT_INPUT_H
#define PORT_INPUT_H

#include <types.h>

#include <revolution/WPAD.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * WPAD backend
 * ---------------------------------------------------------------------- */

/* Accelerometer calibration of the virtual remotes: the SDK's own defaults
 * (WPADHIDParser.c, used when a remote's calibration block fails its
 * checksum). Counts per g = one-g minus zero. */
#define PORT_WPAD_CORE_ACC_ZERO 530
#define PORT_WPAD_CORE_ACC_1G 636
#define PORT_WPAD_FS_ACC_ZERO 512
#define PORT_WPAD_FS_ACC_1G 716

/* Reports per second (KPAD's button repeat counts in 1/200 s). */
#define PORT_WPAD_SAMPLE_RATE 200

typedef struct PortWPADRemote {
    int connected;  /* 0: no remote on this channel */
    u8 dev;         /* WPAD_DEV_CORE, WPAD_DEV_FREESTYLE (Nunchuk), WPAD_DEV_CLASSIC */
    u8 battery;     /* 0-4 (WPADInfo.battery) */
    u16 button;     /* WPAD_BUTTON_* (the Nunchuk's Z / C included) */
    s16 acc[3];     /* X, Y, Z: raw counts minus the zero (WPADStatus.accX..Z) */
    DPDObject obj[WPAD_MAX_DPD_OBJECTS]; /* camera dots in WPADStatus form;
                                            size 0: no dot */
    s16 fsAcc[3];   /* Nunchuk accelerometer, counts minus the zero */
    s8 fsStick[2];  /* Nunchuk stick, counts from the centre */
} PortWPADRemote;

/* The state reported from the next sample on. A change of `connected` or
 * `dev` is delivered (connect / extension callbacks) at the next sample. */
void PortWPADSetRemote(s32 chan, const PortWPADRemote* remote);
void PortWPADGetRemote(s32 chan, PortWPADRemote* remote);

/* One report on every channel: finishes the pending asynchronous commands
 * (DPD, info, speaker, LED: their callbacks), delivers connection and
 * extension changes, then calls each connected channel's sampling callback. */
void PortWPADSample(void);
/* The reports of one NTSC frame (1001/60000 s at 200 Hz: 3 or 4, from an
 * exact accumulator). Returns the number of reports. */
int PortWPADRunFrame(void);
/* Number of reports so far. */
u32 PortWPADSampleCount(void);

/* Rumble: called whenever a remote's motor turns on or off. */
typedef void (*PortWPADMotorFunc)(s32 chan, int on, void* user);
void PortWPADSetMotorCallback(PortWPADMotorFunc func, void* user);
/* Speaker: every packet WPADSendStreamData accepts (4-bit ADPCM bytes). */
typedef void (*PortWPADSpeakerFunc)(s32 chan, const void* data, u16 len, void* user);
void PortWPADSetSpeakerCallback(PortWPADSpeakerFunc func, void* user);

/* ------------------------------------------------------------------------
 * Mapping layer
 * ---------------------------------------------------------------------- */

typedef struct PortInputConfig {
    /* The sensor height the game gives KPADSetSensorHeight (Wii Play's
     * RPSysCoreController::applySetting: 0.15 with the bar above the TV,
     * -0.35 below). PortInputDefaultConfig takes it from SC. */
    f32 sensorLevel;
    /* KPAD's sensor bar model (KPADSetObjInterval; KPAD's default 0.2). */
    f32 objInterval;
} PortInputConfig;

typedef struct PortInputState {
    int connected;
    int nunchuk;        /* Nunchuk plugged in */
    int battery;        /* 0-4 */
    u16 buttons;        /* WPAD_BUTTON_* */

    /* Pointer: where KPADStatus.pos should be, x right / y down, -1..1 across
     * the screen (PortInputPointerFromWindow converts window pixels). With
     * pointerValid 0 the remote points away: no dots. */
    int pointerValid;
    f32 pointerX, pointerY;
    f32 distance;       /* KPADStatus.dist: metres from the sensor bar */

    /* Orientation of the remote (degrees) and acceleration on top of gravity
     * (in g, in KPAD's acc axes). roll > 0: clockwise as the player sees it
     * (KPADStatus.horizon = (cos roll, sin roll)); pitch > 0: nose up. */
    f32 roll, pitch;
    f32 accel[3];

    /* Nunchuk: stick (-1..1, x right, y up), orientation, extra acceleration. */
    f32 stickX, stickY;
    f32 fsRoll, fsPitch;
    f32 fsAccel[3];
} PortInputState;

void PortInputDefaultConfig(PortInputConfig* config);
void PortInputDefaultState(PortInputState* state);

/* Window pixels (origin top left) -> pointer coordinates; the game's cursor
 * (RPSysCursorDrawMgr: ConvertFromNormalCC on a centre-canvas screen of the
 * window's size) lands back on the same pixel. */
void PortInputPointerFromWindow(f32 px, f32 py, f32 width, f32 height, f32* x, f32* y);

/* The raw remote state for an input state. */
void PortInputToRemote(const PortInputConfig* config, const PortInputState* state,
                       PortWPADRemote* remote);

/* WPAD_BUTTON_* bit for a name (A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C),
 * 0 if unknown. */
u16 PortInputButtonFromName(const char* name);

/* Default bindings for a window backend: mouse buttons and key names (as SDL
 * names them) to remote buttons. Terminated by an entry with name NULL. */
typedef struct PortInputBinding {
    const char* name;  /* "Mouse1", "Mouse3", "Return", "Up", ... */
    u16 button;
} PortInputBinding;
extern const PortInputBinding PortInputDefaultBindings[];

/* ------------------------------------------------------------------------
 * Scripted input (tests)
 * ---------------------------------------------------------------------- */

typedef struct PortInputScript PortInputScript;

/* Reads a script (format in docs/HOST_BUILD.md §23). NULL on error (the
 * message is in err). */
PortInputScript* PortInputScriptLoad(const char* path, char* err, int errLen);
void PortInputScriptFree(PortInputScript* script);
/* Frames the script covers (its `end`). */
int PortInputScriptFrames(const PortInputScript* script);
/* The window size the script's mouse positions refer to. */
void PortInputScriptWindow(const PortInputScript* script, f32* width, f32* height);
/* Applies the commands of frame `frame` to the four channels' states. */
void PortInputScriptApply(PortInputScript* script, int frame, PortInputState states[4]);

#ifdef __cplusplus
}
#endif

#endif
