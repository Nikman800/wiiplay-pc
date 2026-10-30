#!/usr/bin/env python3
"""Bounds-checked parser for the Wii Play sound formats.

Only structural data is decoded. Names and payload bytes remain opaque.
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


# Struct references:
# - ut::BinaryFileHeader/BinaryBlockHeader: include/nw4r/ut/ut_binaryFileFormat.h:8,13
# - Util::DataRef/Table: include/nw4r/snd/snd_Util.h:63,103
# - SoundArchiveFile structs: include/nw4r/snd/snd_SoundArchiveFile.h:20,29,34,38,63,73,82,88,104,110,119,128,137,149,159,168,178
# - SeqFile structs: include/nw4r/snd/snd_SeqFile.h:18,26
# - BankFile structs: include/nw4r/snd/snd_BankFile.h:25,38,43,53,61,66
# - WaveFile structs: include/nw4r/snd/snd_WaveFile.h:23,37
# - WsdFile structs: include/nw4r/snd/snd_WsdFile.h:21,36,59,73,79,86
# - StrmFile structs: include/nw4r/snd/snd_StrmFile.h:21,41,46,52,56,61,71


class FormatError(ValueError):
    pass


def _need(data: bytes, offset: int, size: int, what: str) -> None:
    if offset < 0 or size < 0 or len(data) < offset + size:
        raise FormatError(f"{what} at 0x{offset:x} ({size} bytes) is out of bounds")


def _unpack(data: bytes, offset: int, fmt: str, what: str):
    unpack_format = ">" + fmt
    _need(data, offset, struct.calcsize(unpack_format), what)
    return struct.unpack_from(unpack_format, data, offset)


def _u8(data: bytes, offset: int, what: str) -> int:
    return _unpack(data, offset, "B", what)[0]


def _u16(data: bytes, offset: int, what: str) -> int:
    return _unpack(data, offset, "H", what)[0]


def _u32(data: bytes, offset: int, what: str) -> int:
    return _unpack(data, offset, "I", what)[0]


def _fourcc(data: bytes, offset: int, what: str) -> str:
    _need(data, offset, 4, what)
    return data[offset:offset + 4].decode("ascii")


def _file_header(data: bytes) -> dict[str, Any]:
    signature, byte_order, version, file_size, header_size, data_blocks = _unpack(
        data, 0, "4sHHIHH", "BinaryFileHeader"
    )
    if signature not in (b"RSAR", b"RSTM", b"RSEQ", b"RBNK", b"RWSD", b"RWAR"):
        raise FormatError(f"unexpected file signature {signature!r}")
    if byte_order != 0xFEFF:
        raise FormatError("expected big-endian byte-order mark 0xFEFF")
    if file_size < header_size:
        raise FormatError(f"fileSize {file_size} is smaller than headerSize {header_size}")
    if header_size not in (0x20, 0x40):
        raise FormatError(f"expected headerSize 0x20 or 0x40, got 0x{header_size:x}")
    if not 1 <= data_blocks <= 4:
        raise FormatError(f"unexpected dataBlocks count {data_blocks}")
    return {
        "signature": signature.decode("ascii"),
        "byteOrder": byte_order,
        "version": version,
        "fileSize": file_size,
        "headerSize": header_size,
        "dataBlocks": data_blocks,
    }


def _block_header(data: bytes, offset: int, expected: str | None = None) -> tuple[str, int]:
    kind = _fourcc(data, offset, f"{expected or 'binary'} block kind")
    size = _u32(data, offset + 4, f"{kind} block size")
    if expected is not None and kind != expected:
        raise FormatError(f"expected {expected} block, found {kind} at 0x{offset:x}")
    _need(data, offset, size, f"{kind} block")
    return kind, size


def _data_ref(
    data: bytes,
    offset: int,
    what: str,
    expected_types: set[int] | None = None,
    allow_address: bool = False,
    allow_null: bool = False,
) -> dict[str, Any]:
    ref_type, data_type, reserved, value = _unpack(data, offset, "BBHI", what)
    if ref_type == 0 and (allow_address or allow_null):
        pass
    elif ref_type != 1:
        raise FormatError(f"{what}: expected REFTYPE_OFFSET, got {ref_type}")
    if reserved != 0:
        raise FormatError(f"{what}: reserved field is nonzero")
    if expected_types is not None and data_type not in expected_types:
        raise FormatError(f"{what}: unexpected dataType {data_type}")
    return {"refType": ref_type, "dataType": data_type, "reserved": reserved, "value": value}


def _nullable_ref_target(
    ref: dict[str, Any],
    base: int,
    end: int,
    what: str,
) -> int | None:
    if ref["refType"] == 0:
        return None
    return _ref_target(ref, base, end, what)


def _ref_target(ref: dict[str, Any], base: int, end: int, what: str) -> int:
    target = base + ref["value"]
    if target < base or target > end:
        raise FormatError(f"{what} target 0x{target:x} is outside block")
    return target


def _table_count(data: bytes, offset: int, end: int, element_size: int, what: str) -> int:
    _need(data, offset, 4, what)
    count = _u32(data, offset, what + " count")
    if count > (end - offset - 4) // element_size:
        raise FormatError(f"{what} count {count} exceeds block bounds")
    return count


def _parse_rseq(data: bytes) -> dict[str, Any]:
    header = _file_header(data)
    if header["signature"] != "RSEQ" or header["dataBlocks"] not in (1, 2) or header["version"] > 0x0100:
        raise FormatError("not a supported RSEQ v1.0 file")
    data_offset, data_size, label_offset, label_size = _unpack(
        data, 0x10, "IIII", "RSEQ Header"
    )
    _need(data, data_offset, data_size, "RSEQ data block")
    _need(data, label_offset, label_size, "RSEQ label block")
    kind, size = _block_header(data, data_offset, "DATA")
    base_offset = _u32(data, data_offset + 8, "RSEQ DataBlock.baseOffset")
    if size < 12 or data_offset + size < data_offset + 8 + base_offset:
        raise FormatError("RSEQ DataBlock.baseOffset is out of bounds")
    label_kind = None
    label_block = None
    if label_size:
        label_kind, label_size_field = _block_header(data, label_offset, "LABL")
        _need(data, label_offset, label_size_field, "RSEQ LABL block")
        label_count = _table_count(data, label_offset + 8, label_offset + label_size_field, 4, "RSEQ label table")
        label_offsets = [
            _u32(data, label_offset + 12 + label * 4, f"RSEQ label {label} offset")
            for label in range(label_count)
        ]
        for label, label_offset_value in enumerate(label_offsets):
            string_target = label_offset + 8 + label_offset_value
            if label_offset_value == 0 or string_target > label_offset + label_size_field - 1:
                raise FormatError(f"RSEQ label {label} string offset is out of bounds")
            terminator = data.find(b"\0", string_target, label_offset + label_size_field)
            if terminator < 0:
                raise FormatError(f"RSEQ label {label} string is not NUL-terminated")
        label_block = {
            "blockHeader": {"kind": label_kind, "size": label_size_field},
            "count": label_count,
        }
    return {
        "format": "RSEQ",
        "fileHeader": header,
        "header": {
            "dataBlockOffset": data_offset,
            "dataBlockSize": data_size,
            "labelBlockOffset": label_offset,
            "labelBlockSize": label_size,
        },
        "dataBlock": {"blockHeader": {"kind": kind, "size": size}, "baseOffset": base_offset},
        "labelBlock": label_block,
    }


def _parse_rwar(data: bytes) -> dict[str, Any]:
    header = _file_header(data)
    if header["signature"] != "RWAR" or header["dataBlocks"] != 2 or header["version"] > 0x0100:
        raise FormatError("not a supported RWAR v1.0 file")
    table_offset, table_size, data_offset, data_size = _unpack(data, 0x10, "IIII", "RWAR Header")
    _need(data, table_offset, table_size, "RWAR table block")
    _need(data, data_offset, data_size, "RWAR data block")
    kind, size = _block_header(data, table_offset, "TABL")
    data_kind, data_block_size = _block_header(data, data_offset, "DATA")
    if size < 12:
        raise FormatError("RWAR TABL block is smaller than its wave table")
    wave_count = _table_count(data, table_offset + 8, table_offset + size, 4, "RWAR wave table")
    waves = []
    for i in range(wave_count):
        wave_offset = _u32(data, table_offset + 12 + i * 4, f"RWAR wave {i} offset")
        wave_target = data_offset + wave_offset
        if wave_offset < 8 or wave_target > data_offset + data_block_size:
            raise FormatError(f"RWAR wave {i} offset is outside the DATA block")
        waves.append(_wave_info(data, wave_target, data_offset + data_block_size, f"RWAR wave {i}"))
    return {
        "format": "RWAR",
        "fileHeader": header,
        "header": {
            "tablBlockOffset": table_offset,
            "tablBlockSize": table_size,
            "dataBlockOffset": data_offset,
            "dataBlockSize": data_size,
        },
        "tablBlock": {
            "blockHeader": {"kind": kind, "size": size},
            "waveCount": wave_count,
            "waves": waves,
        },
        "dataBlock": {"blockHeader": {"kind": data_kind, "size": data_block_size}},
    }


def _bank_instrument_node(
    data: bytes,
    ref: dict[str, Any],
    base: int,
    end: int,
    what: str,
) -> dict[str, Any]:
    data_type = ref["dataType"]
    node: dict[str, Any] = {"dataType": data_type, "offset": ref["value"]}
    target = _nullable_ref_target(ref, base, end, what)
    if target is None:
        return node
    if data_type == 0:
        return node
    if data_type == 1:
        _need(data, target, 0x14, f"{what} InstParam")
        node["InstParam"] = {
            "waveIndex": _unpack(data, target, "i", f"{what} InstParam.waveIndex")[0],
            "attack": _u8(data, target + 4, f"{what} InstParam.attack"),
            "decay": _u8(data, target + 5, f"{what} InstParam.decay"),
            "sustain": _u8(data, target + 6, f"{what} InstParam.sustain"),
            "release": _u8(data, target + 7, f"{what} InstParam.release"),
            "originalKey": _u8(data, target + 0xC, f"{what} InstParam.originalKey"),
            "volume": _u8(data, target + 0xD, f"{what} InstParam.volume"),
            "pan": _u8(data, target + 0xE, f"{what} InstParam.pan"),
            "tune": _unpack(data, target + 0x10, "f", f"{what} InstParam.tune")[0],
        }
        return node
    if data_type == 2:
        _need(data, target, 1, f"{what} RangeTable")
        table_size = _u8(data, target, f"{what} RangeTable.tableSize")
        start = (table_size + 2 + 3) & ~3
        keys_end = target + 1 + table_size
        _need(data, target, start, f"{what} RangeTable")
        node["RangeTable"] = {"tableSize": table_size, "keysEnd": keys_end, "subrefsOffset": start}
        return node
    _need(data, target, 4, f"{what} IndexTable")
    minimum, maximum, reserved = _unpack(data, target, "BBH", f"{what} IndexTable")
    if reserved != 0:
        raise FormatError(f"{what} IndexTable.reserved field is nonzero")
    node["IndexTable"] = {"min": minimum, "max": maximum, "subrefsOffset": 4}
    return node


def _wave_info(data: bytes, offset: int, block_end: int, what: str) -> dict[str, Any]:
    _need(data, offset, 0x1C, what)
    format_, loop_flag, channels, rate24 = _unpack(data, offset, "BBBB", what)
    rate = _u16(data, offset + 4, what + ".sampleRate")
    padding = _u16(data, offset + 6, what + ".PADDING_0x6")
    loop_start, loop_end, channel_table_offset, data_offset, reserved = _unpack(
        data, offset + 8, "IIIII", what
    )
    if format_ > 2:
        raise FormatError(f"{what}: unsupported wave format {format_}")
    if channels not in (1, 2):
        raise FormatError(f"{what}: unsupported channel count {channels}")
    if padding != 0 or reserved != 0:
        raise FormatError(f"{what}: reserved field is nonzero")
    if loop_end < loop_start:
        raise FormatError(f"{what}: loopEnd precedes loopStart")
    table_target = offset + channel_table_offset
    if channel_table_offset < 0x1C or table_target > block_end:
        raise FormatError(f"{what}: channelInfoTableOffset is out of bounds")
    table_end = min(block_end, table_target + channels * 4)
    if table_target + channels * 4 > table_end:
        raise FormatError(f"{what}: channel offset table is out of bounds")
    channel_offsets = [_u32(data, table_target + i * 4, f"{what} channel offset") for i in range(channels)]
    channels_info = []
    for channel, channel_offset in enumerate(channel_offsets):
        channel_what = f"{what} channel {channel}"
        channel_target = offset + channel_offset
        if channel_target < offset + 0x1C or channel_target + 0x1C > block_end:
            raise FormatError(f"{channel_what}: WaveChannelInfo is out of bounds")
        values = _unpack(data, channel_target, "7I", channel_what)
        if values[6] != 0:
            raise FormatError(f"{channel_what}: reserved field is nonzero")
        if values[1] and (offset + values[1] < offset + 0x1C or offset + values[1] + 0x30 > block_end):
            raise FormatError(f"{channel_what}: adpcmOffset is out of bounds")
        channels_info.append(
            {
                "channelDataOffset": values[0],
                "adpcmOffset": values[1],
                "volumeFrontLeft": values[2],
                "volumeFrontRight": values[3],
                "volumeRearLeft": values[4],
                "volumeRearRight": values[5],
                "reserved": values[6],
            }
        )
    return {
        "format": format_,
        "loopFlag": loop_flag,
        "numChannels": channels,
        "sampleRate": (rate24 << 16) | rate,
        "loopStart": loop_start,
        "loopEnd": loop_end,
        "channelInfoTableOffset": channel_table_offset,
        "dataOffset": data_offset,
        "channels": channels_info,
    }


def _parse_rbnk(data: bytes) -> dict[str, Any]:
    header = _file_header(data)
    if header["signature"] != "RBNK" or header["dataBlocks"] != 2 or header["version"] > 0x0101:
        raise FormatError("not a supported RBNK v1.0/v1.1 file")
    data_offset, data_size, wave_offset, wave_size = _unpack(data, 0x10, "IIII", "RBNK Header")
    _need(data, data_offset, data_size, "RBNK data block")
    _need(data, wave_offset, wave_size, "RBNK wave block")
    data_kind, data_block_size = _block_header(data, data_offset, "DATA")
    wave_kind, wave_block_size = _block_header(data, wave_offset, "WAVE")
    if data_block_size < 12 or wave_block_size < 12:
        raise FormatError("RBNK block is smaller than its table")
    data_end = data_offset + data_block_size
    wave_end = wave_offset + wave_block_size
    data_count = _table_count(data, data_offset + 8, data_end, 8, "RBNK instrument table")
    instruments = []
    instrument_nodes = []
    for i in range(data_count):
        ref = _data_ref(
            data,
            data_offset + 12 + i * 8,
            f"RBNK instrument {i}",
            set(range(4)),
            allow_null=True,
        )
        instruments.append(ref)
        instrument_nodes.append(_bank_instrument_node(data, ref, data_offset + 8, data_end, f"RBNK instrument {i}"))
    wave_count = _table_count(data, wave_offset + 8, wave_end, 8, "RBNK wave table")
    waves = []
    for i in range(wave_count):
        ref = _data_ref(data, wave_offset + 12 + i * 8, f"RBNK wave {i}", {0})
        target = _ref_target(ref, wave_offset + 8, wave_end, f"RBNK wave {i}")
        waves.append(_wave_info(data, target, wave_end, f"RBNK wave {i}"))
    return {
        "format": "RBNK",
        "fileHeader": header,
        "header": {
            "dataBlockOffset": data_offset,
            "dataBlockSize": data_size,
            "waveBlockOffset": wave_offset,
            "waveBlockSize": wave_size,
        },
        "dataBlock": {
            "blockHeader": {"kind": data_kind, "size": data_block_size},
            "count": data_count,
            "items": instruments,
            "nodes": instrument_nodes,
        },
        "waveBlock": {"blockHeader": {"kind": wave_kind, "size": wave_block_size}, "count": wave_count, "items": waves},
    }


def _parse_rwsd(data: bytes) -> dict[str, Any]:
    header = _file_header(data)
    if header["signature"] != "RWSD" or header["dataBlocks"] != 2 or header["version"] > 0x0102:
        raise FormatError("not a supported RWSD v1.0-v1.2 file")
    data_offset, data_size, wave_offset, wave_size = _unpack(data, 0x10, "IIII", "RWSD Header")
    _need(data, data_offset, data_size, "RWSD data block")
    _need(data, wave_offset, wave_size, "RWSD wave block")
    data_kind, data_block_size = _block_header(data, data_offset, "DATA")
    wave_kind, wave_block_size = _block_header(data, wave_offset, "WAVE")
    if data_block_size < 12 or wave_block_size < 12:
        raise FormatError("RWSD block is smaller than its table")
    data_end = data_offset + data_block_size
    wave_end = wave_offset + wave_block_size
    wsd_count = _u32(data, data_offset + 8, "RWSD wsdCount")
    if wsd_count > (data_end - data_offset - 12) // 8:
        raise FormatError("RWSD wsdCount exceeds block bounds")
    wsds = []
    for i in range(wsd_count):
        ref = _data_ref(data, data_offset + 12 + i * 8, f"RWSD wsd {i}", {0})
        target = _ref_target(ref, data_offset + 8, data_end, f"RWSD wsd {i}")
        wsd_info_ref = _data_ref(data, target, f"RWSD wsd {i} info", {0})
        track_ref = _data_ref(data, target + 8, f"RWSD wsd {i} track table", {0})
        note_ref = _data_ref(data, target + 16, f"RWSD wsd {i} note table", {0})
        info_target = _ref_target(wsd_info_ref, data_offset + 8, data_end, f"RWSD wsd {i} info")
        track_target = _ref_target(track_ref, data_offset + 8, data_end, f"RWSD wsd {i} track table")
        note_target = _ref_target(note_ref, data_offset + 8, data_end, f"RWSD wsd {i} note table")
        track_count = _table_count(data, track_target, data_end, 8, f"RWSD wsd {i} track table")
        note_count = _table_count(data, note_target, data_end, 8, f"RWSD wsd {i} note table")
        info = {
            "pitch": _unpack(data, info_target, "f", f"RWSD wsd {i} pitch")[0],
            "pan": _u8(data, info_target + 4, f"RWSD wsd {i} pan"),
            "surroundPan": _u8(data, info_target + 5, f"RWSD wsd {i} surroundPan"),
            "fxSendA": _u8(data, info_target + 6, f"RWSD wsd {i} fxSendA"),
            "fxSendB": _u8(data, info_target + 7, f"RWSD wsd {i} fxSendB"),
            "fxSendC": _u8(data, info_target + 8, f"RWSD wsd {i} fxSendC"),
            "mainSend": _u8(data, info_target + 9, f"RWSD wsd {i} mainSend"),
            "graphEnvTableRef": _data_ref(
                data, info_target + 12, f"RWSD wsd {i} graph env", {0}, allow_null=True
            ),
            "randomizerTableRef": _data_ref(
                data, info_target + 20, f"RWSD wsd {i} randomizer", {0}, allow_null=True
            ),
            "reserved": _u32(data, info_target + 28, f"RWSD wsd {i} reserved"),
        }
        _nullable_ref_target(info["graphEnvTableRef"], data_offset + 8, data_end, f"RWSD wsd {i} graph env")
        _nullable_ref_target(info["randomizerTableRef"], data_offset + 8, data_end, f"RWSD wsd {i} randomizer")
        notes = []
        for note in range(note_count):
            note_item_ref = _data_ref(data, note_target + 4 + note * 8, f"RWSD note {i}:{note}", {0})
            note_target_item = _ref_target(note_item_ref, data_offset + 8, data_end, f"RWSD note {i}:{note}")
            notes.append(
                {
                    "waveIndex": _unpack(data, note_target_item, "i", f"RWSD note {i}:{note} waveIndex")[0],
                    "attack": _u8(data, note_target_item + 4, f"RWSD note {i}:{note} attack"),
                    "decay": _u8(data, note_target_item + 5, f"RWSD note {i}:{note} decay"),
                    "sustain": _u8(data, note_target_item + 6, f"RWSD note {i}:{note} sustain"),
                    "release": _u8(data, note_target_item + 7, f"RWSD note {i}:{note} release"),
                    "hold": _u16(data, note_target_item + 8, f"RWSD note {i}:{note} hold"),
                    "originalKey": _u8(data, note_target_item + 12, f"RWSD note {i}:{note} originalKey"),
                    "volume": _u8(data, note_target_item + 13, f"RWSD note {i}:{note} volume"),
                    "pan": _u8(data, note_target_item + 14, f"RWSD note {i}:{note} pan"),
                    "surroundPan": _u8(data, note_target_item + 15, f"RWSD note {i}:{note} surroundPan"),
                    "pitch": _unpack(data, note_target_item + 16, "f", f"RWSD note {i}:{note} pitch")[0],
                }
            )
        wsds.append({"wsdInfo": info, "trackCount": track_count, "notes": notes})
    wave_count = _u32(data, wave_offset + 8, "RWSD waveCount")
    if wave_count > (wave_end - wave_offset - 12) // 4:
        raise FormatError("RWSD waveCount exceeds block bounds")
    waves = []
    for i in range(wave_count):
        wave_item_offset = _u32(data, wave_offset + 12 + i * 4, f"RWSD wave {i} offset")
        target = wave_offset + wave_item_offset
        if target < wave_offset or target > wave_end:
            raise FormatError(f"RWSD wave {i} offset is out of bounds")
        waves.append(_wave_info(data, target, wave_end, f"RWSD wave {i}"))
    return {
        "format": "RWSD",
        "fileHeader": header,
        "header": {
            "dataBlockOffset": data_offset,
            "dataBlockSize": data_size,
            "waveBlockOffset": wave_offset,
            "waveBlockSize": wave_size,
        },
        "dataBlock": {"blockHeader": {"kind": data_kind, "size": data_block_size}, "wsdCount": wsd_count, "wsds": wsds},
        "waveBlock": {"blockHeader": {"kind": wave_kind, "size": wave_block_size}, "waveCount": wave_count, "waves": waves},
    }


def _symbol_tree(data: bytes, offset: int, block_end: int, what: str) -> dict[str, Any]:
    root = _u32(data, offset, what + ".rootIdx")
    count = _table_count(data, offset + 4, block_end, 20, what + ".nodeTable")
    if root >= count:
        raise FormatError(f"{what}: rootIdx exceeds node table")
    nodes = []
    for i in range(count):
        node_offset = offset + 8 + i * 20
        flags, bit, left, right, string_index, node_id = _unpack(
            data, node_offset, "HHIIII", f"{what} node {i}"
        )
        is_leaf = bool(flags & 1)
        if not is_leaf and (left >= count or right >= count):
            raise FormatError(f"{what} node {i}: child index exceeds node table")
        nodes.append(
            {
                "flags": flags,
                "bit": bit,
                "leftIdx": left,
                "rightIdx": right,
                "strIdx": string_index,
                "id": node_id,
            }
        )
    return {"rootIdx": root, "nodeTable": {"count": count, "items": nodes}}


def _parse_rsar(data: bytes) -> dict[str, Any]:
    header = _file_header(data)
    if header["signature"] != "RSAR" or header["dataBlocks"] != 3 or not 0x0100 <= header["version"] <= 0x0103:
        raise FormatError("not a supported RSAR v1.0-v1.3 file")
    symbol_offset, symbol_size, info_offset, info_size, file_offset, file_size = _unpack(
        data, 0x10, "IIIIII", "SoundArchiveFile.Header"
    )
    _need(data, symbol_offset, symbol_size, "RSAR SYMB block")
    _need(data, info_offset, info_size, "RSAR INFO block")
    _need(data, file_offset, file_size, "RSAR FILE block")
    symbol_kind, symbol_block_size = _block_header(data, symbol_offset, "SYMB")
    info_kind, info_block_size = _block_header(data, info_offset, "INFO")
    file_kind, file_block_size = _block_header(data, file_offset, "FILE")
    symbol_end = symbol_offset + symbol_block_size
    info_end = info_offset + info_block_size

    string_table_offset, sound_tree_offset, player_tree_offset, group_tree_offset, bank_tree_offset = _unpack(
        data, symbol_offset + 8, "IIIII", "StringChunk"
    )
    symbol = {}
    for name, relative in (
        ("stringTable", string_table_offset),
        ("soundTree", sound_tree_offset),
        ("playerTree", player_tree_offset),
        ("groupTree", group_tree_offset),
        ("bankTree", bank_tree_offset),
    ):
        target = symbol_offset + 8 + relative
        if target < symbol_offset + 8 or target > symbol_end:
            raise FormatError(f"SYMB {name} offset is out of bounds")
        symbol[name] = target
    string_count = _table_count(
        data, symbol["stringTable"], symbol_end, 4, "SYMB string offset table"
    )
    symbol_data = {
        "blockHeader": {"kind": symbol_kind, "size": symbol_block_size},
        "tableOffset": string_table_offset,
        "soundTreeOffset": sound_tree_offset,
        "playerTreeOffset": player_tree_offset,
        "groupTreeOffset": group_tree_offset,
        "bankTreeOffset": bank_tree_offset,
        "stringCount": string_count,
        "soundTree": _symbol_tree(data, symbol["soundTree"], symbol_end, "SYMB sound tree"),
        "playerTree": _symbol_tree(data, symbol["playerTree"], symbol_end, "SYMB player tree"),
        "groupTree": _symbol_tree(data, symbol["groupTree"], symbol_end, "SYMB group tree"),
        "bankTree": _symbol_tree(data, symbol["bankTree"], symbol_end, "SYMB bank tree"),
    }

    info_base = info_offset + 8
    names = ("sound", "bank", "player", "file", "group", "playerInfo")
    info_refs = [
        _data_ref(data, info_base + i * 8, f"INFO {name} table", {0})
        for i, name in enumerate(names)
    ]
    table_targets = [
        _ref_target(ref, info_base, info_end, f"INFO {name} table")
        for ref, name in zip(info_refs, names)
    ]
    sound_count = _table_count(data, table_targets[0], info_end, 8, "sound table")
    bank_count = _table_count(data, table_targets[1], info_end, 8, "bank table")
    player_count = _table_count(data, table_targets[2], info_end, 8, "player table")
    file_count = _table_count(data, table_targets[3], info_end, 8, "file table")
    group_count = _table_count(data, table_targets[4], info_end, 8, "group table")
    if group_count == 0:
        raise FormatError("INFO group table is empty")

    sounds = []
    for i in range(sound_count):
        item_ref = _data_ref(data, table_targets[0] + 4 + i * 8, f"sound {i}", {0})
        target = _ref_target(item_ref, info_base, info_end, f"sound {i}")
        _need(data, target, 0x2C, f"SoundCommonInfo {i}")
        string_id, file_id, player_id = _unpack(data, target, "III", f"SoundCommonInfo {i}")
        param_ref = _data_ref(data, target + 0xC, f"sound {i} 3d param")
        if param_ref["dataType"] == 0:
            _ref_target(param_ref, info_base, info_end, f"sound {i} 3d param")
        fields = _unpack(data, target + 0x14, "BBBB", f"sound {i} fields")
        sound_info_ref = _data_ref(data, target + 0x18, f"sound {i} sound info", set(range(4)))
        user_params = _unpack(data, target + 0x20, "II", f"sound {i} user params")
        pan_mode, pan_curve = _unpack(data, target + 0x28, "BB", f"sound {i} pan")
        # soundInfoRef (SoundInfoOffset, snd_SoundArchiveFile.h) points at the type-specific
        # SeqSoundInfo / StrmSoundInfo / WaveSoundInfo; follow it rather than assume it follows
        # the common info.
        type_info = {}
        if sound_info_ref["dataType"] == 1:
            info = _ref_target(sound_info_ref, info_base, info_end, f"seq sound {i} info")
            _need(data, info, 0xE, f"SeqSoundInfo {i}")
            data_offset, bank_id, alloc_track = _unpack(data, info, "III", f"SeqSoundInfo {i}")
            type_info = {"dataOffset": data_offset, "bankId": bank_id, "allocTrack": alloc_track}
        elif sound_info_ref["dataType"] == 3:
            info = _ref_target(sound_info_ref, info_base, info_end, f"wave sound {i} info")
            _need(data, info, 0xA, f"WaveSoundInfo {i}")
            sub_no, alloc_track = _unpack(data, info, "iI", f"WaveSoundInfo {i}")
            type_info = {"subNo": sub_no, "allocTrack": alloc_track}
        sounds.append(
            {
                "stringId": string_id,
                "fileId": file_id,
                "playerId": player_id,
                "param3dRef": param_ref,
                "volume": fields[0],
                "playerPriority": fields[1],
                "soundType": fields[2],
                "remoteFilter": fields[3],
                "soundInfoRef": sound_info_ref,
                "userParam": list(user_params),
                "panMode": pan_mode,
                "panCurve": pan_curve,
                "typeInfo": type_info,
            }
        )

    banks = []
    for i in range(bank_count):
        item_ref = _data_ref(data, table_targets[1] + 4 + i * 8, f"bank {i}", {0})
        target = _ref_target(item_ref, info_base, info_end, f"bank {i}")
        banks.append({"stringId": _u32(data, target, f"bank {i}.stringId"), "fileId": _u32(data, target + 4, f"bank {i}.fileId")})

    players = []
    for i in range(player_count):
        item_ref = _data_ref(data, table_targets[2] + 4 + i * 8, f"player {i}", {0})
        target = _ref_target(item_ref, info_base, info_end, f"player {i}")
        players.append(
            {
                "stringId": _u32(data, target, f"player {i}.stringId"),
                "playableSoundCount": _u8(data, target + 4, f"player {i}.playableSoundCount"),
                "heapSize": _u32(data, target + 8, f"player {i}.heapSize"),
            }
        )

    files = []
    for i in range(file_count):
        item_ref = _data_ref(data, table_targets[3] + 4 + i * 8, f"file {i}", {0})
        target = _ref_target(item_ref, info_base, info_end, f"file {i}")
        _need(data, target, 0x1C, f"FileInfo {i}")
        file_size, wave_size, entry_num = _unpack(data, target, "IIi", f"FileInfo {i}")
        ext_ref = _data_ref(data, target + 0xC, f"file {i} ext path", allow_address=True)
        pos_table_ref = _data_ref(data, target + 0x14, f"file {i} position table", {0})
        pos_table = _ref_target(pos_table_ref, info_base, info_end, f"file {i} position table")
        pos_count = _table_count(data, pos_table, info_end, 8, f"file {i} position table")
        positions = []
        for position in range(pos_count):
            position_ref = _data_ref(
                data, pos_table + 4 + position * 8, f"file position {i}:{position}", {0},
                allow_address=True,
            )
            if position_ref["refType"] == 0:
                positions.append(None)
                continue
            position_target = _ref_target(position_ref, info_base, info_end, f"file position {i}:{position}")
            group_id, index = _unpack(data, position_target, "II", f"FilePos {i}:{position}")
            if group_id >= group_count:
                positions.append({"groupId": group_id, "index": index, "valid": False})
            else:
                positions.append({"groupId": group_id, "index": index, "valid": True})
        files.append(
            {
                "fileSize": file_size,
                "waveDataSize": wave_size,
                "entryNum": entry_num,
                "extFilePathRef": ext_ref,
                "filePosTableRef": pos_table_ref,
                "positions": positions,
            }
        )

    file_image_offset = 0
    groups = []
    for i in range(group_count):
        item_ref = _data_ref(data, table_targets[4] + 4 + i * 8, f"group {i}", {0})
        target = _ref_target(item_ref, info_base, info_end, f"group {i}")
        _need(data, target, 0x28, f"GroupInfo {i}")
        string_id, entry_num = _unpack(data, target, "Ii", f"GroupInfo {i}")
        ext_ref = _data_ref(data, target + 8, f"group {i} ext path", allow_address=True)
        offsets = _unpack(data, target + 0x10, "IIII", f"GroupInfo {i} offsets")
        item_table_ref = _data_ref(data, target + 0x20, f"group {i} item table", {0})
        item_table = _ref_target(item_table_ref, info_base, info_end, f"group {i} item table")
        item_count = _table_count(data, item_table, info_end, 8, f"group {i} item table")
        items = []
        for item in range(item_count):
            item_ref = _data_ref(data, item_table + 4 + item * 8, f"group item {i}:{item}", {0})
            item_target = _ref_target(item_ref, info_base, info_end, f"group item {i}:{item}")
            item_file_id, item_offset, item_size, wave_offset, wave_size = _unpack(
                data, item_target, "IIIII", f"GroupItemInfo {i}:{item}"
            )
            if item_file_id >= file_count:
                raise FormatError(f"GroupItemInfo {i}:{item} references invalid file")
            embedded_offset = file_image_offset + offsets[0] + item_offset
            _need(data, embedded_offset, item_size, f"group item {i}:{item} embedded file")
            if item_offset + item_size > offsets[1]:
                raise FormatError(f"group item {i}:{item} exceeds its group file region")
            if wave_offset + wave_size > offsets[3]:
                raise FormatError(f"group item {i}:{item} exceeds its group wave region")
            items.append(
                {
                    "fileId": item_file_id,
                    "offset": item_offset,
                    "size": item_size,
                    "waveDataOffset": wave_offset,
                    "waveDataSize": wave_size,
                }
            )
        if ext_ref["refType"] == 1:
            _need(
                data,
                file_image_offset + offsets[0],
                offsets[1],
                f"group {i} file region",
            )
            _need(
                data,
                file_image_offset + offsets[2],
                offsets[3],
                f"group {i} wave region",
            )
        groups.append(
            {
                "stringId": string_id,
                "entryNum": entry_num,
                "extFilePathRef": ext_ref,
                "offset": offsets[0],
                "size": offsets[1],
                "waveDataOffset": offsets[2],
                "waveDataSize": offsets[3],
                "itemTableRef": item_table_ref,
                "items": items,
            }
        )

    for file_info in files:
        for position in file_info["positions"]:
            if position is not None and position["groupId"] < group_count:
                position["valid"] = position["index"] < len(
                    groups[position["groupId"]]["items"]
                )

    player_info_target = _ref_target(info_refs[5], info_base, info_end, "INFO playerInfo")
    archive_player_info = dict(
        zip(
            (
                "seqSoundCount",
                "seqTrackCount",
                "strmSoundCount",
                "strmTrackCount",
                "strmChannelCount",
                "waveSoundCount",
                "waveTrackCount",
            ),
            _unpack(data, player_info_target, "7H", "SoundArchivePlayerInfo"),
        )
    )
    info_data = {
        "blockHeader": {"kind": info_kind, "size": info_block_size},
        "soundTableRef": info_refs[0],
        "bankTableRef": info_refs[1],
        "playerTableRef": info_refs[2],
        "fileTableRef": info_refs[3],
        "groupTableRef": info_refs[4],
        "soundArchivePlayerInfoRef": info_refs[5],
        "sounds": sounds,
        "banks": banks,
        "players": players,
        "files": files,
        "groups": groups,
        "soundArchivePlayerInfo": archive_player_info,
    }

    embedded = []
    seen_positions: set[tuple[int, int]] = set()
    for file_index, file_info in enumerate(files):
        for position_index, position in enumerate(file_info["positions"]):
            what = f"embedded file {file_index}:{position_index}"
            if position is None or not position["valid"]:
                raise FormatError(f"{what}: invalid FilePos")
            position_key = (position["groupId"], position["index"])
            if position_key in seen_positions:
                continue
            seen_positions.add(position_key)
            group = groups[position["groupId"]]
            if position["index"] >= len(group["items"]):
                raise FormatError(f"{what}: FilePos index exceeds group item table")
            if group["extFilePathRef"]["refType"] != 0:
                continue
            item = group["items"][position["index"]]
            if item["fileId"] != file_index:
                raise FormatError(f"{what}: group item fileId disagrees with file table")
            if item["size"] != file_info["fileSize"]:
                raise FormatError(f"{what}: group item size disagrees with FileInfo.fileSize")
            if item["waveDataSize"] != file_info["waveDataSize"]:
                raise FormatError(f"{what}: group item wave size disagrees with FileInfo.waveDataSize")
            embedded_offset = file_image_offset + group["offset"] + item["offset"]
            _need(data, embedded_offset, item["size"], what)
            blob = data[embedded_offset:embedded_offset + item["size"]]
            parsed = None
            if blob[:4] in (b"RSEQ", b"RBNK", b"RWSD", b"RWAR"):
                parsed = parse(blob)
            wave_data = None
            if item["waveDataSize"] and blob[:4] == b"RWAR":
                wave_offset = (
                    file_image_offset
                    + group["waveDataOffset"]
                    + item["waveDataOffset"]
                )
                _need(data, wave_offset, item["waveDataSize"], f"{what} wave data")
                wave_blob = data[wave_offset:wave_offset + item["waveDataSize"]]
                if wave_blob[:4] == b"RWAR":
                    wave_data = parse(wave_blob)
            embedded.append(
                {
                    "fileId": file_index,
                    "groupId": position["groupId"],
                    "index": position["index"],
                    "parsed": parsed,
                    "waveData": wave_data,
                }
            )

    return {
        "format": "RSAR",
        "fileHeader": header,
        "header": {
            "symbolDataOffset": symbol_offset,
            "symbolDataSize": symbol_size,
            "infoOffset": info_offset,
            "infoSize": info_size,
            "fileImageOffset": file_offset,
            "fileImageSize": file_size,
        },
        "symbolBlock": symbol_data,
        "infoBlock": info_data,
        "fileBlock": {"blockHeader": {"kind": file_kind, "size": file_block_size}},
        "embedded": embedded,
    }


def _parse_rstm(data: bytes) -> dict[str, Any]:
    header = _file_header(data)
    if header["signature"] != "RSTM" or header["dataBlocks"] != 2 or header["version"] > 0x0100:
        raise FormatError("not a supported RSTM v1.0 file")
    head_offset, head_size, adpc_offset, adpc_size, data_offset, data_size = _unpack(
        data, 0x10, "IIIIII", "StrmFile.Header"
    )
    _need(data, head_offset, head_size, "RSTM HEAD block")
    if adpc_size:
        _need(data, adpc_offset, adpc_size, "RSTM ADPC block")
    _need(data, data_offset, data_size, "RSTM DATA block")
    head_kind, head_block_size = _block_header(data, head_offset, "HEAD")
    head_end = head_offset + head_block_size
    ref_base = head_offset + 8
    data_header_ref = _data_ref(data, ref_base, "RSTM StrmDataInfo ref")
    track_table_ref = _data_ref(data, ref_base + 8, "RSTM track table ref")
    channel_table_ref = _data_ref(data, ref_base + 16, "RSTM channel table ref")
    data_header = _ref_target(data_header_ref, ref_base, head_end, "RSTM StrmDataInfo")
    track_table = _ref_target(track_table_ref, ref_base, head_end, "RSTM track table")
    channel_table = _ref_target(channel_table_ref, ref_base, head_end, "RSTM channel table")
    _need(data, data_header, 0x34, "StrmDataInfo")
    fields = _unpack(data, data_header, "BBBBHH" + "I" * 11, "StrmDataInfo")
    info = {
        "format": fields[0],
        "loopFlag": fields[1],
        "numChannels": fields[2],
        "sampleRate": (fields[3] << 16) | fields[4],
        "blockHeaderOffset": fields[5],
        "loopStart": fields[6],
        "loopEnd": fields[7],
        "dataOffset": fields[8],
        "numBlocks": fields[9],
        "blockSize": fields[10],
        "blockSamples": fields[11],
        "lastBlockSize": fields[12],
        "lastBlockSamples": fields[13],
        "lastBlockPaddedSize": fields[14],
        "adpcmDataInterval": fields[15],
        "adpcmDataSize": fields[16],
    }
    if info["format"] > 2:
        raise FormatError(f"unsupported RSTM codec {info['format']}")
    if info["numChannels"] not in (1, 2):
        raise FormatError(f"unsupported RSTM channel count {info['numChannels']}")
    if info["loopEnd"] < info["loopStart"] or info["numBlocks"] == 0:
        raise FormatError("invalid RSTM loop or block count")
    if adpc_size and adpc_size < 8 + info["numChannels"] * 4:
        raise FormatError("RSTM ADPC block is smaller than its per-channel state")

    track_count = _u8(data, track_table, "RSTM track count")
    track_type = _u8(data, track_table + 1, "RSTM track type")
    if track_count == 0 or track_type > 1:
        raise FormatError("invalid RSTM track table")
    track_capacity = (head_end - (track_table + 4)) // 8
    if track_count > track_capacity:
        raise FormatError("RSTM track table is truncated")
    channels = []
    channel_count = _u8(data, channel_table, "RSTM channel count")
    if channel_count != info["numChannels"]:
        raise FormatError("RSTM HEAD channel counts disagree")
    channel_capacity = (head_end - (channel_table + 4)) // 8
    if channel_count > channel_capacity:
        raise FormatError("RSTM channel table is truncated")
    for i in range(channel_count):
        channel_ref = _data_ref(data, channel_table + 4 + i * 8, f"RSTM channel {i} ref", {0})
        channel = _ref_target(channel_ref, ref_base, head_end, f"RSTM channel {i}")
        adpcm_ref = _data_ref(data, channel, f"RSTM channel {i} adpcm ref", {0})
        adpcm_target = _ref_target(adpcm_ref, ref_base, head_end, f"RSTM channel {i} adpcm")
        _need(data, adpcm_target, 0x30, f"RSTM channel {i} AdpcmInfo")
        coefficients = list(struct.unpack(">16h", data[adpcm_target:adpcm_target + 0x20]))
        gain, pred_scale, yn1, yn2, loop_pred, loop_yn1, loop_yn2, padding = struct.unpack(
            ">8H", data[adpcm_target + 0x20:adpcm_target + 0x30]
        )
        channels.append(
            {
                "refAdpcmInfo": adpcm_ref,
                "coefficients": coefficients,
                "gain": gain,
                "predScale": pred_scale,
                "yn1": yn1,
                "yn2": yn2,
                "loopPredScale": loop_pred,
                "loopYn1": loop_yn1,
                "loopYn2": loop_yn2,
                "PADDING_0x2E": padding,
            }
        )

    data_block_kind, data_block_size = _block_header(data, data_offset, "DATA")
    stream_data_offset = info["dataOffset"]
    _need(data, stream_data_offset, 0, "RSTM stream data")
    if stream_data_offset < data_offset + 8 or stream_data_offset > data_offset + data_size:
        raise FormatError("RSTM StrmDataInfo.dataOffset is outside the DATA block")
    stream_stride = info["blockHeaderOffset"] + info["blockSize"] * info["numChannels"]
    if stream_stride == 0:
        raise FormatError("RSTM stream stride is zero")
    final_block_size = info["blockHeaderOffset"] + info["lastBlockPaddedSize"] * info["numChannels"]
    if final_block_size == 0:
        raise FormatError("RSTM final block size is zero")
    stream_end = stream_data_offset + stream_stride * (info["numBlocks"] - 1) + final_block_size
    _need(data, stream_data_offset, stream_end - stream_data_offset, "RSTM stream blocks")
    adpc_block = None
    if adpc_size:
        adpc_kind, adpc_block_size = _block_header(data, adpc_offset, "ADPC")
        if adpc_block_size < 8 + info["numChannels"] * 4:
            raise FormatError("RSTM ADPC block is too small")
        adpc_block = {"blockHeader": {"kind": adpc_kind, "size": adpc_block_size}}
    return {
        "format": "RSTM",
        "fileHeader": header,
        "header": {
            "headBlockOffset": head_offset,
            "headBlockSize": head_size,
            "adpcBlockOffset": adpc_offset,
            "adpcBlockSize": adpc_size,
            "dataBlockOffset": data_offset,
            "dataBlockSize": data_size,
        },
        "headBlock": {
            "blockHeader": {"kind": head_kind, "size": head_block_size},
            "refDataHeader": data_header_ref,
            "refTrackTable": track_table_ref,
            "refChannelTable": channel_table_ref,
            "strmDataInfo": info,
            "trackCount": track_count,
            "trackDataType": track_type,
            "channels": channels,
        },
        "adpcBlock": adpc_block,
        "dataBlock": {"blockHeader": {"kind": data_block_kind, "size": data_block_size}},
    }


def parse(data: bytes) -> dict[str, Any]:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("parse expects bytes")
    data = bytes(data)
    if len(data) < 4:
        raise FormatError("file is too short for a signature")
    signature = data[:4]
    if signature == b"RSAR":
        return _parse_rsar(data)
    if signature == b"RSTM":
        return _parse_rstm(data)
    if signature == b"RSEQ":
        return _parse_rseq(data)
    if signature == b"RBNK":
        return _parse_rbnk(data)
    if signature == b"RWSD":
        return _parse_rwsd(data)
    if signature == b"RWAR":
        return _parse_rwar(data)
    raise FormatError(f"unsupported signature {signature!r}")


def _counter_text(counter: dict[int, int]) -> str:
    return ", ".join(f"{key}x{value}" for key, value in sorted(counter.items()))


def check(root: str) -> tuple[int, int, int, dict[str, dict[str, Any]]]:
    counts = {"RSAR": 0, "RSTM": 0}
    errors = {"RSAR": 0, "RSTM": 0}
    facts: dict[str, dict[str, Any]] = {
        "RSAR": {
            "versions": collections.Counter(),
            "sounds": 0,
            "banks": 0,
            "groups": 0,
            "players": 0,
            "files": 0,
            "embedded": collections.Counter(),
            "embeddedVersions": collections.Counter(),
            "instruments": 0,
            "rseqBlocks": collections.Counter(),
            "rwsdWaveSounds": 0,
            "rwsdWaves": 0,
            "rbnkWaves": 0,
            "rwarWaves": 0,
            "rseqLabels": 0,
        },
        "RSTM": {
            "versions": collections.Counter(),
            "channels": collections.Counter(),
            "codecs": collections.Counter(),
            "sampleRates": collections.Counter(),
        },
    }
    for path, blob in walk(root):
        if blob[:4] not in (b"RSAR", b"RSTM"):
            continue
        format_name = blob[:4].decode("ascii")
        try:
            parsed = parse(blob)
            counts[format_name] += 1
            facts[format_name]["versions"][parsed["fileHeader"]["version"]] += 1
            if format_name == "RSAR":
                info = parsed["infoBlock"]
                facts["RSAR"]["sounds"] += len(info["sounds"])
                facts["RSAR"]["banks"] += len(info["banks"])
                facts["RSAR"]["groups"] += len(info["groups"]) - 1
                facts["RSAR"]["players"] += len(info["players"])
                facts["RSAR"]["files"] += len(info["files"])
                for embedded in parsed["embedded"]:
                    embedded_parsed = embedded["parsed"]
                    if embedded_parsed is not None:
                        facts["RSAR"]["embedded"][embedded_parsed["format"]] += 1
                        facts["RSAR"]["embeddedVersions"][
                            (embedded_parsed["format"], embedded_parsed["fileHeader"]["version"])
                        ] += 1
                        if embedded_parsed["format"] == "RSEQ":
                            label_block = embedded_parsed["labelBlock"]
                            facts["RSAR"]["rseqBlocks"][label_block["blockHeader"]["kind"]] += 1
                            facts["RSAR"]["rseqLabels"] += label_block["count"]
                        elif embedded_parsed["format"] == "RBNK":
                            facts["RSAR"]["instruments"] += embedded_parsed["dataBlock"]["count"]
                            facts["RSAR"]["rbnkWaves"] += embedded_parsed["waveBlock"]["count"]
                        elif embedded_parsed["format"] == "RWSD":
                            facts["RSAR"]["rwsdWaveSounds"] += embedded_parsed["dataBlock"]["wsdCount"]
                            facts["RSAR"]["rwsdWaves"] += embedded_parsed["waveBlock"]["waveCount"]
                        else:
                            facts["RSAR"]["rwarWaves"] += embedded_parsed["tablBlock"]["waveCount"]
                    if embedded["waveData"] is not None:
                        facts["RSAR"]["embedded"][embedded["waveData"]["format"]] += 1
                        facts["RSAR"]["embeddedVersions"][
                            (embedded["waveData"]["format"], embedded["waveData"]["fileHeader"]["version"])
                        ] += 1
                        facts["RSAR"]["rwarWaves"] += embedded["waveData"]["tablBlock"]["waveCount"]
            else:
                info = parsed["headBlock"]["strmDataInfo"]
                facts["RSTM"]["channels"][info["numChannels"]] += 1
                facts["RSTM"]["codecs"][info["format"]] += 1
                facts["RSTM"]["sampleRates"][info["sampleRate"]] += 1
        except Exception as error:
            errors[format_name] += 1
            print(f"ERROR {format_name} {path}: {error}", file=sys.stderr)
    return counts["RSAR"], errors["RSAR"], errors["RSTM"], facts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", metavar="ROOT", help="parse every RSAR/RSTM file under ROOT")
    args = parser.parse_args(argv)
    if args.check is None:
        parser.print_usage()
        return 2
    if not os.path.isdir(args.check):
        print(f"not a directory: {args.check}", file=sys.stderr)
        return 2
    rsar_count, rsar_errors, rstm_errors, facts = check(args.check)
    rstm_count = facts["RSTM"]["versions"].total()
    rsar = facts["RSAR"]
    rstm = facts["RSTM"]
    print(
        f"RSAR: files parsed={rsar_count} errors={rsar_errors} "
        f"versions={{{_counter_text(rsar['versions'])}}} "
        f"sounds={rsar['sounds']} banks={rsar['banks']} groups={rsar['groups']} "
        f"players={rsar['players']} files={rsar['files']} "
        f"embedded={{{_counter_text(rsar['embedded'])}}} "
        f"embeddedVersions={{{_counter_text(rsar['embeddedVersions'])}}} "
        f"rseqLabels={rsar['rseqLabels']} instruments={rsar['instruments']} "
        f"rbnkWaves={rsar['rbnkWaves']} rwsdWaveSounds={rsar['rwsdWaveSounds']} "
        f"rwsdWaves={rsar['rwsdWaves']} rwarWaves={rsar['rwarWaves']}"
    )
    print(
        f"RSTM: files parsed={rstm_count} errors={rstm_errors} "
        f"versions={{{_counter_text(rstm['versions'])}}} "
        f"codecs={{{_counter_text(rstm['codecs'])}}} "
        f"channels={{{_counter_text(rstm['channels'])}}} "
        f"sampleRates={{{_counter_text(rstm['sampleRates'])}}}"
    )
    return 1 if rsar_errors or rstm_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
