/**
 * The AX DSP on the host: a software mixer (port/docs/HOST_BUILD.md §21).
 *
 * The CPU half of AX is the decomp's own code, linked unmodified: AX.c
 * (AXInit), AXAlloc.c (voice stacks, priorities, AXAcquireVoice / AXFreeVoice),
 * AXVPB.c (AXSetVoice*, __AXSyncPBs: the copy of every voice's parameter block
 * into the PB array the DSP reads, and back), AXAux.c (the aux bus ring
 * buffers and callbacks), AXSPB.c (depop) and AXProf.c. This file replaces
 * what talks to the hardware:
 *
 *   AXOut.c  __AXOutInit and the frame: PortAXRunFrame does what the AI DMA
 *            interrupt and __AXOutNewFrame do (sync the PBs, run the DSP on
 *            them, service the voice callbacks, process the aux buses, call
 *            the user frame callback), with the DSP work done here.
 *   AXCL.c   the command-list state (mode, master and aux return volumes);
 *            the DSP command list itself is not built: the values it would
 *            carry are captured at the same point of the frame instead.
 *   AI       AIInit / AIRegisterDMACallback: the DMA callback chain is kept
 *            and PortAXRunFrame calls it as the AI interrupt would.
 *
 * The DSP (one frame = 96 samples at 32 kHz, 3 ms), per running voice, after
 * Dolphin's AXWii HLE (Source/Core/Core/HW/DSPHLE/UCodes/AXVoice.h and
 * DSP/DSPAccelerator.cpp; reimplemented, not copied):
 *
 *   accelerator  DSP-ADPCM (nibble addresses; a frame header byte read at
 *                every 16-nibble boundary; prediction
 *                (scale * nibble) + ((0x400 + c1 * yn1 + c2 * yn2) >> 11),
 *                saturated to s16), PCM16 (word addresses) and PCM8 (byte
 *                addresses) through gain >> 11 / gain >> 0; yn1 / yn2 kept in
 *                the PB. After the sample at the end address the address
 *                goes back to the loop address; a looping voice takes the
 *                loop predictor/scale (and, unless it is a stream voice, the
 *                loop history); a one-shot voice stops (state STOP) and reads
 *                zeros for the rest of the frame.
 *   SRC          srcSelect 0: 4-tap polyphase, 128 phases, the coefficient
 *                table chosen by coefSelect (8 / 12 / 16 kHz); the position
 *                advances by the 16.16 ratio before each output, input
 *                samples are read as the integer part crosses, the phase is
 *                (frac >> 9), taps oldest sample first, sum >> 15 saturated.
 *                The 12 kHz table is the decomp's (AXFXSrcCoef.c, exact);
 *                8 / 16 kHz are generated (src/host_ax_coefs.h,
 *                tools/gen_ax_coefs.py; approximations, as in Dolphin's free
 *                DSP ROM). srcSelect 1: linear; 2: none (one input sample per
 *                output). nw4r only uses the polyphase filter.
 *   envelope     x = sat16((x * ve.currentVolume) >> 15), the volume moving
 *                by ve.currentDelta every sample.
 *   filters      LPF (one pole: sat16((a0 * x + b0 * y1) >> 15)) and the Wii
 *                biquad, when on.
 *   mix          for every bus bit of mixerCtrl (main L/R/S, aux A/B/C L/R/S):
 *                s = sat16((x * v) >> 15), added to the bus; v moves by its
 *                delta when the bus's ramp bit is set; the last s is the
 *                bus's depop value.
 *
 * Then per frame: the main buses start from the depop fade of the voices
 * stopped this frame (__AXPrintStudio's values); in stereo mode the previous
 * frame's surround bus is added to L and R; each aux bus with a callback is
 * written to the buffer the CPU processes and the processed buffer of two
 * frames ago comes back into the main buses times its return volume (the
 * AXAux.c ring); the output is sat((main * masterVolume) >> 15) to +-32767,
 * stereo s16. Not emulated: the compressor (a limiter at full scale, table not
 * in the decomp), DPL2 / surround encoding (mixed as stereo), the remote
 * speaker outputs' aux sends and IIR (§37), the ITD (initial time delay)
 * phase shift and the DSP cycle budget's timing (the budget itself is: voices
 * over it are dropped by __AXSyncPBs as on the Wii).
 */

#include <types.h>

#include <revolution/AI.h>
#include <revolution/AX.h>
#include <revolution/AXFX.h>
#include <revolution/OS.h>

#include <port/host.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host_ax_coefs.h"

extern "C" f32* __AXFXGetSrcCoef(u32 i);


namespace {

const int FRAME = AX_SAMPLES_PER_FRAME; // 96
const int MAX_INPUT_SAMPLES = FRAME * 64;

/* The values the command list would carry (captured where __AXNextFrame runs). */
struct CommandList {
    void* auxIn[3];  // DSP writes the aux sends here (NULL: no callback, no aux mix)
    void* auxOut[3]; // DSP reads the processed aux buffers from here
    u16 auxVolume[3];
    u16 masterVolume;
    u32 mode;
};

CommandList sList;
AXOutCallback sUserFrameCallback = NULL;
AIDMACallback sAiCallback = NULL;
bool sAxOutInitialised = false;
u32 sCommandListCycles = 0;

u16 sMasterVolume = AX_MAX_VOLUME;
u16 sAuxVolume[3] = {AX_MAX_VOLUME, AX_MAX_VOLUME, AX_MAX_VOLUME};
s16 sCoef[3][128][4];
bool sCoefReady = false;

/* Buses: main L/R/S, aux A/B/C L/R/S. */
enum { BUS_L, BUS_R, BUS_S, BUS_AL, BUS_AR, BUS_AS, BUS_BL, BUS_BR, BUS_BS,
       BUS_CL, BUS_CR, BUS_CS, BUS_MAX };
s32 sBus[BUS_MAX][FRAME];

/* The Wii Remote speaker outputs (HOST_BUILD.md §37): each voice with `remote` on
 * mixes into four remote buses (rmtMix vMain0-3, ramped as the main mix), taken
 * from the voice after its volume envelope; each frame's 96 samples go down to the
 * remote's 6 kHz (18 samples, averaging 5-6 inputs each) into a ring that
 * AXRmtGetSamples / AXRmtAdvancePtr read (nw4r's RemoteSpeakerManager alarm: 40
 * samples a packet, every 6.67 ms). Not modelled: the remote aux sends and the
 * remote IIR filter. */
const int RMT_FRAME = 18;
const int RMT_RING = 2048;
s32 sRmtBus[AX_RMT_MAX][FRAME];
s16 sRmtRing[AX_RMT_MAX][RMT_RING];
u64 sRmtWrite = 0, sRmtRead = 0;
s32 sSurroundPrev[FRAME];

PortAXVoiceTap sTap = NULL;
int sSrcOverride = -1; // PortAXSetSrcOverride: a test switch, -1 = the PB's srcSelect
void* sTapUser = NULL;
u64 sFrameCount = 0;

inline s16 Sat16(s64 v) {
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return static_cast<s16>(v);
}

inline u8 ReadByte(u32 physical) {
    return *static_cast<const u8*>(OSPhysicalToCached(physical));
}

void InitCoefs() {
    if (sCoefReady) {
        return;
    }
    for (int i = 0; i < 128; i++) {
        const f32* c12 = __AXFXGetSrcCoef(i);
        for (int k = 0; k < 4; k++) {
            sCoef[0][i][k] = sAxSrcCoef8k[i][k];
            sCoef[1][i][k] = static_cast<s16>(c12[k] * 32768.0f);
            sCoef[2][i][k] = sAxSrcCoef16k[i][k];
        }
    }
    sCoefReady = true;
}

/* The accelerator: one voice's sample reader. */
struct Accelerator {
    AXPB* pb;
    u32 cur;
    u32 loop;
    u32 end;
    u16 format;
    u16 predScale;
    s16 yn1;
    s16 yn2;
    bool stopped;

    void Setup(AXPB* p) {
        pb = p;
        cur = static_cast<u32>(p->addr.currentAddressHi) << 16 | p->addr.currentAddressLo;
        loop = static_cast<u32>(p->addr.loopAddressHi) << 16 | p->addr.loopAddressLo;
        end = static_cast<u32>(p->addr.endAddressHi) << 16 | p->addr.endAddressLo;
        format = p->addr.format;
        predScale = p->adpcm.pred_scale;
        yn1 = static_cast<s16>(p->adpcm.yn1);
        yn2 = static_cast<s16>(p->adpcm.yn2);
        stopped = false;
    }

    s16 Read() {
        if (stopped) {
            return 0;
        }
        const s16* coefs = reinterpret_cast<const s16*>(pb->adpcm.a);
        s16 out;
        switch (format) {
        case AX_SAMPLE_FORMAT_DSP_ADPCM: {
            if ((cur & 15) == 0) {
                predScale = ReadByte(cur >> 1);
                cur += 2;
            }
            u8 byte = ReadByte(cur >> 1);
            int nibble = (cur & 1) ? (byte & 0xF) : (byte >> 4);
            if (nibble >= 8) {
                nibble -= 16;
            }
            int scale = 1 << (predScale & 0xF);
            int idx = (predScale >> 4) & 7;
            s32 c1 = coefs[idx * 2 + 0];
            s32 c2 = coefs[idx * 2 + 1];
            s32 v = scale * nibble + ((0x400 + c1 * yn1 + c2 * yn2) >> 11);
            out = Sat16(v);
            break;
        }
        case AX_SAMPLE_FORMAT_PCM_S16:
        case AX_SAMPLE_FORMAT_PCM_S8: {
            s32 raw;
            int shift;
            if (format == AX_SAMPLE_FORMAT_PCM_S16) {
                raw = static_cast<s16>(ReadByte(cur * 2) << 8 | ReadByte(cur * 2 + 1));
                shift = 11;
            } else {
                raw = ReadByte(cur);
                shift = 0;
            }
            int idx = (predScale >> 4) & 7;
            s32 c1 = coefs[idx * 2 + 0];
            s32 c2 = coefs[idx * 2 + 1];
            s32 v = ((static_cast<s32>(pb->adpcm.gain) * raw) >> shift) + ((c1 * yn1) >> shift) +
                    ((c2 * yn2) >> shift);
            out = static_cast<s16>(v);
            break;
        }
        default:
            std::fprintf(stderr, "host_ax: unknown sample format %u\n", format);
            std::abort();
        }
        yn2 = yn1;
        yn1 = out;

        if (cur == end) {
            cur = loop;
            if (pb->addr.loopFlag) {
                predScale = pb->adpcmLoop.loop_pred_scale;
                if (pb->type != AX_VOICE_STREAM) {
                    yn1 = static_cast<s16>(pb->adpcmLoop.loop_yn1);
                    yn2 = static_cast<s16>(pb->adpcmLoop.loop_yn2);
                }
            } else {
                pb->state = AX_VOICE_STOP;
                stopped = true;
            }
        } else {
            cur++;
        }
        return out;
    }

    void WriteBack() {
        pb->addr.currentAddressHi = static_cast<u16>(cur >> 16);
        pb->addr.currentAddressLo = static_cast<u16>(cur);
        pb->adpcm.pred_scale = predScale;
        pb->adpcm.yn1 = static_cast<u16>(yn1);
        pb->adpcm.yn2 = static_cast<u16>(yn2);
    }
};

s16 sInput[MAX_INPUT_SAMPLES];
u32 sInputCount;

inline s16 Fetch(Accelerator& acc) {
    s16 s = acc.Read();
    if (sInputCount < MAX_INPUT_SAMPLES) {
        sInput[sInputCount] = s;
    }
    sInputCount++;
    return s;
}

void Resample(AXPB* pb, Accelerator& acc, s16* out) {
    sInputCount = 0;
    u16 srcSelect = sSrcOverride >= 0 ? static_cast<u16>(sSrcOverride) : pb->srcSelect;
    if (srcSelect == 2) {
        // No SRC: one input sample per output; the history is the last outputs.
        for (int i = 0; i < FRAME; i++) {
            out[i] = Fetch(acc);
        }
        for (int k = 0; k < 4; k++) {
            pb->src.last_samples[k] = static_cast<u16>(out[FRAME - 4 + k]);
        }
        return;
    }

    u32 ratio = static_cast<u32>(pb->src.ratioHi) << 16 | pb->src.ratioLo;
    u32 pos = pb->src.currentAddressFrac;
    s16 ring[4];
    for (int k = 0; k < 4; k++) {
        ring[k] = static_cast<s16>(pb->src.last_samples[k]);
    }
    u32 idx = 0; // ring[idx & 3] is the oldest sample

    const s16(*coef)[4] = sCoef[pb->coefSelect < 3 ? pb->coefSelect : 0];
    for (int i = 0; i < FRAME; i++) {
        pos += ratio;
        while (pos >= 0x10000) {
            ring[idx & 3] = Fetch(acc);
            idx++;
            pos -= 0x10000;
        }
        if (srcSelect == 0) {
            const s16* c = coef[(pos & 0xFFFF) >> 9];
            s64 sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += static_cast<s64>(ring[(idx + k) & 3]) * c[k];
            }
            out[i] = Sat16(sum >> 15);
        } else {
            u32 f = pos & 0xFFFF;
            if (f == 0) {
                out[i] = ring[idx & 3];
            } else {
                s32 s0 = ring[idx & 3];
                s32 s1 = ring[(idx + 1) & 3];
                out[i] = static_cast<s16>((s0 * static_cast<s32>(0x10000 - f) + s1 * static_cast<s32>(f)) >> 16);
            }
        }
    }
    for (int k = 0; k < 4; k++) {
        pb->src.last_samples[k] = static_cast<u16>(ring[(idx + k) & 3]);
    }
    pb->src.currentAddressFrac = static_cast<u16>(pos & 0xFFFF);
}

void MixBus(s32* bus, const s16* x, u16* vol, u16 delta, bool ramp, s16* dpop) {
    u16 v = *vol;
    u16 d = ramp ? delta : 0;
    s16 s = 0;
    for (int i = 0; i < FRAME; i++) {
        s = Sat16((static_cast<s64>(x[i]) * v) >> 15);
        bus[i] += s;
        v = static_cast<u16>(v + d);
    }
    *vol = v;
    *dpop = s;
}

void ProcessVoice(u32 index, AXPB* pb) {
    AXPB before;
    if (sTap != NULL) {
        std::memcpy(&before, pb, sizeof(AXPB));
    }

    Accelerator acc;
    acc.Setup(pb);
    s16 src[FRAME];
    Resample(pb, acc, src);
    acc.WriteBack();

    s16 x[FRAME];
    u16 vol = pb->ve.currentVolume;
    for (int i = 0; i < FRAME; i++) {
        x[i] = Sat16((static_cast<s64>(src[i]) * vol) >> 15);
        vol = static_cast<u16>(vol + pb->ve.currentDelta);
    }
    pb->ve.currentVolume = vol;
    s16 xRmt[FRAME];
    std::memcpy(xRmt, x, sizeof xRmt);

    if (pb->lpf.on) {
        s16 y = static_cast<s16>(pb->lpf.yn1);
        for (int i = 0; i < FRAME; i++) {
            y = Sat16((static_cast<s64>(pb->lpf.a0) * x[i] + static_cast<s64>(pb->lpf.b0) * y) >> 15);
            x[i] = y;
        }
        pb->lpf.yn1 = static_cast<u16>(y);
    }
    if (pb->biquad.on) {
        AXPBBIQUAD& b = pb->biquad;
        s16 x1 = b.xn1, x2 = b.xn2, y1 = b.yn1, y2 = b.yn2;
        for (int i = 0; i < FRAME; i++) {
            s64 a = static_cast<s64>(static_cast<s16>(b.b0)) * x[i] +
                    static_cast<s64>(static_cast<s16>(b.b1)) * x1 +
                    static_cast<s64>(static_cast<s16>(b.b2)) * x2 +
                    static_cast<s64>(static_cast<s16>(b.a1)) * y1 +
                    static_cast<s64>(static_cast<s16>(b.a2)) * y2;
            a <<= 2;
            a += (a & 0x10000) ? 0x8000 : 0x7FFF;
            a >>= 16;
            s16 y = Sat16(a);
            x2 = x1;
            x1 = x[i];
            y2 = y1;
            y1 = y;
            x[i] = y;
        }
        b.xn1 = static_cast<u16>(x1);
        b.xn2 = static_cast<u16>(x2);
        b.yn1 = static_cast<u16>(y1);
        b.yn2 = static_cast<u16>(y2);
    }

    if (pb->remote) {
        // the remote speakers (before the main filters: the remote has its own IIR)
        u16* rv[AX_RMT_MAX] = {&pb->rmtMix.vMain0, &pb->rmtMix.vMain1, &pb->rmtMix.vMain2, &pb->rmtMix.vMain3};
        const u16 rd[AX_RMT_MAX] = {pb->rmtMix.vDeltaMain0, pb->rmtMix.vDeltaMain1, pb->rmtMix.vDeltaMain2,
                                    pb->rmtMix.vDeltaMain3};
        s16* rp[AX_RMT_MAX] = {&pb->rmtDpop.aMain0, &pb->rmtDpop.aMain1, &pb->rmtDpop.aMain2, &pb->rmtDpop.aMain3};
        for (int c = 0; c < AX_RMT_MAX; c++) {
            if (pb->rmtMixerCtrl & (1u << (4 * c))) {
                MixBus(sRmtBus[c], xRmt, rv[c], rd[c], (pb->rmtMixerCtrl & (2u << (4 * c))) != 0, rp[c]);
            }
        }
    }

    u32 ctrl = pb->mixerCtrl;
    AXPBMIX& m = pb->mix;
    AXPBDPOP& dp = pb->dpop;
    struct Route {
        u32 on;
        u32 ramp;
        int bus;
        u16* vol;
        u16 delta;
        s16* dpop;
    };
    const Route routes[] = {
        {AX_MIXER_CTRL_L, AX_MIXER_CTRL_DELTA, BUS_L, &m.vL, m.vDeltaL, &dp.aL},
        {AX_MIXER_CTRL_R, AX_MIXER_CTRL_DELTA, BUS_R, &m.vR, m.vDeltaR, &dp.aR},
        {AX_MIXER_CTRL_S, AX_MIXER_CTRL_DELTA_S, BUS_S, &m.vS, m.vDeltaS, &dp.aS},
        {AX_MIXER_CTRL_AL, AX_MIXER_CTRL_DELTA_A, BUS_AL, &m.vAuxAL, m.vDeltaAuxAL, &dp.aAuxAL},
        {AX_MIXER_CTRL_AR, AX_MIXER_CTRL_DELTA_A, BUS_AR, &m.vAuxAR, m.vDeltaAuxAR, &dp.aAuxAR},
        {AX_MIXER_CTRL_AS, AX_MIXER_CTRL_DELTA_AS, BUS_AS, &m.vAuxAS, m.vDeltaAuxAS, &dp.aAuxAS},
        {AX_MIXER_CTRL_BL, AX_MIXER_CTRL_DELTA_B, BUS_BL, &m.vAuxBL, m.vDeltaAuxBL, &dp.aAuxBL},
        {AX_MIXER_CTRL_BR, AX_MIXER_CTRL_DELTA_B, BUS_BR, &m.vAuxBR, m.vDeltaAuxBR, &dp.aAuxBR},
        {AX_MIXER_CTRL_BS, AX_MIXER_CTRL_DELTA_BS, BUS_BS, &m.vAuxBS, m.vDeltaAuxBS, &dp.aAuxBS},
        {AX_MIXER_CTRL_CL, AX_MIXER_CTRL_DELTA_C, BUS_CL, &m.vAuxCL, m.vDeltaAuxCL, &dp.aAuxCL},
        {AX_MIXER_CTRL_CR, AX_MIXER_CTRL_DELTA_C, BUS_CR, &m.vAuxCR, m.vDeltaAuxCR, &dp.aAuxCR},
        {AX_MIXER_CTRL_CS, AX_MIXER_CTRL_DELTA_CS, BUS_CS, &m.vAuxCS, m.vDeltaAuxCS, &dp.aAuxCS},
    };
    for (size_t r = 0; r < sizeof(routes) / sizeof(routes[0]); r++) {
        if (ctrl & routes[r].on) {
            MixBus(sBus[routes[r].bus], x, routes[r].vol, routes[r].delta,
                   (ctrl & routes[r].ramp) != 0, routes[r].dpop);
        }
    }

    if (sTap != NULL) {
        PortAXVoiceTrace t;
        t.frame = sFrameCount;
        t.index = index;
        t.pbBefore = &before;
        t.pbAfter = pb;
        t.input = sInput;
        t.inputCount = sInputCount < MAX_INPUT_SAMPLES ? sInputCount : MAX_INPUT_SAMPLES;
        t.src = src;
        t.out = x;
        sTap(&t, sTapUser);
    }
}

void StartBus(int bus, s32 value, s16 delta) {
    s32 v = value;
    for (int i = 0; i < FRAME; i++) {
        sBus[bus][i] = v;
        v += delta;
    }
}

/* The DSP's frame, as the command list orders it. */
void RunDsp(s16* out) {
    InitCoefs();
    const AXSTUDIO* st = __AXGetStudio();
    StartBus(BUS_L, st->L, st->dL);
    StartBus(BUS_R, st->R, st->dR);
    StartBus(BUS_S, st->S, st->dS);
    StartBus(BUS_AL, st->AuxAL, st->dAuxAL);
    StartBus(BUS_AR, st->AuxAR, st->dAuxAR);
    StartBus(BUS_AS, st->AuxAS, st->dAuxAS);
    StartBus(BUS_BL, st->AuxBL, st->dAuxBL);
    StartBus(BUS_BR, st->AuxBR, st->dAuxBR);
    StartBus(BUS_BS, st->AuxBS, st->dAuxBS);
    StartBus(BUS_CL, st->AuxCL, st->dAuxCL);
    StartBus(BUS_CR, st->AuxCR, st->dAuxCR);
    StartBus(BUS_CS, st->AuxCS, st->dAuxCS);

    std::memset(sRmtBus, 0, sizeof sRmtBus);

    // ADD_TO_LR: last frame's surround into L and R.
    for (int i = 0; i < FRAME; i++) {
        sBus[BUS_L][i] += sSurroundPrev[i];
        sBus[BUS_R][i] += sSurroundPrev[i];
    }

    // PROCESS: every PB, in list order.
    AXPB* pbs = __AXGetPBs();
    for (u32 i = 0; i < AX_VOICE_MAX; i++) {
        if (pbs[i].state == AX_VOICE_RUN) {
            ProcessVoice(i, &pbs[i]);
        }
    }

    // MIX_AUXA/B/C: upload the sends for the CPU, mix the processed returns.
    for (int a = 0; a < 3; a++) {
        if (sList.auxIn[a] == NULL) {
            continue;
        }
        s32* in = static_cast<s32*>(sList.auxIn[a]);
        const s32* ret = static_cast<const s32*>(sList.auxOut[a]);
        int base = BUS_AL + a * 3;
        for (int c = 0; c < 3; c++) {
            std::memcpy(in + c * FRAME, sBus[base + c], FRAME * sizeof(s32));
        }
        for (int c = 0; c < 3; c++) {
            for (int i = 0; i < FRAME; i++) {
                sBus[BUS_L + c][i] +=
                    static_cast<s32>((static_cast<s64>(ret[c * FRAME + i]) * sList.auxVolume[a]) >> 15);
            }
        }
    }

    // The remote outputs at 6 kHz
    for (int c = 0; c < AX_RMT_MAX; c++) {
        for (int j = 0; j < RMT_FRAME; j++) {
            const int a = j * FRAME / RMT_FRAME, b = (j + 1) * FRAME / RMT_FRAME;
            s64 sum = 0;
            for (int i = a; i < b; i++) {
                sum += sRmtBus[c][i];
            }
            sRmtRing[c][(sRmtWrite + j) % RMT_RING] = Sat16(sum / (b - a));
        }
    }
    sRmtWrite += RMT_FRAME;
    if (sRmtWrite - sRmtRead > RMT_RING - RMT_FRAME) {
        sRmtRead = sRmtWrite - (RMT_RING - RMT_FRAME); // nobody reads: keep the newest
    }

    // OUTPUT: surround kept for the next frame, L/R through the master volume.
    std::memcpy(sSurroundPrev, sBus[BUS_S], sizeof(sSurroundPrev));
    for (int i = 0; i < FRAME; i++) {
        s64 l = (static_cast<s64>(sBus[BUS_L][i]) * sList.masterVolume) >> 15;
        s64 r = (static_cast<s64>(sBus[BUS_R][i]) * sList.masterVolume) >> 15;
        l = l > 32767 ? 32767 : (l < -32767 ? -32767 : l);
        r = r > 32767 ? 32767 : (r < -32767 ? -32767 : r);
        if (out != NULL) {
            out[2 * i] = static_cast<s16>(l);
            out[2 * i + 1] = static_cast<s16>(r);
        }
    }
}

/* What __AXNextFrame does at the end of each frame: capture the next frame's
 * command list (and its cycle count, which __AXSyncPBs budgets with). */
void NextFrame() {
    void* p;
    sCommandListCycles = 7811 + 4126 + 733; // SETUP, ADD_TO_LR
    __AXGetAuxAInput(&p);
    sList.auxIn[0] = p;
    __AXGetAuxAOutput(&sList.auxOut[0]);
    __AXGetAuxBInput(&p);
    sList.auxIn[1] = p;
    __AXGetAuxBOutput(&sList.auxOut[1]);
    __AXGetAuxCInput(&p);
    sList.auxIn[2] = p;
    __AXGetAuxCOutput(&sList.auxOut[2]);
    for (int a = 0; a < 3; a++) {
        sList.auxVolume[a] = sAuxVolume[a];
        if (sList.auxIn[a] != NULL) {
            sCommandListCycles += 2235;
        }
    }
    sList.masterVolume = sMasterVolume;
    sList.mode = __AXClMode;
    sCommandListCycles += 1850 + 409 + 1172 + 30; // COMPRESSOR, WM_OUTPUT, OUTPUT, END
}

s16* sFrameOut = NULL;

/* __AXOutNewFrame with the DSP run in place. */
void NewFrame() {
    __AXSyncPBs(24400);
    __AXPrintStudio();
    RunDsp(sFrameOut);
    __AXServiceCallbackStack();
    __AXProcessAux();
    if (sUserFrameCallback != NULL) {
        sUserFrameCallback();
    }
    NextFrame();
    sFrameCount++;
}

} // namespace

extern "C" {

/* AXOut */
void __AXOutInit(u32 mode) {
    (void)mode;
    std::memset(sSurroundPrev, 0, sizeof(sSurroundPrev));
    sUserFrameCallback = NULL;
    NextFrame();
    sAiCallback = NewFrame;
    sAxOutInitialised = true;
}

AXOutCallback AXRegisterCallback(AXOutCallback callback) {
    BOOL enabled = OSDisableInterrupts();
    AXOutCallback old = sUserFrameCallback;
    sUserFrameCallback = callback;
    OSRestoreInterrupts(enabled);
    return old;
}

s32 AXRmtGetSamplesLeft(void) {
    return static_cast<s32>(sRmtWrite - sRmtRead);
}
s32 AXRmtGetSamples(s32 chan, s16* out, s32 num) {
    const s32 left = static_cast<s32>(sRmtWrite - sRmtRead);
    if (num > left) {
        num = left;
    }
    for (s32 i = 0; i < num; i++) {
        out[i] = chan >= 0 && chan < AX_RMT_MAX ? sRmtRing[chan][(sRmtRead + i) % RMT_RING] : 0;
    }
    return num;
}
s32 AXRmtAdvancePtr(s32 num) {
    const s32 left = static_cast<s32>(sRmtWrite - sRmtRead);
    if (num > left) {
        num = left;
    }
    sRmtRead += static_cast<u64>(num);
    return num;
}

/* AXCL */
u32 __AXClMode = AX_OUTPUT_STEREO;

u32 __AXGetCommandListCycles(void) {
    return sCommandListCycles;
}
void __AXClInit(void) {
    __AXClMode = AX_OUTPUT_STEREO;
    sMasterVolume = AX_MAX_VOLUME;
    sAuxVolume[0] = sAuxVolume[1] = sAuxVolume[2] = AX_MAX_VOLUME;
}
void AXSetMode(u32 mode) {
    __AXClMode = mode;
}
u32 AXGetMode(void) {
    return __AXClMode;
}
u16 AXGetAuxAReturnVolume(void) {
    return sAuxVolume[0];
}
u16 AXGetAuxBReturnVolume(void) {
    return sAuxVolume[1];
}
u16 AXGetAuxCReturnVolume(void) {
    return sAuxVolume[2];
}
void AXSetMasterVolume(u16 volume) {
    sMasterVolume = volume > AX_MAX_VOLUME ? AX_MAX_VOLUME : volume;
}
void AXSetAuxAReturnVolume(u16 volume) {
    sAuxVolume[0] = volume;
}
void AXSetAuxBReturnVolume(u16 volume) {
    sAuxVolume[1] = volume;
}
void AXSetAuxCReturnVolume(u16 volume) {
    sAuxVolume[2] = volume;
}

/* AI: the DMA interrupt is PortAXRunFrame. */
void AIInit(void* stack) {
    (void)stack;
}
AIDMACallback AIRegisterDMACallback(AIDMACallback callback) {
    BOOL enabled = OSDisableInterrupts();
    AIDMACallback old = sAiCallback;
    sAiCallback = callback;
    OSRestoreInterrupts(enabled);
    return old;
}
void AIInitDMA(void* buffer, u32 length) {
    (void)buffer;
    (void)length;
}
void AIStartDMA(void) {}
u32 AIGetDMABytesLeft(void) {
    return 0;
}
u32 AIGetDSPSampleRate(void) {
    return 1; // AI_DSP_32KHZ
}
void AISetDSPSampleRate(u32 rate) {
    (void)rate;
}

/* Host entry points (port/host.h). */
int PortAXIsInitialised(void) {
    return sAxOutInitialised ? 1 : 0;
}

void PortAXRunFrame(short* out) {
    if (!sAxOutInitialised) {
        std::fprintf(stderr, "host_ax: PortAXRunFrame before AXInit\n");
        std::abort();
    }
    sFrameOut = out;
    PortOSEnterInterrupt();
    if (sAiCallback != NULL) {
        sAiCallback();
    }
    PortOSLeaveInterrupt();
    sFrameOut = NULL;
}

void PortAXSetSrcOverride(int srcSelect) {
    sSrcOverride = srcSelect;
}

void PortAXSetVoiceTap(PortAXVoiceTap tap, void* user) {
    sTap = tap;
    sTapUser = user;
}

const short* PortAXGetSrcCoefs(unsigned int coefSelect) {
    InitCoefs();
    return coefSelect < 3 ? &sCoef[coefSelect][0][0] : NULL;
}

} // extern "C"
