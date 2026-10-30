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
           -DVERSION_RHAE01 -DVERSION_RHAE01_01

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

COMMON := $(ARCH) -O2 -g -fno-strict-aliasing -ffunction-sections -fdata-sections $(WIDE) $(INCLUDES) $(DEFINES) $(WARN)
CFLAGS := -std=gnu11 $(COMMON) -Werror=pointer-to-int-cast
# Decompiled class layouts: offsetof on classes with vtables (nw4r lists),
# member init order and anonymous-namespace bases are as in the original.
CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti $(COMMON) \
            -Wno-invalid-offsetof -Wno-subobject-linkage -Wno-reorder
# g3d_resanmchr.cpp switches a u32 over enumerators of (1 << 31) (an int);
# CodeWarrior accepts it, C++11 calls it a narrowing conversion.
CXXFLAGS += -Wno-narrowing

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
    src/egg/util/eggMsgRes.cpp \
    $(LYT_SRCS) \
    $(G3D_SRCS)

# MSL_C wide-string functions, compiled under port_* names (port/wide.h).
# Their headers live in include/MSL/internal; include/MSL is searched last so
# the C library headers still win.
MSL_SRCS := \
    src/MSL_C/wstring.c \
    src/MSL_C/wmem.c

HOST_SRCS := src/host_sdk.cpp src/host_arena.cpp src/host_text.cpp src/host_tpl.cpp src/host_gx.cpp \
    src/host_gxtexobj.cpp

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
         $(OUT)/test_layout

.PHONY: all test unit archives resources layouts models clean
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

unit: $(TESTS) $(STEP4_OBJS)
	$(OUT)/test_be
	$(OUT)/test_be_c
	$(OUT)/test_port
	$(OUT)/test_port_c
	$(OUT)/test_layout

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
    src/nw4r/math/math_types.cpp)
$(ANIM_FP_OBJS): CXXFLAGS += $(ANIM_FP)

$(OUT)/anm_harness: $(HOST)/tests/anm_harness.cpp $(HOST)/tests/disc_walk.h $(LIB)
	$(CXX) $(CXXFLAGS) $(ANIM_FP) -MMD $< $(LIB) -Wl,--gc-sections -o $@

.PHONY: anims
all: $(OUT)/anm_harness
anims: $(OUT)/anm_harness
	python3 $(HOST)/tests/test_anims.py --harness $(OUT)/anm_harness --disc $(DISC)

clean:
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
# stubs (src/host_snd.cpp); there is no playback.
SND_SRCS := $(sort $(wildcard $(REPO)/src/nw4r/snd/*.cpp))
SND_SRCS := $(patsubst $(REPO)/%,%,$(SND_SRCS)) \
    src/nw4r/ut/ut_IOStream.cpp src/nw4r/ut/ut_FileStream.cpp \
    src/nw4r/ut/ut_DvdFileStream.cpp src/nw4r/ut/ut_DvdLockedFileStream.cpp \
    src/egg/audio/eggAudioArcPlayerMgr.cpp src/egg/core/eggCntFile.cpp \
    src/nw4r/ut/ut_NandFileStream.cpp \
    src/revolution/MEM/mem_frameHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c
SND_HOST_SRCS := src/host_snd.cpp
SND_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(SND_SRCS)) \
    $(patsubst %,$(OUT)/host/%.o,$(SND_HOST_SRCS))
SND_LIB := $(OUT)/libwiiplay_snd.a

# mem_frameHeap.c uses CodeWarrior's __abs intrinsic.
$(OUT)/decomp/src/revolution/MEM/%.c.o: CFLAGS += -D__abs=__builtin_abs

$(SND_LIB): $(SND_OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/snd_harness: $(HOST)/tests/snd_harness.cpp $(HOST)/tests/disc_walk.h $(SND_LIB) $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(SND_LIB) $(LIB) $(SND_LIB) -o $@

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
# heap), with tests/hbm_host.cpp for the WPAD / KPAD / AX / OS / WENC calls it
# references (no controllers or speaker on the host).
HBM_SRCS := $(patsubst $(REPO)/%,%,$(sort $(wildcard $(REPO)/src/homebuttonMiniLib/*.cpp))) \
    src/revolution/MEM/mem_expHeap.c src/revolution/MEM/mem_heapCommon.c \
    src/revolution/MEM/mem_list.c
HBM_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(HBM_SRCS)) $(OUT)/tests/hbm_host.cpp.o

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
