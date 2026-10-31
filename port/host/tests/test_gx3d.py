#!/usr/bin/env python3
"""Checks gx3d_harness: Laser Hockey's 3D scene drawn by the game's g3d and EGG
code through the GX SDK into Aurora (port/docs/HOST_BUILD.md §27).

There is no reference renderer, so the checks are the ones an independent
reading of the disc can make:

  inputs      the harness's camera, projection, viewport and world matrices
              against models of RPGrpCamera's orbit camera, EGG::Frustum's
              perspective projection and g3d's CalcWorld (test_scene.py);
  resolver    every address Aurora resolved while the models were drawn is a
              display list, vertex array or texture image of the model's own
              BRRES (exact start offsets from the file), the model's own
              buffers (ScnMdl's copied materials), or the host's big-endian
              copies of the view matrices; before the draw, only GX/EGG
              static data and the light-object copy;
  log         no Aurora warning or error except the documented ones, and every
              unhandled BP/CP/XF register is in the documented list;
  coverage    the opaque frame (--no-xlu) has the clear colour exactly where no
              opaque triangle (vertices from the file, projected with the
              checked matrices) covers the pixel, and something else where
              one does (pixels next to an edge are not judged);
  depth       drawing the puck before the table gives the same frame;
  materials   pixels where the CPU depth buffer puts an unlit material in
              front: 'black' is black, 'shade_L'/'shade_R' (TEXC x material
              colour white) lie within their 2x2 texel neighbourhood; the
              corner lines have their CLR0 colour's dominant channel;
  determinism two runs give the same bytes; with --gpu-compare, the real GPU's
              frame is within a small tolerance of lavapipe's.

These run without the scene's draw paths (--no-bloom --no-sv). With them, as
RPHkyGame sets them up (§28):

  copies      each EFB copy of a frame against an independent model computed
              from the EFB read back just before it (source rectangle, box
              filter, vertical filter, format channels, RGBA6 precision);
              every copy written back, M0 equal to the newest copy;
  bloom       only adds light against the frame without the paths;
  shadow      a volume beside the puck (--puck-shadow, the table as receiver)
              darkens exactly the table pixels inside it by the shadow alpha;
  arena       no allocation off the main thread but the resolver's own.
"""

import argparse
import glob
import math
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "port", "assets", "tools"))
sys.path.insert(0, HERE)
import gxtex  # noqa: E402
import test_scene as S  # noqa: E402
import wiiarc  # noqa: E402

ARC_PATH = "Common/RPHkyScene/common.carc"
W_EFB, H_EFB = 608, 456
CLEAR = (0xFF, 0x00, 0xFF)

# Aurora log lines that are expected, with the reason (HOST_BUILD.md §27).
ALLOWED_LOG = {
    "Device lost: Device was destroyed.": "wpgx_shutdown destroys the device",
}
# Registers Aurora does not handle that the frame writes (BP / CP / XF): all
# outside what this scene needs.
ALLOWED_UNHANDLED = {
    ("BP", 0x01): "copy anti-aliasing sample pattern (the EFB is not multisampled)",
    ("BP", 0x02): "copy anti-aliasing sample pattern (the EFB is not multisampled)",
    ("BP", 0x03): "copy anti-aliasing sample pattern (the EFB is not multisampled)",
    ("BP", 0x04): "copy anti-aliasing sample pattern (the EFB is not multisampled)",
    ("BP", 0x23): "setup-unit performance counter",
    ("BP", 0x24): "rasteriser performance counter",
    ("BP", 0x44): "field mask (interlaced copies)",
    ("BP", 0x45): "draw done (GXDrawDone is synchronous on the host)",
    ("BP", 0x46): "bus clock",
    ("BP", 0x55): "bounding box reset",
    ("BP", 0x56): "bounding box reset",
    ("BP", 0x58): "PE control bits (GXInit)",
    ("BP", 0x67): "performance counter",
    ("BP", 0x68): "field mode",
    ("BP", 0x69): "bus clock",
    ("CP", 0x20): "CP performance counter select",
    ("XF", 0x0000): "XF error register (GXInit)",
    ("XF", 0x0006): "XF performance counter select",
    ("XF", 0x0012): "dual texture transform enable (always on from GXInit; Aurora always applies the post matrices)",
}


class TestError(Exception):
    pass


# --- Running the harness --------------------------------------------------------------

def lavapipe_icd():
    for d in ("/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"):
        hits = sorted(glob.glob(os.path.join(d, "lvp_icd*.json")))
        if hits:
            return hits[0]
    return None


def gpu_icd():
    for d in ("/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d"):
        hits = sorted(glob.glob(os.path.join(d, "intel_icd*.json")))
        if hits:
            return hits[0]
    return None


def run(harness, disc, scratch, name, icd, *extra):
    env = dict(os.environ)
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    env["VK_ICD_FILENAMES"] = icd
    out = os.path.join(scratch, name + ".rgba")
    data = os.path.join(scratch, "data-" + name)
    os.makedirs(data, exist_ok=True)
    p = subprocess.run([harness, disc, out, data, "--log-level", "0"] + list(extra),
                       env=env, capture_output=True, text=True, timeout=600)
    if p.returncode != 0:
        raise TestError(f"{name}: harness failed ({p.returncode}): {p.stderr[-2000:]}")
    with open(out, "rb") as f:
        rgba = f.read()
    if len(rgba) != W_EFB * H_EFB * 4:
        raise TestError(f"{name}: frame has {len(rgba)} bytes")
    return parse_output(p.stdout), p.stderr, rgba


def parse_output(text):
    o = {"brres": {}, "modelbuf": {}, "static": [], "resolved": [], "world": {}}
    for line in text.splitlines():
        f = line.split("\t")
        if f[0] == "BRRES":
            o["brres"][f[1]] = (int(f[2], 16), int(f[3]))
        elif f[0] == "MODELBUF":
            o["modelbuf"][f[1]] = (int(f[2], 16), int(f[3]))
        elif f[0] == "SCRATCH":
            o["scratch"] = (int(f[1], 16), int(f[2]))
        elif f[0] == "STATIC":
            o["static"].append((int(f[1], 16), int(f[2])))
        elif f[0] == "RESOLVED":
            o["resolved"].append((f[1], int(f[2], 16), int(f[3])))
        elif f[0] in ("CAMERA", "PROJ", "VIEWPORT"):
            o[f[0].lower()] = [float(x) for x in f[1:]]
        elif f[0] == "WORLD":
            o["world"].setdefault(f[1], {})[int(f[2])] = [float(x) for x in f[3:]]
        elif f[0] == "COPY":
            o.setdefault("copies", []).append(dict(
                index=int(f[1]), frame=int(f[2]), address=int(f[3], 16), format=int(f[4]), width=int(f[5]),
                height=int(f[6]), src=tuple(int(x) for x in f[7:11]), half=int(f[11]), clear=int(f[12]),
                depth=int(f[13]), vfilter=int(f[14]), size=int(f[15]), pixel_format=int(f[16]),
                weights=tuple(float(x) for x in f[17:20])))
        elif f[0] in ("COPIES_WRITTEN", "ARENA_OTHER_THREADS"):
            o[f[0].lower()] = int(f[1])
        elif f[0] == "WRITEBACK":
            o["writeback"] = (int(f[1]), int(f[2]))
        elif f[0] == "SHADOWVOL":
            o["shadowvol"] = [float(x) for x in f[1:]]
        elif line.startswith("gx3d_harness: "):
            o["summary"] = line
    return o


# --- The models, from the file ------------------------------------------------------------

def be16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


COMP = {0: ("B", 1), 1: ("b", 1), 2: (">H", 2), 3: (">h", 2), 4: (">f", 4)}


class Mdl:
    """An MDL0 with what the draw reads: materials, shapes, vertex arrays, draw order."""

    def __init__(self, brres, sub):
        self.b = brres
        r = brres.r
        self.m = S.Model(r, sub)
        o = sub["offset"]
        self.o = o

        def dic(field):
            rel = r.s32(o + field)
            return S.dic_entries(r, o + rel) if rel else []
        self.pos = {r.u32(a + 0x10): a for _n, a in dic(0x18)}
        self.nrm = {r.u32(a + 0x10): a for _n, a in dic(0x1C)}
        self.clr = {r.u32(a + 0x10): a for _n, a in dic(0x20)}
        self.tex = {r.u32(a + 0x10): a for _n, a in dic(0x24)}
        self.shapes = [a for _n, a in dic(0x30)]
        self.mat_names = [n for n, _a in self.m.mats]
        self.mat_off = [a for _n, a in self.m.mats]
        self.draws = {"DrawOpa": self.byte_code("DrawOpa"), "DrawXlu": self.byte_code("DrawXlu")}

    def byte_code(self, name):
        if name not in self.m.byte_codes:
            return []
        r, o, out = self.b.r, self.m.byte_codes[name], []
        while r.u8(o) != 1:
            if r.u8(o) != 4:
                raise TestError(f"{name}: byte code {r.u8(o)} not modelled")
            out.append((r.u8(o + 1) << 8 | r.u8(o + 2), r.u8(o + 3) << 8 | r.u8(o + 4)))
            o += 8
        return out

    def tag_dl(self, shp, field):
        r = self.b.r
        tag = shp + field
        return tag + r.s32(tag + 8), r.u32(tag + 4)

    def gp_addresses(self):
        """Every start offset g3d hands the GP from this model: display lists, arrays."""
        r, out = self.b.r, {}
        for name, mat in self.m.mats:
            dl = mat + r.s32(mat + 0x38)
            for k, off in (("pix", 0), ("tevcolor", 0x20), ("indmtx", 0xA0), ("texcoordgen", 0xE0)):
                out[dl + off] = f"material {name} {k} DL"
            tev = mat + r.s32(mat + 0x28)
            out[tev + 0x20] = f"material {name} TEV DL"
        for shp in self.shapes:
            name = r.cstr(shp + r.s32(shp + 0x38))
            out[self.tag_dl(shp, 0x18)[0]] = f"shape {name} pre-primitive DL"
            # ResShp::CallPrePrimitiveDisplayList skips the vertex descriptor part (32 bytes)
            # when the previous shape had the same one
            out[self.tag_dl(shp, 0x18)[0] + 32] = f"shape {name} pre-primitive DL (arrays)"
            out[self.tag_dl(shp, 0x24)[0]] = f"shape {name} primitive DL"
        for kind, d in (("position", self.pos), ("normal", self.nrm), ("colour", self.clr),
                        ("texcoord", self.tex)):
            for i, a in d.items():
                out[a + r.s32(a + 8)] = f"{kind} array {i}"
        return out

    def array(self, kind, idx):
        """Decoded vertex array: list of tuples."""
        r = self.b.r
        a = {"pos": self.pos, "tex": self.tex}[kind][idx]
        cnt, tp, frac, stride = r.u32(a + 0x14), r.u32(a + 0x18), r.u8(a + 0x1C), r.u8(a + 0x1D)
        n = r.u16(a + 0x1E)
        ncomp = (2 + cnt) if kind == "pos" else (1 + cnt)
        fmt, size = COMP[tp]
        base = a + r.s32(a + 8)
        scale = 1.0 if tp == 4 else 1.0 / (1 << frac)
        out = []
        for i in range(n):
            out.append(tuple(struct.unpack_from(fmt, r.d, base + i * stride + c * size)[0] * scale
                             for c in range(ncomp)))
        return out

    def triangles(self, shp):
        """[(pos idx, tex0 idx or None) x 3] from the shape's display lists."""
        r, d = self.b.r, self.b.r.d
        pre, pre_n = self.tag_dl(shp, 0x18)
        cp = {}
        i = pre
        while i < pre + pre_n:
            op = d[i]
            if op == 0x08:
                cp[d[i + 1]] = be32(d, i + 2)
                i += 6
            elif op == 0x10:
                i += 5 + 4 * ((be32(d, i + 1) >> 16) + 1)
            elif op == 0x00:
                i += 1
            else:
                raise TestError(f"pre-primitive DL opcode {op:#x} not modelled")
        lo, hi = cp.get(0x50, 0), cp.get(0x60, 0)
        # Attribute order on the wire: PNMTXIDX, TEX0-7MTXIDX, POS, NRM, CLR0, CLR1, TEX0-7
        attrs = [("pnmtx", lo & 1)] + [(f"texmtx{k}", (lo >> (1 + k)) & 1) for k in range(8)]
        attrs += [("pos", (lo >> 9) & 3), ("nrm", (lo >> 11) & 3), ("clr0", (lo >> 13) & 3),
                  ("clr1", (lo >> 15) & 3)] + [(f"tex{k}", (hi >> (2 * k)) & 3) for k in range(8)]
        for name, t in attrs:
            if t == 1 and not (name == "pnmtx" or name.startswith("texmtx")):
                raise TestError(f"direct {name} not modelled")
        prim, prim_n = self.tag_dl(shp, 0x24)
        tris = []
        i = prim
        while i < prim + prim_n:
            op = d[i]
            if op == 0x00:
                i += 1
            elif op in (0x20, 0x28, 0x30, 0x38):
                i += 5
            elif op == 0x08:
                i += 6
            elif op == 0x10:
                i += 5 + 4 * ((be32(d, i + 1) >> 16) + 1)
            elif op == 0x61:
                i += 5
            elif op & 0x80:
                kind, n = op & 0xF8, be16(d, i + 1)
                i += 3
                verts = []
                for _v in range(n):
                    v = {}
                    for name, t in attrs:
                        if t == 0:
                            continue
                        if t == 1:
                            v[name] = d[i]
                            i += 1
                        elif t == 2:
                            v[name] = d[i]
                            i += 1
                        else:
                            v[name] = be16(d, i)
                            i += 2
                    verts.append((v["pos"], v.get("tex0")))
                if kind == 0x80:
                    for q in range(0, n - 3, 4):
                        tris += [(verts[q], verts[q + 1], verts[q + 2]), (verts[q], verts[q + 2], verts[q + 3])]
                elif kind == 0x90:
                    tris += [tuple(verts[k:k + 3]) for k in range(0, n - 2, 3)]
                elif kind == 0x98:
                    for k in range(n - 2):
                        t = (verts[k], verts[k + 1], verts[k + 2]) if k % 2 == 0 else (verts[k + 1], verts[k], verts[k + 2])
                        tris.append(t)
                elif kind == 0xA0:
                    tris += [(verts[0], verts[k], verts[k + 1]) for k in range(1, n - 1)]
                else:
                    raise TestError(f"primitive {kind:#x} not modelled")
            else:
                raise TestError(f"primitive DL opcode {op:#x} not modelled")
        return tris


def load_models(disc):
    with open(os.path.join(disc, ARC_PATH), "rb") as f:
        files = dict(wiiarc.u8(wiiarc.unpack(f.read())))
    out = {}
    for path, mdl_name in (("G3D/Hck_Field_2.brres", "Hck_Field_2"), ("G3D/Hck_pack.brres", "Hck_pack")):
        b = S.Brres(path, files[path])
        sub = [s for s in S.M.parse(files[path])["subfiles"]
               if s["dictionaryName"] == "3DModels(NW4R)" and s["resourceName"] == mdl_name][0]
        out[mdl_name] = (path, b, Mdl(b, sub))
    return out


# --- Matrices ---------------------------------------------------------------------------------

def mul34(a, b):
    """3x4 x 3x4 (affine)."""
    out = []
    for i in range(3):
        for j in range(4):
            v = sum(a[4 * i + k] * b[4 * k + j] for k in range(3))
            if j == 3:
                v += a[4 * i + 3]
            out.append(v)
    return out


def rot(axis, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    if axis == "X":
        return [1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0]
    if axis == "Y":
        return [c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0]
    return [c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0]


def trans(x, y, z):
    return [1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z]


def expected_camera(target=(0.0, 0.0, 0.0), distance=-745.0, rotation=(-90.0, 0.0, 0.0)):
    """RPGrpCamera::CalcMatrix, orbit type (RPHkyGame::Reset), in double precision."""
    m = trans(-target[0], -target[1], -target[2])
    m = mul34(rot("Y", -rotation[1]), m)
    m = mul34(rot("X", -rotation[0]), m)
    m = mul34(rot("Z", -rotation[2]), m)
    return mul34(trans(0.0, 0.0, distance), m)


def expected_projection(fovy=40.0, width=W_EFB, height=H_EFB, near=10.0, far=100000.0):
    """EGG::Frustum's perspective projection (scale 1, offset 0)."""
    cot = 1.0 / math.tan(math.radians(fovy) / 2)
    inv = -near / (far - near)
    return [0.0, cot / (width / height), 0.0, cot, 0.0, inv, far * inv]


class Projector:
    def __init__(self, cam, proj, vp):
        self.cam, self.p, self.vp = cam, proj, vp

    def __call__(self, world, v):
        x, y, z = v[0], v[1], v[2] if len(v) > 2 else 0.0
        wv = [world[4 * i] * x + world[4 * i + 1] * y + world[4 * i + 2] * z + world[4 * i + 3] for i in range(3)]
        c = self.cam
        e = [c[4 * i] * wv[0] + c[4 * i + 1] * wv[1] + c[4 * i + 2] * wv[2] + c[4 * i + 3] for i in range(3)]
        p = self.p
        w = -e[2]
        cx = p[1] * e[0] + p[2] * e[2]
        cy = p[3] * e[1] + p[4] * e[2]
        cz = p[5] * e[2] + p[6]
        vx, vy, vw, vh = self.vp[0], self.vp[1], self.vp[2], self.vp[3]
        return (vx + vw / 2 * (1 + cx / w), vy + vh / 2 * (1 - cy / w), cz / w, w)


# --- Rasterising (CPU depth buffer) -----------------------------------------------------------

def raster(tris, w, h):
    """tris: (key, [(sx, sy, z, w, uv)]*3, xlu). Per pixel: (z, key, uv) of the nearest
    opaque fragment, a 'tie' flag, and a coverage count including edges within 1 px."""
    depth = [None] * (w * h)
    near_edge = bytearray(w * h)
    for key, v, _xlu in tris:
        (x0, y0, z0, w0, t0), (x1, y1, z1, w1, t1), (x2, y2, z2, w2, t2) = v
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9 or min(w0, w1, w2) <= 0:
            continue
        minx, maxx = max(0, int(math.floor(min(x0, x1, x2))) - 1), min(w - 1, int(math.ceil(max(x0, x1, x2))) + 1)
        miny, maxy = max(0, int(math.floor(min(y0, y1, y2))) - 1), min(h - 1, int(math.ceil(max(y0, y1, y2))) + 1)
        for py in range(miny, maxy + 1):
            cy = py + 0.5
            for px in range(minx, maxx + 1):
                cx = px + 0.5
                b0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area
                b1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area
                b2 = 1.0 - b0 - b1
                # distance of the centre to each edge, in pixels
                e = min(b * abs(area) / math.hypot(xa - xb, ya - yb) if math.hypot(xa - xb, ya - yb) > 0 else 0
                        for b, (xa, ya, xb, yb) in ((b0, (x1, y1, x2, y2)), (b1, (x2, y2, x0, y0)),
                                                    (b2, (x0, y0, x1, y1))))
                if e < -1.5:
                    continue
                i = py * w + px
                if e < 1.5:
                    near_edge[i] = 1
                if e < 0:
                    continue
                z = b0 * z0 + b1 * z1 + b2 * z2
                uv = None
                if t0 is not None:
                    q = b0 / w0 + b1 / w1 + b2 / w2
                    uv = tuple((b0 * t0[k] / w0 + b1 * t1[k] / w1 + b2 * t2[k] / w2) / q for k in range(len(t0)))
                cur = depth[i]
                if cur is None or z < cur[0] - 1e-6:
                    depth[i] = [z, key, uv, False]
                elif z <= cur[0] + 1e-6 and cur[1][0] != key[0]:
                    cur[3] = True  # two materials at the same depth: not judged
    return depth, near_edge


# --- Textures --------------------------------------------------------------------------------------

def texture(brres, name):
    t = brres.textures[name]
    o = t["offset"] + t["toTexData"]
    fmt = gxtex._normalize_format(t["fmt"], gxtex.FORMATS, "fmt")
    n = gxtex.image_size(fmt, t["width"], t["height"])
    return t["width"], t["height"], gxtex.decode(t["fmt"], brres.data[o:o + n], t["width"], t["height"])


def texel_range(tex, u, v):
    w, h, rgba = tex
    fx, fy = u * w - 0.5, v * h - 0.5
    x0, y0 = int(math.floor(fx)), int(math.floor(fy))
    lo, hi = [255] * 3, [0] * 3
    for y in (y0, y0 + 1):
        for x in (x0, x0 + 1):
            xi, yi = min(max(x, 0), w - 1), min(max(y, 0), h - 1)
            p = rgba[4 * (yi * w + xi):4 * (yi * w + xi) + 3]
            for c in range(3):
                lo[c], hi[c] = min(lo[c], p[c]), max(hi[c], p[c])
    return lo, hi


# --- The checks -------------------------------------------------------------------------------------

def check_inputs(o, models, errs):
    cam = expected_camera()
    if max(abs(a - b) for a, b in zip(o["camera"], cam)) > 1e-4:
        errs.append(f"camera {o['camera']} != RPGrpCamera orbit model {cam}")
    proj = expected_projection()
    for i, (a, b) in enumerate(zip(o["proj"], proj)):
        if abs(a - b) > 1e-6 * max(1.0, abs(b)):
            errs.append(f"projection[{i}] {a} != EGG::Frustum model {b}")
    if o["viewport"] != [0.0, 0.0, float(W_EFB), float(H_EFB), 0.0, 1.0]:
        errs.append(f"viewport {o['viewport']}")
    for name, (_p, _b, mdl) in models.items():
        want, _attr = S.calc_world(mdl.m, lambda node: None)
        got = o["world"].get(name, {})
        for i in range(mdl.m.num_view):
            if i not in got or max(abs(a - b) for a, b in zip(got[i], want[i])) > 1e-5:
                errs.append(f"{name}: world matrix {i} {got.get(i)} != CalcWorld model {want[i]}")


def check_resolver(o, models, errs):
    known = {}
    for name, (path, _b, mdl) in models.items():
        base, size = o["brres"][path]
        for off, what in mdl.gp_addresses().items():
            known[base + off] = f"{path}: {what}"
    for path, (base, size) in o["brres"].items():
        b = [m for m in models.values() if m[0] == path]
        if b:
            for tname, t in b[0][1].textures.items():
                known[base + t["offset"] + t["toTexData"]] = f"{path}: texture {tname}"
    statics = o["static"]
    sbase, ssize = o["scratch"]
    counts = {}
    used = set()
    for phase, addr, ok in o["resolved"]:
        if not ok:
            errs.append(f"{phase}: address {addr:08x} did not resolve")
            continue
        what = None
        if addr in known:
            what = "model data"
            used.add(addr)
        elif any(b <= addr < b + s for b, s in o["brres"].values()):
            where = [f"{p} +{addr - b:#x}" for p, (b, s) in o["brres"].items() if b <= addr < b + s][0]
            errs.append(f"{phase}: {addr:08x} ({where}) is inside a BRRES but not a display list, array or image start")
            continue
        elif any(b <= addr < b + s for b, s in o["modelbuf"].values()):
            what = "model buffers"
        elif sbase <= addr < sbase + ssize:
            what = "host big-endian copies"
        elif any(b <= addr < b + s for b, s in statics):
            what = "GX/EGG static data"
        if what is None:
            errs.append(f"{phase}: {addr:08x} is outside the model's data")
            continue
        if phase == "draw" and what == "GX/EGG static data":
            # only StateGX's null texture, on the texture maps the draw does not use
            if not any(b == addr and s == 16 for b, s in statics):
                errs.append(f"draw: {addr:08x} is static data other than StateGX's null texture")
        if phase == "setup" and what == "model data":
            errs.append(f"setup: model data {addr:08x} resolved outside the draw")
        counts[(phase, what)] = counts.get((phase, what), 0) + 1
    # every shape's display lists and every texture of a drawn material were used
    for name, (path, b, mdl) in models.items():
        base = o["brres"][path][0]
        for shp in mdl.shapes:
            a = base + mdl.tag_dl(shp, 0x24)[0]
            if a not in used:
                errs.append(f"{name}: primitive DL {a:08x} never called")
    return counts


def check_log(err, errs):
    allowed, unhandled = {}, []
    for line in err.splitlines():
        m = re.match(r"\[aurora (\w+) [^\]]*\] (.*)", line)
        if not m:
            continue
        level, msg = m.groups()
        u = re.match(r"Unhandled (BP|CP|XF) register 0x([0-9A-F]+)", msg)
        if u:
            key = (u.group(1), int(u.group(2), 16))
            unhandled.append(key)
            if key not in ALLOWED_UNHANDLED:
                errs.append(f"Aurora: {msg}")
            continue
        if re.match(r"(Unhandled XF memory|Unimplemented|Unresolved)", msg):
            errs.append(f"Aurora: {msg}")
            continue
        if level in ("WARN", "ERROR", "FATAL"):
            if msg in ALLOWED_LOG:
                allowed[msg] = allowed.get(msg, 0) + 1
            else:
                errs.append(f"Aurora {level}: {msg}")
    return allowed, sorted(unhandled)


def scene_triangles(o, models, projector):
    tris = []
    stats = {}
    for name, (_path, b, mdl) in models.items():
        world = o["world"][name]
        pos_cache, tex_cache = {}, {}
        r = b.r
        for kind in ("DrawOpa", "DrawXlu"):
            for mat, shp_i in mdl.draws[kind]:
                shp = mdl.shapes[shp_i]
                mtx = r.s32(shp + 0x08)
                if mtx < 0:
                    raise TestError(f"{name}: envelope shapes not modelled")
                pid, tid = r.s16(shp + 0x48), r.s16(shp + 0x50)
                if pid not in pos_cache:
                    pos_cache[pid] = mdl.array("pos", pid)
                if tid >= 0 and tid not in tex_cache:
                    tex_cache[tid] = mdl.array("tex", tid)
                pos = pos_cache[pid]
                tc = tex_cache.get(tid)
                for t in mdl.triangles(shp):
                    v = []
                    for pi, ti in t:
                        sx, sy, z, w = projector(world[mtx], pos[pi])
                        v.append((sx, sy, z, w, tc[ti] if tc is not None and ti is not None else None))
                    tris.append(((name + ":" + mdl.mat_names[mat], shp_i), v, kind == "DrawXlu"))
                    stats[kind] = stats.get(kind, 0) + 1
    return tris, stats


def px(rgba, i):
    return rgba[4 * i], rgba[4 * i + 1], rgba[4 * i + 2]


def check_coverage(opa, depth, near_edge, errs):
    judged = bad_cov = bad_empty = empty = 0
    for i in range(W_EFB * H_EFB):
        if near_edge[i]:
            continue
        judged += 1
        empty += depth[i] is None
        is_clear = px(opa, i) == CLEAR
        if depth[i] is None and not is_clear:
            bad_empty += 1
        elif depth[i] is not None and is_clear:
            bad_cov += 1
    if bad_cov or bad_empty:
        errs.append(f"coverage: {bad_cov} covered pixels show the clear colour, {bad_empty} uncovered pixels do not")
    return judged, empty


def interior(depth, near_edge, i, key):
    x, y = i % W_EFB, i // W_EFB
    if x == 0 or y == 0 or x == W_EFB - 1 or y == H_EFB - 1:
        return False
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            j = i + dy * W_EFB + dx
            if (near_edge is not None and near_edge[j]) or depth[j] is None or depth[j][1][0] != key \
                    or depth[j][3]:
                return False
    return True


def check_materials(opa, depth, near_edge, models, errs):
    field_b = models["Hck_Field_2"][1]
    tex = {n: texture(field_b, n) for n in ("shade_43_1", "shade_43_2")}
    shade_tex = {"Hck_Field_2:shade_L": tex["shade_43_1"], "Hck_Field_2:shade_R": tex["shade_43_2"]}
    hue = {"Hck_Field_2:line1": 0, "Hck_Field_2:line4": 2, "Hck_Field_2:line6": 1}
    res = {}
    sums = {}
    for i in range(W_EFB * H_EFB):
        d = depth[i]
        if d is None or d[3]:
            continue
        key = d[1][0]
        if key not in ("Hck_Field_2:black",) and key not in shade_tex and key not in hue:
            continue
        if not interior(depth, None if key in hue else near_edge, i, key):
            continue
        p = px(opa, i)
        st = res.setdefault(key, [0, 0, 0])
        st[0] += 1
        if key == "Hck_Field_2:black":
            if max(p) > 2:
                st[1] += 1
        elif key in shade_tex:
            u, v = d[2]
            if not (0.002 < u < 0.998 and 0.002 < v < 0.998):
                st[0] -= 1
                continue
            lo, hi = texel_range(shade_tex[key], u, v)
            off = max(max(lo[c] - p[c], p[c] - hi[c], 0) for c in range(3))
            st[2] = max(st[2], off)
            if off > 5:
                st[1] += 1
        else:
            s = sums.setdefault(key, [0, 0, 0])
            for c in range(3):
                s[c] += p[c]
    for key, (n, bad, maxoff) in sorted(res.items()):
        if n == 0:
            errs.append(f"{key}: no pixel to judge")
        if bad:
            errs.append(f"{key}: {bad} of {n} pixels outside the model (max {maxoff} off)")
    for key, ch in hue.items():
        s = sums.get(key)
        if s is None or s[ch] <= max(s[c] for c in range(3) if c != ch):
            errs.append(f"{key}: dominant channel is not {'RGB'[ch]} ({s})")
    return res, sums


def compare(a, b):
    diff = [abs(x - y) for k, (x, y) in enumerate(zip(a, b)) if k % 4 != 3]
    return max(diff), sum(diff) / len(diff), sum(1 for x in diff if x > 8)


# --- Render to texture (milestone 3b, HOST_BUILD.md §28) ------------------------------------------

# GX copy formats the model knows: decoder name for gxtex and what the copy keeps
COPY_FORMATS = {6: ("RGBA8", "rgba"), 3: ("IA8", "ia"), 1: ("I8", "i"), 0x27: ("I8", "a"), 0x28: ("I8", "r")}


def q6(v):
    """An 8-bit value at the RGBA6 EFB's precision (6 bits, replicated)."""
    return (v & 0xFC) | (v >> 6)


def copy_model(c, efb, ew):
    """Independent model of an EFB copy (GXCopyTex) from the EFB contents before it: the source rectangle,
    box filter for half-size copies, the vertical filter, then the format's channels. Returns per texel a
    tuple in the decoder's RGBA order."""
    sl, st, sw, sh = c["src"]
    has_alpha = c["pixel_format"] == 1  # RGBA6_Z24
    kind = COPY_FORMATS[c["format"]][1]
    raw = kind in ("rgba", "a", "r")

    def src(x, y):
        x = min(max(x, sl), sl + sw - 1)
        y = min(max(y, st), st + sh - 1)  # copies are clamped top and bottom
        o = 4 * (y * ew + x)
        return (efb[o], efb[o + 1], efb[o + 2], efb[o + 3] if has_alpha else 255)

    s = 2 if c["half"] else 1

    def box(x, y):
        vals = [src(sl + s * x + i, st + s * y + j) for j in range(s) for i in range(s)]
        return [sum(v[k] for v in vals) / len(vals) for k in range(4)]

    w0, w1, w2 = c["weights"] if c["vfilter"] else (0.0, 1.0, 0.0)
    out = []
    for y in range(c["height"]):
        rows = [box(x, y) for x in range(c["width"])]
        if c["vfilter"]:
            up = [box(x, max(y - 1, 0)) for x in range(c["width"])]
            dn = [box(x, min(y + 1, c["height"] - 1)) for x in range(c["width"])]
            rows = [[min(255.0, w0 * a[k] + w1 * b[k] + w2 * d[k]) for k in range(4)] for a, b, d in zip(up, rows, dn)]
        for r, g, b, a in rows:
            if kind == "rgba":
                t = (r, g, b, a)
            elif kind == "ia":
                i = min(255.0, 0.257 * r + 0.504 * g + 0.098 * b + 16.0)
                t = (i, i, i, a)
            elif kind == "i":
                i = min(255.0, 0.257 * r + 0.504 * g + 0.098 * b + 16.0)
                t = (i, i, i, i)
            elif kind == "a":
                t = (a, a, a, a)
            else:
                t = (r, r, r, r)
            out.append(t)
    return out, raw and has_alpha


def texel_error(got, want, quantized):
    """|got - want| for one channel; with the RGBA6 EFB's precision, 0 when got is the 6-bit value of an 8-bit
    value within 1 of the model's (the GPU filters with 8-bit weights)."""
    if quantized and got in {q6(k) for k in range(max(0, int(math.floor(want)) - 1), min(255, int(math.ceil(want)) + 1) + 1)}:
        return 0
    return abs(got - want)


def check_copies(o, copy_dir, errs, frame=1):
    """Every colour copy of `frame` against its model from the EFB read back just before it."""
    results = []
    for c in o.get("copies", []):
        if c["frame"] != frame or c["depth"]:
            continue
        if c["format"] not in COPY_FORMATS:
            errs.append(f"copies: copy {c['index']} has format 0x{c['format']:x}, not modelled")
            continue
        efbs = glob.glob(os.path.join(copy_dir, f"efb{c['index']:03d}_*.rgba"))
        data_path = os.path.join(copy_dir, f"copy{c['index']:03d}.bin")
        if not efbs or not os.path.exists(data_path):
            errs.append(f"copies: copy {c['index']}: no source or data dump")
            continue
        ew, eh = (int(v) for v in re.search(r"_(\d+)x(\d+)\.rgba$", efbs[0]).groups())
        with open(efbs[0], "rb") as f:
            efb = f.read()
        with open(data_path, "rb") as f:
            data = f.read()
        if len(data) != c["size"] or c["size"] != gxtex.image_size(COPY_FORMATS[c["format"]][0], c["width"], c["height"]):
            errs.append(f"copies: copy {c['index']}: {len(data)} bytes, expected the format's size")
            continue
        got = gxtex.decode(COPY_FORMATS[c["format"]][0], data, c["width"], c["height"])
        want, quantized = copy_model(c, efb, ew)
        worst, over2, over4 = 0, 0, 0
        for i, t in enumerate(want):
            d = max(texel_error(got[4 * i + k], t[k], quantized) for k in range(4))
            worst = max(worst, d)
            over2 += d > 2
            over4 += d > 4
        worst = int(math.ceil(worst))
        results.append((c, worst, over2, len(want)))
        if over2:
                errs.append(f"copies: copy {c['index']} (format 0x{c['format']:x}, {c['width']}x{c['height']}): "
                        f"max diff {worst}, {over2} texels over 2, {over4} over 4")
    return results


def check_shadow(o, ref, shadowed, models, errs):
    """The shadow volume path as the game runs it, with bloom (--puck-shadow vs --table-group1, no translucent
    pass, no puck): the
    pixels whose table surface lies inside the volume (a 32-sided prism, RP_drawShadowVolume's cylinder) are
    darkened by the shadow colour's alpha (0xC0: x 63/255), every other pixel is unchanged."""
    m = o["shadowvol"]
    sx, sy, sz, tx, ty, tz = m[0], m[5], m[10], m[3], m[7], m[11]
    projector = Projector(o["camera"], o["proj"], o["viewport"])
    path, b, mdl = models["Hck_Field_2"]
    world = o["world"]["Hck_Field_2"]
    r = b.r
    tris = []
    for kind in ("DrawOpa",):
        for mat, shp_i in mdl.draws[kind]:
            shp = mdl.shapes[shp_i]
            mtx = r.s32(shp + 0x08)
            pos = mdl.array("pos", r.s16(shp + 0x48))
            wm = world[mtx]
            for t in mdl.triangles(shp):
                v = []
                for pi, _ti in t:
                    p = pos[pi]
                    wp = tuple(wm[4 * k] * p[0] + wm[4 * k + 1] * p[1] + wm[4 * k + 2] * p[2] + wm[4 * k + 3]
                               for k in range(3))
                    sxp, syp, zp, wv = projector(wm, p)
                    v.append((sxp, syp, zp, wv, wp))
                tris.append(((mdl.mat_names[mat], shp_i), v, False))
    depth, near_edge = raster(tris, W_EFB, H_EFB)
    n = 32
    edges = [(0.5 * math.sin(2 * math.pi * i / n), 0.5 * math.cos(2 * math.pi * i / n)) for i in range(n + 1)]
    (ax, az), (bx, bz) = edges[0], edges[1]
    orient = 1.0 if (bx - ax) * (0 - az) - (bz - az) * (0 - ax) > 0 else -1.0  # the centre is inside
    inside = outside = bad_in = bad_out = 0
    worst = 0
    for i in range(W_EFB * H_EFB):
        if depth[i] is None or near_edge[i]:
            continue
        x, y, z = depth[i][2]
        lx, ly, lz = (x - tx) / sx, (y - ty) / sy, (z - tz) / sz
        # signed distance (world units) to the prism's side: polygon edges, counter-clockwise or not
        dist = min(orient * ((ex1 - ex0) * (lz - ez0) - (ez1 - ez0) * (lx - ex0)) / math.hypot(ex1 - ex0, ez1 - ez0)
                   for (ex0, ez0), (ex1, ez1) in zip(edges, edges[1:]))
        side = min(dist * sx, (0.5 - abs(ly)) * sy)
        if abs(side) < 2.5:
            continue  # within ~2 px of the volume's outline: blurred by the copy's vertical filter
        a = ref[4 * i:4 * i + 3]
        g = shadowed[4 * i:4 * i + 3]
        if side > 0:
            inside += 1
            d = max(abs(g[k] - a[k] * 63 / 255) for k in range(3))
            worst = max(worst, d)
            bad_in += d > 5
        else:
            outside += 1
            bad_out += g != a
    if inside == 0:
        errs.append("shadow: the volume covers no table pixel")
    if bad_in or bad_out:
        errs.append(f"shadow: {bad_in} of {inside} pixels inside not darkened to 63/255, "
                    f"{bad_out} of {outside} outside changed")
    return inside, outside, worst


def check_bloom(base, bloom, errs):
    """Bloom only adds light (the composite is additive): no pixel darker than the frame without it beyond the
    RGBA6 EFB's precision, and some pixels brighter."""
    darker = brighter = 0
    for i in range(0, len(base), 4):
        d = [bloom[i + k] - base[i + k] for k in range(3)]
        darker += min(d) < -4
        brighter += max(d) > 4
    if darker or not brighter:
        errs.append(f"bloom: {darker} pixels darker than without bloom, {brighter} brighter")
    return darker, brighter


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--gpu-compare", action="store_true", help="also run on the Intel GPU and compare")
    ap.add_argument("--keep-png", action="store_true", help="write PNGs of the frames into --scratch")
    args = ap.parse_args()
    os.makedirs(args.scratch, exist_ok=True)
    icd = lavapipe_icd()
    if icd is None:
        raise SystemExit("FAIL: lavapipe (lvp_icd*.json) not found")
    errs = []

    # Milestone 3a's checks on the scene without its draw paths (--no-bloom --no-sv); the paths are checked below
    base = ("--no-bloom", "--no-sv")
    o, err, full = run(args.harness, args.disc, args.scratch, "base", icd, *base)
    _o2, _e2, full2 = run(args.harness, args.disc, args.scratch, "base2", icd, *base)
    oo, err_opa, opa = run(args.harness, args.disc, args.scratch, "opa", icd, "--no-xlu", *base)
    _o3, _e3, first = run(args.harness, args.disc, args.scratch, "puckfirst", icd, "--puck-first", *base)
    models = load_models(args.disc)

    check_inputs(o, models, errs)
    counts = check_resolver(o, models, errs)
    allowed, unhandled = check_log(err, errs)
    check_log(err_opa, errs)
    if full != full2:
        errs.append("determinism: two runs differ")
    if first != full:
        mx, mean, n = compare(first, full)
        errs.append(f"depth: drawing the puck first changes the frame (max {mx}, {n} channels over 8)")

    projector = Projector(o["camera"], o["proj"], o["viewport"])
    tris, stats = scene_triangles(o, models, projector)
    depth, near_edge = raster([t for t in tris if not t[2]], W_EFB, H_EFB)
    judged, empty = check_coverage(opa, depth, near_edge, errs)
    res, sums = check_materials(opa, depth, near_edge, models, errs)

    # The full frame: bloom and the shadow volume path as RPHkyGame sets them up
    copy_dir = os.path.join(args.scratch, "copies")
    shutil.rmtree(copy_dir, ignore_errors=True)
    os.makedirs(copy_dir)
    fo, ferr, frame = run(args.harness, args.disc, args.scratch, "full", icd, "--capture-sources", "--copy-dir", copy_dir)
    _fo2, _fe2, frame2 = run(args.harness, args.disc, args.scratch, "full2", icd)
    check_log(ferr, errs)
    if frame != frame2:
        errs.append("full frame: a run without --capture-sources differs (source capture must not change it)")
    copies = fo.get("copies", [])
    if fo.get("copies_written") != len(copies) or not copies:
        errs.append(f"copies: {fo.get('copies_written')} written back, {len(copies)} reported")
    wb_ok, wb_bad = fo.get("writeback", (0, -1))
    if wb_bad or not wb_ok:
        errs.append(f"writeback: {wb_bad} destinations in M0 differ from their newest copy ({wb_ok} equal)")
    # The only arena allocations off the main thread are the harness resolver's own map entries (it runs on
    # Aurora's FIFO thread); GPU drivers allocated thousands before §28
    if fo.get("arena_other_threads", 1 << 30) > len(fo["resolved"]):
        errs.append(f"arena: {fo.get('arena_other_threads')} allocations from other threads, "
                    f"at most {len(fo['resolved'])} expected (the resolver's own bookkeeping)")
    copy_results = check_copies(fo, copy_dir, errs)
    darker, brighter = check_bloom(full, frame, errs)
    shadow_base = ("--no-xlu", "--no-puck")
    so, _se, sref = run(args.harness, args.disc, args.scratch, "shadowref", icd, "--table-group1", *shadow_base)
    so2, _se2, sframe = run(args.harness, args.disc, args.scratch, "shadow", icd, "--puck-shadow", *shadow_base)
    inside, outside, sworst = check_shadow(so2, sref, sframe, models, errs)

    gpu = None
    if args.gpu_compare:
        g = gpu_icd()
        if g is None:
            gpu = "no Intel Vulkan driver"
        else:
            go, _ge, gbase = run(args.harness, args.disc, args.scratch, "gpu", g, *base)
            mx, mean, n = compare(gbase, full)
            gpu = f"{go.get('summary', '').split(' on ')[-1].split(';')[0]}: max {mx}, mean {mean:.3f}, {n} channels over 8"
            if mean > 1.0 or n > W_EFB * H_EFB * 3 // 200:
                errs.append(f"GPU vs lavapipe: {gpu}")
            # The full frame: the bloom's blurs and filtered copies amplify the drivers' filtering precision
            _go2, _ge2, gfull = run(args.harness, args.disc, args.scratch, "gpufull", g)
            mx, mean, n = compare(gfull, frame)
            gpu += f"; full frame: max {mx}, mean {mean:.3f}, {n} channels over 8"
            if mean > 2.0:
                errs.append(f"GPU vs lavapipe (full frame): max {mx}, mean {mean:.3f}")

    if args.keep_png:
        for name, rgba in (("full", frame), ("base", full), ("opa", opa), ("shadow", sframe), ("shadowref", sref)):
            d = bytearray(rgba)
            d[3::4] = b"\xff" * (W_EFB * H_EFB)
            gxtex.write_png(os.path.join(args.scratch, name + ".png"), W_EFB, H_EFB, bytes(d))

    print(f"test_gx3d: {o.get('summary', '?').split('gx3d_harness: ')[-1]}")
    print(f"  inputs: camera, projection (fovy 40, 608x456, near 10, far 100000), viewport, "
          f"{sum(len(v) for v in o['world'].values())} world matrices match the models")
    print("  resolver: " + ", ".join(f"{p} {w} {n}" for (p, w), n in sorted(counts.items())))
    print(f"  log: {sum(allowed.values())} allowed warnings ({', '.join(sorted(allowed))}); "
          f"{len(unhandled)} unhandled registers, all documented")
    print(f"  geometry: {stats.get('DrawOpa', 0)} opaque + {stats.get('DrawXlu', 0)} translucent triangles; "
          f"coverage judged on {judged} pixels ({empty} uncovered); puck-first frame identical: {first == full}")
    print("  materials: " + "; ".join(f"{k.split(':')[1]} {n} px" + (f" (max {m} off)" if m else "")
                                      for k, (n, _b, m) in sorted(res.items()))
          + "; hue: " + ", ".join(f"{k.split(':')[1]} {tuple(v // max(1, res.get(k, [1])[0]) for v in s)}"
                                for k, s in sorted(sums.items())))
    print(f"  determinism: two runs identical: {full == full2}" + (f"; GPU {gpu}" if gpu else ""))
    print(f"  full frame (bloom, shadow volume): {len(copies)} EFB copies in 2 frames, all written back "
          f"({wb_ok} destinations equal M0); arena allocations from other threads: {fo.get('arena_other_threads')} "
          f"(the resolver's)")
    print("  copies (frame 1) vs model from the EFB before each: " + "; ".join(
        f"#{c['index']} 0x{c['format']:x} {c['width']}x{c['height']}{' half' if c['half'] else ''}"
        f"{' vf' if c['vfilter'] else ''} max {w}" for c, w, _o, _n in copy_results))
    print(f"  bloom (full frame vs without the draw paths): {brighter} pixels brighter, {darker} darker")
    print(f"  shadow volume: {inside} table pixels inside darkened (max diff {sworst:.1f}), {outside} outside unchanged")
    for e in errs[:40]:
        print("ERROR:", e)
    print(f"errors: {len(errs)}")
    if errs:
        sys.exit(1)
    print("OK")


if __name__ == "__main__":
    main()
