# Collision formats: KCL and PMP

All numeric fields and arrays are big-endian on disc. Offset fields are relative to the start of
the containing file. The loader relocates pointers only in mutable copies; it does not modify the
resource bytes.

## KCL

`RPKcl::SetResource` copies the header and turns the four section offsets into pointers in that
copy (`src/Pack/RPParty/RPPartyCommon/RPKcl.cpp:26-39`).

Header (`RPKclHeader`, `include/Pack/RPParty/RPPartyCommon/RPKcl.h:28-42`):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | `pPositions` | File offset to `f32 x,y,z` positions; relocated to a pointer |
| `0x04` | 4 | `pNormals` | File offset to `f32 x,y,z` normals; relocated to a pointer |
| `0x08` | 4 | `pPrisms` | File offset to `RPKclPrism[1]` (dummy triangle at index 0); relocated |
| `0x0C` | 4 | `pBlocks` | File offset to the octree root table; relocated |
| `0x10` | 4 | `thickness` | Collision thickness (`f32`) |
| `0x14` | 12 | `areaMin.x/y/z` | Area minimum (`f32 × 3`) |
| `0x20` | 4 | `maskX` | X cell mask (`u32`) |
| `0x24` | 4 | `maskY` | Y cell mask (`u32`) |
| `0x28` | 4 | `maskZ` | Z cell mask (`u32`) |
| `0x2C` | 4 | `shift` | Coarse X/Y/Z cell shift (`u32`) |
| `0x30` | 4 | `shiftY` | Y bits in the packed root key (`u32`) |
| `0x34` | 4 | `shiftZ` | Z bits in the packed root key (`u32`) |
| `0x38` | 4 | `sphereRadius` | Header field for `0x3C` files (`f32`) |

The declared header length is the first word. Wii Play has both `0x38` and `0x3C` forms. The C
struct is `0x3C`, so a `0x38` resource's copy overlays `positions[0].x` as `sphereRadius`
(`RPKcl.cpp:26-28`, and `docs/ASSETS.md §5`, open question 2). Copy exactly those bytes if
faithfulness matters; do not assign a real value to this field without confirming a reader.

Sections follow in order:

- Positions: `f32 × 3`, stride 12.
- Normals: `f32 × 3`, stride 12. The section length can have an 8-byte tail; Wii Play's files
  address two additional normal slots across that tail and the dummy prism, so bounds must follow
  the actual indexed extent rather than only the whole-vector count.
- Prisms: `RPKclPrism` (`include/Pack/RPParty/RPPartyCommon/RPKcl.h:14-22`), stride 16:
  `f32 height`, `u16 positionIdx`, `u16 normal0Idx..normal3Idx`, and `u16 attribute`. Triangle
  indices begin at 1; `CheckSphereTri` indexes positions and normals directly
  (`RPKcl.cpp:233-259`).
- Octree: an `s32` root table indexed by
  `((localZ >> shift) << shiftZ) | ((localY >> shift) << shiftY) | (localX >> shift)`. Positive
  words are byte offsets to an 8-word child node. Negative words are `0x80000000 | byteOffset`;
  they select a `u16` list relative to the current node. The runtime points at the list, then
  pre-increments before each read, so the list's first word is skipped and a zero terminates it
  (`RPKcl.cpp:185-219`). Lists may name a triangle from multiple cells.

## PMP (`PMPF`)

`RPResFile::Relocate` turns PMP's index/offset fields into pointers in place
(`src/Pack/RPParty/RPPartyCommon/RPResFile_3.cpp:36-96`).

Header (`RPResData`, `include/Pack/RPParty/RPPartyCommon/RPResFile_2.h:10-21`):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| `0x00` | 4 | magic | `"PMPF"` |
| `0x08` | 2 | `flags` | Bit 0 means already relocated; it is set after relocation |
| `0x10` | 2 | `numEntries` | Number of `RPResEntry` records |
| `0x12` | 2 | `numPaths` | Number of `RPPathRes` records |
| `0x14` | 2 | `numPoints` | Number of `RPPathPoint` records |
| `0x40` | 4 | `pEntries` | File offset; becomes `RPResEntry*`, or NULL for zero entries |
| `0x44` | 4 | `pPaths` | File offset; becomes `RPPathRes*`, or NULL for zero paths |
| `0x48` | 4 | `pPoints` | File offset; becomes `RPPathPoint*`, or NULL for zero points |

Entry (`RPResEntry`, `include/Pack/RPParty/RPPartyCommon/RPPathFollower.h:46-52`), stride `0x58`:
`u32 unk00` at 0, `u8 type` at `0x04`, `RPPathRes* pPath` (path index until relocation) at
`0x44`, and eight `s16 params` at `0x48`. `0xFFFFFFFF` becomes NULL; otherwise `Relocate` changes
it to `&pPaths[index]` (`RPResFile_3.cpp:69-81`). `FindFilteredEntry` compares the entry's `type`
byte, not the leading word.

Path (`RPPathRes`, `include/Pack/RPParty/RPPartyCommon/RPPathFollower.h:30-39`), stride `0x20`:
`u16 numPoints` at 0, `u8 type` at 4 (1 is Bezier, otherwise linear), `u16 flags` at 6 (bit 0:
closed), and `RPPathPoint* pPoints` (first-point index until relocation) at `0x18`
(`RPResFile_3.cpp:84-92`).

Point (`RPPathPoint`, `include/Pack/RPParty/RPPartyCommon/RPPathFollower.h:18-24`), stride
`0x14`: `f32 pos.x/y/z` at 0, then four `s16` parameters at `0x0C`, `0x0E`, `0x10`, and `0x12`.

## PC loading

- Little-endian 32-bit: perform a typed byte swap before relocation—KCL `u32`, `u16`, and `f32`
  fields plus octree words/lists; PMP header fields, `u16` counts/parameters, `u32` index slots,
  and `f32` positions. Keep flags and index sentinels (`0xFFFFFFFF`) intact after conversion.
- 64-bit: do not overlay these structures on file bytes. KCL's pointer members and PMP's pointer
  members widen from 4 to 8 bytes. Build native headers field by field, and represent relocated
  references as native pointers or retain their original indices.
