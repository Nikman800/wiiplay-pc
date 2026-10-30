#!/usr/bin/env python3
"""Bounds-checked parser and whole-disc checker for BRRES animation blocks (CHR0, CLR0, SRT0,
PAT0, SCN0).

    fmt_brres_anim.py --check ROOT     parse every BRRES under ROOT (descends into archives)
    fmt_brres_anim.py --json FILE      print parse() of one decompressed BRRES file as JSON

Every layout and offset base below follows the matching decompiled NW4R code (paths relative to
the decompilation: include/nw4r/g3d/res/, src/nw4r/g3d/res/). General rules found there:

* ResDic (g3d_resdict.h): u32 size, u32 numData, then numData + 1 nodes of 16 bytes
  (u16 ref, u16 flag, u16 idxLeft, u16 idxRight, s32 ofsString, s32 ofsData). Both offsets are
  relative to the start of the dictionary; node 0 is the root of the Patricia tree.
* Every block-level "to..." field is relative to the start of the block (ResCommon::ofs_to_ptr,
  g3d_rescommon.h); 0 means NULL.
* A ResName offset points at the string itself; its u32 length sits 4 bytes before it
  (NW4R_G3D_OFS_TO_RESNAME, g3d_rescommon.h:24).
* Per-node/per-material data offsets are relative to the node/material record
  (ut::AddOffsetToPtr(pNodeData/pMatData, ...)), except ResAnmData / ResColorAnmData /
  ResBoolAnmData words, which are relative to the word itself (g3d_resanm.h), and arrays of name
  offsets (PAT0 texture/palette names, light-set light names), which are relative to the array start.
"""

import argparse
import collections
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wiiarc import walk  # noqa: E402


FORMATS = ("CHR0", "CLR0", "SRT0", "PAT0", "SCN0")
GROUP_NAMES = {
    "AnmChr(NW4R)": "CHR0",
    "AnmClr(NW4R)": "CLR0",
    "AnmTexSrt(NW4R)": "SRT0",
    "AnmTexPat(NW4R)": "PAT0",
    "AnmScn(NW4R)": "SCN0",
}
# ResAnm*::REVISION in the headers.
REVISIONS = {"CHR0": 4, "CLR0": 3, "SRT0": 4, "PAT0": 3, "SCN0": 4}


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise FormatError(message)


class Reader:
    """Big-endian, bounds-checked reads from one decompressed BRRES file."""

    def __init__(self, data):
        self.data = data

    def check(self, offset, size, what):
        require(0 <= offset and offset + size <= len(self.data),
                f"{what} at 0x{offset:X} (+{size}) is out of bounds")

    def unpack(self, offset, fmt, what):
        fmt = ">" + fmt
        self.check(offset, struct.calcsize(fmt), what)
        return struct.unpack_from(fmt, self.data, offset)

    def u32(self, offset, what):
        return self.unpack(offset, "I", what)[0]

    def s32(self, offset, what):
        return self.unpack(offset, "i", what)[0]

    def name(self, offset, what):
        """ResName whose string starts at offset (u32 length at offset - 4)."""
        length = self.u32(offset - 4, f"{what} length")
        self.check(offset, length + 1, f"{what} string")
        raw = self.data[offset:offset + length]
        require(self.data[offset + length] == 0 and b"\0" not in raw,
                f"{what} at 0x{offset:X}: length {length} does not match a NUL-terminated string")
        try:
            return raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise FormatError(f"{what} at 0x{offset:X} is not ASCII") from exc

    def name_at(self, base, field, what):
        """Name addressed by the s32 at field, relative to base; None when the offset is 0."""
        value = self.s32(field, f"{what} offset")
        return None if value == 0 else self.name(base + value, what)

    def dictionary(self, offset, what):
        """ResDic -> list of (name, absolute data offset) in node order (g3d_resdict.h)."""
        size, count = self.unpack(offset, "II", f"{what} dictionary header")
        require(size == 8 + 16 * (count + 1),
                f"{what} dictionary at 0x{offset:X} has size {size} for {count} entries")
        self.check(offset, size, f"{what} dictionary")
        entries = []
        for index in range(count + 1):
            _ref, _flag, left, right, ofs_string, ofs_data = self.unpack(
                offset + 8 + 16 * index, "HHHHii", f"{what} dictionary node")
            require(left <= count and right <= count, f"{what} dictionary node {index} index out of range")
            if index == 0:
                continue
            require(ofs_string != 0 and ofs_data != 0, f"{what} dictionary node {index} has a zero offset")
            data_offset = offset + ofs_data
            self.check(data_offset, 0, f"{what} dictionary node {index} data")
            entries.append((self.name(offset + ofs_string, f"{what} dictionary name"), data_offset))
        return entries


# ---------------------------------------------------------------------------------------------
# Shared value encodings (g3d_resanm.h, g3d_resanm.cpp)

def keyframes(r, offset, what):
    """ResKeyFrameAnmData: u16 numKeyFrame, pad, f32 invKeyFrameRange, {f32 frame, value, slope}[n]."""
    count = r.unpack(offset, "H", f"{what} numKeyFrame")[0]
    inv_range = r.unpack(offset + 4, "f", f"{what} invKeyFrameRange")[0]
    # GetResKeyFrameAnmResult reads keyFrames[0] and keyFrames[n - 1] unconditionally.
    require(count >= 1, f"{what} at 0x{offset:X} has no keyframes")
    r.check(offset + 8, 12 * count, f"{what} keyframes")
    frames = [r.unpack(offset + 8 + 12 * i, "f", f"{what} frame")[0] for i in range(count)]
    require(all(math.isfinite(f) for f in frames), f"{what} at 0x{offset:X} has a non-finite frame")
    # invKeyFrameRange = 1 / (last - first); it is +inf when all keys share one frame, which is
    # harmless because the search returns before using it (frame <= first or last <= frame).
    require(math.isfinite(inv_range) or frames[0] == frames[-1],
            f"{what} at 0x{offset:X} has a non-finite invKeyFrameRange")
    # The estimate-then-walk search needs nondecreasing frames.
    require(frames == sorted(frames), f"{what} at 0x{offset:X} frames are not sorted")
    return count


def anm_value(r, word, constant, what):
    """ResAnmData: constant f32, or s32 offset (relative to the word) to ResKeyFrameAnmData.
    Returns the number of keyframes (0 for a constant)."""
    if constant:
        r.check(word, 4, what)
        return 0
    return keyframes(r, word + r.s32(word, f"{what} offset"), what)


def color_value(r, word, constant, num_frame, what):
    """ResColorAnmData: constant RGBA u32, or s32 offset (relative to the word) to numFrame + 1
    RGBA u32 (GetResColorAnmResult reads [frame] and, between frames, [frame + 1])."""
    if constant:
        r.check(word, 4, what)
        return
    r.check(word + r.s32(word, f"{what} offset"), 4 * (num_frame + 1), f"{what} colors")


def bool_value(r, word, num_frame, what):
    """ResBoolAnmData: s32 offset (relative to the word) to one bit per frame, MSB first."""
    r.check(word + r.s32(word, f"{what} offset"), 4 * (num_frame // 32 + 1), f"{what} bits")


def block_header(r, block, kind, name_field, path_field):
    """ut::BinaryBlockHeader + u32 revision; name/original_path are relative to the block."""
    magic, size, revision = r.unpack(block, "4sII", f"{kind} header")
    require(magic == kind.encode("ascii"), f"expected {kind}, found {magic!r}")
    r.check(block, size, f"{kind} block")
    require(revision == REVISIONS[kind], f"{kind} revision {revision}, expected {REVISIONS[kind]}")
    return {
        "kind": kind,
        "offset": block,
        "size": size,
        "revision": revision,
        "name": r.name_at(block, block + name_field, f"{kind} name"),
        "originalPath": r.name_at(block, block + path_field, f"{kind} original_path"),
    }


def check_policy(value, kind):
    require(value in (0, 1), f"{kind} has invalid AnmPolicy {value}")


def record_dictionary(r, block, kind, field, count):
    entries = r.dictionary(block + r.s32(block + field, f"{kind} dictionary offset"), kind)
    require(len(entries) == count, f"{kind} dictionary has {len(entries)} entries, info says {count}")
    return entries


def record_name(r, record, dict_name, what):
    """Node/material records start with s32 name relative to the record (g3d_anmchr.cpp:643)."""
    name = r.name_at(record, record, what)
    require(name == dict_name, f"{what} name {name!r} differs from dictionary name {dict_name!r}")
    return name


# ---------------------------------------------------------------------------------------------
# CHR0 (g3d_resanmchr.h, g3d_resanmchr.cpp)

CHR_SCALE_UNIFORM = 1 << 4
CHR_HAS_SCALE, CHR_HAS_ROT, CHR_HAS_TRANS = 1 << 22, 1 << 23, 1 << 24
CHR_SCALE_CONST = (1 << 13, 1 << 14, 1 << 15)
CHR_ROT_CONST = (1 << 16, 1 << 17, 1 << 18)
CHR_TRANS_CONST = (1 << 19, 1 << 20, 1 << 21)
# Format fields of ResAnmChrNodeData::flags: scale bits 25-26, rotation bits 27-29, translation
# bits 30-31. Code 0 means every component is an inline constant (GetAnm* "case 0").
CHR_FVS = {1: "FVS32", 2: "FVS48", 3: "FVS96"}
CHR_ROT = {1: "FVS32", 2: "FVS48", 3: "FVS96", 4: "CV8", 5: "CV16", 6: "CV32"}
FVS_STRIDE = {"FVS32": 4, "FVS48": 6, "FVS96": 12}
CV_STRIDE = {"CV8": 1, "CV16": 2, "CV32": 4}


def chr_track(r, node, word, kind, constant, num_frame, what):
    """One component. The AnmData word is a constant f32, or an offset from the NODE record to
    the data (CalcResultFVS / CalcResultCV). Returns the number of stored values."""
    if kind is None or constant:
        r.check(word, 4, what)
        return 0
    data = node + r.s32(word, f"{what} offset")
    if kind in FVS_STRIDE:
        # ResAnmChrFVSData: u16 numFrameValues, pad, f32 invKeyFrameRange, then (FVS32/48 only)
        # f32 scale + f32 offset, then the frame values.
        count = r.unpack(data, "H", f"{what} numFrameValues")[0]
        require(count >= 1, f"{what} at 0x{data:X} has no frame values")
        r.check(data + (8 if kind == "FVS96" else 16), FVS_STRIDE[kind] * count, f"{what} {kind} values")
        return count
    # ResAnmChrCVData: CV8/CV16 start with f32 scale + f32 offset; CalcAnimationCV reads
    # values[0..numFrame].
    r.check(data + (0 if kind == "CV32" else 8), CV_STRIDE[kind] * (num_frame + 1), f"{what} {kind} values")
    return num_frame + 1


def parse_chr(r, block):
    head = block_header(r, block, "CHR0", 0x14, 0x18)
    num_frame, num_node, policy, scaling_rule = r.unpack(block + 0x1C, "HHII", "CHR0 info")
    check_policy(policy, "CHR0")
    require(scaling_rule <= 2, f"CHR0 has invalid ScalingRule {scaling_rule}")
    nodes = []
    for dict_name, node in record_dictionary(r, block, "CHR0", 0x10, num_node):
        what = f"CHR0 node {dict_name!r}"
        name = record_name(r, node, dict_name, what)
        flags = r.u32(node + 4, f"{what} flags")
        # anms[] holds scale (1 word if uniform, else 3), rotation (3), translation (3), each
        # group present only when its FLAG_HAS_* bit is set (GetAnmResult dispatch table).
        groups = []
        if flags & CHR_HAS_SCALE:
            axes = 1 if flags & CHR_SCALE_UNIFORM else 3
            groups.append(("scale", CHR_FVS.get((flags >> 25) & 3), CHR_SCALE_CONST[:axes]))
        if flags & CHR_HAS_ROT:
            code = (flags >> 27) & 7
            require(code == 0 or code in CHR_ROT, f"{what} has invalid rotation format {code}")
            groups.append(("rotate", CHR_ROT.get(code), CHR_ROT_CONST))
        if flags & CHR_HAS_TRANS:
            groups.append(("translate", CHR_FVS.get((flags >> 30) & 3), CHR_TRANS_CONST))
        word = node + 8
        tracks = animated = 0
        for label, kind, const_bits in groups:
            for axis, bit in zip("xyz", const_bits):
                animated += chr_track(r, node, word, kind, flags & bit, num_frame, f"{what} {label}.{axis}") > 0
                tracks += 1
                word += 4
        nodes.append({"name": name, "flags": flags, "tracks": tracks, "animatedTracks": animated})
    head.update(numFrame=num_frame, policy=policy, scalingRule=scaling_rule, nodes=nodes)
    head["records"] = {"nodes": len(nodes), "animatedTracks": sum(n["animatedTracks"] for n in nodes)}
    return head


# ---------------------------------------------------------------------------------------------
# CLR0 (g3d_resanmclr.h, g3d_resanmclr.cpp)

CLR_TARGETS = ("CLR0", "CLR1", "AMB0", "AMB1", "TEV0", "TEV1", "TEV2", "TEVK0", "TEVK1", "TEVK2", "TEVK3")


def parse_clr(r, block):
    head = block_header(r, block, "CLR0", 0x14, 0x18)
    num_frame, num_material, policy = r.unpack(block + 0x1C, "HHI", "CLR0 info")
    check_policy(policy, "CLR0")
    materials = []
    for dict_name, mat in record_dictionary(r, block, "CLR0", 0x10, num_material):
        what = f"CLR0 material {dict_name!r}"
        name = record_name(r, mat, dict_name, what)
        flags = r.u32(mat + 4, f"{what} flags")
        # Two bits per target (ANM_EXISTS, ANM_CONSTANT); anms[] packs only existing targets.
        require(flags >> (2 * len(CLR_TARGETS)) == 0, f"{what} has flags beyond CLA_MAX")
        targets = []
        entry = mat + 8  # ResAnmClrAnmData: u32 mask, ResColorAnmData color
        for i, target in enumerate(CLR_TARGETS):
            bits = flags >> (2 * i)
            if bits & 1:
                r.check(entry, 4, f"{what} {target} mask")
                color_value(r, entry + 4, bits & 2, num_frame, f"{what} {target}")
                targets.append(target)
                entry += 8
        materials.append({"name": name, "flags": flags, "targets": targets})
    head.update(numFrame=num_frame, policy=policy, materials=materials)
    head["records"] = {"materials": len(materials), "colorTracks": sum(len(m["targets"]) for m in materials)}
    return head


# ---------------------------------------------------------------------------------------------
# SRT0 (g3d_resanmtexsrt.h, g3d_resanmtexsrt.cpp)

SRT_SCALE_ONE, SRT_ROT_ZERO, SRT_TRANS_ZERO, SRT_SCALE_UNIFORM = 1 << 1, 1 << 2, 1 << 3, 1 << 4
SRT_SU_CONST, SRT_SV_CONST, SRT_R_CONST, SRT_TU_CONST, SRT_TV_CONST = (1 << b for b in range(5, 10))


def srt_texture(r, tex, what):
    """ResAnmTexSrtTexData: u32 flags, then ResAnmData words in MakeResult order."""
    flags = r.u32(tex, f"{what} flags")
    tracks = []
    if not flags & SRT_SCALE_ONE:
        tracks.append(("Su", SRT_SU_CONST))
        if not flags & SRT_SCALE_UNIFORM:
            tracks.append(("Sv", SRT_SV_CONST))
    if not flags & SRT_ROT_ZERO:
        tracks.append(("R", SRT_R_CONST))
    if not flags & SRT_TRANS_ZERO:
        tracks += [("Tu", SRT_TU_CONST), ("Tv", SRT_TV_CONST)]
    for i, (label, bit) in enumerate(tracks):
        anm_value(r, tex + 4 + 4 * i, flags & bit, f"{what} {label}")
    return len(tracks)


def parse_srt(r, block):
    head = block_header(r, block, "SRT0", 0x14, 0x18)
    num_frame, num_material, mtx_mode, policy = r.unpack(block + 0x1C, "HHII", "SRT0 info")
    check_policy(policy, "SRT0")
    require(mtx_mode <= 2, f"SRT0 has invalid TexMatrixMode {mtx_mode}")
    materials = []
    for dict_name, mat in record_dictionary(r, block, "SRT0", 0x10, num_material):
        what = f"SRT0 material {dict_name!r}"
        name = record_name(r, mat, dict_name, what)
        flags, ind_flags = r.unpack(mat + 4, "II", f"{what} flags")
        # One bit per texture matrix: 8 material matrices, then 3 indirect matrices.
        require(flags >> 8 == 0 and ind_flags >> 3 == 0, f"{what} animates a nonexistent matrix")
        slots = [f"tex{i}" for i in range(8) if flags >> i & 1]
        slots += [f"ind{i}" for i in range(3) if ind_flags >> i & 1]
        tracks = 0
        for i, slot in enumerate(slots):
            # toResAnmTexSrtTexData[] entries are relative to the material record.
            tex = mat + r.s32(mat + 12 + 4 * i, f"{what} {slot} offset")
            tracks += srt_texture(r, tex, f"{what} {slot}")
        materials.append({"name": name, "flags": flags, "indFlags": ind_flags, "matrices": slots, "tracks": tracks})
    head.update(numFrame=num_frame, policy=policy, texMtxMode=mtx_mode, materials=materials)
    head["records"] = {"materials": len(materials), "texMatrices": sum(len(m["matrices"]) for m in materials)}
    return head


# ---------------------------------------------------------------------------------------------
# PAT0 (g3d_resanmtexpat.h, g3d_resanmtexpat.cpp)

def name_array(r, block, field, count, what):
    """s32 array (located by block + field) of name offsets relative to the ARRAY start
    (GetResNameFromOffsetArray)."""
    if count == 0:
        return []
    array = block + r.s32(block + field, f"{what} array offset")
    return [r.name_at(array, array + 4 * i, f"{what} {i}") for i in range(count)]


def parse_pat(r, block):
    head = block_header(r, block, "PAT0", 0x24, 0x28)
    num_frame, num_material, num_texture, num_palette, policy = r.unpack(block + 0x2C, "HHHHI", "PAT0 info")
    check_policy(policy, "PAT0")
    textures = name_array(r, block, 0x14, num_texture, "PAT0 texture name")
    palettes = name_array(r, block, 0x18, num_palette, "PAT0 palette name")
    # ResTex / ResPltt arrays hold runtime pointers filled by Bind(); only their extent is checked.
    for field, count, what in ((0x1C, num_texture, "ResTex"), (0x20, num_palette, "ResPltt")):
        if count:
            r.check(block + r.s32(block + field, f"PAT0 {what} array offset"), 4 * count, f"PAT0 {what} array")
    materials = []
    for dict_name, mat in record_dictionary(r, block, "PAT0", 0x10, num_material):
        what = f"PAT0 material {dict_name!r}"
        name = record_name(r, mat, dict_name, what)
        flags = r.u32(mat + 4, f"{what} flags")
        # Four bits per texture slot (ANM_EXISTS, ANM_CONST, ANM_TEX, ANM_PLTT).
        word = mat + 8
        slots = []
        for i in range(8):
            bits = flags >> (4 * i)
            if not bits & 1:
                continue
            if bits & 2:  # constant: u16 texIndex, u16 plttIndex inline
                pairs = [r.unpack(word, "HH", f"{what} slot {i} constant")]
            else:
                # ResAnmTexPatAnmData, relative to the material record (AddOffsetToPtr(pMatData, ...)).
                data = mat + r.s32(word, f"{what} slot {i} offset")
                count = r.unpack(data, "H", f"{what} slot {i} numFrameValues")[0]
                require(count >= 1, f"{what} slot {i} has no frame values")
                r.check(data + 8, 8 * count, f"{what} slot {i} frame values")
                frames = [r.unpack(data + 8 + 8 * k, "fHH", f"{what} frame") for k in range(count)]
                times = [f[0] for f in frames]
                require(times == sorted(times), f"{what} slot {i} frames are not sorted")
                pairs = [(tex, pltt) for _time, tex, pltt in frames]
            for tex, pltt in pairs:
                require(not bits & 4 or tex < num_texture, f"{what} slot {i} texture index {tex} >= {num_texture}")
                require(not bits & 8 or pltt < num_palette, f"{what} slot {i} palette index {pltt} >= {num_palette}")
            slots.append(i)
            word += 4
        materials.append({"name": name, "flags": flags, "slots": slots})
    head.update(numFrame=num_frame, policy=policy, textures=textures, palettes=palettes, materials=materials)
    head["records"] = {"materials": len(materials), "textures": num_texture, "palettes": num_palette}
    return head


# ---------------------------------------------------------------------------------------------
# SCN0 (g3d_resanmscn.h/.cpp, g3d_reslightset.h, g3d_resanm{amblight,light,fog,camera}.h/.cpp)

def scn_light_set(r, rec, num_frame, what):
    """ResLightSetData: ambLightName relative to the record, lightNames[8] relative to the
    lightNames array start (ResLightSet::GetAmbLightResName / GetLightResName)."""
    r.name_at(rec, rec + 0x14, f"{what} ambLightName")
    num_light = r.unpack(rec + 0x1A, "B", f"{what} numLight")[0]
    require(num_light <= 8, f"{what} numLight {num_light} > 8")
    for i in range(num_light):
        r.name_at(rec + 0x1C, rec + 0x1C + 4 * i, f"{what} light name {i}")


def scn_amb_light(r, rec, num_frame, what):
    flags = r.u32(rec + 0x14, f"{what} flags")
    color_value(r, rec + 0x18, flags & (1 << 31), num_frame, f"{what} color")


def scn_light(r, rec, num_frame, what):
    """Checks every field ResAnmLight::GetAnmResult can read for this light's flags."""
    flags = r.u32(rec + 0x1C, f"{what} flags")
    light_type = flags & 3
    require(light_type <= 2, f"{what} has invalid light type {light_type}")
    enable_const = flags & (1 << 23)
    if not enable_const:
        bool_value(r, rec + 0x20, num_frame, f"{what} enable")
    elif not flags & (1 << 2):
        return  # constantly disabled: nothing else is read
    specular = flags & (1 << 3)
    fields = [(0x24, 19, "posX"), (0x28, 20, "posY"), (0x2C, 21, "posZ")]
    if light_type != 0 or specular:  # hasAim
        fields += [(0x34, 24, "aimX"), (0x38, 25, "aimY"), (0x3C, 26, "aimZ")]
    if light_type in (0, 2):  # point, spot
        fields += [(0x44, 28, "refDistance"), (0x48, 29, "refBrightness")]
    if light_type == 2:
        fields.append((0x50, 27, "cutoff"))
    if specular:
        fields.append((0x58, 31, "shininess"))
        color_value(r, rec + 0x54, flags & (1 << 30), num_frame, f"{what} specColor")
    color_value(r, rec + 0x30, flags & (1 << 22), num_frame, f"{what} color")
    for offset, bit, label in fields:
        anm_value(r, rec + offset, flags & (1 << bit), f"{what} {label}")


def scn_fog(r, rec, num_frame, what):
    # The fog reader is dead-stripped from Wii Play; layout and const bits from g3d_resanmfog.h.
    flags = r.u32(rec + 0x14, f"{what} flags")
    anm_value(r, rec + 0x1C, flags & (1 << 29), f"{what} startz")
    anm_value(r, rec + 0x20, flags & (1 << 30), f"{what} endz")
    color_value(r, rec + 0x24, flags & (1 << 31), num_frame, f"{what} color")


def scn_camera(r, rec, num_frame, what):
    proj_type, flags = r.unpack(rec + 0x14, "II", f"{what} projType/flags")
    require(proj_type <= 1, f"{what} has invalid projType {proj_type}")
    fields = [(0x20, 17, "posX"), (0x24, 18, "posY"), (0x28, 19, "posZ"),
              (0x2C, 20, "aspect"), (0x30, 21, "near"), (0x34, 22, "far")]
    if flags & 1:  # CAMERATYPE_AIM
        fields += [(0x44, 25, "aimX"), (0x48, 26, "aimY"), (0x4C, 27, "aimZ"), (0x50, 28, "twist")]
    else:  # CAMERATYPE_ROTATE
        fields += [(0x38, 29, "rotX"), (0x3C, 30, "rotY"), (0x40, 31, "rotZ")]
    fields.append((0x54, 23, "perspFovy") if proj_type == 0 else (0x58, 24, "orthoHeight"))
    for offset, bit, label in fields:
        anm_value(r, rec + offset, flags & (1 << bit), f"{what} {label}")


# kind: (array offset field in ResAnmScnData, index of the count in the unpacked info,
#        sizeof record, validator). Records start with u32 size, s32 toResAnmScnData,
#        s32 name (relative to the record), u32 id, u32 refNumber.
SCN_KINDS = {
    "lightSets": (0x14, 3, 0x4C, scn_light_set),
    "ambLights": (0x18, 4, 0x1C, scn_amb_light),
    "lights": (0x1C, 5, 0x5C, scn_light),
    "fogs": (0x20, 6, 0x28, scn_fog),
    "cameras": (0x24, 7, 0x5C, scn_camera),
}


def parse_scn(r, block):
    head = block_header(r, block, "SCN0", 0x28, 0x2C)
    info = r.unpack(block + 0x30, "HHIHHHHH", "SCN0 info")
    num_frame, num_specular, policy = info[:3]
    check_policy(policy, "SCN0")
    names = {}
    record_starts = set()
    for kind, (field, count_index, size, validate) in SCN_KINDS.items():
        count = info[count_index]
        names[kind] = []
        if count == 0:
            continue
        array = block + r.s32(block + field, f"SCN0 {kind} array offset")
        for i in range(count):
            rec = array + size * i
            what = f"SCN0 {kind}[{i}]"
            rec_size, to_scn, _name, rec_id = r.unpack(rec, "IiiI", what)
            require(rec_size == size, f"{what} size {rec_size}, expected 0x{size:X}")
            require(rec + to_scn == block, f"{what} toResAnmScnData does not point at the block")
            require(rec_id < count, f"{what} id {rec_id} >= {count}")
            names[kind].append(r.name_at(rec, rec + 8, f"{what} name"))
            validate(r, rec, num_frame, what)
            record_starts.add(rec)
    # Top-level dictionary ("LightSet(NW4R)", "AmbLights(NW4R)", "Lights(NW4R)", ...) of
    # sub-dictionaries whose entries point at the records above.
    groups = {}
    for group_name, sub in r.dictionary(block + r.s32(block + 0x10, "SCN0 dictionary offset"), "SCN0"):
        entries = r.dictionary(sub, f"SCN0 {group_name}")
        for entry_name, rec in entries:
            require(rec in record_starts, f"SCN0 {group_name} entry {entry_name!r} does not point at a record")
        groups[group_name] = len(entries)
    head.update(numFrame=num_frame, policy=policy, numSpecularLight=num_specular, groups=groups, names=names)
    head["records"] = {kind: len(values) for kind, values in names.items()}
    return head


PARSERS = {"CHR0": parse_chr, "CLR0": parse_clr, "SRT0": parse_srt, "PAT0": parse_pat, "SCN0": parse_scn}


# ---------------------------------------------------------------------------------------------
# BRRES container (g3d_resfile.h; same traversal as fmt_brres_model.py)

def animation_blocks(r):
    """Yield (format, dictionary name, block offset) for every animation block in the BRRES."""
    magic, bom, _version, file_size, header_size, _blocks = r.unpack(0, "4sHHIHH", "BRRES header")
    require(magic == b"bres" and bom == 0xFEFF, "not a big-endian BRRES")
    require(file_size == len(r.data), f"BRRES fileSize {file_size} != input length {len(r.data)}")
    root_magic, root_size = r.unpack(header_size, "4sI", "root block")
    require(root_magic == b"root", "BRRES root block is missing")
    r.check(header_size, root_size, "root block")
    for group_name, group in r.dictionary(header_size + 8, "BRRES root"):
        kind = GROUP_NAMES.get(group_name)
        if kind is not None:
            for name, block in r.dictionary(group, group_name):
                yield kind, name, block


def parse_block(r, kind, name, block):
    parsed = PARSERS[kind](r, block)
    require(parsed["name"] == name, f"header name {parsed['name']!r} differs from dictionary name")
    return parsed


def parse(data):
    """Parse every CHR0/CLR0/SRT0/PAT0/SCN0 block of one decompressed BRRES file."""
    r = Reader(data)
    return [parse_block(r, kind, name, block) for kind, name, block in animation_blocks(r)]


def check(root):
    stats = {k: collections.Counter() for k in FORMATS}
    versions = {k: collections.Counter() for k in FORMATS}
    brres = collections.Counter()
    for path, data in walk(root):
        if data[:4] != b"bres":
            continue
        brres["files"] += 1
        r = Reader(data)
        try:
            found = list(animation_blocks(r))
        except (FormatError, struct.error) as exc:
            brres["errors"] += 1
            print(f"error: {path}: BRRES: {exc}", file=sys.stderr)
            continue
        brres["withAnimations"] += bool(found)
        for kind, name, block in found:
            stat = stats[kind]
            stat["blocks"] += 1
            try:
                parsed = parse_block(r, kind, name, block)
            except (FormatError, struct.error) as exc:
                stat["errors"] += 1
                print(f"error: {path}: {kind} {name!r}: {exc}", file=sys.stderr)
                continue
            versions[kind][parsed["revision"]] += 1
            stat["frames"] += parsed["numFrame"]
            stat.update(parsed["records"])
    print(f"BRRES: files={brres['files']} errors={brres['errors']} withAnimations={brres['withAnimations']}")
    for kind in FORMATS:
        stat = stats[kind]
        version = ",".join(f"v{v}={c}" for v, c in sorted(versions[kind].items())) or "none"
        extra = " ".join(f"{key}={value}" for key, value in stat.items() if key not in ("blocks", "errors", "frames"))
        print(f"{kind}: blocks={stat['blocks']} errors={stat['errors']} versions={version} "
              f"totalFrames={stat['frames']} {extra}".rstrip())
    return 1 if brres["errors"] or any(s["errors"] for s in stats.values()) else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", metavar="ROOT", help="parse every BRRES under ROOT")
    parser.add_argument("--json", metavar="FILE", help="print parse() of one decompressed BRRES as JSON")
    args = parser.parse_args(argv)
    if args.json:
        import json
        with open(args.json, "rb") as stream:
            print(json.dumps(parse(stream.read()), indent=1))
        return 0
    if args.check:
        return check(args.check)
    parser.error("use --check ROOT or --json FILE")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
