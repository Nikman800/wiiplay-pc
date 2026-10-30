#!/usr/bin/env python3
"""Decode and validate Wii Play's big-endian EGG settings and game tables."""

from __future__ import annotations

import argparse
import collections
import math
import re
import struct
import sys
from pathlib import Path
from typing import Any

from wiiarc import walk


class FormatError(ValueError):
    """A bounds, size, version, magic, or value validation failure."""


# The field names below follow the decompiled structures. Each parser's comment
# cites the header that defines that structure and its loader.
EGG_FORMATS = {
    b"LGHT": "LightManager",
    b"LMAP": "LightTextureManager",
    b"PBLM": "DrawPathBloom",
    b"MMSC": "RPGrpModelMisc",
}

EGG_EXTENSIONS = {
    ".plight": b"LGHT",
    ".plmap": b"LMAP",
    ".pblm": b"PBLM",
    ".pmmisc": b"MMSC",
}

DUC_PARAM_FIELDS = {
    # Fields and order follow the decompiled structures:
    # RPDucBalloonParam/Pattern, include/Pack/RPParty/RPDucScene/RPDucBalloon.h:20-44;
    # RPDucCanParam/Pattern, include/Pack/RPParty/RPDucScene/RPDucCan.h:21-54;
    # RPDucCircleTargetParam/Pattern, include/Pack/RPParty/RPDucScene/RPDucCircleTarget.h:19-50;
    # RPDucClayParam/Pattern, include/Pack/RPParty/RPDucScene/RPDucClay.h:31-61;
    # RPDucDuckParam/Pattern/Motion, include/Pack/RPParty/RPDucScene/RPDucDuck.h:24-78;
    # RPDucUfoParam/Pattern, include/Pack/RPParty/RPDucScene/RPDucUfo.h:23-80.
    "DucBalloonParam": (
        ("mDrag", "f32"), ("mPoints", "s32"), ("mBaseRadius", "f32"), ("unk0C", "f32"),
        ("unk10", "f32"), ("unk14", "f32"), ("unk18", "f32"), ("mStringLength", "f32"),
        ("mSwayStep", "s32"), ("mSwayAmp", "f32"),
        *((f"mPatterns[{pattern}].{field}", kind) for pattern in range(8)
          for field, kind in (
              ("mHitScale", "f32"), ("mRise", "f32"), ("mDriftX", "f32"), ("mHitRadius", "f32")
          )),
        *((f"mStringSegs[{index}]", "f32") for index in range(5)),
    ),
    "DucCanParam": (
        ("mDrag", "f32"), ("mHitPush", "f32"), ("unk08", "s32"), ("unk0C", "s32"),
        ("mCooldown", "s32"), ("mBaseRadius", "f32"), ("mEdgeScreenY", "f32"),
        ("mSpinDecay", "f32"),
        *((f"mPatterns[{pattern}].{field}", kind) for pattern in range(8)
          for field, kind in (
              ("mAimRangeX", "f32"), ("mAimRangeY", "f32"), ("mAimY", "f32"), ("mAimZ", "f32"),
              ("mHitScale", "f32"), ("mSpinMax", "f32"), ("mSpinMin", "f32"), ("unk1C", "f32"),
              ("mBounceVelZ", "f32"), ("mMaxHits", "s32"), ("mGravityUp", "f32"),
              ("mSpeed", "f32"), ("mGravityDown", "f32"),
              *((f"mBounceHeight[{index}]", "f32") for index in range(4)),
              *((f"mBounceFrames[{index}]", "s32") for index in range(4)),
              ("mHitRadius", "f32"),
          )),
    ),
    "DucCircleMatoParam": (
        ("mToppleMax", "f32"), ("mToppleStep", "f32"), ("mFallDrag", "f32"),
        ("mFallGravity", "f32"), ("unk10", "f32"),
        *((f"mPoints[{index}]", "s32") for index in range(8)),
        ("mBaseRadius", "f32"),
        *((f"mLayout.mGrid[{grid}][{index}]", "f32") for grid in range(2) for index in range(5)),
        *((f"mLayout.mPatterns[{pattern}].{field}[{index}]", kind)
          for pattern in range(8)
          for field, kind in (("mHitScale", "f32"), ("mRadius", "f32"))
          for index in range(2)),
        *((f"mLayout.mPatterns[{pattern}].mGrowFrames", "s32") for pattern in range(8)),
        *((f"mLayout.mPatterns[{pattern}].mStandFrames[{index}]", "s32")
          for pattern in range(8) for index in range(2)),
    ),
    "DucClayParam": (
        ("mDrag", "f32"), ("mNearZ", "f32"), ("mFarZ", "f32"), ("mBonusZ", "f32"),
        ("mBonusPoints", "s32"), ("mPoints", "s32"), ("mBaseRadius", "f32"),
        ("mSpinDecay", "f32"), ("mSpin", "f32"),
        *((f"mPatterns[{pattern}].{field}", kind) for pattern in range(8)
          for field, kind in (
              ("mAimRangeX0", "f32"), ("mAimRangeX1", "f32"), ("mAimRangeY", "f32"),
              ("mAimY", "f32"), ("mAimZ", "f32"), ("mHitScale", "f32"),
              ("mThrustDecay", "f32"), ("mThrust", "f32"), ("mTiltRange0", "f32"),
              ("mTiltRange1", "f32"), ("mFallScaleMax", "f32"), ("mFallScaleAdd", "f32"),
              ("mHitRadius", "f32"), ("mSpeed", "f32"),
          )),
    ),
    "DucDuckParam": (
        ("mFarPoints", "s32"), ("mPoints", "s32"), ("unk08", "s32"),
        ("mApproachPoints", "s32"), ("unk10", "s32"), ("mArcPoints", "s32"),
        ("mBaseRadius", "f32"), ("mDamageDrag", "f32"), ("mDamageLift", "f32"),
        ("mDamageFrames", "s32"), ("mJumpDrag", "f32"), ("mFarScreenY", "f32"),
        ("mApproachRangeMax", "f32"), ("mApproachRangeMin", "f32"),
        ("mApproachRadius", "f32"), ("mForwardY", "f32"), ("mForwardRangeX", "f32"),
        ("mForwardZMax", "f32"), ("mForwardXMin", "f32"), ("mForwardZMin", "f32"),
        ("mForwardAccel", "f32"), ("mForwardSpeedMax", "f32"), ("mForwardSpeed", "f32"),
        *((f"mMotions[{motion}].{field}", kind) for motion in range(4)
          for field, kind in (("mDrag", "f32"), ("mGravity", "f32"))),
        *((f"mPatterns[{pattern}].{field}", kind) for pattern in range(8)
          for field, kind in (
              ("mHitScale", "f32"), ("mHitRadius", "f32"), ("mJumpLift", "f32"),
              ("mJumpRangeX", "f32"), ("mJumpRangeY", "f32"), ("mJumpAimY", "f32"),
              ("mJumpAimZ", "f32"), ("mJumpSpeed", "f32"), ("mBounceFrames", "s32"),
              ("mApproachSpeed", "f32"), ("mArcGravity", "f32"), ("mArcAngle", "f32"),
              ("mArcSpeed", "f32"),
          )),
    ),
    "DucUfoParam": (
        ("mFarDist", "f32"), ("mGrowZ", "f32"), ("mNearDist", "f32"), ("mNearZ", "f32"),
        ("mPointsB", "s32"), ("mPointsVersusB", "s32"), ("mPointsA", "s32"),
        ("mPointsVersusA", "s32"), ("mLiftHeight", "f32"), ("mAbductY", "f32"),
        ("mBaseRadius", "f32"), ("unk2C", "f32"), ("mWanderDistMax", "f32"),
        ("mWanderDistMin", "f32"), ("unk38", "f32"), ("mCircleRadius", "f32"),
        ("mAbductBottom", "f32"), ("mAbductRadius", "f32"),
        *((f"mPatterns[{pattern}].{field}", kind) for pattern in range(8)
          for field, kind in (
              ("mHitScale", "f32"), ("mLeaveAccel", "f32"), ("mSteer", "f32"),
              ("mApproachSpeed", "f32"), ("mHoldFrames", "s32"), ("mAbductFrames", "s32"),
              ("mLiftFrames0", "s32"), ("mLiftFrames1", "s32"), ("mFlyAccel0", "f32"),
              ("mFlyAccel1", "f32"), ("mFleeRangeX", "f32"), ("mFleeRangeY", "f32"),
              ("mFleeAimY", "f32"), ("mFleeAimZ", "f32"), ("mFleeSpeed0", "f32"),
              ("mFleeSpeed1", "f32"), ("mHitRadius", "f32"), ("mWaitFrames", "s32"),
              ("mMoveCount", "s32"), ("mWanderSpeed", "f32"), ("mWanderVelZ", "f32"),
              ("mWanderFrames", "s32"), ("mWait2Frames", "s32"), ("mCircleSpeed", "f32"),
              ("mCircleVelZ", "f32"), ("mCircleTimeMax", "s32"), ("mCircleTimeMin", "s32"),
              ("mCircleStep0", "s32"), ("mCircleStep1", "s32"), ("mCircleFrames", "s32"),
              ("mAbductSpeed", "f32"), ("mAbductTimeMax", "s32"), ("mAbductTimeMin", "s32"),
              ("mAbductStep0", "s32"), ("mAbductStep1", "s32"),
          )),
    ),
}

TABLE_SPECS = {
    "DucBalloonParam": 188,
    "DucCanParam": 736,
    "DucCircleMatoParam": 320,
    "DucClayParam": 484,
    "DucDuckParam": 540,
    "DucUfoParam": 1192,
    "leveltable": 156,
    "rallyleveluptable": 2604,
    "rallyleveluptable2": 1212,
    "wanted_1p_leveltable": 6336,
    "wanted_2p_leveltable": 5120,
    "TnkGameParam": 10484,
    "BomQuestionParam": 19228,
}

TABLE_PATTERN = re.compile(
    r"(?:^|/)(?:param/)?("
    r"Duc(?:Balloon|Can|CircleMato|Clay|Duck|Ufo)Param"
    r"|leveltable"
    r"|rallyleveluptable2?"
    r"|wanted_[12]p_leveltable"
    r"|TnkGameParam"
    r"|BomQuestionParam"
    r")\.bin$",
    re.IGNORECASE,
)

TNK_MAP_PATTERN = re.compile(r"(?:^|/)MapData/TnkMapData_P(\d)_(\d\d)_(\d)\.bin$", re.IGNORECASE)


def require(data: bytes, offset: int, size: int, what: str) -> None:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise FormatError(f"{what}: needs bytes [{offset:#x}, {offset + size:#x}), file size {len(data):#x}")


def unpack(data: bytes, offset: int, format_string: str, what: str) -> tuple[Any, ...]:
    size = struct.calcsize(format_string)
    require(data, offset, size, what)
    return struct.unpack(format_string, data[offset:offset + size])


def color(data: bytes, offset: int, what: str) -> tuple[int, int, int, int]:
    return unpack(data, offset, "BBBB", what)


def floats(values: tuple[int, ...], what: str) -> tuple[float, ...]:
    result = []
    for value in values:
        if math.isnan(value):
            raise FormatError(f"{what}: non-finite f32 value")
    return tuple(values)


def require_finite(values: tuple[Any, ...], what: str) -> tuple[float, ...]:
    return floats(tuple(float(value) for value in values), what)


def section_table(data: bytes, fields: tuple[tuple[str, str], ...], offset: int = 0) -> dict[str, Any]:
    """Decode a table of 32-bit big-endian words. The type is in each entry."""
    result: dict[str, Any] = {}
    for name, type_name in fields:
        raw = unpack(data, offset, ">I", f"{name} at {offset:#x}")[0]
        if type_name == "f32":
            result[name] = require_finite((struct.unpack(">f", struct.pack(">I", raw))[0],), name)[0]
        elif type_name == "s32":
            result[name] = struct.unpack(">i", struct.pack(">I", raw))[0]
        else:
            result[name] = raw
        offset += 4
    return result


def egg_header(data: bytes, expected_magic: bytes, allowed_versions: tuple[int, ...]) -> dict[str, Any]:
    magic, size, version = unpack(data, 0, ">4sIB", "IBinary header")
    if magic != expected_magic:
        raise FormatError(f"IBinary header: magic {magic!r}, expected {expected_magic!r}")
    if version not in allowed_versions:
        raise FormatError(f"IBinary header: unsupported version {version}")
    if size != len(data):
        raise FormatError(f"IBinary header: size {size:#x}, file size {len(data):#x}")
    require(data, 0, 16, "IBinary header")
    return {"magic": magic.decode("ascii"), "size": size, "version": version}


def light_object(data: bytes, offset: int) -> dict[str, Any]:
    # EGG::IBinary<LightObject>::Bin, include/egg/gfxe/eggLightObject.h:14;
    # decoded by LightObject::SetBinaryInner, src/egg/gfxe/eggLightObject.cpp:393.
    magic, size, version = unpack(data, offset, ">4sIB", f"LightObject header at {offset:#x}")
    if magic != b"LOBJ":
        raise FormatError(f"LightObject at {offset:#x}: magic {magic!r}, expected b'LOBJ'")
    if version != 1:
        raise FormatError(f"LightObject at {offset:#x}: unsupported version {version}")
    if size != 80:
        raise FormatError(f"LightObject at {offset:#x}: size {size:#x}, expected 0x50")
    require(data, offset, size, f"LightObject at {offset:#x}")

    spot_fn, dist_attn_fn, byte_12, byte_13 = unpack(data, offset + 0x10, "BBBB", "LightObject modes")
    amb_light_idx, flags = unpack(data, offset + 0x14, ">HH", "LightObject indices")
    pos_x, pos_y, pos_z = require_finite(unpack(data, offset + 0x18, ">fff", "LightObject pos"), "LightObject pos")
    at_x, at_y, at_z = require_finite(unpack(data, offset + 0x24, ">fff", "LightObject target"), "LightObject target")
    intensity = require_finite(unpack(data, offset + 0x30, ">f", "LightObject intensity"), "LightObject intensity")[0]
    spot_arg = require_finite(unpack(data, offset + 0x3C, ">f", "LightObject spotArg"), "LightObject spotArg")[0]
    dist_attn_arg0 = require_finite(unpack(data, offset + 0x40, ">f", "LightObject distAttnArg0"), "LightObject distAttnArg0")[0]
    dist_attn_arg1 = require_finite(unpack(data, offset + 0x44, ">f", "LightObject distAttnArg1"), "LightObject distAttnArg1")[0]
    return {
        "header": {"magic": "LOBJ", "size": size, "version": version},
        "spotFn": spot_fn,
        "distAttnFn": dist_attn_fn,
        "BYTE_0x12": byte_12,
        "BYTE_0x13": byte_13,
        "ambLightIdx": amb_light_idx,
        "flags": flags,
        "pos": {"x": pos_x, "y": pos_y, "z": pos_z},
        "target": {"x": at_x, "y": at_y, "z": at_z},
        "intensity": intensity,
        "color": color(data, offset + 0x34, "LightObject color"),
        "COLOR_0x38": color(data, offset + 0x38, "LightObject COLOR_0x38"),
        "spotArg": spot_arg,
        "distAttnArg0": dist_attn_arg0,
        "distAttnArg1": dist_attn_arg1,
        "UNK_0x48": data[offset + 0x48:offset + 0x50].hex(),
    }


def parse_lght(data: bytes) -> dict[str, Any]:
    # EGG::IBinary<LightManager>::BinData0/1 and LightObject::Bin,
    # src/egg/gfxe/eggLightManager.cpp:25-41; loader :317.
    header = egg_header(data, b"LGHT", (0, 1, 2))
    version = header["version"]
    if version in (0, 1):
        light_num = unpack(data, 0x10, ">H", "LGHT lightNum")[0]
        if version == 0:
            ambient_colors = [color(data, 0x14 + index * 4, "LGHT ambient") for index in range(2)]
            light_offset = 0x30
            light_tex_color = (0, 0, 0, 255)
            ambient_num = 2
        else:
            ambient_colors = [color(data, 0x14 + index * 4, "LGHT ambient") for index in range(8)]
            light_tex_color = color(data, 0x34, "LGHT lightTexColor")
            light_offset = 0x48
            ambient_num = 8
    else:
        light_num, ambient_num = unpack(data, 0x10, ">HH", "LGHT counts")
        light_tex_color = color(data, 0x14, "LGHT lightTexColor")
        light_offset = 0x28
        ambient_colors = None

    if light_num > 16:
        raise FormatError(f"LGHT: lightNum {light_num}, maximum 16")
    lights = []
    for index in range(light_num):
        if light_offset >= len(data):
            raise FormatError(f"LGHT light {index}: starts at {light_offset:#x}, beyond file")
        lights.append(light_object(data, light_offset))
        light_offset += lights[-1]["header"]["size"]
    if light_offset != len(data) and ambient_colors is not None:
        raise FormatError(f"LGHT: light records end at {light_offset:#x}, expected end {len(data):#x}")

    ambient = None
    if ambient_colors is not None:
        ambient = [{"color": value, "unk4": None} for value in ambient_colors]
    else:
        if ambient_num > 8:
            raise FormatError(f"LGHT: ambientNum {ambient_num}, maximum 8")
        ambient_offset = light_offset
        ambient = []
        for index in range(ambient_num):
            require(data, ambient_offset, 8, f"LGHT ambient {index}")
            ambient.append({
                "color": color(data, ambient_offset, f"LGHT ambient {index}"),
                "unk4": data[ambient_offset + 4:ambient_offset + 8].hex(),
            })
            ambient_offset += 8
        if ambient_offset != len(data):
            raise FormatError(f"LGHT: ambient records end at {ambient_offset:#x}, expected end {len(data):#x}")

    return {
        "format": "LGHT",
        "header": header,
        "lightNum": light_num,
        "ambientNum": ambient_num,
        "lightTexColor": light_tex_color,
        "ambient": ambient,
        "lights": lights,
    }


def parse_light_texture(data: bytes, offset: int) -> dict[str, Any]:
    # EGG::IBinary<LightTexture>::Bin, include/egg/gfxe/eggLightTexture.h:17;
    # decoded by LightTexture::SetBinaryInner, src/egg/gfxe/eggLightTexture.cpp:516.
    magic, size, version = unpack(data, offset, ">4sIB", f"LightTexture header at {offset:#x}")
    if magic != b"LTEX":
        raise FormatError(f"LightTexture at {offset:#x}: magic {magic!r}, expected b'LTEX'")
    if version != 0:
        raise FormatError(f"LightTexture at {offset:#x}: unsupported version {version}")
    require(data, offset, size, f"LightTexture at {offset:#x}")

    minimum_size = 0x3C
    light_num = unpack(data, offset + 0x10, ">H", "LightTexture lightNum")[0]
    if size < minimum_size + light_num * 8:
        raise FormatError(
            f"LightTexture at {offset:#x}: size {size:#x} too small for {light_num} lights"
        )
    name_raw = data[offset + 0x14:offset + 0x34]
    if b"\0" not in name_raw:
        raise FormatError(f"LightTexture at {offset:#x}: unterminated name")
    name = name_raw.split(b"\0", 1)[0]
    if not name:
        raise FormatError(f"LightTexture at {offset:#x}: empty name")
    draw_type = data[offset + 0x34]
    light_mask = unpack(data, offset + 0x38, ">I", "LightTexture lightMask")[0]
    light_offset = offset + minimum_size
    lights = []
    for index in range(light_num):
        scale, pattern = unpack(data, light_offset, ">fB", f"LightTexture light {index}")
        scale = require_finite((scale,), f"LightTexture light {index} effectScale")[0]
        lights.append({
            "effectScale": scale,
            "pttrn": pattern,
            "unk5": data[light_offset + 5:light_offset + 8].hex(),
        })
        light_offset += 8
    if light_offset != offset + size:
        raise FormatError(
            f"LightTexture at {offset:#x}: lights end at {light_offset:#x}, section ends {offset + size:#x}"
        )
    return {
        "header": {"magic": "LTEX", "size": size, "version": version},
        "lightNum": light_num,
        "name": name.decode("ascii", "strict"),
        "drawType": draw_type,
        "lightMask": light_mask,
        "lights": lights,
    }


def parse_lmap(data: bytes) -> dict[str, Any]:
    # EGG::IBinary<LightTextureManager>::BinData, include/egg/gfxe/
    # eggLightTextureManager.h:18; loader src/egg/gfxe/eggLightTextureManager.cpp:263.
    header = egg_header(data, b"LMAP", (0,))
    texture_num = unpack(data, 0x10, ">H", "LMAP textureNum")[0]
    if texture_num == 0:
        raise FormatError("LMAP: textureNum is zero")
    textures = []
    texture_offset = 0x20
    for index in range(texture_num):
        if texture_offset >= len(data):
            raise FormatError(f"LMAP texture {index}: starts at {texture_offset:#x}, beyond file")
        textures.append(parse_light_texture(data, texture_offset))
        texture_offset += textures[-1]["header"]["size"]
    if texture_offset != len(data):
        raise FormatError(f"LMAP: textures end at {texture_offset:#x}, expected end {len(data):#x}")
    return {"format": "LMAP", "header": header, "textureNum": texture_num, "textures": textures}


def parse_pblm(data: bytes) -> dict[str, Any]:
    # EGG::IBinary<DrawPathBloom>::BinData, include/egg/gfxe/
    # eggDrawPathBloom.h:31; loader src/egg/gfxe/eggDrawPathBloom.cpp:281.
    header = egg_header(data, b"PBLM", (0, 1))
    version = header["version"]
    minimum_size = 0x94
    if len(data) < minimum_size:
        raise FormatError(f"PBLM: file size {len(data):#x}, needs at least {minimum_size:#x}")
    threshold = require_finite(unpack(data, 0x10, ">f", "PBLM mThreshold"), "PBLM mThreshold")[0]
    flags, unk_e = unpack(data, 0x1C, ">HH", "PBLM flags")
    blur = []
    for index, blur_offset in enumerate((0x20, 0x40, 0x60)):
        radius, intensity = require_finite(
            unpack(data, blur_offset, ">ff", f"PBLM mBlur{index}"), f"PBLM mBlur{index}"
        )
        blur.append({
            "mRadius": radius,
            "mIntensity": intensity,
            "unk8": data[blur_offset + 8:blur_offset + 0x20].hex(),
        })
    blend_mode_idx, num_blur = data[0x80], data[0x81]
    if version == 0:
        gather_scale0 = gather_scale1 = 0.0
    else:
        gather_scale0, gather_scale1 = require_finite(
            unpack(data, 0x8C, ">ff", "PBLM gather scales"), "PBLM gather scales"
        )
    return {
        "format": "PBLM",
        "header": header,
        "mThreshold": threshold,
        "mMaskColor": color(data, 0x14, "PBLM mMaskColor"),
        "mColor": color(data, 0x18, "PBLM mColor"),
        "mFlags": flags,
        "unkE": unk_e,
        "mBlur0": blur[0],
        "mBlur1": blur[1],
        "mBlur2": blur[2],
        "mBlendModeIdx": blend_mode_idx,
        "mNumBlur": num_blur,
        "mGatherScale0": gather_scale0,
        "mGatherScale1": gather_scale1,
    }


def parse_mmsc(data: bytes) -> dict[str, Any]:
    # EGG::IBinary<RPGrpModelMisc>::BinData/MatTarget, include/Pack/
    # RPGraphics/RPGrpModelMisc.h:18-30.
    header = egg_header(data, b"MMSC", (0,))
    target_num = unpack(data, 0x10, ">H", "MMSC targetNum")[0]
    if 0x14 + target_num * 0x28 != len(data):
        raise FormatError(f"MMSC: file size {len(data):#x} does not match {target_num} targets")
    targets = []
    for index in range(target_num):
        target_offset = 0x14 + index * 0x28
        name_raw = data[target_offset:target_offset + 0x20]
        if b"\0" not in name_raw:
            raise FormatError(f"MMSC target {index}: unterminated name")
        name = name_raw.split(b"\0", 1)[0]
        if not name:
            raise FormatError(f"MMSC target {index}: empty name")
        unk24 = require_finite(
            unpack(data, target_offset + 0x24, ">f", f"MMSC target {index} unk24"),
            f"MMSC target {index} unk24",
        )[0]
        targets.append({
            "name": name.decode("ascii", "strict"),
            "unk20": data[target_offset + 0x20],
            "unk24": unk24,
        })
    return {"format": "MMSC", "header": header, "targetNum": target_num, "targets": targets}


def parse_duc_param(data: bytes, name: str) -> dict[str, Any]:
    # Loaded by RPDucBalloon::Load, src/Pack/RPParty/RPDucScene/RPDucBalloon.cpp:225;
    # RPDucCan::Load, RPDucCan.cpp:515; RPDucCircleTarget::LoadParams,
    # RPDucCircleTarget.cpp:55; RPDucClay::Load, RPDucClay.cpp:247;
    # RPDucDuck::LoadParams, RPDucDuck.cpp:76; RPDucUfo::LoadParams, RPDucUfo.cpp:74.
    expected_size = TABLE_SPECS[name]
    if len(data) != expected_size:
        raise FormatError(f"{name}: file size {len(data):#x}, expected {expected_size:#x}")
    word_count = expected_size // 4
    fields = DUC_PARAM_FIELDS[name]
    if len(fields) != word_count:
        raise FormatError(f"{name}: schema has {len(fields)} fields, file has {word_count} words")
    decoded = section_table(data, fields)
    return {
        "format": name,
        "size": len(data),
        "wordCount": word_count,
        "fields": decoded,
        "patterns": 8,
    }


def parse_leveltable(data: bytes, name: str) -> dict[str, Any]:
    # RPPnpLevelTable::{LevelEntry,RallyEntry}, include/Pack/RPParty/RPPnpScene/
    # RPPnpLevelTable.h:16-26; loader src/Pack/RPParty/RPPnpScene/RPPnpLevelTable.cpp:21.
    expected_size = TABLE_SPECS[name]
    if len(data) != expected_size or expected_size % 12:
        raise FormatError(f"{name}: file size {len(data):#x}, expected {expected_size:#x}")
    record_num = expected_size // 12
    fields = ("unk0", "unk4", "unk8") if name == "leveltable" else ("mRally", "mCpuLevel", "mBallSpeed")
    records = []
    for index in range(record_num):
        offset = index * 12
        values = section_table(data, tuple((field, "s32" if index < 2 else "f32") for index, field in enumerate(fields)), offset)
        records.append(values)
    return {"format": name, "size": expected_size, "recordCount": record_num, "records": records}


def parse_wly_leveltable(data: bytes, name: str) -> dict[str, Any]:
    # RPWlyLevelData, include/Pack/RPParty/RPWlyScene/RPWlyLevelTable.h:30;
    # loader src/Pack/RPParty/RPWlyScene/RPWlyLevelTable.cpp:39.
    expected_size = TABLE_SPECS[name]
    if len(data) != expected_size or expected_size % 64:
        raise FormatError(f"{name}: file size {len(data):#x}, expected {expected_size:#x}")
    record_num = expected_size // 64
    fields = (
        ("mAvatarNum", "s32"), ("unk4", "u32"), ("mQuestionType", "s32"),
        ("mFieldType", "s32"), ("unk10", "f32"), ("unk14", "f32"),
        ("mCameraTarget", "f32"), ("mSpeed", "f32"), ("unk20", "f32"),
        ("mSpotlight", "f32"), ("mFoundScale", "f32"), ("mSpotPos.x", "f32"),
        ("mSpotPos.y", "f32"), ("mSpotPos.z", "f32"), ("mPlaneY", "f32"),
        ("mPlaneZ", "f32"),
    )
    records = []
    for index in range(record_num):
        record = section_table(data, fields, index * 64)
        if not 0 <= record["mFieldType"] <= 4:
            raise FormatError(f"{name} record {index}: mFieldType {record['mFieldType']} outside 0..4")
        records.append(record)
    return {"format": name, "size": expected_size, "recordCount": record_num, "records": records}


def parse_tnk_map(data: bytes, players: int, stage: int, tv_mode: int) -> dict[str, Any]:
    # RPTnkMapData::Map, include/Pack/RPParty/RPPartyCommon/RPTnkMapData.h:22;
    # loader src/Pack/RPParty/RPPartyCommon/RPTnkMapData.cpp:71.
    if players not in (1, 2) or not 0 <= stage <= 29 or tv_mode not in (0, 1):
        raise FormatError(f"TnkMapData: invalid key players={players}, stage={stage}, tvMode={tv_mode}")
    if len(data) < 0x10 or (len(data) - 0x10) % 4:
        raise FormatError(f"TnkMapData: file size {len(data):#x} is not a 0x10-byte header plus s32 cells")
    width = unpack(data, 0, ">i", "TnkMapData width")[0]
    expected_width = 22 if tv_mode == 1 else 16
    if width != expected_width:
        raise FormatError(f"TnkMapData: width {width}, expected {expected_width} for tvMode {tv_mode}")
    cell_count = (len(data) - 0x10) // 4
    if cell_count % width or cell_count // width != 17:
        raise FormatError(f"TnkMapData: {cell_count} cells is not a {width} x 17 grid")
    header = section_table(data, (("width", "s32"), ("unk4", "u32"), ("unk8", "s32"), ("unkC", "s32")))
    cells = list(unpack(data, 0x10, f">{cell_count}i", "TnkMapData cells"))
    return {
        "format": "TnkMapData",
        "size": len(data),
        "players": players,
        "stage": stage,
        "tvMode": tv_mode,
        "header": header,
        "width": width,
        "height": 17,
        "cellCount": cell_count,
    }


def parse_tnk_game_param(data: bytes) -> dict[str, Any]:
    # TnkGameParam/TnkStageParam, src/Pack/RPParty/RPTnkScene/RPTnkGameManager.cpp:29-39.
    expected_size = TABLE_SPECS["TnkGameParam"]
    if len(data) != expected_size:
        raise FormatError(f"TnkGameParam: file size {len(data):#x}, expected {expected_size:#x}")
    stage_record_size = 0x58
    lives = unpack(data, 0, ">i", "TnkGameParam lives")[0]
    stage_offset = 4 + 10 * 0xA8
    stages = []
    for index in range(100):
        offset = stage_offset + index * stage_record_size
        stages.append(section_table(data, (
            ("bonus[0]", "s32"), ("bonus[1]", "s32"),
            *((f"enemyTypes[0][{enemy}]", "s32") for enemy in range(8)),
            *((f"enemyTypes[1][{enemy}]", "s32") for enemy in range(8)),
            ("variants[0][0]", "s32"), ("variants[0][1]", "s32"),
            ("variants[1][0]", "s32"), ("variants[1][1]", "s32"),
        ), offset))
    return {"format": "TnkGameParam", "size": len(data), "lives": lives, "enemyCount": 10, "stageCount": 100, "stages": stages}


def parse_bom_question_param(data: bytes) -> dict[str, Any]:
    # RPBomQuestionParamFile and its three record structures, include/Pack/
    # RPParty/RPBomScene/RPBomQuestionParam.h:17-87; loader src/Pack/RPParty/
    # RPBomScene/RPBomQuestionParam.cpp:348.
    expected_size = TABLE_SPECS["BomQuestionParam"]
    if len(data) != expected_size:
        raise FormatError(f"BomQuestionParam: file size {len(data):#x}, expected {expected_size:#x}")
    expected_size = 0x3C + 101 * 0x28 + 250 * 0x38 + 16 * 0x44 + 10 * 4
    if len(data) != expected_size:
        raise FormatError(f"BomQuestionParam: derived size {expected_size:#x} differs from struct layout")

    header_fields = (
        ("mMissZ", "f32"), ("mHitZoneFar", "f32"), ("mHitZoneNear", "f32"),
        ("mTwoPlayerCostScale", "f32"), ("unk10", "u32"),
        ("mSpeedChangeFrames", "s32"), ("mBubbleScale169", "f32"),
        ("mBubbleScale43", "f32"), ("unk20[0]", "u32"), ("unk20[1]", "u32"),
        ("mMissWait", "s32"), ("mMissMax", "s32"),
        ("mSpawnXMargin", "f32"), ("mSpawnZ", "f32"), ("mBubbleWait", "s32"),
    )
    header = section_table(data, header_fields)
    question_offset = 0x3C
    questions = []
    for index in range(101):
        questions.append(section_table(data, (
            ("mMissNext", "s32"), ("mCost", "s32"),
            ("mPoseEnabled[0]", "s32"), ("mPoseEnabled[1]", "s32"), ("mPoseEnabled[2]", "s32"),
            ("mWaveInterval", "s32"), ("mWaveNum[0]", "s32"), ("mWaveNum[1]", "s32"),
            ("mSpeed[0]", "f32"), ("mSpeed[1]", "f32"),
        ), question_offset + index * 0x28))
    pattern_offset = 0x1004
    patterns = []
    for index in range(250):
        patterns.append(section_table(data, (
            ("mSegmentsMax", "u32"), ("mSegmentsMin", "u32"),
            ("mRadiusMax", "f32"), ("mRadiusMin", "f32"), ("unk10", "u32"), ("unk14", "f32"),
            ("mQuestion", "s32"), ("mRotDirMode", "s32"),
            ("mRotSpeedMax", "f32"), ("mRotSpeedMin", "f32"),
            ("mAngleMin", "f32"), ("mAngleMax", "f32"),
            ("mSpawnZMax", "f32"), ("mSpawnZMin", "f32"),
        ), pattern_offset + index * 0x38))
    level_offset = 0x46B4
    levels = []
    for index in range(16):
        fields = (("unk0", "s32"), *((f"mSpecialChance[{chance}]", "s32") for chance in range(11)),
                  ("mWaveIntervalScale", "f32"), ("mTime", "s32"),
                  ("mStartQuestion", "s32"), ("mBgFrame", "s32"), ("mSpecialGap", "s32"))
        levels.append(section_table(data, fields, level_offset + index * 0x44))
    combo_offset = 0x4AF4
    combo_points = list(unpack(data, combo_offset, ">10i", "BomQuestionParam mComboPoints"))
    return {
        "format": "BomQuestionParam", "size": len(data), "header": header,
        "questionCount": len(questions), "patternCount": len(patterns),
        "levelCount": len(levels), "comboCount": len(combo_points), "comboPoints": combo_points,
    }


def parse(data: bytes, *, path: str | None = None) -> dict[str, Any]:
    """Parse a game-data file and return its decoded header and section table."""
    if not data:
        raise FormatError("empty file")
    magic = data[:4]
    if magic in EGG_FORMATS:
        parser = {
            b"LGHT": parse_lght,
            b"LMAP": parse_lmap,
            b"PBLM": parse_pblm,
            b"MMSC": parse_mmsc,
        }[magic]
        return parser(data)
    if path is not None:
        match = TNK_MAP_PATTERN.search("/" + path.replace("\\", "/"))
        if match:
            return parse_tnk_map(data, int(match.group(1)), int(match.group(2)), int(match.group(3)))
        match = TABLE_PATTERN.search("/" + path.replace("\\", "/"))
        if match:
            name = match.group(1)
            if name.startswith("Duc"):
                return parse_duc_param(data, name)
            if name == "leveltable" or name.startswith("rallyleveluptable"):
                return parse_leveltable(data, name)
            if name.startswith("wanted_"):
                return parse_wly_leveltable(data, name)
            if name == "TnkGameParam":
                return parse_tnk_game_param(data)
            if name == "BomQuestionParam":
                return parse_bom_question_param(data)
    raise FormatError("unknown game-data format")


def classify(path: str, data: bytes) -> str | None:
    normalized = "/" + path.replace("\\", "/")
    suffix = Path(normalized).suffix.lower()
    if suffix in EGG_EXTENSIONS:
        return EGG_EXTENSIONS[suffix].decode("ascii")
    match = TNK_MAP_PATTERN.search(normalized)
    if match:
        return "TnkMapData"
    match = TABLE_PATTERN.search(normalized)
    if match:
        return match.group(1)
    if data[:4] in EGG_FORMATS:
        return EGG_FORMATS[data[:4]].decode("ascii")
    return None


def check(root: str) -> int:
    stats: dict[str, dict[str, Any]] = {}
    for path, data in walk(root):
        format_name = classify(path, data)
        if format_name is None:
            continue
        stat = stats.setdefault(format_name, {
            "parsed": 0, "errors": 0, "versions": collections.Counter(), "counts": collections.Counter()
        })
        try:
            decoded = parse(data, path=path)
            stat["parsed"] += 1
            stat["versions"][decoded.get("header", {}).get("version")] += 1
            count_fields = {
                "LGHT": "lightNum", "LMAP": "textureNum", "MMSC": "targetNum",
                "TnkMapData": "cellCount", "TnkGameParam": "stageCount",
                "BomQuestionParam": "patternCount",
            }
            if format_name in count_fields:
                stat["counts"][decoded[count_fields[format_name]]] += 1
            elif format_name.startswith("Duc"):
                stat["counts"][decoded["wordCount"]] += 1
            elif "recordCount" in decoded:
                stat["counts"][decoded["recordCount"]] += 1
        except Exception as error:
            stat["errors"] += 1
            print(f"ERROR {path}: {error}", file=sys.stderr)

    order = ["LGHT", "LMAP", "PBLM", "MMSC", "DucBalloonParam", "DucCanParam", "DucCircleMatoParam",
             "DucClayParam", "DucDuckParam", "DucUfoParam", "leveltable", "rallyleveluptable",
             "rallyleveluptable2", "wanted_1p_leveltable", "wanted_2p_leveltable", "TnkMapData",
             "TnkGameParam", "BomQuestionParam"]
    for format_name in order:
        if format_name not in stats:
            continue
        stat = stats[format_name]
        versions = sorted(key for key in stat["versions"] if key is not None)
        versions_text = ",".join(str(version) for version in versions) if versions else "-"
        counts = ",".join(f"{count}x{value}" for value, count in sorted(stat["counts"].items()))
        print(
            f"{format_name}: parsed={stat['parsed']} errors={stat['errors']} "
            f"versions={versions_text} counts={counts or '-'}"
        )
    missing = [name for name in order if name not in stats]
    if missing:
        print("missing=" + ",".join(missing), file=sys.stderr)
        return 1
    if any(stat["errors"] for stat in stats.values()):
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", metavar="ROOT", help="walk ROOT and validate every game-data file")
    args = parser.parse_args()
    if args.check is None:
        parser.error("--check is required")
    return check(args.check)


if __name__ == "__main__":
    raise SystemExit(main())
