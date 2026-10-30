#!/usr/bin/env python3
"""The health and safety screen through the game's code and Aurora (port/docs/HOST_BUILD.md §26).

Runs tests/gxlyt_harness (Health_strapArchive from main.dol, lyt::Layout::Build and Draw on
the decomp's GX SDK, the GX stream into Aurora headless on lavapipe) and compares the frame
read back with an independent model built from the files:

  - main.dol .data6 -> Yaz0 -> U8 -> strap.arc (wiiarc.py);
  - the BRLYT pane tree, sizes, transforms, base positions, alphas and vertex colours
    (fmt_layout.py), the material records (TEV colours, texture map and its wrap modes,
    texture SRT, alpha compare; read here from the format) and the TPL images (gxtex.py);
  - a rasteriser of the picture quads: pixel centres mapped into each pane, texture
    coordinates interpolated from the pane's corners, bilinear filtering with clamping
    (GX_LINEAR, texel centres at +0.5), lyt's one-texture TEV stage (C0 + (C1 - C0) * tex,
    colour and alpha), the vertex colours times the inherited pane alpha, and
    SRCALPHA/INVSRCALPHA blending over the clear colour, in depth-first draw order.

Nothing from the disc is written into the repository; the frame and the model go to the
scratch directory (--scratch, outside the repository).
"""

import argparse
import glob
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import fmt_layout  # noqa: E402
import gxtex  # noqa: E402
import wiiarc  # noqa: E402

STRAP_ADDRESS = 0x802E9E60  # Health_strapArchive: the start of .data6
LAYOUT = "strap_00_U.brlyt"
W, H = 608, 456
TOLERANCE = 3  # per channel, 8-bit: TEV lerp/blend rounding and filtering arithmetic


class ModelError(Exception):
    pass


def strap_files(dol_path):
    with open(dol_path, "rb") as f:
        dol = f.read()
    offs = struct.unpack_from(">18I", dol, 0)
    addrs = struct.unpack_from(">18I", dol, 0x48)
    sizes = struct.unpack_from(">18I", dol, 0x90)
    for o, a, s in zip(offs, addrs, sizes):
        if a <= STRAP_ADDRESS < a + s:
            blob = dol[o + STRAP_ADDRESS - a:o + s]
            break
    else:
        raise ModelError("Health_strapArchive not in main.dol")
    outer = dict(wiiarc.u8(wiiarc.yaz0(blob)))
    return dict(wiiarc.u8(wiiarc.unpack(outer["strap.arc"])))


def tpl_image(data):
    count, table = struct.unpack_from(">II", data, 4)
    if count != 1:
        raise ModelError("TPL with %d images" % count)
    header, palette = struct.unpack_from(">II", data, table)
    if palette:
        raise ModelError("palette TPLs are not modelled")
    h, w, fmt, offset, _ws, _wt, min_f, mag_f = struct.unpack_from(">HHIIIIII", data, header)
    if min_f != 1 or mag_f != 1:
        raise ModelError("only GX_LINEAR filtering is modelled")
    rgba = gxtex.decode(fmt, data[offset:], w, h)
    return w, h, fmt, rgba


def material_record(blyt, mat):
    """The parts of a material record the model uses, read from the format."""
    o = mat["offset"]
    res = mat["resNum"]
    tev = [struct.unpack_from(">4h", blyt, o + 0x14 + 8 * i) for i in range(3)]
    for key in ("tevStage", "chanCtrl", "matColor", "blendMode", "indTexStage", "tevSwapTable"):
        if res[key]:
            raise ModelError("%s: %s not modelled" % (mat["name"], key))
    if res["texMap"] != 1 or res["texCoordGen"] != 1:
        raise ModelError("%s: one texture map and texgen expected" % mat["name"])
    p = o + 0x40
    tex_idx, wrap_s, wrap_t = struct.unpack_from(">HBB", blyt, p)
    p += 4 * res["texMap"]
    for _ in range(res["texSRT"]):
        tx, ty, rot, sx, sy = struct.unpack_from(">5f", blyt, p)
        if (tx, ty, rot, sx, sy) != (0.0, 0.0, 0.0, 1.0, 1.0):
            raise ModelError("%s: texture SRT not identity" % mat["name"])
        p += 20
    gen = struct.unpack_from(">4B", blyt, p)
    if gen[0] != 1 or gen[1] != 4 or gen[2] != 30:  # GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY
        raise ModelError("%s: texgen %r not modelled" % (mat["name"], gen))
    p += 4 * res["texCoordGen"]
    alpha = None
    if res["alphaCompare"]:
        comp, op, ref0, ref1 = struct.unpack_from(">4B", blyt, p)
        alpha = (comp & 7, comp >> 4, op, ref0, ref1)
    return {"tev": tev, "tex": tex_idx, "wrap": (wrap_s, wrap_t), "alpha": alpha}


def compare_op(c, a, ref):
    return [False, a < ref, a == ref, a <= ref, a > ref, a != ref, a >= ref, True][c]


def alpha_pass(cfg, a):
    if cfg is None:
        return True
    c0, c1, op, r0, r1 = cfg
    x, y = compare_op(c0, a, r0), compare_op(c1, a, r1)
    return [x and y, x or y, x != y, x == y][op]


def pane_tree(sections):
    """Panes in depth-first order with their parents (pas1/pae1 nesting)."""
    panes, stack, last = [], [], None
    for s in sections:
        k = s["kind"]
        if k in ("pan1", "pic1", "txt1", "wnd1", "bnd1"):
            s = dict(s)
            s["parent"] = stack[-1] if stack else None
            panes.append(s)
            last = s
        elif k == "pas1":
            stack.append(last)
        elif k == "pae1":
            stack.pop()
    return panes


def affine(p):
    """Local 2x3 transform of a pane (translate * rotate z * scale), y up."""
    t, r, sc = p["translate"], p["rotate"], p["scale"]
    if r["x"] or r["y"]:
        raise ModelError("%s: x/y rotation not modelled" % p["name"])
    a = math.radians(r["z"])
    c, s = math.cos(a), math.sin(a)
    return (c * sc["x"], -s * sc["y"], t["x"], s * sc["x"], c * sc["y"], t["y"])


def mul(m, n):
    return (m[0] * n[0] + m[1] * n[3], m[0] * n[1] + m[1] * n[4], m[0] * n[2] + m[1] * n[5] + m[2],
            m[3] * n[0] + m[4] * n[3], m[3] * n[1] + m[4] * n[4], m[3] * n[2] + m[4] * n[5] + m[5])


def expected_frame(files, clear):
    blyt = files["blyt/" + LAYOUT]
    lay = fmt_layout.parse_brlyt(blyt)
    secs = lay["sections"]
    tex_names = [e["name"] for e in next(s for s in secs if s["kind"] == "txl1")["entries"]]
    mats = next(s for s in secs if s["kind"] == "mat1")["materials"]
    frame = [list(clear) for _ in range(W * H)]
    glob_mtx, glob_alpha, visible = {}, {}, {}
    drawn = []
    for p in pane_tree(secs):
        par = p["parent"]
        pm = glob_mtx[id(par)] if par else (1, 0, 0, 0, 1, 0)
        m = mul(pm, affine(p))
        glob_mtx[id(p)] = m
        # alpha: a pane's own alpha, times its parent's when the parent passes it on (flag bit 1)
        inherited = glob_alpha[id(par)] if par and (par["flag"] & 2) else 255
        glob_alpha[id(p)] = p["alpha"] * inherited / 255.0
        visible[id(p)] = bool(p["flag"] & 1) and (visible[id(par)] if par else True)
        if p["kind"] in ("txt1", "wnd1"):
            raise ModelError("%s: %s panes not modelled" % (p["name"], p["kind"]))
        if p["kind"] != "pic1" or not visible[id(p)]:
            continue
        alpha = glob_alpha[id(p)]
        if alpha == 0:
            continue
        mat = material_record(blyt, mats[p["materialIdx"]])
        tw, th, fmt, tex = tpl_image(files["timg/" + tex_names[mat["tex"]]])
        if mat["wrap"] != (0, 0):
            raise ModelError("%s: only GX_CLAMP modelled" % p["name"])
        drawn.append((p["name"], tex_names[mat["tex"]], fmt))
        w, h = p["size"]["x"], p["size"]["y"]
        bx, by = p["basePosition"] % 3, p["basePosition"] // 3
        left = -w / 2 * bx
        top = h / 2 * by
        # pane rectangle corners -> screen pixels (layout y up; screen y down)
        det = m[0] * m[4] - m[1] * m[3]
        corners = [(left, top), (left + w, top), (left, top - h), (left + w, top - h)]
        sx = [m[0] * x + m[1] * y + m[2] + W / 2 for x, y in corners]
        sy = [H / 2 - (m[3] * x + m[4] * y + m[5]) for x, y in corners]
        x0, x1 = max(0, int(math.floor(min(sx)))), min(W, int(math.ceil(max(sx))))
        y0, y1 = max(0, int(math.floor(min(sy)))), min(H, int(math.ceil(max(sy))))
        tc = [(c["x"], c["y"]) for c in p["texCoords"][0:4]]
        cols = [((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255) for v in p["vtxCols"]]
        c0, c1 = mat["tev"][0], mat["tev"][1]
        for py in range(y0, y1):
            ly = H / 2 - (py + 0.5)
            for px in range(x0, x1):
                lx = px + 0.5 - W / 2
                # inverse affine: screen -> pane local
                dx, dy = lx - m[2], ly - m[5]
                ux = (m[4] * dx - m[1] * dy) / det
                uy = (-m[3] * dx + m[0] * dy) / det
                u = (ux - left) / w
                v = (top - uy) / h
                if not (0 <= u < 1 and 0 <= v < 1):
                    continue
                s = (tc[0][0] * (1 - u) + tc[1][0] * u) * (1 - v) + (tc[2][0] * (1 - u) + tc[3][0] * u) * v
                t = (tc[0][1] * (1 - u) + tc[1][1] * u) * (1 - v) + (tc[2][1] * (1 - u) + tc[3][1] * u) * v
                texel = bilinear(tex, tw, th, s, t)
                out = [c0[i] + (c1[i] - c0[i]) * texel[i] / 255.0 for i in range(4)]
                vc = [(cols[0][i] * (1 - u) + cols[1][i] * u) * (1 - v) +
                      (cols[2][i] * (1 - u) + cols[3][i] * u) * v for i in range(4)]
                out = [out[i] * vc[i] / 255.0 for i in range(4)]
                out[3] = out[3] * alpha / 255.0
                a = max(0.0, min(255.0, out[3]))
                if not alpha_pass(mat["alpha"], int(round(a))):
                    continue
                dst = frame[py * W + px]
                for i in range(3):
                    dst[i] = max(0.0, min(255.0, out[i])) * a / 255.0 + dst[i] * (255.0 - a) / 255.0
    return frame, drawn


def bilinear(tex, tw, th, s, t):
    x = s * tw - 0.5
    y = t * th - 0.5
    xi, yi = math.floor(x), math.floor(y)
    fx, fy = x - xi, y - yi
    out = [0.0] * 4
    for oy, wy in ((0, 1 - fy), (1, fy)):
        cy = min(max(yi + oy, 0), th - 1)
        for ox, wx in ((0, 1 - fx), (1, fx)):
            wgt = wx * wy
            if wgt == 0:
                continue
            cx = min(max(xi + ox, 0), tw - 1)
            o = (cy * tw + cx) * 4
            for i in range(4):
                out[i] += tex[o + i] * wgt
    return out


def lavapipe_icd():
    for d in ("/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"):
        hits = sorted(glob.glob(os.path.join(d, "lvp_icd*.json")))
        if hits:
            return hits[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True, help="extracted disc files directory")
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--gpu", action="store_true", help="use the real Vulkan driver, not lavapipe")
    ap.add_argument("--png", action="store_true", help="also write the frame and the model as PNGs")
    args = ap.parse_args()

    env = dict(os.environ)
    if not args.gpu:
        icd = lavapipe_icd()
        if icd is None:
            raise SystemExit("FAIL: lavapipe (mesa-vulkan-drivers) not found")
        env["VK_ICD_FILENAMES"] = icd
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    scratch = os.path.join(args.scratch, "gxlyt")
    data_dir = os.path.join(scratch, "data")
    os.makedirs(data_dir, exist_ok=True)

    files = strap_files(os.path.join(args.disc, "..", "sys", "main.dol"))
    errors = 0
    results = []
    for clear, extra in (((255, 255, 255), []), ((0, 0, 255), ["--clear", "0x0000FF"]),
                         ((255, 255, 255), ["--display-list"])):
        out = os.path.join(scratch, "frame%d.rgba" % len(results))
        cmd = [args.harness, args.disc, out, data_dir] + extra
        p = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if p.returncode != 0:
            sys.stderr.write(p.stderr[-4000:])
            raise SystemExit("FAIL: %s exited %d" % (" ".join([os.path.basename(cmd[0])] + extra), p.returncode))
        summary = p.stdout.strip().splitlines()[-1]
        with open(out, "rb") as f:
            got = f.read()
        if len(got) != W * H * 4:
            raise SystemExit("FAIL: frame size")
        want, drawn = expected_frame(files, clear)
        diffs = []
        for i, px in enumerate(want):
            d = max(abs(got[i * 4 + c] - px[c]) for c in range(3))
            diffs.append(d)
        over = sum(1 for d in diffs if d > TOLERANCE)
        bad = over > 0
        errors += bad
        results.append((summary, extra, max(diffs), sum(diffs) / len(diffs), over, drawn))
        if args.png or bad:
            gxtex.write_png(out[:-5] + ".png", W, H, got)
            model = bytes(b for px in want for b in (int(round(px[0])), int(round(px[1])), int(round(px[2])), 255))
            gxtex.write_png(out[:-5] + "_model.png", W, H, model)

    print("test_gxlyt: " + results[0][0].split(": ", 1)[1])
    print("  pictures drawn: " + ", ".join("%s (%s, format %d)" % d for d in results[0][5]))
    for summary, extra, mx, mean, over, _ in results:
        print("  %s: %d pixels, max diff %d, mean %.3f, %d over %d" % (
            " ".join(extra) or "white clear", W * H, mx, mean, over, TOLERANCE))
    print("errors: %d" % errors)
    print("OK" if errors == 0 else "FAIL")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ModelError as e:
        raise SystemExit("FAIL: model: %s" % e)
