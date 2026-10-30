#!/usr/bin/env python3
"""Strict parsers for the Wii Play text, font, and standalone texture assets.

These formats are big-endian on disc. BTI has no magic; the checker selects it
by its `.bti` extension. All other formats are selected by magic.
"""

from __future__ import annotations

import argparse
import collections
import os
import struct
from typing import Any

try:
    from . import wiiarc
except ImportError:
    import wiiarc


class FormatError(ValueError):
    """A malformed file or a file that does not match the game's expectations."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise FormatError(message)


def _unpack(data: bytes, offset: int, fmt: str) -> tuple[Any, ...]:
    size = struct.calcsize(fmt)
    end = offset + size
    _require(offset >= 0 and end <= len(data), f"out-of-bounds read at 0x{offset:X}")
    return struct.unpack_from(fmt, data, offset)


def _u8(data: bytes, offset: int) -> int:
    return _unpack(data, offset, ">B")[0]


def _u16(data: bytes, offset: int) -> int:
    return _unpack(data, offset, ">H")[0]


def _u32(data: bytes, offset: int) -> int:
    return _unpack(data, offset, ">I")[0]


def _sig(data: bytes, offset: int) -> bytes:
    return _unpack(data, offset, ">4s")[0]


def parse_bmg(data: bytes) -> dict[str, Any]:
    """Decode a MESGbmg1 file.

    Struct sources:
    - EGG::MsgRes::MessageHeader: include/egg/util/eggMsgRes.h:29-47
    - EGG::MsgRes::DataBlock: include/egg/util/eggMsgRes.h:24-27
    - EGG::MsgRes::MessageInfoDataBlock: include/egg/util/eggMsgRes.h:49-62
    - EGG::MsgRes::MessageIDDataBlock: include/egg/util/eggMsgRes.h:74-81
    - RPSysMessage::Entry: include/Pack/RPKernel/RPSysMessage.h:25-35
    """
    _require(data[:4] == b"MESG", "BMG signature must be MESG")
    _require(len(data) >= 32, "BMG file header is truncated")
    _require(_u32(data, 4) == 0x626D6731, "BMG type must be bmg1")
    data_size, num_blocks = _unpack(data, 8, ">II")
    encoding = _u8(data, 16)
    header_size = 32
    _require(encoding == 2, "BMG encoding must be UTF-16 (encoding 2)")
    _require(data_size == len(data), "BMG data size does not match file size")

    sections: dict[str, dict[str, Any]] = {}
    counts = collections.Counter()
    offset = header_size
    for index in range(num_blocks):
        kind = _sig(data, offset)
        size = _u32(data, offset + 4)
        _require(size >= 8, f"BMG section 0x{index:X} is smaller than its header")
        end = offset + size
        _require(end <= data_size and end <= len(data), f"BMG section 0x{index:X} is out of bounds")
        _require(offset + 8 <= len(data), "BMG section header is truncated")
        if kind == b"INF1":
            _require(kind not in sections, "duplicate BMG INF1 section")
            num_entries, entry_size, group_id = _unpack(data, offset + 8, ">HHH")
            _require(entry_size == 12, "BMG INF1 entries must be 12 bytes")
            expected_size = 16 + num_entries * entry_size
            expected_size += -expected_size % 32
            _require(size == expected_size, "BMG INF1 size does not match its entries")
            entries: list[dict[str, Any]] = []
            for entry_index in range(num_entries):
                entry_offset = offset + 16 + entry_index * entry_size
                data_offset, attribute, scale, char_space = _unpack(
                    data, entry_offset, ">IIHb"
                )
                entries.append(
                    {
                        "dataOffset": data_offset,
                        "attribute": attribute,
                        "scale": scale,
                        "charSpace": char_space,
                    }
                )
            sections["INF1"] = {
                "offset": offset,
                "size": size,
                "kind": "INF1",
                "numEntries": num_entries,
                "entrySize": entry_size,
                "groupID": group_id,
                "entries": entries,
            }
            counts["INF1"] += 1
        elif kind == b"DAT1":
            _require(kind not in sections, "duplicate BMG DAT1 section")
            sections["DAT1"] = {
                "offset": offset,
                "size": size,
                "kind": "DAT1",
                "dataOffset": offset + 8,
            }
            counts["DAT1"] += 1
        elif kind == b"MID1":
            _require(kind not in sections, "duplicate BMG MID1 section")
            num_entries, form, form_supplement = _unpack(data, offset + 8, ">HBB")
            expected_size = 16 + num_entries * 4
            expected_size += -expected_size % 32
            _require(size == expected_size, "BMG MID1 size does not match its entries")
            message_ids = [
                _u32(data, offset + 16 + message_index * 4)
                for message_index in range(num_entries)
            ]
            sections["MID1"] = {
                "offset": offset,
                "size": size,
                "kind": "MID1",
                "numEntries": num_entries,
                "form": form,
                "formSupplement": form_supplement,
                "messageIDs": message_ids,
            }
            counts["MID1"] += 1
        else:
            _require(False, f"unsupported BMG section {kind!r}")
        offset = end

    _require(offset == data_size, "BMG sections do not exactly cover the file")
    _require(counts["INF1"] == 1, "BMG requires exactly one INF1 section")
    _require(counts["DAT1"] == 1, "BMG requires exactly one DAT1 section")
    _require(counts["MID1"] == 1, "BMG requires exactly one MID1 section")
    inf1 = sections["INF1"]
    mid1 = sections["MID1"]
    dat1 = sections["DAT1"]
    _require(inf1["numEntries"] == mid1["numEntries"], "BMG INF1 and MID1 counts differ")

    text_begin = dat1["dataOffset"]
    text_end = dat1["offset"] + dat1["size"]
    for entry in inf1["entries"]:
        text_offset = text_begin + entry["dataOffset"]
        _require(
            text_begin <= text_offset <= text_end,
            "BMG DAT1 text offset is out of bounds",
        )
        _validate_bmg_text(data, text_offset, text_end)

    return {
        "format": "BMG",
        "header": {
            "signature": "MESGbmg1",
            "type": 0x626D6731,
            "dataSize": data_size,
            "numBlocks": num_blocks,
            "headerSize": header_size,
            "unknown20": 0,
            "encoding": encoding,
        },
        "sections": sections,
    }


def _validate_bmg_text(data: bytes, offset: int, end: int) -> None:
    """Validate UTF-16BE code units and in-place 0x1A tags without storing text."""
    unit_offset = offset
    while unit_offset + 2 <= end:
        code = _u16(data, unit_offset)
        if code == 0:
            return
        if code == 0x1A:
            _require(unit_offset + 4 <= end, "truncated BMG tag header")
            length = _u8(data, unit_offset + 2)
            _require(length >= 2, "BMG tag length is smaller than its identifier")
            tag_id = (_u8(data, unit_offset + 3) << 16) | _u16(data, unit_offset + 4)
            tag_end = unit_offset + length
            _require(length % 2 == 0, "BMG tag byte length is not UTF-16 aligned")
            _require(tag_end <= end, "BMG tag extends past DAT1")
            if tag_id == 0xFF0007:
                _require(length >= 8, "BMG ruby tag is truncated")
                _require((length - 8) % 2 == 0, "BMG ruby text is not UTF-16 aligned")
                base_length = _u8(data, unit_offset + 5)
                _require(
                    tag_end + base_length * 2 <= end,
                    "BMG ruby base text extends past DAT1",
                )
            unit_offset = tag_end
            continue
        unit_offset += 2
    _require(False, "BMG text is not NUL-terminated")


def parse_brfnt(data: bytes) -> dict[str, Any]:
    """Decode an RFNT resource font.

    Struct sources:
    - nw4r::ut::BinaryFileHeader: include/nw4r/ut/ut_binaryFileFormat.h:13-19
    - nw4r::ut::BinaryBlockHeader: include/nw4r/ut/ut_binaryFileFormat.h:8-11
    - nw4r::ut::CharWidths: include/nw4r/ut/ut_Font.h:21-25
    - nw4r::ut::FontTextureGlyph: include/nw4r/ut/ut_ResFontBase.h:18-29
    - nw4r::ut::FontWidth: include/nw4r/ut/ut_ResFontBase.h:31-36
    - nw4r::ut::FontCodeMap: include/nw4r/ut/ut_ResFontBase.h:38-45
    - nw4r::ut::FontInformation: include/nw4r/ut/ut_ResFontBase.h:47-57
    """
    _require(_sig(data, 0) == b"RFNT", "BRFNT signature must be RFNT")
    _require(len(data) >= 16, "BRFNT file header is truncated")
    byte_order, version, file_size, header_size, data_blocks = _unpack(
        data, 4, ">HHIHH"
    )
    _require(len(data) >= 16, "BRFNT file header is truncated")
    _require(byte_order == 0xFEFF, "BRFNT byte order must be big-endian (0xFEFF)")
    _require(version == 0x0104, "BRFNT version must be 1.4")
    _require(header_size == 16, "BRFNT header size must be 16")
    _require(file_size == len(data), "BRFNT file size does not match actual size")
    _require(data_blocks >= 2, "BRFNT requires at least FINF and TGLP")

    sections: list[dict[str, Any]] = []
    section_counts = collections.Counter()
    offset = header_size
    for index in range(data_blocks):
        kind = _sig(data, offset)
        size = _u32(data, offset + 4)
        _require(size >= 8, f"BRFNT section 0x{index:X} is smaller than its header")
        end = offset + size
        _require(end <= file_size, f"BRFNT section 0x{index:X} is out of bounds")
        if kind == b"FINF":
            _require(kind not in section_counts, "duplicate BRFNT FINF section")
            _require(size >= 32, "BRFNT FINF section is truncated")
            section = _parse_finf(data, offset, size)
        elif kind == b"TGLP":
            _require(kind not in section_counts, "duplicate BRFNT TGLP section")
            section = _parse_tglp(data, offset, size)
        elif kind == b"CWDH":
            section = _parse_cwdh(data, offset, size)
        elif kind == b"CMAP":
            section = _parse_cmap(data, offset, size)
        else:
            _require(False, f"unsupported BRFNT section {kind!r}")
        section["offset"] = offset
        section["size"] = size
        sections.append(section)
        section_counts[kind.decode("ascii")] += 1
        offset = end

    _require(offset == file_size, "BRFNT sections do not exactly cover the file")
    _require(section_counts["FINF"] == 1, "BRFNT requires exactly one FINF section")
    _require(section_counts["TGLP"] == 1, "BRFNT requires exactly one TGLP section")
    finf = sections[0]
    _require(finf["kind"] == "FINF", "BRFNT first section must be FINF")

    by_kind = {section["kind"]: [] for section in sections}
    for section in sections:
        by_kind[section["kind"]].append(section)
    tglp = by_kind["TGLP"][0]
    _require(
        finf["pGlyph"] == tglp["offset"] + 8,
        "FINF pGlyph does not point to the TGLP payload",
    )
    if finf["pWidth"] != 0:
        width_sections = by_kind.get("CWDH", [])
        _require(
            any(section["offset"] + 8 == finf["pWidth"] for section in width_sections),
            "FINF pWidth does not point to a CWDH payload",
        )
    if finf["pMap"] != 0:
        map_sections = by_kind.get("CMAP", [])
        _require(
            any(section["offset"] + 8 == finf["pMap"] for section in map_sections),
            "FINF pMap does not point to a CMAP payload",
        )

    # ResFont::Rebuild resolves every nonzero pNext as an offset from the
    # start of the file (src/nw4r/ut/ut_ResFont.cpp ResolveOffset), so it must
    # name the payload (block + 8) of a section of the same kind.
    for section in by_kind.get("CWDH", []):
        p_next = section["pNext"]
        if p_next != 0:
            _require(
                any(other["offset"] + 8 == p_next for other in by_kind["CWDH"]),
                "CWDH pNext does not point to a CWDH payload",
            )
    for section in by_kind.get("CMAP", []):
        p_next = section["pNext"]
        if p_next != 0:
            _require(
                any(other["offset"] + 8 == p_next for other in by_kind["CMAP"]),
                "CMAP pNext does not point to a CMAP payload",
            )
    _chain(sections, finf["pWidth"], "CWDH")
    _chain(sections, finf["pMap"], "CMAP")

    return {
        "format": "BRFNT",
        "header": {
            "signature": "RFNT",
            "byteOrder": byte_order,
            "version": version,
            "fileSize": file_size,
            "headerSize": header_size,
            "dataBlocks": data_blocks,
        },
        "sections": sections,
    }


def _parse_finf(data: bytes, offset: int, size: int) -> dict[str, Any]:
    values = _unpack(data, offset + 8, ">BbH3sBIIIBBB")
    left, glyph_width, char_width = struct.unpack(">bBb", values[3])
    default_width = {
        "left": left,
        "glyphWidth": glyph_width,
        "charWidth": char_width,
    }
    return {
        "kind": "FINF",
        "fontType": values[0],
        "linefeed": values[1],
        "alterCharIndex": values[2],
        "defaultWidth": default_width,
        "encoding": values[4],
        "pGlyph": values[5],
        "pWidth": values[6],
        "pMap": values[7],
        "height": values[8],
        "width": values[9],
        "ascent": values[10],
    }


def _parse_tglp(data: bytes, offset: int, size: int) -> dict[str, Any]:
    values = _unpack(data, offset + 8, ">BBbBIHHHHHHI")
    image_end = values[11] + values[5] * values[4]
    expected_size = image_end - offset
    _require(
        offset + 32 <= values[11] <= image_end,
        "BRFNT TGLP sheetImage offset is invalid",
    )
    _require(size == expected_size, "BRFNT TGLP size does not match sheet storage")
    return {
        "kind": "TGLP",
        "cellWidth": values[0],
        "cellHeight": values[1],
        "baselinePos": values[2],
        "maxCharWidth": values[3],
        "sheetSize": values[4],
        "sheetNum": values[5],
        "sheetFormat": values[6],
        "sheetRow": values[7],
        "sheetLine": values[8],
        "sheetWidth": values[9],
        "sheetHeight": values[10],
        "sheetImage": values[11],
    }


def _parse_cwdh(data: bytes, offset: int, size: int) -> dict[str, Any]:
    index_begin, index_end, p_next = _unpack(data, offset + 8, ">HHI")
    _require(index_begin <= index_end, "BRFNT CWDH index range is reversed")
    count = index_end - index_begin + 1
    expected_size = 8 + 8 + count * 3
    expected_size += -expected_size % 4
    _require(size == expected_size, "BRFNT CWDH size does not match its width table")
    width_table = []
    for index in range(count):
        left, glyph_width, char_width = _unpack(
            data, offset + 16 + index * 3, ">bBb"
        )
        width_table.append(
            {"left": left, "glyphWidth": glyph_width, "charWidth": char_width}
        )
    return {
        "kind": "CWDH",
        "indexBegin": index_begin,
        "indexEnd": index_end,
        "pNext": p_next,
        "widthTable": width_table,
    }


def _parse_cmap(data: bytes, offset: int, size: int) -> dict[str, Any]:
    # FontCodeMap (payload at offset + 8): ccodeBegin, ccodeEnd,
    # mappingMethod, reserved (u16 each), pNext (+0x8), mapInfo (+0xC).
    ccode_begin, ccode_end, mapping_method, _reserved, p_next = _unpack(
        data, offset + 8, ">HHHHI"
    )
    _require(ccode_begin <= ccode_end, "BRFNT CMAP code range is reversed")
    span = ccode_end - ccode_begin + 1
    if mapping_method == 0:
        map_offset = offset + 20
        expected_size = 24
        _require(size == expected_size, "BRFNT CMAP direct size is invalid")
        map_info = [_u16(data, map_offset)]
    elif mapping_method == 1:
        expected_size = 20 + span * 2
        expected_size += -expected_size % 4
        _require(size == expected_size, "BRFNT CMAP table size is invalid")
        map_info = [
            _u16(data, offset + 20 + index * 2) for index in range(span)
        ]
    elif mapping_method == 2:
        entry_count = _u16(data, offset + 20)
        _require(entry_count > 0, "BRFNT CMAP scan table is empty")
        expected_size = 22 + entry_count * 4
        expected_size += -expected_size % 4
        _require(size == expected_size, "BRFNT CMAP scan size is invalid")
        map_info = []
        for index in range(entry_count):
            code, glyph_index = _unpack(data, offset + 22 + index * 4, ">HH")
            map_info.append({"ccode": code, "index": glyph_index})
    else:
        _require(False, f"unsupported BRFNT CMAP mapping method {mapping_method}")
    return {
        "kind": "CMAP",
        "ccodeBegin": ccode_begin,
        "ccodeEnd": ccode_end,
        "mappingMethod": mapping_method,
        "pNext": p_next,
        "mapInfo": map_info,
    }


def _chain(sections: list[dict[str, Any]], first: int, kind: str) -> list[dict[str, Any]]:
    """Follow a FINF pWidth/pMap chain (file offsets of payloads) and return
    the sections in chain order. Rejects dangling links and cycles."""
    by_payload = {
        section["offset"] + 8: section for section in sections if section["kind"] == kind
    }
    chain: list[dict[str, Any]] = []
    seen: set[int] = set()
    link = first
    while link != 0:
        _require(link in by_payload, f"{kind} chain link 0x{link:X} is not a {kind} payload")
        _require(link not in seen, f"{kind} chain has a cycle")
        seen.add(link)
        section = by_payload[link]
        chain.append(section)
        link = section["pNext"]
    return chain


GLYPH_INDEX_NOT_FOUND = 0xFFFF


class BrfntFont:
    """Glyph lookups on a parsed BRFNT, written from the file format (not by
    translating ResFontBase), for cross-checking the host build."""

    def __init__(self, decoded: dict[str, Any]):
        sections = decoded["sections"]
        self.finf = sections[0]
        self.tglp = next(s for s in sections if s["kind"] == "TGLP")
        self.maps = _chain(sections, self.finf["pMap"], "CMAP")
        self.widths = _chain(sections, self.finf["pWidth"], "CWDH")
        self._scan = [
            {entry["ccode"]: entry["index"] for entry in m["mapInfo"]}
            if m["mappingMethod"] == 2 else None
            for m in self.maps
        ]

    def find_glyph_index(self, ch: int) -> int:
        """The first code map whose range holds ch decides (0xFFFF: none)."""
        for code_map, scan in zip(self.maps, self._scan):
            if code_map["ccodeBegin"] <= ch <= code_map["ccodeEnd"]:
                method = code_map["mappingMethod"]
                if method == 0:
                    return (code_map["mapInfo"][0] + ch - code_map["ccodeBegin"]) & 0xFFFF
                if method == 1:
                    return code_map["mapInfo"][ch - code_map["ccodeBegin"]]
                return scan.get(ch, GLYPH_INDEX_NOT_FOUND)
        return GLYPH_INDEX_NOT_FOUND

    def glyph_index(self, ch: int) -> int:
        index = self.find_glyph_index(ch)
        return self.finf["alterCharIndex"] if index == GLYPH_INDEX_NOT_FOUND else index

    def char_widths(self, index: int) -> tuple[int, int, int]:
        for width in self.widths:
            if width["indexBegin"] <= index <= width["indexEnd"]:
                w = width["widthTable"][index - width["indexBegin"]]
                return (w["left"], w["glyphWidth"], w["charWidth"])
        w = self.finf["defaultWidth"]
        return (w["left"], w["glyphWidth"], w["charWidth"])

    def glyph(self, index: int) -> dict[str, int]:
        """Texture sheet offset (from the file start) and cell of a glyph."""
        t = self.tglp
        per_sheet = t["sheetRow"] * t["sheetLine"]
        sheet, cell = divmod(index, per_sheet)
        line, row = divmod(cell, t["sheetRow"])
        return {
            "texOffset": t["sheetImage"] + sheet * t["sheetSize"],
            "cellX": row * (t["cellWidth"] + 1) + 1,
            "cellY": line * (t["cellHeight"] + 1) + 1,
            "height": t["cellHeight"],
            "texFormat": t["sheetFormat"],
            "texWidth": t["sheetWidth"],
            "texHeight": t["sheetHeight"],
        }


BMG_FORM_SUPPLEMENT_ID_BITS = (32, 24, 16, 8, 0)


def bmg_messages(data: bytes) -> list[dict[str, Any]]:
    """One record per MID1 entry: the (group, index) split of its message id,
    the INF1 entry a lookup by that id finds (the first with the same id),
    and the text: code units outside tags, tag count, tag-aware length and
    the offset of the first 0x0000 unit."""
    decoded = parse_bmg(data)
    inf1 = decoded["sections"]["INF1"]
    mid1 = decoded["sections"]["MID1"]
    dat1 = decoded["sections"]["DAT1"]
    supplement = mid1["formSupplement"]
    _require(supplement < len(BMG_FORM_SUPPLEMENT_ID_BITS), "BMG MID1 form supplement")
    index_bits = 32 - BMG_FORM_SUPPLEMENT_ID_BITS[supplement]
    first_by_id: dict[int, int] = {}
    for i, message_id in enumerate(mid1["messageIDs"]):
        first_by_id.setdefault(message_id, i)
    records = []
    for i, message_id in enumerate(mid1["messageIDs"]):
        group = message_id >> index_bits if index_bits < 32 else 0
        index = message_id & ((1 << index_bits) - 1)
        entry_index = first_by_id[message_id]
        entry = inf1["entries"][entry_index]
        start = dat1["dataOffset"] + entry["dataOffset"]
        pos = start
        units: list[int] = []
        tags: list[tuple[int, int]] = []
        while True:
            code = _u16(data, pos)
            if code == 0:
                break
            if code == 0x1A:
                length = _u8(data, pos + 2)
                tag_id = (_u8(data, pos + 3) << 16) | _u16(data, pos + 4)
                tags.append((length - 2, tag_id))
                units.append(-1 - len(tags) + 1)
                pos += length
                continue
            units.append(code)
            pos += 2
        first_zero = start
        while _u16(data, first_zero) != 0:
            first_zero += 2
        records.append(
            {
                "messageID": message_id,
                "group": group,
                "index": index,
                "entryIndex": entry_index,
                "attribute": entry["attribute"],
                "scale": entry["scale"],
                "charSpace": entry["charSpace"],
                "textOffset": entry["dataOffset"],
                "codeUnits": [u for u in units if u >= 0],
                "tags": tags,
                "sequence": units,
                "wcslen": (first_zero - start) // 2,
            }
        )
    return records


def parse_bti(data: bytes) -> dict[str, Any]:
    """Decode an EGG standalone GX texture.

    Struct source: EGG::ResTIMG, include/egg/gfx/eggResTIMG.h:12-37.
    """
    _require(len(data) >= 32, "BTI file header is truncated")
    values = _unpack(
        data,
        0,
        ">BBHHBBBBHiBBBBBBbbBBbh",
    )
    format_name = {
        0: "I4",
        1: "I8",
        2: "IA4",
        3: "IA8",
        4: "RGB565",
        5: "RGB5A3",
        6: "RGBA8",
        14: "CMPR",
    }.get(values[0])
    _require(format_name is not None, f"unsupported BTI texture format {values[0]}")
    assert format_name is not None
    image_offset = values[21] if values[21] != 0 else 32
    _require(image_offset >= 32, "BTI image offset is inside its header")
    _require(image_offset <= len(data), "BTI image offset is out of bounds")
    _require(values[2] > 0 and values[3] > 0, "BTI dimensions must be nonzero")
    _require(values[8] in (0, 16, 256, 1024), "unsupported BTI palette entry count")
    _require(values[9] == 0, "BTI palette offset must be zero for this disc")
    _require(values[10] == 0, "BTI mip-map flag must be zero for this disc")

    image_size = _bti_image_size(values[0], values[2], values[3], values[10], values[18])
    _require(
        image_offset + image_size <= len(data),
        "BTI image data extends past the file",
    )

    return {
        "format": "BTI",
        "header": {
            "format": values[0],
            "formatName": format_name,
            "transparency": values[1],
            "width": values[2],
            "height": values[3],
            "wrapS": values[4],
            "wrapT": values[5],
            "tlutName": values[6],
            "tlutFormat": values[7],
            "paletteEntryNum": values[8],
            "paletteOffset": values[9],
            "mipMap": values[10],
            "edgeLODEnable": values[11],
            "biasClampEnable": values[12],
            "anisotropy": values[13],
            "minFilter": values[14],
            "magFilter": values[15],
            "minLOD": values[16],
            "maxLOD": values[17],
            "mipMapLevel": values[18],
            "byte19": values[19],
            "lodbias": values[20],
            "imageOffset": values[21],
        },
        "imageOffsetResolved": image_offset,
        "imageSize": image_size,
    }


def _bti_image_size(format_id: int, width: int, height: int, mipmap: int, max_lod: int) -> int:
    block_bytes = {0: 8, 1: 32, 2: 16, 3: 32, 4: 32, 5: 32, 6: 32, 14: 32}[format_id]
    block_width = {0: 8, 14: 8}.get(format_id, 4)
    block_height = {0: 8, 14: 8}.get(format_id, 4)
    width_blocks = (width + block_width - 1) // block_width
    height_blocks = (height + block_height - 1) // block_height
    size = width_blocks * height_blocks * block_bytes
    if mipmap:
        for _ in range(max_lod):
            width = max(1, width // 2)
            height = max(1, height // 2)
            width_blocks = (width + block_width - 1) // block_width
            height_blocks = (height + block_height - 1) // block_height
            size += width_blocks * height_blocks * block_bytes
    return size


def parse(data: bytes) -> dict[str, Any]:
    """Parse a file by format magic, or as BTI when there is no magic."""
    if data[:4] == b"MESG":
        return parse_bmg(data)
    if data[:4] == b"RFNT":
        return parse_brfnt(data)
    if data[:4] == b"RFNU":
        raise FormatError("RFNU is an already-relocated BRFNT, not a disc file")
    return parse_bti(data)


def _parse_path(root: str, path: str, data: bytes) -> tuple[str, dict[str, Any]]:
    if data[:4] in (b"MESG", b"RFNT"):
        name = "BMG" if data[:4] == b"MESG" else "BRFNT"
    elif os.path.splitext(path)[1].lower() == ".bti":
        name = "BTI"
    else:
        name = ""
    if not name:
        raise FormatError(f"no supported text/font magic and not a .bti file: {path}")
    return name, parse(data)


def _check(root: str) -> int:
    stats: dict[str, dict[str, Any]] = {
        "BMG": {
            "parsed": 0,
            "errors": 0,
            "error_examples": [],
            "versions": collections.Counter(),
            "entries": 0,
            "encodings": collections.Counter(),
        },
        "BRFNT": {
            "parsed": 0,
            "errors": 0,
            "error_examples": [],
            "versions": collections.Counter(),
            "sections": collections.Counter(),
            "formats": collections.Counter(),
        },
        "BTI": {
            "parsed": 0,
            "errors": 0,
            "error_examples": [],
            "formats": collections.Counter(),
            "pixels": 0,
        },
    }
    for path, data in wiiarc.walk(root):
        try:
            name, decoded = _parse_path(root, path, data)
        except (FormatError, struct.error) as error:
            likely = (
                "BMG" if data[:4] == b"MESG" else
                "BRFNT" if data[:4] == b"RFNT" else
                "BTI" if os.path.splitext(path)[1].lower() == ".bti" else ""
            )
            if likely:
                item = stats[likely]
                item["errors"] += 1
                if len(item["error_examples"]) < 3:
                    item["error_examples"].append(f"{path}: {error}")
            continue
        item = stats[name]
        item["parsed"] += 1
        if name == "BMG":
            item["versions"][decoded["header"]["type"]] += 1
            item["encodings"][decoded["header"]["encoding"]] += 1
            item["entries"] += decoded["sections"]["INF1"]["numEntries"]
        elif name == "BRFNT":
            item["versions"][decoded["header"]["version"]] += 1
            for section in decoded["sections"]:
                item["sections"][section["kind"]] += 1
            tglp = next(section for section in decoded["sections"] if section["kind"] == "TGLP")
            item["formats"][tglp["sheetFormat"]] += 1
        else:
            item["formats"][decoded["header"]["formatName"]] += 1
            item["pixels"] += decoded["header"]["width"] * decoded["header"]["height"]

    def counter_text(counter: collections.Counter[Any]) -> str:
        return ", ".join(f"{key}={value}" for key, value in sorted(counter.items()))

    print(
        f"BMG: files parsed={stats['BMG']['parsed']}, errors={stats['BMG']['errors']}, "
        f"versions={counter_text(stats['BMG']['versions'])}, "
        f"encodings={counter_text(stats['BMG']['encodings'])}, entries={stats['BMG']['entries']}"
    )
    print(
        f"BRFNT: files parsed={stats['BRFNT']['parsed']}, errors={stats['BRFNT']['errors']}, "
        f"versions={counter_text(stats['BRFNT']['versions'])}, "
        f"sheet formats={counter_text(stats['BRFNT']['formats'])}, "
        f"sections={counter_text(stats['BRFNT']['sections'])}"
    )
    print(
        f"BTI: files parsed={stats['BTI']['parsed']}, errors={stats['BTI']['errors']}, "
        f"formats={counter_text(stats['BTI']['formats'])}, pixels={stats['BTI']['pixels']}"
    )
    for name in ("BMG", "BRFNT", "BTI"):
        for example in stats[name]["error_examples"]:
            print(f"{name} error: {example}")
    return int(any(stats[name]["errors"] for name in ("BMG", "BRFNT", "BTI")))


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Wii Play BMG, BRFNT, and BTI files")
    parser.add_argument("root", help="extracted disc root, including nested archives")
    parser.add_argument("--check", action="store_true", help="parse every matching file")
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check with an extracted disc root")
    return _check(args.root)


if __name__ == "__main__":
    raise SystemExit(main())
