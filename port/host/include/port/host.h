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

/**
 * Swaps the UTF-16BE text of a loaded BMG (MESGbmg1, 16-bit encoding) to host
 * byte order in place. Tag payloads stay big-endian (EGG::MsgRes::analyzeTag
 * reads them with port_load_be32 on the host). Call once per buffer, after
 * loading and before constructing EGG::MsgRes / RPSysMessage on it. Returns 1
 * on success, 0 if the buffer is not a 16-bit BMG or a tag is malformed.
 */
int PortSwapBmgText(void* pBmg);

/*
 * Threads (src/host_thread.cpp, HOST_BUILD.md §21): cooperative and
 * deterministic. Interrupt context defers rescheduling to its end;
 * PortOSIdle lets every ready thread run until all of them block (the main
 * thread waiting for the next retrace).
 */
void PortOSEnterInterrupt(void);
void PortOSLeaveInterrupt(void);
void PortOSIdle(void);

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
/* Test switch: every voice converts with this srcSelect (2: none, one input
 * sample per output) instead of its own; -1 restores the PB's. */
void PortAXSetSrcOverride(int srcSelect);
/* The polyphase table for coefSelect 0..2: 128 phases x 4 taps, oldest first. */
const short* PortAXGetSrcCoefs(unsigned int coefSelect);

#ifdef __cplusplus
}
#endif

#endif
