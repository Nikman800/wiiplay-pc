# BRRES, MDL0, TEX0, and PLT0

This note covers structure only. All fields are big-endian and offsets are
signed 32-bit values relative to the containing structure unless stated otherwise.

## BRRES container

The file header (`ut::BinaryFileHeader`,
`include/nw4r/ut/ut_binaryFileFormat.h:16`) is:

| Offset | Type | Field | Meaning |
|---:|---|---|---|
| `0x00` | `u32` | signature | Must be `bres` |
| `0x04` | `u16` | byteOrder | Must be `0xFEFF` (big-endian) |
| `0x06` | `u16` | version | Container revision, expected to be zero |
| `0x08` | `u32` | fileSize | Total file size in bytes |
| `0x0C` | `u16` | headerSize | Must be `0x10` |
| `0x0E` | `u16` | dataBlocks | Number of top-level blocks |

`ResFileData` places the root block at `0x10`
(`include/nw4r/g3d/res/g3d_resfile.h:30`). The root begins with
`ut::BinaryBlockHeader` (`include/nw4r/ut/ut_binaryFileFormat.h:11`):
`u32 kind` at `0x00` (`root`) and `u32 size` at `0x04`. A
`ResDicData` dictionary starts at root `+0x08`
(`include/nw4r/g3d/res/g3d_resdict.h:19`).

A dictionary has `u32 size`, `u32 numData`, then one root node followed by
`numData` resource nodes. Each `ResDicNodeData`
(`include/nw4r/g3d/res/g3d_resdict.h:10`) is 16 bytes:

| Node offset | Type | Field | Meaning |
|---:|---|---|---|
| `0x00` | `u16` | ref | Patricia search discriminator |
| `0x02` | `u16` | flag | Unused by this loader |
| `0x04` | `u16` | idxLeft | Index of the left child |
| `0x06` | `u16` | idxRight | Index of the right child |
| `0x08` | `s32` | ofsString | Node name, relative to the dictionary |
| `0x0C` | `s32` | ofsData | Resource, relative to the dictionary |

The root dictionary names the 11 groups constructed by
`src/nw4r/g3d/res/g3d_resfile.cpp:7`: models, textures, palettes, the seven
animation groups, and external resources. Each group points to another
dictionary. Its `ofsData` values point to subfile blocks. Two names may legally
reference the same block; `ResDic` resolves each name independently
(`src/nw4r/g3d/res/g3d_resdict.cpp:63`).

## MDL0

`ResMdlData` is `include/nw4r/g3d/res/g3d_resmdl.h:102`.
Its leading `ResBlockHeaderData` is `u32 kind` (`MDL0`) and `u32 size`
(`include/nw4r/g3d/res/g3d_rescommon.h:140`).

| Offset | Type | Field |
|---:|---|---|
| `0x08` | `u32` | revision |
| `0x0C` | `s32` | toResFileData |
| `0x10` | `s32` | toResByteCodeDic |
| `0x14` | `s32` | toResNodeDic |
| `0x18` | `s32` | toResVtxPosDic |
| `0x1C` | `s32` | toResVtxNrmDic |
| `0x20` | `s32` | toResVtxClrDic |
| `0x24` | `s32` | toResVtxTexCoordDic |
| `0x28` | `s32` | toResMatDic |
| `0x2C` | `s32` | toResTevDic |
| `0x30` | `s32` | toResShpDic |
| `0x34` | `s32` | toResTexNameToTexPlttInfoDic |
| `0x38` | `s32` | toResPlttNameToTexPlttInfoDic |
| `0x3C` | `s32` | name |
| `0x40` | `ResMdlInfoData` | info |

The revision expected by this build is 9. `toResFileData` resolves with the
subfile as base; the other offsets and `name` also resolve with the subfile as
base. Every section field points to another `ResDicData`.

`ResMdlInfoData` is 0x40 bytes
(`include/nw4r/g3d/res/g3d_resmdl.h:45`):

| Info offset | Type | Field |
|---:|---|---|
| `0x00` | `u32` | size |
| `0x04` | `s32` | toResMdlData |
| `0x08` | `s32` | scaling_rule |
| `0x0C` | `s32` | tex_mtx_mode |
| `0x10` | `s32` | vertex_size |
| `0x14` | `s32` | triangle_size |
| `0x18` | `s32` | original_path |
| `0x1C` | `u32` | numViewMtx |
| `0x20` | `u8` | need_nrm_mtx_array |
| `0x21` | `u8` | need_tex_mtx_array |
| `0x22` | `u8` | is_valid_volume |
| `0x23` | `u8` | envelope_mtx_mode |
| `0x24` | `s32` | toMtxIDToNodeID |
| `0x28` | `f32[3]` | volume_min |
| `0x34` | `f32[3]` | volume_max |

Offsets inside `info` are relative to `info`. `ResMdl::GetResMdlInfo` returns an
`info` wrapper (`include/nw4r/g3d/res/g3d_resmdl.h:187`), and
`ResMdlInfo::GetNumPosNrmMtx` applies `toMtxIDToNodeID` from that base
(`include/nw4r/g3d/res/g3d_resmdl.h:80`).

## TEX0 and PLT0

`ResTexData` is `include/nw4r/g3d/res/g3d_restex.h:12`:

| Offset | Type | Field |
|---:|---|---|
| `0x00` | `u32` | header.kind (`TEX0`) |
| `0x04` | `u32` | header.size |
| `0x08` | `u32` | revision |
| `0x0C` | `s32` | toResFileData |
| `0x10` | `s32` | toTexData |
| `0x14` | `s32` | name |
| `0x18` | `u32` | flag |
| `0x1C` | `u16` | width |
| `0x1E` | `u16` | height |
| `0x20` | `u32` | fmt/cifmt |
| `0x24` | `u32` | mipmap_level |
| `0x28` | `f32` | min_lod |
| `0x2C` | `f32` | max_lod |
| `0x30` | `s32` | original_path |
| `0x34` | `s32` | toResUserData |

`ResPlttData` is `include/nw4r/g3d/res/g3d_respltt.h:12`:

| Offset | Type | Field |
|---:|---|---|
| `0x00` | `u32` | header.kind (`PLT0`) |
| `0x04` | `u32` | header.size |
| `0x08` | `u32` | revision |
| `0x0C` | `s32` | toResFileData |
| `0x10` | `s32` | toPlttData |
| `0x14` | `s32` | name |
| `0x18` | `u32` | fmt |
| `0x1C` | `u16` | numEntries |
| `0x1E` | `u16` | padding |
| `0x20` | `s32` | original_path |
| `0x24` | `s32` | toResUserData |

Both subfile revisions are 1. `toResFileData` resolves from the subfile and
normally points back to the container header. `name`, `original_path`,
`toTexData`, `toPlttData`, and `toResUserData` also resolve from the subfile.
`ResTex::GetTexData` and `ResPltt::GetPlttData` apply their payload offsets
directly (`include/nw4r/g3d/res/g3d_restex.h:73`,
`include/nw4r/g3d/res/g3d_respltt.h:48`).

## In-place writes and host handling

g3d does not wholesale relocate `to*` fields. They remain offsets and
`ResCommon::ofs_to_ptr` resolves them on each access
(`include/nw4r/g3d/res/g3d_rescommon.h:95`). This means the BRRES bytes can be
shared and read-only for the structures described here.

Runtime writes occur after `ResFile::Init`
(`src/nw4r/g3d/res/g3d_resfile.cpp:500`) and during `ResFile::Bind`
(`src/nw4r/g3d/res/g3d_resfile.cpp:468`):

- `ResTexPlttInfo::BindTex_` and `BindPltt_` write handles into the 4-byte
  `pTexData`/`pPlttData` slots in MDL0 material data
  (`src/nw4r/g3d/res/g3d_resmat.cpp:1519`).
- `ResAnmTexPat::Bind` writes `ResTex`/`ResPltt` handles into PAT0 arrays
  (`src/nw4r/g3d/res/g3d_resanmtexpat.cpp:118`).
- `ResLightSet::Bind` writes 16-bit light ids into SCN0 data
  (`src/nw4r/g3d/res/g3d_reslightset.cpp:17`).
- Material and shape display-list patching writes GX command words byte by byte
  in big-endian order (`include/nw4r/g3d/res/g3d_rescommon.h:295`).

On little-endian 32-bit or 64-bit PC loaders, every multi-byte CPU field above
must be read with a big-endian accessor. Texture images, palette entries,
vertex arrays, and display-list payloads remain in their GX byte order and
should be decoded by the graphics layer rather than word-swapped in place.

On 64-bit hosts, the 4-byte handle slots listed above cannot contain native
pointers. Store a signed offset from the BRRES base, or an index into the
already-decoded model/texture/palette arrays, and resolve it at access. The
original on-disc GX object layouts must be preserved; replacing them with native
objects changes MDL0 offsets.

The parser's structure-only `--json FILE` mode exposes decoded fields but never
writes resource names, paths, payload bytes, or texture data to the format note.
