/**
 * The host operating system under the port: Linux (POSIX) and Windows (Win32) halves
 * of port/platform.h (port/docs/HOST_BUILD.md §41). Only the C library and the OS
 * headers here, never the decomp's: <windows.h> and the game's types do not mix.
 *
 * Windows: paths cross the Win32 calls as UTF-16 (converted from and to UTF-8 here);
 * the C library's narrow file functions elsewhere get UTF-8 through the executables'
 * manifest (activeCodePage). Directories use '/' in what these functions return; Win32
 * accepts it.
 */

#include <port/platform.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif

// Windows also takes '\\' between path components.
#ifdef _WIN32
#define PORT_PATH_BACKSLASH 1
#else
#define PORT_PATH_BACKSLASH 0
#endif

namespace {

#ifdef _WIN32
std::wstring Wide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    }
    return w;
}

std::string Utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    }
    return s;
}

std::string Slashes(std::string s) {
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    return s;
}

// A Win32 error as errno, for the callers' strerror / errno checks.
void SetErrno(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        errno = EEXIST;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        errno = EACCES;
        break;
    case ERROR_DIR_NOT_EMPTY:
        errno = ENOTEMPTY;
        break;
    case ERROR_DISK_FULL:
        errno = ENOSPC;
        break;
    default:
        errno = EIO;
        break;
    }
}

std::string KnownFolder(int csidl) {
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK) {
        return "";
    }
    return Slashes(Utf8(buf));
}
#endif

int Copy(const std::string& s, char* out, size_t size) {
    if (s.size() + 1 > size) {
        if (size > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    std::memcpy(out, s.c_str(), s.size() + 1);
    return 0;
}

} // namespace

extern "C" {

/* --- Files and directories ------------------------------------------------------------ */

int PortPathIsDir(const char* path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(Wide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

int PortPathExists(const char* path) {
#ifdef _WIN32
    return GetFileAttributesW(Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

int64_t PortPathMTime(const char* path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(Wide(path).c_str(), GetFileExInfoStandard, &d)) {
        return -1;
    }
    // 100 ns units since 1601
    uint64_t t = static_cast<uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32 | d.ftLastWriteTime.dwLowDateTime;
    return static_cast<int64_t>(t / 10000000ull) - 11644473600ll;
#else
    struct stat st;
    return stat(path, &st) == 0 ? static_cast<int64_t>(st.st_mtime) : -1;
#endif
}

int PortMakeDir(const char* path) {
#ifdef _WIN32
    if (CreateDirectoryW(Wide(path).c_str(), nullptr)) {
        return 0;
    }
    DWORD e = GetLastError();
    if (PortPathIsDir(path)) {
        return 0; // also "C:" and "C:/", which CreateDirectory refuses
    }
    SetErrno(e);
    return -1;
#else
    return mkdir(path, 0755) == 0 || (errno == EEXIST && PortPathIsDir(path)) ? 0 : -1;
#endif
}

int PortMakeDirs(const char* path) {
    std::string p = path;
    for (size_t i = 1; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/' || (p[i] == '\\' && PORT_PATH_BACKSLASH)) {
            std::string sub = p.substr(0, i);
            if (PortMakeDir(sub.c_str()) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int PortRenameReplace(const char* from, const char* to) {
#ifdef _WIN32
    if (MoveFileExW(Wide(from).c_str(), Wide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return 0;
    }
    SetErrno(GetLastError());
    return -1;
#else
    return std::rename(from, to) == 0 ? 0 : -1;
#endif
}

int PortRealPath(const char* path, char* out, size_t size) {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetFullPathNameW(Wide(path).c_str(), 32768, buf, nullptr);
    if (n == 0 || n >= 32768) {
        Copy(path, out, size);
        return -1;
    }
    std::string s = Slashes(Utf8(buf));
    while (s.size() > 3 && s.back() == '/') {
        s.pop_back(); // "C:/dir/" -> "C:/dir"; "C:/" stays
    }
    return Copy(s, out, size);
#else
    char buf[4096];
    if (realpath(path, buf) == nullptr) {
        Copy(path, out, size);
        return -1;
    }
    return Copy(buf, out, size);
#endif
}

int PortPathInside(const char* path, const char* dir) {
    if (dir == nullptr || dir[0] == '\0') {
        return 0;
    }
    char p[4096], d[4096];
    PortRealPath(path, p, sizeof p);
    PortRealPath(dir, d, sizeof d);
    size_t n = std::strlen(d);
    while (n > 1 && d[n - 1] == '/') {
        d[--n] = '\0';
    }
    if (std::strlen(p) < n || (p[n] != '\0' && p[n] != '/')) {
        return 0;
    }
#ifdef _WIN32
    std::wstring wp = Wide(std::string(p, n).c_str()), wd = Wide(d);
    return CompareStringOrdinal(wp.c_str(), -1, wd.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return std::strncmp(p, d, n) == 0;
#endif
}

int PortExePath(char* out, size_t size) {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0 || n >= 32768) {
        return -1;
    }
    return Copy(Slashes(Utf8(buf)), out, size);
#else
    return PortRealPath("/proc/self/exe", out, size);
#endif
}

/* --- Per-user folders ------------------------------------------------------------------ */

int PortUserDir(int kind, char* out, size_t size) {
#ifdef _WIN32
    std::string base = KnownFolder(kind == PORT_USER_DATA || kind == PORT_USER_CONFIG ? CSIDL_APPDATA
                                                                                      : CSIDL_LOCAL_APPDATA);
    if (base.empty()) {
        return -1;
    }
    return Copy(base + "/wiiplay", out, size);
#else
    const char* xdgVar = kind == PORT_USER_CONFIG ? "XDG_CONFIG_HOME"
                         : kind == PORT_USER_STATE ? "XDG_STATE_HOME"
                                                   : "XDG_DATA_HOME";
    const char* fallback = kind == PORT_USER_CONFIG ? "/.config"
                           : kind == PORT_USER_STATE ? "/.local/state"
                                                     : "/.local/share";
    const char* xdg = std::getenv(xdgVar);
    if (xdg != nullptr && xdg[0] == '/') {
        return Copy(std::string(xdg) + "/wiiplay", out, size);
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return -1;
    }
    return Copy(std::string(home) + fallback + "/wiiplay", out, size);
#endif
}

int PortDolphinUserDirs(char (*dirs)[1024], int max) {
    int n = 0;
    auto add = [&](const std::string& d) {
        if (n < max && !d.empty() && Copy(d, dirs[n], sizeof dirs[n]) == 0) {
            n++;
        }
    };
#ifdef _WIN32
    // Dolphin's own rules (UICommon::SetUserDirectory), minus the portable cases
    // (portable.txt or LocalUserConfig: the User folder next to Dolphin's executable,
    // which only --mii-db FILE can name): the registry's UserConfigPath, the old
    // Documents\Dolphin Emulator, then AppData\Roaming\Dolphin Emulator.
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Dolphin Emulator", 0, KEY_QUERY_VALUE, &key) ==
        ERROR_SUCCESS) {
        wchar_t buf[32768];
        DWORD bytes = sizeof buf - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(key, L"UserConfigPath", nullptr, &type, reinterpret_cast<BYTE*>(buf), &bytes) ==
                ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            buf[bytes / sizeof(wchar_t)] = L'\0';
            add(Slashes(Utf8(buf)));
        }
        RegCloseKey(key);
    }
    std::string docs = KnownFolder(CSIDL_PERSONAL);
    if (!docs.empty()) {
        add(docs + "/Dolphin Emulator");
    }
    std::string appdata = KnownFolder(CSIDL_APPDATA);
    if (!appdata.empty()) {
        add(appdata + "/Dolphin Emulator");
    }
#else
    // Flatpak, then the XDG data folder and the old dot folder
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return 0;
    }
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string h = home;
    add(h + "/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu");
    add((xdg != nullptr && xdg[0] == '/' ? std::string(xdg) : h + "/.local/share") + "/dolphin-emu");
    add(h + "/.dolphin-emu");
#endif
    return n;
}

/* --- Memory -------------------------------------------------------------------------- */

void* PortMemReserve(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* p = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

int PortMemCommit(void* p, size_t size) {
#ifdef _WIN32
    return VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE) != nullptr ? 0 : -1;
#else
    return mprotect(p, size, PROT_READ | PROT_WRITE) == 0 ? 0 : -1;
#endif
}

/* --- Fibers -------------------------------------------------------------------------- */

#ifdef _WIN32
struct PortFiber {
    void* handle;
    void (*func)(void*);
    void* arg;
};

static void CALLBACK FiberStart(void* param) {
    PortFiber* f = static_cast<PortFiber*>(param);
    f->func(f->arg);
    std::abort(); // fibers never return
}

PortFiber* PortFiberSelf(void) {
    static thread_local PortFiber self;
    if (self.handle == nullptr) {
        self.handle = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
    }
    return self.handle != nullptr ? &self : nullptr;
}

PortFiber* PortFiberCreate(size_t stackSize, void (*func)(void*), void* arg) {
    PortFiber* f = static_cast<PortFiber*>(std::calloc(1, sizeof(PortFiber)));
    if (f == nullptr) {
        return nullptr;
    }
    f->func = func;
    f->arg = arg;
    // The commit size: the whole stack, as the malloc'd stacks on Linux
    f->handle = CreateFiberEx(stackSize, stackSize, FIBER_FLAG_FLOAT_SWITCH, FiberStart, f);
    if (f->handle == nullptr) {
        std::free(f);
        return nullptr;
    }
    return f;
}

void PortFiberDelete(PortFiber* fiber) {
    if (fiber != nullptr) {
        DeleteFiber(fiber->handle);
        std::free(fiber);
    }
}

void PortFiberSwitch(PortFiber* from, PortFiber* to) {
    (void)from;
    SwitchToFiber(to->handle);
}
#else
struct PortFiber {
    ucontext_t ctx;
    void* stack;
    void (*func)(void*);
    void* arg;
};

static PortFiber* sStarting = nullptr;

static void FiberStart() {
    PortFiber* f = sStarting;
    f->func(f->arg);
    std::abort(); // fibers never return
}

PortFiber* PortFiberSelf(void) {
    static thread_local PortFiber self;
    return &self;
}

PortFiber* PortFiberCreate(size_t stackSize, void (*func)(void*), void* arg) {
    PortFiber* f = static_cast<PortFiber*>(std::calloc(1, sizeof(PortFiber)));
    if (f == nullptr) {
        return nullptr;
    }
    f->func = func;
    f->arg = arg;
    f->stack = std::malloc(stackSize);
    if (f->stack == nullptr || getcontext(&f->ctx) != 0) {
        std::free(f->stack);
        std::free(f);
        return nullptr;
    }
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stackSize;
    f->ctx.uc_link = NULL;
    makecontext(&f->ctx, FiberStart, 0);
    return f;
}

void PortFiberDelete(PortFiber* fiber) {
    if (fiber != nullptr) {
        std::free(fiber->stack);
        std::free(fiber);
    }
}

void PortFiberSwitch(PortFiber* from, PortFiber* to) {
    // A new fiber reads its function from sStarting when it first runs
    sStarting = to;
    if (swapcontext(&from->ctx, &to->ctx) != 0) {
        std::fprintf(stderr, "host_platform: swapcontext failed\n");
        std::abort();
    }
}
#endif

/* --- Time ---------------------------------------------------------------------------- */

uint64_t PortNowNs(void) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

void PortSleepUntilNs(uint64_t deadline) {
#ifdef _WIN32
    // A high-resolution waitable timer (Windows 10 1803 and later) wakes within about
    // half a millisecond; a plain Sleep only at the next scheduler tick (up to 15.6 ms).
    // Either way the last stretch spins on the clock.
    const uint64_t spinNs = 1000000;
    static thread_local HANDLE sTimer = nullptr;
    static thread_local bool sTried = false;
    if (!sTried) {
        sTried = true;
        sTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_ALL_ACCESS);
    }
    uint64_t now = PortNowNs();
    if (deadline > now + spinNs) {
        uint64_t ns = deadline - now - spinNs;
        if (sTimer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(ns / 100); // relative, 100 ns units
            if (SetWaitableTimer(sTimer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(sTimer, INFINITE);
            }
        } else if (ns >= 16000000) {
            Sleep(static_cast<DWORD>((ns - 16000000) / 1000000));
        }
    }
    while (PortNowNs() < deadline) {
        if (deadline - PortNowNs() > 200000) {
            SwitchToThread();
        } else {
            YieldProcessor();
        }
    }
#else
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(deadline))));
#endif
}

void PortSleepMs(int ms) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(ms));
#else
    usleep(static_cast<useconds_t>(ms) * 1000);
#endif
}

void PortLocalTime(int64_t t, struct tm* out) {
    time_t tt = static_cast<time_t>(t);
#ifdef _WIN32
    localtime_s(out, &tt);
#else
    localtime_r(&tt, out);
#endif
}

} // extern "C"
