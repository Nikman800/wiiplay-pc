#!/usr/bin/env python3
"""Check that the port's edits to decomp files leave the matching build unchanged.

For every compilation unit whose dependencies include one of the given files
(by default: the decomp files the PC port has touched), recompile this
repository's source with the matching build's CodeWarrior command line and
compare the object byte for byte with the object in the decomp checkout's
build directory.

    python3 port/host/tools/check_matching.py [--decomp ~/wii-play] [-j 8] [file ...]

A unit is only compared when all of its other dependencies are identical in
both checkouts; units whose sources differ for unrelated reasons (the decomp
moved on) are reported as "skipped". Objects are written to a temporary
directory, never to build/.
"""
import argparse
import concurrent.futures
import filecmp
import os
import re
import shlex
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

TOUCHED = [
    "include/port/be.h",
    "include/nw4r/ut/ut_binaryFileFormat.h",
    "include/nw4r/ut/ut_algorithm.h",
    "src/revolution/ARC/arc.c",
    "src/egg/core/eggDvdFile.cpp",
    "include/port/resptr.h",
    "include/nw4r/ut/ut_ResFontBase.h",
    "include/nw4r/ut/ut_CharStrmReader.h",
    "src/nw4r/ut/ut_ResFont.cpp",
    "src/nw4r/ut/ut_ResFontBase.cpp",
    "include/egg/util/eggMsgRes.h",
    "src/egg/util/eggMsgRes.cpp",
    "include/Pack/RPKernel/RPSysMessage.h",
    # TPL / layouts / step 4 (port/docs/HOST_BUILD.md §4)
    "include/nw4r/ut/ut_Color.h",
    "include/nw4r/lyt/lyt_types.h",
    "include/nw4r/lyt/lyt_common.h",
    "include/nw4r/lyt/lyt_resources.h",
    "include/nw4r/lyt/lyt_pane.h",
    "include/nw4r/lyt/lyt_picture.h",
    "include/nw4r/lyt/lyt_textBox.h",
    "include/nw4r/lyt/lyt_window.h",
    "include/nw4r/lyt/lyt_group.h",
    "include/nw4r/lyt/lyt_animation.h",
    "src/nw4r/lyt/lyt_animation.cpp",
    "src/nw4r/lyt/lyt_arcResourceAccessor.cpp",
    "src/nw4r/lyt/lyt_common.cpp",
    "src/nw4r/lyt/lyt_material.cpp",
    "src/nw4r/lyt/lyt_picture.cpp",
    "src/nw4r/lyt/lyt_texMap.cpp",
    "src/nw4r/lyt/lyt_textBox.cpp",
    "src/nw4r/lyt/lyt_window.cpp",
    "include/Pack/RPKernel/RPSysSystemWinMgr.h",
    "src/Pack/RPKernel/RPSysTagProcessor.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPPlayerNameTag.cpp",
    # g3d resources (POINTERS.md step 5; HOST_BUILD.md §11)
    "include/egg/gfxe/eggCpuTexture.h",
    "include/nw4r/g3d/g3d_obj.h",
    "include/nw4r/g3d/platform/g3d_cpu.h",
    "include/nw4r/g3d/res/g3d_resanm.h",
    "include/nw4r/g3d/res/g3d_resanmamblight.h",
    "include/nw4r/g3d/res/g3d_resanmcamera.h",
    "include/nw4r/g3d/res/g3d_resanmchr.h",
    "include/nw4r/g3d/res/g3d_resanmclr.h",
    "include/nw4r/g3d/res/g3d_resanmfog.h",
    "include/nw4r/g3d/res/g3d_resanmlight.h",
    "include/nw4r/g3d/res/g3d_resanmscn.h",
    "include/nw4r/g3d/res/g3d_resanmshp.h",
    "include/nw4r/g3d/res/g3d_resanmtexpat.h",
    "include/nw4r/g3d/res/g3d_resanmtexsrt.h",
    "include/nw4r/g3d/res/g3d_resanmvis.h",
    "include/nw4r/g3d/res/g3d_rescommon.h",
    "include/nw4r/g3d/res/g3d_resdict.h",
    "include/nw4r/g3d/res/g3d_reslightset.h",
    "include/nw4r/g3d/res/g3d_resmat.h",
    "include/nw4r/g3d/res/g3d_resmdl.h",
    "include/nw4r/g3d/res/g3d_resnode.h",
    "include/nw4r/g3d/res/g3d_respltt.h",
    "include/nw4r/g3d/res/g3d_resshp.h",
    "include/nw4r/g3d/res/g3d_restev.h",
    "include/nw4r/g3d/res/g3d_restex.h",
    "include/nw4r/g3d/res/g3d_resvtx.h",
    "src/nw4r/g3d/res/g3d_resanm.cpp",
    "src/nw4r/g3d/res/g3d_resanmtexpat.cpp",
    "src/nw4r/g3d/res/g3d_resanmtexsrt.cpp",
    "src/nw4r/g3d/res/g3d_reslightset.cpp",
    "src/nw4r/g3d/res/g3d_resmat.cpp",
    # POINTERS.md step 6 (nw4r::ef resources; HOST_BUILD.md §12). ef_animcurve.cpp is an unlinked
    # functional unit; its object must stay identical to the decomp's too.
    "include/port/reslist.h",
    "include/nw4r/ef/ef_types.h",
    "include/nw4r/ef/ef_effect.h",
    "include/nw4r/ef/ef_resource.h",
    "include/nw4r/ef/ef_res_texture.h",
    "include/nw4r/ef/ef_res_animcurve.h",
    "include/nw4r/ef/ef_res_emitter.h",
    "include/nw4r/ef/ef_particle.h",
    "src/nw4r/ef/ef_resource.cpp",
    "src/nw4r/ef/ef_animcurve.cpp",
    # POINTERS.md step 7 (nw4r::snd sound archives; HOST_BUILD.md §13)
    "include/nw4r/snd/snd_AxfxImpl.h",
    "include/nw4r/snd/snd_BankFile.h",
    "include/nw4r/snd/snd_Channel.h",
    "include/nw4r/snd/snd_NoteOnCallback.h",
    "include/nw4r/snd/snd_SeqFile.h",
    "include/nw4r/snd/snd_SeqTrack.h",
    "include/nw4r/snd/snd_SoundArchive.h",
    "include/nw4r/snd/snd_SoundArchiveFile.h",
    "include/nw4r/snd/snd_SoundArchivePlayer.h",
    "include/nw4r/snd/snd_SoundStartable.h",
    "include/nw4r/snd/snd_Types.h",
    "include/nw4r/snd/snd_Util.h",
    "include/nw4r/snd/snd_WaveFile.h",
    "include/nw4r/snd/snd_WsdFile.h",
    "include/nw4r/snd/snd_WsdPlayer.h",
    "include/egg/audio/eggAudioArcPlayerMgr.h",
    "include/egg/audio/eggAudioHeapMgr.h",
    "include/revolution/AX/AXAlloc.h",
    "include/revolution/AX/AXVPB.h",
    "src/revolution/AX/AXAlloc.c",
    "src/nw4r/snd/snd_AxManager.cpp",
    "src/nw4r/snd/snd_AxVoice.cpp",
    "src/nw4r/snd/snd_AxVoiceManager.cpp",
    "src/nw4r/snd/snd_AxfxImpl.cpp",
    "src/nw4r/snd/snd_Channel.cpp",
    "src/nw4r/snd/snd_SeqTrack.cpp",
    "src/nw4r/snd/snd_SoundThread.cpp",
    "src/nw4r/snd/snd_Util.cpp",
    "src/nw4r/snd/snd_WaveFile.cpp",
    "src/nw4r/snd/snd_WsdPlayer.cpp",
]


def parse_ninja(path):
    """Yield (obj, src, rule, vars) for mwcc build statements."""
    with open(path) as f:
        text = f.read().replace("$\n", "")
    stmt = None
    for line in text.splitlines():
        m = re.match(r"build (\S+\.o):\s+(mwcc\S*)\s+(\S+)", line)
        if m:
            if stmt:
                yield stmt
            stmt = [m.group(1), m.group(3), m.group(2), {}]
            continue
        if stmt and line.startswith("  ") and "=" in line:
            k, v = line.strip().split("=", 1)
            stmt[3][k.strip()] = v.strip()
        elif stmt and not line.startswith("  "):
            yield stmt
            stmt = None
    if stmt:
        yield stmt


def all_deps(decomp):
    """obj -> [deps], from ninja's dependency log (`ninja -t deps`, read-only)."""
    out = subprocess.run(["ninja", "-t", "deps"], cwd=decomp, stdout=subprocess.PIPE,
                         text=True, check=True).stdout
    deps, cur = {}, None
    for line in out.splitlines():
        if line and not line.startswith(" "):
            cur = line.split(":")[0]
            deps[cur] = []
        elif line.strip() and cur:
            p = line.strip()
            if os.path.isabs(p):
                p = os.path.relpath(p, decomp)
            deps[cur].append(p)
    return deps


def same(rel, decomp):
    a, b = os.path.join(REPO, rel), os.path.join(decomp, rel)
    return os.path.exists(b) and filecmp.cmp(a, b, shallow=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", default=os.path.expanduser("~/wii-play"))
    ap.add_argument("-j", type=int, default=8)
    ap.add_argument("files", nargs="*", default=TOUCHED)
    args = ap.parse_args()
    decomp = os.path.abspath(args.decomp)
    touched = set(args.files)

    units = []
    dep_log = all_deps(decomp)
    for obj, src, rule, var in parse_ninja(os.path.join(decomp, "build.ninja")):
        deps = dep_log.get(obj)
        if deps is None:
            continue
        hit = touched & (set(deps) | {src})
        # include/port/be.h is new, so the decomp's dependency log cannot list
        # it; every unit that uses it includes another touched file.
        if not hit:
            continue
        others = [p for p in set(deps) | {src} if p not in touched and not p.startswith("build/")]
        units.append((obj, src, rule, var, all(same(p, decomp) for p in others)))

    tmp = tempfile.mkdtemp(prefix="check_matching_")

    def run(u):
        obj, src, rule, var, comparable = u
        if not comparable:
            return obj, "skipped"
        out = os.path.join(tmp, obj.replace("/", "_"))
        tools = os.path.join(decomp, "build")
        cmd = [os.path.join(tools, "tools", "wibo")]
        if rule == "mwcc_sjis":
            cmd.append(os.path.join(tools, "tools", "sjiswrap.exe"))
        cmd.append(os.path.join(tools, "compilers", var["mw_version"], "mwcceppc.exe"))
        flags = shlex.split(var["cflags"])
        # build/RHAE01/include only exists in the decomp checkout.
        flags = [os.path.join(decomp, f) if f.startswith("build/") else f for f in flags]
        cmd += flags + ["-c", src, "-o", out]
        r = subprocess.run(cmd, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode != 0:
            return obj, "COMPILE ERROR: " + r.stdout.strip()[:300]
        return obj, "identical" if filecmp.cmp(out, os.path.join(decomp, obj), shallow=False) else "DIFFERENT"

    with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
        results = list(ex.map(run, units))

    counts = {}
    for obj, res in results:
        key = res.split(":")[0]
        counts[key] = counts.get(key, 0) + 1
        if key not in ("identical", "skipped"):
            print("%s: %s" % (obj, res))
    print("units depending on touched files: %d; %s" % (
        len(results), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    return 0 if all(r.split(":")[0] in ("identical", "skipped") for _, r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
