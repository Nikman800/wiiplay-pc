/**
 * The Wii Remote speaker on the host (port/docs/HOST_BUILD.md §37): the remote's
 * 4-bit ADPCM decoded and mixed into the host's audio output.
 *
 * The games and the HOME Menu encode the speaker's sound with WENC
 * (src/revolution/WENC/wenc.c: 40 samples of 6 kHz mono in 20 bytes a packet,
 * the first sample in the high nibble) and send it with WPADSendStreamData.
 * PortWencDecode is the matching decoder: it reproduces exactly the
 * reconstruction the encoder tracks (its xn), so the decoded sound differs from
 * the encoder's input only by the codec's own quantisation. The encoder starts
 * from xn 0, dl 127 when the speaker is turned on (WPAD_SPEAKER_ON), as the
 * remote's decoder does.
 */
#ifndef PORT_SPEAKER_H
#define PORT_SPEAKER_H

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SPEAKER_RATE 6000 /* samples per second (WPAD's speaker set-up: 12 MHz / 2000) */

typedef struct PortWencDecoder {
    s32 xn; /* the last sample */
    s32 dl; /* the step size */
} PortWencDecoder;

void PortWencDecoderReset(PortWencDecoder* d);
/* Decodes `samples` samples ((samples + 1) / 2 bytes) into out; returns samples. */
s32 PortWencDecode(PortWencDecoder* d, const u8* adpcm, s32 samples, s16* out);

/* The live mixer: per remote a decoder and a queue of 6 kHz samples. */
void PortSpeakerReset(s32 chan);                      /* speaker on: a fresh decoder, queue cleared */
void PortSpeakerPacket(s32 chan, const void* data, u16 len); /* a packet WPAD accepted */
/* Adds the remotes' sound to `frames` stereo s16 samples at 32 kHz (resampled
 * from 6 kHz), times gain; consumes frames * 6000 / 32000 samples a remote. */
void PortSpeakerMix(s16* stereo, u32 frames, f32 gain);
/* Packets received, output samples (32 kHz) with speaker sound mixed in. */
void PortSpeakerStats(u32* packets, u32* mixed);

#ifdef __cplusplus
}
#endif

#endif
