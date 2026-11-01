/**
 * The live platform layer of the game executable (port/docs/HOST_BUILD.md §30):
 * real-time pacing of the VI clock, the AX mixer's output on an audio device,
 * and window / keyboard / mouse / gamepad input into the §23 mapping layer.
 * Built on wpgx's window, input and audio functions (SDL3 inside the Aurora
 * library); used only by build/host64/boot.
 */

#ifndef PORT_LIVE_H
#define PORT_LIVE_H

#include <types.h>

#include <port/input.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Settings from the config file (format: HOST_BUILD.md §30 / RUNNING.md). */
typedef struct PortLiveConfig {
    f32 pointerSpeed;     /* right stick: screens per second at full deflection */
    f32 gyroSensitivity;  /* gyro pointer: screens per radian; 0 turns it off */
    int tilt;             /* gamepad accelerometer -> remote roll / pitch */
    int rumble;           /* remote motor -> gamepad rumble */
    f32 volume;           /* 0..1 */
    int muteUnfocused;    /* silence while the window has no focus */
    int nunchuk;          /* report a Nunchuk on every connected remote */
    f32 stickDeadzone;
    int pointerStabilise; /* keep KPAD's pointer stabilisation for remote 0 (the mouse); default off */
    /* Motions (HOST_BUILD.md §37) */
    int leftStick;        /* 0 auto (tilt in the games that read tilt / twist), 1 D-pad, 2 tilt */
    f32 twistSpeed;       /* degrees per second while a twist key is held */
    f32 wheelTwist;       /* degrees per mouse wheel step */
    f32 tiltAngle;        /* degrees of a tilt key held / the stick at full deflection */
    f32 tiltSpeed;        /* degrees per second the tilt keys move the remote */
    f32 shakeStrength;    /* g of the shake pulse along the remote's up */
    int shakeFrames;      /* its length in frames */
    f32 pullDistance;     /* metres the remote draws back while PULL is held */
    f32 pushDistance;     /* metres closer while PUSH is held */
    f32 speakerVolume;    /* the Wii Remote speaker's sound in the output (0 mutes; §37) */
} PortLiveConfig;

/* Reads the config file (NULL: defaults only). Returns 0, or -1 with a
 * message in err. */
int PortLiveLoadConfig(const char* path, char* err, int errLen);
const PortLiveConfig* PortLiveGetConfig(void);

/* Real-time pacing: installs the VI clock's pacer. With audio, AX frames go
 * to the device and the queued amount steers the pace (the device's clock
 * paces the game). speed 1 = real time. */
void PortLiveStartPacing(f32 speed);
/* Audio output to the default device (32 kHz stereo s16, resampled by SDL).
 * Returns 0 on success. */
int PortLiveStartAudio(void);
/* Called by the host's audio sink with each AX frame. */
void PortLiveAudioFrame(const short* samples, u32 frames);

/* Live input: opens the gamepad subsystem; remote 0 is connected (keyboard and
 * mouse), gamepads 1-3 connect remotes 1-3, gamepad 0 also drives remote 0. */
int PortLiveStartInput(void);
/* Each retrace: polls the window system, applies the events to the input
 * states. Returns 1 when the user asked to quit. */
int PortLivePoll(PortInputState states[WPAD_MAX_CONTROLLERS]);
/* With a test script (--input) as well: the live layer adds its buttons and
 * motions to the script's remotes instead of replacing them (§37). */
void PortLiveSetMerge(int merge);
/* The motions (twist, tilt, shake, pull / thrust, push: §37) added to a copy of
 * remote CHAN's state before it goes to the WPAD backend. */
void PortLiveApplyMotion(int chan, PortInputState* st);
/* Tests: a key event through SDL's queue (the game-side players' `play 2`). */
void PortLiveTestKey(const char* name, int down);

/* Synthetic events for tests (the --events script): pushes the commands of
 * retrace `retrace` into SDL's queue before PortLivePoll reads it. */
int PortLiveLoadEvents(const char* path, char* err, int errLen);
void PortLivePushEvents(u32 retrace);

/* Counters for the END line. */
typedef struct PortLiveStats {
    double wallSeconds;      /* since pacing started */
    u32 lateResets;          /* the pace fell behind and was re-anchored */
    u32 underruns, overruns; /* audio queue empty / over the limit */
    u32 queuedFrames;        /* audio frames queued now */
    u32 rumbleOn;            /* motor-on calls forwarded to a gamepad */
    u32 testRumbles;         /* rumble requests the virtual test gamepad got */
    u32 events;              /* window-system events applied */
    u32 skippedFrames;       /* AX frames not queued: the queue was over its high mark */
    double correction;       /* the audio controller's last pace correction (fraction, + = slower) */
} PortLiveStats;
void PortLiveGetStats(PortLiveStats* stats);

void PortLiveShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
