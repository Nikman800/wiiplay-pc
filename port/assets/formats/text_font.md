# Text, font, and standalone texture formats

All multibyte numeric fields below are big-endian on disc. Offsets are relative to the start of
the containing file unless a row says otherwise. These are structural notes only.

## BMG (`MESGbmg1`)

Sources: `MessageHeader`, `DataBlock`, `MessageInfoDataBlock`, and `MessageIDDataBlock` in
`~/wii-play/include/egg/util/eggMsgRes.h:20-81`; `RPSysMessage::Entry` in
`~/wii-play/include/Pack/RPKernel/RPSysMessage.h:25-35`; `EGG::MsgRes::getMsg` and
`analyzeTag` in `~/wii-play/src/egg/util/eggMsgRes.cpp:102-194`; tag interpretation in
`~/wii-play/src/Pack/RPKernel/RPSysTagProcessor.cpp:51-186`.

### File header

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | `u8[4]` | `MESG` |
| 0x04 | `u8[4]` | `bmg1` |
| 0x08 | `u32` | data size, including the 0x20-byte file header |
| 0x0C | `u32` | number of following sections |
| 0x10 | `u8` | encoding: 2 means 16-bit text |
| 0x11 | `u8[15]` | reserved/padding |

`EGG::MsgRes` treats the file as `MessageHeader` and iterates blocks at
`data + sizeof(MessageHeader)`, using the block size to advance.

### Sections

Every section starts with `u32 kind` and `u32 size`; the payload begins at section + 8.

**INF1**

| Offset from section | Type | Meaning |
|---:|---|---|
| 0x08 | `u16` | entry count |
| 0x0A | `u16` | entry size (12 in this game) |
| 0x0C | `u16` | group ID |
| 0x0E | `u16` | padding |
| 0x10 | entry array | count × 12 bytes |

Each entry is `u32 dataOffset`, `u32 attribute`, `u16 scale`, `s8 charSpace`, and one padding
byte. `dataOffset` is relative to the DAT1 payload; `EGG::MsgRes::getMsg` adds it to the DAT1
content address and returns that pointer. It is not rewritten on disc.

**DAT1**

The payload at section + 8 is an array of NUL-terminated UTF-16BE messages. A message may contain
binary escape tags. The tag begins with UTF-16BE code unit `0x1A`. Immediately after that unit are:

1. one `u8` total tag byte length, including the `0x1A` code unit;
2. one `u8` tag identifier;
3. two more `u8` identifier bytes, making the 24-bit identifier together with item 2;
4. tag-specific parameter bytes.

`analyzeTag` computes `tagLength = firstPayloadByte - 2`, takes the 24-bit ID from the next three
bytes, and sets the parameter pointer at byte offset 6 when the payload length exceeds four. Thus
the stored byte length equals 2 + ID bytes (3) + parameter bytes. Parameters are byte-oriented.
`0xFF0007` is the ruby tag: at byte offset 6 its parameter bytes are ruby scale, base-character
count, then UTF-16BE ruby text; the base text follows the whole tag. Other IDs select window,
case, color, and scale behavior. A PC parser must not treat a tag as UTF-16 after the `0x1A` unit:
swap text units only, and either preserve or separately decode tag bytes.

**MID1**

| Offset from section | Type | Meaning |
|---:|---|---|
| 0x08 | `u16` | message-ID count |
| 0x0A | `u8` | form |
| 0x0B | `u8` | form supplement (group/index bit partition) |
| 0x0C | `u8[4]` | padding |
| 0x10 | `u32[]` | message IDs |

### PC loading

The host build overlays the real `EGG::MsgRes` / `RPSysMessage` structs on the file: header,
section, INF1 and MID1 numeric fields are `be_*` wrapper types, and the host compiles with
`-fshort-wchar`, so `wchar_t` is 16 bits and `MessageDataBlock::mContent` has the Wii layout on
32- and 64-bit hosts. After loading, `PortSwapBmgText` (port/host/src/host_text.cpp) swaps the
UTF-16BE text units to host order once, tag-aware: the `0x001A` mark is swapped, the tag's
length, group, id and parameter bytes stay as in the file, and `MsgRes::analyzeTag` reads group
and id with `port_load_be32` on the host. BMG stores no on-disc pointers, so no in-place pointer
relocation is required. `port/host/tests/test_resources.py` checks all 714 messages against
`bmg_messages()` in `fmt_text_font.py`.

## BRFNT (`RFNT`)

Sources: `BinaryFileHeader` and `BinaryBlockHeader` in
`~/wii-play/include/nw4r/ut/ut_binaryFileFormat.h:8-19`; `CharWidths` in
`~/wii-play/include/nw4r/ut/ut_Font.h:21-25`; `FontTextureGlyph`, `FontWidth`,
`FontCodeMap`, and `FontInformation` in
`~/wii-play/include/nw4r/ut/ut_ResFontBase.h:18-57`; relocation and lookup in
`~/wii-play/src/nw4r/ut/ut_ResFont.cpp:13-110` and
`~/wii-play/src/nw4r/ut/ut_ResFontBase.cpp:90-150`.

### File header

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | `u8[4]` | `RFNT` |
| 0x04 | `u16` | byte-order mark, `0xFEFF` |
| 0x06 | `u16` | version, `0x0104` on this disc |
| 0x08 | `u32` | file size |
| 0x0C | `u16` | header size, 16 |
| 0x0E | `u16` | block count |

Each block starts with `u32 kind` and `u32 size`, followed by its payload.

### FINF

Payload offsets below are relative to the FINF payload:

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | `u8` | font type |
| 0x01 | `s8` | linefeed |
| 0x02 | `u16` | alternate-character glyph index |
| 0x04 | `s8, u8, s8` | default left, glyph width, character width |
| 0x07 | `u8` | font encoding |
| 0x08 | `u32` | offset to TGLP payload |
| 0x0C | `u32` | offset to first CWDH payload, or zero |
| 0x10 | `u32` | offset to first CMAP payload, or zero |
| 0x14 | `u8` | height |
| 0x15 | `u8` | width |
| 0x16 | `u8` | ascent |
| 0x17 | `u8` | padding |

`ResFont::Rebuild` rewrites `pGlyph`, `pWidth`, and `pMap` in place as
`fileBase + s32(storedValue)` in the FINF case at `ut_ResFont.cpp:74-83`.

### TGLP

| Offset from payload | Type | Meaning |
|---:|---|---|
| 0x00 | `u8` | cell width |
| 0x01 | `u8` | cell height |
| 0x02 | `s8` | baseline position |
| 0x03 | `u8` | maximum character width |
| 0x04 | `u32` | bytes per sheet |
| 0x08 | `u16` | sheet count |
| 0x0A | `u16` | GX texture format |
| 0x0C | `u16` | sheet row count |
| 0x0E | `u16` | sheet line count |
| 0x10 | `u16` | sheet width |
| 0x12 | `u16` | sheet height |
| 0x14 | `u32` | offset to sheet data |

`Rebuild` rewrites `sheetImage` in place at `ut_ResFont.cpp:84-89`. Pixel data remains in the GX
byte layout selected by `sheetFormat`.

### CWDH

| Offset from payload | Type | Meaning |
|---:|---|---|
| 0x00 | `u16` | first glyph index |
| 0x02 | `u16` | last glyph index |
| 0x04 | `u32` | offset from the start of the file to the next CWDH payload, or zero |
| 0x08 | widths | inclusive-index table of `s8 left, u8 glyphWidth, s8 charWidth` |

`Rebuild` rewrites a nonzero `pNext` in place at `ut_ResFont.cpp:90-97` (file start + value, like
every other slot). `ResFontBase::GetCharWidthsFromIndex` follows the chain; the first CWDH whose
index range holds the glyph wins, otherwise the FINF default widths apply.

### CMAP

| Offset from payload | Type | Meaning |
|---:|---|---|
| 0x00 | `u16` | first character code |
| 0x02 | `u16` | last character code |
| 0x04 | `u16` | mapping method: 0 direct, 1 table, 2 scan |
| 0x06 | `u16` | reserved |
| 0x08 | `u32` | offset from the start of the file to the next CMAP payload, or zero |
| 0x0C | mapping | method-specific data |

Direct maps use one `u16` base glyph index. Table maps use
`(lastCode - firstCode + 1)` `u16` glyph indices. Scan maps use one `u16` count followed by
`count` pairs of `u16 characterCode, u16 glyphIndex`. `Rebuild` rewrites a nonzero `pNext` in place
at `ut_ResFont.cpp:98-105`, and `ResFontBase::FindGlyphIndex` follows the resolved chain: the
first CMAP whose code range holds the character decides, even when it does not map it (a table
entry of 0xFFFF, or a code missing from a scan map, gives "not found" and the alternate glyph).
Scan maps are searched by bisection, so their pairs must be sorted by code (all disc fonts are).

On this disc the 8 fonts have 29 CMAP blocks in chains of 4 to 7. An earlier version of the
parser read `pNext` at payload +0x6 (it skipped the reserved `u16`) and treated it as relative to
the current block, and so reported five fonts as broken; the files are fine.

### PC loading

The host build runs the real `ResFont` code in place: the numeric fields are `be_*` wrapper
types and the five pointer slots are `RES_PTR(T)` (4-byte big-endian emulated Wii addresses,
`port/docs/POINTERS.md` M0/M1), so `Rebuild` relocates exactly as on the Wii and the struct
layouts stay the file layouts on 32- and 64-bit hosts. `port/host/tests/test_resources.py`
compares every code point of every disc font with the `BrfntFont` helper in
`fmt_text_font.py` (see `port/docs/HOST_BUILD.md`). GX texture sheets need the PC texture
decoder; they are not endian-swapped as scalar arrays.

## BTI (`EGG::ResTIMG`)

Source: `EGG::ResTIMG` in `~/wii-play/include/egg/gfx/eggResTIMG.h:12-37`; use in
`EGG::GXUtility::getTexObj` at `~/wii-play/src/egg/gfxe/eggGXUtility.cpp:35-61`.
BTI has no magic. The header is exactly 32 bytes:

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | `u8` | GX texture format |
| 0x01 | `u8` | transparency flag |
| 0x02 | `u16` | width in pixels |
| 0x04 | `u16` | height in pixels |
| 0x06 | `u8` | horizontal wrap mode |
| 0x07 | `u8` | vertical wrap mode |
| 0x08 | `u8` | TLUT name |
| 0x09 | `u8` | TLUT format |
| 0x0A | `u16` | palette entry count |
| 0x0C | `s32` | palette offset from file start |
| 0x10 | `u8` | mip-map enabled flag |
| 0x11 | `u8` | edge LOD enable |
| 0x12 | `u8` | bias clamp enable |
| 0x13 | `u8` | anisotropy mode |
| 0x14 | `u8` | minification filter |
| 0x15 | `u8` | magnification filter |
| 0x16 | `s8` | minimum LOD (units of 1/8) |
| 0x17 | `s8` | maximum LOD (units of 1/8) |
| 0x18 | `u8` | extra mip levels |
| 0x19 | `u8` | reserved |
| 0x1A | `s16` | LOD bias (percent) |
| 0x1C | `s32` | image offset from file start; zero means 32 |

`getTexObj` resolves `imageOffset` by adding it to the resource address and passes the result plus
width, height, format, wrap, LOD, and filter fields to GX. BTI contains no pointers and does not
rewrite its header. A little-endian loader swaps the scalar header fields and palette/image offsets,
then decodes the unchanged GX pixel/palette bytes with the format-specific PC decoder. On 64-bit
hosts this is also safest as a decoded native header rather than an overlaid resource struct.
