/**
 * The Wii Remote speaker's ADPCM decoder and the live mixer
 * (include/port/speaker.h, port/docs/HOST_BUILD.md §37). Host code; no
 * allocation.
 */
#include <port/speaker.h>

#include <cstring>

namespace {

// WENCGetEncodeData's step size table (wenc.c)
const double kWencTable[8] = {0.89843750, 0.89843750, 0.89843750, 0.89843750,
                              1.19921875, 1.59765625, 2.00000000, 2.39843750};

const int QUEUE = 4096;     // samples a remote (0.68 s)
const int PREFILL = 120;    // samples queued before a remote's sound starts (20 ms)
struct Channel {
    PortWencDecoder dec;
    s16 q[QUEUE];
    u32 head, count;        // read position, samples queued
    bool started;           // playing (PREFILL reached; until the queue runs dry)
    f32 phase;              // position between q[head] and the next sample
    s16 last;
};
Channel sChan[4];
u32 sPackets = 0, sMixed = 0;

} // namespace

extern "C" {

void PortWencDecoderReset(PortWencDecoder* d) {
    d->xn = 0;
    d->dl = 127;
}

s32 PortWencDecode(PortWencDecoder* d, const u8* adpcm, s32 samples, s16* out) {
    s32 xn = d->xn, dl = d->dl;
    for (s32 i = 0; i < samples; i++) {
        const u32 n = (adpcm[i / 2] >> ((i & 1) == 0 ? 4 : 0)) & 0xF;
        const s32 l3 = (n >> 3) & 1, l2 = (n >> 2) & 1, l1 = (n >> 1) & 1, l0 = n & 1;
        // the encoder's reconstruction, step for step
        const s32 dlh = dl / 2, dlq = dlh / 2, dlx = dlq / 2;
        s32 qn = (1 - l3 * 2) * (dl * l2 + dlh * l1 + dlq * l0 + dlx);
        if (qn > 0xFFFF) {
            qn = 0xFFFF;
        }
        if (qn < -0x10000) {
            qn = -0x10000;
        }
        s32 xnc = xn + qn;
        if (xnc > 0x7FFF) {
            xnc = 0x7FFF;
        }
        if (xnc < -0x8000) {
            xnc = -0x8000;
        }
        xn = xnc;
        out[i] = static_cast<s16>(xn);
        dl = static_cast<s32>(dl * kWencTable[l2 * 4 + l1 * 2 + l0]); // `dl *= table[index]`
        if (dl <= 127) {
            dl = 127;
        }
        if (dl >= 0x6000) {
            dl = 0x6000;
        }
    }
    d->xn = xn;
    d->dl = dl;
    return samples;
}

void PortSpeakerReset(s32 chan) {
    if (chan < 0 || chan >= 4) {
        return;
    }
    Channel& c = sChan[chan];
    std::memset(&c, 0, sizeof c);
    PortWencDecoderReset(&c.dec);
}

void PortSpeakerPacket(s32 chan, const void* data, u16 len) {
    if (chan < 0 || chan >= 4 || data == nullptr) {
        return;
    }
    Channel& c = sChan[chan];
    sPackets++;
    if (c.dec.dl == 0) {
        PortWencDecoderReset(&c.dec); // never reset (no WPAD_SPEAKER_ON seen)
    }
    s16 pcm[2 * 255];
    const s32 n = PortWencDecode(&c.dec, static_cast<const u8*>(data), len * 2 > 510 ? 510 : len * 2, pcm);
    for (s32 i = 0; i < n; i++) {
        if (c.count == QUEUE) {
            // the output is not consuming (no audio device): drop the oldest
            c.head = (c.head + 1) % QUEUE;
            c.count--;
        }
        c.q[(c.head + c.count) % QUEUE] = pcm[i];
        c.count++;
    }
}

void PortSpeakerMix(s16* stereo, u32 frames, f32 gain) {
    if (gain <= 0.0f) {
        // muted: still consume, so that unmuting does not play a backlog
        for (Channel& c : sChan) {
            const u32 take = frames * PORT_SPEAKER_RATE / 32000;
            const u32 k = c.count < take ? c.count : take;
            c.head = (c.head + k) % QUEUE;
            c.count -= k;
        }
        return;
    }
    const f32 step = static_cast<f32>(PORT_SPEAKER_RATE) / 32000.0f;
    for (Channel& c : sChan) {
        if (!c.started) {
            if (c.count < PREFILL) {
                continue;
            }
            c.started = true;
            c.phase = 0.0f;
        }
        for (u32 i = 0; i < frames; i++) {
            if (c.count < 2) {
                // ran dry: the sound ended (or a gap); wait for PREFILL again
                c.started = false;
                c.count = 0;
                break;
            }
            sMixed++;
            const s32 a = c.q[c.head], b = c.q[(c.head + 1) % QUEUE];
            const f32 v = (a + (b - a) * c.phase) * gain;
            for (int ch = 0; ch < 2; ch++) {
                s32 o = stereo[2 * i + ch] + static_cast<s32>(v);
                stereo[2 * i + ch] = static_cast<s16>(o > 32767 ? 32767 : (o < -32768 ? -32768 : o));
            }
            c.phase += step;
            while (c.phase >= 1.0f) {
                c.phase -= 1.0f;
                c.head = (c.head + 1) % QUEUE;
                c.count--;
            }
        }
    }
}

void PortSpeakerStats(u32* packets, u32* mixed) {
    *packets = sPackets;
    *mixed = sMixed;
}

} // extern "C"
