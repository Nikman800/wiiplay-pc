# CMake toolchain for the Windows build of Aurora and the glue DLL (port/host/Makefile,
# WIN64=1; port/docs/HOST_BUILD.md §41): MinGW-w64 GCC, posix threads.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(_mingw x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${_mingw}-gcc-posix)
set(CMAKE_CXX_COMPILER ${_mingw}-g++-posix)
set(CMAKE_RC_COMPILER ${_mingw}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${_mingw})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# pkg-config must not hand out the build machine's (Linux) libraries: none here, so
# Aurora fetches and builds what it needs (zlib, libpng, ...)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/${_mingw}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
# Windows 10 as the target API level. NTDDI_VERSION explicitly at plain Windows 10:
# abseil (20240722) otherwise takes MinGW's newest headers as a cue to use WinRT's
# time zone API, whose MinGW declarations do not compile (Aurora needs no local time zone)
set(CMAKE_C_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000000")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000000")
