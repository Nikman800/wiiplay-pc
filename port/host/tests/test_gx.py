#!/usr/bin/env python3
"""GX bring-up test on Aurora (port/docs/HOST_BUILD.md §25).

Runs gx_harness (headless, on lavapipe unless --gpu), then checks the frame it
read back against an independent model:

  * the inputs: the texture parameters and display-list bytes the harness
    submitted (found through the port's loaders) must be the ones this script
    finds itself in banner.tpl and fsh_lake.brres;
  * the top quad: the texture decoded by port/assets/tools/gxtex.py, GX_NEAR at
    an exact 3x scale, blended over the clear colour by its alpha;
  * the bottom quad: the same texels through the material's TEV stages, alpha
    compare and blend mode, decoded here from the BP commands of its display
    lists (a small TEV model; anything it does not model is an error);
  * the background: the clear colour, exactly.

Then the pipeline cache (Aurora patch: disableShaderCache/pipelineCacheKey):
no cache files without --disk-cache, the same frame with a cache, and a
changed key discards the cached pipelines. Nothing may be written to the
per-user data directory (~/.local/share/wiiplay). No image is stored anywhere
but the scratch directory.
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "assets", "tools"))
import gxtex  # noqa: E402
import wiiarc  # noqa: E402

W, H = 640, 480
CLEAR = (0x1E, 0x1E, 0x3C)
TOP = (32, 32, 32 + 576, 32 + 192)
BOTTOM = (32, 256, 32 + 576, 256 + 192)
TOLERANCE = 2  # per channel; the TEV model is float maths, as are Aurora's shaders

TPL_PATH = "banner.tpl"
ARC_PATH = "Common/RPFshScene/common.carc"
BRRES_PATH = "G3D/fsh_lake.brres"
MAT_NAME = "a_a_lake_mat_v"


class ModelError(Exception):
    pass


# --- Independent parse of the disc files -------------------------------------------

def tpl_image0(data):
    count, table = struct.unpack_from(">II", data, 4)
    if count < 1:
        raise ModelError("banner.tpl has no images")
    header, _palette = struct.unpack_from(">II", data, table)
    height, width, fmt, offset = struct.unpack_from(">HHII", data, header)
    size = gxtex.image_size(gxtex._normalize_format(fmt, gxtex.FORMATS, "format"), width, height)
    return width, height, fmt, offset, data[offset:offset + size]


def s32(d, o):
    return struct.unpack_from(">i", d, o)[0]


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def cstr(d, o):
    return d[o:d.index(b"\0", o)].decode("ascii")


def find_material(brres, name):
    """(pix DL, TEV colour DL, TEV DL, nStages) of a material, by its MDL0 dictionary."""
    o = brres.find(b"MDL0")
    while o >= 0:
        dic = o + s32(brres, o + 0x28)  # ResMdlData::toResMatDic
        if 0 < dic < len(brres) - 8:
            n = u32(brres, dic + 4)
            for i in range(1, n + 1):
                e = dic + 8 + 16 * i
                if not (0 < e + 16 <= len(brres)):
                    break
                ofs_name, ofs_data = s32(brres, e + 8), s32(brres, e + 12)
                if cstr(brres, dic + ofs_name) != name:
                    continue
                mat = dic + ofs_data
                tev = mat + s32(brres, mat + 0x28)      # toResTevData
                dl = mat + s32(brres, mat + 0x38)       # toResMatDLData
                return (brres[dl:dl + 0x20], brres[dl + 0x20:dl + 0xA0],
                        brres[tev + 0x20:tev + 0x200], brres[tev + 0xC])
        o = brres.find(b"MDL0", o + 4)
    raise ModelError(f"material {name} not found")


def load_inputs(disc):
    with open(os.path.join(disc, TPL_PATH), "rb") as f:
        tpl = f.read()
    with open(os.path.join(disc, ARC_PATH), "rb") as f:
        arc = wiiarc.unpack(f.read())
    files = dict(wiiarc.u8(arc))
    if BRRES_PATH not in files:
        raise ModelError(f"{BRRES_PATH} not in {ARC_PATH}")
    return tpl_image0(tpl), find_material(files[BRRES_PATH], MAT_NAME)


# --- BP state from display lists ------------------------------------------------------

class BP:
    """BP register file as GXInit leaves it for what the model reads, plus the DLs."""

    def __init__(self):
        self.reg = {}
        self.mask = 0xFFFFFF
        # Swap tables identity (GXInit), indirect off, colour/alpha update on.
        for i in range(4):
            self.reg[0xF6 + 2 * i] = 0x0 | (1 << 2)
            self.reg[0xF7 + 2 * i] = 0x2 | (3 << 2)
        self.reg[0x41] = (1 << 3) | (1 << 4)

    def write(self, r, v):
        if r == 0xFE:
            self.mask = v
            return
        old = self.reg.get(r, 0)
        self.reg[r] = (old & ~self.mask & 0xFFFFFF) | (v & self.mask)
        self.mask = 0xFFFFFF
        if r in range(0xE0, 0xE8):  # TEV registers: konst and colour live apart
            kind = "k" if v & 0x800000 else "c"
            self.reg[(kind, r)] = self.reg[r]

    def run(self, dl):
        i = 0
        while i < len(dl):
            op = dl[i]
            if op == 0x00:
                i += 1
            elif op == 0x61:
                v = u32(dl, i + 1)
                self.write(v >> 24, v & 0xFFFFFF)
                i += 5
            elif op == 0x08:
                i += 6
            elif op == 0x10:
                n = (struct.unpack_from(">H", dl, i + 1)[0] & 0xFFFF) + 1
                i += 5 + 4 * n
            else:
                raise ModelError(f"display list opcode {op:#x} not modelled")

    def field(self, r, lo, n):
        return (self.reg.get(r, 0) >> lo) & ((1 << n) - 1)


def s11(x):
    return x - 0x800 if x & 0x400 else x


def tev_color(bp, kind, idx):
    """(r, g, b, a) of colour register idx (0 = PREV) or konst idx, 0..255 scale."""
    lo = bp.reg.get((kind, 0xE0 + 2 * idx))
    hi = bp.reg.get((kind, 0xE1 + 2 * idx))
    if lo is None or hi is None:
        return None
    r, a = s11(lo & 0x7FF), s11((lo >> 12) & 0x7FF)
    b, g = s11(hi & 0x7FF), s11((hi >> 12) & 0x7FF)
    return (r, g, b, a)


KFRAC = [255, 223, 191, 159, 128, 96, 64, 32]  # GX_TEV_KCSEL_1 .. 1_8


def konst_sel(bp, stage, alpha):
    r = 0xF6 + stage // 2
    lo = (9 if alpha else 4) + (10 if stage & 1 else 0)
    sel = bp.field(r, lo, 5)
    if sel < 8:
        v = KFRAC[sel]
        return (v, v, v, v)
    if not alpha and 0x0C <= sel <= 0x0F:
        k = tev_color(bp, "k", sel - 0x0C)
        if k is None:
            raise ModelError(f"stage {stage}: konst {sel - 0x0C} never set")
        return k
    if 0x10 <= sel <= 0x1F:
        k = tev_color(bp, "k", sel & 3)
        if k is None:
            raise ModelError(f"stage {stage}: konst {sel & 3} never set")
        v = k[(sel - 0x10) // 4]
        return (v, v, v, v)
    raise ModelError(f"stage {stage}: konst selection {sel:#x} not modelled")


def swap_table(bp, idx):
    rb = bp.field(0xF6 + 2 * idx, 0, 4)
    ga = bp.field(0xF7 + 2 * idx, 0, 4)
    return (rb & 3, rb >> 2, ga & 3, ga >> 2)


def tev_stages(bp, n, tex_enabled):
    """Per stage: a function (tex rgba, ras rgba, regs) -> (dest, rgba)."""
    stages = []
    for s in range(n):
        order = bp.reg.get(0x28 + s // 2)
        if order is None:
            raise ModelError(f"stage {s}: no TEV order")
        sh = 12 if s & 1 else 0
        texmap, texcoord, texen, chan = ((order >> sh) & 7, (order >> (sh + 3)) & 7,
                                         (order >> (sh + 6)) & 1, (order >> (sh + 7)) & 7)
        if bp.reg.get(0x10 + s, 0) != 0:
            raise ModelError(f"stage {s}: indirect texturing not modelled")
        c = bp.reg.get(0xC0 + 2 * s)
        a = bp.reg.get(0xC1 + 2 * s)
        if c is None or a is None:
            raise ModelError(f"stage {s}: no TEV combiner")
        cf = dict(d=c & 15, c=(c >> 4) & 15, b=(c >> 8) & 15, a=(c >> 12) & 15, bias=(c >> 16) & 3,
                  sub=(c >> 18) & 1, clamp=(c >> 19) & 1, scale=(c >> 20) & 3, dest=(c >> 22) & 3)
        af = dict(rswap=a & 3, tswap=(a >> 2) & 3, d=(a >> 4) & 7, c=(a >> 7) & 7, b=(a >> 10) & 7,
                  a=(a >> 13) & 7, bias=(a >> 16) & 3, sub=(a >> 18) & 1, clamp=(a >> 19) & 1,
                  scale=(a >> 20) & 3, dest=(a >> 22) & 3)
        if cf["bias"] == 3 or af["bias"] == 3:
            raise ModelError(f"stage {s}: TEV compare mode not modelled")
        if chan not in (0, 7):  # COLOR0A0, COLOR_ZERO (the quad has colour 0 only)
            raise ModelError(f"stage {s}: rasterised channel {chan} not modelled")
        uses_tex = 8 in (cf["a"], cf["b"], cf["c"], cf["d"]) or 9 in (cf["a"], cf["b"], cf["c"], cf["d"]) \
            or 4 in (af["a"], af["b"], af["c"], af["d"])
        if uses_tex and not (texen and texmap in tex_enabled and texcoord in tex_enabled[texmap]):
            raise ModelError(f"stage {s}: texture map {texmap}/coord {texcoord} not loaded")
        stages.append(dict(cf=cf, af=af, chan=chan, kc=konst_sel(bp, s, False), ka=konst_sel(bp, s, True),
                           rswap=swap_table(bp, af["rswap"]), tswap=swap_table(bp, af["tswap"])))
    return stages


def combine(x, y, z, w, f, alpha=False):
    v = w + (-1 if f["sub"] else 1) * (x * (255 - z) + y * z) / 255.0
    v += (0, 128, -128)[f["bias"]]
    v *= (1, 2, 4, 0.5)[f["scale"]]
    return max(0.0, min(255.0, v)) if f["clamp"] else max(-1024.0, min(1023.0, v))


def run_tev(stages, regs0, tex, ras):
    regs = [list(r) if r is not None else None for r in regs0]
    out = None
    for st in stages:
        t = tuple(tex[i] for i in st["tswap"])
        r = tuple(ras[i] for i in st["rswap"]) if st["chan"] == 0 else (0, 0, 0, 0)

        def reg(i):
            if regs[i] is None:
                raise ModelError(f"TEV register {i} read before it was set")
            return regs[i]

        def carg(k, ch):
            if k < 8:
                rr = reg(k // 2)
                return rr[3] if k & 1 else rr[ch]
            return {8: t[ch], 9: t[3], 10: r[ch], 11: r[3], 12: 255, 13: 128, 14: st["kc"][ch], 15: 0}[k]

        def aarg(k):
            if k < 4:
                return reg(k)[3]
            return {4: t[3], 5: r[3], 6: st["ka"][3], 7: 0}[k]

        cf, af = st["cf"], st["af"]
        rgb = [combine(carg(cf["a"], ch), carg(cf["b"], ch), carg(cf["c"], ch), carg(cf["d"], ch), cf)
               for ch in range(3)]
        al = combine(aarg(af["a"]), aarg(af["b"]), aarg(af["c"]), aarg(af["d"]), af, True)
        dest_c = regs[cf["dest"]] or [0, 0, 0, 0]
        dest_c[:3] = rgb
        regs[cf["dest"]] = dest_c
        dest_a = regs[af["dest"]] or [0, 0, 0, 0]
        dest_a[3] = al
        regs[af["dest"]] = dest_a
        out = (rgb[0], rgb[1], rgb[2], al)
    return tuple(max(0.0, min(255.0, v)) for v in out)


def alpha_test(bp, a):
    v = bp.reg.get(0xF3, 0)
    ref0, ref1, c0, c1, logic = v & 255, (v >> 8) & 255, (v >> 16) & 7, (v >> 19) & 7, (v >> 22) & 3
    a = int(round(a))

    def cmp(c, ref):
        return [False, a < ref, a == ref, a <= ref, a > ref, a != ref, a >= ref, True][c]

    x, y = cmp(c0, ref0), cmp(c1, ref1)
    return [x and y, x or y, x != y, x == y][logic]


# --- Expected frame ---------------------------------------------------------------------

def expected_frame(tex_rgba, tw, th, pix, tevcolor, tev, nstages):
    texels = [tuple(tex_rgba[i:i + 4]) for i in range(0, len(tex_rgba), 4)]
    bp = BP()
    for dl in (pix, tevcolor, tev):
        bp.run(dl)
    cm = bp.reg[0x41]
    if cm & 1 or cm & 2:
        raise ModelError("material blending/logic op not modelled (blend mode must be NONE)")
    if not (cm >> 3) & 1:
        raise ModelError("material disables colour update")
    zm = bp.reg.get(0x40, 0)
    if zm & 1 and ((zm >> 1) & 7) not in (3, 6, 7):  # LEQUAL, GEQUAL, ALWAYS pass at z = 0.5 vs clear
        raise ModelError("material z compare not modelled")
    regs0 = [tev_color(bp, "c", i) for i in range(4)]
    stages = tev_stages(bp, nstages, {0: range(8), 1: range(8)})

    # Per texel (NEAR at an exact 3x scale: every pixel shows one texel).
    top, bottom = {}, {}
    ras = (255, 255, 255, 255)
    discards = 0
    for idx, t in enumerate(texels):
        a = t[3] / 255.0
        top[idx] = tuple(t[ch] * a + CLEAR[ch] * (1 - a) for ch in range(3))
        r, g, b, al = run_tev(stages, regs0, t, ras)
        if alpha_test(bp, al):
            bottom[idx] = (r, g, b)
        else:
            bottom[idx] = CLEAR
            discards += 1

    def quad_pixel(q, table, x, y):
        tx = int((x - q[0] + 0.5) * tw / (q[2] - q[0]))
        ty = int((y - q[1] + 0.5) * th / (q[3] - q[1]))
        return table[ty * tw + tx]

    def at(x, y):
        if TOP[0] <= x < TOP[2] and TOP[1] <= y < TOP[3]:
            return quad_pixel(TOP, top, x, y), "top"
        if BOTTOM[0] <= x < BOTTOM[2] and BOTTOM[1] <= y < BOTTOM[3]:
            return quad_pixel(BOTTOM, bottom, x, y), "bottom"
        return CLEAR, "background"

    return at, discards, len(stages)


# --- Running the harness ----------------------------------------------------------------

def lavapipe_icd():
    for d in ("/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"):
        hits = sorted(glob.glob(os.path.join(d, "lvp_icd*.json")))
        if hits:
            return hits[0]
    return None


def snapshot(paths):
    out = {}
    for p in paths:
        try:
            st = os.stat(p)
            out[p] = (st.st_mtime_ns, sorted(os.listdir(p)) if os.path.isdir(p) else None)
        except FileNotFoundError:
            out[p] = None
    return out


def run(args, env, out, data_dir, *extra):
    cmd = [args.harness, args.disc, out, data_dir] + list(extra)
    p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit(f"FAIL: {' '.join(os.path.basename(c) for c in cmd[:1] + list(extra))} exited {p.returncode}")
    return p.stdout, p.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--harness", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--scratch", required=True, help="directory for the frame and Aurora's files (not the repo)")
    ap.add_argument("--gpu", action="store_true", help="use the system's Vulkan driver instead of lavapipe")
    args = ap.parse_args()

    shutil.rmtree(args.scratch, ignore_errors=True)
    os.makedirs(args.scratch)
    env = dict(os.environ)
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    if not args.gpu:
        icd = lavapipe_icd()
        if icd is None:
            raise SystemExit("FAIL: lavapipe (lvp_icd*.json) not found; install Mesa's Vulkan drivers or pass --gpu")
        env["VK_ICD_FILENAMES"] = icd
    home = os.path.expanduser("~")
    xdg_data = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    watched = [os.path.join(xdg_data, "wiiplay"), os.path.join(xdg_data, "Aurora")]
    before = snapshot(watched)

    errors = []
    (tw, th, fmt, tex_off, tex_bytes), (pix, tevcolor, tev, nstages) = load_inputs(args.disc)

    data_dir = os.path.join(args.scratch, "aurora")
    os.makedirs(data_dir)
    frame_path = os.path.join(args.scratch, "frame.rgba")
    stdout, _ = run(args, env, frame_path, data_dir)
    lines = [l.split("\t") for l in stdout.splitlines()]
    got = {}
    for f in lines:
        got.setdefault(f[0], []).append(f[1:])

    # Inputs: what the harness found through the port's loaders.
    want_tex = [TPL_PATH, "0", str(tw), str(th), str(fmt), str(tex_off)]
    if got.get("TEX", [None])[0] != want_tex:
        errors.append(f"TEX: harness {got.get('TEX')} != {want_tex}")
    want_mat = [f"{ARC_PATH}/{BRRES_PATH}", MAT_NAME, str(nstages)]
    if got.get("MAT", [None])[0] != want_mat:
        errors.append(f"MAT: harness {got.get('MAT')} != {want_mat}")
    dls = {d[0]: bytes.fromhex(d[2]) for d in got.get("DL", [])}
    for name, want in (("pix", pix), ("tevcolor", tevcolor), ("tev", tev)):
        if dls.get(name) != want:
            errors.append(f"DL {name}: the harness submitted different bytes than {BRRES_PATH} holds")
    adapter = got.get("ADAPTER", [["?"]])[0][0]
    fw, fh, frames = (int(x) for x in got["FRAME"][0])
    if (fw, fh) != (W, H):
        raise SystemExit(f"FAIL: frame is {fw}x{fh}, expected {W}x{H}")
    if os.listdir(data_dir):
        errors.append(f"files written to the Aurora data dir without --disk-cache: {os.listdir(data_dir)}")

    with open(frame_path, "rb") as f:
        frame = f.read()

    # Expected image.
    tex_rgba = gxtex.decode(fmt, tex_bytes, tw, th)
    at, discards, nst = expected_frame(tex_rgba, tw, th, pix, tevcolor, tev, nstages)
    stats = {k: [0, 0, 0] for k in ("top", "bottom", "background")}  # pixels, over tolerance, max diff
    for y in range(H):
        row = y * W * 4
        for x in range(W):
            want, region = at(x, y)
            o = row + x * 4
            d = max(abs(frame[o + ch] - want[ch]) for ch in range(3))
            s = stats[region]
            s[0] += 1
            if region == "background" and d != 0 or d > TOLERANCE:
                s[1] += 1
                if s[1] <= 3:
                    errors.append(f"{region} pixel ({x},{y}): got {tuple(frame[o:o + 3])}, "
                                  f"expected {tuple(round(v) for v in want)}")
            s[2] = max(s[2], d)
    for region, (n, bad, mx) in stats.items():
        if bad:
            errors.append(f"{region}: {bad}/{n} pixels differ (max {mx})")

    # Pipeline cache: same frame with a disk cache; a new key discards it.
    cache_dir = os.path.join(args.scratch, "cache")
    os.makedirs(cache_dir)
    cache_runs = []
    for key in ("1", "1", "2"):
        out = os.path.join(args.scratch, f"cache{len(cache_runs)}.rgba")
        _, err = run(args, env, out, cache_dir, "--disk-cache", "--cache-key", key, "--log-level", "1")
        with open(out, "rb") as f:
            cache_runs.append((f.read() == frame, "Pipeline cache key changed" in err))
    if not os.path.exists(os.path.join(cache_dir, "pipeline_cache.db")):
        errors.append("--disk-cache wrote no pipeline_cache.db")
    if [c[0] for c in cache_runs] != [True] * 3:
        errors.append(f"frames with a pipeline cache differ: {[c[0] for c in cache_runs]}")
    if [c[1] for c in cache_runs] != [False, False, True]:
        errors.append(f"cache key change not detected as expected: {[c[1] for c in cache_runs]}")

    after = snapshot(watched)
    if before != after:
        errors.append(f"the per-user data directory changed: {[p for p in watched if before[p] != after[p]]}")

    print(f"test_gx: {fw}x{fh} after {frames} frames on {adapter}; inputs match the disc "
          f"({TPL_PATH} {tw}x{th} format {fmt}, {MAT_NAME}: {nst} TEV stages)")
    for region, (n, bad, mx) in stats.items():
        extra = f", {discards} of {tw * th} texels alpha-tested out" if region == "bottom" else ""
        print(f"  {region}: {n} pixels, max diff {mx:.2f}{extra}")
    print(f"  pipeline cache: 3 runs (key 1, 1, 2) same frame, key change discarded: "
          f"{[c[1] for c in cache_runs]}")
    print(f"errors: {len(errors)}")
    for e in errors:
        print("  " + e)
    if errors:
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ModelError as e:
        print(f"FAIL: {e}")
        sys.exit(1)
