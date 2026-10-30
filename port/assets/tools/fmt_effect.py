#!/usr/bin/env python3
"""Bounds-checked parser for Wii Play BREFF (REFF) and BREFT (REFT) effects.

Only structural metadata is decoded. Names, images, and other payload bytes stay opaque.
"""

from __future__ import annotations

import argparse
import collections
import os
from pathlib import Path
import struct
import sys
from typing import Any

try:
    from .wiiarc import walk
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from wiiarc import walk


# Struct references (all file data is big-endian):
# - FileHeader/BlockHeader/NameTable: src/nw4r/ef/ef_resource.cpp:30,39,44
# - EffectProject/TextureProject: include/nw4r/ef/ef_resource.h:16,24
# - ut::List/ut::Link: include/nw4r/ut/ut_list.h:8,15
# - EmitterResource: include/nw4r/ef/ef_res_emitter.h:263
# - EmitterDesc: include/nw4r/ef/ef_res_emitter.h:193
# - EmitterDrawSetting: include/nw4r/ef/ef_res_emitter.h:92
# - ParticleParameterDesc: include/nw4r/ef/ef_particle.h:20
# - TextureData: include/nw4r/ef/ef_res_texture.h:11
# - AnimCurveHeader and its tables: include/nw4r/ef/ef_res_animcurve.h:93,119,125,131,137
# - Field parameter blocks (FieldGravityParam ... FieldVortexParam, the info table of a
#   particle F32 track whose kindType is a field): include/nw4r/ef/ef_res_animcurve.h,
#   read by ParticleManager::Calc (src/nw4r/ef/ef_particlemanager.cpp), which switches on
#   kindType 32 gravity, 39 random, 38 spin, 34 magnet, 35 newton, 36 vortex. Kind 33
#   (FIELD_SPEED) has a 4-byte info table that the game never reads; it is decoded here as
#   one f32 for completeness.
#
# Each EmitterResource track table is two u32 arrays (relocated pointers then sizes)
# from EmitterResource::LocateTracks (include/nw4r/ef/ef_res_emitter.h:360). Track
# data immediately follows the two tables. AnimCurveHeader's keyTable, rangeTable,
# randomTable, nameTable and infoTable fields are byte lengths used by the curve
# evaluators and Resource::relocateCurveTexture/relocateCurveChild
# (src/nw4r/ef/ef_resource.cpp:397-469): the game advances from the header to the key
# table, then from each table to the next. The offsets returned by parse() are absolute
# byte offsets so PC tooling does not need to repeat this pointer arithmetic.

EXPECTED_VERSION = 7
FILE_HEADER_SIZE = 0x10
BLOCK_HEADER_SIZE = 8
NAME_TABLE_SIZE = 8
PROJECT_SIZE = 0x14
EMITTER_RESOURCE_SIZE = 8
EMITTER_DESC_SIZE = 0x1D8
PARTICLE_DESC_PREFIX_SIZE = 0x88
TEXTURE_DATA_SIZE = 0x20
ANIM_CURVE_HEADER_SIZE = 0x20
ANIM_TABLE_HEADER_SIZE = 4
ANIM_NAME_ENTRY_SIZE = 4
TEX_LAYERS = 3
ANIM_CURVE_MAGIC = 0xAC
ANIM_BAKED_MAGIC = 0xAB


AC_TYPE_PARTICLE_F32 = 3

# kindType -> (name, struct format, member names); VEC3 members are three floats.
FIELD_PARAMS = {
    32: ("gravity", "4f2B", ("power", "dir.x", "dir.y", "dir.z", "space", "type")),
    33: ("speed", "f", ("speed",)),
    34: ("magnet", "4f4B", ("power", "pos.x", "pos.y", "pos.z", "space", "type",
                             "reserved0", "reserved1")),
    35: ("newton", "5f4B", ("power", "range", "pos.x", "pos.y", "pos.z", "space", "type",
                             "reserved0", "reserved1")),
    36: ("vortex", "6f4B", ("innerSpeed", "outerSpeed", "radius", "rot.x", "rot.y", "rot.z",
                             "space", "type", "reserved0", "reserved1")),
    38: ("spin", "4f4B", ("speed", "rot.x", "rot.y", "rot.z", "space", "type",
                           "reserved0", "reserved1")),
    39: ("random", "2fH2B", ("power", "diffuse", "interval", "flags", "type")),
}


def _field_param(data: bytes, offset: int, size: int, kind: int, what: str) -> dict[str, Any] | None:
    """Decode the field parameter block in a particle F32 track's info table."""
    spec = FIELD_PARAMS.get(kind)
    if spec is None:
        return None
    name, fmt, members = spec
    need = struct.calcsize(">" + fmt)
    if size < need:
        raise FormatError(f"{what}: {name} field info table is {size} bytes, needs {need}")
    values = _unpack(data, offset, fmt, f"{what} {name} field parameters")
    return {"kind": name, "offset": offset, "size": size,
            "values": dict(zip(members, values)),
            "bits": dict(zip(members, _field_bits(data, offset, fmt)))}


def _field_bits(data: bytes, offset: int, fmt: str) -> list[int]:
    """The members as raw integers (floats as their IEEE bits)."""
    raw_fmt = ">" + fmt.replace("f", "I")
    return list(struct.unpack_from(raw_fmt, data, offset))


class FormatError(ValueError):
    pass


def _need(data: bytes, offset: int, size: int, what: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise FormatError(f"{what} at 0x{offset:x} ({size} bytes) is out of bounds")


def _unpack(data: bytes, offset: int, fmt: str, what: str) -> tuple[Any, ...]:
    _need(data, offset, struct.calcsize(fmt), what)
    return struct.unpack_from(">" + fmt, data, offset)


def _u8(data: bytes, offset: int, what: str) -> int:
    return _unpack(data, offset, "B", what)[0]


def _u16(data: bytes, offset: int, what: str) -> int:
    return _unpack(data, offset, "H", what)[0]


def _u32(data: bytes, offset: int, what: str) -> int:
    return _unpack(data, offset, "I", what)[0]


def _fourcc(data: bytes, offset: int, what: str) -> str:
    _need(data, offset, 4, what)
    try:
        return data[offset:offset + 4].decode("ascii")
    except UnicodeDecodeError as error:
        raise FormatError(f"{what} is not ASCII") from error


def _file_header(data: bytes, signature: str) -> dict[str, Any]:
    values = _unpack(data, 0, "4sHHIHH", "FileHeader")
    file_signature, byte_order, version, file_size, header_size, data_blocks = values
    if file_signature != signature.encode("ascii"):
        raise FormatError(f"unexpected signature {file_signature!r}")
    if byte_order != 0xFEFF:
        raise FormatError(f"expected big-endian byte order 0xFEFF, got 0x{byte_order:04x}")
    if version != EXPECTED_VERSION:
        raise FormatError(f"expected version 7, got {version}")
    if file_size != len(data):
        raise FormatError(f"fileSize {file_size} != file length {len(data)}")
    if header_size != FILE_HEADER_SIZE:
        raise FormatError(f"expected headerSize 0x10, got 0x{header_size:x}")
    if data_blocks != 1:
        raise FormatError(f"expected one data block, got {data_blocks}")
    return {
        "signature": signature,
        "byteOrder": byte_order,
        "version": version,
        "fileSize": file_size,
        "headerSize": header_size,
        "dataBlocks": data_blocks,
    }


def _project(data: bytes, offset: int, signature: str) -> tuple[dict[str, Any], int]:
    _need(data, offset, PROJECT_SIZE, f"{signature} project")
    header_size = _u32(data, offset, f"{signature} project headerSize")
    name_length = _u16(data, offset + 0xC, f"{signature} project projectnameLen")
    reserved = _u16(data, offset + 0xE, f"{signature} project PADDING_0xE")
    table_offset = offset + header_size
    if reserved != 0:
        raise FormatError(f"{signature} project PADDING_0xE is nonzero")
    _need(data, offset + 0x10, name_length, f"{signature} project projectname")
    _need(data, table_offset, 0, f"{signature} project end")
    project = {
        "headersize": header_size,
        "projectlink": {"prevObject": _u32(data, offset + 4, "projectlink prevObject"),
                        "nextObject": _u32(data, offset + 8, "projectlink nextObject")},
        "projectnameLen": name_length,
        "PADDING_0xE": reserved,
        "projectnameOffset": offset + 0x10,
    }
    return project, table_offset


def _block(data: bytes, expected: str) -> tuple[dict[str, Any], int, int]:
    kind = _fourcc(data, FILE_HEADER_SIZE, "block kind")
    size = _u32(data, FILE_HEADER_SIZE + 4, "block headersize")
    if kind != expected:
        raise FormatError(f"expected {expected} block, found {kind}")
    project_offset = FILE_HEADER_SIZE + BLOCK_HEADER_SIZE
    block_end = project_offset + size
    _need(data, project_offset, block_end - project_offset, f"{expected} block")
    if block_end != len(data):
        raise FormatError(f"{expected} block ends at 0x{block_end:x}, expected file length 0x{len(data):x}")
    return {"kind": kind, "headersize": size}, project_offset, block_end


def _name_table(data: bytes, offset: int, end: int, label: str) -> tuple[dict[str, Any], int, list[dict[str, Any]]]:
    _need(data, offset, NAME_TABLE_SIZE, f"{label} NameTable")
    table_size = _u32(data, offset, f"{label} NameTable tablesize")
    count = _u16(data, offset + 4, f"{label} NameTable numEntry")
    padding = _u16(data, offset + 6, f"{label} NameTable PADDING_0x4")
    table_end = offset + table_size
    if padding != 0:
        raise FormatError(f"{label} NameTable PADDING_0x4 is nonzero")
    if table_end > end:
        raise FormatError(f"{label} NameTable tablesize 0x{table_size:x} exceeds block")
    entries: list[dict[str, Any]] = []
    cursor = offset + NAME_TABLE_SIZE
    for index in range(count):
        what = f"{label} entry {index}"
        length = _u16(data, cursor, f"{what} name length")
        cursor += 2
        _need(data, cursor, length, f"{what} name")
        name_offset = cursor
        cursor += length
        entry_offset = _u32(data, cursor, f"{what} offset")
        cursor += 4
        entry_size = _u32(data, cursor, f"{what} size")
        cursor += 4
        entries.append({
            "nameLength": length,
            "nameOffset": name_offset,
            "offset": entry_offset,
            "size": entry_size,
        })
    if cursor > table_end:
        raise FormatError(f"{label} NameTable entries exceed tablesize")
    return {"offset": offset, "tablesize": table_size, "numEntry": count,
            "PADDING_0x4": padding, "end": table_end}, cursor, entries


def _range(data: bytes, start: int, size: int, end: int, what: str) -> int:
    finish = start + size
    if start < 0 or size < 0 or finish > end:
        raise FormatError(f"{what} at 0x{start:x} (0x{size:x} bytes) is out of bounds")
    return finish


def _count_table(data: bytes, offset: int, end: int, what: str) -> tuple[int, int]:
    count, init_count = _unpack(data, offset, "HH", what)
    if offset + 4 + count * 8 > end:
        raise FormatError(f"{what} count {count} exceeds bounds")
    return count, init_count


def _anim_name_table(data: bytes, offset: int, end: int, what: str) -> dict[str, Any]:
    count, padding = _unpack(data, offset, "HH", what)  # AnimCurveNameTable: u16 count at 0x0, padding at 0x2 (include/nw4r/ef/ef_res_animcurve.h)
    cursor = offset + ANIM_TABLE_HEADER_SIZE + count * ANIM_NAME_ENTRY_SIZE
    if cursor > end:
        raise FormatError(f"{what} count {count} exceeds curve")
    names: list[dict[str, Any]] = []
    for index in range(count):
        length = _u16(data, cursor, f"{what} name {index} length")
        cursor += 2
        name_offset = cursor
        cursor = _range(data, cursor, length, end, f"{what} name {index}")
        names.append({"length": length, "nameOffset": name_offset})
    return {"offset": offset, "count": count, "PADDING_0x2": padding,
            "workOffsets": [offset + 4 + index * 4 for index in range(count)], "names": names}


def _anim_curve(data: bytes, offset: int, end: int, what: str) -> dict[str, Any]:
    curve_offset = offset
    _need(data, curve_offset, ANIM_CURVE_HEADER_SIZE, f"{what} AnimCurveHeader")
    values = _unpack(
        data, curve_offset, "6B3H5I", f"{what} AnimCurveHeader")
    (magic, kind_type, curve_flag, kind_enable, process_flag, loop_count,
     random_seed, frame_length, padding, key_table, range_table, random_table,
     name_table, info_table) = values
    if magic not in (ANIM_CURVE_MAGIC, ANIM_BAKED_MAGIC):
        raise FormatError(f"{what}: expected AnimCurve magic 0xAC/0xAB, got 0x{magic:02x}")
    if padding != 0:
        raise FormatError(f"{what}: AnimCurveHeader PADDING_0x6 is nonzero")
    key_offset = curve_offset + ANIM_CURVE_HEADER_SIZE
    key_end = _range(data, key_offset, key_table, end, f"{what} key table")
    random_offset = key_end
    random_end = _range(data, random_offset, range_table, end, f"{what} random table")
    random_table_offset = random_end
    random_table_end = _range(
        data, random_table_offset, random_table, end, f"{what} random-range table")
    name_table_offset = random_table_end
    if name_table_offset > end:
        raise FormatError(f"{what}: name table is out of bounds")
    names = _anim_name_table(data, name_table_offset, end, f"{what} name table")
    names_end = names["names"][-1]["nameOffset"] + names["names"][-1]["length"] if names["names"] else name_table_offset + 4
    if info_table:
        _range(data, names_end, info_table, end, f"{what} info table")
    curve = {
        "offset": offset,
        "AnimCurveHeaderOffset": curve_offset,
        "magic": magic,
        "kindType": kind_type,
        "curveFlag": curve_flag,
        "kindEnable": kind_enable,
        "processFlag": process_flag,
        "loopCount": loop_count,
        "randomSeed": random_seed,
        "frameLength": frame_length,
        "PADDING_0x6": padding,
        "nameTableSize": name_table,
        "keyTableOffset": key_offset,
        "keyTableSize": key_table,
        "randomTableOffset": random_offset,
        "randomTableSize": range_table,
        "randomRangeTableOffset": random_table_offset,
        "randomRangeTableSize": random_table,
        "nameTable": names,
        "infoTableOffset": names_end,
        "infoTableSize": info_table,
        "endOffset": end,
    }
    if magic == ANIM_CURVE_MAGIC and curve_flag == AC_TYPE_PARTICLE_F32 and info_table:
        field = _field_param(data, names_end, info_table, kind_type, what)
        if field is not None:
            curve["fieldParam"] = field
    return curve


def _particle_desc(data: bytes, offset: int, end: int, what: str) -> tuple[dict[str, Any], int, int]:
    desc_size = _u32(data, offset, f"{what} ParticleParameterDesc headersize")
    desc_offset = offset + 4
    desc_end = _range(data, desc_offset, desc_size, end, f"{what} ParticleParameterDesc")
    if desc_size < PARTICLE_DESC_PREFIX_SIZE:
        raise FormatError(f"{what} ParticleParameterDesc headersize is too small")
    names_offset = desc_offset + PARTICLE_DESC_PREFIX_SIZE
    texture_names: list[dict[str, Any]] = []
    cursor = names_offset
    for index in range(TEX_LAYERS):
        length = _u16(data, cursor, f"{what} texture name {index} length")
        cursor += 2
        name_offset = cursor
        cursor = _range(data, cursor, length, desc_end, f"{what} texture name {index}")
        texture_names.append({"length": length, "nameOffset": name_offset})
    if cursor > desc_end:
        raise FormatError(f"{what} ParticleParameterDesc headersize does not match texture names")
    desc = {
        "offset": desc_offset,
        "headersize": desc_size,
        "texturePointerOffsets": [desc_offset + 0x68 + index * 4 for index in range(TEX_LAYERS)],
        "textureNames": texture_names,
    }
    return desc, desc_end, cursor


def _emitter(data: bytes, entry: dict[str, Any], table_offset: int, end: int,
             index: int) -> dict[str, Any]:
    what = f"emitter {index}"
    offset = table_offset + entry["offset"]
    finish = offset + entry["size"]
    _range(data, offset, entry["size"], end, f"{what} entry")
    if entry["size"] < EMITTER_RESOURCE_SIZE:
        raise FormatError(f"{what} entry is too small")
    emitter_offset = struct.unpack_from(">i", data, offset + 4)[0]
    _need(data, offset, 8, f"{what} EmitterResource")
    desc_offset = offset + EMITTER_RESOURCE_SIZE
    particle_size_offset = offset + EMITTER_RESOURCE_SIZE + emitter_offset
    _range(data, desc_offset, particle_size_offset - desc_offset, finish, f"{what} emitter section")
    particle_size = _u32(data, particle_size_offset, f"{what} particle headersize")
    particle_offset = particle_size_offset + 4
    particle, track_offset, _ = _particle_desc(
        data, particle_size_offset, finish, what)
    _range(data, particle_offset, particle_size, finish, f"{what} ParticleParameterDesc")
    particle_track_count, particle_init_count = _count_table(
        data, track_offset, finish, f"{what} particle track table")
    emit_table_offset = track_offset + 4 + particle_track_count * 8
    emit_track_count, emit_init_count = _count_table(
        data, emit_table_offset, finish, f"{what} emit track table")
    tail_offset = emit_table_offset + 4 + emit_track_count * 8
    particle_pointer_base = track_offset + 4
    emit_pointer_base = emit_table_offset + 4
    curves: list[dict[str, Any]] = []
    cursor = tail_offset
    for index in range(particle_track_count):
        size_offset = particle_pointer_base + particle_track_count * 4 + index * 4
        size = _u32(data, size_offset, f"{what} particle track {index} size")
        curve_offset = cursor
        curve_end = _range(data, curve_offset, size, finish, f"{what} particle track {index}")
        curve = _anim_curve(data, curve_offset, curve_end, f"{what} particle track {index}")
        curve["pointerOffset"] = particle_pointer_base + index * 4
        curves.append(curve)
        cursor = curve_end
    for index in range(emit_track_count):
        size_offset = emit_pointer_base + emit_track_count * 4 + index * 4
        size = _u32(data, size_offset, f"{what} emit track {index} size")
        curve_offset = cursor
        curve_end = _range(data, curve_offset, size, finish, f"{what} emit track {index}")
        curve = _anim_curve(data, curve_offset, curve_end, f"{what} emit track {index}")
        curve["pointerOffset"] = emit_pointer_base + index * 4
        curves.append(curve)
        cursor = curve_end
    return {
        "offset": offset,
        "size": entry["size"],
        "namePointerOffset": offset,
        "headersize": emitter_offset,
        "emitterDescOffset": desc_offset,
        "particleParameterDesc": particle,
        "particleTrackCount": particle_track_count,
        "particleInitTrackCount": particle_init_count,
        "emitTrackCount": emit_track_count,
        "emitInitTrackCount": emit_init_count,
        "trackPointerOffsets": {
            "particle": [particle_pointer_base + index * 4 for index in range(particle_track_count)],
            "emit": [emit_pointer_base + index * 4 for index in range(emit_track_count)],
        },
        "tracks": curves,
    }


def _parse_breff(data: bytes) -> dict[str, Any]:
    file_header = _file_header(data, "REFF")
    block_header, project_offset, block_end = _block(data, "REFF")
    if block_end != len(data):
        raise FormatError(f"REFF block ends at 0x{block_end:x}, expected file length 0x{len(data):x}")
    project, table_offset = _project(data, project_offset, "REFF")
    name_table, _, entries = _name_table(data, table_offset, block_end, "REFF")
    emitters = [_emitter(data, entry, table_offset, block_end, index)
                for index, entry in enumerate(entries)]
    return {
        "format": "BREFF",
        "fileHeader": file_header,
        "blockHeader": block_header,
        "project": project,
        "nameTable": name_table,
        "emitters": emitters,
    }


def _parse_breft(data: bytes) -> dict[str, Any]:
    file_header = _file_header(data, "REFT")
    block_header, project_offset, block_end = _block(data, "REFT")
    if block_end != len(data):
        raise FormatError(f"REFT block ends at 0x{block_end:x}, expected file length 0x{len(data):x}")
    project, table_offset = _project(data, project_offset, "REFT")
    name_table, _, entries = _name_table(data, table_offset, block_end, "REFT")
    textures: list[dict[str, Any]] = []
    for index, entry in enumerate(entries):
        what = f"texture {index}"
        offset = table_offset + entry["offset"]
        header_end = offset + entry["size"]
        _need(data, offset, entry["size"], f"{what} entry")
        if entry["size"] < TEXTURE_DATA_SIZE:
            raise FormatError(f"{what} entry is too small for TextureData")
        _need(data, offset, TEXTURE_DATA_SIZE, f"{what} TextureData")
        width, height = _unpack(data, offset + 4, "HH", f"{what} TextureData dimensions")
        data_size = _u32(data, offset + 8, f"{what} TextureData dataSize")
        format_ = _u8(data, offset + 0xC, f"{what} TextureData format")
        tlut_format = _u8(data, offset + 0xD, f"{what} TextureData tlutFormat")
        tlut_entries = _u16(data, offset + 0xE, f"{what} TextureData tlutEntries")
        tlut_size = _u32(data, offset + 0x10, f"{what} TextureData tlutSize")
        mipmap, min_filter, mag_filter, reserved = _unpack(
            data, offset + 0x14, "BBBB", f"{what} TextureData filters")
        texture_offset = offset + TEXTURE_DATA_SIZE
        _range(data, texture_offset, 0, block_end, f"{what} texture data")
        texture_end = _range(data, texture_offset, data_size, block_end, f"{what} texture data")
        tlut_offset = texture_end
        tlut_end = _range(data, tlut_offset, tlut_size, block_end, f"{what} tlut data")
        textures.append({
            "offset": offset,
            "size": entry["size"],
            "namePointerOffset": offset,
            "width": width,
            "height": height,
            "dataSize": data_size,
            "format": format_,
            "tlutFormat": tlut_format,
            "tlutEntries": tlut_entries,
            "tlutSize": tlut_size,
            "mipmap": mipmap,
            "min_filt": min_filter,
            "mag_filt": mag_filter,
            "reserved": reserved,
            "textureDataEndOffset": texture_end,
            "tlutDataEndOffset": tlut_end,
            "texturePointerOffset": offset + 0x18,
            "tlutPointerOffset": offset + 0x1C,
            "textureDataOffset": texture_offset,
            "tlutDataOffset": tlut_offset,
        })
    return {
        "format": "BREFT",
        "fileHeader": file_header,
        "blockHeader": block_header,
        "project": project,
        "nameTable": name_table,
        "textures": textures,
    }


def parse(data: bytes) -> dict[str, Any]:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("parse expects bytes")
    data = bytes(data)
    if len(data) < 4:
        raise FormatError("file is too short for a signature")
    signature = data[:4]
    if signature == b"REFF":
        return _parse_breff(data)
    if signature == b"REFT":
        return _parse_breft(data)
    raise FormatError(f"unsupported signature {signature!r}")


def _counter_text(counter: collections.Counter[int]) -> str:
    return ", ".join(f"{key}={value}" for key, value in sorted(counter.items()))


def check(root: str) -> tuple[dict[str, int], dict[str, int], dict[str, dict[str, Any]]]:
    counts = {"BREFF": 0, "BREFT": 0}
    errors = {"BREFF": 0, "BREFT": 0}
    facts: dict[str, dict[str, Any]] = {
        "BREFF": {"versions": collections.Counter(), "emitters": 0,
                  "particleTracks": 0, "emitTracks": 0,
                  "fieldParams": collections.Counter()},
        "BREFT": {"versions": collections.Counter(), "textures": 0,
                  "formats": collections.Counter(), "tlutFormats": collections.Counter()},
    }
    for path, blob in walk(root):
        if blob[:4] not in (b"REFF", b"REFT"):
            continue
        format_name = blob[:4].decode("ascii")
        format_name = "BREFF" if format_name == "REFF" else "BREFT"
        try:
            parsed = parse(blob)
            counts[format_name] += 1
            facts[format_name]["versions"][parsed["fileHeader"]["version"]] += 1
            if format_name == "BREFF":
                facts["BREFF"]["emitters"] += len(parsed["emitters"])
                facts["BREFF"]["particleTracks"] += sum(
                    emitter["particleTrackCount"] for emitter in parsed["emitters"])
                facts["BREFF"]["emitTracks"] += sum(
                    emitter["emitTrackCount"] for emitter in parsed["emitters"])
                for emitter in parsed["emitters"]:
                    for track in emitter["tracks"]:
                        if "fieldParam" in track:
                            facts["BREFF"]["fieldParams"][track["fieldParam"]["kind"]] += 1
            else:
                facts["BREFT"]["textures"] += len(parsed["textures"])
                for texture in parsed["textures"]:
                    facts["BREFT"]["formats"][texture["format"]] += 1
                    facts["BREFT"]["tlutFormats"][texture["tlutFormat"]] += 1
        except Exception as error:
            errors[format_name] += 1
            print(f"ERROR {format_name} {path}: {error}", file=sys.stderr)
    return counts, errors, facts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", metavar="ROOT", help="parse every BREFF/BREFT file under ROOT")
    args = parser.parse_args(argv)
    if args.check is None:
        parser.print_usage()
        return 2
    if not os.path.isdir(args.check):
        print(f"not a directory: {args.check}", file=sys.stderr)
        return 2
    counts, errors, facts = check(args.check)
    breff = facts["BREFF"]
    breft = facts["BREFT"]
    print(
        f"BREFF: files parsed={counts['BREFF']} errors={errors['BREFF']} "
        f"versions[({_counter_text(breff['versions'])})] emitters={breff['emitters']} "
        f"particleTracks={breff['particleTracks']} emitTracks={breff['emitTracks']} "
        f"fieldParams[({', '.join(f'{k}={v}' for k, v in sorted(breff['fieldParams'].items()))})]"
    )
    print(
        f"BREFT: files parsed={counts['BREFT']} errors={errors['BREFT']} "
        f"versions[({_counter_text(breft['versions'])})] textures={breft['textures']} "
        f"formats[({_counter_text(breft['formats'])})] "
        f"tlutFormats[({_counter_text(breft['tlutFormats'])})]"
    )
    return 1 if errors["BREFF"] or errors["BREFT"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
