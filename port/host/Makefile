# Host (PC) build of the decompiled loader path, plus tests.
#
#   make                 native build (64-bit on x86-64) into build/host64
#   make M32=1           32-bit cross-check build into build/host32
#   make test DISC=...   unit tests + archive comparison against wiiarc.py
#
# Run from port/host (or `make -C port/host`). Objects never go near the
# matching build's build/ directory.

REPO := $(abspath ../..)
HOST := $(abspath .)

ifeq ($(M32),1)
ARCH := -m32
OUT := $(HOST)/build/host32
else
ARCH :=
OUT := $(HOST)/build/host64
endif

CC ?= gcc
CXX ?= g++
ifeq ($(CC),cc)
CC := gcc
endif

DISC ?= $(HOME)/wii-play/extracted/files

# port/host/include must come first: it shadows include/types.h,
# include/decomp/compat.h and include/port/be.h for the host build.
# include/revolution/BTE is searched recursively by CodeWarrior (-ir); GCC has
# no recursive form, so every subdirectory is added (after the system headers).
BTE_DIRS := $(shell find $(REPO)/include/revolution/BTE -type d)
# Likewise -ir include/egg, -i include/nw4r and -i include/Pack (EGG headers include their
# siblings by bare name, e.g. "eggIBinary.h").
EGG_DIRS := $(shell find $(REPO)/include/egg -type d)
INCLUDES := -I$(HOST)/include -I$(REPO)/include $(addprefix -idirafter ,$(BTE_DIRS)) \
            $(addprefix -idirafter ,$(EGG_DIRS)) -idirafter $(REPO)/include/nw4r -idirafter $(REPO)/include/Pack

DEFINES := -DPORT_HOST=1 -DNONMATCHING -DREVOLUTION -DNDEBUG=1 \
           -DVERSION_RHAE01 -DVERSION_RHAE01_01 -DPACK_PARTY

WARN := -Wall -Wno-unknown-pragmas -Wno-unused-function -Wno-unused-variable \
        -Wno-unused-but-set-variable -Wno-sign-compare -Wno-parentheses
# OSAddress.h casts u32 physical addresses to pointers; harmless here, listed
# in docs/HOST_BUILD.md as a 64-bit site.
# Pointer-size checklist (port/docs/POINTERS.md §2): a cast between a
# pointer and a narrower integer is an error, not a silent truncation. (In
# C++ reinterpret_cast<u32>(ptr) is already ill-formed on LP64.)
WARN += -Werror=int-to-pointer-cast

# 16-bit wchar_t as on the Wii; port/wide.h checks it and routes the wide
# string calls to the decomp's MSL_C versions (port/docs/POINTERS.md §6).
WIDE := -fshort-wchar -include $(HOST)/include/port/wide.h

# -fno-aggressive-loop-optimizations: some units index one past an array into the member after
# it, as the original does (RPWlyStartCountLyt::Reset's four animations in a three-entry array);
# GCC would otherwise treat that iteration as unreachable.
COMMON := $(ARCH) -O2 -g -fno-strict-aliasing -fno-aggressive-loop-optimizations -ffunction-sections \
          -fdata-sections $(WIDE) $(INCLUDES) $(DEFINES) $(WARN)
CFLAGS := -std=gnu11 $(COMMON) -Werror=pointer-to-int-cast
# Decompiled class layouts: offsetof on classes with vtables (nw4r lists),
# member init order and anonymous-namespace bases are as in the original.
CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti $(COMMON) \
            -Wno-invalid-offsetof -Wno-subobject-linkage -Wno-reorder
# g3d_resanmchr.cpp switches a u32 over enumerators of (1 << 31) (an int);
# CodeWarrior accepts it, C++11 calls it a narrowing conversion.
CXXFLAGS += -Wno-narrowing
# Every executable keeps its operator new / delete to itself (exe.map, §28):
# shared libraries (GPU drivers) allocate with libstdc++'s. Compile-only runs
# ignore it.
EXE_LDFLAGS := -Wl,--version-script=$(HOST)/exe.map
CFLAGS += $(EXE_LDFLAGS)
CXXFLAGS += $(EXE_LDFLAGS)

# nw4r::lyt (layouts and layout animations), built and bound without drawing.
LYT_SRCS := $(addprefix src/nw4r/lyt/,lyt_animation.cpp lyt_arcResourceAccessor.cpp \
    lyt_bounding.cpp lyt_common.cpp lyt_drawInfo.cpp lyt_group.cpp lyt_init.cpp \
    lyt_layout.cpp lyt_material.cpp lyt_pane.cpp lyt_picture.cpp \
    lyt_resourceAccessor.cpp lyt_texMap.cpp lyt_textBox.cpp lyt_window.cpp)

# nw4r::g3d resource code (POINTERS.md step 5): ResFile CheckRevision, Init,
# Bind; no drawing.
G3D_SRCS := $(addprefix src/nw4r/g3d/res/,g3d_rescommon.cpp g3d_resdict.cpp \
    g3d_resfile.cpp g3d_resmdl.cpp g3d_resmat.cpp g3d_resshp.cpp g3d_restev.cpp \
    g3d_restex.cpp g3d_resvtx.cpp g3d_resnode.cpp g3d_resanm.cpp g3d_resanmchr.cpp \
    g3d_resanmclr.cpp g3d_resanmtexpat.cpp g3d_resanmtexsrt.cpp g3d_resanmvis.cpp \
    g3d_resanmshp.cpp g3d_resanmscn.cpp g3d_resanmamblight.cpp g3d_resanmlight.cpp \
    g3d_resanmcamera.cpp g3d_reslightset.cpp)

# The RVL MTX library: its paired-single asm has host forms (docs/HOST_BUILD.md §18).
MTX_SRCS := $(addprefix src/revolution/MTX/,mtx.c mtxvec.c vec.c quat.c mtx44.c)

# Real decompiled sources compiled for the host (unmodified unless listed in
# docs/HOST_BUILD.md).
DECOMP_SRCS := \
    src/revolution/ARC/arc.c \
    src/egg/core/eggArchive.cpp \
    src/egg/core/eggDecomp.cpp \
    src/egg/core/eggDisposer.cpp \
    src/egg/core/eggDvdFile.cpp \
    src/egg/core/eggDvdRipper.cpp \
    src/egg/prim/eggAssert.cpp \
    src/nw4r/ut/ut_binaryFileFormat.cpp \
    src/nw4r/ut/ut_list.cpp \
    src/nw4r/ut/ut_CharStrmReader.cpp \
    src/nw4r/ut/ut_Font.cpp \
    src/nw4r/ut/ut_ResFont.cpp \
    src/nw4r/ut/ut_ResFontBase.cpp \
    src/nw4r/ut/ut_LinkList.cpp \
    src/nw4r/ut/ut_CharWriter.cpp \
    src/nw4r/ut/ut_TextWriterBase.cpp \
    src/nw4r/ut/ut_TagProcessorBase.cpp \
    src/nw4r/math/math_triangular.cpp \
    src/nw4r/math/math_types.cpp \
    src/nw4r/math/math_arithmetic.cpp \
    src/nw4r/g3d/platform/g3d_cpu.cpp \
    $(MTX_SRCS) \
    src/egg/util/eggMsgRes.cpp \
    $(LYT_SRCS) \
    $(G3D_SRCS)

# MSL_C wide-string functions, compiled under port_* names (port/wide.h).
# Their headers live in include/MSL/internal; include/MSL is searched last so
# the C library headers still win.
MSL_SRCS := \
    src/MSL_C/wstring.c \
    src/MSL_C/wmem.c

HOST_SRCS := src/host_sdk.cpp src/host_arena.cpp src/host_text.cpp src/host_tpl.cpp src/host_gx.cpp src/host_mtx.cpp \
    src/host_gxtexobj.cpp src/host_wgpipe.cpp src/host_wprintf.cpp

# POINTERS.md step 4 sites (§5.1): compiled (not linked; most of RPKernel and
# RPParty does not build on the host yet) so the host forms keep building.
# RPPlayerNameTag also pulls in the g3d and EGG CpuTexture headers (64-bit
# clean since POINTERS.md step 5).
STEP4_SRCS := src/Pack/RPKernel/RPSysTagProcessor.cpp \
    src/Pack/RPParty/RPPartyCommon/RPPlayerNameTag.cpp
STEP4_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(STEP4_SRCS))

DECOMP_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(DECOMP_SRCS))
MSL_OBJS := $(patsubst %,$(OUT)/msl/%.o,$(MSL_SRCS))
HOST_OBJS := $(patsubst %,$(OUT)/host/%.o,$(HOST_SRCS))
LIB := $(OUT)/libwiiplay_loader.a

TESTS := $(OUT)/test_be $(OUT)/test_be_c $(OUT)/test_port $(OUT)/test_port_c \
         $(OUT)/test_layout $(OUT)/test_math

.PHONY: all test unit archives resources layouts models clean distclean
all: $(OUT)/arc_harness $(OUT)/res_harness $(OUT)/lyt_harness $(OUT)/g3d_harness $(TESTS) $(STEP4_OBJS)

$(OUT)/decomp/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OUT)/decomp/%.cpp.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(OUT)/msl/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPORT_WIDE_IMPL -idirafter $(REPO)/include/MSL -MMD -c $< -o $@

$(OUT)/host/%.cpp.o: $(HOST)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(OUT)/host/%.c.o: $(HOST)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(LIB): $(DECOMP_OBJS) $(MSL_OBJS) $(HOST_OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/arc_harness: $(HOST)/tests/arc_harness.cpp $(HOST)/tests/disc_walk.h $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(LIB) -o $@

$(OUT)/res_harness: $(HOST)/tests/res_harness.cpp $(HOST)/tests/rpsysmessage_unit.cpp \
                   $(HOST)/tests/disc_walk.h $(LIB)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) $(LIB) -o $@

$(OUT)/lyt_harness: $(HOST)/tests/lyt_harness.cpp $(HOST)/tests/disc_walk.h $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(LIB) -o $@

$(OUT)/g3d_harness: $(HOST)/tests/g3d_harness.cpp $(HOST)/tests/disc_walk.h $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(LIB) -o $@

$(OUT)/test_be: $(HOST)/tests/test_be.cpp $(HOST)/include/port/be.h
	$(CXX) $(CXXFLAGS) $< -o $@

$(OUT)/test_be_c: $(HOST)/tests/test_be_c.c $(HOST)/include/port/be.h
	$(CC) $(CFLAGS) $< -o $@

$(OUT)/test_port: $(HOST)/tests/test_port.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(LIB) -o $@

$(OUT)/test_port_c: $(HOST)/tests/test_port_c.c $(LIB)
	$(CC) $(CFLAGS) -MMD $< $(LIB) -lstdc++ -o $@

$(OUT)/test_layout: $(HOST)/tests/layout_asserts.cpp
	$(CXX) $(CXXFLAGS) -MMD $< -o $@

# Host forms of the paired-single asm against reference math (HOST_BUILD.md §18).
$(OUT)/test_math: $(HOST)/tests/test_math.cpp $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(LIB) -o $@

unit: $(TESTS) $(STEP4_OBJS)
	$(OUT)/test_be
	$(OUT)/test_be_c
	$(OUT)/test_port
	$(OUT)/test_port_c
	$(OUT)/test_layout
	$(OUT)/test_math

archives: $(OUT)/arc_harness
	python3 $(HOST)/tests/test_archives.py --harness $(OUT)/arc_harness --disc $(DISC)

resources: $(OUT)/res_harness
	python3 $(HOST)/tests/test_resources.py --harness $(OUT)/res_harness --disc $(DISC)

layouts: $(OUT)/lyt_harness
	python3 $(HOST)/tests/test_layouts.py --harness $(OUT)/lyt_harness --disc $(DISC)

models: $(OUT)/g3d_harness
	python3 $(HOST)/tests/test_models.py --harness $(OUT)/g3d_harness --disc $(DISC)

test: unit archives resources layouts models anims

# --- g3d animation evaluation (docs/HOST_BUILD.md §17) ------------------------
# The real ResAnm*::GetAnmResult readers at a set of frames, against an f32
# model in test_anims.py. On -m32, -mfpmath=sse keeps the arithmetic in single
# precision (x87 would keep extended precision between operations).
ifeq ($(M32),1)
ANIM_FP := -msse2 -mfpmath=sse
endif
ANIM_FP_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(G3D_SRCS) src/nw4r/math/math_triangular.cpp \
    src/nw4r/math/math_types.cpp src/nw4r/math/math_arithmetic.cpp \
    src/nw4r/g3d/platform/g3d_cpu.cpp $(MTX_SRCS))
$(ANIM_FP_OBJS): CXXFLAGS += $(ANIM_FP)
$(ANIM_FP_OBJS): CFLAGS += $(ANIM_FP)

$(OUT)/anm_harness: $(HOST)/tests/anm_harness.cpp $(HOST)/tests/disc_walk.h $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(LIB) -Wl,--gc-sections -o $@

.PHONY: anims
all: $(OUT)/anm_harness
anims: $(OUT)/anm_harness
	python3 $(HOST)/tests/test_anims.py --harness $(OUT)/anm_harness --disc $(DISC)

# `clean` keeps the Aurora build (build/aurora: slow to rebuild, downloads);
# `distclean` removes it too.
clean:
	rm -rf $(HOST)/build/host64 $(HOST)/build/host32

distclean:
	rm -rf $(HOST)/build

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)

# --- nw4r::ef resources (POINTERS.md step 6; docs/HOST_BUILD.md §12) ---------
# Resource::Add / AddTexture / RelocateCommand / Remove*, the emitter track
# tables and ResEmitter/ResAnimCurve accessors, linked into ef_harness with the
# RES_LIST helper. ef_animcurve.cpp (curve evaluation; FUNCTIONAL in the decomp) is
# linked into ef_harness only, which evaluates every curve with --curves
# (docs/HOST_BUILD.md §17); on -m32 it is built with ANIM_FP (SSE floats).
EF_SRCS := src/nw4r/ef/ef_resource.cpp src/nw4r/ef/ef_res_emitter_ac.cpp \
    src/nw4r/ef/ef_res_animcurve_ac.cpp
EF_CURVE_SRCS := src/nw4r/ef/ef_animcurve.cpp
EF_HOST_SRCS := src/host_reslist.cpp
EF_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(EF_SRCS)) $(patsubst %,$(OUT)/host/%.o,$(EF_HOST_SRCS))
EF_CURVE_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(EF_CURVE_SRCS))
$(EF_CURVE_OBJS): CXXFLAGS += $(ANIM_FP)

$(OUT)/ef_harness: $(HOST)/tests/ef_harness.cpp $(HOST)/tests/disc_walk.h $(EF_OBJS) $(EF_CURVE_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(EF_OBJS) $(EF_CURVE_OBJS) $(LIB) -o $@

$(OUT)/test_ef: $(HOST)/tests/test_ef.cpp $(EF_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(EF_OBJS) $(LIB) -o $@

.PHONY: effects effects-unit efcurves
all: $(OUT)/ef_harness $(OUT)/test_ef
unit: effects-unit
effects-unit: $(OUT)/test_ef
	$(OUT)/test_ef
effects: $(OUT)/ef_harness
	python3 $(HOST)/tests/test_effects.py --harness $(OUT)/ef_harness --disc $(DISC)
efcurves: $(OUT)/ef_harness
	python3 $(HOST)/tests/test_efcurves.py --harness $(OUT)/ef_harness --disc $(DISC)
test: effects efcurves

# --- nw4r::snd sound archives (POINTERS.md step 7; docs/HOST_BUILD.md §13) ----
# The whole snd library (all units compile on the host; the archive below only
# links what the harness reaches), the ut file streams DvdSoundArchive uses,
# EGG::ArcPlayer (the game's archive open / group load calls) and the SDK MEM
# frame heap behind snd::FrameHeap. AX, AI, DSP and the sound thread are host
# stubs (src/host_snd.cpp, src/host_snd_stubs.cpp); there is no playback.
SND_SRCS := $(sort $(wildcard $(REPO)/src/nw4r/snd/*.cpp))
SND_SRCS := $(patsubst $(REPO)/%,%,$(SND_SRCS)) \
    src/nw4r/ut/ut_IOStream.cpp src/nw4r/ut/ut_FileStream.cpp \
    src/nw4r/ut/ut_DvdFileStream.cpp src/nw4r/ut/ut_DvdLockedFileStream.cpp \
    src/egg/audio/eggAudioArcPlayerMgr.cpp src/egg/core/eggCntFile.cpp \
    src/nw4r/ut/ut_NandFileStream.cpp \
    src/revolution/MEM/mem_frameHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c
SND_HOST_SRCS := src/host_snd.cpp src/host_snd_stubs.cpp
SND_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(SND_SRCS)) \
    $(patsubst %,$(OUT)/host/%.o,$(SND_HOST_SRCS))
SND_LIB := $(OUT)/libwiiplay_snd.a

# mem_frameHeap.c uses CodeWarrior's __abs intrinsic.
$(OUT)/decomp/src/revolution/MEM/%.c.o: CFLAGS += -D__abs=__builtin_abs

$(SND_LIB): $(SND_OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/snd_harness: $(HOST)/tests/snd_harness.cpp $(HOST)/tests/disc_walk.h $(SND_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(SND_LIB) $(LIB) $(SND_LIB) -Wl,--gc-sections -o $@

.PHONY: sound
all: $(OUT)/snd_harness
sound: $(OUT)/snd_harness
	python3 $(HOST)/tests/test_sound.py --harness $(OUT)/snd_harness --disc $(DISC)
test: sound

# --- Game-owned formats (POINTERS.md step 8; docs/HOST_BUILD.md §14, §15) ----
# The game's units that load them call RPSysResourceManager's two archive
# lookups; the harnesses define them over the archive the disc walk mounted.
# -mfpmath=sse on -m32 keeps the collision queries in single precision (x87
# would compute them in extended precision), as on x86-64.
ifeq ($(M32),1)
STEP8_FP := -msse2 -mfpmath=sse
endif
RPPC := src/Pack/RPParty/RPPartyCommon
COLL_SRCS := $(RPPC)/RPKcl.cpp $(RPPC)/RPResFile_2.cpp \
    $(RPPC)/RPResFile_3.cpp $(RPPC)/RPResFile_4.cpp $(RPPC)/RPPathFollower.cpp \
    $(RPPC)/RPPathFollower_2.cpp $(RPPC)/RPPathFollower_3.cpp $(RPPC)/RPPathSegment.cpp \
    src/egg/math/eggMatrix.cpp src/egg/math/eggMath.cpp src/egg/math/eggVector.cpp \
    src/egg/math/eggQuat.cpp
COLL_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(COLL_SRCS))
# RPCollisionModel (RPKcl's subclass; CheckSphere reads the header copy) is
# compiled only: it needs RPBBox and the RP matrix helpers to link.
COLL_COMPILE_OBJS := $(OUT)/decomp/$(RPPC)/RPCollisionModel.cpp.o

$(patsubst %,$(OUT)/decomp/%.o,$(filter src/Pack/%,$(COLL_SRCS))) $(COLL_COMPILE_OBJS): CXXFLAGS += $(STEP8_FP)

$(OUT)/coll_harness: $(HOST)/tests/coll_harness.cpp $(HOST)/tests/disc_walk.h $(COLL_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $(STEP8_FP) -MMD $< $(COLL_OBJS) $(LIB) -o $@

.PHONY: collision
all: $(OUT)/coll_harness $(COLL_COMPILE_OBJS)
collision: $(OUT)/coll_harness
	python3 $(HOST)/tests/test_collision.py --harness $(OUT)/coll_harness --disc $(DISC)
test: collision

# EGG binary settings and game tables: the EGG gfx/gfxe/math code the light,
# light-map and bloom objects are made of, the table loaders that build on the
# host, and src/host_gfx_stubs.cpp for the GX / g3d scene functions that code
# references (they abort if called; nothing on the load path calls them).
DUC := src/Pack/RPParty/RPDucScene
TNK := src/Pack/RPParty/RPTnkScene
DATA_SRCS := $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/egg/gfxe/*.cpp) \
    $(wildcard $(REPO)/src/egg/gfx/*.cpp) $(wildcard $(REPO)/src/egg/math/*.cpp))) \
    $(RPPC)/RPTnkMapData.cpp src/Pack/RPParty/RPBomScene/RPBomQuestionParam.cpp \
    $(RPPC)/RPSceneObj.cpp \
    src/Pack/RPParty/RPWlyScene/RPWlyLevelTable.cpp src/Pack/RPParty/RPPnpScene/RPPnpLevelTable.cpp \
    $(DUC)/RPDucCircleTarget.cpp $(DUC)/RPDucDuck.cpp $(DUC)/RPDucUfo.cpp \
    $(TNK)/RPTnkGameManager.cpp $(TNK)/RPTnkTask_2.cpp
DATA_HOST_SRCS := src/host_gfx_stubs.c
DATA_OBJS := $(sort $(patsubst %,$(OUT)/decomp/%.o,$(DATA_SRCS))) \
    $(patsubst %,$(OUT)/host/%.o,$(DATA_HOST_SRCS))
# Other readers of the same files, compiled only (docs/HOST_BUILD.md §15): the
# Balloon / Can / Clay loaders (their Load goes on to build models) and the Tnk
# stage map (LoadStage reads the stage records' enemy types).
DATA_COMPILE_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(DUC)/RPDucBalloon.cpp $(DUC)/RPDucCan.cpp \
    $(DUC)/RPDucClay.cpp $(TNK)/RPTnkStageMap.cpp)

$(OUT)/data_harness: $(HOST)/tests/data_harness.cpp $(HOST)/tests/disc_walk.h $(DATA_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(DATA_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: data
all: $(OUT)/data_harness $(DATA_COMPILE_OBJS)
data: $(OUT)/data_harness
	python3 $(HOST)/tests/test_data.py --harness $(OUT)/data_harness --disc $(DISC)
test: data

# --- Remaining loaders (POINTERS.md step 8; docs/HOST_BUILD.md §16) ---------
# BTI: RPGrpTexture / EGG::GXUtility::getTexObj / DrawGX::LoadTexture on every
# .bti, and the Tanks scene's RPTnkFootmarkTex::Load.
BTI_SRCS := src/Pack/RPGraphics/RPGrpTexture.cpp src/Pack/RPParty/RPTnkScene/RPTnkTask_3.cpp \
    $(RPPC)/RPSceneObj.cpp src/egg/gfxe/eggGXUtility.cpp src/egg/gfxe/eggDrawGX.cpp
BTI_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(BTI_SRCS)) $(OUT)/host/src/host_gfx_stubs.c.o

$(OUT)/bti_harness: $(HOST)/tests/bti_harness.cpp $(HOST)/tests/disc_walk.h $(BTI_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(BTI_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: bti
all: $(OUT)/bti_harness
bti: $(OUT)/bti_harness
	python3 $(HOST)/tests/test_bti.py --harness $(OUT)/bti_harness --disc $(DISC)
test: bti

# RFL_Res.dat: all of RVLFaceLib, linked with --gc-sections against the MEM
# expanded heap and tests/rfl_host.c (NAND, alarm, GX display-list and other
# SDK stand-ins the library references; no NAND on the host).
RFL_SRCS := $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/RVLFaceLib/*.c))) \
    src/revolution/MEM/mem_expHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c
RFL_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(RFL_SRCS)) $(OUT)/tests/rfl_host.c.o

$(OUT)/tests/%.c.o: $(HOST)/tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OUT)/rfl_harness: $(HOST)/tests/rfl_harness.cpp $(HOST)/tests/disc_walk.h $(RFL_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(RFL_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: rfl
all: $(OUT)/rfl_harness
rfl: $(OUT)/rfl_harness
	python3 $(HOST)/tests/test_rfl.py --harness $(OUT)/rfl_harness --disc $(DISC)
test: rfl

# HOME Menu: homebuttonMiniLib's HBMCreate on the disc's HOME Menu files
# (layouts through nw4r::lyt, the snd library it references, the MEM expanded
# heap), with tests/hbm_host.cpp for the KPAD / AX / OS calls it references.
HBM_SRCS := $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/homebuttonMiniLib/*.cpp))) \
    src/revolution/MEM/mem_expHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c
# The remote speaker path runs on the §23 WPAD backend (with SC for its
# settings) and the decomp's WENC encoder.
HBM_SRCS += src/revolution/WENC/wenc.c
HBM_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(HBM_SRCS)) $(OUT)/tests/hbm_host.cpp.o \
    $(OUT)/host/src/host_wpad.cpp.o $(OUT)/host/src/host_sc.cpp.o

$(OUT)/tests/%.cpp.o: $(HOST)/tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(OUT)/hbm_harness: $(HOST)/tests/hbm_harness.cpp $(HOST)/tests/disc_walk.h $(HBM_OBJS) $(SND_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(HBM_OBJS) $(SND_LIB) $(LIB) $(SND_LIB) -Wl,--gc-sections -o $@

.PHONY: hbm
all: $(OUT)/hbm_harness
hbm: $(OUT)/hbm_harness
	python3 $(HOST)/tests/test_hbm.py --harness $(OUT)/hbm_harness --disc $(DISC)
test: hbm

# POINTERS.md §5.1 leftovers: RPPolyline::Calc (pointer-sized arithmetic; run
# by test_polyline). Compiled only, so their host forms keep building:
# RPKokeshiMgr_3 (EGG::Frustum::mFlags by name; an unlinked FUNCTIONAL unit),
# and the game's RFL / HBM callers RPSysKokeshiManager (RFLInitRes),
# RPSysHomeMenuMgr (HBMDataInfo) and g3d_scnrfl (RFLCharModel).
POLY_OBJS := $(OUT)/decomp/$(RPPC)/RPPolyline.cpp.o
KOKESHI3_OBJS := $(OUT)/decomp/$(RPPC)/RPKokeshiMgr_3.cpp.o \
    $(OUT)/decomp/src/Pack/RPKernel/RPSysKokeshiManager.cpp.o \
    $(OUT)/decomp/src/Pack/RPKernel/RPSysHomeMenuMgr.cpp.o \
    $(OUT)/decomp/src/nw4r/g3d/g3d_scnrfl.cpp.o

$(OUT)/test_polyline: $(HOST)/tests/test_polyline.cpp $(POLY_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(POLY_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: polyline-unit
all: $(OUT)/test_polyline $(KOKESHI3_OBJS)
unit: polyline-unit
polyline-unit: $(OUT)/test_polyline $(KOKESHI3_OBJS)
	$(OUT)/test_polyline

# --- nw4r::ef runtime (Phase 7; docs/HOST_BUILD.md §19) -----------------------
# EffectSystem / Effect / Emitter / ParticleManager / Particle, the emit forms and
# the game's RPSysParticleManager and RPSysEffectMemoryManager, linked with the
# resource units into ptcl_harness, which creates every emitter on the disc and
# steps it (tests/test_particles.py). The draw strategies are linked (the
# builder hands them out in ParticleManager::Initialize); their GX calls are
# src/host_gfx_stubs.c entries that abort if reached.
PTCL_SRCS := $(addprefix src/nw4r/ef/,ef_effectsystem.cpp ef_effect.cpp ef_emitter.cpp \
    ef_particlemanager.cpp ef_particle.cpp ef_emitterform.cpp ef_creationqueue.cpp \
    ef_draworder.cpp ef_handle.cpp ef_util.cpp) \
    $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/nw4r/ef/emform/*.cpp))) \
    $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/nw4r/ef/drawstrategy/*.cpp))) \
    src/Pack/RPKernel/RPSysParticleManager.cpp
PTCL_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(PTCL_SRCS)) $(OUT)/host/src/host_gfx_stubs.c.o
# On -m32, single-precision SSE arithmetic as in §17 (x87 would round differently);
# host_mtx.cpp too (PSMTXInverse, reached through MTX34Inv in every Calc).
$(PTCL_OBJS) $(OUT)/host/src/host_mtx.cpp.o: CXXFLAGS += $(ANIM_FP)

$(OUT)/ptcl_harness: $(HOST)/tests/ptcl_harness.cpp $(HOST)/tests/disc_walk.h $(PTCL_OBJS) $(EF_OBJS) $(EF_CURVE_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(PTCL_OBJS) $(EF_OBJS) $(EF_CURVE_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: particles
all: $(OUT)/ptcl_harness
# --compare: the other build's harness, if it has been built, must print the same.
PTCL_OTHER := $(HOST)/build/$(if $(filter 1,$(M32)),host64,host32)/ptcl_harness
particles: $(OUT)/ptcl_harness
	python3 $(HOST)/tests/test_particles.py --harness $(OUT)/ptcl_harness --disc $(DISC) \
	    --compare $(PTCL_OTHER)
test: particles

# --- nw4r::g3d scene / model runtime (Phase 7; docs/HOST_BUILD.md §20) --------
# ScnRoot / ScnMdl / ScnMdlSimple with CalcWorld / CalcMaterial / CalcView, the
# AnmObj units and the DCC world-matrix rules, on an EGG::ExpHeap allocator as
# the game sets it up. mdl_harness builds every model on the disc, applies every
# compatible animation and prints the world, view and material results
# (tests/test_scene.py). Draw paths are linked; their GX and FIFO calls are
# src/host_gfx_stubs.c entries that abort if reached.
MDL_SRCS := $(addprefix src/nw4r/g3d/,g3d_scnroot.cpp g3d_scnobj.cpp g3d_scnmdl.cpp \
    g3d_scnmdlsmpl.cpp g3d_scnproc.cpp g3d_calcworld.cpp g3d_calcview.cpp g3d_calcvtx.cpp \
    g3d_calcmaterial.cpp g3d_anmobj.cpp g3d_anmchr.cpp g3d_anmclr.cpp g3d_anmtexsrt.cpp \
    g3d_anmtexpat.cpp g3d_anmvis.cpp g3d_anmshp.cpp g3d_anmscn.cpp g3d_obj.cpp g3d_basic.cpp \
    g3d_dcc.cpp g3d_maya.cpp g3d_xsi.cpp g3d_3dsmax.cpp g3d_workmem.cpp g3d_camera.cpp \
    g3d_light.cpp g3d_fog.cpp g3d_state.cpp g3d_draw.cpp g3d_draw1mat1shp.cpp g3d_init.cpp) \
    src/nw4r/math/math_geometry.cpp src/nw4r/ut/ut_LockedCache.cpp \
    src/revolution/MEM/mem_expHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c src/revolution/MEM/mem_allocator.c
MDL_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(MDL_SRCS)) $(OUT)/host/src/host_gfx_stubs.c.o \
    $(OUT)/host/src/host_g3d.cpp.o $(OUT)/host/src/host_gxlight.cpp.o
$(MDL_OBJS): CXXFLAGS += $(ANIM_FP)

$(OUT)/mdl_harness: $(HOST)/tests/mdl_harness.cpp $(HOST)/tests/disc_walk.h $(MDL_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(MDL_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: scene
all: $(OUT)/mdl_harness
# --compare: the other build's harness, if it has been built, must print the same.
MDL_OTHER := $(HOST)/build/$(if $(filter 1,$(M32)),host64,host32)/mdl_harness
scene: $(OUT)/mdl_harness
	python3 $(HOST)/tests/test_scene.py --harness $(OUT)/mdl_harness --disc $(DISC) \
	    --compare $(MDL_OTHER)
test: scene

# --- nw4r::snd runtime and the AX mixer (Phase 7; docs/HOST_BUILD.md §21) -----
# The sound runtime (SoundSystem, SoundArchivePlayer, SeqPlayer, WsdPlayer, the
# sound and task threads) plays on the host AX DSP (src/host_ax.cpp: the
# decomp's AX CPU code with a software mixer behind it), with cooperative
# threads (src/host_thread.cpp), the decomp's AXFX effects and EGG's audio
# managers. audio_harness renders sounds from the disc (tests/test_audio.py).
AUDIO_SRCS := $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/nw4r/snd/*.cpp))) \
    src/nw4r/ut/ut_IOStream.cpp src/nw4r/ut/ut_FileStream.cpp \
    src/nw4r/ut/ut_DvdFileStream.cpp src/nw4r/ut/ut_DvdLockedFileStream.cpp \
    src/nw4r/ut/ut_NandFileStream.cpp src/egg/core/eggCntFile.cpp \
    $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/egg/audio/*.cpp))) \
    $(addprefix src/revolution/AX/,AX.c AXAlloc.c AXVPB.c AXAux.c AXSPB.c AXProf.c) \
    $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/revolution/AXFX/*.c))) \
    src/revolution/MEM/mem_frameHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c
# The game's own audio manager (RPSndAudioMgr and the RPAudio units it calls)
# on the §22 platform layer's EGG heaps, with the §23 WPAD backend under the
# remote speaker code (and the decomp's WENC encoder); the controller manager
# and KPAD for the two controller queries RPSndAudioMgr / RPSndSpeakerMgr make.
AUDIO_SRCS += $(addprefix src/Pack/RPAudio/,RPSndAudioMgr.cpp RPSndStaticMgr.cpp \
    RPSndSpeakerMgr.cpp RPSndMoveParam.cpp RPSndUtility.cpp) \
    src/Pack/RPSystem/RPSysProjectLocal.cpp src/revolution/WENC/wenc.c \
    src/egg/core/eggController.cpp src/Pack/RPKernel/RPSysControllerMgr.cpp \
    src/revolution/KPAD/KPAD.c
AUDIO_HOST_SRCS := src/host_snd.cpp src/host_ax.cpp src/host_thread.cpp src/host_os_clock.c \
    src/host_wpad.cpp
AUDIO_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(AUDIO_SRCS)) \
    $(patsubst %,$(OUT)/host/%.o,$(AUDIO_HOST_SRCS)) $(OUT)/tests/audio_host.cpp.o
# On -m32, single-precision SSE arithmetic as in §17 (x87 would round the
# runtime's float volume / pitch / pan computations differently).
$(AUDIO_OBJS): CXXFLAGS += $(ANIM_FP)
$(AUDIO_OBJS): CFLAGS += $(ANIM_FP)
# The heap layer and EGG math as sys_harness / input_harness link them (without
# $(ANIM_FP), as there).
AUDIO_HEAP_SRCS := $(addprefix src/egg/core/,eggHeap.cpp eggExpHeap.cpp eggFrmHeap.cpp \
    eggAllocator.cpp eggSystem.cpp) \
    $(addprefix src/revolution/MEM/,mem_expHeap.c mem_allocator.c) \
    src/revolution/OS/OSArena.c src/revolution/OS/OSAlloc.c \
    src/egg/math/eggVector.cpp src/egg/math/eggMatrix.cpp src/egg/math/eggMath.cpp
AUDIO_HEAP_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(AUDIO_HEAP_SRCS)) \
    $(OUT)/host/src/host_os.cpp.o $(OUT)/host/src/host_sc.cpp.o
$(OUT)/decomp/src/revolution/WENC/%.c.o: CFLAGS += -D__abs=__builtin_abs

$(OUT)/audio_harness: $(HOST)/tests/audio_harness.cpp $(HOST)/tests/disc_walk.h $(AUDIO_OBJS) \
    $(AUDIO_HEAP_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(AUDIO_OBJS) $(AUDIO_HEAP_OBJS) $(LIB) \
	    -Wl,--gc-sections -o $@

.PHONY: audio
all: $(OUT)/audio_harness
# --compare: the other build's harness, if it has been built, must print the same.
AUDIO_OTHER := $(HOST)/build/$(if $(filter 1,$(M32)),host64,host32)/audio_harness
audio: $(OUT)/audio_harness
	python3 $(HOST)/tests/test_audio.py --harness $(OUT)/audio_harness --disc $(DISC) \
	    --compare $(AUDIO_OTHER)
test: audio

# --- OS / storage platform layer (Phase 7; docs/HOST_BUILD.md §22) -------------
# The real EGG heaps (Heap / ExpHeap / FrmHeap / Allocator) over the decomp's MEM
# heaps and OSAlloc / OSArena, inside the M0 arenas (src/host_os.cpp); the DVD
# file system (dvdfs.c, dvdqueue.c) on the host drive (src/host_dvd.cpp); NAND
# (src/host_nand.cpp) and SC (src/host_sc.cpp); and the game's RPSysSystem,
# RPSysResourceManager and RPSysSaveDataMgr on top. sys_harness boots the way the
# game does and runs them (tests/test_sys.py). Test saves go to a scratch NAND
# directory (SYS_SCRATCH, wiped by each run), never into the repository or the
# disc: the banner file holds the disc's banner images.
SYS_SRCS := $(addprefix src/egg/core/,eggHeap.cpp eggExpHeap.cpp eggFrmHeap.cpp eggAllocator.cpp \
    eggStream.cpp eggThread.cpp eggTaskThread.cpp eggSystem.cpp eggScene.cpp eggSceneManager.cpp) \
    $(addprefix src/revolution/MEM/,mem_expHeap.c mem_frameHeap.c mem_heapCommon.c mem_list.c \
    mem_allocator.c) \
    src/revolution/OS/OSArena.c src/revolution/OS/OSAlloc.c \
    src/revolution/DVD/dvdfs.c src/revolution/DVD/dvdqueue.c \
    $(addprefix src/Pack/RPSystem/,RPSysResourceManager.cpp RPSysSaveDataMgr.cpp RPSysSaveData.cpp \
    RPPartyPlayerData.cpp RPPartySystemData.cpp RPSportsPlayerData.cpp RPSysSceneCreator.cpp \
    RPSysProjectLocal.cpp) \
    $(addprefix src/Pack/RPKernel/,RPSysSystem.cpp RPSysSystem_2.cpp RPSysFile.cpp \
    IRPSysHostIOSocket.cpp)
SYS_HOST_SRCS := src/host_os.cpp src/host_dvd.cpp src/host_nand.cpp src/host_sc.cpp \
    src/host_thread.cpp src/host_os_clock.c
SYS_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(SYS_SRCS)) \
    $(patsubst %,$(OUT)/host/%.o,$(SYS_HOST_SRCS)) $(OUT)/tests/sys_host.c.o \
    $(OUT)/tests/rpsysmessage_unit.cpp.o

$(OUT)/sys_harness: $(HOST)/tests/sys_harness.cpp $(SYS_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(SYS_OBJS) $(LIB) -Wl,--gc-sections -o $@

SYS_SCRATCH ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/wiiplay-sys-$(notdir $(OUT))
.PHONY: platform
all: $(OUT)/sys_harness
# --compare: the other build's harness, if it has been built, must print the same.
SYS_OTHER := $(HOST)/build/$(if $(filter 1,$(M32)),host64,host32)/sys_harness
platform: $(OUT)/sys_harness
	python3 $(HOST)/tests/test_sys.py --harness $(OUT)/sys_harness --disc $(DISC) \
	    --scratch $(SYS_SCRATCH) --compare $(SYS_OTHER)
test: platform

# --- Input platform layer (Phase 7; docs/HOST_BUILD.md §23) --------------------
# The host WPAD backend (src/host_wpad.cpp: four virtual remotes, 200 Hz
# reports) under the decomp's KPAD (unmodified), EGG::CoreController and the
# game's RPSysCoreController / RPSysCoreControllerMgr / RPSysPairingMgr; the
# mapping layer (src/host_input.cpp: pointer -> sensor-bar dots by inverting
# KPAD's math, tilt -> accelerometer counts, buttons, Nunchuk) fed by scripted
# input. input_harness prints KPAD's status and the game's cursor per frame
# (tests/test_input.py checks them against the model).
INPUT_FP_SRCS := src/revolution/KPAD/KPAD.c src/egg/core/eggController.cpp \
    $(addprefix src/Pack/RPKernel/,RPSysController.cpp RPSysControllerMgr.cpp RPSysPairingMgr.cpp) \
    src/egg/gfxe/eggScreen.cpp src/egg/gfxe/eggFrustum.cpp src/Pack/RPGraphics/RPGrpScreen.cpp
INPUT_SRCS := $(INPUT_FP_SRCS) \
    $(addprefix src/egg/core/,eggHeap.cpp eggExpHeap.cpp eggFrmHeap.cpp eggAllocator.cpp eggSystem.cpp) \
    $(addprefix src/revolution/MEM/,mem_expHeap.c mem_frameHeap.c mem_heapCommon.c mem_list.c \
    mem_allocator.c) \
    src/revolution/OS/OSArena.c src/revolution/OS/OSAlloc.c \
    src/egg/math/eggVector.cpp src/egg/math/eggMatrix.cpp src/egg/math/eggMath.cpp \
    src/Pack/RPKernel/IRPSysHostIOSocket.cpp
INPUT_HOST_SRCS := src/host_wpad.cpp src/host_input.cpp src/host_os.cpp src/host_sc.cpp
INPUT_FP_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(INPUT_FP_SRCS)) \
    $(OUT)/host/src/host_wpad.cpp.o $(OUT)/host/src/host_input.cpp.o $(OUT)/tests/input_host.cpp.o
INPUT_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(INPUT_SRCS)) \
    $(patsubst %,$(OUT)/host/%.o,$(INPUT_HOST_SRCS)) $(OUT)/tests/input_host.cpp.o
# On -m32, single-precision SSE arithmetic as in §17: KPAD's float math must
# round the same on both builds.
$(INPUT_FP_OBJS): CXXFLAGS += $(ANIM_FP)
$(INPUT_FP_OBJS): CFLAGS += $(ANIM_FP)

$(OUT)/input_harness: $(HOST)/tests/input_harness.cpp $(INPUT_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(INPUT_OBJS) $(LIB) -Wl,--gc-sections -o $@

.PHONY: input
all: $(OUT)/input_harness
# --compare: the other build's harness, if it has been built, must print the same.
INPUT_OTHER := $(HOST)/build/$(if $(filter 1,$(M32)),host64,host32)/input_harness
input: $(OUT)/input_harness
	python3 $(HOST)/tests/test_input.py --harness $(OUT)/input_harness \
	    --scripts $(HOST)/tests/input --compare $(INPUT_OTHER)
test: input

# --- GX layer on Aurora (docs/HOST_BUILD.md §25) -------------------------------
# 64-bit only (Dawn has no 32-bit builds): with M32=1 these targets are skipped.
# CMake (port/host/aurora/CMakeLists.txt) builds Aurora and the glue library
# libwiiplay_gx.so into build/aurora; only GX-layer targets link it. NO_GX=1
# skips them on 64-bit too (no CMake, no downloads).
ifneq ($(M32),1)
ifneq ($(NO_GX),1)
AURORA_BUILD := $(HOST)/build/aurora
AURORA_STAMP := $(AURORA_BUILD)/.configured
WPGX_LIB := $(AURORA_BUILD)/libwiiplay_gx.so
CMAKE ?= cmake
AURORA_GENERATOR := $(if $(shell command -v ninja 2>/dev/null),Ninja,Unix Makefiles)
AURORA_JOBS ?= $(shell nproc 2>/dev/null || echo 4)
GX_SCRATCH ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/wiiplay-gx
LAVAPIPE_ICD ?= $(firstword $(wildcard /usr/share/vulkan/icd.d/lvp_icd*.json /etc/vulkan/icd.d/lvp_icd*.json))

$(AURORA_STAMP): $(HOST)/aurora/CMakeLists.txt
	@mkdir -p $(AURORA_BUILD)
	@echo "cmake: configuring Aurora in $(AURORA_BUILD) (log: configure.log)"
	@$(CMAKE) -G "$(AURORA_GENERATOR)" -S $(HOST)/aurora -B $(AURORA_BUILD) \
	    -DCMAKE_BUILD_TYPE=Release > $(AURORA_BUILD)/configure.log 2>&1 \
	    || { tail -30 $(AURORA_BUILD)/configure.log; exit 1; }
	@touch $@

# One CMake build for both GX targets (the glue library and Aurora's tests),
# always handed to CMake, which rebuilds only what changed (the library's
# timestamp moves only when it is relinked). A single recipe: two parallel
# `cmake --build` runs in one tree both re-run the configure step after a
# submodule change and fail ("Configuring incomplete").
$(WPGX_LIB): $(AURORA_STAMP) FORCE
	@$(CMAKE) --build $(AURORA_BUILD) --target wiiplay_gx gx_fifo_tests gfx_readback_tests -j $(AURORA_JOBS) > $(AURORA_BUILD)/build.log 2>&1 \
	    || { grep -E "error|FAILED|CMake Error" $(AURORA_BUILD)/build.log | head -20; tail -5 $(AURORA_BUILD)/build.log; exit 1; }

$(OUT)/gx_harness: $(HOST)/tests/gx_harness.cpp $(HOST)/tests/disc_walk.h $(HOST)/aurora/wpgx.h $(WPGX_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) -I$(HOST)/aurora -MMD $< $(LIB) -L$(AURORA_BUILD) -lwiiplay_gx \
	    -Wl,-rpath,$(AURORA_BUILD) -o $@

# The decomp's GX SDK as the command encoder (docs/HOST_BUILD.md §26): the real
# units, plus host GXFifo/GXMisc (src/host_gxfifo.cpp, host_gxmisc.cpp) in place of
# the hardware FIFO and PE interrupt code. GNU89 inline: the SDK's plain `inline`
# helpers (GXTransform.c) also get an out-of-line copy, as with CodeWarrior.
GX_SDK_SRCS := $(addprefix src/revolution/GX/,GXAttr.c GXBump.c GXDisplayList.c GXDraw.c GXGeometry.c \
    GXInit.c GXLight.c GXPixel.c GXTev.c GXTexture.c GXTransform.c GXFrameBuf.c GXPerf.c)
GX_SDK_DECOMP_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(GX_SDK_SRCS))
$(GX_SDK_DECOMP_OBJS): CFLAGS += -fgnu89-inline
GX_SDK_OBJS := $(GX_SDK_DECOMP_OBJS) $(OUT)/host/src/host_gxfifo.cpp.o $(OUT)/host/src/host_gxmisc.cpp.o

# The health and safety screen through lyt, the GX SDK and Aurora (§26).
$(OUT)/gxlyt_harness: $(HOST)/tests/gxlyt_harness.cpp $(HOST)/aurora/wpgx.h $(GX_SDK_OBJS) $(WPGX_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) -I$(HOST)/aurora -MMD $< $(GX_SDK_OBJS) $(LIB) -L$(AURORA_BUILD) -lwiiplay_gx \
	    -Wl,-rpath,$(AURORA_BUILD) -o $@

# A minigame's 3D scene through g3d, EGG and the GX SDK (§27): the g3d scene,
# calc and draw units of mdl_harness (§20) with g3d's FIFO writes
# (platform/g3d_gpu.cpp, g3d_tmem.cpp), EGG's gfx units (ScnRenderer, light and
# fog managers, Screen, StateGX, DrawGX) and the game's camera and screen. The
# real GXLight.c replaces host_gxlight.cpp; host_gfx_stubs.c's entries are weak.
GX3D_SRCS := src/nw4r/g3d/platform/g3d_gpu.cpp src/nw4r/g3d/platform/g3d_tmem.cpp \
    $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/egg/gfxe/*.cpp) \
    $(wildcard $(REPO)/src/egg/gfx/*.cpp) $(wildcard $(REPO)/src/egg/math/*.cpp))) \
    src/Pack/RPGraphics/RPGrpCamera.cpp src/Pack/RPGraphics/RPGrpScreen.cpp \
    src/Pack/RPParty/RPPartyCommon/RPKcl_3.cpp
GX3D_OBJS := $(filter-out $(OUT)/host/src/host_gxlight.cpp.o,$(MDL_OBJS)) \
    $(sort $(patsubst %,$(OUT)/decomp/%.o,$(GX3D_SRCS))) $(OUT)/host/src/host_sc.cpp.o
$(OUT)/gx3d_harness: $(HOST)/tests/gx3d_harness.cpp $(HOST)/tests/disc_walk.h $(HOST)/aurora/wpgx.h \
    $(GX_SDK_OBJS) $(GX3D_OBJS) $(WPGX_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) -I$(HOST)/aurora -MMD $< $(GX_SDK_OBJS) $(GX3D_OBJS) $(LIB) \
	    -L$(AURORA_BUILD) -lwiiplay_gx -Wl,-rpath,$(AURORA_BUILD) -Wl,--gc-sections -o $@

# The game's own boot and frame loop (docs/HOST_BUILD.md §29): the decomp's main,
# RPSysSystem, the scene manager and creator, the boot (health and safety) and
# title scenes, on every platform layer (§22 heaps/DVD/NAND/SC, §23 WPAD, §21 AX,
# §26-28 GX on Aurora) plus a host VI (src/host_vi.cpp) that paces game time.
# The game's units are taken whole and linked with --gc-sections: only what the
# reached code references stays. src/host_boot.cpp has the stand-ins that remain.
RP_DIRS := RPKernel RPSystem RPGraphics RPAudio RPUtility RPParty/RPPartyCommon \
    $(addprefix RPParty/,RPBilScene RPBomScene RPCowScene RPDucScene RPFshScene RPHkyScene RPPnpScene \
    RPTnkScene RPWlyScene)
BOOT_RP_SRCS := $(patsubst $(REPO)/%,%,$(sort $(foreach d,$(RP_DIRS),$(wildcard $(REPO)/src/Pack/$(d)/*.cpp))))
BOOT_LIB_SRCS := $(patsubst $(REPO)/%,%,$(sort \
    $(wildcard $(REPO)/src/egg/*/*.cpp) \
    $(wildcard $(REPO)/src/nw4r/ut/*.cpp) $(wildcard $(REPO)/src/nw4r/math/*.cpp) \
    $(wildcard $(REPO)/src/nw4r/ef/*.cpp) $(wildcard $(REPO)/src/nw4r/ef/*/*.cpp) \
    $(wildcard $(REPO)/src/nw4r/g3d/*.cpp) $(wildcard $(REPO)/src/nw4r/g3d/*/*.cpp) \
    $(wildcard $(REPO)/src/homebuttonMiniLib/*.cpp) $(wildcard $(REPO)/src/RVLFaceLib/*.c) \
    $(wildcard $(REPO)/src/revolution/AXFX/*.c)))
BOOT_SDK_SRCS := $(addprefix src/revolution/AX/,AX.c AXAlloc.c AXVPB.c AXAux.c AXSPB.c AXProf.c) \
    $(addprefix src/revolution/MEM/,mem_expHeap.c mem_frameHeap.c mem_heapCommon.c mem_list.c \
    mem_allocator.c) \
    src/revolution/OS/OSArena.c src/revolution/OS/OSAlloc.c \
    src/revolution/DVD/dvdfs.c src/revolution/DVD/dvdqueue.c \
    src/revolution/KPAD/KPAD.c src/revolution/WENC/wenc.c
# Minigame units the party menus reference (the Mii entries of Wally's game config)
BOOT_SRCS := $(sort $(BOOT_RP_SRCS) $(BOOT_LIB_SRCS) $(BOOT_SDK_SRCS) $(SND_SRCS) \
    src/Pack/RPParty/RPWlyScene/RPWlyGameConfig.cpp)
BOOT_HOST_SRCS := src/host_os.cpp src/host_dvd.cpp src/host_nand.cpp src/host_sc.cpp \
    src/host_thread.cpp src/host_os_clock.c src/host_snd.cpp src/host_ax.cpp src/host_wpad.cpp \
    src/host_input.cpp src/host_reslist.cpp src/host_g3d.cpp src/host_gfx_stubs.c \
    src/host_vi.cpp src/host_boot.cpp src/host_live.cpp src/host_probe.cpp src/host_probe_games.cpp \
    src/host_probe_hbm.cpp src/host_perf.cpp src/host_main.cpp
BOOT_OBJS := $(sort $(patsubst %,$(OUT)/decomp/%.o,$(filter-out $(GX_SDK_SRCS),$(BOOT_SRCS))) \
    $(patsubst %,$(OUT)/host/%.o,$(BOOT_HOST_SRCS))) $(OUT)/decomp/src/main.cpp.o
# Data the decomp reaches by its CodeWarrior name (RPTextWriter: a private static; Laser
# Hockey and Charge!: EGG::Screen::sTVModeWidths, EGG::DrawGX::s_cameraMtx)
BOOT_CW_NAMES := -Wl,--defsym=spCurrentScreen__13RPGrpRenderer=_ZN13RPGrpRenderer15spCurrentScreenE \
    -Wl,--defsym=s_cameraMtx__Q23EGG6DrawGX=_ZN3EGG6DrawGX11s_cameraMtxE \
    -Wl,--defsym=sTVModeWidths__Q23EGG6Screen=_ZN3EGG6Screen13sTVModeWidthsE
# The decomp's main is the game's entry point; the host's main (host_main.cpp)
# sets up the platform and calls it under this name.
$(OUT)/decomp/src/main.cpp.o: CXXFLAGS += -Dmain=PortGameMain
$(OUT)/boot: $(HOST)/aurora/wpgx.h $(GX_SDK_OBJS) $(BOOT_OBJS) $(WPGX_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) $(GX_SDK_OBJS) $(BOOT_OBJS) $(LIB) \
	    -L$(AURORA_BUILD) -lwiiplay_gx -Wl,-rpath,$(AURORA_BUILD) -Wl,--gc-sections \
	    $(BOOT_CW_NAMES) -Wl,--wrap=KPADSetPosParam -o $@
$(OUT)/host/src/host_main.cpp.o $(OUT)/host/src/host_vi.cpp.o $(OUT)/host/src/host_live.cpp.o \
    $(OUT)/host/src/host_perf.cpp.o: CXXFLAGS += -I$(HOST)/aurora

.PHONY: gx gx-aurora-tests FORCE
FORCE:
all: $(OUT)/gx_harness $(OUT)/gxlyt_harness $(OUT)/gx3d_harness
# Aurora's own GX FIFO encoder/decoder tests (googletest), from our build.
gx-aurora-tests: $(WPGX_LIB)
	@$(AURORA_BUILD)/aurora/tests/gx_fifo_tests --gtest_brief=1 > $(AURORA_BUILD)/gx_fifo_tests.log 2>&1 \
	    || { tail -30 $(AURORA_BUILD)/gx_fifo_tests.log; exit 1; }
	@echo "gx_fifo_tests: $$(grep -E 'PASSED|tests ran' $(AURORA_BUILD)/gx_fifo_tests.log | tr '\n' ' ')"
	@# The GPU tests (readback, EFB copies, Z textures, logic ops; §28) on lavapipe
	@VK_ICD_FILENAMES=$(LAVAPIPE_ICD) MESA_SHADER_CACHE_DISABLE=true \
	    $(AURORA_BUILD)/aurora/tests/gfx_readback_tests --gtest_brief=1 > $(AURORA_BUILD)/gfx_readback_tests.log 2>&1 \
	    || { grep -vE '^\[(debug|info|2)\]' $(AURORA_BUILD)/gfx_readback_tests.log | tail -30; exit 1; }
	@echo "gfx_readback_tests: $$(grep -E 'PASSED|SKIPPED|tests ran' $(AURORA_BUILD)/gfx_readback_tests.log | tr '\n' ' ')"
# The bring-up render on lavapipe (software Vulkan; --gpu for the real adapter).
gx: $(OUT)/gx_harness $(OUT)/gxlyt_harness $(OUT)/gx3d_harness gx-aurora-tests
	python3 $(HOST)/tests/test_gx.py --harness $(OUT)/gx_harness --disc $(DISC) --scratch $(GX_SCRATCH)
	python3 $(HOST)/tests/test_gxlyt.py --harness $(OUT)/gxlyt_harness --disc $(DISC) --scratch $(GX_SCRATCH)
	python3 $(HOST)/tests/test_gx3d.py --harness $(OUT)/gx3d_harness --disc $(DISC) --scratch $(GX_SCRATCH)/gx3d
test: gx
# The game's boot to the title screen, the menus, short Shooting Range and Laser Hockey runs,
# headless on lavapipe (§29-§32)
.PHONY: boot
all: $(OUT)/boot
# Its own scratch directory (NAND saves, Aurora data): test_gx wipes GX_SCRATCH, and
# make runs them in parallel.
BOOT_SCRATCH ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/wiiplay-boot
boot: $(OUT)/boot
	python3 $(HOST)/tests/test_boot.py --boot $(OUT)/boot --disc $(DISC) --scratch $(BOOT_SCRATCH)
test: boot
# Whole minigames, twice each (§32): not part of `make test` (about 15 minutes); run before
# committing a game milestone.
.PHONY: playtest
PLAY_SCRATCH ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/wiiplay-play
playtest: $(OUT)/boot
	python3 $(HOST)/tests/test_play.py --boot $(OUT)/boot --disc $(DISC) --scratch $(PLAY_SCRATCH) $(PLAYTEST_ARGS)
# The two-player games and the later levels (§36): not part of `make playtest`; run before
# committing a game milestone, with it.
.PHONY: playtest2
PLAY2_SCRATCH ?= $(if $(TMPDIR),$(TMPDIR),/tmp)/wiiplay-play2
playtest2: $(OUT)/boot
	python3 $(HOST)/tests/test_play.py --set 2 --boot $(OUT)/boot --disc $(DISC) --scratch $(PLAY2_SCRATCH) $(PLAYTEST2_ARGS)
else
.PHONY: gx gx-aurora-tests boot playtest playtest2
gx gx-aurora-tests boot playtest playtest2:
	@echo "$@: skipped (NO_GX=1)"
test: gx boot
endif
else
.PHONY: gx gx-aurora-tests boot playtest playtest2
gx gx-aurora-tests boot playtest playtest2:
	@echo "$@: skipped (the GX layer is 64-bit only)"
test: gx boot
endif
