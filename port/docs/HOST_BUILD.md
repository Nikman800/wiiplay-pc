# Host build: the loader harnesses, the big-endian field wrapper and pointer slots

Status (2026-10-30): the first pieces of the game that run natively on a PC are the disc load
path, the font loader, the message loader, TPL binding, the layout loader (`nw4r::lyt`
build and animation binding, no drawing yet) and the g3d resource loader (`nw4r::g3d::ResFile`
check, init and bind, no drawing yet).

- The decompiled Yaz0 decoder and U8 archive code, compiled with GCC for Linux x86-64, open every
  archive on the player's disc and read every file inside. The result matches
  `port/assets/tools/wiiarc.py` byte for byte.
- The real `nw4r::ut::ResFont` relocates every `.brfnt` on the disc in place (including the HOME
  Menu fonts inside layout archives) and answers glyph lookups for every code point exactly as an
  independent Python decoder does.
- The real `RPSysMessage` / `EGG::MsgRes` reads both BMG files with a 16-bit `wchar_t`.
- `TPLBind` relocates every `.tpl` on the disc (471, all inside layout archives) in place with
  M0 addresses, and `lyt::TexMap` reads them back.
- The real `lyt::Layout::Build` builds all 140 `.brlyt` files on the disc (4120 panes, 2633
  materials, 207 groups), and every `.brlan` (604) is bound to every layout of its archive
  through `Layout::CreateAnimTransform` / `AnimTransformBasic` (4248 binds). Everything matches
  an independent decoding with `port/assets/tools/fmt_layout.py` (§1, the layout harness).
- The real `nw4r::g3d` resource code sets up all 132 `.brres` files on the disc (all inside
  archives) the way `RPGrpModelResManager` does: `ResFile::CheckRevision`, `Init` (shape vertex
  array commands), `Bind` (textures, palettes, PAT0) and `ResAnmScn::Bind`. Each set-up file is
  the Wii's in-memory image of that file, and every model, texture, palette and animation
  matches `port/assets/tools/fmt_brres_model.py` and an animation reader in the test (§11).
- The real `nw4r::ef::Resource` registers every BREFF/BREFT pair on the disc (13 each) the way
  `RPSysEffectCreator` does (`Add`, `AddTexture`, `RelocateCommand`; `RemoveEffectProject`,
  `RemoveTextureProject`, `RelocateCommand`), with the cursor and static pairs linked to each
  scene pair in turn. Every emitter, track and texture matches
  `port/assets/tools/fmt_effect.py`, and after every step each registered file is
  byte-identical to the Wii's in-memory copy: list links, name, track, texture and
  cross-file slots hold big-endian emulated addresses (§12).
- The real `nw4r::snd` code opens all 11 sound archives on the disc the way the game does
  (`EGG::ArcPlayer` over `DvdSoundArchive` / `MemorySoundArchive`, `SoundArchivePlayer::Setup`),
  reads every sound, bank, player, group and file with its name, loads every group into a
  `SoundHeap` and parses every loaded RSEQ, RBNK and RWSD through the real readers (no
  playback). Everything matches an independent reading of the bytes and, where it parses,
  `port/assets/tools/fmt_sound.py` (§13).

`POINTERS.md` steps 0 to 7 are implemented: M0 (emulated Wii addresses for all host heap memory),
`RES_PTR(T)` slots, `-fshort-wchar` with the MSL_C wide functions, ResFont (§5 to §7 below), TPL
and the layout resource structs (§9), the two hard-coded runtime offsets (§10), g3d with the
GX texture-object contract (§11), the `nw4r::ef` resources with `RES_LINK` (§12), and the
`nw4r::snd` callback cookies and sound archive readers (§13).

## 1. Build and run

Requirements: GCC/G++ (tested with 15.2), GNU make and Python 3. CMake is not needed. The
32-bit cross-check also needs gcc-multilib and g++-multilib.

```sh
cd port/host
make                       # 64-bit (primary): build/host64/{arc_harness,res_harness,lyt_harness,g3d_harness,ef_harness,test_*}
make test                  # unit tests + archive and resource comparisons (DISC defaults to ~/wii-play/extracted/files)
make test DISC=/path/to/extracted/files
make M32=1 test            # same code built with -m32 into build/host32 (cross-check)
make clean
```

Expected `make test` output (`make M32=1 test` prints the same with pointer size 4):

```
test_ef: OK (pointer size 8)
test_be (C++): OK
test_be (C): OK
test_port: OK (pointer size 8, wchar_t 2)
test_port (C): OK
test_layout: OK (static_asserts, pointer size 8)
arc_harness: 59 disc files, 62 archives, 1615 leaf files, 0 errors (pointer size 8)
disc files: 59 (.carc: 31, opened by the harness: 31)
archives mounted: harness 62, wiiarc 62 (disc-level 31, nested 31)
leaf files: harness 1615, wiiarc 1615
nw4r headers checked: 935
missing 0, extra 0, mismatched 0
OK
res_harness: 8 fonts (1983 glyph lines), 2 BMG files (714 messages), 0 errors (pointer size 8, wchar_t 2)
files: BRFNT 8, BMG 2
lines: F 8/8, C 8/8, W 8/8, G 1983/1983, A 8/8, M 714/714 (harness/python)
missing 0, extra 0
OK
lyt_harness: 471 TPL files (443 already bound by a layout), 140 layouts (4120 panes), 604 animations (4248 binds, 8086 bound contents), 0 errors (pointer size 8)
files: TPL 471, BRLYT 140, BRLAN 604
lines: T 471/471, TD 472/472, L 140/140, P 4120/4120, M 2633/2633, G 207/207, A 4248/4248, AC 41949/41949 (harness/python)
missing 0, extra 0
OK
g3d_harness: 132 BRRES (bound: 131 by themselves, 1 with the archive's other BRRES, 0 unresolved), 177 models, 564 textures, 5 palettes, 222 animations; set-up checks: 1340 shapes (4227 array commands), 665 texture infos (665 tex objs, 0 tlut objs), 56 PAT0 slots, 49005 bytes changed; 0 errors (pointer size 8)
files: BRRES 132; animations: CHR0 147, CLR0 27, PAT0 10, SCN0 16, SRT0 22
lines: F 132/132, G 314/314, M 177/177, T 564/564, P 5/5, A 222/222 (harness/python)
missing 0, extra 0
OK
ef_harness: 13 BREFF (373 emitters, 1168 tracks), 13 BREFT (159 textures), 24 stages, 6773 changed runs, 4002 particle texture slots and 202 curve slots checked (90 into another pair's file), 3 capture slots; 0 errors (pointer size 8)
files: BREFF 13, BREFT 13 (scene pairs 11)
lines: EF 13/13, E 373/373, ED 373/373, PD 373/373, K 1168/1168, TF 13/13, T 159/159, STAGE 24/24, FILE 138/138, W 6773/6773, CAP 3/3 (harness/python)
missing 0, extra 0
OK
```

The number of changed bytes depends on the emulated addresses the arena hands out, so it
differs slightly between the 64-bit and `-m32` builds (48690 there; 6774 changed runs for
`ef_harness`); everything else is the same.

| Target | What it runs |
|---|---|
| `unit` | `test_be`, `test_be_c` (wrapper), `test_port`, `test_port_c` (M0 arena, `RES_PTR`, wide functions, host `GXInitTexObj`/`GXInitTlutObj` words, C++ and C), `test_layout` (compile-time struct layout checks); also compiles the step 4 units (§10) |
| `archives` | `arc_harness` + `tests/test_archives.py` |
| `resources` | `res_harness` + `tests/test_resources.py` |
| `layouts` | `lyt_harness` + `tests/test_layouts.py` |
| `models` | `g3d_harness` + `tests/test_models.py` (§11) |
| `sound` | `snd_harness` + `tests/test_sound.py` (§13) |
| `effects` | `ef_harness` + `tests/test_effects.py` (§12); `test_ef` (`RES_LIST`, ef slots) runs with `unit` |

The harnesses take well under a second each. The Python reference walks take about 13 s
(archives, which hashes every file), 3 s (resources) and 3 s (layouts). Nothing is written to disk except the
build directory, which is ignored by git. Disc data never leaves the extracted folder.

### What the harness does

`tests/disc_walk.h` (shared by both harnesses) loads each disc file the way `RPSysResourceManager::LoadFromDVD` and
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
U8 inside an archive) with the same rule as `wiiarc._expand`, and hands each leaf file to the
harness while its archive is still mounted. `tests/arc_harness.cpp` prints one line per archive
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

### The resource harness (`tests/res_harness.cpp`)

For every leaf file that starts with `RFNT` or `MESG` (8 fonts and 2 message files on the US
disc, three of the fonts inside the HOME Menu layout archives
`US/PartyStatic/local.carc/HomeMenu/homeBtn_*.arc`):

- **Fonts.** `ResFont::SetResource` on the buffer. This runs `Rebuild`, which relocates the
  six offset slots in place (they become emulated Wii addresses, §5) and renames the file
  `RFNU`. For every code point 0 to 0xFFFF the harness then calls:
  - `SetAlternateChar(ch)`, which is `FindGlyphIndex(ch)` behind the public API; the index it
    finds is read back from `FontInformation.alterCharIndex` and the original restored;
  - `GetCharWidths(ch)`, `GetCharWidth(ch)` and `GetGlyph(&glyph, ch)`.

  It also loads the same buffer into a second `ResFont`, which takes the `RFNU` branch (the path
  `lyt::TextBox` takes for fonts in layout archives), and requires both fonts to agree on every
  code point, and every unmapped code point to give the alternate glyph. It prints the font
  metrics from the `ResFontBase` getters, the relocated slots as file offsets, the CMAP and CWDH
  chains as the decomp follows them, and one line per mapped code point.
- **Messages.** `PortSwapBmgText` (§6), then `RPSysMessage` (the game's loader; it derives from
  `EGG::MsgRes`). `src/Pack/RPKernel/RPSysMessage.cpp` is compiled unmodified through
  `tests/rpsysmessage_unit.cpp`, which supplies the include guard of the `Pack/RPKernel.h`
  umbrella header (most of RPKernel does not build on the host yet). For each MID1 entry the
  harness splits the id as `getMsgEntry` does, calls `GetMsgEntry`, `GetAttribute`, `GetScale`,
  `GetCharSpace` and `GetMsg`, walks the text with `MsgRes::analyzeTag`, and calls `wcslen`
  (the MSL_C version).

`tests/test_resources.py` builds the same lines from `port/assets/tools/fmt_text_font.py`: the
`BrfntFont` class (CMAP/CWDH chains from FINF, first matching map decides, dictionary lookup for
scan maps, cell and sheet arithmetic from TGLP) and `bmg_messages()` (id split per MID1 form
supplement, first INF1 entry with the same id, code units and tags read from the UTF-16BE bytes).
Both are written from the file format, not translated from the decompiled readers.

`fmt_text_font.py` used to report 5 of the 8 fonts as broken ("CMAP pNext does not point to a
CMAP payload"). The parser was wrong: it unpacked the CMAP header as `>HHHI`, which reads
`pNext` at payload +0x6 instead of +0x8 (it dropped the reserved `u16`), and it treated `pNext`
as relative to the current block, whereas `Rebuild` resolves it from the start of the file. It
also read direct maps' base index from +0x8 instead of +0xC. All 8 fonts parse now; the report is
`port/assets/formats/text_font.report.txt`.

### The layout harness (`tests/lyt_harness.cpp`)

For every mounted archive with a `blyt` directory (at the root, as in the game's layout
archives, or under `arc/`, as in the HOME Menu archives) it attaches a real
`lyt::ArcResourceAccessor` (root `""` or `"arc"`; the game passes `""`,
`RPSysLytResAccessor::mountArchive`), then for every `.brlyt`:

- `Layout::Build`: panes, pictures, text boxes (fonts from the archive through `ResFont`),
  windows, boundings, groups and materials. Each material fetches its `timg/*.tpl` and calls
  `TexMap::Set(TPLPalette*, 0)`, which runs `TPLBind` (§9) the first time.
- It prints the pane tree depth-first (kind from `ut::DynamicCast`, name, user data, visibility,
  alpha, base position, translate/rotate/scale/size, material; text length, hash, font,
  font size, spacing and colours for text boxes; vertex colours; window frame materials), every
  material (TEV colours and constant colours, texture SRT 0, every texture's TPL size, format
  and wrap modes through `TexMap`), and every group with its panes.
- For every `.brlan` of the same archive: `Layout::CreateAnimTransform` and
  `Layout::BindAnimation`. It prints `GetFrameSize`, `IsLoopData`, and for each animation
  content whether `AnimTransformBasic::Bind` linked it to a pane or material.

Layouts are allocated through `Layout::SetAllocator` with an `MEMAllocator` whose functions call
the M0 arena. For every `.tpl` leaf it calls `TPLBind` itself unless a layout already bound it
(the "already bound" test of `TexMap::Set`, which therefore runs on both branches: 443 palettes
are bound by layouts first, 28 by the harness), and reads every descriptor back through `TPLGet`
and `lyt::TexMap::Set(TPLPalette*, id)`, which must agree.

`tests/test_layouts.py` builds the same lines from `port/assets/tools/fmt_layout.py` (imported
read-only; BRLYT, BRLAN and TPL decoded from the file format) and `wiiarc.py`. The rules that
decide a line (block nesting, `grp1` depth 1 only, the `GX_MAX_TEXMAP` and `MAX_TEX_SRT`
clamps, the text buffer length, name matching for binding) are written out from the format.
One `fmt_layout.py` bug turned up: `parse_string_list` resolves `txl1`/`fnl1` name offsets
from the block start, but they are relative to the first entry (block + 12, as
`Material` and `TextBox` resolve them), so its `name` fields are empty or wrong; the test
reads the names itself.

### Host stand-ins (`src/host_sdk.cpp`, `src/host_arena.cpp`, `src/host_text.cpp`, `src/host_tpl.cpp`, `src/host_gx.cpp`)

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
  - `Heap::alloc`/`free` and the global and heap `operator new`/`delete` allocate from the M0
    arena (§5): a first-fit, address-ordered free list with coalescing, MEM2 first, then MEM1.
    Any power-of-two alignment is honoured (the sign of the alignment, head or tail, is ignored)
  - `Heap::findContainHeap` returns NULL, so disposers are not attached to heaps
- **OS addresses**: `port/host/include/revolution/OS/OSAddress.h` shadows the SDK header.
  `OSPhysicalToCached`/`Uncached` decode an address through M0; `OSCachedToPhysical` returns the
  physical address as a `u32` (`vaddr & 0x3FFFFFFF`). On the Wii it returns a pointer that every
  caller casts to `u32`.
- **Text**: `PortSwapBmgText` (§6).
- **TPL**: `TPLBind` / `TPLGet` (§9).
- **ef lists**: `RES_LIST_*` (`src/host_reslist.cpp`, §12).
- **GX objects**: `GXInitTexObj`, `GXInitTexObjCI`, `GXInitTexObjLOD`, `GXInitTexObjTlut`,
  `GXGetTexObjTlut` and `GXInitTlutObj` fill the Wii-layout objects with big-endian words and
  emulated physical addresses (`src/host_gxtexobj.cpp`, §11).
- **GX**: every other GX call that `lyt`, `ut::CharWriter` and the g3d resource code link is a
  no-op (`src/host_gx.cpp`), including `GXSetArray` and `GXCallDisplayList`. The same file has
  `nw4r::g3d::detail::Copy32ByteBlocks` as a `memcpy` (`g3d_cpu.cpp` is paired-single
  assembly).
- **MTX**: `PSMTXCopy`, `Concat`, `Scale`, `TransApply`, `RotRad` and `Identity` in plain C++ (the decomp's
  versions are paired-single assembly).
- **MEM**: `MEMAllocFromAllocator` / `MEMFreeToAllocator` dispatch through the allocator's
  function table, as `src/revolution/MEM/mem_allocator.c` does.
- **VI, DC**: no-ops (`DCFlushRange[NoSync]`, `DCStoreRange[NoSync]`, `DCInvalidateRange`).

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
- **Pointer slots inside files** need a different type: `RES_PTR(T)`, §5.
- **Explicit conversions.** `be<T>`'s constructor from `T` is `explicit`, so `cond ? u16 : be_u16`
  is not ambiguous (`ResFontBase::GetGlyphIndex` has one); assignment still takes a plain `T`.
  `static_cast<SomeEnum>(field)` works through an explicit conversion to enum types
  (`static_cast<GXTexFmt>(sheetFormat)`).

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
- **Result on 2026-10-30, after the ef changes (§12).** All 734 units that
  depend on the files touched up to step 6 (§4; the `TOUCHED` entries before the step 7
  `snd` block in the script, 75 files) are byte-identical, and none were skipped. 320 of them
  depend on the ef files, among them every `nw4r/ef` unit (including the unlinked
  `ef_animcurve.o`) and `RPSysEffectMgr.o`, `RPSysEffectCreator.o` and `RPSysEffectBase.o`.
  (Run on a copy of the tree with the parallel `snd` work reverted to the decomp's files; the
  full current list, 745 units, was also identical.) Before the ef changes, after the g3d
  changes (§11), the same 734 units were byte-identical with 65 files in `TOUCHED`. They include every `nw4r/lyt` and `nw4r/g3d` unit
  (508 units depend on the g3d files),
  `RPPlayerNameTag.o` and the unlinked `RPSysTagProcessor.o` (identical to the decomp's object,
  so its 4 differences from the target are unchanged). Run it after every change to a decomp file:

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
| `port/be.h`, `port/host.h` | The wrapper; `PortHostSetDiscRoot`, `PortSwapBmgText`. |
| `port/arena.h`, `port/resptr.h`, `port/wide.h` | M0, `RES_PTR`, 16-bit `wchar_t` (§5, §6). |
| `revolution/OS/OSAddress.h` | Physical/cached conversions through M0 (§1, host stand-ins). |
| `revolution/TPL/TPL.h` | The TPL structs with `be_*`, `BE_ENUM` and `RES_PTR` members (§9). |
| `port/reslist.h` | `RES_LINK` / `RES_LIST` and the `RES_LIST_*` functions (M2, §12). |

`types.h` also defines `std::stricmp` (MSL has it; `ArcResourceAccessor`'s name lookup uses it)
as `strcasecmp`, and `port/wide.h` adds `std::` using-declarations for the `port_wcs*`
functions so `std::wcslen` resolves after the renaming macros.

Build flags: `-DPORT_HOST=1 -DNONMATCHING -DREVOLUTION -DNDEBUG=1 -DVERSION_RHAE01
-DVERSION_RHAE01_01`, `-std=gnu++17 -fno-exceptions -fno-rtti` (C: `-std=gnu11`),
`-fshort-wchar -include port/wide.h`, `-Werror=int-to-pointer-cast` (C also
`-Werror=pointer-to-int-cast`). CodeWarrior's recursive `-ir include/revolution/BTE` and
`-ir include/egg` become one `-idirafter` per subdirectory, and its `-i include/nw4r` and
`-i include/Pack` become `-idirafter` too (EGG and game headers include siblings by bare name).
The MSL_C wide-string units are built with `-DPORT_WIDE_IMPL -idirafter include/MSL`.

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
| `include/revolution/OS/OSAddress.h` | `OSPhysicalToCached`/`Uncached` turn a `u32` into a pointer | host shadow header through M0 (§1 host stand-ins); `-Wno-int-to-pointer-cast` is gone and the cast is now an error everywhere |
| `include/nw4r/g3d/g3d_obj.h` `TypeObj::GetTypeID` | `reinterpret_cast<u32>` of the type-name address (only compared for equality) | `#if defined(PORT_HOST)`: returns `uintptr_t` |
| `include/egg/gfxe/eggCpuTexture.h` `setBuffer` | `EGG_ASSERT((u32)pBuffer % 32 == 0)` | `#if defined(PORT_HOST)`: the same assert through `uintptr_t` (same `#line`) |
| g3d PAT0 `toResTexArray` / `toResPlttArray` | arrays of `ResTex` / `ResPltt` (one pointer each) overlaid on 4-byte file slots | `PAT0_TEX_SLOT` / `PAT0_PLTT_SLOT` (§11) |

### Pointer slots inside files

There are none on the archive path:

- **U8 nodes** hold offsets and indices, never pointers.
- **`ARCHandle`** is a runtime struct and stores native pointers.
- **`EGG::Archive`** and **`DVDFileInfo`** are also runtime structs, so their larger 64-bit
  layout does not matter.

The 4-byte pointer slots that do bite are in the formats that relocate in place. These are TPL,
`ut::ResFont`, `ef`, g3d texture binding, PMP and the KCL header copy (ASSETS.md §3.1 item 3).
They are inventoried in `port/docs/POINTERS.md`. `RES_PTR(T)` (§5) sits beside `be_*` with the
same Wii/host header split; ResFont is the first user.

One layout trap the wrapper avoids: a byte array like `u8[4]` would have alignment 1 and would
shift every field after a `u8`. `be<T>` stores an `alignas(T)` integer, so it keeps the Wii
offsets. The unit test checks this with `offsetof`.

## 4. Decomp files touched

Every change is minimal and either produces the same tokens for CodeWarrior or is guarded. The
check in §2 confirms all of them.

| File | Change |
|---|---|
| `include/port/be.h` | **New.** The Wii side of the wrapper: macros for the plain types. |
| `include/port/resptr.h` | **New.** The Wii side of `RES_PTR`: `RES_PTR(T)` is `T*`, the cast macros are the casts they replace (§5). |
| `src/revolution/ARC/arc.c` | `#include <port/be.h>`. `ARCHeader` and `ARCNode` fields are declared `be_u32`/`be_s32`. The node's `is_folder:8`/`name:24` bitfield is under `#if PORT_BE_WRAPPED` (host: `u8 is_folder; be_u24 name;`). All 18 direct field reads, and the two `ARCNodeIsFolder`/`ARCNodeGetName` macros, are wrapped in `BE_GET(...)`. |
| `include/nw4r/ut/ut_binaryFileFormat.h` | `#include <port/be.h>`. The `BinaryFileHeader` and `BinaryBlockHeader` members are `be_u32`/`be_u16`. This is the first C++ use: `IsValidBinaryFile` compiles unchanged and reads through the wrapper. |
| `include/nw4r/ut/ut_algorithm.h` | `GetIntPtr` returns `uintptr_t` under `#if defined(PORT_HOST)`. |
| `src/egg/core/eggDvdFile.cpp` | `DvdFile::sync` casts `OSMessage` to `s32` through `intptr_t` under `#if defined(PORT_HOST)`. |
| `include/nw4r/ut/ut_ResFontBase.h` | `#include <port/resptr.h>`. `FontTextureGlyph`, `FontWidth`, `FontCodeMap`, `FontInformation`: the `u16`/`u32` members are `be_u16`/`be_u32` (including `mapInfo[]`), and the six pointer members (`sheetImage`, both `pNext`, `pGlyph`, `pWidth`, `pMap`) are `RES_PTR(...)`. Type tokens only. |
| `src/nw4r/ut/ut_ResFont.cpp` | A host-only `ResolveOffset(port::res_ptr<T>&, void*)` overload under `#if defined(PORT_HOST)` (7 lines). `Rebuild` and its `ResolveOffset<T>(...)` calls are unchanged. |
| `src/nw4r/ut/ut_ResFontBase.cpp` | The local `CMapScanEntry` / `CMapInfoScan` members in `FindGlyphIndex` are `be_u16`. Type tokens only. |
| `include/nw4r/ut/ut_CharStrmReader.h` | `StepStrm` (`static_cast<const T*>(mCharStrm) += offset`, a CodeWarrior extension) is written as an assignment under `#if defined(PORT_HOST)`; needed by `ut_Font.cpp` / `ut_CharStrmReader.cpp`, which `ResFont` links. |
| `include/egg/util/eggMsgRes.h` | `#include <port/be.h>`. The BMG header, block, INF1 entry and MID1 numeric members are `be_u32`/`be_u16` (`mContent` of MID1 is `be_u32[]`). `wchar_t mContent[1]` (DAT1) is unchanged; `-fshort-wchar` gives it the Wii layout. |
| `src/egg/util/eggMsgRes.cpp` | Two `#if defined(PORT_HOST)` blocks: the block-bounds assert at `#line 77` compares `const u8*` instead of `(u32)` casts, and `analyzeTag` reads the tag id with `port_load_be32` (tag payloads stay big-endian, §6). The Wii branches are the original lines with the original `#line`. |
| `include/Pack/RPKernel/RPSysMessage.h` | `RPSysMessage::Entry::scale` is `be_u16`. |
| `include/nw4r/ut/ut_Color.h` | `#include <port/be.h>`. `ToU32ref()` returns `be_u32&` (and `operator=(const GXColor&)` reads through `be_u32`): the u32 form of a colour is `0xRRGGBBAA` on every host, so colours read from `be_u32` file fields (vertex and text colours) land in the right bytes. Type tokens only. |
| `include/nw4r/lyt/lyt_types.h` | `#include <port/be.h>`. `detail::TestBit` / `GetBits` overloads for `port::be<T, S>` under `#if PORT_BE_WRAPPED`. A `res` block after `TexSRT`: on the host the file value types `res::VEC2`, `VEC3`, `Size`, `GXColorS10`, `TexSRT` (big-endian members, each converting to the runtime type); on the Wii `typedef`s of `math::VEC2`, `math::VEC3` and `TexSRT` (`res::Size` and `res::GXColorS10` are host-only; on the Wii the unqualified names in `res` find the runtime types as before). |
| `include/nw4r/lyt/lyt_common.h` | Includes `lyt_types.h`. `res::TexCoord` (host: `res::VEC2[4]`, Wii: `typedef detail::TexCoord`). `GetSignatureInt` reads `be_s32`, `GetStrTableStr` reads a `be_u32` offset table. |
| `include/nw4r/lyt/lyt_resources.h` | `BinaryFileHeader`, `DataBlockHeader`, `Texture`, `Font`, the three list headers, `res::TexMap` and `MaterialResourceNum::bits`: `be_u16`/`be_u32`. |
| `include/nw4r/lyt/lyt_pane.h` | `res::Pane`: `math::VEC3`/`math::VEC2` → `VEC3`/`VEC2` (resolved in `res`; the same types on the Wii). |
| `include/nw4r/lyt/lyt_picture.h`, `lyt_textBox.h`, `lyt_group.h`, `lyt_animation.h` | `res::Picture`, `res::TextBox`, `res::Group`, `res::AnimationBlock`, `res::AnimationInfo`: integer and float members `be_*`. |
| `include/nw4r/lyt/lyt_window.h` | `res::WindowContent`, `WindowFrame`, `Window` members `be_*`; a host-only `res::InflationLRTB` under `#if PORT_BE_WRAPPED`. |
| `src/nw4r/lyt/lyt_animation.cpp` | `res::AnimationTarget`, `StepKey`, `HermiteKey` members `be_*`; the offset tables are read as `be_u32` (`ConvertOffsToPtr<be_u32>`, `const be_u32*`). |
| `src/nw4r/lyt/lyt_picture.cpp`, `lyt_textBox.cpp`, `lyt_window.cpp` | Material and frame offset tables read as `be_u32`. `lyt_textBox.cpp` also has one `#if defined(PORT_HOST)` block after `SetString` that converts the copied text from UTF-16BE (the file stays as it is). |
| `src/nw4r/lyt/lyt_material.cpp` | The texture SRTs in the file are read as `res::TexSRT` (3 lines). |
| `src/nw4r/lyt/lyt_common.cpp` | `TexCoordAry::Copy` reads `res::TexCoord`. |
| `src/nw4r/lyt/lyt_texMap.cpp` | `#include <port/resptr.h>`; the bound test `reinterpret_cast<u32>(descriptorArray) < 0x80000000` → `PORT_PTR_TO_U32(...)` (POINTERS.md §3.1). |
| `src/nw4r/lyt/lyt_arcResourceAccessor.cpp` | `GetResourceSub`'s `unsigned long* pSize` → `u32* pSize` (the declaration's type; `u32` is `unsigned long` on the Wii). |
| `src/Pack/RPKernel/RPSysTagProcessor.cpp` | §10. |
| `include/Pack/RPKernel/RPSysSystemWinMgr.h` | `friend class RPSysTagProcessor;` (§10). |
| `src/Pack/RPParty/RPPartyCommon/RPPlayerNameTag.cpp` | §10: an `#if defined(PORT_HOST)` include and an `#if defined(PORT_HOST)` argument. |
| `include/nw4r/g3d/res/*.h` (23 headers: `g3d_rescommon`, `resdict`, `resmdl`, `resmat`, `resshp`, `restev`, `restex`, `respltt`, `resvtx`, `resnode`, `reslightset`, `resanm`, `resanmchr`, `resanmclr`, `resanmtexpat`, `resanmtexsrt`, `resanmvis`, `resanmshp`, `resanmscn`, `resanmamblight`, `resanmlight`, `resanmfog`, `resanmcamera`) | §11: every `u16`/`s16`/`u32`/`s32`/`f32` member of the file structs is `be_*`, every enum member `BE_ENUM(E, u32)`; type tokens only. The runtime `*AnmResult` structs, `BlendVtx` and `ResCacheVtxDescv` are unchanged. `g3d_rescommon.h` and `g3d_resanm.h` include `<port/be.h>`; `g3d_resmat.h` includes `<port/resptr.h>` and declares `pTexData`/`pPlttData` as `RES_PTR(...)`. `g3d_rescommon.h`: `NW4R_G3D_RESFILE_NAME_DEF` has a `#if PORT_BE_WRAPPED` form that initialises `len` with `be_u32(...)` (the Wii form is the original). `g3d_resanm.h` (`GetResBoolAnmFramesResult`) and `g3d_reslightset.h` (`GetLightResName`): one local pointer type each is `be_u32*` / `be_s32*`. |
| `src/nw4r/g3d/res/g3d_resmat.cpp`, `g3d_reslightset.cpp`, `g3d_resanm.cpp`, `g3d_resanmtexsrt.cpp` | §11: local pointers into file fields are `be_*` (`pCtrl`; `SetBind`/`IsBound` parameters and `pId`; `pColorArray`; `pToTexData`). Type tokens only. |
| `src/nw4r/g3d/res/g3d_resanmtexpat.cpp` | §11: `PAT0_TEX_SLOT` / `PAT0_PLTT_SLOT` (Wii: `typedef ResTex` / `ResPltt`; host: a 4-byte `res_ptr` that assigns from `ResTex`/`ResPltt`, under `#if defined(PORT_HOST)`) replace `ResTex`/`ResPltt` as the array element type in `GetAnmResult`, `Bind` and `Release`; the name arrays are read as `be_s32`. |
| `include/nw4r/g3d/platform/g3d_cpu.h` | `#include <port/be.h>`; under `#if PORT_BE_WRAPPED`, `fastcast::U16_0ToF32`, `S7_8ToF32` and `S10_5ToF32` overloads for `be_u16*`/`be_s16*` (CHR0 key frames). |
| `include/nw4r/g3d/g3d_obj.h`, `include/egg/gfxe/eggCpuTexture.h` | §3: `uintptr_t` under `#if defined(PORT_HOST)`. |
| `include/nw4r/snd/snd_SoundArchiveFile.h`, `snd_SeqFile.h`, `snd_BankFile.h`, `snd_WsdFile.h`, `snd_WaveFile.h` | §13: every `u16`/`s16`/`u32`/`s32`/`f32` member of the file structs is `be_*` (`StringTable` is `Util::Table<be_u32>`). Type tokens only. |
| `include/nw4r/snd/snd_Util.h` | §13: `#include <port/be.h>`; `DataRef::reserved`/`value` and `Table::count` are `be_u16`/`be_u32`. Type tokens only. |
| `include/nw4r/snd/snd_SoundArchive.h` | §13: `#include <port/be.h>`; `FilePos` members are `be_u32` (`ReadFilePos` copies it whole out of the file). |
| `include/nw4r/snd/snd_Types.h` | §13: `VOLUME_MIN_DB`/`MAX_DB`/`RANGE_DB` are `constexpr` under `#if defined(PORT_HOST)` (GCC needs it for float constants in constant expressions). |
| `include/nw4r/snd/snd_SoundStartable.h`, `snd_SoundArchivePlayer.h`, `include/egg/audio/eggAudioArcPlayerMgr.h` | §13: the `unsigned int` overloads of `StartSound`/`HoldSound`/`PrepareSound`/`LoadGroup` and `ArcPlayer::startSound`/`prepareSound`/`holdSound` under `#if !defined(PORT_HOST)`: `u32` is `unsigned int` on the host, so they would redeclare the `u32` ones. |
| `include/egg/audio/eggAudioHeapMgr.h` | §13: `stateProc` casts `OSMessage` to `s32` through `intptr_t` under `#if defined(PORT_HOST)`. |
| `include/nw4r/ut/ut_algorithm.h` | §13: the pointer `RoundUp`/`RoundDown` round a `uintptr_t` (mask widened first) under `#if defined(PORT_HOST)`. |
| `include/nw4r/snd/snd_Channel.h`, `snd_NoteOnCallback.h`, `snd_SeqTrack.h`, `snd_WsdPlayer.h`, `src/nw4r/snd/snd_Channel.cpp`, `snd_SeqTrack.cpp`, `snd_WsdPlayer.cpp` | §13 (POINTERS.md §5.2): the channel callback argument, `Channel::mCallbackData` and `NoteOnInfo::channelCallbackData` are `uintptr_t`, and `reinterpret_cast<u32>(this)` is `reinterpret_cast<uintptr_t>(this)`. Type tokens only (`uintptr_t` is `unsigned long` on the Wii, like `u32`). |
| `src/nw4r/snd/snd_SoundThread.cpp`, `snd_AxManager.cpp`, `snd_AxVoiceManager.cpp` | §13: the thread message, aux-callback context and voice cookies cast through `uintptr_t`. Type tokens only. |
| `include/revolution/AX/AXAlloc.h`, `AXVPB.h`, `src/revolution/AX/AXAlloc.c` | §13: `AXAcquireVoice`'s `userContext` and `AXVPB::userContext` are `uintptr_t`. Type tokens only. |
| `include/nw4r/snd/snd_AxfxImpl.h`, `src/nw4r/snd/snd_AxfxImpl.cpp` | §13: `AxfxImpl::Alloc` takes `size_t` (the `AXFXAllocHook` parameter type). Type tokens only. |
| `src/nw4r/snd/snd_AxVoice.cpp` | §13: host forms of `GetDspAddressBySample` / `GetSampleByDspAddress` under `#if defined(PORT_HOST)` (the host `OSCachedToPhysical` returns a `u32`); the Wii functions are unchanged in `#else`. |
| `src/nw4r/snd/snd_Util.cpp` | §13: `#include <port/resptr.h>`; the `REFTYPE_ADDRESS` DataRef goes through `PORT_U32_TO_PTR` (Wii: the same `reinterpret_cast`). |
| `src/nw4r/snd/snd_WaveFile.cpp` | §13: the channel offset table is read as `const be_u32*`. |
| `include/port/reslist.h` | **New.** The Wii side of M2: `RES_LINK` is `nw4r::ut::Link`, `RES_LIST` is `nw4r::ut::List`, `RES_LIST_Init/Append/Remove/GetFirst/GetLast/GetNext/GetPrev` are the `nw4r::ut::List_*` functions (§12). |
| `include/nw4r/ef/ef_resource.h` | §12: includes `<port/be.h>`, `<port/reslist.h>`. `EffectProject` / `TextureProject`: `headersize` `be_u32`, `projectlink` `RES_LINK`, `projectnameLen` `be_u16`; `Resource::mBREFFList` / `mBREFTList` `RES_LIST`. Type tokens only. |
| `include/nw4r/ef/ef_res_texture.h` | §12 (ef-4): `TextureData.name`, `.texture`, `.tlut` `RES_PTR(...)`; `width`, `height`, `dataSize`, `tlutEntries`, `tlutSize` `be_*`. Type tokens only. |
| `include/nw4r/ef/ef_res_emitter.h` | §12 (ef-2, ef-3): `EmitterResource.name` `RES_PTR(char)`, `headersize` `be_u32`; the scalar members of `EmitterDesc`, `EmitterDrawSetting` and `Lighting` `be_*` (the `math::VEC3` members stay plain); the track count and size reads through `be_u16*`/`be_u32*`; `Get{Ptcl,Emit}TrackTbl` return `RES_PTR(u8)*`; `LocateTracks` uses `be_u32*` tables and `PORT_STORE_PTR32` for the two pointer stores. |
| `include/nw4r/ef/ef_res_animcurve.h` | §12 (ef-7): `AnimCurveHeader` `u16`/`u32` members, the four table `count`/padding members and `AnimCurveNameTable::Name::work` `be_*`. Type tokens only. |
| `include/nw4r/ef/ef_particle.h` | §12 (ef-6): `ParticleParameterDesc.mTexture[3]` `RES_PTR(TextureData)`; `textureRotate`, `textureWrap`, `rotateOffset` `be_*`. The runtime `ParticleParameter` is unchanged. |
| `src/nw4r/ef/ef_resource.cpp` | §12: the local `FileHeader`, `BlockHeader`, `NameTable` members `be_*`; the 14 `ut::List_*` calls are `RES_LIST_*` and `sizeof(ut::Link)` is `sizeof(RES_LINK)` (ef-1); the name slot store casts to `RES_PTR(char)*` (ef-2); the two curve `work` stores use `PORT_PTR_TO_U32` (ef-7); `ppWork` is `RES_PTR(TextureData)*` (ef-6); three `u16` reads are through `be_u16*`. |
| `src/nw4r/ef/ef_animcurve.cpp` | §12 (ef-7, unlinked unit): the texture name table is read as `AnimCurveTable<RES_PTR(TextureData)>`, the child curve's `work` through `PORT_U32_TO_PTR(EmitterResource*, ...)`. 3 lines. |
| `include/nw4r/ef/ef_types.h`, `include/nw4r/ef/ef_effect.h` | §12 (POINTERS.md §5.2 cookie): `ForEachParam` is `uintptr_t` under `#if defined(PORT_HOST)` (Wii: `u32`), and `Effect::Modifier_SetSimpleLightAmbient/SetScale/SetRotate` cast `&x` to `ForEachParam` instead of `u32`. |

No other files under `src/` or `include/` were changed. Everything else is new and lives under
`port/host/`, `port/docs/` and `port/assets/`.

## 5. M0 and `RES_PTR(T)`

Design: `POINTERS.md` §2. Implementation:

- **M0, `port/host/include/port/arena.h`, `src/host_arena.cpp`.** `PortArenaInit` reserves
  `0x14000000` bytes with `mmap(PROT_NONE, MAP_NORESERVE)` and makes MEM1 (24 MiB at offset 0)
  and MEM2 (64 MiB at offset `0x10000000`) read/write; the gap stays inaccessible. The first
  `0x3100` bytes of MEM1 are never allocated, so address 0 means NULL. `port_vaddr(p)` is
  `0x80000000 + (p - base)` and `port_hptr(v)` the inverse; both are inline and abort with a
  message when the address is outside the block. `PortArenaAlloc`/`PortArenaFree` back every
  heap allocation and every `operator new` (§1), and initialise the arena on first use, so static
  constructors are covered. `PortArenaMEM1Lo/Hi` and `PortArenaMEM2Lo/Hi` give the bounds the
  Phase 7 `OSGetMEM*Arena*` will return.
- **M1, `include/port/resptr.h` (Wii) and `port/host/include/port/resptr.h` (host).**

  | Name | Wii | Host C++ | Host C |
  |---|---|---|---|
  | `RES_PTR(T)` | `T*` | `port::res_ptr<T>`: one `be_u32`, 4 bytes, alignment 4, trivially copyable | `res_ptr_c` |
  | read | the member | implicit `operator T*()`, `->`, `[]` through the decoded pointer; `RES_PTR_GET(slot)` | `RES_PTR_GET(slot)` |
  | write | `slot = p` | `slot = p` (`operator=(T*)` stores `port_vaddr(p)` big-endian); `RES_PTR_SET` | `RES_PTR_SET(slot, p)` |
  | `slot != 0`, `== NULL` | pointer test | compares the raw u32, so it also works on an unrelocated offset | `RES_PTR_RAW(slot) != 0` |
  | `RES_PTR_RAW(slot)` | `reinterpret_cast<u32>(slot)` / `(u32)(slot)` | `slot.raw()`: the u32 in host order (offset before relocation, address after) | same |
  | `PORT_PTR_TO_U32(p)` | `reinterpret_cast<u32>(p)` / `(u32)(p)` | `port_vaddr(p)`; for a `res_ptr` argument its `raw()` | `port_vaddr(p)` |
  | `PORT_U32_TO_PTR(T, v)` | `reinterpret_cast<T>(v)` / `(T)(v)` | `reinterpret_cast<T>(port_hptr(v))` | `(T)port_hptr(v)` |
  | `slot = NULL` (C++) | `slot = NULL` | `operator=` for integer constants: stores 0 (any other value aborts) | — |
  | `PORT_STORE_PTR32(slot, p)` | `slot = PORT_PTR_TO_U32(p)` | same | same |
  | `PORT_RES_PTR_WRAPPED` | 0 | 1 | 1 |

  A relocated file in host memory is the Wii image of that file: every slot holds the
  big-endian address the Wii would have stored, for a buffer at the same emulated address.
- **ResFont.** `Rebuild` calls `ResolveOffset<T>(slot, pHeader)` on each slot. On the host the
  overload in `ut_ResFont.cpp` stores `pBase + (s32)slot.raw()` through `operator=`. All readers
  in `ut_ResFontBase.cpp` compile unchanged. The second load of an `RFNU` buffer finds FINF and
  reads the slots as addresses, exactly as on the Wii.
- **Warnings.** `-Werror=int-to-pointer-cast` (C and C++) and `-Werror=pointer-to-int-cast` (C)
  make every narrowing cast between pointers and integers a build error; in C++
  `reinterpret_cast<u32>(ptr)` already is one on LP64. Together with the layout checks (§7) these
  are the checklist for the remaining `POINTERS.md` sites.

## 6. `wchar_t` and BMG text

- **16-bit `wchar_t`.** Everything in the host build, decomp and host code alike, is compiled with
  `-fshort-wchar -include port/wide.h`. `port/wide.h` `static_assert`s `sizeof(wchar_t) == 2`
  (C and C++), includes the C library's `<wchar.h>`/`<cwchar>` first so their declarations are
  seen under the real names, declares `port_wcslen`, `port_wcscpy`, `port_wcsncpy`,
  `port_wcscmp`, `port_wcschr`, `port_wmemcpy` and `port_wmemchr`, and `#define`s the plain names
  to them. The definitions are the decompiled `src/MSL_C/wstring.c` and `wmem.c`, compiled
  unmodified with `-DPORT_WIDE_IMPL` (which skips the libc include, so the macros rename the MSL
  definitions) and `-idirafter include/MSL` (for `<internal/wstring.h>`). `swprintf` and
  `vswprintf` (`src/MSL_C/wprintf.c`) are not routed yet: they need the MSL locale tables and
  `__num2dec`; add them with the first unit that formats text. Never pass a `wchar_t` string to a
  libc wide function.
- **BMG text byte order.** `PortSwapBmgText(void*)` (`src/host_text.cpp`, declared in
  `port/host.h`) swaps the DAT1 text of a loaded BMG to host order once. It follows each INF1
  entry's string, swaps code units, swaps the `0x001A` tag mark, leaves the tag's length, group,
  id and parameter bytes as in the file, and marks what it has done so shared strings are
  swapped once. `EGG::MsgRes::analyzeTag` reads the length byte directly (unchanged) and group
  and id with `port_load_be32` on the host. Call it after loading and before constructing
  `RPSysMessage`; the Phase 7 loader will call it where the game creates `RPSysMessage`
  (for example `RPTnkLyt`, `RPDucHud`, `RPBomLyt`, `RPPnpGame`, `RPHkyGame`).
  BRLYT `txt1` strings are converted by `lyt::TextBox` when it copies them (§4).

## 7. Layout checks

`tests/layout_asserts.cpp` (`test_layout`) holds `static_assert(sizeof(...))` and
`offsetof(...)` checks, taken from the `// at 0xNN` comments in the headers, for every struct
moved to `be_*`/`RES_PTR` so far: `BinaryFileHeader`, `BinaryBlockHeader`, `CharWidths`,
`FontTextureGlyph`, `FontWidth`, `FontCodeMap`, `FontInformation`, the `EGG::MsgRes` block
structs (checked from a derived class because they are protected), `RPSysMessage::Entry`, the
four TPL structs, the `lyt::res` blocks and value types (`AnimationTarget`, `StepKey` and
`HermiteKey` are local to `lyt_animation.cpp`; the layout harness covers them), and
`ut::Color`, and the g3d file structs (dictionary, file, MDL0 and its info, node, vertex
arrays, pre-primitive DL, shape, material with its GX objects, TEV, TEX0, PLT0, every animation
header and info block, light set, key frames) plus `GXTexObj`/`GXTlutObj` sizes, and the ef
resource structs (`RES_LINK`, `EffectProject`, `TextureProject`, `EmitterResource`,
`TextureData`, `Lighting` under `#pragma pack(1)`, `EmitterDrawSetting`, `EmitterDesc`,
`ParticleParameterDesc`, `AnimCurveHeader`, `AnimCurveNameTable`). It also checks
that DAT1 text elements are 2 bytes. The decomp's `TPL.h` comments
`TPLClutHeader.unpacked` as `at 0x1`; it is at 0x2 (after the `u16`), on the Wii too. The checks run when the file compiles, in both
the 64-bit and `-m32` builds. Add a block for each struct that moves to the wrapper types.

## 8. Next steps

- g3d beyond loading (§11): the `math::_VEC3`/`_MTX34`/`_VEC2` members of the file structs
  (node SRT and matrices, bounding volumes, vertex-array min/max, camera/light/fog values,
  texture-matrix effects) are still plain floats, so they read byte-swapped on a little-endian
  host. Nothing on the load path reads them; the calc and draw code will need `res::` value types
  as in `lyt` (§9). Animation evaluation (`GetAnmResult`) compiles but is not validated: it also
  uses `nw4r::math` and OS fast-cast helpers (`OSu16tof32`, `F32ToU16`) that are inline assembly.
- Drawing layouts needs the Phase 7 GX layer behind `src/host_gx.cpp`, and the
  `nw4r::math` inline-assembly helpers: `math_arithmetic.h` `FAbs` (and others written as
  `register` + asm) return an uninitialised value on the host, because `ASM(...)` compiles away.
  Nothing on the build/bind path calls them.
- Move the next loaders onto `be_*` as they are brought up. Each needs the type declarations, a
  small number of C `BE_GET`s, a `layout_asserts.cpp` block and a `check_matching.py` run:
  - `g3d::Res*Data`
  - `snd` file structs
- ef beyond loading (§12): the `math::VEC2`/`VEC3` members of `EmitterDesc`,
  `ParticleParameterDesc` and `Lighting`, and the curve keys read by `ef_animcurve.cpp`, are
  still plain, and particle evaluation and drawing are not validated.
- Route `swprintf`/`vswprintf` to `src/MSL_C/wprintf.c` (§6) when the first caller is built.
- Replace `src/host_sdk.cpp` with the Phase 7 OS/DVD/heap layer: real threads, message queues
  that block, and MEM heaps (the decomp's `src/revolution/MEM` could compile as-is once the OS
  layer exists), carved from the M0 arena.

## 9. TPL and layouts

- **TPL.** `port/host/include/revolution/TPL/TPL.h` shadows the SDK header: the scalar fields
  are `be_*`, the GX enum fields `BE_ENUM(E, u32)`, and the five pointer slots
  (`descriptorArray`, `textureHeader`, `CLUTHeader`, both `data`) `RES_PTR`. `revolution/TPL.h`
  includes it inside `extern "C"`, so it pulls in the wrapper headers under `extern "C++"`.
  `src/host_tpl.cpp` is `TPLBind`/`TPLGet` with the algorithm of `src/revolution/TPL/TPL.c`
  (same order, the same `unpacked` guards), storing `palette + offset` through `RES_PTR`, i.e.
  as M0 addresses: a bound palette is the Wii image of the palette, and `TexMap::Set`'s test
  `PORT_PTR_TO_U32(descriptorArray) < 0x80000000` (the raw slot value) works on every host.
  The decomp's `TPL.c` is not compiled on the host (its casts through `u32` cannot be expressed
  with the wrapper in C); image and palette bytes stay GX-native.
- **Layout resource structs.** Every integer and float field that the CPU reads from a BRLYT or
  BRLAN is `be_*` (§4). Fields that are copied into runtime objects whole (`math::VEC2/VEC3`,
  `Size`, `TexSRT`, `GXColorS10`, `InflationLRTB`, texture coordinates) have a `res::` file type
  on the host that converts on copy, and are the runtime type itself on the Wii. Fields of one
  byte (most material GX state) are unchanged and copied as bytes. Colours stored as `u32`
  (vertex and text colours) go through `ut::Color`'s `be_u32` view. The animation keys are read
  big-endian on every `Animate`, so the file is never swapped or rewritten.

## 10. Hard-coded runtime offsets (POINTERS.md step 4)

- **`RPSysTagProcessor.cpp` (message tag `0x20002`).** Was
  `*reinterpret_cast<bool*>(reinterpret_cast<u32>(RPSysSystemWinMgr::instance()) + idx + 0xD4)`.
  Now `reinterpret_cast<uintptr_t>(instance()) + idx + offsetof(RPSysSystemWinMgr, mSeType)`:
  the same sum, with the member named and the address in an integer as wide as a pointer. On
  the Wii `uintptr_t` and `size_t` are `unsigned long` and `offsetof` is `0xD4`, so CodeWarrior
  emits the same code (the natural `instance()->mSeType[idx] = true` still swaps registers, so
  the NONMATCHING-DEBT note stays). `RPSysSystemWinMgr` declares `RPSysTagProcessor` a friend
  for the `offsetof` of the private member. No `#if`; the unit compiles in both host builds
  (`make` builds its object).
- **`RPPlayerNameTag.cpp` (the Mii name of each player).** Was
  `reinterpret_cast<const wchar_t*>(reinterpret_cast<u32>(pHeads[mPlayer]) + 0x68)`, i.e.
  `RPSysAvatar::mAdditionalInfo.name`. The Wii build cannot include the class (its header chain
  adds g3d data this unit does not have), so the host form sits in `#if defined(PORT_HOST)`:
  `pParams->pHeads[mPlayer]->mAdditionalInfo.name`, with `RPKokeshiMgr.h` (the
  `RPKokeshiHead` definition) included under the same guard. The Wii branch is the original line.
  It compiles in both host builds (the 64-bit build since the g3d and `eggCpuTexture.h` fixes,
  §3).

## 11. g3d resources (POINTERS.md step 5)

### What runs

`tests/g3d_harness.cpp` walks the disc like the other harnesses. For every mounted archive it
collects the files that start with `bres` (132 on the US disc, all inside archives; none on the
disc's top level) and sets each one up in place as
`RPGrpModelResManager::InternalCreateData` (`src/Pack/RPGraphics/RPGrpModelResManager.cpp`)
does, with the real decompiled `nw4r::g3d` resource code (`src/nw4r/g3d/res/*.cpp`, 22 units):

1. `ResFile::CheckRevision` (every MDL0, TEX0, PLT0 and animation).
2. `ResFile::Init`: `ResShp::Init` writes the CP `ARRAYBASE`/`ARRAYSTRIDE` commands for each
   vertex array into the shape's pre-primitive display list (`ResShp::GXSetArray`); the other
   `Init`s only flush the data cache.
3. `ResFile::Bind(itself)`: `ResMat::Bind` → `ResTexPlttInfo::Bind` stores the `ResTexData` /
   `ResPlttData` address in the material and initialises the material's `GXTexObj` / `GXTlutObj`;
   `ResAnmTexPat::Bind` fills the PAT0 texture and palette arrays.
4. `ResAnmScn::Bind(itself)` for each SCN0 (light-set ids).

The decompiled game code binds a file only to itself (there is no `ResFile::Bind(other)` call
in `src/Pack`; the API supports it). One file needs another:
`Common/RPCowScene/common.carc/G3D/cow_field2P.brres` does not resolve all its textures by
itself, so the harness then binds each file that is still unresolved to the other BRRES of its
archive, which resolves it. After that every texture, palette and PAT0 slot on the disc is bound.

It prints, through the decompiled accessors: per file the root group count, the
`CheckRevision` result and both bind results; every root group with its entry names; per MDL0
the material, shape, node, vertex array, TEV and texture-info counts, `numViewMtx` and the
scaling rule; per TEX0 the size, format, mipmap count, CI flag, image offset and LOD range
(`ResTex::GetTexObjParam`); per PLT0 the format and entry count; per animation (CHR0, CLR0,
SRT0, PAT0, SCN0; VIS0 and SHP0 are supported but not on the disc) the revision, frame count,
policy, node/material/light-set count and the size of its data dictionary.

`tests/test_models.py` builds the same lines from `port/assets/tools/fmt_brres_model.py`
(imported read-only; container, MDL0, TEX0, PLT0) and its own animation header reader (the
animation parser in `port/assets/tools` is not finished). It also predicts the bind results
from the format: a model binds when every name in its texture/palette name dictionaries is in
the file's (or, finally, the archive's) texture/palette groups, and `ResFile::Bind` returns the
last PAT0's result when there is one (the `@bug` noted in `g3d_resfile.cpp`). One
`fmt_brres_model.py` bug turned up: TEX0 is unpacked with `">IiiIIHHIIfFii"`, where `F` is a
complex number, so its `max_lod`, `original_path` and `toResUserData` are wrong; the test
reads `max_lod` itself.

### The set-up file is the Wii's image

Contract (coordinator decision on POINTERS.md §8 question 2): GX objects inside model files
(`GXTexObj` 0x20, `GXTlutObj` 0xC) keep their Wii sizes and layouts and hold 32-bit emulated
Wii addresses (M0); the Phase 7 GX layer decodes them. Display lists and vertex arrays stay
big-endian and GX-native, and `GXSetArray`-style patches write emulated physical addresses
big-endian, exactly as on the Wii. So, for a buffer at the same emulated address, a set-up file
is byte-identical to the Wii's in-memory copy.

- **g3d-1** (`pTexData`, `pPlttData`): `RES_PTR(ResTexData)` / `RES_PTR(ResPlttData)`. The
  code is unchanged; `r.pTexData = NULL` in `Release` uses a new `res_ptr::operator=` for
  integer constants (GCC's `NULL` is an integer; anything but 0 aborts).
- **g3d-2** (GX objects): `src/host_gxtexobj.cpp` is `GXInitTexObj`, `GXInitTexObjCI`,
  `GXInitTexObjLOD`, `GXInitTexObjTlut`, `GXGetTexObjTlut` and `GXInitTlutObj` with the bit
  layout of `src/revolution/GX/GXTexture.c` (`GXTexObjImpl` / `GXTlutObjImpl`), computed in
  host order and stored big-endian; the image and palette address is `OSCachedToPhysical` of
  the M0 address, and `userData` (a `void*` on the Wii) is 0, as after `GXInitTexObj` on the
  Wii. Float-to-`u8` conversions go through `s32`, as `fctiwz` does, so a negative LOD bias
  gives the Wii's byte. `test_port` checks the words for hand-worked examples.
- **g3d-3** (`ResShp::GXSetArray`): no source change. On the host `OSCachedToPhysical`
  returns the physical address as a `u32` (§1), so `reinterpret_cast<u32>(...)` of it is the
  identity, and `ResWriteCPCmd` writes it big-endian.
- **g3d-4** (PAT0 arrays): `PAT0_TEX_SLOT` / `PAT0_PLTT_SLOT` in `g3d_resanmtexpat.cpp` (§4).
  Not needed for Find Mii, but `ResFile::Bind` runs it for every PAT0.

`g3d_harness` checks every set-up file against an independent reading of the bytes and fails
on any difference:

- every changed byte lies in a slot that set-up writes (pre-primitive array commands, material
  texture/palette slots, `ResTexObjData`/`ResTlutObjData`, PAT0 arrays, SCN0 light-set ids);
- every CP command is `08 A0+n <base>` / `08 B0+n <stride>`, where `<base>` is the emulated
  physical address of the vertex array the shape names (4227 commands in 1340 shapes);
- every bound material slot holds the emulated address of a TEX0 / PLT0, and each `GXTexObj`
  holds that texture's image address (`image3`), width, height and format (`image0`, `fmt`),
  `userData` 0 and the mipmap flag, with the map marked used (665 texture objects); each
  `GXTlutObj` the palette address, format and size (no material on the disc names a palette:
  `namePltt` is 0 in all 665 texture infos, although 3 files carry 5 PLT0; so
  `GXInitTlutObj` is covered by `test_port` only);
- every bound PAT0 texture slot holds the emulated address of the texture its name array names
  (56 slots).

### Not covered yet

The `math::_VEC3`, `_VEC2` and `_MTX34` members of the file structs stay plain (§8), and
animation evaluation is not validated. Neither is on the load path.

## 12. ef resources (POINTERS.md step 6)

### What runs

`tests/ef_harness.cpp` registers every BREFF/BREFT pair on the disc with the real
`nw4r::ef::Resource` (`src/nw4r/ef/ef_resource.cpp`, `ef_res_emitter_ac.cpp`,
`ef_res_animcurve_ac.cpp`, linked with `src/host_reslist.cpp`), in the game's order:

1. Boot (`RPSysEffectMgr`, Party Pack): the cursor creator gets `nw4r_defcursor_pack`, then the
   static creator `basic_common`, both from `Common/PartyStatic/common.carc`. Each
   `RPSysEffectCreator::setEffectResource` is `Resource::Add`, `AddTexture`,
   `RelocateCommand`.
2. For every other archive with an `X.breff` / `X.breft` pair at its root (the 11 scene pairs:
   fishing, billiards, tank, ping-pong, cow, hockey, shooting, bomb, wanted, select menu,
   `pack_sequence`), the scene creator sets the pair (three projects in each list), the
   harness calls `setCaptureEfb`'s body for the three creators (ef-5), and
   `resetEffectResource` removes the pair (`RemoveEffectProject`, `RemoveTextureProject`,
   `RelocateCommand`) before the archive is unmounted.

The files are used in place inside the mounted archive, as
`RPSysResourceManager::GetFileFromArchive` returns them. `RelocateCommand` reports 0 errors at
all 24 stages: every particle texture name and every texture and child curve name resolves.
Over the 24 stages the harness reads back 4002 particle texture slots and 202 curve slots; 90
of those resolve into the file of another pair (`_FindTexture` and `_FindEmitter` search every
registered project, from the last one back), so the cross-file links are exercised.
`FindTexture("dummy_tex", project)` falls back to every registered BREFT, so during the
billiards scene all three creators write the capture buffer into `basic_billiards.breft`.

After each registration it prints, through the decompiled accessors, every emitter
(`_GetEmitterIndexOf`, `GetName`, `FindEmitter`, the track counts, `EmitterDesc` and
`ParticleParameterDesc` fields through `be_*`), every particle and emit track
(`GetPtclTrack`, `GetEmitTrack`, `ResEmitter::GetPtclTrack`, `AnimCurveHeader`), and every
texture (`FindTexture`, `TextureData`, the `texture`/`tlut` slots read back through
`RES_PTR`). After every `RelocateCommand` it prints the two project lists (walked with
`RES_LIST_*`), the emulated base address of each registered file, and every run of bytes that
differs from the disc file. It also reads every particle texture slot and curve `work` slot
back (the latter both as `be_u32` and through the `RES_PTR(TextureData)` view of
`ef_animcurve.cpp`) and requires the target `_FindTexture` / `_FindEmitter` returns.

`tests/test_effects.py` builds the same lines from `port/assets/tools/fmt_effect.py`
(read-only) and an independent model of the relocation, written from the format and the call
sequence: links (`+0x4` prev, `+0x8` next: big-endian emulated addresses of the neighbouring
project headers, also in the other files), name, track and texture/tlut slots, particle
texture and curve name resolution searching the registered projects from the last back,
unlinking, and the capture writes. Its predicted changed runs equal the harness's, so each
registered file is, at every stage, byte-identical to the Wii's in-memory copy of that file at
the same emulated address. The emulated base addresses come from the harness and are only
checked to lie in MEM1/MEM2. It also checks that the scene pairs found on the disc are exactly
the 11 the scene code passes to `setEffectResource`, and that all 13 BREFF and 13 BREFT were
registered.

One `fmt_effect.py` bug turned up: `_anim_name_table` unpacks the curve name table header as
(padding, count), but the count is the first `u16` (`AnimCurveNameTable`), so its name lists
are always empty (and `workOffsets` too). The test reads the name tables itself.

### Slots and types

| # | Slot | Host form |
|---|---|---|
| ef-1 | `EffectProject` / `TextureProject` `projectlink` (+0x4) | `RES_LINK`: two `RES_PTR(void)` (8 bytes, alignment 4). `Resource::mBREFFList` / `mBREFTList` are `RES_LIST` (a runtime list head with native pointers and the link offset). `port::res_list_*` in `src/host_reslist.cpp` is `ut_list.cpp` over these types, so `Add` writes the neighbour's link in the other file exactly as the Wii does. |
| ef-2 | `EmitterResource.name` | `RES_PTR(char)`; the store in `breffRelocate` casts to `RES_PTR(char)*`. |
| ef-3 | track pointer tables | `Get{Ptcl,Emit}TrackTbl` return `RES_PTR(u8)*`; `LocateTracks` stores with `PORT_STORE_PTR32` into `be_u32` and reads the size tables as `be_u32`. The stride stays 4. |
| ef-4 | `TextureData.name`, `.texture`, `.tlut` | `RES_PTR(...)`; `breftRelocate` is unchanged. |
| ef-5 | `setCaptureEfb` | Covered by ef-4: the assignment stores the heap buffer's emulated address. `RPSysEffectMgr.cpp` is not built on the host; the harness runs the same four assignments. |
| ef-6 | `ParticleParameterDesc.mTexture[3]` | `RES_PTR(TextureData)`; `RelocateCommand`'s `ppWork` is `RES_PTR(TextureData)*`. |
| ef-7 | curve `AnimCurveNameTable::Name::work` | `be_u32`, stored with `PORT_PTR_TO_U32`; `ef_animcurve.cpp` reads texture names through `AnimCurveTable<RES_PTR(TextureData)>` and child emitters through `PORT_U32_TO_PTR`. |

Everything else the resource code reads from the files is `be_*` (§4). `Effect::Modifier_*`
pass pointers through `ForEachParam`, which is `uintptr_t` on the host (POINTERS.md §5.2).
`ef_animcurve.cpp` (curve evaluation; an unlinked unit in the decomp, its CodeWarrior object
identical to the decomp's) is compiled on the host but not linked or run (`make` builds the
object).

`tests/test_ef.cpp` (`make unit`) checks `RES_LIST` on objects in the arena: the links hold
big-endian emulated addresses, bytes around them are untouched, and `Remove` rewrites the
neighbours; and the `TextureData` and track-slot stores.

### Not covered yet

Particle evaluation and drawing: the `math::VEC2`/`VEC3` members of the emitter and particle
descriptors and the curve key data that `ef_animcurve.cpp` reads are still plain, and the
draw strategies need the Phase 7 GX layer.

## 13. Sound archives (POINTERS.md step 7)

### Byte order: the wrapper, not `ReadBigEndian`

`nw4r::snd` has its own little-endian path, `Util::ReadBigEndian` under the misspelled
`NW4R_LITLE_ENDIAN`, but only 6 reads use it (`GetDataRefAddressImpl` and `SeqFileReader`);
every other reader (`SoundArchiveFileReader`, `BankFileReader`, `WsdFileReader`,
`WaveFileReader`) reads struct members directly. So the file structs use `be_*` (§4) and the
macro stays undefined: `ReadBigEndian` is then the identity on a value the wrapper has already
converted, and nothing is swapped twice. `Util::DataRef` and `Util::Table` are `be_*` too; their
one runtime user (`SoundArchivePlayer::GroupTable`) only goes through the wrapper.
`SoundArchive::FilePos` is `be_u32` because `ReadFilePos` copies it whole out of the file.
Payloads stay as they are: the `AdpcmInfo` block that `WaveFileReader` copies into
`ChannelParam` stays big-endian (DSP data for the Phase 7 mixer), and so do the samples and the
sequence (MML) bytes. `StrmFile.h` (BRSTM, never read on the disc path, ASSETS.md §2.6) is not
converted: its own members are still plain; only the `DataRef`/`Table` it uses are `be_*`.

### Pointer-size sites (POINTERS.md §5.2)

The channel callback data (`SeqTrack`, `WsdPlayer` → `Channel`), the sound-thread messages, the
aux-callback context and the AX voice user context are `uintptr_t` (§4); `AxVoice`'s DSP address
arithmetic takes the `u32` physical address. The `REFTYPE_ADDRESS` DataRef (an absolute
address; the disc only has NULL ones) decodes an M0 address. All of `src/nw4r/snd` compiles in
both host builds.

### What runs

`tests/snd_harness.cpp` links `libwiiplay_snd.a`: the whole snd library, the `ut` file
streams, `EGG::ArcPlayer`, `EGG::CntFile` and the SDK `MEM` frame heap behind `snd::FrameHeap`
(`mem_frameHeap.c`, `mem_heapCommon.c`, `mem_list.c`, compiled unmodified with
`-D__abs=__builtin_abs`), plus the stubs in `src/host_snd.cpp` (AX/AI no-ops with no voices,
threads never started, `OSSleepThread` aborts, NAND and channel content fail to open).

1. The 10 disc-level archives (`Sound/<Scene>/<name>.brsar`): `ArcPlayer::openArchive(path,
   heap, SARC_STORAGE_DVD)`, which is what `RPSndAudioMgr::openArchiveRP` calls for the default
   storage (`RPSysProjectLocal`: `EStorage_DVDStream`): `DvdSoundArchive::Open`, `LoadHeader`,
   `SoundArchivePlayer::GetRequiredMemSize` / `Setup` (sound players, player heaps, sound and
   track pools), `LoadLabelStringData`. The `SoundHeap` sits on M0 memory.
2. `HomeButtonSe.brsar` (inside `Common/PartyStatic/common.carc`): `ArcPlayer::setupMemoryArchive`
   on the mounted buffer, as `RPSysHomeMenuMgr` → `RPSndHomeMenuArcMgr::setupArchive` does.
3. Through `SoundArchive`: every sound (`ReadSoundInfo`, `GetSoundType`, the SEQ/WAVE/STRM
   info, 3D parameters, user parameter, label), bank, player, group with its items, file with
   its positions, and the player info. Names come from the SYMB patricia trees (walked with the
   file structs and `SoundArchiveFileReader::GetString`) and are checked against
   `GetSoundLabelString` and `ConvertLabelStringTo{Sound,Player,Group}Id`.
4. `ArcPlayer::loadGroup` for every group `0 .. GetGroupCount() - 1` (`SoundArchiveLoader`
   through the archive's file stream). Empty groups (size and wave size 0) are skipped: a
   0-length group stream means "the whole archive" to `DvdFileStream`/`MemoryFileStream`.
5. Every file through `SoundArchivePlayer::detail_GetFileAddress` /
   `detail_GetFileWaveDataAddress`: RSEQ (`SeqFileReader`), RBNK
   (`BankFileReader::ReadInstInfo` for every program, key and velocity, `ReadWaveParam` for
   every wave), RWSD (`ReadWaveSoundInfo`, `ReadWaveSoundNoteInfo`, `ReadWaveParam`), and the
   head of each file's wave data.

`ArcPlayer` is constructed in zeroed storage: its destructor closes the (never opened, for the
memory archive) `DvdSoundArchive`, and the host `DVDClose` needs a zeroed `DVDFileInfo`.

Result (the `-m32` build prints the same with pointer size 4; about 10 s):

```
snd_harness: 11 archives, 1124 sounds, 79 banks, 91 players, 24 groups (12 loaded), 273 files (RSEQ 184, RBNK 77, RWSD 8), 85 wave areas (0 RWAR), 87405 instrument lines, 1613 wave params, 23 notes; 0 errors (pointer size 8)
archives: 11 (RSAR); fmt_sound.py cross-checks: 1074, disagreements 0
lines: A 11/11, S 1124/1124, B 79/79, P 91/91, G 24/24, GI 277/277, F 273/273, L 13/13, X 273/273, Q 184/184, K 77/77, KI 87405/87405, KW 1591/1591, W 8/8, WS 23/23, WN 23/23, WW 22/22, R 85/85 (harness/python)
missing 0, extra 0
OK
```

`tests/test_sound.py` rebuilds every line from the bytes (INFO/SYMB, DataRefs, the bank region
walk, the sample arithmetic of `AxVoice::GetSampleByDspAddress`, the version defaults), and
cross-checks the archive-level fields and the embedded files' counts with `fmt_sound.py`
wherever that parses (it is being extended; its parse failures are printed as notes, grouped
by reason). `test_layout` checks the snd file struct layouts. Findings:

- The wave area of these v1.1 archives holds raw sample data, not RWAR containers; WaveInfo
  `dataOffset + channelDataOffset` index into it.
- `WaveInfo.channelInfoTableOffset` points straight at the channel offsets (no count), as
  `WaveFileReader` reads them; `fmt_sound.py` reads a count first and rejects most banks.
- `fmt_sound.py` reads a sound's SEQ `dataOffset` / WAVE `subNo` at SoundCommonInfo + 0x30
  instead of through `soundInfoRef` (wrong for every sound, so not compared), rejects every
  RSEQ (it requires one block; they have DATA and LABL), and rejects NULL (`refType` 0, value 0)
  instrument and graph-envelope DataRefs.
- One file of `rp_Wly_sound.brsar` is only in the last, unnamed group, which
  `GetGroupCount()` leaves out, so `detail_GetFileAddress` finds nothing for it (`X ... none`).
