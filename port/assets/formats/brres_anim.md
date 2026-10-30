# BRRES animations: CHR0, CLR0, SRT0, PAT0, SCN0

Structure of the five animation block kinds Wii Play stores in BRRES files, as read by the
game's NW4R code. All values are big-endian. Parser and checker:
`port/assets/tools/fmt_brres_anim.py` (`--check ROOT`, `--json FILE`). Paths below are in the
decompilation (`include/nw4r/g3d/res/`, `src/nw4r/g3d/res/`).

## Offset bases

The thing most easily gotten wrong: each offset has its own base.

| Offset | Relative to | Source |
|---|---|---|
| ResDic `ofsString`, `ofsData` | start of that dictionary | `g3d_resdict.h`, `g3d_resdict.cpp` |
| block fields `to...`, `name`, `original_path` | start of the block (e.g. the `CHR0` magic); 0 = NULL | `ResCommon::ofs_to_ptr`, `g3d_rescommon.h` |
| node/material `name` (record +0x00) | start of the record | `g3d_anmchr.cpp:643` |
| CHR0 node `anms[i]` data offset | start of the node record | `CalcResultFVS`/`CalcResultCV`, `g3d_resanmchr.cpp` |
| SRT0 `toResAnmTexSrtTexData[i]` | start of the material record | `ResAnmTexSrt::GetAnmResult` |
| PAT0 `anms[i]` data offset | start of the material record | `ResAnmTexPat::GetAnmResult` |
| `ResAnmData`, `ResColorAnmData`, `ResBoolAnmData` offsets | the word itself | `g3d_resanm.h` |
| PAT0 texture/palette name arrays, light-set `lightNames[]` | start of the array (not the element) | `GetResNameFromOffsetArray`, `ResLightSet::GetLightResName` |
| SCN0 record `name`, `ambLightName` | start of the record | `g3d_reslightset.h` |

A name offset points at the characters; the `u32` length is 4 bytes earlier
(`NW4R_G3D_OFS_TO_RESNAME`). Names can be longer than 27 characters.

## Container

The BRRES root dictionary (at `headerSize + 8`) names groups; the animation groups are
`AnmChr(NW4R)`, `AnmClr(NW4R)`, `AnmTexSrt(NW4R)`, `AnmTexPat(NW4R)`, `AnmScn(NW4R)`. Each group
is a ResDic whose entries point at blocks. A ResDic is `u32 size`, `u32 numData`, then
`numData + 1` 16-byte nodes (`u16 ref, flag, idxLeft, idxRight; s32 ofsString, ofsData`); node 0
is the Patricia-tree root and carries no data.

Every block starts with `char magic[4]`, `u32 size`, `u32 revision`, `s32 toResFileData`.
Revisions on the disc: CHR0 4, CLR0 3, SRT0 4, PAT0 3, SCN0 4 (the `REVISION` constants).

## Shared value encodings (`g3d_resanm.h`)

- `ResAnmData` (4 bytes): an inline `f32` when the owner's "constant" flag is set, otherwise an
  `s32` offset to `ResKeyFrameAnmData`: `u16 numKeyFrame`, pad, `f32 invKeyFrameRange`, then
  `numKeyFrame` x `{f32 frame, f32 value, f32 slope}` (Hermite). Frames are nondecreasing.
  `invKeyFrameRange = 1 / (last - first)`; it is `+inf` when all keys share one frame, which is
  harmless because the lookup returns before using it.
- `ResColorAnmData`: inline RGBA `u32` when constant, otherwise an offset to `numFrame + 1` RGBA
  words, one per integer frame (lerped between frames).
- `ResBoolAnmData`: offset to one bit per frame, MSB first, in `u32` words.

## CHR0 (bone animation, `g3d_resanmchr.h`)

Header: `+0x10 toChrDataDic`, `+0x14 name`, `+0x18 original_path`, info at `+0x1C`:
`u16 numFrame, u16 numNode, u32 policy (0 one-time, 1 loop), u32 scalingRule`.

Node record: `s32 name, u32 flags, AnmData anms[]`. Only groups whose `FLAG_HAS_SCALE` (bit 22),
`FLAG_HAS_ROT` (23), `FLAG_HAS_TRANS` (24) bit is set are stored, in that order: scale is 1 word
when `FLAG_SCALE_UNIFORM` (bit 4) else 3, rotation 3, translation 3. Per-axis constant flags are
bits 13-21 (scale XYZ, rotation XYZ, translation XYZ). Format fields:

| Field | Bits | Codes |
|---|---|---|
| scale | 25-26 | 0 all inline constants, 1 FVS32, 2 FVS48, 3 FVS96 |
| rotation | 27-29 | 0 constants, 1 FVS32, 2 FVS48, 3 FVS96, 4 CV8, 5 CV16, 6 CV32 |
| translation | 30-31 | 0 constants, 1 FVS32, 2 FVS48, 3 FVS96 |

A non-constant word is an offset from the node record to:

- FVS (`ResAnmChrFVSData`): `u16 numFrameValues`, pad, `f32 invKeyFrameRange`; FVS32/FVS48 then
  have `f32 scale, f32 offset` and entries of 4 bytes (`u32`: bits 24-31 integer frame, bits 12-23
  unsigned value, bits 0-11 signed slope in 1/32 units) or 6 bytes (`s16 frame` in 1/32 frames,
  `u16 value`, `s16 slope` in 1/256 units); value = raw * scale + offset. FVS96 entries follow
  directly as `f32 frame, value, slope` (`CResAnmChrFrm`, `g3d_resanmchr.cpp`).
- CV (`ResAnmChrCVData`): `numFrame + 1` values per frame; CV8 (`u8`) and CV16 (`u16`) first have
  `f32 scale, f32 offset` (value = raw * scale + offset), CV32 is raw `f32`.

The node flags low bits (identity, scale-one, rotation-zero, SSC, XSI scaling, ...) are copied into
`ChrAnmResult` as hints.

## CLR0 (material color, `g3d_resanmclr.h`)

Header as CHR0; info `u16 numFrame, u16 numMaterial, u32 policy`. Material record:
`s32 name, u32 flags, ResAnmClrAnmData anms[]`. Flags use 2 bits per target (exists, constant)
for, in order, CLR0 CLR1 AMB0 AMB1 TEV0 TEV1 TEV2 TEVK0 TEVK1 TEVK2 TEVK3; `anms[]` is packed
(existing targets only), each `u32 mask` (bits of the register to replace) + `ResColorAnmData`.

## SRT0 (texture matrices, `g3d_resanmtexsrt.h`)

Header as CHR0; info `u16 numFrame, u16 numMaterial, u32 texMtxMode (0 Maya, 1 XSI, 2 3ds Max),
u32 policy`. Material record: `s32 name, u32 flags (bit i = TexMtx i, 8 max), u32 indFlags
(bit i = indirect matrix i, 3 max), s32 toResAnmTexSrtTexData[]` (packed: material matrices then
indirect, offsets from the material record). Texture record: `u32 flags`, then `ResAnmData`
words: Su and (unless `SCALE_UNIFORM`, bit 4) Sv when `SCALE_ONE` (bit 1) is clear; R when
`ROT_ZERO` (bit 2) is clear; Tu, Tv when `TRANS_ZERO` (bit 3) is clear. Constant bits: Su 5, Sv 6,
R 7, Tu 8, Tv 9.

## PAT0 (texture swap, `g3d_resanmtexpat.h`)

Header: `+0x10 toTexPatDataDic, +0x14 toTexNameArray, +0x18 toPlttNameArray, +0x1C toResTexArray,
+0x20 toResPlttArray, +0x24 name, +0x28 original_path`; info at `+0x2C`: `u16 numFrame,
numMaterial, numTexture, numPalette; u32 policy`. Name arrays hold `s32` offsets from the array
start. The ResTex/ResPltt arrays are zero on disc and filled by `Bind()` with pointers to TEX0/PLT0
of the same name.

Material record: `s32 name, u32 flags, AnmData anms[]`; 4 flag bits per slot (exists, constant,
animates texture, animates palette), 8 slots, `anms[]` packed. Constant: `u16 texIndex,
u16 plttIndex` inline. Otherwise an offset (from the material record) to `u16 numFrameValues`, pad,
`f32 invKeyFrameRange`, then `{f32 frame, u16 texIndex, u16 plttIndex}` steps (no interpolation).

## SCN0 (lights, fog, cameras, `g3d_resanmscn.h`)

Header: `+0x10 toScnTopLevelDic`, then arrays `+0x14` light sets, `+0x18` ambient lights,
`+0x1C` lights, `+0x20` fogs, `+0x24` cameras, `+0x28 name, +0x2C original_path`; info at `+0x30`:
`u16 numFrame, u16 numSpecularLight, u32 policy, u16 numLightSet, numAmbLight, numLight, numFog,
numCamera, pad`. The top-level dictionary (`LightSet(NW4R)`, `AmbLights(NW4R)`, `Lights(NW4R)`,
`Cameras(NW4R)`, ...) holds sub-dictionaries pointing at the array records. Every record starts
with `u32 size, s32 toResAnmScnData (back to the block), s32 name, u32 id, u32 refNumber`.

| Record | Size | Rest |
|---|---|---|
| `ResLightSetData` | 0x4C | `+0x14 s32 ambLightName, +0x18 u16 ambLightId, +0x1A u8 numLight, +0x1C s32 lightNames[8], +0x3C u16 lightId[8]` |
| `ResAnmAmbLightData` | 0x1C | `+0x14 flags (0 color, 1 alpha, 31 const), +0x18 color` |
| `ResAnmLightData` | 0x5C | `+0x14 specLightObjIdx, +0x18 toResUserData, +0x1C flags, +0x20 enable (bool anm), pos XYZ, color, aim XYZ, distFunc, refDistance, refBrightness, spotFunc, cutoff, specColor, shininess` |
| `ResAnmFogData` | 0x28 | `+0x14 flags (29 start, 30 end, 31 color const), +0x18 type, startz, endz, color` |
| `ResAnmCameraData` | 0x5C | `+0x14 projType, +0x18 flags, +0x1C toResUserData`, pos XYZ, aspect, near, far, rot XYZ, aim XYZ, twist, perspFovy, orthoHeight |

Light flags: bits 0-1 type (point, directional, spot), 2 enable, 3 specular, 4 color, 5 alpha;
constant bits 19-21 pos, 22 color, 23 enable, 24-26 aim, 27 cutoff, 28 refDistance,
29 refBrightness, 30 specColor, 31 shininess. Only fields the type needs are read (aim for
non-point or specular lights; refDistance/refBrightness for point and spot; cutoff for spot;
specColor/shininess for specular), so unused fields need not hold valid offsets. Camera flags:
bit 0 type (rotate, aim), 1 anm exists; constant bits 17-19 pos, 20 aspect, 21 near, 22 far,
23 fovy, 24 ortho height, 25-27 aim, 28 twist, 29-31 rot. Wii Play has no fog reader (dead-stripped)
and no fog records.

## Disc survey

From `fmt_brres_anim.py --check` (see `brres_anim.report.txt`): 72 of 132 BRRES files hold
animations: 147 CHR0 (2604 nodes), 27 CLR0 (48 materials), 22 SRT0 (26 materials, 31 texture
matrices), 10 PAT0 (11 materials, 56 texture names, no palettes), 16 SCN0 (54 light sets, 54
ambient lights, 48 lights, 19 cameras, no fogs).

## PC port notes

- Byte-swap on load or read through big-endian accessors; every structure above is 4-byte
  aligned and uses 32-bit offsets, so pointer-sized fields do not change layout if the port keeps
  offsets rather than converting them to pointers.
- The PAT0 ResTex/ResPltt arrays and the scene `toResUserData` fields are written at runtime with
  32-bit pointers; a 64-bit port needs a side table instead of writing into the file image.
