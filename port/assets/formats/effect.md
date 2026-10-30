# BREFF / BREFT effect resources

This note describes file structure only. `fmt_effect.py` treats names and image,
palette, animation-key, and per-target data as opaque bytes.

## Shared container

All integer fields are big-endian and offsets below are relative to the containing
object unless described as a parser offset. The current Wii runtime is version 7
(`include/nw4r/ef/ef_resource.h:31`). The source structures are `FileHeader`,
`BlockHeader`, `NameTable`, `EffectProject`, and `TextureProject`
(`src/nw4r/ef/ef_resource.cpp:30,39,44`; `include/nw4r/ef/ef_resource.h:16,24`).

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | char[4] | `REFF` or `REFT` |
| 0x04 | u8[2] | byte order; release files contain `0xFEFF` |
| 0x06 | u16 | version, expected to be 7 |
| 0x08 | u32 | file size in bytes |
| 0x0C | u16 | file-header size, expected to be 0x10 |
| 0x0E | u16 | block count, expected to be 1 |
| 0x10 | char[4] | block type, matching the file signature |
| 0x14 | u32 | project-header size |
| 0x18 | u32 ×2 | `projectlink.prevObject` / `nextObject` |
| 0x20 | u16 | project-name length |
| 0x22 | u16 | alignment padding |
| 0x24 | bytes | project name |
| project end | NameTable | resource table |

`NameTable` starts with `u32 tablesize`, `u16 numEntry`, and `u16 padding`.
Each entry is `u16 nameLength`, the name bytes, then `u32 offset` and `u32 size`.
For BREFF, `size` is the complete emitter entry size. For BREFT, `size` is the
32-byte `TextureData` header size; the texture and palette sizes in that header
determine the following payload. Entry offsets are relative to the `NameTable`;
the game converts them to pointers while relocating
(`src/nw4r/ef/ef_resource.cpp:50,170`).

## BREFF emitters

An entry resolves to `EmitterResource` (`include/nw4r/ef/ef_res_emitter.h:263`):

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | char* / u32 | name pointer slot; file contains a relocated value |
| 0x04 | u32 | `headersize`, size of the following emitter description |
| 0x08 | bytes | `EmitterDesc`, opaque to this structural parser |
| 0x08 + headersize | u32 | `ParticleParameterDesc.headersize` |
| +0x04 | bytes | `ParticleParameterDesc` prefix |
| +0x88 | 3 × u16 + bytes | three texture-name lengths followed by names |

`EmitterDesc` is 0x1D8 bytes and `ParticleParameterDesc` has a 0x88-byte fixed
prefix (`include/nw4r/ef/ef_res_emitter.h:193`; `include/nw4r/ef/ef_particle.h:20`).
The three `mTexture` pointer slots are at `ParticleParameterDesc + 0x68`.

The particle description is followed by a `u16 NumPtclTrack`,
`u16 NumPtclInitTrack` header and `NumPtclTrack` pointer/size pairs, then the same
layout for emit tracks. In the files, the “pointer” halves are offset slots;
`EmitterResource::LocateTracks` rewrites them in place as 32-bit pointers and
places tracks in the declared size order
(`include/nw4r/ef/ef_res_emitter.h:360`).

Each track begins with `AnimCurveHeader`
(`include/nw4r/ef/ef_res_animcurve.h:93`):

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | u8 | magic, `0xAC` (animated) or `0xAB` (baked) |
| 0x01 | u8 ×4 | `kindType`, `curveFlag`, `kindEnable`, `processFlag` |
| 0x05 | u8 | `loopCount` |
| 0x06 | u16 | `randomSeed` |
| 0x08 | u16 | `frameLength` |
| 0x0A | u16 | alignment padding |
| 0x0C | u32 | `keyTable` byte length |
| 0x10 | u32 | `rangeTable` byte length |
| 0x14 | u32 | `randomTable` byte length |
| 0x18 | u32 | `nameTable` byte length |
| 0x1C | u32 | `infoTable` byte length (not consumed by the current runtime) |

The four length fields describe sections laid out in header, key, random,
random-range, name, then target-info order. The name section has `u16 count`,
`u16 padding`, `count` 32-bit `work` slots, then `u16 length` plus name bytes for
each entry. The `work` slots are rewritten in place with 32-bit lookup results in
`Resource::relocateCurveTexture` and `relocateCurveChild`
(`src/nw4r/ef/ef_resource.cpp:394,433`). Some release tracks set `infoTable` to
zero even when target info follows; the runtime computes the info address from
the name-table end instead.

## BREFT textures

An entry resolves to `TextureData` (`include/nw4r/ef/ef_res_texture.h:11`):

| Offset | Type | Meaning |
|---:|---|---|
| 0x00 | char* / u32 | name pointer slot |
| 0x04 | u16 / u16 | width / height |
| 0x08 | u32 | texture data size |
| 0x0C | u8 | GX texture format |
| 0x0D | u8 | GX palette format |
| 0x0E | u16 | palette entries |
| 0x10 | u32 | palette data size |
| 0x14 | u8 ×4 | `mipmap`, `min_filt`, `mag_filt`, reserved |
| 0x18 | u8* / u32 | texture pointer slot |
| 0x1C | u8* / u32 | palette pointer slot |
| 0x20 | bytes | texture data, then palette data |

`breftRelocate` writes the name, texture, and palette pointers in place
(`src/nw4r/ef/ef_resource.cpp:170`). Texture and palette payloads remain
big-endian/GX-encoded and are not decoded here.

## PC loading

A little-endian PC loader must byte-swap every integer before use and cannot cast
the on-disk layouts directly to native C structures. On 32-bit little-endian, an
adapter can map the u32 offset slots to native pointers after loading; on 64-bit,
every pointer slot and the 16-byte `ut::Link` embedded in the project header make
in-place native-structure overlay unsafe. The robust approach is to parse into
separate native objects and keep payloads opaque until the PC graphics backend
decodes the GX texture data.
