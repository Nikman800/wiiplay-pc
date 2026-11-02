/**
 * The host operating system under the port (port/docs/HOST_BUILD.md §41): files and
 * directories, per-user folders, reserved memory, fibers and precise sleeps, for Linux
 * and Windows (src/host_platform.cpp). Host code calls these instead of POSIX or Win32
 * directly; nothing here touches the game's types, so the Windows half can include
 * <windows.h> without the decomp's headers.
 *
 * Paths are UTF-8 (on Windows the executables' manifest makes UTF-8 the process code
 * page, windows/wiiplay.manifest). Paths these functions return use '/' on both.
 */
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Files and directories ------------------------------------------------------ */
int PortPathIsDir(const char* path);
int PortPathExists(const char* path);
/* Modification time in seconds since 1970, or -1 if there is no such file. */
int64_t PortPathMTime(const char* path);
/* 0: `path` was created or already is a directory. Otherwise -1 (errno set). */
int PortMakeDir(const char* path);
/* PortMakeDir on every component of `path`. */
int PortMakeDirs(const char* path);
/* Renames `from` to `to`, replacing `to` if it exists (atomically on both). 0 or -1. */
int PortRenameReplace(const char* from, const char* to);
/* The absolute, resolved form of `path` ('/' separators). 0, or -1 (out holds `path`). */
int PortRealPath(const char* path, char* out, size_t size);
/* 1 if `path` is `dir` or inside it (both resolved; case-insensitive on Windows). */
int PortPathInside(const char* path, const char* dir);
/* This executable's path. 0 or -1. */
int PortExePath(char* out, size_t size);

/* --- Per-user folders --------------------------------------------------------------- */
enum {
    PORT_USER_DATA,   /* saves: $XDG_DATA_HOME/wiiplay; %APPDATA%\wiiplay */
    PORT_USER_CONFIG, /* input.cfg: $XDG_CONFIG_HOME/wiiplay; %APPDATA%\wiiplay */
    PORT_USER_STATE,  /* perf logs: $XDG_STATE_HOME/wiiplay; %LOCALAPPDATA%\wiiplay */
    PORT_USER_CACHE   /* Aurora's data and caches: $XDG_DATA_HOME/wiiplay; %LOCALAPPDATA%\wiiplay */
};
/* The folder for `kind` (not created). 0, or -1 if the user's home is unknown. */
int PortUserDir(int kind, char* out, size_t size);
/* Up to `max` folders where Dolphin may keep its user directory (with Wii/ inside),
 * written to dirs[i] (each `size` bytes). Returns how many. */
int PortDolphinUserDirs(char (*dirs)[1024], int max);

/* --- Memory ------------------------------------------------------------------------- */
/* Reserves `size` bytes of address space (no access). NULL on failure. */
void* PortMemReserve(size_t size);
/* Makes [p, p + size) of a reservation readable and writable (zeroed). 0 or -1. */
int PortMemCommit(void* p, size_t size);

/* --- Fibers (src/host_thread.cpp's cooperative threads) ------------------------------- */
typedef struct PortFiber PortFiber;
/* The calling thread as a fiber (made one the first time). */
PortFiber* PortFiberSelf(void);
/* A fiber that runs func(arg) on its own stack of `stackSize` bytes when switched to;
 * func must never return. NULL on failure. */
PortFiber* PortFiberCreate(size_t stackSize, void (*func)(void*), void* arg);
void PortFiberDelete(PortFiber* fiber);
/* Saves the running fiber in `from` and continues `to`. */
void PortFiberSwitch(PortFiber* from, PortFiber* to);

/* --- Time --------------------------------------------------------------------------- */
/* Monotonic nanoseconds (std::chrono::steady_clock's clock). */
uint64_t PortNowNs(void);
/* Sleeps until PortNowNs() reaches `deadline`, to within about 0.1 ms: the OS sleep
 * for all but the last stretch, then a spin (Windows sleeps are coarse). */
void PortSleepUntilNs(uint64_t deadline);
void PortSleepMs(int ms);
/* localtime_r / localtime_s. */
struct tm;
void PortLocalTime(int64_t t, struct tm* out);

#ifdef __cplusplus
}
#endif

#endif
