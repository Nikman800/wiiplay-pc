#!/usr/bin/env python3
"""Bounds-checked parsers and whole-disc checker for BRLYT, BRLAN and TPL."""

import argparse
import collections
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wiiarc import walk


LYT_BYTE_ORDER = 0xFEFF
LYT_VERSION = 8
TPL_MAGIC = b"\x00\x20\xaf\x30"
TPL_VERSION = 2142000
LYT_FILE_SIZE = 16

# Struct citations are given at each decoder. The following are shared:
# - nw4r::lyt::res::BinaryFileHeader: include/nw4r/lyt/lyt_resources.h:16-23
# - nw4r::lyt::res::DataBlockHeader: include/nw4r/lyt/lyt_resources.h:30-33
# - nw4r::math::VEC2/VEC3: include/nw4r/math.h (the game build uses 8/12-byte BE vectors)
# - GXColor/GXColorS10: include/revolution/GX/GXTypes.h:61-67


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise FormatError(message)


def unpack(data, offset, fmt, what):
    size = struct.calcsize(fmt)
    require(isinstance(data, (bytes, bytearray)), "input is not bytes")
    require(0 <= offset <= len(data) - size, f"{what} at 0x{offset:X} is out of bounds")
    return struct.unpack_from(">" + fmt, data, offset)


def u8(data, offset, what):
    return unpack(data, offset, "B", what)[0]


def u16(data, offset, what):
    return unpack(data, offset, "H", what)[0]


def u32(data, offset, what):
    return unpack(data, offset, "I", what)[0]


def f32(data, offset, what):
    value = unpack(data, offset, "f", what)[0]
    require(math.isfinite(value), f"{what} is not finite")
    return value


def block_at(data, offset):
    require(0 <= offset <= len(data) - 8, f"block header at 0x{offset:X} is out of bounds")
    kind, size = struct.unpack_from(">4sI", data, offset)
    require(size >= 8, f"block {kind!r} size is smaller than its header")
    require(offset + size <= len(data), f"block {kind!r} exceeds input")
    return kind, size


def file_header(data, signature):
    require(data[:4] == signature, f"file signature must be {signature!r}")
    byte_order, version = unpack(data, 4, "HH", "BRLYT/BRLAN byte order/version")
    file_size = u32(data, 8, "BRLYT/BRLAN fileSize")
    header_size, data_blocks = unpack(
        data, 12, "HH", "BRLYT/BRLAN header size/data blocks"
    )
    require(byte_order == LYT_BYTE_ORDER, "BRLYT/BRLAN byte order must be big-endian")
    require(version == LYT_VERSION, "BRLYT/BRLAN version must be 8")
    require(header_size == LYT_FILE_SIZE, "BRLYT/BRLAN header size must be 16")
    require(file_size == len(data), "BRLYT/BRLAN fileSize does not match input")
    return {
        "signature": signature.decode("ascii"),
        "byteOrder": byte_order,
        "version": version,
        "fileSize": file_size,
        "headerSize": header_size,
        "dataBlocks": data_blocks,
    }


def nul_name(base, offset, what):
    require(0 <= offset <= len(base), f"{what} offset 0x{offset:X} is out of bounds")
    end = base.find(b"\0", offset)
    require(end >= 0, f"{what} is not NUL-terminated")
    try:
        return base[offset:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise FormatError(f"{what} is not ASCII") from exc


def fixed_name(field, what):
    end = field.find(b"\0")
    if end < 0:
        end = len(field)
    return nul_name(field, 0, what) if end < len(field) else field.decode("ascii")


def vector2(data, offset, what):
    return {
        "x": f32(data, offset, f"{what}.x"),
        "y": f32(data, offset + 4, f"{what}.y"),
    }


def vector3(data, offset, what):
    return {
        "x": f32(data, offset, f"{what}.x"),
        "y": f32(data, offset + 4, f"{what}.y"),
        "z": f32(data, offset + 8, f"{what}.z"),
    }


def pane_fields(data, offset, size):
    fields = unpack(
        data, offset + 8, "BBBB16s8s", "PAN1-derived pane header"
    )
    return {
        "flag": fields[0],
        "basePosition": fields[1],
        "alpha": fields[2],
        "padding": fields[3],
        "name": fixed_name(fields[4], "pane name"),
        "userData": fixed_name(fields[5], "pane user data"),
        "translate": vector3(data, offset + 0x24, "pane translate"),
        "rotate": vector3(data, offset + 0x30, "pane rotate"),
        "scale": vector2(data, offset + 0x3C, "pane scale"),
        "size": vector2(data, offset + 0x44, "pane size"),
    }


def parse_lyt1(data, offset, size):
    # nw4r::lyt::res::Layout: include/nw4r/lyt/lyt_layout.h:40-47
    require(size == 20, "lyt1 size does not match its DataBlockHeader plus res::Layout")
    origin_type = u8(data, offset + 8, "lyt1 originType")
    padding = data[offset + 9:offset + 12]
    require(not any(padding), "lyt1 padding is not zero")
    return {
        "kind": "lyt1",
        "offset": offset,
        "size": size,
        "originType": origin_type,
        "padding": padding,
        "layoutSize": vector2(data, offset + 12, "lyt1 layoutSize"),
    }


def parse_string_list(data, offset, size, kind):
    # nw4r::lyt::res::TextureList: include/nw4r/lyt/lyt_resources.h:50-54
    # nw4r::lyt::res::Texture: include/nw4r/lyt/lyt_resources.h:40-44
    # nw4r::lyt::res::FontList: include/nw4r/lyt/lyt_resources.h:71-75
    # nw4r::lyt::res::Font: include/nw4r/lyt/lyt_resources.h:61-65
    count = u16(data, offset + 8, f"{kind} count")
    padding = data[offset + 10:offset + 12]
    require(not any(padding), f"{kind} padding is not zero")
    entries_offset = offset + 12
    require(entries_offset + 8 * count <= offset + size, f"{kind} entries exceed block")
    entries = []
    for index in range(count):
        entry_offset = entries_offset + 8 * index
        name_offset = u32(data, entry_offset, f"{kind} entry {index} nameStrOffset")
        resource_type = u8(data, entry_offset + 4, f"{kind} entry {index} type")
        padding = data[entry_offset + 5:entry_offset + 8]
        require(not any(padding), f"{kind} entry {index} padding is not zero")
        # nameStrOffset is relative to the first entry (block + 12), as lyt::res::GetStrTableStr reads it
        require(entries_offset + name_offset < offset + size, f"{kind} entry {index} name is out of bounds")
        name = nul_name(data, entries_offset + name_offset, f"{kind} entry {index} name")
        entries.append({"nameStrOffset": name_offset, "type": resource_type, "padding": padding, "name": name})
    return {
        "kind": kind,
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": kind, "size": size},
        "count": count,
        "padding": padding,
        "entries": entries,
    }


def parse_mat1(data, offset, size):
    # nw4r::lyt::res::MaterialList: include/nw4r/lyt/lyt_resources.h:82-86
    material_num = u16(data, offset + 8, "mat1 materialNum")
    padding = data[offset + 10:offset + 12]
    require(not any(padding), "mat1 padding is not zero")
    table_offset = offset + 12
    require(table_offset + 4 * material_num <= offset + size, "mat1 offset table exceeds block")
    materials = []
    # nw4r::lyt::MaterialResourceNum bits: include/nw4r/lyt/lyt_resources.h:118-154
    for index in range(material_num):
        material_offset = offset + u32(data, table_offset + 4 * index, f"mat1 offset {index}")
        material = parse_material_record(data, material_offset, offset + size)
        materials.append(material)
    return {
        "kind": "mat1",
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": "mat1", "size": size},
        "materialNum": material_num,
        "padding": padding,
        "materials": materials,
    }


def parse_material_record(data, offset, end):
    # nw4r::lyt::res::Material: include/nw4r/lyt/lyt_material.h:50-55
    require(offset + 0x40 <= end, "material fixed header is out of bounds")
    name = fixed_name(data[offset:offset + 20], "material name")
    tev_cols = []
    for index in range(3):
        tev_cols.append(list(unpack(data, offset + 0x14 + 8 * index, "hhhh", "material tevCols")))
    tev_kcols = list(data[offset + 0x2C:offset + 0x3C])
    resource_bits = u32(data, offset + 0x3C, "material resNum.bits")
    counts = {
        "texMap": resource_bits & 0xF,
        "texSRT": (resource_bits >> 4) & 0xF,
        "texCoordGen": (resource_bits >> 8) & 0xF,
        "tevSwapTable": (resource_bits >> 12) & 1,
        "indTexSRT": (resource_bits >> 13) & 3,
        "indTexStage": (resource_bits >> 15) & 7,
        "tevStage": (resource_bits >> 18) & 0x1F,
        "alphaCompare": (resource_bits >> 23) & 1,
        "blendMode": (resource_bits >> 24) & 1,
        "chanCtrl": (resource_bits >> 25) & 1,
        "matColor": (resource_bits >> 27) & 1,
    }
    require(resource_bits >> 29 == 0, "material resNum has unused high bits set")
    payload = 0x40
    payload += 4 * counts["texMap"]
    payload += 20 * counts["texSRT"]
    payload += 4 * counts["texCoordGen"]
    payload += 4 * counts["chanCtrl"]
    payload += 4 * counts["matColor"]
    payload += 4 * counts["tevSwapTable"]
    payload += 4 * counts["alphaCompare"]
    payload += 4 * counts["blendMode"]
    payload += 4 * counts["indTexStage"]
    payload += 20 * counts["indTexSRT"]
    payload += 16 * counts["tevStage"]
    require(offset + payload <= end, "material variable payload exceeds mat1 block")
    return {
        "offset": offset,
        "name": name,
        "tevCols": tev_cols,
        "tevKCols": [{"r": value[0], "g": value[1], "b": value[2], "a": value[3]} for value in zip(tev_kcols[::4], tev_kcols[1::4], tev_kcols[2::4], tev_kcols[3::4])],
        "resNum": {"bits": resource_bits, **counts},
        "payloadSize": payload,
    }


def parse_pas1(data, offset, size):
    # Layout handles pas1 as a DataBlockHeader without a payload structure:
    # src/nw4r/lyt/lyt_layout.cpp:142-145
    require(size == 8, "pas1 size is not 8")
    return {"kind": "pas1", "offset": offset, "size": size, "blockHeader": {"kind": "pas1", "size": size}}


def parse_pae1(data, offset, size):
    # Layout handles pae1 as a DataBlockHeader without a payload structure:
    # src/nw4r/lyt/lyt_layout.cpp:147-150
    require(size == 8, "pae1 size is not 8")
    return {"kind": "pae1", "offset": offset, "size": size, "blockHeader": {"kind": "pae1", "size": size}}


def parse_grs1(data, offset, size):
    # Layout handles grs1 as a DataBlockHeader without a payload structure:
    # src/nw4r/lyt/lyt_layout.cpp:173-175
    require(size == 8, "grs1 size is not 8")
    return {"kind": "grs1", "offset": offset, "size": size, "blockHeader": {"kind": "grs1", "size": size}}


def parse_gre1(data, offset, size):
    # Layout handles gre1 as a DataBlockHeader without a payload structure:
    # src/nw4r/lyt/lyt_layout.cpp:178-180
    require(size == 8, "gre1 size is not 8")
    return {"kind": "gre1", "offset": offset, "size": size, "blockHeader": {"kind": "gre1", "size": size}}


def parse_pan1(data, offset, size):
    # nw4r::lyt::res::Pane: include/nw4r/lyt/lyt_pane.h:53-67
    require(size == 76, "pan1 size does not match res::Pane")
    return {"kind": "pan1", "offset": offset, "size": size, "blockHeader": {"kind": "pan1", "size": size}, **pane_fields(data, offset, size)}


def parse_pic1(data, offset, size):
    # nw4r::lyt::res::Picture: include/nw4r/lyt/lyt_picture.h:22-29
    fields = pane_fields(data, offset, size)
    vtx_cols = list(unpack(data, offset + 0x4C, "IIII", "pic1 vtxCols"))
    material_idx, tex_coord_num, padding = unpack(data, offset + 0x5C, "HBB", "pic1 fields")
    require(padding == 0, "pic1 padding is not zero")
    payload_end = offset + 0x60 + 32 * tex_coord_num
    require(payload_end <= offset + size, "pic1 texture coordinates exceed block")
    return {
        "kind": "pic1",
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": "pic1", "size": size},
        **fields,
        "vtxCols": vtx_cols,
        "materialIdx": material_idx,
        "texCoordNum": tex_coord_num,
        "padding": padding,
        "texCoords": [vector2(data, offset + 0x60 + 32 * index + 8 * vertex, f"pic1 texCoord[{index}][{vertex}]") for index in range(tex_coord_num) for vertex in range(4)],
    }


def parse_txt1(data, offset, size):
    # nw4r::lyt::res::TextBox: include/nw4r/lyt/lyt_textBox.h:36-51
    fields = pane_fields(data, offset, size)
    text_buf_bytes, text_str_bytes, material_idx, font_idx, text_position, text_alignment = unpack(
        data, offset + 0x4C, "HHHHBB", "txt1 fields"
    )
    padding = data[offset + 0x56:offset + 0x58]
    text_offset = u32(data, offset + 0x58, "txt1 textStrOffset")
    text_cols = list(unpack(data, offset + 0x5C, "IIII", "txt1 textCols"))
    require(text_offset % 2 == 0, "txt1 textStrOffset is not UTF-16BE aligned")
    text_begin = offset + text_offset
    require(text_begin <= offset + size, "txt1 text offset is out of bounds")
    require(text_str_bytes <= text_buf_bytes, "txt1 text length exceeds buffer")
    require(text_begin + text_str_bytes <= offset + size, "txt1 text exceeds block")
    if text_str_bytes:
        require(text_str_bytes % 2 == 0, "txt1 text length is not UTF-16BE aligned")
        require(u16(data, text_begin + text_str_bytes - 2, "txt1 terminator") == 0, "txt1 text is not NUL-terminated")
    return {
        "kind": "txt1",
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": "txt1", "size": size},
        **fields,
        "textBufBytes": text_buf_bytes,
        "textStrBytes": text_str_bytes,
        "materialIdx": material_idx,
        "fontIdx": font_idx,
        "textPosition": text_position,
        "textAlignment": text_alignment,
        "padding": padding,
        "textStrOffset": text_offset,
        "textCols": text_cols,
        "fontSize": vector2(data, offset + 0x64, "txt1 fontSize"),
        "charSpace": f32(data, offset + 0x6C, "txt1 charSpace"),
        "lineSpace": f32(data, offset + 0x70, "txt1 lineSpace"),
    }


def parse_wnd1(data, offset, size):
    # nw4r::lyt::res::Window: include/nw4r/lyt/lyt_window.h:59-69
    fields = pane_fields(data, offset, size)
    inflation = vector2(data, offset + 0x4C, "wnd1 inflation l/r")
    inflation.update(vector2(data, offset + 0x54, "wnd1 inflation t/b"))
    frame_num = u8(data, offset + 0x5C, "wnd1 frameNum")
    padding = data[offset + 0x5D:offset + 0x60]
    require(not any(padding), "wnd1 padding is not zero")
    content_offset = u32(data, offset + 0x60, "wnd1 contentOffset")
    frame_table_offset = u32(data, offset + 0x64, "wnd1 frameOffsetTableOffset")
    frame_table = offset + frame_table_offset
    require(frame_table + 4 * frame_num <= offset + size, "wnd1 frame offset table exceeds block")
    frame_offsets = [u32(data, frame_table + 4 * index, f"wnd1 frame offset {index}") for index in range(frame_num)]
    frames = []
    for index, frame_offset in enumerate(frame_offsets):
        frame = offset + frame_offset
        require(offset <= frame <= offset + size - 4, f"wnd1 frame {index} is out of bounds")
        material_idx, texture_flip, padding = unpack(data, frame, "HBB", f"wnd1 frame {index}")
        require(padding == 0, f"wnd1 frame {index} padding is not zero")
        frames.append({"offset": frame_offset, "materialIdx": material_idx, "textureFlip": texture_flip, "padding": padding})
    return {
        "kind": "wnd1",
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": "wnd1", "size": size},
        **fields,
        "inflation": inflation,
        "frameNum": frame_num,
        "padding": padding,
        "contentOffset": content_offset,
        "frameOffsetTableOffset": frame_table_offset,
        "frameOffsets": frame_offsets,
        "frames": frames,
    }


def parse_bnd1(data, offset, size):
    # nw4r::lyt::res::Bounding derives res::Pane with no added fields:
    # include/nw4r/lyt/lyt_bounding.h:22-24
    require(size == 76, "bnd1 size does not match inherited res::Pane")
    return {"kind": "bnd1", "offset": offset, "size": size, "blockHeader": {"kind": "bnd1", "size": size}, **pane_fields(data, offset, size)}


def parse_grp1(data, offset, size):
    # nw4r::lyt::res::Group: include/nw4r/lyt/lyt_group.h:38-45
    name = fixed_name(data[offset + 8:offset + 24], "group name")
    pane_num = u16(data, offset + 0x18, "grp1 paneNum")
    padding = data[offset + 0x1A:offset + 0x1C]
    require(not any(padding), "grp1 padding is not zero")
    require(offset + 0x1C + 16 * pane_num <= offset + size, "grp1 pane names exceed block")
    pane_names = []
    for index in range(pane_num):
        entry = data[offset + 0x1C + 16 * index:offset + 0x2C + 16 * index]
        pane_names.append(fixed_name(entry, f"grp1 pane name {index}"))
    return {
        "kind": "grp1",
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": "grp1", "size": size},
        "name": name,
        "paneNum": pane_num,
        "padding": padding,
        "paneNames": pane_names,
    }


LYT_BLOCK_PARSERS = {
    b"lyt1": parse_lyt1,
    b"txl1": lambda data, offset, size: parse_string_list(data, offset, size, "txl1"),
    b"fnl1": lambda data, offset, size: parse_string_list(data, offset, size, "fnl1"),
    b"mat1": parse_mat1,
    b"pas1": parse_pas1,
    b"pae1": parse_pae1,
    b"grs1": parse_grs1,
    b"gre1": parse_gre1,
    b"pan1": parse_pan1,
    b"pic1": parse_pic1,
    b"txt1": parse_txt1,
    b"wnd1": parse_wnd1,
    b"bnd1": parse_bnd1,
    b"grp1": parse_grp1,
}


def parse_brlyt(data):
    """Parse a BRLYT file. Layout::Build: src/nw4r/lyt/lyt_layout.cpp:61-188."""
    header = file_header(data, b"RLYT")
    offset = header["headerSize"]
    sections = []
    for _ in range(header["dataBlocks"]):
        kind, size = block_at(data, offset)
        parser = LYT_BLOCK_PARSERS.get(kind)
        require(parser is not None, f"unsupported BRLYT block {kind!r}")
        sections.append(parser(data, offset, size))
        offset += size
    require(offset == len(data), "BRLYT blocks do not exactly cover the file")
    return {"format": "BRLYT", "header": header, "sections": sections}


def parse_pai1(data, offset, size):
    # nw4r::lyt::res::AnimationBlock: include/nw4r/lyt/lyt_animation.h:26-34
    # nw4r::lyt::res::AnimationContent: include/nw4r/lyt/lyt_animation.h:61-71
    # nw4r::lyt::res::AnimationInfo: include/nw4r/lyt/lyt_animation.h:41-45
    # nw4r::lyt::res::AnimationTarget: src/nw4r/lyt/lyt_animation.cpp:18-34
    # nw4r::lyt::res::StepKey: src/nw4r/lyt/lyt_animation.cpp:39-43
    # nw4r::lyt::res::HermiteKey: src/nw4r/lyt/lyt_animation.cpp:47-51
    base, end = offset, offset + size
    frame_size, loop, padding, file_num, anim_cont_num = unpack(
        data, offset + 8, "HBBHH", "pai1 fields"
    )
    anim_cont_offsets_offset = u32(data, offset + 16, "pai1 animContOffsetsOffset")
    require(padding == 0, "pai1 padding is not zero")
    string_table = base + 20
    require(string_table + 4 * file_num <= end, "pai1 file-name offset table exceeds block")
    file_names = []
    for index in range(file_num):
        string_offset = u32(data, string_table + 4 * index, f"pai1 file offset {index}")
        file_name = string_table + string_offset
        require(string_table <= file_name <= end, f"pai1 file name {index} is out of bounds")
        file_names.append(nul_name(data, file_name, f"pai1 file name {index}"))
    content_table = base + anim_cont_offsets_offset
    require(content_table + 4 * anim_cont_num <= end, "pai1 animation offset table exceeds block")
    content_offsets = [u32(data, content_table + 4 * index, f"pai1 animation offset {index}") for index in range(anim_cont_num)]
    contents = []
    for index, content_offset in enumerate(content_offsets):
        content = base + content_offset
        require(base <= content <= end - 24, f"pai1 animation content {index} is out of bounds")
        name = fixed_name(data[content:content + 20], f"pai1 animation content {index} name")
        info_num, anim_type = unpack(data, content + 20, "BB", f"pai1 animation content {index}")
        require(anim_type in (0, 1), f"pai1 animation content {index} type is invalid")
        info_table = content + 24
        require(info_table + 4 * info_num <= end, f"pai1 animation content {index} info table exceeds block")
        infos = []
        for info_index in range(info_num):
            info_offset = u32(data, info_table + 4 * info_index, f"pai1 animation info offset {info_index}")
            info = content + info_offset
            require(content <= info <= end - 8, f"pai1 animation info {info_index} is out of bounds")
            kind = data[info:info + 4]
            info_num_targets = u8(data, info + 4, f"pai1 animation info {info_index} num")
            info_padding = data[info + 5:info + 8]
            require(not any(info_padding), f"pai1 animation info {info_index} padding is not zero")
            require(kind in (b"RLPA", b"RLVI", b"RLVC", b"RLMC", b"RLTS", b"RLTP", b"RLIM"), f"pai1 animation kind {kind!r} is unsupported")
            target_table = info + 8
            require(target_table + 4 * info_num_targets <= end, f"pai1 animation info {info_index} target table exceeds block")
            targets = []
            for target_index in range(info_num_targets):
                target_offset = u32(data, target_table + 4 * target_index, f"pai1 target offset {target_index}")
                target = info + target_offset
                require(info <= target <= end - 12, f"pai1 target {target_index} is out of bounds")
                target_id, target_field, curve_type, target_padding = unpack(
                    data, target, "BBBB", f"pai1 target {target_index} header"
                )
                key_num = u16(data, target + 4, f"pai1 target {target_index} keyNum")
                keys_padding = data[target + 6:target + 8]
                keys_offset = u32(data, target + 8, f"pai1 target {target_index} keysOffset")
                require(target_padding == 0, f"pai1 target {target_index} padding is not zero")
                require(not any(keys_padding), f"pai1 target {target_index} key padding is not zero")
                require(curve_type in (1, 2), f"pai1 target {target_index} curve type is invalid")
                key_size = 8 if curve_type == 1 else 12
                keys = target + keys_offset
                require(keys + key_size * key_num <= end, f"pai1 target {target_index} keys exceed block")
                keys_data = []
                for key_index in range(key_num):
                    key = keys + key_size * key_index
                    if curve_type == 1:
                        frame = f32(data, key, f"pai1 step key {key_index} frame")
                        value = u16(data, key + 4, f"pai1 step key {key_index} value")
                        key_padding = data[key + 6:key + 8]
                        keys_data.append({"frame": frame, "value": value, "padding": key_padding})
                    else:
                        frame, value, slope = unpack(data, key, "fff", f"pai1 Hermite key {key_index}")
                        keys_data.append({"frame": frame, "value": value, "slope": slope})
                targets.append({
                    "id": target_id,
                    "target": target_field,
                    "curveType": curve_type,
                    "padding": target_padding,
                    "keyNum": key_num,
                    "keysPadding": keys_padding,
                    "keysOffset": keys_offset,
                    "keys": keys_data,
                })
            infos.append({"kind": kind.decode("ascii"), "offset": info_offset, "num": info_num_targets, "padding": info_padding, "targets": targets})
        contents.append({"name": name, "offset": content_offset, "num": info_num, "type": anim_type, "infos": infos})
    return {
        "kind": "pai1",
        "offset": offset,
        "size": size,
        "blockHeader": {"kind": "pai1", "size": size},
        "frameSize": frame_size,
        "loop": loop,
        "padding": padding,
        "fileNum": file_num,
        "animContNum": anim_cont_num,
        "animContOffsetsOffset": anim_cont_offsets_offset,
        "fileNameOffsets": [u32(data, string_table + 4 * index, f"pai1 file offset {index}") for index in range(file_num)],
        "fileNames": file_names,
        "animContOffsets": content_offsets,
        "animationContents": contents,
    }


def parse_brlan(data):
    """Parse a BRLAN file. Layout::CreateAnimTransform: src/nw4r/lyt/lyt_layout.cpp:190-245."""
    header = file_header(data, b"RLAN")
    offset = header["headerSize"]
    sections = []
    for _ in range(header["dataBlocks"]):
        kind, size = block_at(data, offset)
        require(kind == b"pai1", f"unsupported BRLAN block {kind!r}")
        sections.append(parse_pai1(data, offset, size))
        offset += size
    require(offset == len(data), "BRLAN blocks do not exactly cover the file")
    return {"format": "BRLAN", "header": header, "sections": sections}


def tex_buffer_size(width, height, texture_format, mip_map, max_lod):
    # GXGetTexBufferSize and its tile shifts:
    # src/revolution/GX/GXTexture.c:26-68 and src/revolution/GX/GXTexture.c:70-104
    shifts = {
        0: (3, 3), 1: (3, 2), 2: (3, 2), 3: (2, 2), 4: (2, 2), 5: (2, 2),
        6: (2, 2), 8: (3, 3), 9: (3, 2), 10: (2, 2), 14: (3, 3),
        17: (3, 2), 19: (2, 2), 22: (2, 2), 32: (3, 3), 34: (3, 2),
        35: (2, 2), 38: (2, 2), 39: (3, 2), 40: (3, 2), 41: (3, 2),
        42: (3, 2), 43: (2, 2), 44: (2, 2), 48: (3, 3), 57: (3, 2),
        58: (3, 2), 60: (2, 2),
    }
    require(texture_format in shifts, f"unsupported TPL texture format {texture_format}")
    shift_x, shift_y = shifts[texture_format]
    tile_bytes = 64 if texture_format in (6, 22) else 32
    if not mip_map:
        return tile_bytes * (
            ((width + (1 << shift_x) - 1) >> shift_x)
            * ((height + (1 << shift_y) - 1) >> shift_y)
        )
    total = 0
    for _ in range(max_lod):
        total += tile_bytes * (
            ((width + (1 << shift_x) - 1) >> shift_x)
            * ((height + (1 << shift_y) - 1) >> shift_y)
        )
        if width == 1 and height == 1:
            break
        width = width >> 1 if width > 1 else 1
        height = height >> 1 if height > 1 else 1
    return total


def parse_tpl(data):
    """Parse a TPL file. TPLBind/TPLGet: src/revolution/TPL/TPL.c:6-53."""
    # TPLPalette: include/revolution/TPL/TPL.h:39-43
    require(data[:4] == TPL_MAGIC, "TPL signature must be 00 20 AF 30")
    version = u32(data, 0, "TPL versionNumber")
    descriptor_num = u32(data, 4, "TPL numDescriptors")
    descriptor_table_offset = u32(data, 8, "TPL descriptorArrayOffset")
    require(version == TPL_VERSION, "TPL version must be 2142000")
    require(
        12 <= descriptor_table_offset <= len(data),
        "TPL descriptor-array offset is out of bounds",
    )
    descriptor_table = descriptor_table_offset
    require(descriptor_table + 8 * descriptor_num <= len(data), "TPL descriptor table is out of bounds")
    descriptors = []
    for index in range(descriptor_num):
        descriptor_offset = descriptor_table + 8 * index
        texture_offset = u32(data, descriptor_offset, f"TPL texture-header offset {index}")
        palette_offset = u32(data, descriptor_offset + 4, f"TPL CLUT-header offset {index}")
        require(texture_offset <= len(data), f"TPL texture header {index} is out of bounds")
        texture_header = None
        if texture_offset:
            texture_header = parse_tpl_header(data, texture_offset)
        palette_header = None
        if palette_offset:
            palette_header = parse_tpl_palette(data, palette_offset)
        descriptors.append({
            "offset": descriptor_offset,
            "textureHeaderOffset": texture_offset,
            "CLUTHeaderOffset": palette_offset,
            "textureHeader": texture_header,
            "CLUTHeader": palette_header,
        })
    return {
        "format": "TPL",
        "header": {
            "magic": data[:4].hex(),
            "versionNumber": version,
            "numDescriptors": descriptor_num,
            "descriptorArrayOffset": descriptor_table_offset,
        },
        "descriptors": descriptors,
    }


def parse_tpl_header(data, offset):
    # TPLHeader: include/revolution/TPL/TPL.h:10-24
    require(offset + 0x24 <= len(data), "TPL texture header is out of bounds")
    height, width, texture_format, data_offset = unpack(data, offset, "HHII", "TPL texture header")
    wrap_s, wrap_t, min_filter, mag_filter = unpack(data, offset + 0xC, "IIII", "TPL texture LOD fields")
    lod_bias = f32(data, offset + 0x1C, "TPL LODBias")
    edge_lod_enable, min_lod, max_lod, unpacked = unpack(data, offset + 0x20, "BBBB", "TPL texture flags")
    require(wrap_s in (0, 1, 2), "TPL wrapS is invalid")
    require(wrap_t in (0, 1, 2), "TPL wrapT is invalid")
    require(min_filter <= 5, "TPL minFilter is invalid")
    require(mag_filter in (0, 1), "TPL magFilter is invalid")
    require(unpacked == 0, "TPL texture header is already marked unpacked")
    mip_map = min_lod != max_lod
    payload_size = tex_buffer_size(width, height, texture_format, mip_map, max_lod)
    require(data_offset + payload_size <= len(data), "TPL image data is out of bounds")
    return {
        "offset": offset,
        "height": height,
        "width": width,
        "format": texture_format,
        "dataOffset": data_offset,
        "wrapS": wrap_s,
        "wrapT": wrap_t,
        "minFilter": min_filter,
        "magFilter": mag_filter,
        "LODBias": lod_bias,
        "edgeLODEnable": edge_lod_enable,
        "minLOD": min_lod,
        "maxLOD": max_lod,
        "unpacked": unpacked,
        "dataSize": payload_size,
    }


def parse_tpl_palette(data, offset):
    # TPLClutHeader: include/revolution/TPL/TPL.h:26-32
    require(offset + 12 <= len(data), "TPL CLUT header is out of bounds")
    num_entries, unpacked, padding, palette_format, data_offset = unpack(data, offset, "HBBII", "TPL CLUT header")
    require(padding == 0, "TPL CLUT padding is not zero")
    require(unpacked == 0, "TPL CLUT header is already marked unpacked")
    require(palette_format in (0, 1, 2), "TPL CLUT format is invalid")
    require(data_offset + 2 * num_entries <= len(data), "TPL CLUT data is out of bounds")
    return {
        "offset": offset,
        "numEntries": num_entries,
        "unpacked": unpacked,
        "pad8": padding,
        "format": palette_format,
        "dataOffset": data_offset,
        "dataSize": 2 * num_entries,
    }


def parse(data):
    """Parse one BRLYT, BRLAN or TPL file selected by its magic."""
    if data[:4] == b"RLYT":
        return parse_brlyt(data)
    if data[:4] == b"RLAN":
        return parse_brlan(data)
    if data[:4] == TPL_MAGIC:
        return parse_tpl(data)
    raise FormatError("unrecognized layout format magic")


def version_summary(versions):
    return ",".join(f"{version}={count}" for version, count in sorted(versions.items())) or "none"


def count_summary(counter):
    return ",".join(f"{name}={count}" for name, count in sorted(counter.items())) or "none"


def check(root):
    stats = {
        name: {"files": 0, "errors": 0, "versions": collections.Counter(), "counts": collections.Counter()}
        for name in ("BRLYT", "BRLAN", "TPL")
    }
    for path, data in walk(root):
        magic = data[:4]
        if magic == b"RLYT":
            format_name = "BRLYT"
        elif magic == b"RLAN":
            format_name = "BRLAN"
        elif magic == TPL_MAGIC:
            format_name = "TPL"
        else:
            continue
        stat = stats[format_name]
        stat["files"] += 1
        try:
            parsed = parse(data)
            stat["versions"][parsed["header"]["version" if format_name != "TPL" else "versionNumber"]] += 1
            if format_name == "BRLYT":
                for section in parsed["sections"]:
                    stat["counts"][section["kind"]] += 1
            elif format_name == "BRLAN":
                for section in parsed["sections"]:
                    stat["counts"][section["kind"]] += 1
                    stat["counts"]["animationContents"] += section["animContNum"]
                    stat["counts"]["targets"] += sum(len(info["targets"]) for content in section["animationContents"] for info in content["infos"])
                    stat["counts"]["keys"] += sum(len(target["keys"]) for content in section["animationContents"] for info in content["infos"] for target in info["targets"])
            else:
                stat["counts"]["descriptors"] += len(parsed["descriptors"])
                stat["counts"]["textureHeaders"] += sum(1 for descriptor in parsed["descriptors"] if descriptor["textureHeader"])
                stat["counts"]["CLUTHeaders"] += sum(1 for descriptor in parsed["descriptors"] if descriptor["CLUTHeader"])
        except (FormatError, struct.error, ValueError, IndexError) as exc:
            stat["errors"] += 1
            print(f"error: {path}: {format_name}: {exc}", file=sys.stderr)
    for format_name, stat in stats.items():
        print(
            f"{format_name}: files={stat['files']} errors={stat['errors']} "
            f"versions={version_summary(stat['versions'])} counts={count_summary(stat['counts'])}"
        )
    return 0 if all(stat["errors"] == 0 for stat in stats.values()) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", metavar="ROOT", help="parse every BRLYT, BRLAN and TPL under ROOT")
    args = parser.parse_args(argv)
    if args.check is None:
        parser.error("--check ROOT is required")
    return check(args.check)


if __name__ == "__main__":
    raise SystemExit(main())
