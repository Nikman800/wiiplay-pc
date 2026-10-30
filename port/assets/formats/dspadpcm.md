# Wii Play wave decoding (DSP-ADPCM, PCM16, PCM8)

`port/assets/tools/dspadpcm.py` decodes every wave the game can play to signed 16-bit PCM: the 14 loose `RSTM` streams and every wave of the `RBNK` (1612) and `RWSD` (22) files embedded in the 11 `RSAR` archives (10 under `Sound/`, plus `HomeButtonSe.brsar` inside `Common/PartyStatic/common.carc`). Container parsing comes from `fmt_sound.py` (see `sound.md`); this note covers only where the sample data is and how it decodes.

    python3 port/assets/tools/dspadpcm.py --check ROOT        # summary, exit 1 on any error
    python3 port/assets/tools/dspadpcm.py --dump DIR ROOT     # RIFF PCM16 WAVs; DIR must be outside the repo

Output channels are kept separate internally and interleaved only when a WAV is written. Each embedded file is decoded once even when it is placed in several groups.

## Codecs

`WaveFile::Format` / `StrmDataInfo.format`: 0 = PCM8 (signed 8-bit; widened with `<< 8`), 1 = PCM16 (big-endian s16), 2 = DSP-ADPCM.

### DSP-ADPCM

`AdpcmInfo` (`include/nw4r/snd/snd_Types.h`), 0x30 bytes: `s16 coef[16]` (8 predictor pairs), `u16 gain`, `u16 pred_scale`, `s16 yn1`, `s16 yn2`, `u16 loop_pred_scale`, `s16 loop_yn1`, `s16 loop_yn2`, padding.

Data is 8-byte frames. Byte 0 is `predictor << 4 | scale` (predictor 0..7), bytes 1..7 hold 14 signed 4-bit samples, high nibble first. With `c1 = coef[2*predictor]`, `c2 = coef[2*predictor+1]`, and history starting at `yn1`/`yn2`:

    sample = clamp16((c1*yn1 + c2*yn2 + ((nibble << scale) << 11) + 1024) >> 11)
    yn2 = yn1; yn1 = sample

`>>` is an arithmetic shift. `pred_scale` equals the first frame header, `loop_pred_scale` the header of the frame that holds the loop start, and `loop_yn1`/`loop_yn2` equal the decoded samples just before the loop start; all three hold for every wave on the disc, which makes them a bit-exact test of a decoder.

## Bank and wave-sound waves (inside BRSAR)

`WaveFileReader::ReadWaveParam` (`src/nw4r/snd/snd_WaveFile.cpp`):

- `waveAddr = GroupInfo.waveDataOffset + GroupItemInfo.waveDataOffset`. Both are absolute offsets in the RSAR (as the group file offsets are). The region holds raw sample data, not an `RWAR`.
- Channel `c` starts at `waveAddr + WaveInfo.dataOffset + WaveChannelInfo[c].channelDataOffset`.
- `WaveChannelInfo.adpcmOffset` is relative to the `WaveInfo` itself, inside the embedded RBNK/RWSD (not the wave region). The WaveInfo for RBNK wave `i` is at `waveBlock + 8 + ref[i].value`; for RWSD it is at `waveBlock + offset[i]`.
- `WaveInfo.loopStart` and `loopEnd` are DSP addresses, not sample numbers. `AxVoice::GetSampleByDspAddress` (`src/nw4r/snd/snd_AxVoice.cpp`) converts them; for DSP-ADPCM, `samples = addr % 16 + addr / 16 * 14 - 2` (an address counts nibbles, 16 per frame including the 2 header nibbles); for PCM the address is the sample number. The wave length is `sample(loopEnd) + 1` for looping and one-shot waves alike, and the loop is `[sample(loopStart), sample(loopEnd) + 1)`.

The decoder takes exactly that many samples and checks that they fit inside the group item's wave region (`waveDataSize`).

## Streams (BRSTM)

From `StrmPlayer::UpdateLoadingBlockIndex` / `LoadStreamData` (`src/nw4r/snd/snd_StrmPlayer.cpp`): block `b` of channel `c` starts at

    dataOffset + b * (blockHeaderOffset + blockSize * numChannels) + blockHeaderOffset + c * RoundUp(size, 32)

where `size` is `blockSize`, or `lastBlockPaddedSize` for the last block. Blocks hold `blockSamples` samples (`lastBlockSamples` for the last); total length is `(numBlocks - 1) * blockSamples + lastBlockSamples`, and `loopStart`/`loopEnd` are already sample numbers. The player copies the blocks into one ring buffer per channel and plays it as one voice, so DSP-ADPCM history carries across block boundaries: start each channel from its `AdpcmInfo.yn1`/`yn2` and never reset it.

The `ADPC` block (`StrmFileLoader::ReadAdpcBlockData`) is only used to seek to a start offset. It stores `s16 yn1, yn2` per channel every `adpcmDataInterval` (1024) samples. On this disc the values are the encoder's source-PCM history, close to but mostly not equal to the decoded history (mean difference 40 vs mean amplitude 2238), and the table is sized by bytes, not samples, so it ends partway through each stream. A full decode does not need it. The runtime picks the entry by `startSample / blockSamples`, which does not match the 1024-sample spacing; that only matters for start-offset playback.

## What is on the disc

| Container | Waves | Codecs | Channels | Seconds |
| --- | --- | --- | --- | --- |
| BRSTM | 14 (4 are duplicates under `RPWlyScene/sound/sound/`) | DSP-ADPCM 10, PCM16 4 | stereo | 182.5 |
| RBNK | 1612 | DSP-ADPCM 961, PCM16 643, PCM8 8 | mono 1418, stereo 194 | 1180.4 |
| RWSD | 22 | DSP-ADPCM 22 | mono 12, stereo 10 | 171.7 |

Rates: streams 32000/44100; bank waves 11000 to 44100 (mostly 32000). 9 waves clip on more than 1% of samples; 7 are PCM16, stored that way, and 2 are loud DSP-ADPCM content. See `dspadpcm.report.txt`.

## PC port notes

Decode once at load (or offline) to native-endian s16; nothing needs the DSP format at runtime. Loops should play `[loopStart, loopEnd)` of the decoded PCM; with decoded data the `loop_yn` context and the ADPC table are not needed.
