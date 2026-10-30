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

COMMON := $(ARCH) -O2 -g -fno-strict-aliasing $(WIDE) $(INCLUDES) $(DEFINES) $(WARN)
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

test: unit archives resources layouts models

clean:
	rm -rf $(HOST)/build

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)
