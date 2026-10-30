#!/usr/bin/env python3
"""Parse the BRRES container and its MDL0, TEX0, and PLT0 subfiles."""

import argparse
import collections
import os
import struct

try:
    from .wiiarc import walk
except ImportError:
    from wiiarc import walk


BRRES_MAGIC = b"bres"
ROOT_MAGIC = b"root"
BIG_ENDIAN = 0xFEFF
MDL0_MAGIC = b"MDL0"
TEX0_MAGIC = b"TEX0"
PLT0_MAGIC = b"PLT0"

MDL0_REVISION = 9
TEX0_REVISION = 1
PLT0_REVISION = 1

# Names in ResFileData's root dictionary; the game constructs these names at
# include/nw4r/g3d/res/g3d_resfile.h:30 and src/nw4r/g3d/res/g3d_resfile.cpp:7.
ROOT_GROUP_NAMES = (
    "3DModels(NW4R)",
    "Textures(NW4R)",
    "Palettes(NW4R)",
    "AnmChr(NW4R)",
    "AnmVis(NW4R)",
    "AnmClr(NW4R)",
    "AnmTexPat(NW4R)",
    "AnmTexSrt(NW4R)",
    "AnmShp(NW4R)",
    "AnmScn(NW4R)",
    "External",
)

# Struct citations:
# - ut::BinaryFileHeader: include/nw4r/ut/ut_binaryFileFormat.h:13
# - ut::BinaryBlockHeader: include/nw4r/ut/ut_binaryFileFormat.h:8
# - ResTopLevelDictData and ResFileData: include/nw4r/g3d/res/g3d_resfile.h:25,30
# - ResDicNodeData and ResDicData: include/nw4r/g3d/res/g3d_resdict.h:10,19
# - ResBlockHeaderData: include/nw4r/g3d/res/g3d_rescommon.h:140
# - ResMdlInfoData: include/nw4r/g3d/res/g3d_resmdl.h:45
# - ResMdlData: include/nw4r/g3d/res/g3d_resmdl.h:102
# - ResTexData: include/nw4r/g3d/res/g3d_restex.h:12
# - ResPlttData: include/nw4r/g3d/res/g3d_respltt.h:12


class FormatError(ValueError):
    pass


def _require(condition, message):
    if not condition:
        raise FormatError(message)


def _unpack(data, offset, fmt):
    _require(0 <= offset <= len(data), f"offset {offset:#x} is out of bounds")
    end = offset + struct.calcsize(fmt)
    _require(end <= len(data), f"range {offset:#x}..{end:#x} is out of bounds")
    return struct.unpack_from(">" + fmt, data, offset)


def _cstring(data, offset):
    _require(0 <= offset < len(data), f"string offset {offset:#x} is out of bounds")
    end = data.find(b"\0", offset)
    _require(end >= 0, f"string at {offset:#x} is not null-terminated")
    try:
        return data[offset:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise FormatError(f"string at {offset:#x} is not ASCII") from exc


def _binary_file_header(data):
    signature, byte_order, version, file_size, header_size, data_blocks = _unpack(
        data, 0, "4sHHIHH"
    )
    _require(signature == BRRES_MAGIC, f"expected bres magic, got {signature!r}")
    _require(byte_order == BIG_ENDIAN, f"expected byteOrder 0xFEFF, got {byte_order:#x}")
    _require(version == 0, f"expected container version 0, got {version:#x}")
    _require(file_size == len(data), f"fileSize {file_size} != {len(data)}")
    _require(header_size == 0x10, f"headerSize {header_size} != 0x10")
    _require(data_blocks >= 1, "dataBlocks must be nonzero")
    return {
        "signature": signature,
        "byteOrder": byte_order,
        "version": version,
        "fileSize": file_size,
        "headerSize": header_size,
        "dataBlocks": data_blocks,
    }


def _dictionary(data, offset, container_offset, allow_zero=False, top_level=False):
    _require(0 <= offset <= len(data), f"dictionary offset {offset:#x} is out of bounds")
    size, num_data = _unpack(data, offset, "II")
    node_end = offset + 8 + 16 * (num_data + 1)
    _require(node_end <= len(data), f"dictionary at {offset:#x} is out of bounds")
    _require(
        size == 8 + 16 * (num_data + 1),
        f"dictionary at {offset:#x} has size {size}, expected {node_end - offset}",
    )

    nodes = []
    for index in range(num_data + 1):
        ref, flag, left, right, string_offset, data_offset = _unpack(
            data, offset + 8 + 16 * index, "HHHHii"
        )
        _require(left <= num_data, f"node {index} left index {left} is out of range")
        _require(right <= num_data, f"node {index} right index {right} is out of range")
        absolute_data = container_offset + data_offset if index else None
        if index:
            # For the top-level dictionary, ofsData is relative to ResFileData.
            if offset == 0x18:
                absolute_data = offset + data_offset
        if index:
            _require(
                data_offset != 0,
                f"resource node {index} in dictionary at {offset:#x} has a zero data offset",
            )
            if not top_level:
                _require(
                    data_offset >= 0,
                    f"resource node {index} in dictionary at {offset:#x} "
                    f"has a negative data offset {data_offset:#x}",
                )
            _require(
                absolute_data in range(len(data) + 1),
                f"resource node {index} data offset {data_offset:#x} is out of bounds",
            )
        nodes.append(
            {
                "ref": ref,
                "flag": flag,
                "idxLeft": left,
                "idxRight": right,
                "ofsString": string_offset,
                "ofsData": data_offset,
                "name": _cstring(data, offset + string_offset)
                if string_offset != 0
                else None,
                "dataOffset": absolute_data,
            }
        )
    if not allow_zero:
        _require(nodes[0]["idxLeft"] != 0, f"dictionary at {offset:#x} is empty")
    _require(
        nodes[0]["ref"] == 0xFFFF or nodes[0]["ref"] == 0,
        f"dictionary root ref is {nodes[0]['ref']:#x}",
    )
    return {"offset": offset, "size": size, "numData": num_data, "data": nodes}


def _root_groups(data, file_header, root_block):
    root_offset = file_header["headerSize"]
    kind, size = _unpack(data, root_offset, "4sI")
    _require(kind == ROOT_MAGIC, f"expected root magic, got {kind!r}")
    _require(root_offset + size <= len(data), "root block extends beyond file")

    dictionary = _dictionary(
        data,
        root_offset + 8,
        container_offset=root_offset,
        allow_zero=True,
        top_level=True,
    )
    group_dictionaries = []
    for node in dictionary["data"][1:]:
        group_offset = node["dataOffset"]
        _require(group_offset % 4 == 0, f"root group at {group_offset:#x} is not aligned")
        group_dictionary = _dictionary(
            data, group_offset, container_offset=group_offset, allow_zero=True
        )
        group_block_end = group_offset + group_dictionary["size"]
        _require(group_block_end <= len(data), "root group extends beyond file")
        group_dictionaries.append(group_dictionary)

    for group_dictionary in group_dictionaries:
        names = [node["name"] for node in group_dictionary["data"][1:]]
        _require(
            len(names) == len(set(names)),
            "resource group contains duplicate resource names",
        )

    return {
        "header": {"kind": kind, "size": size},
        "offset": root_offset,
        "dictionary": dictionary,
        "groups": group_dictionaries,
    }


def _block_header(data, offset, expected_magic=None):
    kind, size = _unpack(data, offset, "4sI")
    if expected_magic is not None:
        _require(kind == expected_magic, f"expected {expected_magic!r}, got {kind!r}")
    _require(size >= 8, f"block at {offset:#x} has size {size}, below 8")
    _require(offset + size <= len(data), f"block at {offset:#x} extends beyond file")
    return {"kind": kind, "size": size}


def _mdl0(data, offset):
    header = _block_header(data, offset, MDL0_MAGIC)
    _require(header["size"] >= 0x80, "MDL0 block is shorter than ResMdlData plus ResMdlInfoData")
    (
        revision,
        to_res_file_data,
        to_res_byte_code_dic,
        to_res_node_dic,
        to_res_vtx_pos_dic,
        to_res_vtx_nrm_dic,
        to_res_vtx_clr_dic,
        to_res_vtx_tex_coord_dic,
        to_res_mat_dic,
        to_res_tev_dic,
        to_res_shp_dic,
        to_res_tex_name_to_tex_pltt_info_dic,
        to_res_pltt_name_to_tex_pltt_info_dic,
        name,
    ) = _unpack(data, offset + 8, "I" + "i" * 13)
    _require(revision == MDL0_REVISION, f"MDL0 revision {revision} != {MDL0_REVISION}")
    _require(to_res_file_data == -offset, "MDL0 toResFileData does not target the file header")

    (
        scaling_rule,
        tex_mtx_mode,
        vertex_size,
        triangle_size,
        original_path,
        num_view_mtx,
    ) = _unpack(data, offset + 0x48, "iiiiii")
    info_size, to_res_mdl_data = _unpack(data, offset + 0x40, "Ii")
    (
        need_nrm_mtx_array,
        need_tex_mtx_array,
        is_valid_volume,
        envelope_mtx_mode,
        to_mtx_id_to_node_id,
    ) = _unpack(data, offset + 0x60, "BBBBi")
    (
        volume_min_x,
        volume_min_y,
        volume_min_z,
        volume_max_x,
        volume_max_y,
        volume_max_z,
    ) = _unpack(data, offset + 0x64, "ffffff")
    _require(info_size == 0x40, f"ResMdlInfoData size {info_size} != 0x40")
    _require(to_res_mdl_data == -0x40, "ResMdlInfoData toResMdlData does not target ResMdlData")
    if original_path != 0:
        _require(original_path >= 0, f"MDL0 original_path offset {original_path:#x} is negative")
        _require(
            offset + 0x40 + original_path in range(len(data)),
            f"MDL0 original_path offset {original_path:#x} is out of bounds",
        )
    if to_mtx_id_to_node_id != 0:
        _require(
            offset + 0x40 + to_mtx_id_to_node_id in range(len(data) + 1),
            f"MDL0 toMtxIDToNodeID offset {to_mtx_id_to_node_id:#x} is out of bounds",
        )

    section_fields = {
        "toResByteCodeDic": to_res_byte_code_dic,
        "toResNodeDic": to_res_node_dic,
        "toResVtxPosDic": to_res_vtx_pos_dic,
        "toResVtxNrmDic": to_res_vtx_nrm_dic,
        "toResVtxClrDic": to_res_vtx_clr_dic,
        "toResVtxTexCoordDic": to_res_vtx_tex_coord_dic,
        "toResMatDic": to_res_mat_dic,
        "toResTevDic": to_res_tev_dic,
        "toResShpDic": to_res_shp_dic,
        "toResTexNameToTexPlttInfoDic": to_res_tex_name_to_tex_pltt_info_dic,
        "toResPlttNameToTexPlttInfoDic": to_res_pltt_name_to_tex_pltt_info_dic,
    }
    sections = {}
    for field_name, relative_offset in section_fields.items():
        if relative_offset == 0:
            sections[field_name] = None
            continue
        absolute = offset + relative_offset
        _require(
            absolute in range(len(data) + 1),
            f"MDL0 {field_name} offset {relative_offset:#x} is out of bounds",
        )
        sections[field_name] = _dictionary(
            data, absolute, container_offset=offset, allow_zero=True
        )
        _require(
            relative_offset >= 0,
            f"MDL0 {field_name} offset {relative_offset:#x} is negative",
        )

    return {
        "header": header,
        "revision": revision,
        "toResFileData": to_res_file_data,
        **section_fields,
        "name": _cstring(data, offset + name) if name != 0 else None,
        "info": {
            "size": info_size,
            "toResMdlData": to_res_mdl_data,
            "scaling_rule": scaling_rule,
            "tex_mtx_mode": tex_mtx_mode,
            "vertex_size": vertex_size,
            "triangle_size": triangle_size,
            "original_path": _cstring(data, offset + 0x40 + original_path)
            if original_path != 0
            else None,
            "numViewMtx": num_view_mtx,
            "need_nrm_mtx_array": need_nrm_mtx_array,
            "need_tex_mtx_array": need_tex_mtx_array,
            "is_valid_volume": is_valid_volume,
            "envelope_mtx_mode": envelope_mtx_mode,
            "toMtxIDToNodeID": to_mtx_id_to_node_id,
            "toMtxIDToNodeIDOffset": (
                offset + 0x40 + to_mtx_id_to_node_id
                if to_mtx_id_to_node_id != 0
                else None
            ),
            "volume_min": [volume_min_x, volume_min_y, volume_min_z],
            "volume_max": [volume_max_x, volume_max_y, volume_max_z],
        },
        "sections": sections,
    }


def _tex0(data, offset):
    header = _block_header(data, offset, TEX0_MAGIC)
    _require(header["size"] >= 0x38, "TEX0 block is shorter than ResTexData")
    (
        revision,
        to_res_file_data,
        to_tex_data,
        name,
        flag,
        width,
        height,
        fmt,
        mipmap_level,
        min_lod,
        max_lod,
        original_path,
        to_res_user_data,
    ) = _unpack(data, offset + 8, "IiiIIHHIIfFii")
    _require(revision == TEX0_REVISION, f"TEX0 revision {revision} != {TEX0_REVISION}")
    _require(to_res_file_data == -offset, "TEX0 toResFileData does not target the file header")
    _require(width != 0, "TEX0 width is zero")
    _require(height != 0, "TEX0 height is zero")
    _require(mipmap_level != 0, "TEX0 mipmap_level is zero")
    if to_tex_data != 0:
        _require(to_tex_data >= 0, f"TEX0 toTexData offset {to_tex_data:#x} is negative")
        _require(
            offset + to_tex_data < len(data),
            f"TEX0 toTexData offset {to_tex_data:#x} is out of bounds",
        )
    if original_path != 0:
        _require(original_path >= 0, f"TEX0 original_path offset {original_path:#x} is negative")
        _require(
            offset + original_path in range(len(data)),
            f"TEX0 original_path offset {original_path:#x} is out of bounds",
        )
    return {
        "header": header,
        "revision": revision,
        "toResFileData": to_res_file_data,
        "toTexData": to_tex_data,
        "name": _cstring(data, offset + name) if name != 0 else None,
        "flag": flag,
        "width": width,
        "height": height,
        "fmt": fmt,
        "mipmap_level": mipmap_level,
        "min_lod": min_lod,
        "max_lod": max_lod,
        "original_path": _cstring(data, offset + original_path)
        if original_path != 0
        else None,
        "toResUserData": to_res_user_data,
        "toResUserDataOffset": (
            offset + to_res_user_data if to_res_user_data != 0 else None
        ),
        "toResUserDataOffset": (
            offset + to_res_user_data if to_res_user_data != 0 else None
        ),
    }


def _plt0(data, offset):
    header = _block_header(data, offset, PLT0_MAGIC)
    _require(header["size"] >= 0x28, "PLT0 block is shorter than ResPlttData")
    (
        revision,
        to_res_file_data,
        to_pltt_data,
        name,
        fmt,
        num_entries,
        padding,
        original_path,
        to_res_user_data,
    ) = _unpack(data, offset + 8, "IiiiIHHii")
    _require(revision == PLT0_REVISION, f"PLT0 revision {revision} != {PLT0_REVISION}")
    _require(to_res_file_data == -offset, "PLT0 toResFileData does not target the file header")
    _require(num_entries != 0, "PLT0 numEntries is zero")
    _require(padding == 0, f"PLT0 PADDING_0x1E is {padding}, expected zero")
    if to_pltt_data != 0:
        _require(to_pltt_data >= 0, f"PLT0 toPlttData offset {to_pltt_data:#x} is negative")
        _require(
            offset + to_pltt_data < len(data),
            f"PLT0 toPlttData offset {to_pltt_data:#x} is out of bounds",
        )
    if original_path != 0:
        _require(
            offset + original_path in range(len(data)),
            f"PLT0 original_path offset {original_path:#x} is out of bounds",
        )
    return {
        "header": header,
        "revision": revision,
        "toResFileData": to_res_file_data,
        "toPlttData": to_pltt_data,
        "name": _cstring(data, offset + name) if name != 0 else None,
        "fmt": fmt,
        "numEntries": num_entries,
        "PADDING_0x1E": padding,
        "original_path": _cstring(data, offset + original_path)
        if original_path != 0
        else None,
        "toResUserData": to_res_user_data,
    }


def parse(data):
    """Parse bytes and return decoded headers, dictionaries, and section tables."""
    data = bytes(data)
    file_header = _binary_file_header(data)
    root_block = {"kind": ROOT_MAGIC, "size": _unpack(data, 0x10, "I")[0]}
    root = _root_groups(data, file_header, root_block)

    subfiles = []
    root_nodes = root["dictionary"]["data"][1:]
    _require(
        len(root_nodes) == len(root["groups"]),
        "root dictionary/group count mismatch",
    )
    for node, group in zip(root_nodes, root["groups"]):
        group_name = node["name"]
        _require(
            group_name in ROOT_GROUP_NAMES,
            f"unknown BRRES root group {group_name!r}",
        )
        for resource_node in group["data"][1:]:
            resource_offset = resource_node["dataOffset"]
            _require(resource_offset % 4 == 0, f"subfile at {resource_offset:#x} is not aligned")
            kind = _unpack(data, resource_offset, "4s")[0]
            if kind == MDL0_MAGIC:
                parsed = _mdl0(data, resource_offset)
            elif kind == TEX0_MAGIC:
                parsed = _tex0(data, resource_offset)
            elif kind == PLT0_MAGIC:
                parsed = _plt0(data, resource_offset)
            else:
                parsed = {"header": _block_header(data, resource_offset)}
            parsed["dictionaryName"] = group_name
            parsed["resourceName"] = resource_node["name"]
            parsed["offset"] = resource_offset
            subfiles.append(parsed)

    block_offsets = [root["offset"]] + [subfile["offset"] for subfile in subfiles]
    block_offsets.sort()
    expected = file_header["headerSize"]
    for block_offset in block_offsets:
        _require(block_offset >= expected, f"overlapping block at {block_offset:#x}")
        size = _unpack(data, block_offset + 4, "I")[0]
        expected = block_offset + size
    _require(
        expected <= len(data),
        f"BRRES blocks end at {expected:#x}, beyond file size {len(data):#x}",
    )

    return {"fileHeader": file_header, "root": root, "subfiles": subfiles}


def _summary(items):
    parsed = len(items)
    errors = sum(1 for _path, _decoded, error in items if error is not None)
    versions = collections.Counter(
        (subfile["header"]["kind"].decode("ascii"), subfile["revision"])
        for _path, decoded, error in items
        if error is None
        for subfile in decoded.get("subfiles", [decoded])
        if subfile["header"]["kind"] in (MDL0_MAGIC, TEX0_MAGIC, PLT0_MAGIC)
    )
    counts = collections.Counter()
    sizes = collections.Counter()
    widths = collections.Counter()
    heights = collections.Counter()
    mipmaps = collections.Counter()
    formats = collections.Counter()
    entries = collections.Counter()
    section_counts = collections.Counter()
    for _path, decoded, error in items:
        if error is not None:
            continue
        subfiles = decoded.get("subfiles", [decoded])
        for subfile in subfiles:
            kind = subfile["header"]["kind"].decode("ascii")
            counts[kind] += 1
            sizes[kind] += subfile["header"]["size"]
            if kind == "TEX0":
                widths[subfile["width"]] += 1
                heights[subfile["height"]] += 1
                mipmaps[subfile["mipmap_level"]] += 1
                formats[subfile["fmt"]] += 1
            elif kind == "PLT0":
                entries[subfile["numEntries"]] += 1
            elif kind == "MDL0":
                for name, section in subfile["sections"].items():
                    if section is not None:
                        section_counts[name] += section["numData"]
    return {
        "files": parsed,
        "errors": errors,
        "versions": versions,
        "counts": counts,
        "sizes": sizes,
        "widths": widths,
        "heights": heights,
        "mipmaps": mipmaps,
        "formats": formats,
        "entries": entries,
        "sections": section_counts,
    }


def _format_counter(counter, key_format=None):
    if not counter:
        return "0"
    if key_format:
        return ", ".join(key_format.format(key, value) for key, value in sorted(counter.items()))
    return ", ".join(f"{key}={value}" for key, value in sorted(counter.items(), key=lambda x: str(x[0])))


def check(root_path):
    format_items = collections.OrderedDict(
        (name, []) for name in ("BRRES", "MDL0", "TEX0", "PLT0")
    )
    for path, data in walk(root_path):
        if data[:4] == BRRES_MAGIC:
            try:
                decoded = parse(data)
                error = None
            except (FormatError, struct.error, ValueError) as exc:
                decoded = None
                error = f"{path}: {exc}"
            format_items["BRRES"].append((path, decoded, error))
            if error is None:
                for subfile in decoded["subfiles"]:
                    kind = subfile["header"]["kind"].decode("ascii")
                    if kind in ("MDL0", "TEX0", "PLT0"):
                        format_items[kind].append((path, subfile, None))
        elif data[:4] in (MDL0_MAGIC, TEX0_MAGIC, PLT0_MAGIC):
            # These magics are embedded subfile blocks, not standalone disc files.
            # Continue walking; only matching BRRES files are selected by magic.
            pass

    summaries = []
    total_errors = 0
    for container_name, items in format_items.items():
        summary = _summary(items)
        total_errors += summary["errors"]
        if container_name == "BRRES":
            facts = [
                f"subfiles={_format_counter(summary['counts'])}",
                f"subfile_bytes={dict(summary['sizes'])}",
            ]
        elif container_name == "MDL0":
            facts = [
                f"versions={_format_counter(summary['versions'])}",
                f"sections={_format_counter(summary['sections'])}",
                f"section_entries={sum(summary['sections'].values())}",
            ]
        elif container_name == "TEX0":
            facts = [
                f"versions={_format_counter(summary['versions'])}",
                f"widths={_format_counter(summary['widths'])}",
                f"heights={_format_counter(summary['heights'])}",
                f"mipmaps={_format_counter(summary['mipmaps'])}",
                f"formats={_format_counter(summary['formats'], '{0:#x}:{1}')}",
            ]
        else:
            facts = [
                f"versions={_format_counter(summary['versions'])}",
                f"formats={_format_counter(summary['formats'], '{0:#x}:{1}')}",
                f"entries={_format_counter(summary['entries'])}",
            ]
        summaries.append(
            f"{container_name}: files={summary['files']} errors={summary['errors']} "
            + " ".join(facts)
        )

    lines = list(summaries)
    if not any(line.startswith("BRRES: files=") for line in lines):
        lines.insert(0, "BRRES: files=0 errors=0")
    for _path, _decoded, error in format_items["BRRES"]:
        if error is not None:
            lines.append(f"ERROR {error}")
    return total_errors, os.linesep.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", metavar="ROOT", help="parse every BRRES under ROOT")
    parser.add_argument("--json", action="store_true", help="print parse() as JSON")
    parser.add_argument("file", nargs="?", help="BRRES file for --json")
    args = parser.parse_args()

    if args.json:
        if not args.file:
            parser.error("--json requires FILE")
        import json

        with open(args.file, "rb") as stream:
            print(json.dumps(parse(stream.read()), indent=2))
        return 0
    if args.check:
        errors, output = check(args.check)
        print(output)
        return 1 if errors else 0
    parser.error("use --check ROOT or --json FILE")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
