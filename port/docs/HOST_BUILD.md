# Host build: the loader harness and the big-endian field wrapper

Status (2026-10-30): the first piece of the game that runs natively on a PC is the disc load path.
The decompiled Yaz0 decoder and U8 archive code, compiled with GCC for Linux x86-64, open every
archive on the player's disc and read every file inside. The result matches
`port/assets/tools/wiiarc.py` byte for byte.

## 1. Build and run

Requirements: GCC/G++ (tested with 15.2), GNU make and Python 3. CMake is not needed. The
32-bit cross-check also needs gcc-multilib and g++-multilib.

```sh
cd port/host
make                       # 64-bit (primary): build/host64/{arc_harness,test_be,test_be_c}
make test                  # unit tests + archive comparison (DISC defaults to ~/wii-play/extracted/files)
make test DISC=/path/to/extracted/files
make M32=1 test            # same code built with -m32 into build/host32 (cross-check)
make clean
```

Expected `make test` output:

```
test_be (C++): OK
test_be (C): OK
arc_harness: 59 disc files, 62 archives, 1615 leaf files, 0 errors (pointer size 8)
disc files: 59 (.carc: 31, opened by the harness: 31)
archives mounted: harness 62, wiiarc 62 (disc-level 31, nested 31)
leaf files: harness 1615, wiiarc 1615
nw4r headers checked: 935
missing 0, extra 0, mismatched 0
OK
```

The harness takes about 0.2 s. The Python reference walk takes about 13 s. Nothing is written to
disk except the build directory, which is ignored by git. Disc data never leaves the extracted
folder.

### What the harness does

`tests/arc_harness.cpp` loads each disc file the way `RPSysResourceManager::LoadFromDVD` and
`ExpandCompressed` do. Everything named below is the real decompiled code, compiled unmodified
unless §4 lists it:

| Step | Code |
|---|---|
| open and read | `EGG::DvdFile::open(path, multiHandle)`, `EGG::DvdRipper::loadToMainRAM` (`src/egg/core/eggDvdFile.cpp`, `eggDvdRipper.cpp`) |
| decompress | `EGG::Decomp::checkCompressed`, `getExpandSize`, `decode` (`src/egg/core/eggDecomp.cpp`) |
| mount | `EGG::Archive::mount`, `getFile`, the archive list, `~Archive` (`src/egg/core/eggArchive.cpp`, `eggDisposer.cpp`, `src/nw4r/ut/ut_list.cpp`) |
| list and look up | `ARCInitHandle`, `ARCOpenDir`, `ARCReadDir`, `ARCOpen`, `ARCFastOpen`, `ARCConvertPathToEntrynum`, `ARCGetStartAddrInMem` (`src/revolution/ARC/arc.c`) |
| nw4r header | `nw4r::ut::IsValidBinaryFile` on every file with an nw4r header (`src/nw4r/ut/ut_binaryFileFormat.cpp`) |
| asserts | `system_halt` and `system_print` (`src/egg/prim/eggAssert.cpp`) |

For each file it calls `Archive::getFile` with the file's path, then cross-checks the result
against `ARCFastOpen` and `ARCConvertPathToEntrynum`. It descends into nested archives (Yaz0 or
U8 inside an archive) with the same rule as `wiiarc._expand`. It prints one line per archive
(path, node count) and one per leaf file (path, size, FNV-1a 64 hash, nw4r header fields read
through the wrapper).

`tests/test_archives.py` compares all of these with wiiarc.py:

- the sets of paths
- sizes and content hashes
- archive node counts
- nw4r header fields, against `struct.unpack(">4sHHIHH")`
- every `.carc` on the disc was mounted

The disc has 31 `.carc` files: 28 are archives with files in them and 3 are empty archives
(`Effect/RPCommonParty/effect.carc`, `Common/RPPartyTitle/common.carc`, `US/Kokeshi/local.carc`;
each is 39 bytes of Yaz0 holding a root-only U8). Inside them are 31 nested archives and 1615 leaf
files, including the 28 top-level non-archive disc files that are loaded through the same DVD
path.

### Host stand-ins (`src/host_sdk.cpp`)

These are deliberately minimal and synchronous. The Phase 7 platform layer replaces them.

- **OS**:
  - mutexes are no-ops; message queues are single-threaded ring buffers
  - `OSGetCurrentThread` returns a static thread
  - `OSReport` prints to stderr; `OSPanic` aborts
- **DVD**:
  - paths resolve against the disc root
  - an entry number indexes a table of opened paths
  - `DVDFileInfo.block.userData` holds the host `FILE*` (a runtime struct, so a 64-bit pointer is
    fine there)
  - reads past the end of a file return zeros (callers round sizes up to 32)
  - the async read calls its callback before returning
- **EGG heap**:
  - `Heap::alloc`/`free` and the global and heap `operator new`/`delete` use `posix_memalign` with
    the requested alignment (the sign of the alignment, head or tail, is ignored)
  - `Heap::findContainHeap` returns NULL, so disposers are not attached to heaps
- **VI, DC**: no-ops.

## 2. The big-endian field wrapper

### Use

Declare members that map onto file bytes with these types. They have the same size and alignment
as the plain type, so offsets do not change:

| Type | Field |
|---|---|
| `be_u16`, `be_s16`, `be_u32`, `be_s32`, `be_f32` | integer and float fields |
| `be_u64`, `be_s64`, `be_f64` | 64-bit fields (C++ only) |
| `BE_ENUM(E, Storage)` | a member the original declares as enum `E`, stored as the integer `Storage` (u8, u16 or u32) |
| `be_u24` | 3-byte field, alignment 1 (host only; U8 node name offsets) |

- **C++.** `be<T>` converts implicitly to `T`, and assigning a `T` stores big-endian, so the
  decompiled code compiles unchanged: `pHeader->fileSize < x`, `hdr->signature = 'RFNU'`,
  `flags |= 2`, `++count`. Only the type declarations change.
- **C.** C has no operator overloading, so C code reads with `BE_GET(field)` and writes with
  `BE_SET(field, value)`. On the host the C types are one-member structs, so a forgotten
  `BE_GET` is a compile error rather than a silent wrong value.
- **Both languages.** `BE_GET` and `BE_SET` also work in C++.
- **Raw bytes.** `port_load_be16/32/64` and `port_store_be16/32/64` read and write big-endian at
  any address, aligned or not. Use them for code that walks bytes without a struct.

Rules:

- **Leave payloads alone.** Do not wrap fields that GX consumes: display lists, vertex arrays,
  texture and palette data stay big-endian bytes for the Phase 7 GX layer (ASSETS.md §3.1). Wrap
  only what the CPU reads.
- **Keep `NW4R_LITTLE_ENDIAN` undefined on the host.** With the wrapper, the CPU reads the
  file's `0xFEFF` byte-order mark as `0xFEFF`, which is `NW4R_BYTEORDER_BIG`, the value
  `IsValidBinaryFile` expects.
- **Avoid double swaps in `snd`.** `snd::Util::ReadBigEndian` has its own little-endian branch,
  under the misspelled macro `NW4R_LITLE_ENDIAN`. When `snd` structs move to `be_*`, either
  leave that macro undefined or keep those fields plain. Do not do both, or the value is swapped
  twice.
- **Pointer slots inside files** need a different type. See §5 and `POINTERS.md`.

### Implementation

| Build | Header | What the names are |
|---|---|---|
| Host (`PORT_HOST` defined) | `port/host/include/port/be.h` | C++: `template <class T, class Storage = T> class port::be` holds one `alignas(Storage)` unsigned integer of the same size and byte-swaps on `get`/`set` (`__builtin_bswap*`, or `_byteswap_*` on MSVC; no swap on a big-endian host). `static_assert` checks size, alignment, trivial copyability and standard layout. C: `typedef struct { u32 raw_; } be_u32;` and so on, with `BE_GET`/`BE_SET` dispatched by C11 `_Generic`. `PORT_BE_WRAPPED` is 1. |
| Wii (matching) | `include/port/be.h` | Each name is a macro for the plain type (`#define be_u32 u32`). `BE_GET(x)` is `(x)` and `BE_SET(f, v)` is `((f) = (v))`. `BE_ENUM(E, S)` is `E`. `PORT_BE_WRAPPED` is 0. |

### How it stays out of the matching build

- **Include-path order selects the header; no flag is involved.** The host Makefile puts
  `port/host/include` before `include/`. CodeWarrior only searches `include/`, so it finds the
  Wii version. Each header `#error`s if it is reached from the wrong build.
- **What the edited lines become on the Wii.** The edited type declarations expand to the same
  tokens as before, and the `BE_GET` wraps add only parentheses. In a few places a type change
  could not be expressed as a macro (for example, U8 node names are a 24-bit bitfield on the
  Wii). There the original code sits under `#if PORT_BE_WRAPPED ... #else ... #endif` or
  `#if defined(PORT_HOST)` and is unchanged.
- **Machine check: `port/host/tools/check_matching.py`.**
  - It finds every unit that depends on a touched file, using the decomp checkout's
    `ninja -t deps` (read-only).
  - It recompiles each one from this repository with the exact CodeWarrior command line from
    the decomp's `build.ninja`, writing to a temporary directory.
  - It compares each object byte for byte with the decomp checkout's object.
  - It skips a unit when any of its other dependencies differs between the two checkouts.
- **Result on 2026-10-30.** All 734 units that depend on the touched files are byte-identical,
  and none were skipped. Run it after every change to a decomp file:

  ```sh
  python3 port/host/tools/check_matching.py --decomp ~/wii-play -j 16    # ~1 min
  ```

### Other host-only overrides

All of these live in `port/host/include`, which shadows `include/` on the host only.

| File | Why |
|---|---|
| `types.h` | `u32`/`s32` come from `<stdint.h>`. The original uses `unsigned long`, which is 8 bytes on LP64 and would change every struct layout. Also: `ROUND_UP_PTR`/`ROUND_DOWN_PTR` through `uintptr_t`; `DECL_HW_REGS` becomes a weak fake register array; `<cfloat>` for `FLT_EPSILON`. |
| `decomp/compat.h` | The CodeWarrior syntax shims, without the parser-only `0` placeholders. Defines `COMPAT_ANY`, so `DECOMP_FORCEACTIVE` and the other matching hacks compile away. |
| `revolution/OS/__ppc_eabi_init.h` | Emptied: PowerPC boot hooks and linker section symbols, and an `exit(void)` that conflicts with libc. |
| `port/be.h`, `port/host.h` | The wrapper; `PortHostSetDiscRoot`. |

Build flags: `-DPORT_HOST=1 -DNONMATCHING -DREVOLUTION -DNDEBUG=1 -DVERSION_RHAE01
-DVERSION_RHAE01_01`, `-std=gnu++17 -fno-exceptions -fno-rtti` (C: `-std=gnu11`). CodeWarrior's
recursive `-ir include/revolution/BTE` becomes one `-idirafter` per subdirectory.

## 3. 64-bit

Native 64-bit is the primary target. The `-m32` build is a cross-check: a bug that shows up only
in 64-bit is a pointer-size problem, and one that shows up in both is an endianness problem. Both
builds pass the full test.

### Pointer-size problems on this load path

All of these are compile errors or truncations in 64-bit only; none is a problem in `-m32`:

| Site | Problem | Fix |
|---|---|---|
| `include/types.h` | `u32`/`s32` are `unsigned long`/`long`, which are 8 bytes on LP64 | host `types.h` (§2) |
| `include/macros.h` `ROUND_UP_PTR`, `ROUND_DOWN_PTR` | cast the pointer through `u32`. `DvdRipper::loadToMainRAM` uses `ROUND_UP_PTR` for its 32-byte chunk when the offset is non-zero | host `types.h` redefines both through `uintptr_t` |
| `include/nw4r/ut/ut_algorithm.h` `GetIntPtr` | `reinterpret_cast<u32>(ptr)`. It is the basis of `AddOffsetToPtr`, `GetOffsetFromPtr` and `ComparePtr`, which every nw4r resource accessor uses | `#if defined(PORT_HOST)`: returns `uintptr_t` |
| `src/egg/core/eggDvdFile.cpp` `DvdFile::sync` | `reinterpret_cast<s32>(OSMessage)`: the DVD result travels through a message queue as a `void*` | `#if defined(PORT_HOST)`: cast through `intptr_t` |
| `include/revolution/OS/OSAddress.h` | `OSPhysicalToCached`/`Uncached` turn a `u32` into a pointer (warning silenced) | not on this path; the Phase 7 address model (`POINTERS.md` M0) decides |

### Pointer slots inside files

There are none on this path:

- **U8 nodes** hold offsets and indices, never pointers.
- **`ARCHandle`** is a runtime struct and stores native pointers.
- **`EGG::Archive`** and **`DVDFileInfo`** are also runtime structs, so their larger 64-bit
  layout does not matter.

The 4-byte pointer slots that do bite are in the formats that relocate in place. These are TPL,
`ut::ResFont`, `ef`, g3d texture binding, PMP and the KCL header copy (ASSETS.md §3.1 item 3).
They are inventoried, with a proposed `RES_PTR(T)` slot type, in `port/docs/POINTERS.md`. That
type is meant to sit beside `be_*` with the same Wii/host header split.

One layout trap the wrapper avoids: a byte array like `u8[4]` would have alignment 1 and would
shift every field after a `u8`. `be<T>` stores an `alignas(T)` integer, so it keeps the Wii
offsets. The unit test checks this with `offsetof`.

## 4. Decomp files touched

Every change is minimal and either produces the same tokens for CodeWarrior or is guarded. The
check in §2 confirms all of them.

| File | Change |
|---|---|
| `include/port/be.h` | **New.** The Wii side of the wrapper: macros for the plain types. |
| `src/revolution/ARC/arc.c` | `#include <port/be.h>`. `ARCHeader` and `ARCNode` fields are declared `be_u32`/`be_s32`. The node's `is_folder:8`/`name:24` bitfield is under `#if PORT_BE_WRAPPED` (host: `u8 is_folder; be_u24 name;`). All 18 direct field reads, and the two `ARCNodeIsFolder`/`ARCNodeGetName` macros, are wrapped in `BE_GET(...)`. |
| `include/nw4r/ut/ut_binaryFileFormat.h` | `#include <port/be.h>`. The `BinaryFileHeader` and `BinaryBlockHeader` members are `be_u32`/`be_u16`. This is the first C++ use: `IsValidBinaryFile` compiles unchanged and reads through the wrapper. |
| `include/nw4r/ut/ut_algorithm.h` | `GetIntPtr` returns `uintptr_t` under `#if defined(PORT_HOST)`. |
| `src/egg/core/eggDvdFile.cpp` | `DvdFile::sync` casts `OSMessage` to `s32` through `intptr_t` under `#if defined(PORT_HOST)`. |

No other files under `src/` or `include/` were changed. Everything else is new and lives under
`port/host/` and `port/docs/`.

## 5. Next steps

- Move the next loaders onto `be_*` as they are brought up. Each needs the type declarations, a
  small number of C `BE_GET`s, and a `check_matching.py` run:
  - TPL headers (C)
  - `lyt::res::*`
  - `g3d::Res*Data`
  - `ef` headers
  - BMG `INF1`/`MID1`
  - `snd` file structs
- Use `RES_PTR(T)` for in-file pointer slots (`POINTERS.md`).
- Replace `src/host_sdk.cpp` with the Phase 7 OS/DVD/heap layer: real threads, message queues
  that block, and MEM heaps (the decomp's `src/revolution/MEM` could compile as-is once the OS
  layer exists).
