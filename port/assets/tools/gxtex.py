#!/usr/bin/env python3
"""Standard-library GX texture decoder and whole-disc texture checker."""

from __future__ import annotations

import argparse
import collections
import os
from pathlib import Path
import struct
import sys
import zlib
from typing import Any

try:
    from . import fmt_brres_model, fmt_effect, fmt_layout, fmt_text_font, wiiarc
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import fmt_brres_model
    import fmt_effect
    import fmt_layout
    import fmt_text_font
    import wiiarc


FORMATS = {
    "I4": 0,
    "I8": 1,
    "IA4": 2,
    "IA8": 3,
    "RGB565": 4,
    "RGB5A3": 5,
    "RGBA8": 6,
    "C4": 8,
    "C8": 9,
    "C14X2": 10,
    "CMPR": 14,
}
FORMAT_IDS = dict(FORMATS)
FORMAT_IDS.update((f"C{n}", n) for n, f in ((8, "C4"), (9, "C8"), (10, "C14X2")))
PALETTE_FORMATS = {"IA8": 0, "RGB565": 1, "RGB5A3": 2}
PALETTE_FORMAT_IDS = dict(PALETTE_FORMATS)


def _normalize_format(fmt: Any, valid: dict[str, int], what: str) -> str:
    if isinstance(fmt, str):
        name = fmt.upper()
        if name not in valid:
            raise ValueError(f"unsupported {what} {fmt!r}")
        return name
    if isinstance(fmt, int) and not isinstance(fmt, bool):
        for name, value in valid.items():
            if value == fmt:
                return name
        raise ValueError(f"unsupported {what} {fmt}")
    raise ValueError(f"{what} must be a name or numeric format ID")


def _tile(fmt: str) -> tuple[int, int, int]:
    if fmt in ("I4", "C4", "CMPR"):
        return 8, 8, 32
    if fmt in ("I8", "IA4", "C8"):
        return 8, 4, 32
    return 4, 4, 64 if fmt == "RGBA8" else 32


def image_size(fmt: str, width: int, height: int, mipmap: bool = False, levels: int = 1) -> int:
    """Return the exact GX image size, matching GXGetTexBufferSize."""
    fmt = _normalize_format(fmt, FORMATS, "texture format")
    if width <= 0 or height <= 0 or levels <= 0:
        raise ValueError("width, height, and mipmap levels must be nonzero")
    tile_width, tile_height, tile_bytes = _tile(fmt)
    total = 0
    for _level in range(levels):
        tiles_x = (width + tile_width - 1) // tile_width
        tiles_y = (height + tile_height - 1) // tile_height
        total += tile_bytes * tiles_x * tiles_y
        if mipmap and width == 1 and height == 1:
            break
        if mipmap:
            width = max(1, (width + 1) // 2)
            height = max(1, (height + 1) // 2)
        else:
            break
    return total


def _decode_palette(palette: bytes, palette_fmt: Any) -> list[tuple[int, int, int, int]]:
    palette = bytes(palette)
    name = _normalize_format(palette_fmt, PALETTE_FORMATS, "palette format")
    if len(palette) % 2:
        raise ValueError("palette byte length must be even")
    colors: list[tuple[int, int, int, int]] = []
    for offset in range(0, len(palette), 2):
        word = int.from_bytes(palette[offset : offset + 2], "big")
        colors.append(_palette_color(name, word))
    return colors


def _palette_color(name: str, word: int) -> tuple[int, int, int, int]:
    if name == "IA8":
        intensity = word >> 8
        return intensity, intensity, intensity, word & 0xFF
    if name == "RGB565":
        r = ((word >> 11) & 0x1F) << 3
        g = ((word >> 5) & 0x3F) << 2
        b = (word & 0x1F) << 3
        return r, g, b, 255
    if word & 0x8000:
        r = ((word >> 10) & 0x1F) << 3
        g = ((word >> 5) & 0x1F) << 3
        b = (word & 0x1F) << 3
        return r, g, b, 255
    a = ((word >> 12) & 0x7) << 5
    r = ((word >> 8) & 0xF) << 4
    g = ((word >> 4) & 0xF) << 4
    b = (word & 0xF) << 4
    return r, g, b, a


def _indexed_formats() -> frozenset[str]:
    return frozenset(FORMATS[name] for name in ("C4", "C8", "C14X2"))


def _texel(fmt: str, source: bytes, palette: list[tuple[int, int, int, int]], index: int) -> tuple[int, int, int, int]:
    if fmt in ("I4", "I8", "IA4"):
        if fmt == "I8":
            value = source[index]
            return value, value, value, 255
        if fmt == "IA4":
            value = source[index]
            intensity = value & 0xF
            intensity |= intensity << 4
            return intensity, intensity, intensity, (value >> 4) << 4
        value = (source[index >> 1] >> (4 if index & 1 == 0 else 0)) & 0xF
        value |= value << 4
        return value, value, value, 255
    if fmt == "IA8":
        value = int.from_bytes(source[index * 2 : index * 2 + 2], "big")
        intensity = value >> 8
        return intensity, intensity, intensity, value & 0xFF
    if fmt == "RGB565":
        return _palette_color("RGB565", int.from_bytes(source[index * 2 : index * 2 + 2], "big"))
    if fmt == "RGB5A3":
        return _palette_color("RGB5A3", int.from_bytes(source[index * 2 : index * 2 + 2], "big"))
    raise ValueError(f"format {fmt} has no direct texel accessor")


def _decode_cmp_block(block: bytes) -> list[tuple[int, int, int, int]]:
    c1 = int.from_bytes(block[0:2], "big")
    c2 = int.from_bytes(block[2:4], "big")
    endpoints = [_palette_color("RGB565", c1), _palette_color("RGB565", c2)]
    if c1 > c2:
        palette = [
            endpoints[0],
            endpoints[1],
            _average_rgb(endpoints[0], endpoints[1], 2, 1),
            _average_rgb(endpoints[0], endpoints[1], 1, 2),
        ]
    else:
        palette = [
            endpoints[0],
            endpoints[1],
            _average_rgb(endpoints[0], endpoints[1], 1, 1),
            (0, 0, 0, 0),
        ]
    colors: list[tuple[int, int, int, int]] = []
    codes = int.from_bytes(block[4:8], "big")
    for y in range(4):
        for x in range(4):
            code = (codes >> (2 * (15 - y * 4 - x))) & 3
            colors.append(palette[code])
    return colors


def _average_rgb(
    first: tuple[int, int, int, int], second: tuple[int, int, int, int], first_scale: int, second_scale: int
) -> tuple[int, int, int, int]:
    return (
        (first[0] * first_scale + second[0] * second_scale) // (first_scale + second_scale),
        (first[1] * first_scale + second[1] * second_scale) // (first_scale + second_scale),
        (first[2] * first_scale + second[2] * second_scale) // (first_scale + second_scale),
        255,
    )


def _level_info(
    fmt: str, width: int, height: int, mipmap: bool, levels: int
) -> list[tuple[int, int, int]]:
    tile_width, tile_height, _tile_bytes = _tile(fmt)
    result: list[tuple[int, int, int]] = []
    offset = 0
    for _level in range(levels):
        tiles_x = (width + tile_width - 1) // tile_width
        tiles_y = (height + tile_height - 1) // tile_height
        size = image_size(fmt, width, height)
        result.append((offset, width, height))
        offset += size
        if mipmap and width == 1 and height == 1:
            break
        if mipmap:
            width = max(1, (width + 1) // 2)
            height = max(1, (height + 1) // 2)
        else:
            break
    return result


def decode(
    fmt: Any,
    data: bytes,
    width: int,
    height: int,
    palette: bytes | None = None,
    palette_fmt: Any = None,
    mipmap: bool = False,
    levels: int = 1,
) -> bytes:
    """Decode one base image plus optional consecutive mips to row-major RGBA."""
    name = _normalize_format(fmt, FORMATS, "texture format")
    data = bytes(data)
    if width <= 0 or height <= 0 or levels <= 0:
        raise ValueError("width, height, and mipmap levels must be nonzero")
    expected = image_size(name, width, height, mipmap, levels)
    if len(data) < expected:
        raise ValueError(f"image data is {len(data)} bytes, expected at least {expected}")

    colors: list[tuple[int, int, int, int]] = []
    if name in ("C4", "C8", "C14X2"):
        if palette is None:
            raise ValueError(f"{name} requires a palette")
        palette_name = "IA8" if palette_fmt is None else _normalize_format(
            palette_fmt, PALETTE_FORMATS, "palette format"
        )
        colors = _decode_palette(palette, palette_name)
        max_index = {"C4": 0xF, "C8": 0xFF, "C14X2": 0x3FFF}[name]
        if len(colors) <= max_index:
            raise ValueError(f"{name} palette must contain at least {max_index + 1} colors")
    elif palette is not None:
        raise ValueError(f"{name} does not use a palette")

    rgba = bytearray()
    for level_offset, level_width, level_height in _level_info(name, width, height, mipmap, levels):
        level_size = image_size(name, level_width, level_height)
        image = _decode_level(
            data[level_offset : level_offset + level_size],
            name,
            level_width,
            level_height,
            colors,
        )
        rgba.extend(image)
    return bytes(rgba)


def _decode_level(
    data: bytes,
    fmt: str,
    width: int,
    height: int,
    palette: list[tuple[int, int, int, int]],
) -> bytes:
    tile_width, tile_height, _tile_bytes = _tile(fmt)
    image = bytearray(width * height * 4)

    def put(x: int, y: int, color: tuple[int, int, int, int]) -> None:
        pixel = (y * width + x) * 4
        image[pixel : pixel + 4] = bytes(color)

    if fmt == "CMPR":
        tile_columns = (width + tile_width - 1) // tile_width
        for tile_y in range((height + 7) // 8):
            for tile_x in range(tile_columns):
                tile_offset = (tile_y * tile_columns + tile_x) * 32
                for block_y in range(2):
                    for block_x in range(2):
                        block_offset = tile_offset + (block_y * 2 + block_x) * 8
                        block = data[block_offset : block_offset + 8]
                        colors16 = _decode_cmp_block(block)
                        origin_x = tile_x * 8 + block_x * 4
                        origin_y = tile_y * 8 + block_y * 4
                        for local_y in range(4):
                            for local_x in range(4):
                                x, y = origin_x + local_x, origin_y + local_y
                                if x < width and y < height:
                                    put(x, y, colors16[local_y * 4 + local_x])
        return bytes(image)

    if fmt == "RGBA8":
        tile_columns = (width + 3) // 4
        for tile_y in range((height + 3) // 4):
            for tile_x in range(tile_columns):
                tile_offset = (tile_y * tile_columns + tile_x) * 64
                for local_y in range(4):
                    for local_x in range(4):
                        x, y = tile_x * 4 + local_x, tile_y * 4 + local_y
                        if x >= width or y >= height:
                            continue
                        pixel = (local_y * 4 + local_x) * 2
                        alpha = data[tile_offset + pixel]
                        red = data[tile_offset + pixel + 1]
                        green = data[tile_offset + 32 + pixel]
                        blue = data[tile_offset + 32 + pixel + 1]
                        put(x, y, (red, green, blue, alpha))
        return bytes(image)

    texels_per_tile = tile_width * tile_height
    palette_count = len(palette) if palette else 0
    for tile_y in range((height + tile_height - 1) // tile_height):
        for tile_x in range((width + tile_width - 1) // tile_width):
            tile_offset = (tile_y * ((width + tile_width - 1) // tile_width) + tile_x) * _tile(fmt)[2]
            for local_y in range(tile_height):
                for local_x in range(tile_width):
                    x, y = tile_x * tile_width + local_x, tile_y * tile_height + local_y
                    if x >= width or y >= height:
                        continue
                    local_index = local_y * tile_width + local_x
                    if fmt == "C4":
                        byte = data[tile_offset + (local_index >> 1)]
                        code = (byte >> (4 if local_index & 1 == 0 else 0)) & 0xF
                    elif fmt == "C8":
                        code = data[tile_offset + local_index]
                    elif fmt == "C14X2":
                        code = int.from_bytes(
                            data[tile_offset + local_index * 2 : tile_offset + local_index * 2 + 2], "big"
                        ) & 0x3FFF
                    else:
                        color = _texel(fmt, data[tile_offset:], palette, local_index)
                        put(x, y, color)
                        continue
                    if code >= palette_count:
                        raise ValueError(f"palette index {code} exceeds {palette_count} entries")
                    put(x, y, palette[code])
    return bytes(image)


def write_png(path: str | os.PathLike[str], width: int, height: int, rgba: bytes) -> None:
    """Write 8-bit RGBA PNG using only zlib."""
    if width <= 0 or height <= 0:
        raise ValueError("PNG dimensions must be nonzero")
    rgba = bytes(rgba)
    if len(rgba) != width * height * 4:
        raise ValueError(f"RGBA length {len(rgba)} does not match {width}x{height}")
    raw = b"".join(b"\x00" + rgba[row * width * 4 : (row + 1) * width * 4] for row in range(height))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)

    output = b"\x89PNG\r\n\x1a\n"
    output += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    output += chunk(b"IDAT", zlib.compress(raw, 9))
    output += chunk(b"IEND", b"")
    Path(path).write_bytes(output)


def _palette_binding(data: bytes, model: dict[str, Any]) -> dict[str, dict[str, Any]]:
    offsets = model["offset"] + model["toResTexNameToTexPlttInfoDic"]
    if offsets == 0:
        return {}
    dictionary = fmt_brres_model._dictionary(data, offsets, offsets)
    bindings: dict[str, dict[str, Any]] = {}
    for node in dictionary["data"][1:]:
        offset_base = node["dataOffset"]
        info_count = fmt_brres_model._unpack(data, offset_base, "I")[0]
        if info_count == 0:
            continue
        for info_index in range(info_count):
            to_info = fmt_brres_model._unpack(data, offset_base + 4 + info_index * 8 + 4, "i")[0]
            if to_info == 0:
                continue
            info = offset_base + to_info
            name_tex, name_pltt = fmt_brres_model._unpack(data, info, "ii")
            if name_tex == 0:
                continue
            texture_name = fmt_brres_model._cstring(data, info + name_tex)
            palette_name = None
            if name_pltt != 0:
                palette_name = fmt_brres_model._cstring(data, info + name_pltt)
            bindings[texture_name] = {"paletteName": palette_name}
    return bindings


def _brres_resources(data: bytes) -> list[tuple[str, int]]:
    root_dictionary = fmt_brres_model._dictionary(data, 0x18, 0x10)
    resources: list[tuple[str, int]] = []
    for group_node in root_dictionary["data"][1:]:
        group_offset = group_node["dataOffset"]
        group_dictionary = fmt_brres_model._dictionary(data, group_offset, group_offset, allow_zero=True)
        for resource_node in group_dictionary["data"][1:]:
            offset = resource_node["dataOffset"]
            fmt_brres_model._require(offset % 4 == 0, f"subfile at {offset:#x} is not aligned")
            resources.append((resource_node["name"], offset))
    return resources


def _tex0_items(data: bytes, decoded: dict[str, Any], path: str) -> list[dict[str, Any]]:
    palettes = {
        subfile["name"]: subfile
        for subfile in decoded["subfiles"]
        if subfile["header"]["kind"] == fmt_brres_model.PLT0_MAGIC
    }
    bindings: dict[str, dict[str, Any]] = {}
    for subfile in decoded["subfiles"]:
        if subfile["header"]["kind"] != fmt_brres_model.MDL0_MAGIC:
            continue
        bindings.update(_palette_binding(data, subfile))

    items: list[dict[str, Any]] = []
    for index, tex in enumerate(
        subfile for subfile in decoded["subfiles"] if subfile["header"]["kind"] == fmt_brres_model.TEX0_MAGIC
    ):
        fmt = tex["fmt"]
        palette = None
        palette_fmt = None
        binding = bindings.get(tex["name"] or tex["resourceName"])
        if fmt in _indexed_formats():
            palette_name = binding["paletteName"] if binding else None
            if palette_name is None and palettes:
                palette_name = tex["name"]
            if palette_name in palettes:
                palette_res = palettes[palette_name]
                start = palette_res["offset"] + palette_res["toPlttData"]
                count = palette_res["numEntries"]
                palette = data[start : start + count * 2]
                palette_fmt = palette_res["fmt"]
        levels = tex["mipmap_level"]
        expected = image_size(fmt, tex["width"], tex["height"], levels > 1, levels)
        start = tex["offset"] + tex["toTexData"]
        end = start + expected
        if end > tex["offset"] + tex["header"]["size"] or end > len(data):
            raise ValueError(f"{path}/TEX0[{index}] payload does not match GX size")
        items.append(
            {
                "label": f"{path}/TEX0/{tex['name'] or tex['resourceName']}",
                "format": fmt,
                "data": data[start:end],
                "width": tex["width"],
                "height": tex["height"],
                "palette": palette,
                "palette_fmt": palette_fmt,
                "levels": levels,
            }
        )
    return items


def _tpl_items(data: bytes, decoded: dict[str, Any], path: str) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for index, descriptor in enumerate(decoded["descriptors"]):
        header = descriptor["textureHeader"]
        if header is None:
            continue
        palette = None
        palette_fmt = None
        clut = descriptor["CLUTHeader"]
        if clut is not None:
            start = clut["dataOffset"]
            palette = data[start : start + clut["numEntries"] * 2]
            palette_fmt = clut["format"]
        levels = 1 if header["minLOD"] == header["maxLOD"] else max(1, header["maxLOD"])
        expected = image_size(header["format"], header["width"], header["height"], levels > 1, levels)
        start = header["dataOffset"]
        end = start + expected
        if end > len(data):
            raise ValueError(f"{path}/TPL[{index}] payload does not match GX size")
        items.append(
            {
                "label": f"{path}/TPL/{index}",
                "format": header["format"],
                "data": data[start:end],
                "width": header["width"],
                "height": header["height"],
                "palette": palette,
                "palette_fmt": palette_fmt,
                "levels": levels,
            }
        )
    return items


def _breft_items(data: bytes, decoded: dict[str, Any], path: str) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for index, texture in enumerate(decoded["textures"]):
        levels = 1 if not texture["mipmap"] else max(1, texture["dataSize"] // image_size(texture["format"], texture["width"], texture["height"]))
        expected = image_size(texture["format"], texture["width"], texture["height"], levels > 1, levels)
        if expected != texture["dataSize"]:
            raise ValueError(f"{path}/BREFT[{index}] dataSize does not match mipmap chain")
        items.append(
            {
                "label": f"{path}/BREFT/{index}",
                "format": texture["format"],
                "data": data[texture["textureDataOffset"] : texture["textureDataEndOffset"]],
                "width": texture["width"],
                "height": texture["height"],
                "palette": data[texture["tlutDataOffset"] : texture["tlutDataEndOffset"]] if texture["tlutSize"] else None,
                "palette_fmt": texture["tlutFormat"],
                "levels": levels,
            }
        )
    return items


def _brfnt_items(data: bytes, decoded: dict[str, Any], path: str) -> list[dict[str, Any]]:
    section = next(section for section in decoded["sections"] if section["kind"] == "TGLP")
    expected = section["sheetSize"]
    if section["sheetSize"] != expected:
        raise ValueError(f"{path}/TGLP sheetSize does not match GX size")
    if section["sheetImage"] + section["sheetNum"] * section["sheetSize"] > len(data):
        raise ValueError(f"{path}/TGLP sheets extend past the file")
    items: list[dict[str, Any]] = []
    for index in range(section["sheetNum"]):
        start = section["sheetImage"] + index * section["sheetSize"]
        items.append(
            {
                "label": f"{path}/BRFNT/sheet-{index}",
                "format": section["sheetFormat"],
                "data": data[start : start + section["sheetSize"]],
                "width": section["sheetWidth"],
                "height": section["sheetHeight"],
                "palette": None,
                "palette_fmt": None,
                "levels": 1,
            }
        )
    return items


def _bti_items(data: bytes, decoded: dict[str, Any], path: str) -> list[dict[str, Any]]:
    header = decoded["header"]
    mipmap = bool(header["mipMap"])
    levels = 1 if not mipmap else max(1, header["maxLOD"])
    expected = image_size(header["format"], header["width"], header["height"], mipmap, levels)
    start = decoded["imageOffsetResolved"]
    if start + expected > len(data):
        raise ValueError(f"{path}/BTI payload does not match GX size")
    palette = None
    palette_fmt = None
    if header["paletteEntryNum"]:
        if header["paletteOffset"] == 0:
            raise ValueError(f"{path}/BTI palette offset is zero")
        palette_start = header["paletteOffset"]
        palette_end = palette_start + header["paletteEntryNum"] * 2
        if palette_end > len(data):
            raise ValueError(f"{path}/BTI palette extends past the file")
        palette = data[palette_start:palette_end]
        palette_fmt = header["tlutFormat"]
    return [
        {
            "label": f"{path}/BTI",
            "format": header["format"],
            "data": data[start : start + expected],
            "width": header["width"],
            "height": header["height"],
            "palette": palette,
            "palette_fmt": palette_fmt,
            "levels": levels,
        }
    ]


def _texture_items(data: bytes, path: str) -> list[dict[str, Any]]:
    if data[:4] == b"bres":
        decoded = {
            "subfiles": [
                dict(
                    fmt_brres_model._tex0(data, offset)
                    if data[offset : offset + 4] == fmt_brres_model.TEX0_MAGIC
                    else fmt_brres_model._plt0(data, offset),
                    resourceName=name,
                    offset=offset,
                )
                for name, offset in _brres_resources(data)
                if data[offset : offset + 4] in (fmt_brres_model.TEX0_MAGIC, fmt_brres_model.PLT0_MAGIC)
            ]
        }
        return _tex0_items(data, decoded, path)
    if data[:4] == fmt_layout.TPL_MAGIC:
        return _tpl_items(data, fmt_layout.parse_tpl(data), path)
    if data[:4] == b"REFT":
        decoded = fmt_effect.parse(data)
        return [] if decoded["format"] != "BREFT" else _breft_items(data, decoded, path)
    if data[:4] == b"RFNT":
        return _brfnt_items(data, fmt_text_font.parse_brfnt(data), path)
    if path.lower().endswith(".bti"):
        return _bti_items(data, fmt_text_font.parse_bti(data), path)
    return []


def _decode_item(item: dict[str, Any]) -> bytes:
    mipmap = item["levels"] > 1
    return decode(
        item["format"],
        item["data"],
        item["width"],
        item["height"],
        item["palette"],
        item["palette_fmt"],
        mipmap,
        item["levels"],
    )


def _safe_name(label: str) -> str:
    name = label.replace("\\", "/").lstrip("/")
    return "".join(("_" if character in "\0/:?*" else character) for character in name)


def check(root: str, dump_dir: str | None = None) -> tuple[int, dict[str, collections.Counter[Any]]]:
    counts: dict[str, collections.Counter[Any]] = {
        "container": collections.Counter(),
        "format": collections.Counter(),
        "mipmap": collections.Counter(),
        "error": collections.Counter(),
    }
    errors = 0
    for path, data in wiiarc.walk(root):
        try:
            items = _texture_items(data, path)
        except Exception as error:
            errors += 1
            counts["error"][path] += 1
            print(f"ERROR {path}: {error}")
            continue
        for item in items:
            try:
                rgba = _decode_item(item)
                counts["container"][item["label"].split("/", 1)[0]] += 1
                counts["format"][FORMAT_IDS_INV[item["format"]]] += 1
                counts["mipmap"][item["levels"]] += 1
                if dump_dir is not None:
                    write_png(
                        os.path.join(dump_dir, _safe_name(item["label"]) + ".png"),
                        item["width"],
                        item["height"],
                        rgba[: item["width"] * item["height"] * 4],
                    )
            except Exception as error:
                errors += 1
                counts["error"][item["label"]] += 1
                print(f"ERROR {item['label']}: {error}")
    return errors, counts


FORMAT_IDS_INV = {
    0: "I4",
    1: "I8",
    2: "IA4",
    3: "IA8",
    4: "RGB565",
    5: "RGB5A3",
    6: "RGBA8",
    8: "C4",
    9: "C8",
    10: "C14X2",
    14: "CMPR",
}


def _counter_text(counter: collections.Counter[Any]) -> str:
    return ",".join(f"{key}={value}" for key, value in sorted(counter.items())) or "none"


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", metavar="ROOT", help="decode every texture under ROOT")
    parser.add_argument("--dump", metavar="DIR", help="write decoded PNGs to DIR outside this repository")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.check is None:
        _parser().error("--check ROOT is required")
    if not os.path.isdir(args.check):
        print(f"not a directory: {args.check}", file=sys.stderr)
        return 2
    dump_dir = None
    if args.dump is not None:
        root = Path.cwd().resolve()
        target = Path(args.dump).expanduser().resolve()
        if target == root or root in target.parents:
            print(f"--dump must be outside this repository: {target}", file=sys.stderr)
            return 2
        dump_dir = str(target)
        os.makedirs(dump_dir, exist_ok=True)
    errors, counts = check(args.check, dump_dir)
    print(f"textures decoded={sum(counts['format'].values())} errors={errors}")
    print(f"containers={_counter_text(counts['container'])}")
    print(f"formats={_counter_text(counts['format'])}")
    print(f"mipmaps={_counter_text(counts['mipmap'])}")
    if counts["error"]:
        print(f"error-paths={_counter_text(counts['error'])}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
