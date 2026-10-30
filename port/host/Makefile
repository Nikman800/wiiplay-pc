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
INCLUDES := -I$(HOST)/include -I$(REPO)/include $(addprefix -idirafter ,$(BTE_DIRS))

DEFINES := -DPORT_HOST=1 -DNONMATCHING -DREVOLUTION -DNDEBUG=1 \
           -DVERSION_RHAE01 -DVERSION_RHAE01_01

WARN := -Wall -Wno-unknown-pragmas -Wno-unused-function -Wno-unused-variable \
        -Wno-unused-but-set-variable -Wno-sign-compare -Wno-parentheses
# OSAddress.h casts u32 physical addresses to pointers; harmless here, listed
# in docs/HOST_BUILD.md as a 64-bit site.
WARN += -Wno-int-to-pointer-cast

COMMON := $(ARCH) -O2 -g -fno-strict-aliasing $(INCLUDES) $(DEFINES) $(WARN)
CFLAGS := -std=gnu11 $(COMMON)
# Decompiled class layouts: offsetof on classes with vtables (nw4r lists),
# member init order and anonymous-namespace bases are as in the original.
CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti $(COMMON) \
            -Wno-invalid-offsetof -Wno-subobject-linkage -Wno-reorder

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
    src/nw4r/ut/ut_list.cpp

HOST_SRCS := src/host_sdk.cpp

DECOMP_OBJS := $(patsubst %,$(OUT)/decomp/%.o,$(DECOMP_SRCS))
HOST_OBJS := $(patsubst %,$(OUT)/host/%.o,$(HOST_SRCS))
LIB := $(OUT)/libwiiplay_loader.a

.PHONY: all test unit archives clean
all: $(OUT)/arc_harness $(OUT)/test_be $(OUT)/test_be_c

$(OUT)/decomp/%.c.o: $(REPO)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OUT)/decomp/%.cpp.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(OUT)/host/%.cpp.o: $(HOST)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(LIB): $(DECOMP_OBJS) $(HOST_OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/arc_harness: $(HOST)/tests/arc_harness.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -MMD $< $(LIB) -o $@

$(OUT)/test_be: $(HOST)/tests/test_be.cpp $(HOST)/include/port/be.h
	$(CXX) $(CXXFLAGS) $< -o $@

$(OUT)/test_be_c: $(HOST)/tests/test_be_c.c $(HOST)/include/port/be.h
	$(CC) $(CFLAGS) $< -o $@

unit: $(OUT)/test_be $(OUT)/test_be_c
	$(OUT)/test_be
	$(OUT)/test_be_c

archives: $(OUT)/arc_harness
	python3 $(HOST)/tests/test_archives.py --harness $(OUT)/arc_harness --disc $(DISC)

test: unit archives

clean:
	rm -rf $(HOST)/build

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)
