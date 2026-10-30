# Layout asset formats

This note describes only on-disc structure. All BRLYT/BRLAN integer and floating-point fields and all TPL header fields are big-endian. `wchr_t` text in TXT1 is UTF-16BE. A PC loader must byte-swap every listed scalar before native access (or use explicit big-endian accessors); it must not reinterpret packed resource structs as native-endian structures.

## Common BRLYT/BRLAN container

`res::BinaryFileHeader` (`include/nw4r/lyt/lyt_resources.h:16-23`):

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `char[4]` | `RLYT` for BRLYT or `RLAN` for BRLAN |
| `0x04` | `u16` | Byte-order mark; `0xFEFF` |
| `0x06` | `u16` | Version; game expects 8 |
| `0x08` | `u32` | Total file size |
| `0x0C` | `u16` | Header size; game expects 16 |
| `0x0E` | `u16` | Number of contiguous blocks |

Each block starts with `res::DataBlockHeader` (`lyt_resources.h:30-33`): `char kind[4]` at `+0x00`, `u32 size` at `+0x04`. The size includes its 8-byte header, and `Layout::Build` / `CreateAnimTransform` advance by it (`src/nw4r/lyt/lyt_layout.cpp:82-85` and `:190-243`).

BRLYT/BRLAN offsets are relative to the cited base object, not always to file start. The game converts them with `detail::ConvertOffsToPtr` (`include/nw4r/lyt/lyt_types.h:27-31`); it does not rewrite these fields in place.

## BRLYT blocks

- **LYT1** (`res::Layout`, `include/nw4r/lyt/lyt_layout.h:40-47`): origin type `u8` at `+0x08`, 3 zero pad bytes at `+0x09`, and width/height `f32[2]` at `+0x0C`. Block size is 20.
- **TXL1** (`res::TextureList`/`res::Texture`, `lyt_resources.h:50-54`, `:40-44`): `u16 texNum` at `+0x08`, 2 pad bytes at `+0x0A`, then 8-byte entries at `+0x0C`. Entry: name offset `u32` at `+0x00` and type `u8` at `+0x04`. Material builds names with each entry as base (`lyt_material.cpp:311-323`), but shipped files can place the referenced NUL string past the TXL1 block yet within the file; the parser therefore bounds-checks the final string against the file.
- **FNL1** (`res::FontList`/`res::Font`, `lyt_resources.h:71-75`, `:61-65`): same table shape as TXL1. `TextBox` resolves the selected font name against its entry (`lyt_textBox.cpp:67-73`).
- **MAT1** (`res::MaterialList`, `lyt_resources.h:82-86`): `u16 materialNum` at `+0x08`, 2 pad bytes, then a `u32[materialNum]` offset table at `+0x0C`; offsets are relative to the list. Each `res::Material` (`lyt_material.h:50-55`) has name `char[20]`, TEV colors `s16[3][4]`, TEV key colors `u8[4][4]`, and resource-count bits at `+0x3C`. Bits (`lyt_resources.h:118-154`) count/presence: texMap `0-3`, texSRT `4-7`, texCoordGen `8-11`, tevSwapTable 12, indTexSRT `13-14`, indTexStage `15-17`, tevStage `18-22`, alphaCompare 23, blendMode 24, chanCtrl 25, matColor 27. Variable material resources follow the fixed 0x40-byte record in the listed order.
- **PAN1** (`res::Pane`, `lyt_pane.h:53-67`): flags, base position and alpha bytes at `+0x08-0x0B`; name `char[16]`, user data `char[8]`, translate `f32[3]`, rotate `f32[3]`, scale `f32[2]`, and size `f32[2]` at the cited offsets. Block size is 76.
- **PAS1/PAE1/GRS1/GRE1** (`lyt_layout.cpp:142-180`): marker blocks with no payload; size is 8. PAS1/PAE1 open/close pane trees, and GRS1/GRE1 open/close groups.
- **PIC1** (`res::Picture`, `lyt_picture.h:22-29`): Pane fields through `+0x4B`; vertex colors `u32[4]` at `+0x4C`; material index `u16` at `+0x5C`; texture-coordinate count `u8` at `+0x5E`; then `u32[2][count]` UV coordinates. `Picture` resolves the material through the MAT1 offset table (`lyt_picture.cpp:35-43`).
- **TXT1** (`res::TextBox`, `lyt_textBox.h:36-65`): Pane fields through `+0x4B`; text buffer bytes `u16`, string bytes `u16`, font index `u16`, and text position `u16` at `+0x4C-0x54`; character/line spacing and font size `f32` at `+0x58-0x68`; vertex colors `u32[2]` at `+0x6C`; then `u32[2][2]` coordinates at `+0x74`; text offset `u32` at `+0x84` is relative to the TXT1 block. `TextBox` copies UTF-16BE text (`lyt_textBox.cpp:50-55`) and resolves font/material indices (`:67-73`, `:97-105`).
- **WND1** (`res::Window` plus `WindowContent`/`WindowFrame`, `lyt_window.h:36-69`): Pane fields through `+0x4B`; left/right/top/bottom inflation `f32[4]` at `+0x4C`; frame count `u8` at `+0x5C`; content offset `u32` at `+0x60`; frame table offset `u32` at `+0x64`. Content is relative to WND1 and contains colors, material index and texture-coordinate count, followed by coordinates. Frame entries are `u16 materialIdx` plus `u8 textureFlip`. `Window` resolves content, frame entries and materials with `ConvertOffsToPtr` (`lyt_window.cpp:237-297`).
- **BND1** (`res::Bounding`, `lyt_bounding.h:22-25`): exactly the Pane layout; no additional fields.
- **GRP1** (`res::Group`, `lyt_group.h:38-45`): name `char[16]` at `+0x08`, pane count `u16` at `+0x18`, 2 pad bytes, then 16-byte pane-name records. `Group` treats the names as fixed-length records immediately after the 0x1C-byte header (`lyt_group.cpp:14-29`).

## BRLAN blocks

BRLAN begins with PAT1 followed by PAI1.

- **PAT1** (`res::AnimationInfo`, `lyt_animation.h:26-54`): animation kind `u32` at `+0x00` (`RLPA`, `RLVI`, `RLVC`, `RLMC`, `RLTS`, `RLTP`, or `RLIM`), target count `u8` at `+0x04`, then 3 pad bytes.
- **PAI1** (`res::AnimationBlock`, `lyt_animation.h:26-34`): frame size `u16` at `+0x08`, loop `u8` at `+0x0A`, pad at `+0x0B`, file count `u16` at `+0x0C`, content count `u16` at `+0x0E`, and content-offset-table offset `u32` at `+0x10`. The table is relative to PAI1; each entry points to an `AnimationContent` (`lyt_animation.h:61-71`): 20-byte name, animation target count `u8`, pane/material type `u8`, and 2 pad bytes. After it is a `u32[num]` info-offset table. Each info holds PAT1 and a `u32[num]` target-offset table.

Each target has target `u32`, key count `u16`, and key-table offset `u16`; the table follows in the same record. Step keys are frame `f32` plus value `u16`; Hermite keys are frame/value/slope `f32[3]`. `Animate*` resolves info, target and key records through `ConvertOffsToPtr` (`lyt_animation.cpp:145-295`), and binding walks the PAI1-relative content table (`:379-445`). BRLAN is pure data: no in-place relocation is required.

## TPL

On-disc TPL starts with `TPLPalette` (`include/revolution/TPL/TPL.h:34-38`): version `u32` at `0x00` (2142000), descriptor count `u32` at `0x04`, and descriptor-array offset `u32` at `0x08`. Descriptors are 8-byte pairs of texture-header and palette-header offsets. A zero offset means that subresource is absent.

`TPLHeader` (`TPL.h:14-30`) is 36 bytes: height `u16`, width `u16`, GX format `u32`, image-data offset `u32`, wrap/filter enums as `u32`, LOD bias `f32`, edge-LOD/min-LOD/max-LOD/unpacked bytes. `TPLClutHeader` (`TPL.h:26-32`) is 16 bytes: entry count `u16`, unpacked/pad bytes, format `u32` at `+0x04`, and data offset `u32` at `+0x08`. Image and palette payloads are opaque GX-format data and remain byte-oriented.

`TPLBind` performs in-place relocation (`src/revolution/TPL/TPL.c:6-45`): the palette descriptor-array offset, each nonzero texture/CLUT header offset, and each associated data offset become 32-bit absolute pointers. Thus a little-endian 32-bit PC can either preserve that layout after byteswapping the offset fields or, preferably, keep the file immutable and use native side descriptors. A 64-bit PC cannot store native pointers in the 32-bit slots and must use a side descriptor/pointer table indexed by descriptor and subresource.

## Parser output and validation

`port/assets/tools/fmt_layout.py` returns one dictionary per file containing `header`, `sections` or `descriptors`, with offsets, sizes and decoded fields. `python3 port/assets/tools/fmt_layout.py --check ROOT` walks every file (including Yaz0/U8 archives through `wiiarc.walk`), selects BRLYT/BRLAN/TPL by magic, and prints per-format file/error/version and block/subresource counts.
