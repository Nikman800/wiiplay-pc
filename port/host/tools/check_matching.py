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
    "include/nw4r/snd/snd_StrmFile.h",
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
    "src/nw4r/snd/snd_StrmFile.cpp",
    "src/nw4r/snd/snd_Util.cpp",
    "src/nw4r/snd/snd_WaveFile.cpp",
    "src/nw4r/snd/snd_WsdPlayer.cpp",
    # Step 8 (HOST_BUILD.md §14): KCL, PMP, EGG binary settings, game tables.
    "include/port/bemath.h",
    "include/Pack/RPParty/RPPartyCommon/RPKcl.h",
    "src/Pack/RPParty/RPPartyCommon/RPKcl.cpp",
    "include/Pack/RPParty/RPPartyCommon/RPPathFollower.h",
    "include/Pack/RPParty/RPPartyCommon/RPResFile_2.h",
    "src/Pack/RPParty/RPPartyCommon/RPResFile_3.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPResFile_4.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPPathFollower_2.cpp",
    "include/egg/core/eggIBinary.h",
    "include/egg/gfxe/eggLightManager.h",
    "include/egg/gfxe/eggLightObject.h",
    "include/egg/gfxe/eggLightTexture.h",
    "include/egg/gfxe/eggDrawPathBloom.h",
    "src/egg/gfxe/eggLightManager.cpp",
    "src/egg/gfxe/eggLightTextureManager.cpp",
    "include/Pack/RPParty/RPPartyCommon/RPTnkMapData.h",
    "include/Pack/RPParty/RPBomScene/RPBomQuestionParam.h",
    "include/Pack/RPParty/RPWlyScene/RPWlyLevelTable.h",
    "include/Pack/RPParty/RPPnpScene/RPPnpLevelTable.h",
    # Step 8, math value types in g3d / ef file structs (BE_MATH, BE_MATH_CAST).
    "src/nw4r/g3d/g3d_calcworld.cpp",
    "src/nw4r/g3d/g3d_draw.cpp",
    "src/nw4r/g3d/g3d_state.cpp",
    # Step 8, RPAudio headers on the host (HOST_BUILD.md §15): ui32, dependent-base
    # using-declarations; Duc / Tnk parameters and the Wly / Pnp level tables.
    "include/Pack/types_pack.h",
    "include/Pack/RPAudio/RPSndObject.h",
    "include/Pack/RPParty/RPPartyCommon/RPActorList.h",
    # Windows (HOST_BUILD.md §41): one definition of each inline / out-of-line pair
    "include/Pack/RPParty/RPPartyCommon/RPActor.h",
    "src/Pack/RPParty/RPBilScene/RPBilCamera_2.cpp",
    "src/Pack/RPParty/RPWlyScene/RPWlySound_3.cpp",
    # Windows x64 assigns argument registers by position (HOST_BUILD.md §41)
    "src/Pack/RPParty/RPCowScene/RPCowCowModel.cpp",
    "src/Pack/RPParty/RPCowScene/RPCowFieldObj.cpp",
    "src/Pack/RPParty/RPCowScene/RPCowPlayer_6.cpp",
    "src/egg/gfxe/eggAnalizeDL.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPPathFollower_2.cpp",
    "src/Pack/RPParty/RPBilScene/RPBilResult.cpp",
    "src/Pack/RPParty/RPFshScene/RPFshResultLayout.cpp",
    "include/Pack/RPAudio/RPSndObject2D.h",
    "include/Pack/RPAudio/RPSndAudioMgr.h",
    "src/Pack/RPAudio/RPSndAudioMgr.cpp",
    "src/Pack/RPParty/RPWlyScene/RPWlySound_2.cpp",
    "include/Pack/RPKernel/RPSysParticleManager.h",
    "include/nw4r/ef/ef_memorymanagerimpl.h",
    "include/Pack/RPParty/RPDucScene/RPDucBalloon.h",
    "include/Pack/RPParty/RPDucScene/RPDucCan.h",
    "include/Pack/RPParty/RPDucScene/RPDucCircleTarget.h",
    "include/Pack/RPParty/RPDucScene/RPDucClay.h",
    "include/Pack/RPParty/RPDucScene/RPDucDuck.h",
    "include/Pack/RPParty/RPDucScene/RPDucUfo.h",
    "src/Pack/RPParty/RPTnkScene/RPTnkGameManager.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkStageMap.cpp",
    "src/Pack/RPParty/RPWlyScene/RPWlyLevelTable.cpp",
    # Step 8, remaining loaders (HOST_BUILD.md §16): BTI, RFL_Res.dat, HOME Menu, cookies.
    "src/homebuttonMiniLib/HBMBase.cpp",
    "src/homebuttonMiniLib/HBMController.cpp",
    "src/homebuttonMiniLib/HBMRemoteSpk.cpp",
    "include/egg/gfx/eggResTIMG.h",
    "include/RVLFaceLib/RFLi_HiddenDatabase.h",
    "include/RVLFaceLib/RFLi_Model.h",
    "include/RVLFaceLib/RFLi_Texture.h",
    "include/RVLFaceLib/RFLi_Types.h",
    "src/RVLFaceLib/RFL_Database.c",
    "src/RVLFaceLib/RFL_Format.c",
    "src/RVLFaceLib/RFL_HiddenDatabase.c",
    "src/RVLFaceLib/RFL_MakeTex.c",
    "src/RVLFaceLib/RFL_MiddleDatabase.c",
    "src/RVLFaceLib/RFL_Model.c",
    "src/RVLFaceLib/RFL_NANDAccess.c",
    "src/RVLFaceLib/RFL_NANDLoader.c",
    "include/homebuttonMiniLib/HBMRemoteSpk.h",
    "src/Pack/RPParty/RPPartyCommon/RPKokeshiMgr_3.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPPolyline.cpp",
    "include/RVLFaceLib/RFL_Model.h",
    "include/RVLFaceLib/RFL_MiddleDatabase.h",
    "include/nw4r/math/math_types.h",
    # The Mii database on the NAND (HOST_BUILD.md §40): hidden database fields big-endian.
    "include/RVLFaceLib/RFLi_Database.h",
    "include/RVLFaceLib/RFLi_Format.h",
    # Animation evaluation (HOST_BUILD.md §17): fast-cast / math asm blocks named with ASM_PS,
    # SCN0 enums BE_ENUM, SHP0 index table and ef curve keys be_*.
    "include/revolution/OS/OSFastCast.h",
    "include/nw4r/math/math_arithmetic.h",
    "src/nw4r/math/math_types.cpp",
    "src/nw4r/g3d/res/g3d_resanmshp.cpp",
    "include/nw4r/ef/ef_emitter.h",
    "src/nw4r/ef/ef_emitter.cpp",
    "src/nw4r/ef/ef_particlemanager.cpp",
    # Paired-single asm host forms (HOST_BUILD.md §18): ASM_PS block names and
    # #if defined(PORT_HOST) bodies for asm functions; ef field parameter blocks be_*.
    "src/nw4r/math/math_arithmetic.cpp",
    "src/nw4r/g3d/platform/g3d_cpu.cpp",
    "src/nw4r/g3d/g3d_fog.cpp",
    "src/nw4r/g3d/g3d_calcview.cpp",
    "src/nw4r/g3d/g3d_calcvtx.cpp",
    "src/revolution/MTX/mtx.c",
    "src/revolution/MTX/mtxvec.c",
    "src/revolution/MTX/vec.c",
    "src/revolution/MTX/quat.c",
    "src/Pack/RPParty/RPTnkScene/RPTnkBlock.cpp",
    "src/Pack/RPParty/RPHkyScene/RPHkyPaddle.cpp",
    "src/Pack/RPGraphics/RPGrpModelRfl.cpp",
    "src/Pack/RPSystem/RPSysPauseMgr.cpp",
    "src/Pack/RPParty/RPCowScene/RPCowKakasiGroup.cpp",
    "src/nw4r/ef/ef_effect.cpp",
    "src/nw4r/ef/drawstrategy/ef_drawstrategyimpl.cpp",
    "include/nw4r/ef/ef_memorymanagertmp.h",
    # Phase 7 g3d scene runtime (HOST_BUILD.md §20)
    "src/nw4r/g3d/g3d_workmem.cpp",
    # Phase 7 sound runtime and AX mixer (HOST_BUILD.md §21); snd_AxManager.cpp and
    # snd_WaveFile.cpp are already listed above.
    "src/revolution/AX/AXVPB.c",
    # Phase 7 OS / storage platform layer (HOST_BUILD.md §22)
    "src/revolution/DVD/dvdfs.c",
    "src/egg/core/eggStream.cpp",
    "src/Pack/RPSystem/RPSysSaveData.cpp",
    "include/revolution/NAND/nand.h",
    "src/Pack/RPKernel/RPSysSystem.cpp",
    # §26: the GX SDK as the host's command encoder (WGPIPE_WRITE, Wii-order
    # texture objects, host forms of the write-gather asm)
    "include/port/wgpipe.h",
    "include/revolution/GX/GXHardware.h",
    "include/revolution/GX/GXVert.h",
    "include/revolution/GX/GXDisplayList.h",
    "include/revolution/GX/GXInternal.h",
    "src/revolution/GX/GXAttr.c",
    "src/revolution/GX/GXDisplayList.c",
    "src/revolution/GX/GXGeometry.c",
    "src/revolution/GX/GXInit.c",
    "src/revolution/GX/GXLight.c",
    "src/revolution/GX/GXTexture.c",
    "src/revolution/GX/GXTransform.c",
    "src/revolution/GX/GXFrameBuf.c",
    "src/revolution/GX/GXTev.c",
    "src/revolution/GX/GXPixel.c",
    "include/nw4r/g3d/platform/g3d_gpu.h",
    # §27: a minigame's 3D scene (big-endian copies of static GP data; g3d's view
    # matrices and EGG's light objects for indexed loads; g3d_draw.cpp and
    # eggLightManager.cpp are already listed above)
    "src/egg/gfxe/eggStateGX.cpp",
    "src/egg/gfxe/eggDrawGX.cpp",
    "src/egg/gfxe/eggLightTexture.cpp",
    # §29: the game's frame loop (RPGrpModel*, RPGrpRenderer, effects, scenes on the host)
    "src/Pack/RPGraphics/RPGrpModelAnmImplement.cpp",
    "src/Pack/RPGraphics/RPGrpModelImplement.cpp",
    "src/Pack/RPGraphics/RPGrpModelMaterial.cpp",
    "src/egg/core/eggProcessMeter.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPBodyJoints.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPCollisionShape.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPDoll.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPPlayerSettingPanel.cpp",
    "src/Pack/RPSystem/RPSysTagParms.cpp",
    "include/Pack/RPParty/RPPartyCommon/RPSceneInfo.h",
    "src/Pack/RPParty/RPPartyCommon/RPDebugGraph.cpp",
    "src/RVLFaceLib/RFL_System.c",
    "src/nw4r/ef/drawstrategy/ef_drawfreestrategy.cpp",
    "src/nw4r/ef/drawstrategy/ef_drawbillboardstrategy.cpp",
    "src/nw4r/ef/drawstrategy/ef_drawdirectionalstrategy.cpp",
    # Milestone 5 (HOST_BUILD.md §30): the menus
    "src/Pack/RPKernel/RPSysLayout.cpp",
    # Milestone 6 (HOST_BUILD.md §31): Shooting Range
    "src/Pack/RPParty/RPDucScene/RPDucField_2.cpp",
    "src/Pack/RPParty/RPDucScene/RPDucHud_5.cpp",
    # Milestone 7 (HOST_BUILD.md §32): the other minigames
    "include/Pack/RPAudio/RPSndObject.h",
    "include/Pack/RPParty/RPPartyCommon/RPActorList.h",
    # Windows (HOST_BUILD.md §41): one definition of each inline / out-of-line pair
    "src/Pack/RPParty/RPBilScene/RPBilCamera_2.cpp",
    "src/egg/gfxe/eggAnalizeDL.cpp",
    "src/Pack/RPParty/RPPartyCommon/RPPathFollower_2.cpp",
    "src/Pack/RPParty/RPBilScene/RPBilResult.cpp",
    "src/Pack/RPParty/RPFshScene/RPFshResultLayout.cpp",
    "src/Pack/RPParty/RPBilScene/RPBilBall.cpp",
    "src/Pack/RPParty/RPBilScene/RPBilPlayer.cpp",
    "src/Pack/RPParty/RPBilScene/RPBilSound.cpp",
    "src/Pack/RPParty/RPBomScene/RPBomScore.cpp",
    "src/Pack/RPParty/RPBomScene/RPBomBorder.cpp",
    "include/Pack/RPParty/RPPartyCommon/RPColliderList.h",
    "src/Pack/RPParty/RPCowScene/RPCowGoal.cpp",
    "src/Pack/RPParty/RPCowScene/RPCowHud_3.cpp",
    "src/Pack/RPParty/RPCowScene/RPCowHud_4.cpp",
    "src/Pack/RPParty/RPCowScene/RPCowHud_8.cpp",
    "src/Pack/RPParty/RPFshScene/RPFshFish.cpp",
    "src/Pack/RPParty/RPHkyScene/RPHkyPaddle.cpp",
    "src/Pack/RPParty/RPPnpScene/RPPnpBall.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkBlock.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkScene.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkAi_2.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkTank.cpp",
    "src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp",
    "src/Pack/RPParty/RPWlyScene/RPWlySound.cpp",
    # §36 (two players, later levels)
    "include/Pack/RPParty/RPPartyCommon/RPMedalResult_5.h",
    "include/Pack/RPParty/RPPartyCommon/RPMedalResult.h",
    "src/Pack/RPParty/RPPartyCommon/RPMedalResult_2.cpp",
    "include/Pack/RPParty/RPBomScene/RPBomPanel_3.h",
    # §42 (the game's own math): fdlibm's word order on a little-endian host
    "include/MSL/internal/fdlibm.h",
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
