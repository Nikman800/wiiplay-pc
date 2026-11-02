# Aurora (port/extern/aurora, GX on WebGPU/Dawn) for the host build, plus the
# thin C glue library the port's GX layer links (docs: port/docs/HOST_BUILD.md §25).
#
# Driven by port/host/Makefile (`make gx`); 64-bit only (Dawn has no 32-bit
# builds). Aurora is built as a subdirectory, Release, position-independent,
# and linked statically into libwiiplay_gx.so, which exports only the wpgx_*
# C API: Aurora's GX/OS/VI symbols stay hidden, so they cannot clash with the
# decomp's own revolution SDK when both end up in one process.
cmake_minimum_required(VERSION 3.25)
project(wiiplay_aurora C CXX)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The Aurora build is 64-bit only (Dawn has no 32-bit builds)")
endif ()
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Do not derive package prefixes from PATH: a Python/conda environment on PATH
# would otherwise supply libraries (zstd, zlib) the system loader cannot find.
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)

set(AURORA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/aurora")
if (NOT EXISTS "${AURORA_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "port/extern/aurora is empty: run `git submodule update --init port/extern/aurora`")
endif ()

# --- Aurora features: GX only ------------------------------------------------
set(AURORA_ENABLE_GX ON CACHE BOOL "" FORCE)
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)   # the port has its own DVD layer (and nod is fetched unpinned)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
option(WIIPLAY_AURORA_TESTS "Configure Aurora's own unit tests (gx_fifo_tests)" ON)
set(AURORA_ENABLE_TESTS ${WIIPLAY_AURORA_TESTS} CACHE BOOL "" FORCE)

# --- Pinned downloads ----------------------------------------------------------
# Aurora fetches Dawn, SDL3, abseil and nlohmann_json without a hash. FetchContent
# keeps the first declaration of a name, so declaring them here (same names,
# same URLs, plus URL_HASH) pins them without patching Aurora. The versions are
# forced to the ones hashed below; bumping the submodule may need new hashes
# (the configure step then fails with a hash mismatch, never silently).
include(FetchContent)
set(AURORA_DAWN_PROVIDER package CACHE STRING "" FORCE)
if (WIN32)
  # Windows (HOST_BUILD.md §41): the prebuilt package is webgpu_dawn.dll (built with
  # MSVC; its C API links from MinGW) plus its import library and dxcompiler / dxil.
  set(AURORA_DAWN_LINKAGE shared CACHE STRING "" FORCE)
else ()
  set(AURORA_DAWN_LINKAGE static CACHE STRING "" FORCE)
endif ()
set(AURORA_DAWN_VERSION "v20260807.225922" CACHE STRING "" FORCE)
set(AURORA_SDL3_PROVIDER vendor CACHE STRING "" FORCE)
set(AURORA_SDL3_LINKAGE static CACHE STRING "" FORCE)
set(AURORA_SDL3_REF "refs/tags/release-3.4.10" CACHE STRING "" FORCE)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  FetchContent_Declare(dawn_prebuilt
    URL "https://github.com/encounter/dawn/releases/download/${AURORA_DAWN_VERSION}/dawn-linux-x86_64.tar.gz"
    URL_HASH SHA256=2a4523426ab9b3a8970b4e151c3b005304ad37c2c01bcff80c78fffe486fc768
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
elseif (WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  FetchContent_Declare(dawn_prebuilt
    URL "https://github.com/encounter/dawn/releases/download/${AURORA_DAWN_VERSION}/dawn-windows-amd64.tar.gz"
    URL_HASH SHA256=42a7a295c87b2e8a36312e2f84a5823aac63325e8e1d271bc7ced6f83bfb075f
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
else ()
  message(WARNING "No pinned Dawn package hash for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}; Aurora fetches it unpinned")
endif ()

set(_aurora_patches "${AURORA_DIR}/cmake/patches")
FetchContent_Declare(SDL
  URL "https://github.com/libsdl-org/SDL/archive/${AURORA_SDL3_REF}.tar.gz"
  URL_HASH SHA256=0dc11d980ba17250200718fa4e28011da293f27ed92f92203afffe396811f307
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PATCH_COMMAND ${CMAKE_COMMAND} -DSDL_SOURCE_DIR=<SOURCE_DIR>
    -P "${_aurora_patches}/apply-sdl3-android-nintendo-auto-mapping.cmake"
  COMMAND ${CMAKE_COMMAND} -DSDL_SOURCE_DIR=<SOURCE_DIR>
    -P "${_aurora_patches}/apply-sdl3-android-security-exception.cmake"
  EXCLUDE_FROM_ALL
)
set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_Declare(abseil-cpp
  URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz
  URL_HASH SHA256=f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  EXCLUDE_FROM_ALL
)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  EXCLUDE_FROM_ALL
)
# Use the pinned copies even where a system package exists.
set(CMAKE_DISABLE_FIND_PACKAGE_absl ON)
set(CMAKE_DISABLE_FIND_PACKAGE_nlohmann_json ON)

add_subdirectory("${AURORA_DIR}" aurora EXCLUDE_FROM_ALL)

# --- Glue library ----------------------------------------------------------------
add_library(wiiplay_gx SHARED wpgx.cpp wpgx_platform.cpp wpgx.h)
target_include_directories(wiiplay_gx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${AURORA_DIR}/lib")
target_compile_features(wiiplay_gx PRIVATE cxx_std_20)
target_compile_definitions(wiiplay_gx PRIVATE WPGX_BUILD WEBGPU_DAWN)
target_link_libraries(wiiplay_gx PRIVATE aurora::core aurora::gx aurora::mtx aurora::vi
  absl::flat_hash_map absl::btree dawn::webgpu_dawn)
set_target_properties(wiiplay_gx PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
# Export the wpgx_* API only; nothing from the static archives. The C++
# runtime is linked statically and hidden as well: the port's executables
# define their own operator new/delete (the game's heaps, a single-threaded
# arena), and Aurora's allocations, made from its worker threads, must not
# be routed there.
if (WIN32)
  # Windows (HOST_BUILD.md §41): wiiplay_gx.dll exports the wpgx_* functions named in
  # wpgx.h (a .def file made from it; without one GNU ld would export every symbol).
  # The C++ runtime and winpthreads are linked in statically, so the DLL needs only
  # the system's DLLs and webgpu_dawn.dll; it is not "lib"-prefixed.
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/wpgx.h" _wpgx_lines REGEX "wpgx_[a-z0-9_]+\\(")
  set(_wpgx_exports "")
  foreach (_line IN LISTS _wpgx_lines)
    string(REGEX MATCHALL "wpgx_[a-z0-9_]+\\(" _names "${_line}")
    foreach (_name IN LISTS _names)
      string(REPLACE "(" "" _name "${_name}")
      list(APPEND _wpgx_exports "${_name}")
    endforeach ()
  endforeach ()
  list(REMOVE_DUPLICATES _wpgx_exports)
  list(JOIN _wpgx_exports "\n  " _wpgx_exports)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/wpgx.def" "LIBRARY wiiplay_gx.dll\nEXPORTS\n  ${_wpgx_exports}\n")
  target_sources(wiiplay_gx PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wpgx.def")
  set_target_properties(wiiplay_gx PROPERTIES PREFIX "")
  target_link_options(wiiplay_gx PRIVATE -static-libstdc++ -static-libgcc -static "LINKER:--no-undefined")
  set_property(TARGET wiiplay_gx APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wpgx.h")
  # Next to the DLL: what it loads at run time (the tests and `make dist` take them from here)
  add_custom_command(TARGET wiiplay_gx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dawn::webgpu_dawn>
      $<TARGET_FILE_DIR:dawn::webgpu_dawn>/dxcompiler.dll $<TARGET_FILE_DIR:dawn::webgpu_dawn>/dxil.dll
      $<TARGET_FILE_DIR:wiiplay_gx>)
else ()
  target_link_options(wiiplay_gx PRIVATE -static-libstdc++ -static-libgcc "LINKER:--exclude-libs,ALL"
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/wpgx.map" "LINKER:--no-undefined")
  set_property(TARGET wiiplay_gx APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wpgx.map")
endif ()
