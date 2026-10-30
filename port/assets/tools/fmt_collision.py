#!/usr/bin/env python3
"""Parser and validator for Wii Play's KCL and PMP collision resources.

Layout source locations:
* RPKclHeader: include/Pack/RPParty/RPPartyCommon/RPKcl.h:28
* RPKclPrism: include/Pack/RPParty/RPPartyCommon/RPKcl.h:14
* RPResData: include/Pack/RPParty/RPPartyCommon/RPResFile_2.h:10
* RPPathRes: include/Pack/RPParty/RPPartyCommon/RPPathFollower.h:30
* RPPathPoint: include/Pack/RPParty/RPPartyCommon/RPPathFollower.h:18
* RPResEntry: include/Pack/RPParty/RPPartyCommon/RPPathFollower.h:46
"""

from __future__ import annotations

import argparse
import collections
import math
import os
import struct
from typing import Any, Iterator

import wiiarc


KCL_HEADER_SIZE_38 = 0x38
KCL_HEADER_SIZE_3C = 0x3C
KCL_HEADER_SIZES = (KCL_HEADER_SIZE_38, KCL_HEADER_SIZE_3C)
PMP_MAGIC = b"PMPF"


class FormatError(ValueError):
    """A collision resource does not match the loader's invariants."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise FormatError(message)


def _u16(data: bytes, offset: int, name: str) -> int:
    _require(0 <= offset <= len(data) - 2, f"{name} at 0x{offset:X} is out of bounds")
    return struct.unpack_from(">H", data, offset)[0]


def _u32(data: bytes, offset: int, name: str) -> int:
    _require(0 <= offset <= len(data) - 4, f"{name} at 0x{offset:X} is out of bounds")
    return struct.unpack_from(">I", data, offset)[0]


def _s32(data: bytes, offset: int, name: str) -> int:
    _require(0 <= offset <= len(data) - 4, f"{name} at 0x{offset:X} is out of bounds")
    return struct.unpack_from(">i", data, offset)[0]


def _f32(data: bytes, offset: int, name: str) -> float:
    _require(0 <= offset <= len(data) - 4, f"{name} at 0x{offset:X} is out of bounds")
    value = struct.unpack_from(">f", data, offset)[0]
    _require(math.isfinite(value), f"{name} at 0x{offset:X} is not finite")
    return value


def _offset(data: bytes, offset: int, name: str) -> int:
    value = _u32(data, offset, name)
    _require(value < len(data), f"{name} 0x{value:X} is outside the file")
    return value


def _is_kcl(data: bytes) -> bool:
    return len(data) >= 4 and struct.unpack_from(">I", data)[0] in KCL_HEADER_SIZES


def parse_kcl(data: bytes) -> dict[str, Any]:
    """Decode a big-endian KCL and validate all offsets and references."""
    _require(len(data) >= KCL_HEADER_SIZE_38, "KCL file is shorter than the 0x38-byte header")
    header_size = _u32(data, 0, "header size")
    _require(header_size in KCL_HEADER_SIZES, f"unsupported KCL header size 0x{header_size:X}")

    positions_offset = _offset(data, 0, "pPositions")
    _require(positions_offset == header_size, "pPositions does not equal the header size")
    normals_offset = _offset(data, 4, "pNormals")
    prisms_offset = _offset(data, 8, "pPrisms")
    blocks_offset = _offset(data, 12, "pBlocks")
    _require(
        header_size <= positions_offset < normals_offset < prisms_offset < blocks_offset,
        "KCL section offsets must be ordered positions, normals, prisms, octree",
    )
    for name, value in (
        ("pPositions", positions_offset),
        ("pNormals", normals_offset),
        ("pPrisms", prisms_offset),
        ("pBlocks", blocks_offset),
    ):
        _require(value % 4 == 0, f"{name} is not 4-byte aligned")

    position_capacity = (normals_offset - positions_offset) // 12
    normal_capacity = (prisms_offset - normals_offset) // 12
    _require(normals_offset - positions_offset >= 12, "positions section is too short")
    _require(prisms_offset - normals_offset >= 12, "normals section is too short")
    prism_count, remainder = divmod(blocks_offset - prisms_offset, 16)
    _require(remainder == 0, "prisms section is not a whole number of RPKclPrisms")
    overlay_normal_count = _kcl_overlay_normal_count(prisms_offset, prism_count, normals_offset)

    thickness = _f32(data, 16, "thickness")
    area_min = (
        _f32(data, 20, "areaMin.x"),
        _f32(data, 24, "areaMin.y"),
        _f32(data, 28, "areaMin.z"),
    )
    mask_x = _u32(data, 32, "maskX")
    mask_y = _u32(data, 36, "maskY")
    mask_z = _u32(data, 40, "maskZ")
    shift = _u32(data, 44, "shift")
    shift_y = _u32(data, 48, "shiftY")
    shift_z = _u32(data, 52, "shiftZ")

    _require(shift < 32, "shift must be less than 32")
    _require(shift_y + shift < 32, "shiftY plus shift overflows the key")
    _require(shift_z + shift < 32, "shiftZ plus shift overflows the key")
    _require((mask_x & 1) == 0, "maskX includes the cell-size bit")
    _require((mask_y & 1) == 0, "maskY includes the cell-size bit")
    _require((mask_z & 1) == 0, "maskZ includes the cell-size bit")
    extent_x = (~mask_x & 0xFFFFFFFF) >> shift
    extent_y = (~mask_y & 0xFFFFFFFF) >> shift
    extent_z = (~mask_z & 0xFFFFFFFF) >> shift
    _require(extent_x < (1 << shift_y), "maskX extent overlaps maskY bits")
    _require(extent_y < (1 << shift_z), "maskY extent overlaps maskZ bits")
    _require(extent_z < (1 << (32 - shift - shift_z)), "maskZ extent overflows the key")

    # RPKclHeader::sphereRadius is at 0x38. It belongs to the header only when
    # header_size is 0x3C; for 0x38 files it overlays positions[0].x.
    sphere_radius = _f32(data, 0x38, "sphereRadius")

    positions = []
    for index in range(position_capacity):
        base = positions_offset + index * 12
        positions.append(
            {
                "x": _f32(data, base, f"positions[{index}].x"),
                "y": _f32(data, base + 4, f"positions[{index}].y"),
                "z": _f32(data, base + 8, f"positions[{index}].z"),
            }
        )

    normals = []
    for index in range(normal_capacity + overlay_normal_count):
        base = normals_offset + index * 12
        source = "dummy prism overlay" if index >= normal_capacity else "section"
        normals.append(
            {
                "source": source,
                "x": _f32(data, base, f"normals[{index}].x"),
                "y": _f32(data, base + 4, f"normals[{index}].y"),
                "z": _f32(data, base + 8, f"normals[{index}].z"),
            }
        )

    prisms = []
    used_normals = set()
    attributes = set()
    for index in range(prism_count):
        base = prisms_offset + index * 16
        prism = {
            "height": _f32(data, base, f"prisms[{index}].height"),
            "positionIdx": _u16(data, base + 4, f"prisms[{index}].positionIdx"),
            "normal0Idx": _u16(data, base + 6, f"prisms[{index}].normal0Idx"),
            "normal1Idx": _u16(data, base + 8, f"prisms[{index}].normal1Idx"),
            "normal2Idx": _u16(data, base + 10, f"prisms[{index}].normal2Idx"),
            "normal3Idx": _u16(data, base + 12, f"prisms[{index}].normal3Idx"),
            "attribute": _u16(data, base + 14, f"prisms[{index}].attribute"),
        }
        if index != 0:
            _require(
                prism["positionIdx"] < position_capacity,
                f"prism {index} position index is out of range",
            )
            for field in ("normal0Idx", "normal1Idx", "normal2Idx", "normal3Idx"):
                _require(
                    prism[field] < normal_capacity + overlay_normal_count,
                    f"prism {index} {field} is out of range",
                )
                used_normals.add(prism[field])
            attributes.add(prism["attribute"])
        prisms.append(prism)

    referenced_prisms, node_count = _walk_kcl_octree(
        data,
        blocks_offset,
        prism_count,
        mask_x,
        mask_y,
        mask_z,
        shift,
        shift_y,
        shift_z,
    )

    return {
        "format": "KCL",
        "header": {
            "header_size": header_size,
            "pPositions": positions_offset,
            "pNormals": normals_offset,
            "pPrisms": prisms_offset,
            "pBlocks": blocks_offset,
            "thickness": thickness,
            "areaMin": {"x": area_min[0], "y": area_min[1], "z": area_min[2]},
            "maskX": mask_x,
            "maskY": mask_y,
            "maskZ": mask_z,
            "shift": shift,
            "shiftY": shift_y,
            "shiftZ": shift_z,
            "sphereRadius": sphere_radius,
            "sphereRadius_overlay": "header" if header_size == KCL_HEADER_SIZE_3C else "positions[0].x",
        },
        "sections": {
            "positions": {
                "offset": positions_offset,
                "capacity": position_capacity,
                "count": 1 + max(prism["positionIdx"] for prism in prisms[1:]) if len(prisms) > 1 else 0,
                "entries": positions,
            },
            "normals": {
                "offset": normals_offset,
                "capacity": normal_capacity,
                "overlay_count": overlay_normal_count,
                "count": 1 + max(index for prism in prisms[1:] for index in (
                    prism["normal0Idx"], prism["normal1Idx"], prism["normal2Idx"], prism["normal3Idx"],
                )) if len(prisms) > 1 else 0,
                "entries": normals,
            },
            "prisms": {"offset": prisms_offset, "count": prism_count, "entries": prisms},
            "blocks": {
                "offset": blocks_offset,
                "node_count": node_count,
                "referenced_prisms": len(referenced_prisms),
            },
        },
        "used_normals": len(used_normals),
        "attributes": sorted(attributes),
    }


def _kcl_overlay_normal_count(
    prisms_offset: int,
    prism_count: int,
    normals_offset: int,
) -> int:
    """Return the number of normal slots spanning the section's 8-byte tail."""
    remainder = (prisms_offset - normals_offset) % 12
    _require(remainder in (0, 8), "normal section has an unsupported tail size")
    if remainder == 0 or prism_count == 0:
        return 0

    return 2


def _walk_kcl_octree(
    data: bytes,
    blocks_offset: int,
    prism_count: int,
    mask_x: int,
    mask_y: int,
    mask_z: int,
    shift: int,
    shift_y: int,
    shift_z: int,
) -> tuple[set[int], int]:
    """Walk root keys and child nodes, collecting every reachable prism index."""
    referenced: set[int] = set()
    visited_nodes: set[int] = set()
    visited_lists: set[int] = set()

    def read_list(list_offset: int) -> None:
        _require(list_offset % 2 == 0, "octree list offset is not 2-byte aligned")
        _require(list_offset >= blocks_offset, "octree list is outside the section")
        _require(list_offset <= len(data) - 2, "octree list start is out of bounds")
        if list_offset in visited_lists:
            return
        visited_lists.add(list_offset)

        # RPKcl::SearchSphereOctree assigns pList to the flagged target, then
        # pre-increments before each read.
        cursor = list_offset + 2
        while True:
            _require(cursor <= len(data) - 2, "octree list is not terminated")
            index = _u16(data, cursor, f"octree list at 0x{cursor:X}")
            if index == 0:
                break
            _require(index < prism_count, f"octree list prism index {index} is out of range")
            referenced.add(index)
            cursor += 2

    def walk_node(node: int, depth: int) -> None:
        _require(depth <= 32, "octree recursion is too deep")
        _require(node >= blocks_offset and node % 4 == 0, "octree node is outside the section")
        _require(node <= len(data) - 32, "octree node is out of bounds")
        _require(node not in visited_nodes, "octree node was visited more than once")
        visited_nodes.add(node)

        for child in range(8):
            value = _s32(data, node + child * 4, f"octree node 0x{node:X} child {child}")
            if value >= 0:
                _require(value != 0, "octree child offset is zero")
                _require(value % 4 == 0, "octree child offset is not 4-byte aligned")
                walk_node(node + value, depth + 1)
            else:
                leaf_offset = value & 0x7FFFFFFF
                _require(leaf_offset % 2 == 0, "octree leaf offset is not 2-byte aligned")
                read_list(node + leaf_offset)

    extent_x = (~mask_x & 0xFFFFFFFF) >> shift
    extent_y = (~mask_y & 0xFFFFFFFF) >> shift
    extent_z = (~mask_z & 0xFFFFFFFF) >> shift
    root_key_count = ((extent_z << shift_z) | (extent_y << shift_y) | extent_x) + 1
    for root_key in range(root_key_count):
        root_offset = blocks_offset + root_key * 4
        value = _s32(data, root_offset, f"octree root key {root_key}")
        if value >= 0:
            _require(value != 0, "octree root child offset is zero")
            _require(value % 4 == 0, "octree root child offset is not 4-byte aligned")
            walk_node(blocks_offset + value, 0)
        else:
            leaf_offset = value & 0x7FFFFFFF
            _require(leaf_offset % 2 == 0, "octree root leaf offset is not 2-byte aligned")
            read_list(blocks_offset + leaf_offset)

    _require(
        len(referenced) == prism_count - 1,
        "octree does not reach every non-dummy prism at least once",
    )
    return referenced, len(visited_nodes)


def parse_pmp(data: bytes) -> dict[str, Any]:
    """Decode a big-endian PMPF path table and validate its relocation slots."""
    _require(len(data) >= 0x4C, "PMP file is shorter than RPResData")
    magic = data[:4]
    _require(magic == PMP_MAGIC, f"invalid PMP magic {magic!r}")

    flags = _u16(data, 8, "flags")
    _require(flags & 1 == 0, "PMP data is already relocated")
    num_entries = _u16(data, 0x10, "numEntries")
    num_paths = _u16(data, 0x12, "numPaths")
    num_points = _u16(data, 0x14, "numPoints")
    entries_offset = _offset(data, 0x40, "pEntries")
    paths_slot = _u32(data, 0x44, "pPaths")
    points_slot = _u32(data, 0x48, "pPoints")
    _require(
        num_paths != 0 or paths_slot == len(data),
        "pPaths is not the zero-count end sentinel",
    )
    _require(paths_slot < len(data) if num_paths != 0 else True, "pPaths is outside the file")
    _require(
        num_points != 0 or points_slot == len(data),
        "pPoints is not the zero-count end sentinel",
    )
    _require(points_slot < len(data) if num_points != 0 else True, "pPoints is outside the file")
    paths_offset = paths_slot
    points_offset = points_slot

    _require(entries_offset == 0x80, "pEntries is not 0x80")
    if num_paths == 0:
        _require(paths_offset == len(data), "pPaths is not the zero-count end sentinel")
    else:
        _require(
            paths_offset == entries_offset + num_entries * 0x58,
            "pPaths does not follow the entries",
        )
    if num_points == 0:
        _require(points_offset == len(data), "pPoints is not the zero-count end sentinel")
    else:
        _require(
            points_offset == paths_offset + num_paths * 0x20,
            "pPoints does not follow the paths",
        )
    _require(points_offset + num_points * 0x14 <= len(data), "points section exceeds the file")

    entries = []
    entry_types: collections.Counter[int] = collections.Counter()
    for index in range(num_entries):
        base = entries_offset + index * 0x58
        _require(base + 0x58 <= len(data), f"entry {index} exceeds the file")
        path_slot = _u32(data, base + 0x44, f"entry {index} pPath")
        if path_slot != 0xFFFFFFFF:
            _require(path_slot < num_paths, f"entry {index} path index is out of range")
        entry = {
            "unk00": _u32(data, base, f"entry {index} unk00"),
            "type": data[base + 4],
            "pPath": None if path_slot == 0xFFFFFFFF else path_slot,
            "params": [_u16(data, base + 0x48 + i * 2, f"entry {index} params[{i}]") for i in range(8)],
        }
        entry_types[entry["type"]] += 1
        entries.append(entry)

    paths = []
    used_points: set[int] = set()
    path_types: collections.Counter[int] = collections.Counter()
    for index in range(num_paths):
        base = paths_offset + index * 0x20
        _require(base + 0x20 <= len(data), f"path {index} exceeds the file")
        path_point_count = _u16(data, base, f"path {index} numPoints")
        path_type = data[base + 4]
        path_flags = _u16(data, base + 6, f"path {index} flags")
        first_point = _u32(data, base + 0x18, f"path {index} pPoints")
        _require(first_point + path_point_count <= num_points, f"path {index} point range is out of bounds")
        _require(path_type in (0, 1), f"path {index} has unsupported type {path_type}")
        path_types[path_type] += 1
        for point_index in range(first_point, first_point + path_point_count):
            _require(point_index not in used_points, f"path {index} reuses point {point_index}")
            used_points.add(point_index)
        paths.append(
            {
                "numPoints": path_point_count,
                "unk02": _u16(data, base + 2, f"path {index} unk02"),
                "type": path_type,
                "unk05": data[base + 5],
                "flags": path_flags,
                "pPoints": first_point,
            }
        )

    points = []
    for index in range(num_points):
        base = points_offset + index * 0x14
        points.append(
            {
                "pos": {
                    "x": _f32(data, base, f"point {index}.pos.x"),
                    "y": _f32(data, base + 4, f"point {index}.pos.y"),
                    "z": _f32(data, base + 8, f"point {index}.pos.z"),
                },
                "param0C": _u16(data, base + 0x0C, f"point {index}.param0C"),
                "param0E": _u16(data, base + 0x0E, f"point {index}.param0E"),
                "param10": _u16(data, base + 0x10, f"point {index}.param10"),
                "param12": _u16(data, base + 0x12, f"point {index}.param12"),
            }
        )

    return {
        "format": "PMP",
        "header": {
            "magic": magic.decode("ascii"),
            "flags": flags,
            "numEntries": num_entries,
            "numPaths": num_paths,
            "numPoints": num_points,
            "pEntries": entries_offset,
            "pPaths": paths_offset,
            "pPoints": points_offset,
        },
        "sections": {
            "entries": {"offset": entries_offset, "count": num_entries, "entries": entries},
            "paths": {"offset": paths_offset, "count": num_paths, "entries": paths},
            "points": {"offset": points_offset, "count": num_points, "entries": points},
        },
        "entry_types": dict(entry_types),
        "path_types": dict(path_types),
    }


def parse(data: bytes) -> dict[str, Any]:
    """Parse a KCL or PMP resource by its magic/header marker."""
    if data[:4] == PMP_MAGIC:
        return parse_pmp(data)
    if _is_kcl(data):
        return parse_kcl(data)
    raise FormatError("data is neither KCL nor PMP")


def _matching_files(root: str) -> Iterator[tuple[str, bytes]]:
    for path, data in wiiarc.walk(root):
        if data[:4] == PMP_MAGIC or _is_kcl(data):
            yield path, data


def check(root: str) -> int:
    """Parse every matching disc file and print one summary line per format."""
    stats = {
        "KCL": {
            "files": 0,
            "errors": 0,
            "versions": collections.Counter(),
            "positions": 0,
            "normals": 0,
            "prisms": 0,
            "nodes": 0,
        },
        "PMP": {
            "files": 0,
            "errors": 0,
            "versions": collections.Counter(),
            "entries": 0,
            "paths": 0,
            "points": 0,
        },
    }
    first_errors: dict[str, tuple[str, str]] = {}

    for path, data in _matching_files(root):
        format_name = "PMP" if data[:4] == PMP_MAGIC else "KCL"
        stats[format_name]["files"] += 1
        try:
            parsed = parse(data)
        except (FormatError, struct.error, ValueError) as error:
            stats[format_name]["errors"] += 1
            first_errors.setdefault(format_name, (path, str(error)))
            continue

        if format_name == "KCL":
            stats["KCL"]["versions"][parsed["header"]["header_size"]] += 1
            stats["KCL"]["positions"] += parsed["sections"]["positions"]["count"]
            stats["KCL"]["normals"] += parsed["sections"]["normals"]["count"]
            stats["KCL"]["prisms"] += parsed["sections"]["prisms"]["count"]
            stats["KCL"]["nodes"] += parsed["sections"]["blocks"]["node_count"]
        else:
            stats["PMP"]["versions"][0] += 1
            stats["PMP"]["entries"] += parsed["header"]["numEntries"]
            stats["PMP"]["paths"] += parsed["header"]["numPaths"]
            stats["PMP"]["points"] += parsed["header"]["numPoints"]

    versions_kcl = ",".join(f"0x{version:X}" for version in sorted(stats["KCL"]["versions"]))
    versions_pmp = ",".join(str(version) for version in sorted(stats["PMP"]["versions"]))
    print(
        f"KCL: files={stats['KCL']['files']} errors={stats['KCL']['errors']} "
        f"versions={versions_kcl} positions={stats['KCL']['positions']} "
        f"normals={stats['KCL']['normals']} prisms={stats['KCL']['prisms']} "
        f"octree_nodes={stats['KCL']['nodes']}"
    )
    print(
        f"PMP: files={stats['PMP']['files']} errors={stats['PMP']['errors']} "
        f"versions={versions_pmp} entries={stats['PMP']['entries']} "
        f"paths={stats['PMP']['paths']} points={stats['PMP']['points']}"
    )

    for format_name in ("KCL", "PMP"):
        if format_name in first_errors:
            path, message = first_errors[format_name]
            print(f"{format_name} first_error: {path}: {message}")
    return 1 if any(value["errors"] for value in stats.values()) else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Wii Play KCL and PMP collision files")
    parser.add_argument("--check", metavar="DISC_ROOT", help="parse every matching file under DISC_ROOT")
    args = parser.parse_args()
    if args.check is None:
        parser.error("--check is required")
    root = os.path.expanduser(args.check)
    if not os.path.isdir(root):
        parser.error(f"{root} is not a directory")
    return check(root)


if __name__ == "__main__":
    raise SystemExit(main())
