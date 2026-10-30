# Wii Play sound formats

This note covers `RSAR` sound archives and loose `RSTM` streams. All multibyte fields are big-endian. File and block headers use `u32 signature`, `u16 byteOrder`, `u16 version`, `u32 fileSize`, `u16 headerSize`, `u16 dataBlocks`; `byteOrder` is `0xFEFF`, `headerSize` is `0x40`. A block starts with `u32 kind`, `u32 size`.

## `DataRef` and tables

A `DataRef` is `u8 refType`, `u8 dataType`, `u16 reserved`, `u32 value`. On this disc `refType` is `REFTYPE_OFFSET`, so `value` is added to the containing struct/table. The runtime resolves it in `nw4r::snd::Util::GetDataRefAddressImpl` (`src/nw4r/snd/snd_Util.cpp:98`); `REFTYPE_ADDRESS` instead casts the u32 to a native pointer (`snd_Util.cpp:104`) and is not used by these assets. A table is `u32 count` followed by `count` elements.

## BRSAR (`RSAR`)

The `SoundArchiveFile::Header` is the 0x40-byte file header followed by six `u32` fields at `0x10`: `symbolDataOffset`, `symbolDataSize`, `infoOffset`, `infoSize`, `fileImageOffset`, `fileImageSize`. It has three blocks: `SYMB`, `INFO`, and `FILE` (`include/nw4r/snd/snd_SoundArchiveFile.h:178`).

- `SYMB` (`SymbolBlock`, header at `0`): `StringChunk` at `0x8` has five offsets: string table, sound, player, group, and bank trees. The string table is `u32 count` plus offsets to NUL-terminated strings. Each tree has `u32 rootIdx` and a table of 0x14-byte nodes (`u16 flags`, `u16 bit`, `u32 leftIdx`, `u32 rightIdx`, `u32 strIdx`, `u32 id`). These are offsets from the beginning of the symbol data; the reader stores derived pointers in `SoundArchiveFileReader::SetStringChunk` (`src/nw4r/snd/snd_SoundArchiveFile.cpp:33`).
- `INFO` (`InfoBlock`, block header at `0`, `Info` at `0x8`): six offset `DataRef`s at `0x00`, `0x08`, `0x10`, `0x18`, `0x20`, `0x28` select sound, bank, player, file, group, and archive-player tables. Each table entry is another `DataRef`. Sound common entries are 0x2c bytes (`stringId`, `fileId`, `playerId`, optional 3D `DataRef`, volume/priority/type/filter, typed sound `DataRef`, two user params, pan mode/curve). Typed payloads are SEQ (`dataOffset`, `bankId`, `allocTrack`, priorities), empty STRM, or WAVE (`subNo`, `allocTrack`, priorities). Bank entries have `stringId` and `fileId`. Player entries have `stringId`, `u8 playableSoundCount`, padding, and `heapSize`. File entries have `fileSize`, `waveDataSize`, `entryNum`, external-path `DataRef`, and a position-table `DataRef`; positions are `groupId`/`index`. Group entries have `stringId`, `entryNum`, external-path `DataRef`, embedded group `offset`/`size`, wave-data `offset`/`size`, and item-table `DataRef`; items have `fileId`, `offset`/`size`, and wave `offset`/`size`. The archive-player entry has seven `u16` counts.
- `FILE`: a block header followed by group regions. An internal group is addressed as `fileImageOffset + group.offset + item.offset`; its wave data is `fileImageOffset + group.waveDataOffset + item.waveDataOffset`. `SoundArchive::detail_OpenFileStream` combines group and item offsets (`src/nw4r/snd/snd_SoundArchive.cpp:144`), and `MemorySoundArchive::detail_GetFileAddress`/`detail_GetWaveDataFileAddress` do the same (`src/nw4r/snd/snd_MemorySoundArchive.cpp:99,122`).

## Embedded sound files

The game reads embedded `RSEQ`, `RBNK`, and `RWSD` headers from the selected group item. They share the same 0x40-byte file header and block model.

- `RSEQ` (`SeqFile::Header`, `include/nw4r/snd/snd_SeqFile.h:18`): `dataBlockOffset`, `dataBlockSize`, `labelBlockOffset`, `labelBlockSize` at `0x10`. Its `DATA` block adds `u32 baseOffset` at `0x8`; `SeqFileReader` turns the block offset and then the base offset into addresses (`src/nw4r/snd/snd_SeqFile.cpp:35,42`). Sequence bytecode is read byte-wise.
- `RBNK` (`BankFile::Header`, `include/nw4r/snd/snd_BankFile.h:53`): `DATA` and `WAVE` block offsets/sizes at `0x10`. `DATA` has an instrument `DataRef` table; data types select an `InstParam`, range table, or index table. `WAVE` has a `DataRef` table to `WaveFile::WaveInfo` records. `BankFileReader` resolves instrument and wave references (`src/nw4r/snd/snd_BankFile.cpp:88,134,158,196`).
- `RWSD` (`WsdFile::Header`, `include/nw4r/snd/snd_WsdFile.h:65`): `DATA` and `WAVE` block offsets/sizes at `0x10`. `DATA` has `wsdCount` and `DataRef`s to `Wsd`; each has refs to `WsdInfo`, a track table, and a note table. `WAVE` has `waveCount` and direct `u32` offsets to `WaveInfo` records. `WsdFileReader` resolves the data and wave references (`src/nw4r/snd/snd_WsdFile.cpp:43,46,80,83,86,111`).
- `RWAR` has the same header model and one `DATA` block. The disc's group wave regions contain raw DSP-ADPCM data rather than a standalone `RWAR` file.

`WaveInfo` (`include/nw4r/snd/snd_WaveFile.h:23`) is 0x1c bytes: format, loop flag, channel count, split sample-rate fields, loop start/end, channel-info-table offset, sample-data offset, and reserved. Each channel entry has data/adpcm offsets, four mix volumes, and reserved. `WaveFileReader` adds the channel-table and channel offsets to `WaveInfo`, and adds `WaveInfo.dataOffset + channelDataOffset` to the external wave-data address (`src/nw4r/snd/snd_WaveFile.cpp:32,48,62`). ADPCM metadata is 16×s16 coefficients followed by gain, predictor/scale, `yn1`, `yn2`, loop predictor/scale, and loop history (`include/nw4r/snd/snd_Types.h:62,70,76`).

## BRSTM (`RSTM`)

`StrmFile::Header` (`include/nw4r/snd/snd_StrmFile.h:61`) is the file header plus six `u32` fields at `0x10`: `headBlockOffset`, `headBlockSize`, `adpcBlockOffset`, `adpcBlockSize`, `dataBlockOffset`, and `dataBlockSize`. The disc uses two declared blocks (`HEAD`, `DATA`); four files omit `ADPC`.

`HEAD` (`HeadBlock`, `include/nw4r/snd/snd_StrmFile.h:71`) has three offset `DataRef`s at `0x8`, `0x10`, and `0x18`, based on the block's first ref. They target `StrmDataInfo`, a track table, and a channel table. `StrmDataInfo` (`include/nw4r/snd/snd_StrmFile.h:21`) is 0x34 bytes: codec, loop flag, channels, split sample rate, block header offset, loop start/end, data offset, block count/size/samples, last-block values, and ADPCM interval/size. The channel table leads to one `AdpcmInfo` per channel. `StrmFileReader` resolves those refs (`src/nw4r/snd/snd_StrmFile.cpp:44,69`).

`ADPC`, when present, is a block header plus `(block, channel)` pairs of `u16 yn1`/`yn2` used for mid-stream starts. `DATA` is block header plus interleaved channel blocks. The runtime computes a stream address as `StrmDataInfo.dataOffset + blockIndex * (blockHeaderOffset + blockSize * channels)` (`src/nw4r/snd/snd_StrmPlayer.cpp:693`). DSP-ADPCM nibbles are byte-oriented; only metadata and block fields need endian conversion.

## PC loader requirements

A little-endian 32-bit loader must swap every `u16`/`u32`/`f32` structural field and must not overlay these files with native C structs. It must resolve `DataRef` values relative to the runtime base identified above, and combine group/file/wave offsets exactly as the sound archive does. Strings and ADPCM nibble payloads can remain byte arrays. On 64-bit little-endian hosts, additionally avoid storing pointers in the original 4-byte slots: `REFTYPE_ADDRESS` is a u32-to-pointer cast in Wii code, although this disc only uses offset refs. Build relocated/runtime structures separately with native-width pointers.
