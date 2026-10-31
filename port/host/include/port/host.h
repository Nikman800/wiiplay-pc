/**
 * Host-only entry points of the port's SDK stand-ins (port/host/src).
 */

#ifndef PORT_HOST_H
#define PORT_HOST_H

#include <types.h>

#include <revolution/AX/AXPB.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Directory that DVD paths resolve against (the extracted disc's files/). */
void PortHostSetDiscRoot(const char* pRoot);

/* Phase 7 platform layer (HOST_BUILD.md §22). Call in this order, before the
 * game's own init (RPSysSystem::initialize / DVDInit / NANDInit / SCInit):
 *
 *   PortOSInit          boot information, MEM1/MEM2 arenas in M0
 *   PortDVDSetDisc      the extracted disc: its files/ directory and the FST
 *                       (NULL: files/../sys/fst.bin, or built from the tree
 *                       when that is missing; ""  forces the tree); returns 1
 *                       when the FST was built from the tree
 *   PortNANDSetRoot     the directory that stands for the NAND
 *   PortSCLoadConfig    system settings (NULL: the defaults) */
void PortOSInit(void);
int PortDVDSetDisc(const char* filesRoot, const char* fstPath);
const void* PortDVDGetFst(u32* size, int* fromTree);
int PortNANDSetRoot(const char* root);
int PortSCLoadConfig(const char* path);

/**
 * Swaps the UTF-16BE text of a loaded BMG (MESGbmg1, 16-bit encoding) to host
 * byte order in place. Tag payloads stay big-endian (EGG::MsgRes::analyzeTag
 * reads them with port_load_be32 on the host). Call once per buffer, after
 * loading and before constructing EGG::MsgRes / RPSysMessage on it. Returns 1
 * on success, 0 if the buffer is not a 16-bit BMG or a tag is malformed.
 */
int PortSwapBmgText(void* pBmg);
/* PortSwapBmgText's mark in the last reserved byte of a swapped BMG header. */
#define PORT_BMG_SWAPPED 0x48

/*
 * Threads (src/host_thread.cpp, HOST_BUILD.md §21): cooperative and
 * deterministic. Interrupt context defers rescheduling to its end;
 * PortOSIdle lets every ready thread run until all of them block (the main
 * thread waiting for the next retrace).
 */
void PortOSEnterInterrupt(void);
void PortOSLeaveInterrupt(void);
void PortOSIdle(void);
/* A device (HOST_BUILD.md §22): finishes its pending commands, calling their
 * callbacks, and returns nonzero if it did anything. Devices run when every
 * thread is blocked and when the main thread idles (src/host_thread.cpp). */
typedef int (*PortOSDeviceFunc)(void);
void PortOSAddDevice(PortOSDeviceFunc func);
/* The clock (HOST_BUILD.md §29): when every thread is blocked and no device has
 * work, it is called (in interrupt context) to advance time to the next timed
 * event; it returns 0 if nothing can happen any more (a deadlock). */
void PortOSSetClock(PortOSDeviceFunc func);
/* A thread polling for something an interrupt would change (HOST_BUILD.md §29):
 * every other ready thread runs, then the devices or the clock, and the caller
 * resumes after that. */
void PortOSSpinWait(void);

/*
 * AX (src/host_ax.cpp, HOST_BUILD.md §21): the DSP as a software mixer.
 * PortAXRunFrame runs one AX frame (96 samples at 32 kHz, 3 ms) as the AI DMA
 * interrupt would and writes 96 stereo s16 frames (L, R interleaved) to out
 * (if not NULL). A voice tap sees every running voice of every frame.
 */
#define PORT_AX_FRAME_SAMPLES 96

typedef struct PortAXVoiceTrace {
    unsigned long long frame; /* AX frame number, from 0 */
    unsigned int index;       /* voice (PB) index */
    const AXPB* pbBefore;     /* the PB the DSP started the frame with */
    const AXPB* pbAfter;      /* and left */
    const s16* input;         /* accelerator output (decoded samples), in read order */
    unsigned int inputCount;
    const s16* src;           /* 96 samples after sample-rate conversion */
    const s16* out;           /* 96 samples after the volume envelope and filters */
} PortAXVoiceTrace;
typedef void (*PortAXVoiceTap)(const PortAXVoiceTrace* trace, void* user);
void PortAXSetVoiceTap(PortAXVoiceTap tap, void* user);

void PortAXRunFrame(short* out);
/* Non-zero once AXInit has set up the output (PortAXRunFrame may run). */
int PortAXIsInitialised(void);

/* The host VI and game clock (src/host_vi.cpp, HOST_BUILD.md §29). */
typedef void (*PortVIRetraceHook)(u32 retraceCount, void* user);
typedef void (*PortVIAudioSink)(const short* samples, u32 frames, void* user);
void PortVIInitClock(void);
/* Called at every retrace, first (scripted input, WPAD reports). */
void PortVISetRetraceHook(PortVIRetraceHook hook, void* user);
/* Every AX frame's stereo samples (PORT_AX_FRAME_SAMPLES frames). */
void PortVISetAudioSink(PortVIAudioSink sink, void* user);
/* Real-time pacing (HOST_BUILD.md §30): called before the clock advances to
 * `units` (1/60000 s); a live host sleeps there until the wall clock reaches
 * it. Without a pacer the clock jumps at once (deterministic runs). */
typedef void (*PortVIPacer)(long long units, void* user);
void PortVISetPacer(PortVIPacer pacer, void* user);
unsigned long long PortVIAudioFrames(void);
long long PortVIGameTimeUnits(void); /* 1/60000 s */
int PortVIIsBlack(void);

/* Health_strapArchive (main.dol .data6) is at most this big (host_boot.cpp). */
#define PORT_STRAP_ARCHIVE_MAX 0x20500
/* Test switch: every voice converts with this srcSelect (2: none, one input
 * sample per output) instead of its own; -1 restores the PB's. */
void PortAXSetSrcOverride(int srcSelect);
/* The polyphase table for coefSelect 0..2: 128 phases x 4 taps, oldest first. */
const short* PortAXGetSrcCoefs(unsigned int coefSelect);

#ifdef __cplusplus
}
#endif

#endif
