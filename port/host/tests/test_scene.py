#!/usr/bin/env python3
"""Compare tests/mdl_harness (the real nw4r::g3d scene runtime on the host: ScnRoot, ScnMdl,
ScnMdlSimple, the AnmObj units, CalcWorld / CalcMaterial / CalcView) with an independent
model of what it computes for every model on the disc (port/docs/HOST_BUILD.md §20).

The models and their node, material and texture records are located with
port/assets/tools/fmt_brres_model.py and the animations with fmt_brres_anim.py (read-only);
the animation results come from test_anims.py's f32 model of the g3d readers (§17). On top of
those, this file models, from the file bytes:

* the scene set-up: every MDL0 on the disc is built, and every CHR0 / CLR0 / SRT0 / PAT0 of its
  own BRRES or of a model-less BRRES of the same archive whose names bind to it is applied at
  the harness's frames (play policy: one-time or loop);
* CalcWorld: the NodeTree byte code (CALC, MTXDUP), each node's result from its CHR0 track or
  its bind pose (ResNode::CalcChrAnmResult), the world-matrix attributes, the standard and
  Maya segment-scale-compensation rules (g3d_basic.cpp, g3d_maya.cpp), the final per-matrix
  scale, and CalcSkinning (NodeMix: EVPMTX, WEIGHT);
* CalcView with the printed camera matrix: view position matrices (PSMTXConcatArray) and, for
  matrices whose scale is uniform all the way up, the normal matrix (the view matrix's 3x3);
* CalcMaterial: CLR0 colours merged into the material channel and TEV registers through
  the masks, SRT0 texture SRTs and flags, PAT0 texture objects (texture and size).

The host's matrix arithmetic is the paired-single code's (port/host/include/port/ps_forms.h):
ps_madd and friends are fused (one rounding), everything else rounds every operation to
single precision. Fused steps are computed here with an exact single-precision fma, so every
modelled value must match bit for bit. Not modelled, compared within a tolerance: the camera
matrix (C_MTXLookAt normalises with frsqrte and a Newton step) and the normal matrices of
non-uniformly scaled matrices (MTX34InvTranspose divides through fres plus two Newton steps):
both within 1e-5 of the double-precision result, relative to the row's largest element.
Everything is also regression-checked: with --compare, the other build's harness must print the
same bytes (64- against 32-bit).

    test_scene.py --harness build/host64/mdl_harness --disc ~/wii-play/extracted/files
"""
import argparse
import collections
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "port", "assets", "tools"))
sys.path.insert(0, HERE)
import fmt_brres_anim as A  # noqa: E402  (read-only reference)
import fmt_brres_model as M  # noqa: E402  (read-only reference)
import test_anims as T  # noqa: E402  (the §17 animation model)
from wiiarc import walk  # noqa: E402

f32, bits, from_bits, add, sub, mul, div = T.f32, T.bits, T.from_bits, T.add, T.sub, T.mul, T.div


def fma(a, b, c):
    """Single-precision fused multiply-add (fmaf): a*b is exact in double; the sum is rounded
    to odd in double, which then rounds correctly to single."""
    p = a * b
    s = p + c
    if math.isinf(s) or math.isnan(s):
        return f32(s)
    # TwoSum: the exact error of s.
    bb = s - p
    err = (p - (s - bb)) + (c - bb)
    if err != 0.0:
        m = struct.unpack("<Q", struct.pack("<d", s))[0]
        if m & 1 == 0:
            s = math.nextafter(s, math.inf if err > 0 else -math.inf)
    return f32(s)


# --- matrices (row-major 3x4 lists of 12 floats) --------------------------------------------

IDENT = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]


def concat(a, b, array=False):
    """PSMTXConcat / PSMTXConcatArray (port_mtx_concat): a x b."""
    d = []
    for r in range(3):
        a0, a1, a2, a3 = a[4 * r:4 * r + 4]
        row = []
        for c in range(4):
            v = mul(b[c], a0)
            v = fma(b[4 + c], a1, v)
            v = fma(b[8 + c], a2, v)
            row.append(v)
        row[2] = fma(0.0, a2 if array else a3, row[2])
        row[3] = fma(1.0, a3, row[3])
        d += row
    return d


def scale_cols(m, s):
    """nw4r::math::MTX34Scale(out, in, scale): column j of the 3x3 times s[j]."""
    out = []
    for r in range(3):
        out += [mul(m[4 * r], s[0]), mul(m[4 * r + 1], s[1]), mul(m[4 * r + 2], s[2]),
                mul(m[4 * r + 3], 1.0)]
    return out


def scale_rows(s, m):
    """MTX34Scale(out, scale, in) = PSMTXScaleApply: row i times s[i]."""
    return [mul(m[4 * r + c], s[r]) for r in range(3) for c in range(4)]


def trans(m, t):
    """nw4r::math::MTX34Trans(out, in, trans) (port_mtx34_trans)."""
    out = list(m)
    for r in range(3):
        w0 = mul(m[4 * r], t[0])
        w0b = mul(m[4 * r + 1], t[1])
        w1 = fma(m[4 * r + 2], t[2], w0)
        w1b = fma(m[4 * r + 3], 1.0, w0b)
        out[4 * r + 3] = add(w1, w1b)
    return out


# --- file reads -----------------------------------------------------------------------------

class Data(T.Data):
    def floats(self, o, n):
        return [self.f(o + 4 * i) for i in range(n)]

    def cstr(self, o):
        return self.d[o:self.d.index(b"\0", o)].decode("ascii", "replace")


def dic_entries(r, dic):
    """ResDic entries in index order: (name, absolute data offset)."""
    n = r.u32(dic + 4)
    out = []
    for i in range(1, n + 1):
        e = dic + 8 + 16 * i
        name = r.cstr(dic + r.s32(e + 8)) if r.s32(e + 8) else ""
        out.append((name, dic + r.s32(e + 12)))
    return out


class Model:
    def __init__(self, r, sub):
        o = sub["offset"]
        self.r, self.o = r, o
        self.name = sub["resourceName"]
        info = o + 0x40
        self.scaling_rule = r.u32(info + 0x08)
        self.num_view = r.u32(info + 0x1C)
        self.need_nrm = r.u8(info + 0x20)
        self.num_mtx = r.u32(r.s32(info + 0x24) + info) if r.s32(info + 0x24) else 0
        self.mtx_to_node = [r.s32(info + r.s32(info + 0x24) + 4 + 4 * i)
                            for i in range(self.num_mtx)]

        def dic(field):
            rel = r.s32(o + field)
            return dic_entries(r, o + rel) if rel else []
        self.byte_codes = dict(dic(0x10))
        self.nodes = []
        for name, no in dic(0x14):
            self.nodes.append({
                "name": name, "id": r.u32(no + 0xC), "mtx": r.u32(no + 0x10),
                "flags": r.u32(no + 0x14), "bb": r.u32(no + 0x18),
                "scale": r.floats(no + 0x20, 3), "rot": r.floats(no + 0x2C, 3),
                "trans": r.floats(no + 0x38, 3), "inv": r.floats(no + 0xA0, 12)})
        self.mats = dic(0x28)
        self.node_by_name = {n["name"]: n for n in self.nodes}
        self.mat_index = {name: i for i, (name, _o) in enumerate(self.mats)}


# --- CalcWorld (g3d_calcworld.cpp, g3d_basic.cpp, g3d_maya.cpp, g3d_resnode.cpp) ----------

# ChrAnmResult flags
F_MTX_IDENT, F_ROT_TRANS_ZERO, F_SCALE_ONE, F_SCALE_UNIFORM = 1 << 1, 1 << 2, 1 << 3, 1 << 4
F_ROT_ZERO, F_TRANS_ZERO, F_SSC_APPLY, F_SSC_PARENT = 1 << 5, 1 << 6, 1 << 10, 1 << 11
# WorldMtxAttr
A_S_UNIFORM, A_ALL_S_UNIFORM, A_S_ONE, A_ALL_S_ONE = 1 << 28, 1 << 29, 1 << 30, 1 << 31
ROOT_ATTR = A_S_UNIFORM | A_ALL_S_UNIFORM | A_S_ONE | A_ALL_S_ONE


def bind_result(node):
    """ResNode::CalcChrAnmResult: (flags, s, rt)."""
    nf = node["flags"]
    flags = 0
    if nf & (1 << 3):
        flags |= F_SCALE_ONE | F_SCALE_UNIFORM
        s = [1.0, 1.0, 1.0]
    else:
        if nf & (1 << 4):
            flags |= F_SCALE_UNIFORM
        s = list(node["scale"])
    if nf & (1 << 2):
        rt = list(IDENT)
        flags |= F_ROT_ZERO
    else:
        m = T.rot_xyz_deg(*node["rot"])
        rt = m[0] + m[1] + m[2]
    if nf & (1 << 1):
        flags |= F_TRANS_ZERO
    else:
        rt[3], rt[7], rt[11] = node["trans"]
    if flags & F_ROT_ZERO and flags & F_TRANS_ZERO:
        flags |= F_ROT_TRANS_ZERO
        if flags & F_SCALE_ONE:
            flags |= F_MTX_IDENT
    flags |= 1 | (1 << 31)
    if nf & (1 << 5):
        flags |= F_SSC_APPLY
    if nf & (1 << 6):
        flags |= F_SSC_PARENT
    return flags, s, rt


PATHS = collections.Counter()  # which world-matrix rules the modelled runs went through


def anm_not_scale_one(a):
    return a & ~(A_S_ONE | A_ALL_S_ONE) & 0xFFFFFFFF


def anm_not_scale_uniform(a):
    return a & ~(A_S_UNIFORM | A_ALL_S_UNIFORM | A_S_ONE | A_ALL_S_ONE) & 0xFFFFFFFF


def calc_world_mtx(res, w1, s1, attr):
    """CalcWorldMtx_Basic / CalcWorldMtx_Maya_SSC_Apply: (W, S, attr)."""
    flags, rs, rt = res
    new = attr
    if flags & F_SCALE_ONE:
        new |= A_S_ONE
        s = [1.0, 1.0, 1.0]
    else:
        new = anm_not_scale_one(new)
        s = list(rs)
    parent_one = attr & A_S_ONE
    t = [rt[3], rt[7], rt[11]]
    PATHS["%s %s %s" % ("maya-ssc" if flags & F_SSC_APPLY else "basic",
                        "ident" if flags & (F_MTX_IDENT | F_ROT_TRANS_ZERO) else
                        "trans" if flags & F_ROT_ZERO else "rt",
                        "parent-s1" if parent_one else "parent-scaled")] += 1
    if flags & F_SSC_APPLY:
        if flags & (F_MTX_IDENT | F_ROT_TRANS_ZERO):
            w = list(w1)
        elif flags & F_ROT_ZERO:
            w = trans(w1, t if parent_one else [mul(s1[0], t[0]), mul(s1[1], t[1]),
                                                   mul(s1[2], t[2])])
        elif parent_one:
            w = concat(w1, rt)
        else:
            m = list(rt)
            m[3], m[7], m[11] = mul(m[3], s1[0]), mul(m[7], s1[1]), mul(m[11], s1[2])
            w = concat(w1, m)
    else:
        if flags & (F_MTX_IDENT | F_ROT_TRANS_ZERO):
            w = list(w1) if parent_one else scale_cols(w1, s1)
        elif flags & F_ROT_ZERO:
            w = trans(w1, t) if parent_one else concat(w1, scale_rows(s1, rt))
        elif parent_one:
            w = concat(w1, rt)
        else:
            w = concat(scale_cols(w1, s1), rt)
    if flags & F_SCALE_UNIFORM:
        new |= A_S_UNIFORM
    else:
        new = anm_not_scale_uniform(new)
    return w, s, new


def calc_world(mdl, chr_results):
    """CalcWorld + CalcSkinning: (world matrices, attributes) by matrix ID.
    chr_results(node) -> (flags, s, rt) or None (no animation for the node)."""
    r = mdl.r
    n = mdl.num_mtx
    W = [None] * max(n, 1)
    S = [None] * max(n, 1)
    attr = [0] * max(n, 1)
    W[0], S[0], attr[0] = list(IDENT), [1.0, 1.0, 1.0], ROOT_ATTR
    ids = []
    o = mdl.byte_codes["NodeTree"]
    while True:
        c = r.u8(o)
        if c == 1:  # END
            break
        if c == 2:  # CALC
            node = mdl.nodes[r.u8(o + 1) << 8 | r.u8(o + 2)]
            parent = r.u8(o + 3) << 8 | r.u8(o + 4)
            mtx = node["mtx"]
            ids.append(mtx)
            res = chr_results(node)
            if res is None or res[0] == 0:
                res = bind_result(node)
            w, s, a = calc_world_mtx(res, W[parent], S[parent], attr[parent])
            W[mtx], S[mtx] = w, s
            attr[mtx] = (a & ~0xFF) | node["bb"]
            o += 5
        elif c == 6:  # MTXDUP
            to, frm = r.u8(o + 1) << 8 | r.u8(o + 2), r.u8(o + 3) << 8 | r.u8(o + 4)
            ids.append(to)
            attr[to], W[to], S[to] = attr[frm], list(W[frm]), list(S[frm])
            PATHS["mtxdup"] += 1
            o += 5
        else:
            raise ValueError("%s: NodeTree opcode %d" % (mdl.name, c))
    for mtx in ids:
        if S[mtx] != [1.0, 1.0, 1.0]:
            W[mtx] = scale_cols(W[mtx], S[mtx])
            PATHS["final scale"] += 1
    if "NodeMix" in mdl.byte_codes:
        skin = {}
        o = mdl.byte_codes["NodeMix"]
        while True:
            c = r.u8(o)
            if c == 1:
                break
            if c == 3:  # WEIGHT
                tgt, num = r.u8(o + 1) << 8 | r.u8(o + 2), r.u8(o + 3)
                a = ROOT_ATTR
                acc = [0.0] * 12
                o += 4
                for _ in range(num):
                    mtx = r.u8(o) << 8 | r.u8(o + 1)
                    w = r.f(o + 2)
                    acc = [fma(skin[mtx][k], w, acc[k]) for k in range(12)]
                    a &= attr[mtx]
                    o += 6
                W[tgt], attr[tgt] = acc, a
                PATHS["skin weight"] += 1
            elif c == 5:  # EVPMTX
                mtx, node = r.u8(o + 1) << 8 | r.u8(o + 2), r.u8(o + 3) << 8 | r.u8(o + 4)
                skin[mtx] = concat(W[mtx], mdl.nodes[node]["inv"])
                o += 5
            else:
                raise ValueError("%s: NodeMix opcode %d" % (mdl.name, c))
    return W, attr


# --- animations -----------------------------------------------------------------------------

class Anim:
    def __init__(self, path, data, kind, name, block):
        self.path, self.kind, self.name, self.block = path, kind, name, block
        self.r = Data(data)
        reader = A.Reader(data)
        parsed = A.parse_block(reader, kind, name, block)
        self.num_frame = parsed["numFrame"]
        self.loop = parsed["policy"] == 1
        count_at = {"CHR0": 0x1E, "CLR0": 0x1E, "SRT0": 0x1E, "PAT0": 0x2E}[kind]
        self.records = A.record_dictionary(reader, block, kind, 0x10,
                                           self.r.u16(block + count_at))
        self.parsed = parsed

    def frame(self, f):
        """FrameCtrl::SetFrm with the animation's play policy."""
        if not self.loop:
            return f
        length = float(self.num_frame)
        if f >= 0.0:
            return f32(math.fmod(f, length))
        off = f32(math.fmod(add(f, length), length))
        return add(off, 0.0 if off >= 0 else length)


def harness_frames(num_frame):
    """mdl_harness Frames()."""
    n = float(num_frame)
    out = [0.0, 1.5, add(float(int(div(n, 3.0))), 0.25), float(int(div(mul(n, 2.0), 3.0)))]
    if n >= 1.0:
        out.append(sub(n, 1.0))
    return out


def chr_lookup(anm, frame):
    recs = {name: rec for name, rec in anm.records}

    def result(node):
        rec = recs.get(node["name"])
        if rec is None:
            return None
        v = T.chr_result(anm.r, anm.block, rec, frame)
        flags = int(v[0], 16)
        fl = [from_bits(int(x, 16)) for x in v[1:]]
        return flags, fl[0:3], fl[6:18]
    return result


# --- materials (g3d_anmclr.cpp, g3d_anmtexsrt.cpp, g3d_anmtexpat.cpp, g3d_resmat.cpp) ---------

def bp_reg(r, o):
    return r.u32(o + 1)


def mat_colors(r, mat):
    """chan mat/amb colours as RGBA u32, TEV registers 1-3 (11-bit fields) and konst 0-3
    (8-bit), or None for a register whose display-list command is empty."""
    chan = [r.u32(mat + 0x3EC + 0x14 * c + k) for c in range(2) for k in (4, 8)]
    dl = mat + r.s32(mat + 0x38) + 0x20
    tev = []
    for i in range(3):
        o = dl + 20 * i
        if r.u8(o) == 0:
            tev.append(None)
            continue
        ra, bg = bp_reg(r, o), bp_reg(r, o + 5)
        tev.append([ra & 0x7FF, bg >> 12 & 0x7FF, bg & 0x7FF, ra >> 12 & 0x7FF])
    kon = []
    for i in range(4):
        o = dl + 0x40 + 10 * i
        if r.u8(o) == 0:
            kon.append(None)
            continue
        ra, bg = bp_reg(r, o), bp_reg(r, o + 5)
        kon.append([ra & 0xFF, bg >> 12 & 0xFF, bg & 0xFF, ra >> 12 & 0xFF])
    return chan, tev, kon


def rgba_u32(c):
    return c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3]


def u32_rgba(v):
    return [v >> 24 & 0xFF, v >> 16 & 0xFF, v >> 8 & 0xFF, v & 0xFF]


def color_line(chan, tev, kon):
    out = []
    for v in chan:
        out.append("%08x" % v)
    for t in tev:
        if t is None:
            out.append("0000000000000000")
        else:
            out.append("".join("%04x" % (((x ^ 0x400) - 0x400) & 0xFFFF) for x in t))
    for k in kon:
        out.append("%08x" % (rgba_u32(k) if k is not None else 0))
    return out


def apply_clr(chan, tev, kon, rgba, mask, exist):
    """ApplyClrAnmResult: (c & mask) | rgba; empty TEV registers read as opaque black."""
    chan = list(chan)
    order = {0: 0, 1: 2, 2: 1, 3: 3}  # ClrAnmResult CLR0, CLR1, AMB0, AMB1 -> printed order
    for i in range(4):
        if exist >> i & 1:
            j = order[i]
            chan[j] = (chan[j] & mask[i]) | rgba[i]
    tev = list(tev)
    for k in range(3):
        if exist >> (4 + k) & 1:
            c = rgba_u32([x & 0xFF for x in tev[k]]) if tev[k] is not None else 0x000000FF
            tev[k] = u32_rgba((c & mask[4 + k]) | rgba[4 + k])
    kon = list(kon)
    for k in range(4):
        if exist >> (7 + k) & 1:
            c = rgba_u32(kon[k]) if kon[k] is not None else 0x000000FF
            kon[k] = u32_rgba((c & mask[7 + k]) | rgba[7 + k])
    return chan, tev, kon


def srt_state(r, mat):
    o = mat + 0x1A4
    flag, mode = r.u32(o), r.u32(o + 4)
    srt = [r.floats(o + 8 + 0x14 * i, 5) for i in range(8)]
    return flag, mode, srt


def srt_line(flag, mode, srt):
    out = ["%08x" % flag, "%u" % mode]
    for i in range(8):
        if flag >> (4 * i) & 1:
            out.append(",".join("%08x" % bits(v) for v in srt[i]))
        else:
            out.append("-")
    return out


def tex_image0(tex):
    return ((tex["width"] - 1) & 0x3FF) | ((tex["height"] - 1) & 0x3FF) << 10 | \
        (tex["fmt"] & 0xF) << 20


def pat_state(r, mat, textures):
    """The material's texture maps after ResMat::Bind: [(name, image0) or None] * 8."""
    used = r.u32(mat + 0x3C)
    maps = [None] * 8
    for i in range(8):
        if used >> i & 1:
            maps[i] = ("?", r.u32(mat + 0x40 + 0x20 * i + 8))
    info = mat + r.s32(mat + 0x30)
    for k in range(r.u32(mat + 0x2C)):
        o = info + 0x34 * k
        name = r.cstr(o + r.s32(o)) if r.s32(o) else ""
        mid = r.u32(o + 0x10)
        if name in textures:  # ResTexPlttInfo::Bind initialises and validates the map
            maps[mid] = (name, tex_image0(textures[name]))
    return maps


def pat_line(maps):
    return ["-" if m is None else "%s,%08x" % m for m in maps]


# --- the expected output --------------------------------------------------------------------

def archive_of(path):
    for ext in (".carc/", ".arc/"):
        if ext in path:
            return path[:path.index(ext) + len(ext) - 1]
    return ""


class Brres:
    def __init__(self, path, data):
        self.path, self.data = path, data
        self.r = Data(data)
        parsed = M.parse(data)
        self.models = [Model(self.r, s) for s in parsed["subfiles"]
                       if s["dictionaryName"] == "3DModels(NW4R)"]
        self.textures = {s["resourceName"]: s for s in parsed["subfiles"]
                         if s["dictionaryName"] == "Textures(NW4R)"}
        reader = A.Reader(data)
        self.anims = collections.defaultdict(list)
        for kind, name, block in A.animation_blocks(reader):
            if kind != "SCN0":
                self.anims[kind].append(Anim(path, data, kind, name, block))


def mat_offsets(mdl):
    return [o for _n, o in mdl.mats]


def expected_model(f, mdl, sources, archive_textures, cam, errs):
    """[(RUN fields, [line fields])] for one model, in the harness's order."""
    runs = []
    r = mdl.r
    mats = mat_offsets(mdl)
    textures = dict(archive_textures)
    textures.update(f.textures)

    def matrices(chr_results):
        W, attr = calc_world(mdl, chr_results)
        lines = []
        for i in range(mdl.num_mtx):
            lines.append(["W", str(i), "%08x" % attr[i]] + ["%08x" % bits(v) for v in W[i]])
        for i in range(mdl.num_view):
            v = concat(cam, W[i], array=mdl.num_view > 1)
            lines.append(["V", str(i)] + ["%08x" % bits(x) for x in v])
            if mdl.need_nrm:
                if attr[i] & A_ALL_S_UNIFORM:
                    n = v[0:3] + v[4:7] + v[8:11]
                    lines.append(["N", str(i)] + ["%08x" % bits(x) for x in n])
                else:
                    lines.append(("N~", str(i), v))
        return lines

    runs.append((["BIND", "-", "-", "%08x" % 0], matrices(lambda node: None)))
    chrs = [(s, a) for s in sources for a in s.anims["CHR0"]
            if any(name in mdl.node_by_name for name, _rec in a.records)]
    for src, anm in chrs:
        for frame in harness_frames(anm.num_frame):
            runs.append((["CHR", src.path, anm.name, "%08x" % bits(frame)],
                         matrices(chr_lookup(anm, anm.frame(frame)))))
    if len(chrs) >= 2:
        runs.append((["BLEND", chrs[0][0].path + "+" + chrs[1][0].path,
                      chrs[0][1].name + "+" + chrs[1][1].name, "%08x" % bits(1.5)], None))
    for kind, line in (("CLR0", "C"), ("SRT0", "T"), ("PAT0", "P")):
        for src in sources:
            arc_tex = set(textures)
            for anm in src.anims[kind]:
                recs = {name: rec for name, rec in anm.records}
                if not any(name in mdl.mat_index for name in recs):
                    continue
                for frame in harness_frames(anm.num_frame):
                    fr = anm.frame(frame)
                    lines = []
                    for mi, (mname, mo) in enumerate(mdl.mats):
                        rec = recs.get(mname)
                        if line == "C":
                            chan, tev, kon = mat_colors(r, mo)
                            if rec is not None:
                                v = T.clr_result(anm.r, anm.block, rec, fr)
                                exist = int(v[0], 16)
                                rgba = [int(x.split(":")[0], 16) for x in v[1:]]
                                mask = [int(x.split(":")[1], 16) for x in v[1:]]
                                chan, tev, kon = apply_clr(chan, tev, kon, rgba, mask, exist)
                            lines.append(["C", str(mi)] + color_line(chan, tev, kon))
                        elif line == "T":
                            flag, mode, srt = srt_state(r, mo)
                            if rec is not None:
                                v = T.srt_result(anm.r, anm.block, rec, fr)
                                rflags = int(v[0], 16)
                                for i in range(8):
                                    if rflags >> (4 * i) & 1:
                                        mode = int(v[2])
                                        srt[i] = [from_bits(int(x, 16))
                                                  for x in v[3 + i].split(",")]
                                        mask = 0xF << (4 * i)
                                        flag = (flag & ~mask) | (rflags & mask)
                            lines.append(["T", str(mi)] + srt_line(flag, mode, srt))
                        else:
                            maps = pat_state(r, mo, textures)
                            if rec is not None:
                                names = [t if t in arc_tex else None
                                         for t in anm.parsed["textures"]]
                                v = T.pat_result(anm.r, anm.block, rec, fr, names,
                                                 [None] * len(anm.parsed["palettes"]))
                                btex = int(v[0], 16)
                                for i in range(8):
                                    if btex >> i & 1:
                                        name = v[2 + i].split(",")[0]
                                        maps[i] = (name, tex_image0(textures[name]))
                            lines.append(["P", str(mi)] + pat_line(maps))
                    runs.append(([kind[:3], src.path, anm.name, "%08x" % bits(frame)], lines))
    return runs


def check_close(want_v, got, what, errs):
    """Normal matrix of a non-uniformly scaled matrix: inverse transpose of the view matrix's
    3x3 in double precision, within 1e-5 of each row's largest element. False if the 3x3 is
    singular (MTX34InvTranspose then fails and leaves the normal matrix as it was)."""
    m = [[want_v[4 * r + c] for c in range(3)] for r in range(3)]
    det = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    if det == 0.0:
        return False
    cof = [[m[1][1] * m[2][2] - m[1][2] * m[2][1], m[1][2] * m[2][0] - m[1][0] * m[2][2],
            m[1][0] * m[2][1] - m[1][1] * m[2][0]],
           [m[0][2] * m[2][1] - m[0][1] * m[2][2], m[0][0] * m[2][2] - m[0][2] * m[2][0],
            m[0][1] * m[2][0] - m[0][0] * m[2][1]],
           [m[0][1] * m[1][2] - m[0][2] * m[1][1], m[0][2] * m[1][0] - m[0][0] * m[1][2],
            m[0][0] * m[1][1] - m[0][1] * m[1][0]]]
    have = [from_bits(int(x, 16)) for x in got]
    for r in range(3):
        row = [cof[r][c] / det for c in range(3)]
        tol = 1e-5 * max(abs(x) for x in row)
        for c in range(3):
            if abs(have[3 * r + c] - row[c]) > tol:
                errs.append("%s: N[%d][%d] %r, expected %r" % (what, r, c, have[3 * r + c], row[c]))
                return True
    return True


def check_camera(cam):
    """C_MTXLookAt(pos (30, 120, 400), up (0, 1, 0), target (0, 40, 0)) in double precision."""
    pos, up, tgt = (30.0, 120.0, 400.0), (0.0, 1.0, 0.0), (0.0, 40.0, 0.0)

    def norm(v):
        n = math.sqrt(sum(x * x for x in v))
        return [x / n for x in v]

    def cross(a, b):
        return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    look = norm([p - t for p, t in zip(pos, tgt)])
    right = norm(cross(up, look))
    up2 = cross(look, right)
    want = []
    for row in (right, up2, look):
        want += row + [-sum(a * b for a, b in zip(row, pos))]
    return all(abs(a - b) <= 1e-5 * max(1.0, abs(b)) for a, b in zip(cam, want))


def parse_output(text):
    """{(brres, model): [(RUN fields, [lines])]}, MDL lines, camera."""
    runs = collections.OrderedDict()
    mdls = {}
    cam = None
    cur = None
    for line in text.splitlines():
        f = line.split("\t")
        if f[0] == "CAM":
            cam = [from_bits(int(x, 16)) for x in f[1:]]
        elif f[0] == "MDL":
            mdls[(f[1], f[2])] = f[3:]
            runs.setdefault((f[1], f[2]), [])
        elif f[0] == "RUN":
            cur = (f[3:], [])
            runs.setdefault((f[1], f[2]), []).append(cur)
        else:
            cur[1].append(f)
    return runs, mdls, cam


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--compare", help="the other build's mdl_harness; skipped if missing")
    args = ap.parse_args()

    run = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        print("mdl_harness failed (exit %d)" % run.returncode)
        return 1
    errors = []

    # Regression: deterministic, and the same as the other build.
    again = subprocess.run([args.harness, args.disc], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, text=True)
    if again.stdout != run.stdout:
        errors.append("two runs of the harness differ")
    compared = "not compared"
    if args.compare and os.path.exists(args.compare):
        other = subprocess.run([args.compare, args.disc], stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True)
        name = os.path.basename(os.path.dirname(os.path.abspath(args.compare)))
        if other.stdout != run.stdout:
            errors.append("%s prints something else" % args.compare)
            compared = "differs from " + name
        else:
            compared = "identical to " + name
    got, mdls, cam = parse_output(run.stdout)
    if cam is None or not check_camera(cam):
        errors.append("camera matrix not the expected look-at")

    files = [Brres(p, d) for p, d in walk(args.disc) if d[:4] == b"bres"]
    by_archive = collections.defaultdict(list)
    for f in files:
        by_archive[archive_of(f.path)].append(f)

    counts = collections.Counter()
    for arc, group in by_archive.items():
        archive_textures = {}
        for f in group:
            for name, t in f.textures.items():
                archive_textures.setdefault(name, t)
        for f in group:
            sources = [f] + [o for o in group if o is not f and not o.models]
            for mdl in f.models:
                key = (f.path, mdl.name)
                counts["models"] += 1
                have = got.pop(key, None)
                if have is None:
                    errors.append("%s: %s: not built by the harness" % key)
                    continue
                want = expected_model(f, mdl, sources, archive_textures, cam, errors)
                if [w[0] for w in want] != [h[0] for h in have]:
                    errors.append("%s: %s: runs differ:\n  want %s\n  have %s" % (
                        key + ([w[0] for w in want][:6], [h[0] for h in have][:6])))
                    continue
                last_n = {}
                for (head, wlines), (_h, hlines) in zip(want, have):
                    counts["runs"] += 1
                    counts["run " + head[0]] += 1
                    if wlines is None:  # BLEND: regression only
                        counts["lines (regression)"] += len(hlines)
                        last_n.update((h[1], h) for h in hlines if h[0] == "N")
                        continue
                    if len(wlines) != len(hlines):
                        errors.append("%s: %s: %s: %d lines, expected %d" % (
                            key + (" ".join(head), len(hlines), len(wlines))))
                        continue
                    for w, h in zip(wlines, hlines):
                        what = "%s: %s: %s: %s %s" % (key + (" ".join(head), h[0], h[1]))
                        if w[0] == "N~":
                            counts["N (tolerance)"] += 1
                            if h[0] != "N" or h[1] != w[1]:
                                errors.append(what + ": unexpected line")
                            elif not check_close(w[2], h[2:], what, errors):
                                counts["N (singular)"] += 1
                                if h1 := last_n.get(h[1]):
                                    if h1 != h:
                                        errors.append(what + ": singular, but the normal "
                                                      "matrix changed")
                                else:
                                    counts["lines (regression)"] += 1
                            last_n[h[1]] = h
                            continue
                        if h[0] == "N":
                            last_n[h[1]] = h
                        counts[w[0]] += 1
                        if w != h:
                            errors.append("%s\n  want %s\n  have %s" % (
                                what, "\t".join(w), "\t".join(h)))
    for key in got:
        errors.append("%s: %s: harness built a model that is not on the disc" % key)

    print("world-matrix paths: " + ", ".join("%s %d" % kv for kv in sorted(PATHS.items())))
    for e in errors[:int(os.environ.get("TEST_SCENE_MAX_ERRORS", "25"))]:
        print("ERROR: " + e)
    print("test_scene: %d models, %d runs (%s); W %d, V %d, N %d exact + %d within tolerance "
          "(%d singular: unchanged), C %d, T %d, P %d lines bit for bit, %d regression-only; "
          "64/32: %s; %d errors" % (
              counts["models"], counts["runs"],
              ", ".join("%s %d" % (k[4:], v) for k, v in sorted(counts.items())
                        if k.startswith("run ")),
              counts["W"], counts["V"], counts["N"], counts["N (tolerance)"],
              counts["N (singular)"], counts["C"],
              counts["T"], counts["P"], counts["lines (regression)"], compared, len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
