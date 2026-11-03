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
- The sound runtime plays: sequences and wave sounds from the disc's archives run through the
  real `nw4r::snd` players, threads and AXFX effects on a software model of the AX DSP, and
  every voice's decoded samples, sample-rate conversion, envelope and mix are bit-exact against
  an independent model (§21). It is set up by the game's own `RPSndAudioMgr` on the real EGG
  heaps, and a BRSTM from the disc streams through the real `StrmPlayer` bit-exactly (§24).
- The game boots its memory and storage the way it does on the Wii (§22): the real EGG heap
  stack (`ExpHeap`, `FrmHeap`, `Allocator` over the decomp's MEM heaps) over MEM1/MEM2 arenas
  in M0, the decomp's DVD file system (`dvdfs.c`) on a host drive backed by the extracted
  disc and its `sys/fst.bin`, a NAND directory and an SC config file. On it the game's own
  `RPSysResourceManager` loads every scene's archives byte-identically to `wiiarc.py`, and
  `RPSysSaveDataMgr` writes `RPParty.dat` and `banner.bin` (big-endian, as on the Wii) on its
  NAND task thread and loads them back.
- Graphics run on Aurora (GX on WebGPU), headless: the game's own GX SDK is the command
  encoder; the health screen layout renders pixel-exactly through `nw4r::lyt` (§26), and Laser
  Hockey's 3D table is built, calculated and drawn by the real `nw4r::g3d` and EGG scene code
  (§27).
- The game boots on the host (§29): the decomp's own `main` runs `RPSysSystem`'s start-up and
  frame loop, the boot scene draws the health and safety screen (pixel-exact against a model of
  the layout), a scripted A press moves to the title screen, with sound, deterministically.

`POINTERS.md` steps 0 to 7 are implemented: M0 (emulated Wii addresses for all host heap memory),
`RES_PTR(T)` slots, `-fshort-wchar` with the MSL_C wide functions, ResFont (§5 to §7 below), TPL
and the layout resource structs (§9), the two hard-coded runtime offsets (§10), g3d with the
GX texture-object contract (§11), the `nw4r::ef` resources with `RES_LINK` (§12), and the
`nw4r::snd` callback cookies and sound archive readers (§13). Step 8 covers the game-owned
formats (§14): KCL and PMP through the game's loaders, the EGG binary settings through the
real EGG light, light-map and bloom objects, and the game tables.

## 1. Build and run

Requirements: GCC/G++ (tested with 15.2), GNU make and Python 3. The 32-bit cross-check also
needs gcc-multilib and g++-multilib. The 64-bit GX targets (`gx`, part of `make test`) also need
CMake, Ninja (optional), network access on the first build, the X11/Wayland/audio development
headers SDL3 builds against, and a Vulkan driver (lavapipe for the tests); `NO_GX=1` skips them
(§25).

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
anm_harness: 132 BRRES, 222 animations, 59010 results; 0 errors (pointer size 8)
test_anims: 132 BRRES; AMB 1134, CAM 399, CHR 54684, CLR 1008, LIT 1008, PAT 231, SRT 546; 0 errors
ef_harness: 206736 curve evaluations, 2286 child creation requests
test_efcurves: 206736 evaluations (U8 71154, F32 100536, Texture 3186, Child 15045, Rotate 16815), 2286 child requests; 0 errors
sys_harness: 5 heaps; FST 59 files 67 directories (sys/fst.bin and the tree agree); resources: 24 cached, 27 expanded, 384 archive files; save written, reloaded and reloaded by a second run; SC defaults and config, compared with host32
errors: 0
OK
test_input: 4 scripts x 2 modes, 5094 statuses checked
  checked: Nunchuk 232, acc_vertical 2934, both dots out of view 20, button edges 100, buttons 5070, cursor vs mouse 399, events 16, motor changes 36, one dot out of view 12, pitched away 164, pointing away 20, pos exact 2738, pos within the play radius 1940, repeat flags 70
  (the largest errors, see §23)
  compared with host32: 5234 lines identical
errors: 0
OK
```

(The other targets' summaries are in their sections. `sys_harness` counts 6 heaps on `-m32`,
where the assert heap fits, §22.)

The number of changed bytes depends on the emulated addresses the arena hands out, so it
differs slightly between the 64-bit and `-m32` builds (48690 there; 6774 changed runs for
`ef_harness`); everything else is the same.

| Target | What it runs |
|---|---|
| `unit` | `test_be`, `test_be_c` (wrapper), `test_port`, `test_port_c` (M0 arena, `RES_PTR`, wide functions, host `GXInitTexObj`/`GXInitTlutObj` words, the fast-cast and `nw4r::math` asm stand-ins of §17, C++ and C), `test_layout` (compile-time struct layout checks); also compiles the step 4 units (§10) |
| `archives` | `arc_harness` + `tests/test_archives.py` |
| `resources` | `res_harness` + `tests/test_resources.py` |
| `layouts` | `lyt_harness` + `tests/test_layouts.py` |
| `models` | `g3d_harness` + `tests/test_models.py` (§11) |
| `sound` | `snd_harness` + `tests/test_sound.py` (§13) |
| `effects` | `ef_harness` + `tests/test_effects.py` (§12); `test_ef` (`RES_LIST`, ef slots) runs with `unit` |
| `collision` | `coll_harness` + `tests/test_collision.py` (§14: KCL, PMP) |
| `data` | `data_harness` + `tests/test_data.py` (§14: `.plight`, `.plmap`, `.pblm`, game tables) |
| `anims` | `anm_harness` + `tests/test_anims.py` (§17: every g3d animation evaluated by the real readers) |
| `efcurves` | `ef_harness --curves` + `tests/test_efcurves.py` (§17: every BREFF curve through `ef_animcurve.cpp`) |
| `platform` | `sys_harness` + `tests/test_sys.py` (§22: EGG heaps, DVD, NAND / save, SC; saves go to `SYS_SCRATCH`, default `$TMPDIR/wiiplay-sys-host64` (or `host32`; `/tmp` without `TMPDIR`), outside the repository: the banner holds the disc's images) |
| `input` | `input_harness` + `tests/test_input.py` (§23: the host WPAD backend under the real KPAD and the game's controller code, scripted input from `tests/input/`) |

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
The DVD and EGG heap stand-ins below are weak: a program that links the platform layer of
§22 (`dvdfs.c` with `src/host_dvd.cpp`, the real `eggHeap.cpp` and friends) gets those.

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

Build flags (all objects are also built with `-ffunction-sections -fdata-sections`, so a
harness can link with `--gc-sections`, §14): `-DPORT_HOST=1 -DNONMATCHING -DREVOLUTION -DNDEBUG=1 -DVERSION_RHAE01
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
| `include/port/bemath.h` | **New** (§14). The Wii side of the math value types in file structs: `BE_MATH(T)` is `nw4r::math::T`. The host header makes it `port::be_math::T` (`_VEC2`, `VEC2`, `_VEC3`, `VEC3`, `_MTX34`, `MTX34`): `be_f32` members, the runtime type's size and alignment, conversion to and assignment from the runtime type. `BE_MATH_CAST(T, p)` is `static_cast<T>(p)` (the host keeps the big-endian pointer for the matrix overloads). |
| `include/Pack/RPParty/RPPartyCommon/RPKcl.h`, `src/.../RPKcl.cpp` | §14 (POINTERS.md §3.6): `RPKclPrism` and `RPKclHeader` members `be_*`, `BE_MATH(VEC3)` and `RES_PTR(...)`; `SetResource` reads the section offsets with `RES_PTR_RAW`; the octree words and prism lists are read as `be_s32` / `be_u16`, the positions and normals as `BE_MATH(VEC3)`, and `pBlocks` through `RES_PTR_GET`. Type tokens and macro wraps only; the query arguments stay `nw4r::math::VEC3`. |
| `include/Pack/RPParty/RPPartyCommon/RPResFile_2.h`, `RPPathFollower.h`, `src/.../RPResFile_3.cpp`, `RPResFile_4.cpp`, `RPPathFollower_2.cpp` | §14 (POINTERS.md §3.5): `RPResData`, `RPResEntry`, `RPPathRes` counts, flags and parameters `be_*`, the five pointer slots `RES_PTR(...)`; `Relocate` converts through `PORT_U32_TO_PTR` / `PORT_PTR_TO_U32` / `RES_PTR_RAW`; `FindNthEntryOfType` / `CountEntriesOfType` compare the entry type through `be_u32*`; `RPPathObject` reads the entry floats through `const be_f32*`. `RPPathPoint::pos` is still `EGG::Vector3f` (no PMP on the disc has points). |
| `include/nw4r/g3d/res/g3d_rescommon.h`, `g3d_resmdl.h`, `g3d_resnode.h`, `g3d_resvtx.h`, `g3d_resmat.h`, `src/nw4r/g3d/res/g3d_resmat.cpp`, `src/nw4r/g3d/g3d_calcworld.cpp`, `g3d_draw.cpp`, `g3d_state.cpp`, `include/nw4r/ef/ef_res_emitter.h`, `ef_particle.h` | §14: `g3d_rescommon.h` includes `<port/bemath.h>`; the math members of the file structs are `BE_MATH(...)`; the 8 matrix upcasts are `BE_MATH_CAST`; `ResNode::GetTranslate` returns by value under `#if defined(PORT_HOST)`. |
| `include/egg/core/eggIBinary.h` | §14: `BinHeader::size` is `be_u32`; `GetBinarySize` uses `sizeof(typename T::Bin)` under `#if defined(PORT_HOST)` (GCC needs the `typename`). |
| `include/egg/gfxe/eggLightManager.h`, `eggLightObject.h`, `eggLightTexture.h`, `eggDrawPathBloom.h`, `src/egg/gfxe/eggLightManager.cpp`, `eggLightTextureManager.cpp` | §14: the `BinData` structs (and `BinData0` / `BinData1`, `BlurParam`) have `be_*` and `BE_MATH(_VEC3)` members; the light-map texture count is read and written through `be_u16*`. `DrawPathBloom::mData` is a `BinData`, so the runtime copy reads through the wrapper too. Type tokens only. |
| `include/Pack/RPParty/RPPartyCommon/RPTnkMapData.h`, `RPBomScene/RPBomQuestionParam.h`, `RPWlyScene/RPWlyLevelTable.h`, `RPPnpScene/RPPnpLevelTable.h` | §14: the table record structs (`RPTnkMapData::Map`, `RPBomQuestionData` / `PatternData` / `LevelData` / `QuestionParamFile`, `RPWlyLevelVec3` / `LevelData`, `RPPnpLevelTable::LevelEntry` / `RallyEntry`) have `be_*` members. Type tokens only. |
| `include/revolution/OS/OSFastCast.h`, `include/nw4r/math/math_arithmetic.h`, `include/nw4r/g3d/platform/g3d_cpu.h`, `src/nw4r/math/math_types.cpp` | §17: the quantized load/store asm blocks (`__OSu8tof32` ... `__OSf32tos16`, `S7_8ToF32`, `S10_5ToF32`, `F32ToS10_5`), `FAbs`, `FInv`, `FSelect` and the `FSEL_MAX` / `FSEL_MIN` blocks are opened with `ASM_PS(name)` (each header defines `ASM_PS(name)` as `ASM` when it is not defined); `MTX34InvTranspose` (a CodeWarrior `asm` function) is under `#if !defined(PORT_HOST)`. |
| `include/nw4r/g3d/res/g3d_resanmcamera.h`, `g3d_resanmlight.h`, `g3d_resanmfog.h`, `src/nw4r/g3d/res/g3d_resanmshp.cpp` | §17: `projType`, `distFunc`, `spotFunc` and the fog `type` are `BE_ENUM(..., u32)`; the SHP0 index table is read as `be_u16`. |
| `src/nw4r/ef/ef_animcurve.cpp`, `include/nw4r/ef/ef_emitter.h`, `src/nw4r/ef/ef_emitter.cpp`, `ef_particlemanager.cpp`, `include/port/be.h` | §17: the curve key, range and table structs are `be_*`, the key reads are `be_u16*` / `be_f32*`; `EmitterInheritSetting::speed` is `be_s16`; the random seed's byte fold indexes `bytes[BE_BYTE(i, 4)]` (new macro, `i` on the Wii); the track-type word is read as `be_u16`. |

| `include/nw4r/ef/ef_res_animcurve.h`, `src/nw4r/ef/ef_particlemanager.cpp` | §18: the field parameter blocks (`FieldGravityParam`, `FieldRandomParam`, `FieldSpinParam`, `FieldMagnetParam`, `FieldNewtonParam`, `FieldVortexParam`) move from the `.cpp` into the header with `be_f32` / `be_u16` / `BE_MATH(VEC3)` members; `Calc`'s local copies of the last four are `NW4R_EF_FIELD_VALUE(T)` (the type itself on the Wii, a host-order `T##Value` on the host). |
| `include/nw4r/ut/ut_algorithm.h`, `src/nw4r/math/math_types.cpp`, `math_arithmetic.cpp`, `src/nw4r/g3d/platform/g3d_cpu.cpp`, `g3d_fog.cpp`, `g3d_calcview.cpp`, `g3d_calcworld.cpp`, `g3d_calcvtx.cpp`, `src/revolution/MTX/vec.c`, `quat.c`, `mtx.c`, `mtxvec.c`, `src/Pack/RPParty/RPTnkScene/RPTnkBlock.cpp`, `RPHkyScene/RPHkyPaddle.cpp` | §18: every remaining paired-single / asm block is opened with `ASM_PS(name)` (files that do not get the fallback from a header define `ASM_PS(name)` as `ASM` themselves); the CodeWarrior `asm` functions (`MTX34InvTranspose`, `GetModelLocalAxisY2/3`, `PSMTXIdentity`, `PSMTXCopy`, `PSMTXConcat`, `PSMTXConcatArray`, `PSMTXMultVec`, `PSVECAdd`, `PSVECDotProduct`, `PSVECCrossProduct`) get a host body under `#if defined(PORT_HOST)`. |
| `src/Pack/RPGraphics/RPGrpModelRfl.cpp`, `src/Pack/RPSystem/RPSysPauseMgr.cpp`, `src/Pack/RPParty/RPCowScene/RPCowKakasiGroup.cpp` | §18: the `asm volatile { }` scheduling blocks have their C under `#if defined(PORT_HOST)`; `RPCowKakasiGroup::OnActivate`'s asm body is `#if !defined(PORT_HOST)` and its C++ equivalent (formerly `#if 0`) is the host form. |
| `include/nw4r/g3d/res/g3d_resmdl.h`, `src/nw4r/g3d/g3d_workmem.cpp` | §20: `ResMdlInfo::GetNodeIDFromMtxID` reads the matrix-to-node table as `be_s32`; the work-memory accessors take `&sTemp.x[0]` (GCC rejects `static_cast` from a base-class array to a derived pointer). Type tokens / the same address. |
| `src/nw4r/snd/snd_WaveFile.cpp`, `snd_AxManager.cpp`, `src/revolution/AX/AXVPB.c` | §21: `ReadWaveParam` copies the file's `AdpcmInfo` into the runtime `ChannelParam` through `be_u16` under `#if PORT_BE_WRAPPED` (the Wii line is the original struct copy); `AxManager::Init` puts the DSP's zero buffer on the M0 arena under `#if defined(PORT_HOST)`; `AXSetVoiceAddr`'s `gain` / `pred_scale` word is `GAIN_WORD(0x0800)` / `GAIN_WORD(0x0100)` (Wii: `0x08000000` / `0x01000000`, the same constants). |
| `src/revolution/DVD/dvdfs.c`, `src/egg/core/eggStream.cpp`, `src/Pack/RPSystem/RPSysSaveData.cpp`, `include/revolution/NAND/nand.h`, `src/Pack/RPKernel/RPSysSystem.cpp`, `include/port/be.h` | §22: `entryToPath` reads the name through `DVDNodeGetName` (the bitfield is little-endian-ordered on the host); `EGG::Stream`'s binary reads go through `be_*` locals and its writes through `BE_STORED(T, value)` (new macro, `(v)` on the Wii); the save checksum reads `be_u16`; `NANDBanner`'s `magic`, `flags`, `iconSpeed` are `be_u32` / `be_u16`; `RPSysSystem::initialize` rounds through `uintptr_t` instead of `u32` (64-bit compile only; on the Wii both are `unsigned long`). |
| `include/port/wgpipe.h` | **New** (§26). The Wii side of the write-gather pipe: `WGPIPE_WRITE(m, v)` is `(WGPIPE.m = (v))`, `PORT_GP_STATIC(p, size)` is `(p)`. |
| `include/revolution/GX/GXHardware.h`, `GXVert.h`, `GXDisplayList.h`, `src/revolution/GX/GXAttr.c`, `GXDisplayList.c`, `GXGeometry.c`, `GXInit.c`, `GXTransform.c` | §26: every `WGPIPE.x = y;` is `WGPIPE_WRITE(x, y);` (`GXHardware.h` includes `<port/wgpipe.h>`). |
| `src/revolution/GX/GXTransform.c`, `GXLight.c` | §26: the write-gather asm blocks are `ASM_VOLATILE_PS(name)` (`ASM_VOLATILE` on the Wii). |
| `include/revolution/GX/GXInternal.h`, `src/revolution/GX/GXTexture.c` | §26: `GXTexObjImpl`/`GXTlutObjImpl` members `be_*`, `BE_ENUM`, `RES_PTR(void)`; field accesses `BE_GET`/`BE_SET`; `GX_PHY_ADDR` through `PORT_PTR_TO_U32`; `GXGetTexObjAll`'s host form. |
| `src/revolution/GX/GXTev.c`, `GXGeometry.c`, `GXLight.c`, `GXPixel.c`, `include/nw4r/g3d/platform/g3d_gpu.h` | §26: colours read or stored as one word through `BE_GET`/`BE_SET(*(be_u32*)&...)`; `GXSetTevColorS10`'s host form. |
| `src/revolution/GX/GXAttr.c`, `GXFrameBuf.c`, `GXInit.c`, `GXGeometry.c` | §26: `PORT_PTR_TO_U32` for `GXSetArray`'s base and `GX_PHY_ADDR`; `GXCopyDisp` ends with `PortGXDisplayCopied` (host only); `PORT_GP_STATIC` for `GXInit`'s static texture and array base and host-only prototypes; `__GXSetGenMode`'s host form. |
| `include/port/wgpipe.h`, `src/egg/gfxe/eggStateGX.cpp`, `eggDrawGX.cpp`, `eggLightTexture.cpp`, `eggLightManager.cpp`, `src/nw4r/g3d/g3d_draw.cpp` | §27: `PORT_GP_STATIC_BE16` / `PORT_GP_STATIC_BE32` / `PORT_GP_LIGHT_OBJS` (`(p)` on the Wii) for static GP data and the light-object array; `DrawResMdlDirectly` hands the GP big-endian copies of the view matrices under `#if defined(PORT_HOST)`. |
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

- g3d beyond loading (§11): the model file structs' math members are `BE_MATH` since step 8
  (§14), and every animation on the disc evaluates through the real `ResAnm*::GetAnmResult`
  readers since §17. Still open: the `AnmObj*` / `AnmScn*` objects (`g3d_anm*.cpp`) and drawing.
- Drawing layouts needs the Phase 7 GX layer behind `src/host_gx.cpp`. Every paired-single
  block and asm function the host builds or will build has a host form since §18 (table
  there), and the RVL MTX library is built from `src/revolution/MTX` plus
  `src/host_mtx.cpp`.
- Move the next loaders onto `be_*` as they are brought up. Each needs the type declarations, a
  small number of C `BE_GET`s, a `layout_asserts.cpp` block and a `check_matching.py` run:
  - `g3d::Res*Data`
  - `snd` file structs
- ef beyond loading (§12): the `math::VEC2`/`VEC3` members of `EmitterDesc`,
  `ParticleParameterDesc` and `Lighting` are `BE_MATH` since step 8 (§14), and every curve
  evaluates through the real `ef_animcurve.cpp` since §17. Still open: the field parameter
  blocks after a curve's tables (`FieldGravityParam`, `FieldRandomParam`, ... in
  `ef_particlemanager.cpp`) are plain, `Emitter` / `ParticleManager` / `Particle` calculation
  is not built on the host, and drawing needs Phase 7.
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

The `math::_VEC3`, `_VEC2` and `_MTX34` members of the file structs are `BE_MATH` since §14,
and animation evaluation is validated since §17. The `AnmObj*` classes and drawing are not.

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

Particle evaluation and drawing. The descriptor math members are `BE_MATH` (§14) and the
curves evaluate correctly (§17), but `Emitter` / `ParticleManager` / `Particle` calculation is
not built on the host, the field parameter blocks are plain, and the draw strategies need the
Phase 7 GX layer.

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
Payloads stay as they are: the samples and the sequence (MML) bytes. The `AdpcmInfo` block
that `WaveFileReader` copies into `ChannelParam` was left big-endian here; since §21 the copy
converts it to host order (the runtime computes with it: `CalcOffsetAdpcmParam`, `SetAdpcm`). `StrmFile.h` (BRSTM, never read on the disc path, ASSETS.md §2.6) is not
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

## 14. Game-owned formats (POINTERS.md step 8)

KCL, PMP, the EGG binary settings and the game tables now load through the game's own code on
the host. Their file structs use `be_*`, `BE_MATH(T)` (new: `include/port/bemath.h`, a struct
of `be_f32` with the layout of `nw4r::math::T` on the host) and `RES_PTR` (§4). Nothing is
swapped in place. `check_matching.py` lists the step 8 files at the end of `TOUCHED`. With
them, all 750 units that depend on a touched file are byte-identical (2026-10-30).

### What runs

`tests/coll_harness.cpp` (`make collision`):

- **KCL (7 files; header size 0x38 in 2, 0x3C in 5).** `RPKcl::SetResource` runs on each file
  in place, then `CheckPoint` and `SearchSphereOctree` / `CheckSphereTri` run at a point inside
  every prism (7959 queries).
  - The harness prints the header copy, every prism with its position and normals, and every
    hit.
  - `tests/test_collision.py` takes the values from `fmt_collision.parse_kcl`. It models both
    queries in single precision, rounding each float operation to f32, and walks the octree
    from the bytes. All hits and distances match bit for bit.
  - On 0x38-byte files `sphereRadius` is `positions[0].x`, as on the Wii (ASSETS.md §2.8).
  - The file is unchanged, and a second `SetResource` does nothing.
- **PMP (7 files, 1744 entries).** The real `RPResTable` constructor runs (`RPResFile::Create`,
  `Relocate`), then `CountEntriesOfType` / `FindNthEntryOfType` for the 11 Cow types, then
  `RPPathObject` on every entry found.
  - Every changed byte is predicted, so each file is the Wii's in-memory image: flag bit 0, the
    section slots, and NULL path slots.
  - `Relocate` only fixes the entries that `RPResFile`'s filter (0) returns, those with type
    byte 0. On the disc that is all of them.
  - Positions, `vec20`, radius and parameters match bit for bit. The rotation (`calcRPY`, libm
    `atan2f` / `asinf`) matches within 1e-5.
  - No PMP on the disc has paths or points, so the harness also relocates a PMP built in memory
    with 2 paths and 4 points. It checks every slot and `RP_getPathPoint`.

`tests/data_harness.cpp` (`make data`):

- **Settings files.** Each file is loaded with `SetBinary` into the real object the game uses:
  - `.plight` (17, version 1): `EGG::LightManager(16, 8, 1)`, as `RPGrpModelScene` creates it.
  - `.plmap` (11): the manager's `LightTextureManager`. Its textures are made with
    `createTexture` for the names in the file, because the game makes them from its scene
    models.
  - `.pblm` (4, versions 0 and 1): `EGG::DrawPathBloom`.

  Each object is then written back with the real `GetBinary`, and that image is read through
  the file structs. The runtime getters (`GetIntensity`, `GetAt`) are printed too.
  `tests/test_data.py` compares the result with `fmt_game_data.py`.
- **Tables.**
  - The real `RPTnkMapData` constructor runs (120 maps). Every cell is read through
    `RP_TnkMapData_getCell` in both TV modes, plus `GetUnk08` / `GetUnk0C`.
  - `RPBomQuestionParam::Load` runs (`BuildLevelTable`), and every record word is read through
    `RPBomQuestionParamFile`.
  - `RPWlyLevelTable` and `RPPnpLevelTable` are copied as their constructors do and read through
    the real record structs. Their units include RPAudio headers, see below. (Now their real
    constructors run, §15.)
- **Results.** Every value matches as a 32-bit pattern (64- and 32-bit):

  ```
  coll_harness: 7 KCL (7959 prisms, 7959 point queries: 16715 point hits, 32065 sphere hits), 7 PMP (1744 entries found by type, 1744 path objects, 1772 changed runs); 0 errors
  data_harness: 17 .plight (272 lights), 11 .plmap (22 light textures), 4 .pblm; tables: 120 Tnk maps, 1 BomQuestionParam, 179 Wly levels, 331 Pnp records; 0 errors
  ```

### Host pieces

- **`tests/shadow/Pack/RPSystem/RPSysResourceManager.h`** (removed in §15; the real header
  builds now). The loader units call
  `LoadGameCommonArchive` / `GetFileFromArchive`. The real header pulls in RPAudio headers
  that do not build on the host, so the step 8 Pack units are compiled with this stand-in
  first on the include path. It has the same two declarations, and each harness defines them
  over the archive the disc walk has mounted.
- **`src/host_gfx_stubs.c`.** The EGG light / light-map / bloom objects are built from all of
  `src/egg/gfxe`, `gfx` and `math`. Their vtables reference drawing code, so `data_harness`
  links with `--gc-sections`. The 30 GX, g3d camera and game functions that remain are stubs
  that abort with their name (four data symbols are zero); nothing on the load path calls them. The exception is
  `PSVECMag`, which is written in C here because the Wii version is paired-single code.
- **`GXGetTexBufferSize`** (pure arithmetic, `src/host_gxtexobj.cpp`) and
  `EGG::Heap::sCurrentHeap` (NULL, `src/host_sdk.cpp`) are new host stand-ins.
- **Other harness-side definitions.** `RP_DEBUG_STUB_1`'s empty constructor and destructor, and
  the CodeWarrior-ABI entry `calcRPY__Q23EGG9Matrix34fCFv` that `RPPathFollower_2.cpp` calls.
- **`-m32`.** On `-m32` the KCL units and `coll_harness` use `-msse2 -mfpmath=sse`, so the
  queries round like x86-64 instead of in x87 extended precision.
- **`be<T>`** gained `*=` and `/=` (the Wly loader scales `mSpeed`).
- **Parser fix.** `fmt_game_data.py` read the PBLM gather scales at file offset 0x8C instead
  of 0x9C (`BinData` + 0x8C), so they were always 0.

### Math value members of the g3d and ef file structs

The `math::_VEC2` / `_VEC3` / `_MTX34` members of these file structs are now `BE_MATH(...)`:

- g3d: `ResMdlInfoData` (volume), `ResNodeData` (scale, rot, translate, volume, model and
  inverse model matrices), `ResVtxPosData` / `ResVtxTexCoordData` (min, max) and
  `TexMtxEffect::effectMtx`.
- ef: `EmitterDesc` (velSpecDir, scale, rotate, translate), `Lighting::mPosition`, and
  `ParticleParameterDesc` (size, scale, rotate, textureScale, textureTranslate).

Reading a member converts it; most code compiles unchanged. A few code changes were needed:

- **Matrix pointers.** The g3d code passes the file's matrices to the math functions as
  `static_cast<math::MTX34*>(&data.effectMtx)` (resmat, calcworld, draw, state: 8 sites).
  Those casts are now `BE_MATH_CAST(T, p)`, which is the same cast on the Wii. On the host the
  pointer keeps its big-endian type, and `port/host/include/port/bemath.h` overloads
  `MTX34Copy` / `MTX34Identity` / `MTX34Mult` for it (they convert, call the runtime function
  and store the result back).
- **`ResNode::GetTranslate`.** On the host it returns the `VEC3` by value under
  `#if defined(PORT_HOST)`, because a reference would be to a converted temporary.

Every `nw4r/g3d` and `nw4r/ef` unit still compiles on the host, except four that already
failed before this change for other reasons: `g3d_calcview` (assembly), `g3d_workmem`,
`ef_effect` and `ef_drawstrategyimpl`.

Tests:

- **`g3d_harness`** prints the members of every model, node, vertex array and material
  texture slot through the wrapper. The effect matrices go through the real
  `ResTexSrt::GetEffectMtx`. That is 177 + 1539 + 631 + 444 + 3200 lines.
  `tests/test_models.py` checks every value against the big-endian floats at the struct
  offset the harness reports, and the line counts against the model's counts.
- **`ef_harness`** prints the emitter and particle members (`EV`, `PV`, 373 each), and
  `tests/test_effects.py` builds them from the bytes.

All match on both builds. The decomp header comment for `ResVtxTexCoordData::max` said 0x2C;
the member is at 0x28 (fixed).

### Not covered yet

- **Duc parameters (6 files) and `TnkGameParam` (1).** Done in §15. Their structs are declared in units and
  headers that include the RPAudio headers. Those do not build on the host yet: the
  `unsigned int` / `u32` virtual overload pairs in `RPSndObject.h` / `RPSndAudioMgr.h` are the
  same type on the host, there are dependent-base names in templates, and `RPSysFile::PATH_MAX`
  clashes with `<limits.h>`. Their structs are still plain. The Wly and Pnp loader units are
  blocked the same way: their structs are converted and tested, but the units do not compile on
  the host. `RPWlyLevelTable::sDefaultLevel`'s aggregate initialiser will need `be<T>` values
  when they do.
- **`.pmmisc` (2).** `RPGrpModelMisc::SetBinaryInner` is empty in Wii Play, so loading reads
  nothing past the magic. Its `BinData` is unchanged.
- **Other gaps.**
  - `RPPathPoint::pos` (no points on the disc).
  - `RPCollisionModel` (compiled, not linked: it needs `RPBBox` and the RP matrix helpers).
  - Light-object state that only drawing uses (`Calc` results).

## 15. RPAudio headers on the host; Duc, Tnk, Wly and Pnp loaders

The RPAudio headers now build on the host, so the real `RPSysResourceManager.h` does too, and
the test-only stand-in `tests/shadow/` is gone. Every harness compiles against the real header.
The Shooting Range parameter files, `TnkGameParam` and the Wly / Pnp level tables load through
the game's code. All 786 units that depend on a touched file are byte-identical (2026-10-30).

### Header fixes

None of these changes the Wii build.

- **`ui32`** (`include/Pack/types_pack.h`).
  - On the Wii, `u32` is `unsigned long` and the RPAudio sound actors have virtual overload pairs
    that take a sound ID as `u32` or as `unsigned int`. On the host both are `unsigned int`, so
    the pairs collided.
  - The `unsigned int` member of each pair is now spelled `ui32`. That is `unsigned int` on the
    Wii and `unsigned long` on the host, so the pairs and their vtable slots stay.
  - Where: `RPSndAudioActorBase`, `TRPSndObject` and `RPSndObject2D` (start / hold / prepare /
    stop / `getSoundId`), `RPSndAudioMgr::startSound`, and their definitions in
    `RPSndAudioMgr.cpp` and `RPWlySound_2.cpp`.
  - On the host, a call with a `u32` or `unsigned int` argument picks the `u32` overload. Each
    pair has the same body.
  - Overloads that are not such pairs are unchanged, for example `loadGroup(unsigned int)` /
    `loadGroup(int)` and `startBgm(unsigned int)`.
  - The typedef is not in `include/types.h` because of the precompiled header (see below).
- **Dependent bases.** The class templates name `mHandleCount` (`TRPSndAudioActorBaseWithHandles`,
  `TRPSndObject`) and `smCommon3DManager` (`TRPSndAudio3DActor`) without `this->`, inside
  `RP_ASSERT`.
  - `RP_ASSERT` stringizes its expression, and those strings are in the binary, so `this->`
    would change `.data`.
  - Instead, each template has a `using Base::member;` declaration.
- **`PATH_MAX`.** `RPSysFile` and `RPSndAudioMgr` have static members of that name. The host
  `types.h` includes `<limits.h>` / `<climits>` (its guard keeps it from coming back), then
  `#undef PATH_MAX`.
- **Other headers.**
  - `RPSysParticleManager`'s destructor loses `override`. The base destructor
    (`ef::ParticleManager`) is not virtual, and `decomp/lang.h` defines `override` away for
    CodeWarrior.
  - `ef::MemoryManagerImpl` rounds its heap start through `reinterpret_cast<uintptr_t>`
    instead of `<u32>`. MSL's `uintptr_t` is `unsigned long`, so the code is the same.
  - On the host, `ut::RoundUp(T, ...)` now widens its mask to `T`, so a 64-bit address keeps
    its upper half.
- **Host `decomp/compat.h`.**
  - `__declspec(x)` becomes `PORT_DECLSPEC_x`. Only `novtable` is defined (empty), so any other
    use, such as `section ".ctors"`, fails to compile instead of being dropped.
  - `__fmadds`, `__fmsubs`, `__fnmadds` and `__fnmsubs` are single-precision `fmaf`, rounded
    once as on Gekko. The Duc units use them.

### `check_matching.py` and the precompiled header

The Pack units are compiled with `-prefix SportsPack.mch`, which is built from
`include/SportsPack.pch` → `nw4r/ut/ut_algorithm.h` → `nw4r/types_nw4r.h` → `types.h`.

`check_matching.py` uses the decomp checkout's `.mch`, so it never reads this repository's
copies of those headers. Their include guards are already set. That is why `ui32` is in
`types_pack.h`; a first version in `types.h` failed to compile there.

The `ut_algorithm.h` change is inside `#if defined(PORT_HOST)`, so the stale `.mch` does not
matter for it. A future change to the non-host text of these headers needs a rebuilt `.mch`.

### What runs

All of these are in `tests/data_harness.cpp` (`make data`), checked by `tests/test_data.py`.

- **Duc parameters (6 files, each in both TV modes).**
  - `RPDucCircleTarget`, `RPDucDuck` and `RPDucUfo::LoadParams` run for real. They triple the
    frame counts, and `RPDucDuck` scales two x ranges by `0.75f` on 4:3.
  - `RPDucBalloon`, `RPDucCan` and `RPDucClay` load their file in the first statement of `Load`,
    which goes on to build models. The harness does that statement (the struct copy) itself.
    Their units are compiled only.
  - The harness prints each struct after loading as words (`DP`), plus `mBaseRadius` and every
    pattern's hit scale read through the member names (`DN`).
  - The structs are `be_*`. `mParam` is a copy of the file, so the object holds big-endian
    values and every read converts.
- **`TnkGameParam`.**
  - `RPTnkGameManager::Load` runs with the real `RPTnkEmptyTask`. Then `PickStageVariant` runs
    for all 100 stages in both modes, with fixed `RP_randSeedX` / `RP_randSeedY` (`TP`).
  - `PickStageVariant` reads each stage record's variant range and draws with `TnkGameRandom`.
  - The views in `RPTnkGameManager.cpp` (`TnkGameParam`, `TnkStageParam`) and in
    `RPTnkStageMap.cpp` are `be_*`. `RPTnkStageMap` is compiled only.
- **Level tables.**
  - The real `RPWlyLevelTable` constructor runs in both TV modes (`mSpeed * 1.3684211f` on
    16:9, `unk20 = 6.25f` on field type 3), and again with `sUseDefaultLevel` set.
  - `sDefaultLevel`'s initialiser spells each value as `be_s32(8)`, `be_f32(-130.0f)` and so
    on, because the host wrapper's constructor is explicit. On the Wii these are plain casts.
  - The real `RPPnpLevelTable` constructor runs, and its levels are read through
    `GetLevelEntry`.
- **How the harness calls these objects.**
  - `LoadParams` and the Tnk calls run on zeroed storage of the object, without the
    constructor. A constructor would set the vtable, and the vtable pulls in every virtual
    function and the scene code behind it. These calls only touch the members the harness
    prints.
  - Private members are read through explicit template instantiation of a member pointer
    (`PRIVATE_MEMBER`); the classes are unchanged.
  - The Tnk test sets the two members `ResetForMode` would set. `ResetForMode` itself also
    resets the stats, guide and BGM objects, which the harness does not create.
  - `Wly_fieldType3Unk20` is defined in the harness (6.25f, as in `RPWlyAvatarMgr_2`).
- **Results.** Both builds match:

  ```
  data_harness: ... tables: 120 Tnk maps, 1 BomQuestionParam, 716 Wly levels, 331 Pnp records, 12 Duc params, 1 TnkGameParam; 0 errors
  lines: ... WL 716/716, PL 331/331, DP 12/12, DN 12/12, TP 2/2 (harness/python)
  ```

### Parser fixes (`port/assets/tools/fmt_game_data.py`)

- **`DucCircleMatoParam`.** The schema listed all eight patterns' hit scales and radii first,
  then the grow frames, then the stand frames. In the struct (`RPDucCircleTargetPattern`, 0x1C
  bytes) each pattern holds its own seven words. The `DN` line, read through the member names,
  showed this.
- **`leveltable.bin`.** `unk8` was decoded as `f32`. All three words are `u32` weights, summed
  as `u32` by `PnpPlayerPickTargetIndex` (`RPPnpPlayer.cpp:376-379`). The struct already said
  `u32`. `port/assets/formats/game_data.md` is corrected too (and its `RPWlyLevelData` field
  list, which left out `unk4`).

### Build note

Objects compiled before this change list `tests/shadow/...` in their `.d` files. Make stops
with "No rule to make target". Delete those `.d` files and their objects, or `make clean`.

### Not covered yet

- **`TnkGameParam`'s enemy records** (10 × 0xA8 bytes at 0x4). The game reads them through
  three more views: `TnkEnemyParamsData` (`RPTnkTank.cpp`), `AttackEnemyParamsData`
  (`RPTnkAi_4.cpp`) and `RPTnkPlayerParams` (`RPTnkPlayerController.cpp`).
  - Those views are still plain types.
  - `fmt_game_data.parse_tnk_game_param` does not decode the records either.
  - `lives` and the stage bonus are read only by `SetupFirstStage` and a state function,
    which the harness does not run.
- **The full `Load` of Balloon, Can and Clay** (models). `RPDucCan::AdjustPatterns` is inline
  in `Load` and does not run.

## 16. BTI, `RFL_Res.dat`, HOME Menu; the last step 8 pointer sites

The remaining formats now load through the game's own code on the host: BTI textures through
`EGG::GXUtility::getTexObj`, `RFL_Res.dat` through the whole of RVLFaceLib (`src/RVLFaceLib`,
all decompiled and matching), and the HOME Menu files through homebuttonMiniLib's `HBMCreate`.
The step 8 pointer sites that were still design (POINTERS.md §5.1, §5.2) are done. As before,
nothing is swapped in place except where noted, and `check_matching.py` lists the files at the
end of `TOUCHED`: all 797 units that depend on a touched file are byte-identical (2026-10-30).

### Source changes

- **BTI.** `EGG::ResTIMG` (`include/egg/gfx/eggResTIMG.h`): `width`, `height`,
  `paletteEntryNum`, `paletteOffset`, `LODBias` and `imageOffset` are `be_*`. `getTexObj`,
  `CpuTexture::initResTIMG` (which writes a header for its own image) and the other readers
  compile unchanged.
- **`RFL_Res.dat`.**
  - `RFL_NANDLoader.c`: every header and offset-table word is read as
    `BE_GET(*(be_u32*)p)` / `be_u16`. That covers the cached path the game uses
    (`parseOnmemoryRes_`, `getCachedLength_`, `getCachedFile_`) and the NAND path.
  - `RFLiTexture` (`RFLi_Texture.h`): the `u16` / `u32` / `s16` fields are `be_*`. Its readers
    use `BE_GET`: `RFLiGetTexImage`, `RFLiInitRFLTexture` (`RFL_MakeTex.c`), `RFLiInitTexRes`.
  - `RFLiInitShapeRes` (`RFL_Model.c`): the vertex counts are `be_u16`, the vertex data
    `be_s16`. The faceline's three `Vec`s are loaded with `RFLi_LOAD_VEC` (a `memcpy` on the
    Wii, three `be_f32` reads on the host).
  - The vertex arrays RFL builds (`RFLiCharModelRes`, `RFLiShapeRes`) are GX data. Like the
    BRRES vertex arrays, they stay big-endian on every platform, so they are `be_s16`, and
    `RFLiTransformCoordinate` reads and writes them with `BE_GET` / `BE_SET`. The texcoords are
    copied from the file unchanged, as before.
- **HOME Menu.**
  - `home_nosave.csv` is UTF-16 with a byte-order mark, big-endian on the disc.
    `HomeButton::set_text` stores pointers into it and hands them to `lyt::TextBox`. On the
    host it first converts the buffer to native order in place, up to the terminating 0 (the
    archive padding after the file), if the mark reads `0xFFFE`. A second `HBMCreate` on the
    same buffer leaves it alone. `#if defined(PORT_HOST)` in `HBMBase.cpp`.
  - `SpeakerSe.arc`'s `.bwav` files are big-endian 16-bit PCM. `RemoteSpk`'s PCM pointer is
    `be_s16`, and `MakeVolumeData` reads it with `BE_GET` (`HBMRemoteSpk.h` / `.cpp`).
  - `config.txt` is ASCII. The layout archives are covered by §9 (`nw4r::lyt`).
- **Cookies (POINTERS.md §5.2).**
  - RFL: `RFLiExCallback` takes a `uintptr_t`. So do `RFLiLoadHiddenDataAsync` and
    `RFLiHDBManager::loadArg`, and the callbacks that receive it (`RFL_MiddleDatabase.c`
    passes `(uintptr_t)db`; `RFL_Database.c`, `RFL_Format.c` and `RFL_NANDAccess.c` take the
    same parameter type). On the Wii `uintptr_t` is `unsigned long`, the same type as `u32`.
  - HBM alarm user data: `reinterpret_cast<uintptr_t>` / `<intptr_t>` (`HBMBase.cpp`,
    `HBMController.cpp`, `HBMRemoteSpk.cpp`). `HBMAllocMem` takes a `size_t` (the
    `AXFXSetHooks` type; `unsigned long` on the Wii, like `u32`).
- **Opaque RFL structs.** `RFLCharModel` (0x88) and `RFLMiddleDB` (0x18) are byte arrays that
  RFL casts to `RFLiCharModel` / `RFLiMiddleDB`. Those hold pointers and are larger on a
  64-bit host. Under `PORT_HOST` the public structs declare the same members; a
  `_Static_assert` in `RFL_Model.c` / `RFL_MiddleDatabase.c` checks the size.
- **POINTERS.md §5.1.**
  - `RPPolyline::Calc`: under `PORT_HOST` the control-point walk adds the offset to a
    `const u8*`, and `mPointNum` is read as a member instead of by the inline `lwz`.
  - `RPKokeshiMgr_3`'s `SetScreenFlags` names `EGG::Frustum::mFlags` (protected) through a
    pointer to member taken in a derived class, instead of writing byte 0x34. A public setter
    in `eggFrustum.h` would change the matching objects of every unit that includes it.
- **`nw4r::math` paired-single inlines.** `VEC3Add`, `VEC3Sub`, `VEC3Scale`, `VEC3Lerp`,
  `VEC3Dot` and `VEC3LenSq` (`math_types.h`) are inline asm only. On the host they were
  no-ops: `ASM(...)` expands to nothing, so `VEC3` `+ - *` did nothing, and `Dot` / `LenSq`
  returned an uninitialised value.
  - Each asm block is now opened with `ASM_PS(name)`. On the Wii, `ASM_PS(name)` is `ASM`
    (defined at the top of `math_types.h`).
  - The Wii definition is in `math_types.h`, not in `decomp/compat.h`: the game units'
    precompiled header (`SportsPack.pch`) includes `compat.h`, and `check_matching.py`
    compiles them with the decomp checkout's copy of it.
  - On the host, `port/host/include/decomp/compat.h` turns `ASM_PS(name) ( asm )` into the
    same operations in C on the function's own parameters, and drops the asm text. The fused
    multiply-adds use `__fmadds`.
  - `RPPolyline` needs this (`VEC3Lerp`). Every earlier harness gives the same results with
    it, on both builds.
- **Host headers.**
  - `port/host/include/types.h` wraps its C++ standard headers in `extern "C++"`, and the
    host `be.h` its C++ part, because `HBMTypes.h` and RVLFaceLib include them inside
    `extern "C"`.

### What runs

`tests/bti_harness.cpp` (`make bti`):

- **All 15 `.bti` files.** Each goes through `RPGrpTexture(const ResTIMG*)` (as
  `RPSysKokeshiIcon::SetupModel` does for `lm_0` / `lm_1`), `GXUtility::getTexObj` and
  `DrawGX::LoadTexture`. The harness checks that all three give the same `GXTexObj` and that
  the header is unchanged.
- **Tanks.** `Common/RPTnkScene/common.carc`'s `RPTnkFootmarkTex::Load` (`RPTnkTask_3.cpp`)
  runs while the archive is mounted, and its 11 textures go through `getTexObj`.
- **Test.** `tests/test_bti.py` builds every `GXTexObj` word from the header
  (`fmt_text_font.parse_bti`). It uses a Python model of `GXInitTexObj` / `GXInitTexObjLOD`
  (`gx_tex_obj`, after `GXTexture.c`, which `test_rfl.py` shares). It also decodes every image
  with `gxtex.py` at the offset, size and format the `GXTexObj` holds.
- **`ball_face.bti`** (Billiards) is not named anywhere in the DOL. Billiards uses the Mii
  icon texture (`RPSysKokeshiIcon::GetResTIMG`), so the file is walked like the others but the
  game never loads it.

`tests/rfl_harness.cpp` (`make rfl`):

- **Init.** `RFLInitRes(work, RFL_Res.dat, size, FALSE)` runs as `RPSysKokeshiManager` does.
  - There is no NAND on the host (`tests/rfl_host.c`). The Mii database open reports
    `NAND_RESULT_NOEXISTS`.
  - `RFLiBootLoadDatabaseAsync` treats that as an empty database and goes on to read the
    cached resource's header.
- **Every file of the 18 part archives (472 files).**
  - Textures (eye, eyebrow, mouth, mustache, mole): `RFLiLoadTexture` and
    `RFLiInitRFLTexture`.
  - Shape textures (face, cap, nose line, glasses): `RFLiLoadShpTexture` and
    `RFLiInitTexRes`, which copies the image and makes the `GXTexObj`.
  - Shapes: `RFLiLoadShape` and `RFLiInitShapeRes` in four modes: plain, flipped, transformed
    (scale 0.75 and an offset, as the hair, cap and beard are) and both.
- **Test.** `tests/test_rfl.py` parses `RFL_Res.dat` on its own: the section table, the
  per-archive offset tables, the texture headers and the shapes. From that it builds the
  loader state, every file's size and hash, the `GXTexObj` words, the image copies, and the
  vertex arrays in the four modes (as big-endian GX data) with the faceline `Vec`s.
- **Not checked.** The display lists `RFLiInitShapeRes` builds go to the GX FIFO (`WGPIPE`).
  The host has no FIFO yet (`rfl_host.c` defines it as a plain variable), so their bytes are
  not checked. Their source bytes are 8-bit indices and need no conversion.

`tests/hbm_harness.cpp` (`make hbm`):

- **Set-up.** The harness mounts `Common/PartyStatic/common.carc` and
  `US/PartyStatic/local.carc` as `RPSysResourceManager` does. It fills `HBMDataInfo` as
  `RPSysHomeMenuMgr` does (work memory, `HBM_CONFIG_SIZE`, the same archive paths) and runs the
  real `HBMCreate` for each `homeBtn_*.arc` on the disc (ENG, FRA, SPA). That covers:
  - `set_config`
  - `set_text`
  - the layout, its 6 button animations, the group and pair animations, and the GUI pane
    manager
  - `RemoteSpk`
  - `init_msg`, which puts the region's three messages in the text boxes
- **Speaker sounds.** Every `SpeakerSe.arc` sound is played with `RemoteSpk::Play`, and the
  real `UpdateSpeaker` (from `RemoteSpk::Start`'s periodic alarm) runs until it is done.
  Since Phase 7 the packets go through the real speaker path: the decomp's WENC encoder and
  the §23 WPAD backend, whose speaker `RemoteSpk::Connect` turns on and sets playing; the
  harness takes them from the host speaker callback (`HS`: packets, bytes, hash of the
  4-bit ADPCM), and `test_hbm.py` encodes the scaled PCM itself (a model of `wenc.c`).
- **Repeated create.** The library has no `HBMDelete` (and `~HomeButton` is not in the game),
  so each language makes a new instance in new work memory. The second and third creates see
  the message buffer already in native order.
- **Test.** `tests/test_hbm.py`:
  - parses `config.txt` the way `set_config` does
  - splits the big-endian `.csv` the way `set_text` does
  - scales the PCM into 40-sample packets, times the volume / 10 and zero-padded
  - checks that every file in a `HomeMenu` directory is covered: here, or by the snd harness
    (`HomeButtonSe.brsar`), the BTI harness (`homeBtnIcon.bti`) or the layout harness (the
    archives' contents)

`test_polyline` (in `make unit`): the real `RPPolyline::Calc` for 3 to 8 control points at 65
values of `t`, compared bit for bit with de Casteljau's algorithm written out with the same
`VEC3Lerp`.

Compiled only, so their host forms keep building: `RPKokeshiMgr_3`, `RPSysKokeshiManager`,
`RPSysHomeMenuMgr` and `g3d_scnrfl`.

Results (64- and 32-bit):

```
bti_harness: 15 BTI, 11 RPTnkFootmarkTex textures; 0 errors
rfl_harness: 1 RFL_Res.dat, 472 part files (105 textures, 106 shape textures, 1044 shape loads); 0 errors
hbm_harness: 3 HBMCreate, 84 messages, 9 text boxes, 5 speaker sounds; 0 errors
test_polyline: 390 checks, 0 errors
```

### Host stand-ins

- **`tests/rfl_host.c`.**
  - NAND: open reports `NOEXISTS`; the other file calls abort.
  - `GXBeginDisplayList` / `GXEndDisplayList`: no-ops; End returns 0 bytes, so
    `RFLiCharModelRes`'s display-list sizes are 0 on the host until Phase 7.
  - `WGPIPE`, `OSRegisterVersion`, alarms and interrupts.
- **`tests/hbm_host.cpp`.**
  - `MEMInitAllocatorForExpHeap`: the SDK's, in `mem_allocator.c`. That file is not built,
    because its two dispatch functions are already in `src/host_gx.cpp`.
  - Alarms: `RemoteSpk::Start`'s periodic alarm is recorded, not scheduled; the one-shot
    retry alarms (after a communication error, which the backend never reports) abort.
  - WPAD and WENC: the §23 backend (`src/host_wpad.cpp`, with `src/host_sc.cpp`) and
    `src/revolution/WENC/wenc.c` since Phase 7 (they were stand-ins before).
  - The drawing and pointer-math calls that `HBMCreate` does not reach abort.
  - Thread and interrupt stand-ins come from `src/host_snd.cpp` (`libwiiplay_snd.a`, which
    also provides the snd classes HBM references).

### Not covered yet

- **Mii data.** `RFLiCharData` / `RFLiHiddenCharData` / `RFLStoreData` are packed big-endian
  bitfield records (the NAND database `RFL_DB.dat`, Wii Remote memory, `RFLMakeIcon` input).
  They are not disc files, and the host has no NAND, so `RFL_DataUtility.c`'s conversions run
  on host bitfield layouts that do not match the Wii's.
  - That needs a typed big-endian accessor for the bitfields, and test data from a real
    database.
  - It is needed before Miis from a Wii save can be shown. The default Miis
    (`RFL_DefaultDatabase.c`) are built in code and do not depend on it.
- **RFL display lists and face textures.** `RFLiInitShapeRes`'s display lists and
  `RFLiMakeTexture`'s render-to-texture face (`GXCopyTex`) need the Phase 7 GX layer.
- **HOME Menu at run time.** Done in the boot since §34 (`HBMInit` / `HBMCalc` / `HBMDraw`,
  the pointer hit tests, drawing, the AX reverb hooks, the sounds through
  `RPSndHomeMenuArcMgr`).

## 17. Animation evaluation: g3d animations and ef curves

Every animation on the disc is now evaluated on the host through the game's own readers, and
every result matches, bit for bit, an independent evaluation written from the formats and the
decompiled formulas: all 222 g3d animations (CHR0, CLR0, SRT0, PAT0, SCN0) through
`ResAnm*::GetAnmResult`, and all 1168 BREFF particle and emit curves through
`ef_animcurve.cpp`. The matching build is unchanged: `check_matching.py` compiles the 970 units
that depend on a touched file, all identical (2026-10-30), `ef_animcurve` (a FUNCTIONAL unit)
included.

### What was still read wrongly

The g3d animation file structs were already `be_*` (§11). What made evaluation wrong on the
host was elsewhere:

- **Inline-assembly helpers.** `ASM(...)` compiles to nothing on the host, so these returned an
  uninitialised value:
  - `OSFastCast.h`: `OSu8tof32`, `OSu16tof32`, `OSs16tof32`, `OSf32tou8`, `OSf32tou16`,
    `OSf32tos16`, and through them `math::U16ToF32`, `F32ToU16`, `S16ToF32`, `F32ToS16`;
  - `g3d_cpu.h` fastcast on host-order values: `S7_8ToF32`, `S10_5ToF32` (GQR7 / GQR6),
    `F32ToS10_5`, and through `OSFastCast.h` `U8_0ToF32`, `U16_0ToF32`, `F32ToU8_0`;
  - `math_arithmetic.h`: `FAbs`, `FInv`, `FSelect`.

  Every keyframe search uses `F32ToU16` for its first estimate, CHR0 FVS32 / FVS48 keys
  decode their frames, values and slopes with the fastcast loads, CV tracks and `SinCosFIdx`
  truncate with `F32ToU16`, the Hermite interpolations divide with `FInv`, and
  `MTX34RotXYZDeg` calls `SinCosFIdx`, which starts with `FAbs`.
- **SCN0 enums.** `ResAnmCameraData::projType`, `ResAnmLightData::distFunc` / `spotFunc` and
  `ResAnmFogData::type` were plain enums. `ResAnmCamera::GetAnmResult` switches on the
  projection type: an orthographic camera (1, read as 0x01000000) took neither branch, and the
  result's `projType` was wrong (the disc has both kinds). They are `BE_ENUM(..., u32)` now.
- **SHP0.** `ResAnmShp::GetAnmResult` read its index table through `const u16*`; it is
  `const be_u16*` (no SHP0 on the disc).
- **ef curve keys.** `ef_animcurve.cpp` describes keys and ranges with its own structs
  (`AnimCurveKeyTexture`, `AnimCurveRangeTexture`, `AnimCurveKeyChild`, `AnimCurveTable<T>`,
  ...) and reads byte-addressed keys through `u16*` / `f32*` casts. The structs are `be_*` now
  and the casts `be_u16*` / `be_f32*` (on the Wii `be_u16` is the macro `u16`, so the code is
  the same tokens). `EmitterInheritSetting::speed`, which a child curve key embeds and
  `CreationQueue` copies, is `be_s16`.
- **The random seed.** `CalcRandomSeed` (and `createChild`, and the random field in
  `ef_particlemanager.cpp`) fold a `u32` byte by byte through a `union { u32; u8[4]; }`:
  `bytes[2] ^= bytes[3]` and so on, where `bytes[0]` is the most significant byte on the Wii.
  On a little-endian host that folds the other way and every random key, range and table
  pick changes. The indexes are `bytes[BE_BYTE(i, 4)]` now: `BE_BYTE(i, size)` is `i` in
  `include/port/be.h` and `size - 1 - i` on a little-endian host.
- The `curveFlag` / `kindEnable` pair that `Emitter` and `ParticleManager` read as one `u16`
  is read as `be_u16` (`ef_emitter.cpp`, `ef_particlemanager.cpp`; neither is built on the
  host yet).

### Host forms of the asm helpers

As for the `VEC3` inlines in §16, each asm block is opened with `ASM_PS(name)`. Each header
defines `ASM_PS(name)` as `ASM` when it is not already defined (`OSFastCast.h`,
`math_arithmetic.h`, `g3d_cpu.h`; `math_types.h` already did), so the Wii compiles the same
asm. On the host, `port/host/include/decomp/compat.h` replaces the block with C on the
function's own variables:

- quantized loads (`psq_l`): convert and divide by 2^scale (u8 / u16 / s16 at scale 0 for
  the OS GQRs; S7.8 and S10.5 for g3d's GQR7 / GQR6);
- quantized stores (`psq_st`, `port_psq_quantize`): multiply by 2^scale, clamp to the
  integer type's range, truncate toward zero (what Dolphin's `ScaleAndClamp` does); NaN, which
  the hardware leaves undefined, gives 0;
- `FAbs` is `fabsf`; `FSelect` and `math_types.cpp`'s `FSEL_MAX` / `FSEL_MIN` follow `fsel`
  (`value >= 0`, so -0 selects the first operand and NaN the second);
- `FInv` divides: `1.0f / x`.

`src/nw4r/math/math_types.cpp` is now built into the host library (`MTX34RotXYZFIdx`).
`MTX34InvTranspose` is a CodeWarrior `asm` function, which GCC cannot compile; it is under
`#if !defined(PORT_HOST)`. `test_port` checks the conversions, the clamping and `FSelect` on
hand-worked values.

### Floats on the host and on the Wii

The host computes every operation in single precision with SSE and no fused multiply-add:
x86-64 has no FMA without `-march` options, and on `-m32` the g3d resource, `nw4r::math` and
`ef_animcurve` objects and both harnesses are built with `-msse2 -mfpmath=sse` (`ANIM_FP` in
the Makefile; x87 would keep extended precision between operations). The Python models round
every operation to f32 the same way, so the comparison has no tolerance: 0 ulp.

The console can differ from the host in the last bit or so of a float result, for three
reasons the port does not reproduce:

- `FInv` on the Wii (Rev 1) is `fres` refined by one Newton-Raphson step, within about an
  ulp of `1 / x`; the host divided exactly until §42, which gives it Broadway's `fres` (the
  model in `test_anims.py` too).
- `g3d_resanmchr.cpp` is compiled with `#pragma fp_contract on`, so CodeWarrior fuses
  multiply-adds (the FVS value `scale * v + offset`, the Hermite polynomial) into `fmadds`,
  rounding once where the host rounds twice.
- The truncating `psq_st` follows Dolphin; it only decides which integer a float lands on,
  which matters at exact integer frames, not in between.

Key and frame selection does not depend on any of these except at exact boundaries, and the
integer, colour, enable-bit and texture results are exact. Pinning the last bits would need
results from Dolphin or a console.

### What runs

`tests/anm_harness.cpp` (`make anims`):

- Walks the disc and sets every BRRES up as `g3d_harness` does (§11): `CheckRevision`,
  `Init`, `Bind(itself)`, `ResAnmScn::Bind`, then the archive's other files for one that
  stays unresolved, so all 56 PAT0 slots are bound.
- Evaluates every animation at 21 frames: before the range (-1.5), 0, 0.25, 1, 1.5, six
  fractional and six integer frames spread over the range, the last frames and 2.5 past the
  end. Per frame it calls, for every node, material, light, ambient light and camera:
  `ResAnmChr::GetAnmResult`, `ResAnmClr::GetAnmResult`, `ResAnmTexSrt::GetAnmResult`,
  `ResAnmTexPat::GetAnmResult`, and through `ResAnmScn::Get*ByRefNumber` the
  `ResAnmAmbLight`, `ResAnmLight` and `ResAnmCamera` readers. Each result struct is zeroed
  first and printed whole (floats as bits, PAT0 textures as the bound `ResTex` name).
- `tests/test_anims.py` locates the blocks and records with
  `port/assets/tools/fmt_brres_anim.py` (read-only) and evaluates them from the bytes: the
  keyframe estimate-and-walk search and both Hermite forms, CHR0 FVS32 / FVS48 / FVS96 keys
  (quantised frames, 12- and 16-bit values, S10.5 / S7.8 slopes) and CV8 / CV16 / CV32
  tracks, `MTX34RotXYZDeg` with `SinCosFIdx`'s table (read from
  `src/nw4r/math/math_triangular.cpp`), the colour lerp, the light enable bits, `ClipFrame`,
  the PAT0 frame search and the SCN0 records' flag logic. Perturbing the model (either
  Hermite form, the S10.5 frame quantisation, the colour lerp shift, `SinCosFIdx`, the CV
  lerp) makes it report differences.

`ef_harness --curves` (`make efcurves`):

- Registers the pairs in the game's order as in §12. Right after a file is registered it runs
  each particle and emit track through `AnimCurveExecuteU8`, `F32`, `Rotate`, `Texture` or
  `Child` (by the curve type) for seeds 0, 0x1234 and 0xBEEF, lifetimes 20 (every tick to 21)
  and 97 (every third tick to 99), and ticks 250 and 1000.
- Targets start as 0xEE bytes. The texture curve gets a particle whose three texture slots
  are printed as the `TextureData` names RelocateCommand stored, plus the wrap and reverse
  bits. The child curve gets a particle → `ParticleManager` → `Emitter` → `Effect` →
  `EffectSystem` chain (raw storage; only these members are used), and the harness defines
  `CreationQueue::AddParticleCreation` / `AddEmitterCreation` to record the requests: type,
  the 8 `EmitterInheritSetting` bytes and `speed`, the emitter's name and `calcRemain`.
- `tests/test_efcurves.py` locates the tracks with `port/assets/tools/fmt_effect.py`
  (read-only; it also checks the harness's track offsets against it) and evaluates them:
  `CalcTime` / `CalcFrameStep` in every loop mode, the stride and step key searches, the
  per-key random seed (folded in the Wii's byte order), range and table picks, the random
  rotation sign, `AdjustTurnLoop`, the linear / smooth / eased / step interpolations of bytes
  and floats, texture keys with random reverse bits, and the child walk over loops. Texture
  and child names resolve when a registered project (the boot pairs and the current scene
  pair) has them.

What the disc exercises: U8, F32 and Rotate curves in one-shot, loop-count, infinite, turn,
fitting and sync modes; linear and smooth keys (no eased or step key on the disc); random keys
from the range table (all three types) and from the random table (U8). Texture curves are all
one-shot; child curves are one-shot, loop-count, infinite and fitting, none in turn mode. The
other paths are ported in the test but no disc data reaches them. Perturbing the test's model
(the little-endian seed fold, the rotation sign bit, the turn-loop seed, the fitting loop, the
random reverse bits, the smooth curve) makes it report differences, so these paths are covered.

Results (`make test` and `make M32=1 test`):

```
anm_harness: 132 BRRES, 222 animations, 59010 results; 0 errors
test_anims: 132 BRRES; AMB 1134, CAM 399, CHR 54684, CLR 1008, LIT 1008, PAT 231, SRT 546; 0 errors
ef_harness: 206736 curve evaluations, 2286 child creation requests
test_efcurves: 206736 evaluations (U8 71154, F32 100536, Texture 3186, Child 15045, Rotate 16815), 2286 child requests; 0 errors
```

`ef_harness`'s changed-run count without `--curves` is unchanged; with it, the evaluation's
own allocations move the scene files to other emulated addresses, which changes how many runs
the relocated addresses split into.

### Not covered yet

- VIS0 and SHP0: their readers build, but no file on the disc has one. `ResAnmFog` has no
  reader in Wii Play (dead-stripped).
- The `AnmObj*` classes (`g3d_anm*.cpp`: frame control, blending, `AnmScnRes`) are not built
  on the host.
- Eased and step keys, and turn-mode texture and child curves: ported in the test, not on the
  disc.
- The field parameter blocks behind a curve's tables are big-endian and checked since §18;
  `Emitter`, `ParticleManager` and `Particle` calculation do not link on the host yet.
- Console equivalence of the last float bits (see above).

## 18. ef field parameters; every paired-single asm block with a host form

Two things: the BREFF field parameter blocks are read big-endian through the game's own
structs, and every asm block or asm function in `src/` and `include/` that the host builds or
will build has a C form, so none of them silently compiles to nothing. The matching build is
unchanged: `check_matching.py` compiles the 974 units that depend on a touched file, all
identical (2026-10-30).

### ef field parameter blocks

A particle F32 track whose kind is a field carries a parameter block in its info table, after
the name table; `ParticleManager::Calc` finds it at `header + sizeof(AnimCurveHeader) +
keyTable + rangeTable + randomTable + nameTable` and switches on the kind: 32 gravity, 39
random, 38 spin, 34 magnet, 35 newton, 36 vortex. (Kind 33, `FIELD_SPEED`, has a 4-byte info
table the game never reads.) The structs were local to `ef_particlemanager.cpp` and plain;
they are in `include/nw4r/ef/ef_res_animcurve.h` now, with `be_f32`, `be_u16` and
`BE_MATH(VEC3)` members:

| Struct | Size | Members |
|---|---|---|
| `FieldGravityParam` | 0x14 | `power`, `dir`, `space`, `type` |
| `FieldRandomParam` | 0xC | `power`, `diffuse`, `interval` (u16), `flags`, `type` |
| `FieldSpinParam` | 0x14 | `speed`, `rot`, `space`, `type`, 2 reserved |
| `FieldMagnetParam` | 0x14 | `power`, `pos`, `space`, `type`, 2 reserved |
| `FieldNewtonParam` | 0x18 | `power`, `range`, `pos`, `space`, `type`, 2 reserved |
| `FieldVortexParam` | 0x1C | `innerSpeed`, `outerSpeed`, `radius`, `rot`, `space`, `type`, 2 reserved |

Gravity and random are read through a pointer, which works unchanged. For spin, magnet,
newton and vortex `Calc` copies the block into a local and lets the curve overwrite one member
as a float (`AnimCurveExecuteF32(..., &param.speed, ...)`); a `be_f32` member cannot take
that. The local is declared `NW4R_EF_FIELD_VALUE(FieldSpinParam)`: the struct itself on the
Wii, and on the host `FieldSpinParamValue`, a host-order copy with a converting constructor
(defined in the header under `#if PORT_BE_WRAPPED`). `layout_asserts.cpp` checks the sizes
and member offsets.

`ef_harness` prints an `FP` line for every field block of a particle F32 track, located with
`Calc`'s expression and read through the structs (the four copied kinds through
`NW4R_EF_FIELD_VALUE`, as `Calc` does). `fmt_effect.py` decodes the same blocks (it places
the info table after the last name entry, independently of `nameTable`), and
`test_effects.py` compares them: 96 blocks on the disc (gravity 70, spin 16, random 8,
magnet 2; no newton or vortex; plus 87 speed tracks whose info table is not read). Reading
`power` and `interval` as plain types makes 77 of the 96 lines differ.

### Asm on the host

`ASM(...)` is empty on the host and GCC cannot compile CodeWarrior `asm` functions, so the
audit covered every `ASM`, `ASM_VOLATILE`, `asm { }` / `asm volatile { }` block and `asm`
function in `src/` and `include/`. As in §16 and §17, each block is opened with
`ASM_PS(name)` (`ASM` on the Wii: same tokens, same object) and
`port/host/include/decomp/compat.h` turns `ASM_PS(name)` into `PORT_PS_name`, C on the
block's own variables. The C is in `port/host/include/port/ps.h` (one helper per
paired-single instruction: `ps_madd`, `ps_msub`, `ps_nmsub`, `ps_madds0/1` are fused
`fmaf`; `ps_sum0`, `ps_merge*`, `psq_l` / `psq_st` with W=1 loading `(x, 1.0)`) and
`port/host/include/port/ps_forms.h` (each block or function transliterated instruction by
instruction, loads before stores where the asm does so, so in-place calls behave the same).
`asm` functions get a body under `#if defined(PORT_HOST)` that calls the transliteration.
Blocks that keep a paired value across several asm blocks (the skinning and key-shape sums)
declare the pair as a local in the first block's host form (`port_skin[12]`,
`port_acc[4]`).

Precision: the host forms do in single precision (SSE; `ANIM_FP` on `-m32`) what the
instructions do, fused where the Wii fuses, so they are bit-exact except where the Wii starts
from an estimate. `fres` and `frsqrte` are about 12-bit estimates on the Wii, which the code
then refines (one or two Newton-Raphson steps); the host started from the exact `1 / x` and
`1 / sqrt(x)` and ran the same steps (since §42: Broadway's estimates, bit for bit, checked against the game's own functions in
Dolphin). The refined results agree with a double-precision reference to 2^-21 relative
(`test_math`).

`tests/test_math.cpp` (`make unit`) calls the built functions and runs the macros of the
units that are not built yet in a scope with their variables. Exact: copies, identities,
quantized stores, single products, small-integer sums, sign flips. Fused sums and paired sums:
a few ulp of the operand magnitudes against double. Estimates: 2^-21 relative (InvTranspose
and the local axis: 2^-15 of the inverse's magnitude, with ill-conditioned inputs skipped).
7794 checks on both builds.

| Function / block | File | What it does | Host form | Test |
|---|---|---|---|---|
| `VEC2Normalize` | `src/nw4r/math/math_types.cpp` | 2D normalize: frsqrte + 1 NR | `port_vec2_normalize` | 2^-21 rel, in place |
| `MTX33Identity`, `MTX44Identity` | same | identity | `port_mtx33_identity`, `port_mtx44_identity` | exact |
| `MTX34ToMTX33` | same | copy 3x3 | `port_mtx34_to_mtx33` | exact |
| `MTX34InvTranspose` (asm fn) | same | (3x3 inverse)^T; FALSE if det == 0; fres + 2 NR | `port_mtx34_inv_transpose` | 2^-15, hand-worked exact, singular |
| `MTX34Zero` | same | clear | `port_mtx34_zero` | exact |
| `MTX34Scale` | same | columns times (x, y, z) | `port_mtx34_scale` | exact, in place |
| `MTX34Trans` | same | copy 3x3, `_i3 = M_i·t + _i3` (fused pairs) | `port_mtx34_trans` | few ulp; integers exact |
| `MTX44Copy` | same | copy | `port_mtx44_copy` | exact, in place |
| `FSEL_MAX` / `FSEL_MIN`, `FAbs`, `FInv`, `FSelect`, fast casts | §17 | | §17 | `test_port` |
| `FrSqrt` | `src/nw4r/math/math_arithmetic.cpp` | 1 / sqrt: frsqrte + 1 NR | `port_frsqrt_nr` | 2^-21 rel; `FrSqrt(4) == 0.5` |
| `ut::Abs<f32>` | `include/nw4r/ut/ut_algorithm.h` | fabs | `fabsf` | exact, -0, NaN |
| `Copy32ByteBlocks`, `ZeroMemory32ByteBlocks` | `src/nw4r/g3d/platform/g3d_cpu.cpp` | 32-byte block copy / clear | `memcpy` / `memset` per block (replaces `host_gx.cpp`'s copy) | exact |
| `Fog::CopyTo` block | `src/nw4r/g3d/g3d_fog.cpp` | 48-byte copy (lfd/stfd) | `memcpy` | exact (macro) |
| `GetModelLocalAxisY2`, `Y3` (asm fns) | `src/nw4r/g3d/g3d_calcview.cpp` | rows of M times column 1 of P^-1 (Y2: z = 0); fres + 1 NR | `port_g3d_local_axis_y` | 2^-15; singular P |
| `SetMdlViewMtxSR` (3 overloads) | same | billboard scale/rotate rows | `port_g3d_view_sr1/3/7` | exact (macros) |
| `CalcSkinning` weight blocks (3) | `src/nw4r/g3d/g3d_calcworld.cpp` | sum of weight · matrix, fused | `port_skin[12]` + `fmaf` | few ulp (macros) |
| `CalcVtx` position / normal blocks (3, twice) | `src/nw4r/g3d/g3d_calcvtx.cpp` | key-shape blend of big-endian vertex arrays | `port_g3d_vtx_first/key/store` | exact (macros) |
| `CalcVtx` colour blocks (3) | same | GQR2 u8 loads, blend, clamped u8 stores | `port_g3d_clr_first/key` + `port_psq_quantize` | exact (macros) |
| `BlockAbs` | `src/Pack/RPParty/RPTnkScene/RPTnkBlock.cpp` | fabs | `fabsf` | exact (macro) |
| `PointerScreenWidth` (2 blocks) | `src/Pack/RPParty/RPHkyScene/RPHkyPaddle.cpp` | `lhzx` from the TV-mode width table | C load | exact (macro) |
| `DrawGX` scheduling block | `src/Pack/RPGraphics/RPGrpModelRfl.cpp` | `setting = &sDrawCoreSetting; spDrawModel = this; stageNum = TEV_STAGE_NUM[type]` | `#if defined(PORT_HOST)` C | compiles (unit not linked) |
| ctor / `init` blocks | `src/Pack/RPSystem/RPSysPauseMgr.cpp` | `li clear, 0`; `second = this + 0xF0` | C | compiles |
| `RPCowKakasiGroup::OnActivate` (asm fn) | `src/Pack/RPParty/RPCowScene/RPCowKakasiGroup.cpp` | game logic | its C++ equivalent (was `#if 0`) | compiles |
| `RPPolyline` block | `src/Pack/RPParty/RPPartyCommon/RPPolyline.cpp` | member load | `#if defined(PORT_HOST)` since §5.1 | `test_polyline` |
| `PSMTXIdentity`, `PSMTXCopy` (asm fns) | `src/revolution/MTX/mtx.c` | identity, copy | C | exact |
| `PSMTXConcat`, `PSMTXConcatArray` (asm fns) | same | A · B (fused column pairs; the array form adds the translation with `ps_madd`) | `port_mtx_concat` | few ulp; identity exact; in place |
| `PSMTXMultVec` (asm fn) | `src/revolution/MTX/mtxvec.c` | M · v + t | `port_mtx_mult_vec` | few ulp |
| `PSVECAdd`, `PSVECDotProduct`, `PSVECCrossProduct` (asm fns) | `src/revolution/MTX/vec.c` | add, dot, cross | C / `port_vec_dot` / `port_vec_cross` | exact / few ulp, in place |
| `PSVECScale`, `PSVECNormalize`, `PSVECMag` (2 blocks), `PSVECSquareDistance` | same | scale, normalize (frsqrte + NR), magnitude, squared distance | `PORT_PS_*` | exact / 2^-21 / few ulp |
| `PSQUATMultiply`, `PSQUATNormalize` | `src/revolution/MTX/quat.c` | Hamilton product; normalize (double-precision NR, zero below 1e-5) | `port_quat_multiply`, `port_quat_normalize` | few ulp; 2^-21; i·j = k; tiny → 0 |
| `C_VECHalfAngle`, `C_QUATMtx`, `C_QUATSlerp`, `C_MTXFrustum` / `Perspective` / `Ortho` | `vec.c`, `quat.c`, `mtx44.c` | plain C | built as is | `C_VECHalfAngle` |

`src/revolution/MTX/{mtx,mtxvec,vec,quat,mtx44}.c` are built into the host library now, which
replaces the C stand-ins `host_gx.cpp` had for `PSMTXIdentity`, `PSMTXCopy` and `PSMTXConcat`
and `host_gfx_stubs.c` had for `PSVECMag` and `C_MTXOrtho` (and the aborting `PSMTXInverse`,
`PSMTXMultVec`, `PSMTXTrans` in `tests/hbm_host.cpp`). The MTX functions the game calls that
the decomp does not contain at all (`PSMTXInverse`, `PSMTXInvXpose`, `PSMTXTranspose`,
`PSMTXTrans`, `PSMTXScaleApply`, `PSMTXRotTrig`, `PSMTXRotAxisRad`, `PSMTXQuat`,
`C_MTXLookAt`, `C_MTXLightFrustum`, `C_MTXLightPerspective`, `C_MTXLightOrtho`) are in
`src/host_mtx.cpp`, written from the SDK's C reference forms (`PSMTXScale`,
`PSMTXTransApply`, `PSMTXRotRad` stay in `host_gx.cpp`). Without the Wii's code they are not
bit-exact: the paired-single library rounds some sums differently.

Other CodeWarrior intrinsics the decomp calls get host definitions in `compat.h`:
`__frsqrte`, `__fres` (exact, see above), `__fabs`, `__fnabs`, `__abs`, `__cntlzw`,
`__rlwimi`.

**A Wii quirk the host keeps.** `GetModelLocalAxisY3`'s z multiplies row 2 of the model
matrix by the register that holds `(inv21, (P02 - P22) / det)` instead of `(inv01, inv11)`,
so z = (M20 + M22) · inv21 + M21 · (P02 - P22) / det rather than row 2 times the Y column.
The asm is the binary's (it matches), so the host does the same; `test_math` models it.

### Hardware-only asm: not emulated

These touch processor or chipset state. The host does not build them; what it does instead:

| Asm | File | Host |
|---|---|---|
| `OSInitFastCast`, `OSSetGQR6`, `OSSetGQR7` (GQR SPR writes) | `include/revolution/OS/OSFastCast.h` | empty: the host forms hard-wire what they set (GQR2-5 u8 / u16 / s8 / s16 scale 0; g3d's GQR6 s16 scale 5, GQR7 s16 scale 8), and these are the only GQR settings in the code |
| `DC*` / `IC*` cache ops, `LC*` locked cache DMA, `DCZeroRange` | `src/revolution/OS/OSCache.c` | `DCFlushRange` / `DCStoreRange` / `DCInvalidateRange` (and NoSync) are no-ops in `host_sdk.cpp` (host memory is coherent); `IC*`, `LC*` and `DCZeroRange` have no host definition, so a caller fails to link rather than run (none is on the load paths) |
| `OSDisableInterrupts` / `OSEnableInterrupts` / `OSRestoreInterrupts`, exception and decrementer handlers, system call vector | `OSInterrupt.c`, `OS.c`, `OSAlarm.c`, `OSSync.c`, `db.c` | interrupts are a flag in `host_snd.cpp` (and the `tests/*_host` stand-ins); no exception vectors on the host |
| Context save / load, fibers, FPU init | `OSContext.c`, `OS.c` (`__OSFPRInit`, `__OSPSInit`) | not built; threads are `host_sdk.cpp`'s stand-ins |
| `OSGetTime`, `OSGetTick` (`mftb`) | `OSTime.c` | `host_snd.cpp` (`OSGetTick` counts calls) and the test stand-ins; a real clock comes with Phase 7 |
| MMU / BAT set-up (`ConfigMEM*`, `RealMode`) | `OSMemory.c` | the M0 arena (§5) |
| `__start`, `__init_registers`, `__init_hardware`, `__flush_cache` | `__start.c`, `__ppc_eabi_init.c` | the host's own start-up |
| `PPCMf*` / `PPCMt*` SPRs, `PPCHalt`, `PPCSync` | `src/revolution/BASE/PPCArch.c` | not built |
| `__AICallbackStackSwitch` | `src/revolution/AI/ai.c` | `AIRegisterDMACallback` in `host_snd.cpp` |
| FIFO writes (`WriteMTXPS*`, `WriteProjPS`, `WriteLightObj`, `LoadProjPS`, `Copy6Floats`, `GXGetProjectionv`) | `src/revolution/GX/GXTransform.c`, `GXLight.c` | the GX layer (`host_gx.cpp` now, Phase 7) |
| Compiler runtime (`__save_gpr`, `__restore_fpr`, `__div2i`, `__cvt_fp2unsigned`, ...), pointer-to-member thunks | `src/runtime/runtime.c`, `ptmf.c` | the host compiler's own |
| MetroTRK debugger | `src/MetroTRK/...` | not part of the port |

### Still open

- `ParticleManager::Calc` itself (and `Emitter`, `Particle`) compiles on the host but does
  not link yet, so the field forces are not evaluated against a model; the parameter blocks
  are. (Done in §19.)
- The MTX functions in `src/host_mtx.cpp` are reference math, not the Wii's rounding.
- Bit-exact `fres` / `frsqrte` (Dolphin's tables) if console-exact floats are ever needed.
- `g3d_calcview.cpp`, `g3d_calcvtx.cpp`, `g3d_calcworld.cpp`, `g3d_fog.cpp` and the game
  units with host forms compile on the host; they are linked as the g3d runtime and game code
  are brought up (the game units still have unrelated host errors, e.g. `RPTnkBlock.cpp`'s
  anonymous union, `RPHkyPaddle.cpp`'s address of a temporary).

## 19. The nw4r::ef runtime on the host: every emitter created and stepped

The particle runtime links and runs on the host: `EffectSystem`, `Effect`, `Emitter`,
`ParticleManager` (`Calc` with its field forces), `Particle`, the emit forms, `CreationQueue`
and the curve evaluator, with the game's own `RPSysParticleManager` and
`RPSysEffectMemoryManager`. `ptcl_harness` creates every emitter of every BREFF on the disc
(373) as an effect through the real `EffectSystem` API and steps it 60 frames;
`test_particles.py` checks the result against independent models where practical and for
determinism otherwise. 64- and 32-bit output is byte-identical. The matching build is
unchanged: `check_matching.py` compiles the 974 units that depend on a touched file, all
identical (2026-10-30).

### Changes to the decomp sources

Three, all natural C++ that compiles to the same objects on the Wii:

| File | Change | Why |
|---|---|---|
| `include/nw4r/ef/ef_memorymanagertmp.h` | `AssignObjectID` casts `void*` to `T*`, not to `LinkedObject*` | `LinkedObject` has no virtual functions and `ReferencedObject` adds the first, so under the Itanium ABI the vtable pointer comes first and `LinkedObject` sits at offset 8 (4 on `-m32`) of `Effect` / `Emitter` / `ParticleManager` / `Particle`. CodeWarrior puts the vtable pointer after the base's members, so the base is at offset 0 on the Wii and the `void*` cast happened to work. On the host it added 0x10000 to the free-list link instead of the object ID, and the next allocation returned a pointer into the middle of the pool. |
| `src/nw4r/ef/ef_effect.cpp` | `CreateEmitter` returns `NULL`, not `false`, on failure | GCC rejects `bool` to pointer. |
| `src/nw4r/ef/drawstrategy/ef_drawstrategyimpl.cpp` | `#if PORT_BE_WRAPPED`: copy `mIndTexOffsetMtx` (`be_f32[2][3]`) into a float matrix for `GXSetIndTexMtx` | Draw path only; it lets the draw strategies compile so the real `DrawStrategyBuilder` can be linked. |

`check_matching.py`'s list of touched files has the three files.

### What is linked, and what is stubbed

`PTCL_SRCS` in the Makefile: every `src/nw4r/ef` unit except the resource units already in
`EF_OBJS`, all emit forms, all draw strategies and the builder, and
`src/Pack/RPKernel/RPSysParticleManager.cpp`. `ParticleManager::Initialize` asks the
`DrawStrategyBuilder` for a strategy and keeps the pointer; nothing on the calc path calls
it. The GX functions the strategies reference (`GXEnableTexOffsets`, `GXGetVtxAttrFmt`,
`GXGetVtxDesc`, `GXSetChanAmbColor`, `GXSetClipMode`, `GXSetCoPlanar`, `GXSetLineWidth`,
`GXSetPointSize`, `GXSetZCompLoc`, besides those already there) are `PORT_STUB` entries in
`src/host_gfx_stubs.c` that abort with their name if called. `OSRegisterVersion` (the
`EffectSystem` constructor, a static initialiser, registers the library banner) is a no-op
there.

On `-m32` the ef runtime objects and `src/host_mtx.cpp` (`PSMTXInverse`, which `MTX34Inv`
reaches in every `ParticleManager::Calc` and `Emitter::CalcEmission`) are built with
`ANIM_FP` (SSE), as the curve evaluator already was (§17). With x87 arithmetic 25821 output
lines differed from the 64-bit build, and still 4595 with only `host_mtx.cpp` left on x87
(last-bit differences, e.g. positions near zero after a matrix inverse).

### The harness

`tests/ptcl_harness.cpp` follows the game's set-up:

- `RPSysSystem`'s `EffectArg` (100 effects, 250 emitters, 250 particle managers, 1500
  particles) and `RPSysEffectMgr::createMemoryManager`: `RPSysEffectMemoryManager` on a
  work buffer of `CalcMemorySize`, handed to `EffectSystem::SetMemoryManager` with
  `RPSysEffectDefine::MAX_GROUP_ID`;
- `RPSysEffectCreator::setEffectResource` / `resetEffectResource`: the cursor and static
  pairs stay registered, each scene pair is registered, run and removed;
- `RPSysEffectCreator::createEffect`: the group ID from the creator's base
  (`drawScene << 7`), draw group 0 and the userdata flags, then
  `EffectSystem::CreateEffect(name, groupID, 0)`; one effect at a time;
- `RPSysEffectCreator::update`: `EffectSystem::Calc(groupID, false)` once a frame, the root
  matrix left at the identity (what `RPSysEffect::update` sets without a transform);
- `RPSysEffect::kill` at the end (`RetireEmitterAll`, `RetireParticleAll`, then
  `RetireEffect` and one more `Calc` to collect), after which every pool must be empty.

`EffectSystem::mRandom` (it seeds emitters whose `randomSeed` is 0) is reseeded before each
`CreateEffect`, with 0x1234 or the second argument, so every effect is deterministic.
Output: per frame the emitter count, the root emitter's tick, live and new particles of the
root emitter's resource and of all managers; at frames 1-5, 10, 11, 20, 21, 40, 41 and 60
every live particle (object ID, resource, tick, life, seed, position, velocity, momentum,
size, scale, rotation as float bits, the four colours). 47690 particle lines, at most 260
live particles at once, 0.2 s.

### What the test models, and what it only regression-checks

`test_particles.py` decodes the BREFFs itself (`fmt_effect.py` for the emitter and track
offsets, its own `EmitterDesc` reads) and reuses `test_efcurves.py`'s f32 curve model.

Modelled:

| Check | Model | Coverage |
|---|---|---|
| Coverage | one effect per emitter of each of the 13 BREFFs, each name resolved to its own `EmitterResource`, group ID from the userdata flags, pools empty after teardown | 373 effects |
| Emission | `Emitter::CalcEmitter` / `CalcEmission` / `Emission` simulated from `emitEmitStart`, `emitLife` (or `MAX_LIFE`), `emitEmitInterval`, `emitEmit` with f32 accumulation and the first-emission rule: the emitter's tick and the particles created each frame | 280 emitters with deterministic emission (no emission or interval randomness, no LOD, no emit-past, no equal-division flag, no emission curve); 4619 particles over 60 frames |
| Lifetime | life = `ptclLife` (at least 1), or within `EmitterForm::CalcLife`'s range when `ptclLifeRandom` is set; tick in [1, life]; one tick per frame with life and seed unchanged; retired exactly when tick reaches life; new P lines agree with the F counts | all 47690 particle lines, 26045 one-frame steps |
| Motion and fields | from each selected frame to the next: gravity power from the field's curve (or the block's `power`) times the decoded direction, into velocity (type 0) or position (type 1); speed field adds `(speed - 1) * velocity`; then `velocity += addVel`, `position += addPos * momentum`, `position += velocity * momentum`, all f32; bit for bit | 5487 particle steps (2256 gravity, 2138 speed evaluations), for effects with one emitter, no emitter rotation or scale and no other motion fields or TIMING-flagged ones (the field matrices are then the identity) |
| Seed independence | an emitter with a non-zero `randomSeed` never reads the system random, so another system seed must not change its effect (particle object IDs aside: pool slots depend on what earlier effects freed) | 9 effects unchanged, 357 changed |

Regression only (not modelled): initial positions and velocities (the emit forms and their
use of the emitter random), sizes, colours, rotations and texture curves as applied to
particles (the curves themselves are bit-exact in §17), random / spin / magnet fields and
TIMING-flagged tracks, child emitters and particles, emit-past and LOD. These are covered by
determinism: the same seed twice gives identical output, and the test runs the other
build's harness (`--compare`; `make test` passes the 32-bit one if it is built, and
`make M32=1 test` the 64-bit one) and requires byte-identical output.

A mutation check while writing the test (dropping the gravity direction, adding one
particle to one frame of the emission model) produced 2233 errors.

### Still open

- Drawing: the strategies are linked but their GX calls abort; Phase 7's GX layer.
- No model yet for the emit forms (initial position and velocity per form), the random,
  spin and magnet fields (the disc has no newton or vortex), child emitters, emit-past and
  the LOD ratio; they are regression-checked only.
- The effects run with an identity root matrix; transformed effects (`SetRootMtx`,
  inheritance flags) are exercised only through whatever the emitters set themselves.
- `RPSysEffectCreator` / `RPSysEffectMgr` compile on the host but are not linked (they
  reach the scene and archive managers); the harness repeats their few lines of set-up.

## 20. The nw4r::g3d scene runtime on the host: every model built and calculated

The g3d scene and model runtime links and runs on the host, without drawing: `ScnRoot`,
`ScnGroup`, `ScnMdl`, `ScnMdlSimple`, `CalcWorld` / `CalcSkinning`, `CalcMaterial` /
`CalcMaterialDirectly`, `CalcView`, the `AnmObj` units (CHR, CLR, SRT, PAT, VIS, SHP and their
blend / override forms), the DCC world-matrix rules (standard, Maya, Softimage, 3ds Max) and the
camera, light and fog objects. `mdl_harness` builds every model on the disc (177 MDL0 in 132
BRRES) the way the game does, applies every compatible animation at a set of frames and prints
the world, view and normal matrices and the material results; `test_scene.py` recomputes them
from the file bytes. 64- and 32-bit output is byte-identical. The matching build is unchanged:
`check_matching.py` compiles the 974 units that depend on a touched file, all identical
(2026-10-30).

### Changes to the decomp sources

Two, both type tokens or the same address on the Wii:

| File | Change | Why |
|---|---|---|
| `include/nw4r/g3d/res/g3d_resmdl.h` | `ResMdlInfo::GetNodeIDFromMtxID` reads the matrix-ID-to-node table through `const be_s32*` | File data. `CalcView` looks up every view matrix's node; the native read gave node IDs like 0x01000000 and `GetResNode` crashed. |
| `src/nw4r/g3d/g3d_workmem.cpp` | `GetScaleTemporary`, `GetSkinningMtxTemporary`, `GetBillboardMtxTemporary` cast `&sTemp.x[0]` instead of the array | GCC does not apply the array-to-pointer conversion before a `static_cast` from `_VEC3[]` / `_MTX34[]` to the derived `VEC3*` / `MTX34*`; CodeWarrior does. |

`check_matching.py`'s list of touched files has `g3d_workmem.cpp` (`g3d_resmdl.h` was already
there).

### What is linked, and what is stubbed

`MDL_SRCS` in the Makefile: the g3d scene units (`g3d_scnroot`, `g3d_scnobj`, `g3d_scnmdl`,
`g3d_scnmdlsmpl`, `g3d_scnproc`), the calc units (`g3d_calcworld`, `g3d_calcview`,
`g3d_calcvtx`, `g3d_calcmaterial`), the `AnmObj` units (`g3d_anmobj`, `g3d_anmchr`,
`g3d_anmclr`, `g3d_anmtexsrt`, `g3d_anmtexpat`, `g3d_anmvis`, `g3d_anmshp`, `g3d_anmscn`),
`g3d_obj`, the DCC units (`g3d_basic`, `g3d_dcc`, `g3d_maya`, `g3d_xsi`, `g3d_3dsmax`),
`g3d_workmem`, `g3d_camera`, `g3d_light`, `g3d_fog`, and the draw units `g3d_state`, `g3d_draw`,
`g3d_draw1mat1shp`, `g3d_init` (the `DRAW_OPA` / `DRAW_XLU` procs reference them); with
`math_geometry.cpp` (bounding volumes), `ut_LockedCache.cpp` and the MEM expanded heap and
allocator (`mem_expHeap.c`, `mem_heapCommon.c`, `mem_list.c`, `mem_allocator.c`). On `-m32`
they are built with `ANIM_FP` (SSE), as in §17 and §19. `AnmScnRes` (the `AnmScn` subclass
that would animate a `ScnRoot` from a SCN0) is not in Wii Play: the game reads SCN0 through
`ResAnmScn` in EGG's light manager (§17), so `g3d_anmscn.cpp` is linked but unreachable. Its
`GetLightSetting` stores an ambient colour through a native `u32*`, which would swap the
bytes on the host; nothing reaches it.

Host files:

| File | What |
|---|---|
| `src/host_g3d.cpp` | **New.** `AnmObj::Release`: declared, never defined in the decomp, and absent from the Wii binary (no symbol, no `AnmObj` vtable in `symbols.txt`): every concrete animation object overrides it. GCC emits a vtable next to the first non-inline virtual function, so the `AnmObj` constructor's vtable needs a definition. It aborts if called. |
| `src/host_gxlight.cpp` | **New.** The CPU half of `src/revolution/GX/GXLight.c` (`GXInitLight*`, `GXGetLightPos` / `Dir`, `GXInitSpecularDir`): they only fill in a `GXLightObj`, which is runtime data (native words). `G3DState`'s static light state calls them at start-up. `GXLoadLightObjImm` (the GPU half) aborts. |
| `src/host_gxtexobj.cpp` | `GXGetTexObjWidth` / `Height` / `WrapS` / `WrapT` / `LODAll`, as in `GXTexture.c`, on the big-endian words: `ApplyTexPatAnmResult` re-initialises the texture object with the old wrap and LOD settings. |
| `src/host_gx.cpp` | `GXInvalidateVtxCache` is a no-op (`ScnRoot::CalcView` calls it before computing the view matrices; nothing to invalidate). `MEMAllocFromAllocator` / `MEMFreeToAllocator` are weak, so a harness that links `mem_allocator.c` gets the real ones. |
| `src/host_gfx_stubs.c` | Aborting entries for the GX calls of the draw, light and fog paths (`GXLoadNrmMtxImm`, `GXLoadNrmMtxIndx3x3`, `GXLoadPosMtxIndx`, `GXLoadLightObjImm`, `GXSetFogRangeAdj`, `PPCSync`), g3d's direct FIFO writes (`fifo::GDSetGenMode2`, `GDSetCullMode`, `GDSetTexCoordScale2`, `GDSetIndTexMtx`, `GDResetCurrentMtx`, `GDSetCurrentMtx`, `GDLoadTexMtxImm3x3`; `platform/g3d_gpu.cpp` writes the GX FIFO and is not built) and the locked-cache SDK (`LCEnable` ... `LCQueueWait`, `OSYieldThread`). The `g3d::Camera` setters and `G3dObj::Destroy` entries are weak now (`PORT_STUB_MANGLED_WEAK`), so `mdl_harness` gets the real ones and the data harnesses keep their stubs. |

The locked cache: the game calls `g3d::G3dInit(false)` (`RPGrpRenderer`), so `ut::LC::Lock`
fails on the Wii too and `ScnMdlSimple` / `CalcView` take their main-memory paths; the harness
calls `ut::LC::Disable()` (the locked-cache part of `G3dInit`) and the LC SDK entries are never
reached.

### The harness

`tests/mdl_harness.cpp` follows the game's set-up:

- the allocator: `RPGrpModelManager::BecomeCurrent` hands the scene heap's
  `EGG::Allocator(heap)` (align 32) to `RPGrpModel` and `RPGrpModelScene`. `EGG::Heap` is the
  host's M0 stand-in (`src/host_sdk.cpp`), so the harness builds what `EGG::ExpHeap::create` and
  the allocator build underneath: a MEM expanded heap (32 MB) and
  `MEMInitAllocatorForExpHeap(allocator, heap, 32)`. After teardown the heap must be empty;
- `RPGrpModelScene`: `ScnRoot::Construct(allocator, 0x400, 0x400, 16, 8)`; camera 0 at
  (30, 120, 400) looking at (0, 40, 0) through `Camera::SetPosition` / `SetPosture`
  (look-at), current;
- `RPGrpModelG3D` (`G3D_ConstructScnObj`): `ScnMdlSimple::Construct(allocator, mdl, 1)` (the
  game's model for buffer option 0), and next to it
  `ScnMdl::Construct(allocator, mdl, ANM_MATCLR | ANM_TEXSRT | ANM_TEXPAT, 1)`, a model with
  material buffers; one of the two is in the root at a time;
- `RPGrpModelAnmG3D`: CHR through an `AnmObjChrBlend` of 8 with the `AnmObjChrRes` attached at
  slot 0, weight 1; CLR / SRT / PAT as `AnmObj*Res` set on the model directly (what the game
  does when a model has one animation of the kind); every `AnmObj*Res` constructed with
  `cache = false` and bound;
- per frame: `SetFrame`, then `ScnRoot::CalcWorld`, `CalcMaterial`, `CalcVtx`, `CalcView`.

Animations tried on a model: those of its own BRRES, then those of every BRRES without models
in the same archive (the animation files); an animation is applied when its `Bind` succeeds.
Frames: 0, 1.5, `int(n/3) + 0.25`, `int(2n/3)`, `n - 1`. With two or more CHR0 the first two
are also blended 0.5 / 0.5 (frames 1.5 and 2.25). CHR runs on the `ScnMdlSimple`, and its
world matrices and attributes must equal the `ScnMdl`'s; material runs on the `ScnMdl` (copied
buffers) and again on the `ScnMdlSimple` (`CalcMaterialDirectly` writes into the resource,
which the harness restores after each frame), and the printed results must agree.

Output per run: every position/normal matrix (`W`: matrix ID, attribute, 12 floats), every
view matrix (`V`) and its normal matrix (`N`, if the model has them); for material runs every
material's channel colours, TEV registers 1-3 and konst colours (`C`), texture SRTs, flags and
mode (`T`, the matrices that exist) or texture maps (`P`: the bound texture's name and
`image0`). 177 models, 288 animations, 1652 runs, 48000 lines; 0.1 s.

### What the test models, and what it only regression-checks

`test_scene.py` locates the records with `fmt_brres_model.py` / `fmt_brres_anim.py` and takes
the animation results from `test_anims.py`'s f32 model (§17). It models the host's arithmetic:
the paired-single forms (`port/ps_forms.h`) fuse `ps_madd` and friends (one rounding) and round
everything else to single precision; the fused steps are computed with an exact single-precision
fma (double product, sum rounded to odd, then to single).

| Check | Model | Coverage |
|---|---|---|
| Coverage | every MDL0 on the disc built; the runs (kind, animation file, name, frame) in the harness's order, from the binding rule (a CHR0 node or CLR0 / SRT0 / PAT0 material named like one of the model's) | 177 models, 1652 runs |
| World matrices | `CalcWorld`: NodeTree byte code; per node the CHR0 result (play policy applied) or `ResNode::CalcChrAnmResult`; `CalcWorldMtx_Basic` and `CalcWorldMtx_Maya_SSC_Apply` with the attribute rules; the final per-matrix scale; `CalcSkinning` (EVPMTX: world x inverse bind matrix; WEIGHT: fused weighted sum); `MTX34Mult` / `Scale` / `Trans` as their paired-single forms; bit for bit, attributes included | 14462 `W` lines |
| View matrices | `PSMTXConcatArray` (or `PSMTXConcat` for one matrix) of the printed camera matrix and the world matrix; bit for bit | 14462 `V` lines |
| Normal matrices | uniform scale all the way up: the view matrix's 3x3, bit for bit; otherwise the double-precision inverse transpose within 1e-5 of each row's largest element (`MTX34InvTranspose` divides through `fres` and two Newton steps); a singular 3x3 (zero-scaled nodes) must leave the normal matrix as it was | 13527 exact, 811 within tolerance, 12 singular |
| Camera | `C_MTXLookAt` against a double-precision look-at, within 1e-5 | 1 |
| CLR0 | `ApplyClrAnmResult`: `(old & mask) \| rgba` into the channel colours and the TEV / konst display lists (an empty register reads as opaque black), printed through the S10 getter | 1150 `C` lines (215 runs) |
| SRT0 | `ApplyTexSrtAnmResult`: the animated matrices' SRT, flag nibble and mode | 365 `T` lines (115 runs) |
| PAT0 | `ApplyTexPatAnmResult`: the animated maps take the frame's texture (name and `image0` from the TEX0 header), the others keep the texture `ResMat::Bind` gave them | 140 `P` lines (90 runs) |
| Determinism, 64/32 | the harness twice, and the other build's harness (`--compare`; `make test` passes the 32-bit one if it is built, `make M32=1 test` the 64-bit one): byte-identical | all output |

World-matrix rules the disc reaches (counted by the test): standard with an unscaled parent
(identity, translation-only and full SRT), standard with a scaled parent (identity and
translation-only), Maya segment scale compensation with an unscaled parent (all three), the
final scale (884 matrices) and skinning (7 models with NodeMix, 1323 weighted matrices). Not on
the disc: billboards, `MTXDUP`, the Softimage rule (no model uses it), Maya compensation under
a scaled parent, VIS0 and SHP0 (so `CalcVtx` has no shape animation to apply). Regression only:
the two-animation CHR blend (35 runs), the indirect texture matrices of SRT0 (not printed), and
everything not printed (texture matrices for environment mapping, bounding volumes).

A mutation check while writing the test (multiply-add not fused; the standard rule's scaled-parent
branches dropped) produced 16286 and 84 errors.

### Still open

- Drawing: `DRAW_OPA` / `DRAW_XLU`, `G3DState` and the FIFO writes are linked but abort;
  Phase 7's GX layer.
- Cross-archive pairs: 44 CHR0 (the avatar and hand animations of the Party, Pnp, Wly and Fsh
  archives) bind to models in other archives (`Common/Kokeshi`, the Mii models through
  `ScnRfl`); the harness only tries animation files of the model's own archive. Their readers
  are covered by §17.
- The game's `RPGrpModel*` layer is not linked; the harness repeats the few g3d calls it
  makes. `RPGrpModel.cpp` and `RPGrpModelManager.cpp` compile on the host;
  `RPGrpModelImplement.cpp` and `RPGrpModelAnmImplement.cpp` do not yet (GCC wants
  `template<>` on the `RPGrpModelAnmG3D` member specialisations and rejects specialisations
  after the first use, both of which CodeWarrior accepts).
- `ScnRfl` (Mii models) is compiled (§16) but not built or calculated.

## 21. The nw4r::snd runtime on a software AX mixer: sounds played and rendered

The sound runtime runs on the host and makes sound: `SoundSystem`, `SoundArchivePlayer`,
`SeqPlayer` / `MmlParser` / `SeqTrack`, `WsdPlayer`, `Channel`, `Voice`, `AxVoice`, the sound and
task threads and the AXFX effects, all decompiled code, play on a software model of the AX DSP
(`src/host_ax.cpp`). `audio_harness` sets the system up the way the game does, opens an
archive, starts sounds and renders the mixed 32 kHz stereo output. `test_audio.py` decodes every
voice's wave from the archive bytes (`dspadpcm.py`) and models the DSP: the decoded samples,
the sample-rate conversion, the envelope and the mix are bit-exact against it. 64- and 32-bit
output is identical. The matching build is unchanged: `check_matching.py` compiles the 974
units that depend on a touched file, all identical (2026-10-30).

### Changes to the decomp sources

Three, each the same object on the Wii:

| File | Change | Why |
|---|---|---|
| `src/nw4r/snd/snd_WaveFile.cpp` | `ReadWaveParam` copies the file's `AdpcmInfo` (24 `u16`) into the runtime `ChannelParam` through `be_u16` under `#if PORT_BE_WRAPPED` | The runtime computes with it (`CalcOffsetAdpcmParam` decodes from an offset; `SetAdpcm` hands the coefficients, predictor / scale and history to the DSP). §13 had left the copy big-endian. |
| `src/nw4r/snd/snd_AxManager.cpp` | `Init` takes the 256-byte zero buffer from the M0 arena under `#if defined(PORT_HOST)` | Voices that end, or start past their loop end, are pointed at it by DSP address (`GetDspAddressBySample`, `IsPlayFinished`); a static array in the host's data has no emulated address. |
| `src/revolution/AX/AXVPB.c` | `AXSetVoiceAddr` writes `GAIN_WORD(0x0800)` / `GAIN_WORD(0x0100)` instead of `0x08000000` / `0x01000000` | One `u32` store fills `gain` and `pred_scale`; on a little-endian host the halves swap, and PCM16 / PCM8 voices played silence (gain 0). On the Wii the macro gives the same constants. |

`check_matching.py`'s list of touched files has `AXVPB.c` (the other two were already there).

Integration (Phase 7 clean-up), one more, also the same object on the Wii:

| File | Change | Why |
|---|---|---|
| `include/nw4r/snd/snd_SoundStartable.h` | `StartSound` / `HoldSound` / `PrepareSound` take `unsigned long` where the Wii overload takes `unsigned int` (`#if !defined(PORT_HOST)` ... `#else`) | On the host `u32` is `unsigned int`, so the second overload had been left out; `ui32` (Pack's other 32-bit type, `unsigned long` on the host) then made `RPSndAudioMgr::startSound(SoundHandle*, ui32)` ambiguous. |

### The AX layer: decomp CPU code, host DSP

The CPU half of AX is the decomp's, linked unmodified: `AX.c` (`AXInit`), `AXAlloc.c` (voice
stacks, priorities, voice stealing), `AXVPB.c` (`AXSetVoice*`, `__AXSyncPBs`: each voice's
parameter block is copied into the PB array the DSP reads, field by field as the sync flags say,
and the DSP's results are copied back; the DSP cycle budget that drops voices), `AXAux.c` (the
three-buffer aux rings and callbacks), `AXSPB.c` (depop) and `AXProf.c`. `src/host_ax.cpp`
replaces what talks to the hardware:

- `AXOut.c`: `__AXOutInit` registers the frame as the AI DMA callback; `PortAXRunFrame` calls
  the callback chain as the AI interrupt would (so nw4r's reset hook, `AxManager::PrepareReset`,
  chains in as on the Wii). The frame is `__AXOutNewFrame` with the DSP run in place:
  `__AXSyncPBs(24400)`, `__AXPrintStudio`, the DSP, `__AXServiceCallbackStack`,
  `__AXProcessAux`, the user frame callback, then the next frame's command-list values.
- `AXCL.c`: the mode and the master / aux return volumes. The DSP command list itself is not
  built (its addresses would be host pointers); the values it would carry are captured where
  `__AXNextFrame` runs, and `__AXGetCommandListCycles` returns the same cycle count.
- AI: `AIInit`, the DMA callback registration; `OS_BUS_CLOCK_SPEED` (`src/host_os_clock.c`,
  243 MHz) for the cycle budget and the OS time macros.

The DSP, after Dolphin's AXWii HLE (`AXVoice.h`, `DSPAccelerator.cpp`), reimplemented:

| Stage | Behaviour |
|---|---|
| Accelerator | DSP-ADPCM (nibble addresses; a frame's predictor / scale byte is read at each 16-nibble boundary; `scale * nibble + ((0x400 + c1 * yn1 + c2 * yn2) >> 11)`, saturated to s16), PCM16 (word addresses) and PCM8 (byte addresses) through `gain >> 11` / `gain >> 0`; the history stays in the PB. After the sample at the end address the address returns to the loop address: a looping voice takes the loop predictor / scale and, unless it is a stream voice, the loop history; a one-shot voice stops (`state` STOP, which `__AXSyncPBs` copies back) and reads zeros for the rest of the frame. |
| SRC | `srcSelect` 0: 4-tap polyphase. The 16.16 position advances by the ratio before each output sample, input samples are read as its integer part crosses, the phase is `frac >> 9` (128 phases), the taps run oldest sample first, the sum `>> 15` is saturated. `srcSelect` 1: linear; 2: none (one input per output). nw4r uses only the polyphase filter (`SRC_4TAP_AUTO`: 8 kHz table above a ratio of 4/3, 12 kHz above 1, 16 kHz otherwise). |
| SRC tables | The DSP reads them from its coefficient ROM. The 12 kHz table is in the decomp: `AXFXSrcCoef.c` (AXFX's chorus uses it on the CPU) as floats that are exactly the ROM's s16 values / 32768; `host_ax.cpp` builds the table from it. The 8 and 16 kHz tables are not in the decomp: `src/host_ax_coefs.h` is generated by `tools/gen_ax_coefs.py` the way Dolphin's free DSP ROM generates its table (a windowed sinc, Hamming window, cutoff 0.5 and 1.0 over 512 points split into 128 phases). They are approximations: the same construction for the 12 kHz table (Kaiser window, cutoff 0.75) is within 19/32768 of the real one. |
| Envelope, filters | `x = sat16((x * ve.currentVolume) >> 15)`, the volume moving by `currentDelta` each sample; the one-pole LPF and the Wii biquad when on. |
| Mix | Per bus bit of `mixerCtrl` (main L/R/S, aux A/B/C L/R/S): `s = sat16((x * v) >> 15)` added to the bus, `v` moving by its delta when the bus's ramp bit is set; the last `s` is the bus's depop value. |
| Frame | The buses start from the depop fade of voices stopped this frame (`__AXPrintStudio`); last frame's surround bus is added to L and R; each aux bus with a callback is written for the CPU (AXFX, through `__AXProcessAux`) and the bus processed earlier in the ring comes back into L/R/S times its return volume; the output is `(main * masterVolume) >> 15` clamped to +-32767, stereo s16. |

Not emulated: the compressor (a full-scale limiter; its table is not in the decomp, and Dolphin's
HLE skips it too), DPL2 / surround encoding (the output is stereo; `SCGetSoundMode` says
stereo), the Wii Remote speaker outputs (`AXRmt*` report no samples), the ITD phase shift.
PCM8 is implemented but no wave on the disc uses it.

### Threads

`src/host_thread.cpp` runs OS threads as coroutines (`ucontext`) on one host thread, so runs are
reproducible: a switch happens only where the Wii would reschedule (a thread blocks on a message
queue, `OSSleepThread`, a mutex or `OSJoinThread`, or a thread of higher priority becomes
ready). `PortOSEnterInterrupt` / `PortOSLeaveInterrupt` (around the AX frame) defer the switch to
the end of the "interrupt"; `PortOSIdle` is the main thread waiting for the next retrace: every
ready thread runs until all of them block. The sound thread (woken by the AX frame callback
through its message queue) runs the frame's sequence ticks, channel and voice updates, then
blocks again. Threads get 1 MB host stacks (the decomp's stacks are sized for the PowerPC); the
main thread is priority 16. Mutexes are recursive. Its `OSMutex` / `OSMessageQueue` /
`OSGetCurrentThread` replace the single-threaded ones in `src/host_sdk.cpp`, which are now weak.

Dead stand-ins (Phase 7 clean-up): every harness was relinked with `--gc-sections` and a link
map, and a stand-in whose section no link keeps was removed: from `host_snd_stubs.cpp` the AI /
AX callback, aux, volume and mode calls and `OSCreateThread` / `OSResumeThread` /
`OSJoinThread` / `OSJamMessage` (the loader harnesses never initialise the sound system); the
weak `DVDOpen` in `host_sdk.cpp` (every user has `dvdfs.c`'s or opens by entry number);
`LCEnable` in `host_gfx_stubs.c`. `snd_harness` now links with `--gc-sections` like the
others (without it, the unused `SoundThread` / `AxManager` functions in the library still
referenced the removed calls). The weak `OSEnableInterrupts` in `host_os.cpp` is also unkept
but is part of the platform layer's interface, so it stays.

### What is linked, and what is stubbed

`AUDIO_SRCS` in the Makefile: all of `src/nw4r/snd`, the `ut` file streams, `src/egg/audio`
(the EGG audio managers), the AX units above, all of `src/revolution/AXFX`, the MEM frame heap
(`snd::FrameHeap`); on `-m32` with `ANIM_FP` (SSE), as in §17. Host files:

| File | What |
|---|---|
| `src/host_ax.cpp` | **New.** The DSP, the `AXOut` / `AXCL` / AI replacements, `PortAXRunFrame`, a voice tap (`PortAXSetVoiceTap`: each running voice's PB before and after the frame, its decoded input, SRC output and final output) and a test switch that turns SRC off (`PortAXSetSrcOverride`). |
| `src/host_ax_coefs.h`, `tools/gen_ax_coefs.py` | **New.** The generated 8 / 16 kHz SRC tables (`--check` verifies the header). |
| `src/host_thread.cpp` | **New.** The threads. |
| `src/host_os_clock.c` | **New.** `OS_BUS_CLOCK_SPEED` (every unit that includes `OSHardware.h` gets a weak zero definition on the host; this strong one is in a file that does not). |
| `src/host_snd.cpp`, `src/host_snd_stubs.cpp` | `host_snd.cpp` keeps the interrupts, `OSGetTick` (a call counter), DVD cancel, NAND and CNT; its AX / AI / thread stubs moved to `host_snd_stubs.cpp`, which the loader harnesses (`snd_harness`, `hbm_harness`) still link. After the Phase 7 clean-up `host_snd_stubs.cpp` has only what their links keep: `AXAcquireVoice` / `AXFreeVoice` / `AXSetVoiceAddr` / `Priority` / `State` and the thread-queue calls (below). |
| `src/host_sdk.cpp` | The mutex, message-queue and current-thread functions are weak. |
| `tests/audio_host.cpp` | **New.** Alarms that never fire (only the remote speaker uses them, and the host AX produces no remote speaker samples), `OSGetTime`, `OSRegisterVersion`, and the static members the decomp defines in units it does not have yet (`AudioSound3DActor`'s, `RPSndAudioActorBase::sActorList`, `AudioSoundActorBaseWithCamera<4>::smCommon3DManager`). Its SC, WPAD / WENC, OS heap and `EGG::Allocator` / `ExpHeap::create` stand-ins are gone since the integration below. |

### The harness

`tests/audio_harness.cpp` runs the game's own set-up, `RPSndAudioMgr` included, on the §22 EGG
heaps (since the Phase 7 integration; before it, a `HarnessAudioMgr` repeated
`RPSndAudioMgr::initialize`'s lines on the M0 arena and created the effects itself):

- boot: `PortOSInit`, `EGG::ConfigurationData::initMemory` (root heaps, the 0x600000 system heap),
  `WPADInit` (the §23 backend; KPADInit's in the game); then `RPSysSystem::initialize`'s
  `RPSysProjectLocal` / `RPSndAudioMgr` / `RPSndSpeakerMgr::CreateInstance` on the system heap;
- `RPSysSystem::createEffectWork`'s lines: the MEM1 root heap current,
  `RPSndAudioMgr::initialize` with a 0xB00000 sound heap from it: `ExpAudioMgr::initialize`
  (`AIInit`, `AXInit`, `SoundSystem::InitSoundSystem`, which starts the sound and task threads,
  the 3D managers), `SoundHeapMgr::createSoundHeap`, `AudioFxMgr::initializeFx` with the Party
  Pack aux heap sizes (the fx heaps are real `EGG::ExpHeap`s now), the static sound archive
  (`RPSndStaticMgr::setupArchive`), the BGM volume parameter, the speaker master volume
  (`WPADGetSpeakerVolume`); the MEM2 root heap is current afterwards, as at the end of the
  game's boot (the harness's own containers go there: `operator new` is `EGG::Heap::alloc`);
- `--fx`: `RPSndAudioMgr::setDefaultFx` itself (`configureTitle` calls it): reverb
  (`FxReverbHi`) on aux A, chorus on B, delay on C, created by `AudioFx` on its heaps;
- `RPSndAudioMgr::openArchiveRP(path, NULL, NULL)` (default storage: DVD), every group loaded
  with `ArcPlayer::loadGroup`;
- the clock: AX frames of 96 samples back to back; before the first AX frame that starts at or
  after `floor(k * 32000 / 60)` samples, game frame k: the sounds due at k are started
  (`RPSndAudioMgr::startSound`), `RPSndAudioMgr::calc` (the arc volume, `SimpleAudioMgr::calc`,
  the speaker and static managers), then `PortOSIdle`.

`RPSndAudioMgr` links with `RPSndStaticMgr`, `RPSndSpeakerMgr`, `RPSndMoveParam`, `RPSndUtility`,
`RPSysProjectLocal`, and, for its two controller queries (`RPSysCoreControllerMgr::isDpdCtrlEnable`,
`CoreControllerMgr::getNthController` in the speaker manager), `eggController.cpp`,
`RPSysControllerMgr.cpp` and KPAD; the remote speaker code runs on the §23 WPAD backend and the
decomp's WENC. The expected audio did not change: every scenario renders the same samples and
the same voice events as with the stand-ins (`audio_expected.txt` only gained the stream
scenario of §24).

```
audio_harness --disc DIR --list ARCHIVE
audio_harness --disc DIR [--frames N] [--wav PATH] [--tap PATH] [--fx] [--src-none] ARCHIVE SOUND[@F] ...
audio_harness --disc DIR [--frames N] [--wav PATH] [--tap PATH] --strm FILE.brstm      (§24)
```

It prints a line per started sound (`S`), the file whose waves it plays with that file's wave
data address (`I`), each voice that starts a note (`N`: AX frame, voice, format, start / loop /
end address relative to the file's wave data, ratio, table, envelope volume, mixer control,
L/R volume), voices that stop (`E`), the running-voice count (`C`) and the render (`R`: RMS,
peaks, FNV-1a of the samples). The WAV goes where `--wav` says (renders are user data: outside
the repository).

### What the test models, and what it only regression-checks

`test_audio.py` (`make audio`, part of `make test`) runs 11 scenarios, 25 s:

| Check | Model | Coverage |
|---|---|---|
| Voice identity | each run of a voice (from an `N` to the next `N` / end) starts at sample 0 of a wave of the sound's RWSD or its bank's RBNK (`fmt_sound.py` offsets), in that wave's format | 333 runs, all identified |
| Decode | the accelerator output equals `dspadpcm.py`'s decode of that wave channel, the loop section repeated for looping waves, then zeros in the frame where a one-shot wave ends (and the voice is stopped there) | 333 runs, 15.3 M samples bit for bit: DSP-ADPCM and PCM16, one-shot and looping (`cow-wave-loop` crosses its loop) |
| SRC | the polyphase filter over the expected samples with each frame's ratio and table (`gen_ax_coefs.tables`: the decomp's 12 kHz table and the generated ones), from the PB's history and fraction | 333 runs bit for bit; ratios 1.0, 0.9, 1.378 and the sequences' pitches; all three tables |
| Envelope | `sat16((x * v) >> 15)`, `v` stepping by the delta; the LPF when on | 333 runs bit for bit |
| No SRC | with `--src-none` the SRC output is the decode itself, so the voice output is the decode times its envelope | `bil-wave-native` |
| Mix | the rendered L/R from the tapped voices' mix volumes, deltas and ramp bits through the master volume | 4000 frames of the single-wave renders without effects, bit for bit |
| Regression | the `N` / `E` / `C` lines (which voices start and stop when) and the samples' hash per scenario, against `tests/audio_expected.txt` (`--update` rewrites it) | 11 scenarios: 6 wave-sound, 5 sequence (up to 77 voices), 4 with the effects |
| Determinism | each scenario twice (output and taps identical); the other build's harness (`--compare`; `make test` passes the 32-bit one if built, `make M32=1 test` the 64-bit one): identical lines (only the `I` lines' heap addresses differ) and identical samples | all scenarios |

For a single wave sound at its native rate (`RP_BIL_SE_TURN_DECIDE`, 32 kHz, ratio 1.0, centre
pan) the accelerator output equals `dspadpcm.py` exactly. The voice output is not the decode
itself even then: nw4r always selects the polyphase filter, which at ratio 1.0 is still a
4-tap low-pass (16 kHz table) with two samples of delay, and the envelope scales by at most
32767/32768. With the SRC switched off the output is exactly `sat16((decode * v) >> 15)`.

A mutation check while writing the test (ADPCM rounding constant 0x3FF instead of 0x400; the
filter taps reversed) produced 645 and 342 errors.

Renders (`--wav-dir`, 64 / 32 identical):

| Scenario | Length | Notes | Voices (max) | RMS | Peak L / R |
|---|---|---|---|---|---|
| `bil-wave-native` (`RP_BIL_SE_TURN_DECIDE`) | 0.66 s | 1 | 1 | 1704 | 19667 / 19667 |
| `bom-wave-pan` (`RP_BOM_SE_GENTEN_1`, panned) | 0.42 s | 1 | 1 | 1943 | 29175 / 8023 |
| `cow-wave-loop` (sound 148, 44.1 kHz stereo, looping) | 9.9 s | 2 | 2 | 1309 | 12767 / 12403 |
| `common-seqs` (party menu BGM and four SEs) | 6.0 s | 117 | 33 | 1266 | 13898 / 14295 |
| `duc-seqs-fx` (Shooting Range BGM, combo, fanfare; effects) | 9.0 s | 185 | 77 | 2296 | 21799 / 21119 |
| `common-bgm-fx` (`RP_COMMON_BGM_PARTY_MENU`, effects) | 18 s | 314 | 31 | 1051 | 7484 / 7177 |

### Streams

No archive has a stream sound: `audio_harness --list` over the 10 disc-level archives gives
1079 SEQ and 23 WAVE sounds (the two `*_STRM` labels in `rp_Hky_sound.brsar` are wave sounds),
and ASSETS.md (§1.5, §2.6) finds no STRM sound or external file entry in any BRSAR, the HOME
Menu's included: the 14 `.brstm` files on the disc are unreferenced, so the game never starts a
`StrmPlayer`. §24 converts its file structs and plays one through `StrmPlayer` directly.

### Still open

- An audio device: `PortAXRunFrame` produces the samples; the PC platform layer has to call it
  every 3 ms of output (and drive `PortOSIdle` from its frame loop).
- The 8 / 16 kHz SRC tables are approximations until the real ROM values are known (the 12 kHz
  one is exact).
- The compressor, DPL2 output, the remote speaker and the ITD shift (see above).
- `RPSndAudioMgr`'s scene-side paths (`changeScene`, `attachArchive` through
  `RPSysResourceManager`'s sound paths, the pause / fanfare logic) are linked but not driven;
  the harness runs set-up, `openArchiveRP`, `startSound`, `setDefaultFx` and `calc`.
- `OSGetTick` / `OSGetTime` count calls; nothing in the sound path depends on their rate yet.

## 22. The OS / storage platform layer: EGG heaps, DVD, NAND and saves, SC

The game's memory and storage code runs on the host the way it runs on the Wii. The real EGG
heap stack (`EGG::Heap`, `ExpHeap`, `FrmHeap`, `Allocator`, the disposers) over the decomp's
MEM heaps and `OSAlloc` / `OSArena` manages MEM1 / MEM2 arenas inside M0; the decomp's DVD file
system (`dvdfs.c`, `dvdqueue.c`) runs on a host drive backed by the extracted disc; NAND is a
host directory and SC a small config file. On this layer `sys_harness` boots the way
`RPSysSystem` does and runs the game's own `RPSysResourceManager` (every scene's archives,
byte-identical to `wiiarc.py`) and `RPSysSaveDataMgr` (the save and banner files written on the
NAND task thread, read back, and found again by a second run). 64- and 32-bit output is the
same apart from heap addresses, and the save files are byte-identical. The matching build is
unchanged: `check_matching.py` compiles the 974 units that depend on a touched file, all
identical (2026-10-30).

### Changes to the decomp sources

Six, each the same object on the Wii (checked one by one as well):

| File | Change | Why |
|---|---|---|
| `src/revolution/DVD/dvdfs.c` | `entryToPath` reads the name offset with `DVDNodeGetName(FstStart[entrynum])` (as `DVDConvertPathToEntrynum` already does) instead of the `name:24` bitfield | The FST words are in host order on the host; GCC puts a `u32` bitfield's first member in the low bits on little-endian, so `.name` read the wrong 24 bits. |
| `src/egg/core/eggStream.cpp` | Binary `read_u16` / `s16` / `u32` / `s32` / `float` read into `be_*` locals; the writes pass `&BE_STORED(T, value)`; `#include <port/be.h>` takes the place of a blank line | The save file is a file: `RPSysSaveData` streams it through `EGG::RamStream`, which copied native bytes. The line count stays the same because `DECOMP_FORCEACTIVE` puts `__LINE__` into symbol names (an earlier `#if` block changed `eggStream_cpp214` to `219` and broke the match). |
| `include/port/be.h` (Wii) and `port/host/include/port/be.h` | New `BE_STORED(T, v)`: `(v)` on the Wii; on the host a big-endian temporary that lives until the end of the full expression (`port::be_stored`) | For code that writes a value's bytes out. |
| `src/Pack/RPSystem/RPSysSaveData.cpp` | `calcCrc` reads the buffer as `const be_u16*` | The checksum is over big-endian halfwords; the host now computes the Wii's value. |
| `include/revolution/NAND/nand.h` | `NANDBanner::magic`, `flags`, `iconSpeed` are `be_u32` / `be_u16` | `banner.bin` is written straight from the struct (`RPSysSaveDataMgr::initBanner` sets the icon speed through it). The title strings stay `wchar_t`; the host `NANDInitBanner` stores them as UTF-16BE. |
| `src/Pack/RPKernel/RPSysSystem.cpp` | `initialize` rounds the arena bounds through `reinterpret_cast<uintptr_t>` instead of `<u32>` (4 lines) | 64-bit compile error; `uintptr_t` is MSL's `unsigned long`, i.e. `u32`, on the Wii. `initialize` itself is not run on the host yet (its second half sets up GX / VI). |

`check_matching.py`'s list of touched files has all six (`be.h` was there).

### The platform layer

| File | What |
|---|---|
| `src/host_os.cpp` | **New.** `PortOSInit`: the boot information at 0x80000000 (host layout; `physMemSize` is at 0x28 in both) and the arenas through the decomp's `OSSetMEM1ArenaLo` etc. (`OSArena.c`). The arena ranges are taken out of the host allocator (`PortArenaReserve`, new in `host_arena.cpp`), so arena memory and `PortArenaAlloc` memory never overlap. Weak `OSDisableInterrupts` / `OSRestoreInterrupts` / `OSEnableInterrupts`. |
| `src/host_dvd.cpp` | **New.** The drive under `dvdfs.c`: `PortDVDSetDisc`, `DVDInit`, `DVDReadAbsAsyncPrio`, `DVDCheckDiskAsync`, `DVDCancel` / `DVDCancelAsync`, `DVDGetCommandBlockStatus`, `DVDGetDriveStatus`, `DVDGetCurrentDiskID`, `__DVDLayoutFormat`. |
| `src/host_nand.cpp` | **New.** NAND over a directory: `PortNANDSetRoot`, `NANDInit`, open / read / write / seek / length / close and their async and private / safe forms, create / delete / type / status / move / check, home and current directory, `NANDInitBanner`. |
| `src/host_sc.cpp` | **New.** `PortSCLoadConfig` and the `SCGet*` / `SCSet*` the game and its libraries call. |
| `src/host_thread.cpp` | Devices (`PortOSAddDevice`): when every thread is blocked, or the main thread idles in `PortOSIdle`, each device finishes its queued work and calls its callbacks in interrupt context. `OSSetSwitchThreadCallback` (EGG's `Thread::switchThreadCallback` runs on every switch), `OSCancelThread`, `OSDetachThread`. The thread table is plain `malloc` memory now: with the real `eggHeap.cpp` linked, `operator new` is `EGG::Heap::alloc` and there may be no heap yet. |
| `src/host_sdk.cpp` | The DVD functions, `EGG::Heap::alloc` / `free` / `findContainHeap` / `sCurrentHeap` and the global `operator new` / `delete` stand-ins are weak; the sized deletes forward to the unsized ones (which are EGG's when it is linked). The older harnesses keep using them. |
| `include/port/host.h` | `PortOSInit`, `PortDVDSetDisc`, `PortDVDGetFst`, `PortNANDSetRoot`, `PortSCLoadConfig`, `PortOSAddDevice`. |

Start-up order, before the game's own init (`RPSysSystem::initialize`: `DVDInit`, the heaps):
`PortOSInit()`, `PortDVDSetDisc(files, fst)`, `PortNANDSetRoot(dir)`, `PortSCLoadConfig(path)`.
All paths are arguments; nothing in the layer has a built-in path.

### Memory

| Range | What |
|---|---|
| 0x80000000 - 0x80003100 | OS globals (never allocated; the boot information) |
| 0x80003100 - 0x80500000 | stands for the DOL (the game's code and data end near 0x8044C000); host allocator |
| 0x80500000 - FST | MEM1 arena: OSInitAlloc's heap table, then the MEM1 root heap (19.0 MB) |
| FST - 0x81800000 | the FST (3212 bytes), placed by `PortDVDSetDisc` as the apploader does |
| 0x90000000 - 0x90000800 | reserved, as on the Wii |
| 0x90000800 - 0x933E0000 | MEM2 arena: the MEM2 root heap (51.9 MB; under 64 MB, so no debug heap) |
| 0x933E0000 - 0x94000000 | IOS's part of MEM2; host allocator |

`EGG::ConfigurationData::initMemory` (the decomp's EGG system code, run on the game's own
`RPSysSystem::sConfigData` with the 0x600000 system heap `RPSysSystem::initialize` uses) builds the
root heaps and the system heap and hands the whole arenas to them, exactly as on the Wii;
`RPSysSystem::setup`'s heaps follow (the 24 MiB resource `FrmHeap` and the 0x100-byte assert heap
from MEM2, the 0x80000 reserve heap from MEM1).

The heaps' bookkeeping is host-sized: `EGG::ExpHeap` is 104 bytes and `MEMiHeapHead` 120 on
64-bit (about half on `-m32`), and every MEM block header has two pointers. Two consequences:

- `RPSysSystem::setup`'s assert heap (`ExpHeap::create(0x100, ...)`) did not fit on 64-bit
  (`create` returned NULL and `setup` would call `disableAllocation` on it). Decision
  (2026-10-30): the call reads `PORT_HOST_SIZE(0x100, 0x400)`, which is 0x100 on the Wii (the
  object is unchanged) and 0x400 on every host build (`include/port/be.h`).
- Heaps the game sizes tightly lose more to headers than on the Wii. Nothing on the paths tested
  here runs out, but the scenes' heap budgets are untested.

### DVD

- **FST.** `sys/fst.bin` next to the `files/` directory (Dolphin's "Extract Entire Disc" and wit
  both produce this layout), or a given path. If there is none, the FST is built from the tree
  in the mastering tools' order: each directory's entries sorted by name compared as upper case,
  files and directories mixed, depth first. `test_sys.py` checks that this gives the disc's entry
  numbers for all 126 paths (59 files, 67 directories); a case-sensitive sort already moves 125.
- **Reads.** `dvdfs.c` turns a file read into `DVDReadAbsAsyncPrio(offset in words)`. The drive
  finds the extracted file whose FST extent contains the disc address and reads it; bytes past
  the end of the file are zero (the disc would return whatever follows).
- **Timing.** A command waits in the decomp's priority queue until the CPU waits: when every
  thread is blocked (`DVDReadPrio`'s `OSSleepThread`, `EGG::DvdFile::sync`'s message queue) or
  the main thread idles. Then the drive completes all queued commands in queue order and calls
  their callbacks in interrupt context, so the woken threads run afterwards, as on the Wii. A
  callback is never called before the call that queued it returns. Busy-polling
  `DVDGetCommandBlockStatus` without waiting would not see progress (nothing in the game does).
- **Cancel.** A waiting command is taken off the queue, its callback gets
  `DVD_RESULT_CANCELED` and the cancel callback `DVD_RESULT_GOOD`; a finished one only gets the
  cancel callback. `DVDCheckDiskAsync` completes with TRUE (the disc is always there).

### NAND and the save files

A NAND path maps to `<root><path>`; relative paths are relative to the title's home directory,
`/title/00010000/52484145/data` (from the disc ID), which `PortNANDSetRoot` creates with `/tmp`.
`.` and `..` components are refused. Results follow the SDK: `NANDOpen` does not truncate,
`NANDCreate` of an existing file is `NAND_RESULT_EXISTS`, missing files are
`NAND_RESULT_NOEXISTS`, `NANDMove` moves a file into a directory, `NANDCheck` always has room.
Async calls do the work at once and deliver the callback when the NAND device runs (the same
rule as the DVD). `NANDPrivateSafeOpenAsync` / `NANDSafeCloseAsync` are a plain open and close.

The game's save path runs unmodified: `RPSysSaveDataMgr::CreateInstance` on the MEM2 root heap,
`initBanner` (the title and subtitle are `party_message.bmg` messages 1001 / 1002, the images
`banner.tpl`'s two textures), then `existPackFileAsync`, `existNandMemoryAsync`,
`createPackFileAsync`, `loadAsync` and `saveAsync`, each a request to the NAND
`EGG::TaskThread` (`RPSysSystem::createFromSystemHeap`'s, priority 18), finished the way
`RPSysScene::updateNandEndMessage` does once a frame. The files it writes:

- `RPParty.dat`, 32768 bytes: `RPPT`, `0000`, the system data, 100 players, the checksum, all
  big-endian through `EGG::Stream` (20644 bytes; the rest is the heap buffer's content).
- `banner.bin`, 29344 bytes (`NAND_BANNER_SIZE(1)`): `WIBN`, flags 0, icon speed 2, the UTF-16BE
  title and subtitle, the 192x64 banner and one 48x48 icon frame.

Both have the Wii's layout, so a save from Dolphin's NAND (`title/00010000/52484145/data`)
should load unchanged (not tried yet: no such save is at hand).

### SC

`PortSCLoadConfig(path)` reads `key = value` lines (`#` comments): `language` (`jp en de fr es
it nl zh-s zh-t kr` or 0-9), `aspect` (`4:3`, `16:9`), `progressive`, `eurgb60`, `sound`
(`mono stereo surround`), `sensorbar` (`bottom top`), `sensitivity` (1-5), `rumble`, `speaker`
(0-127), `screensaver`, `offset` (-32..32). Defaults: English, 4:3, interlaced, 60 Hz, stereo,
sensor bar below, sensitivity 3, rumble on, speaker 85, screen saver on. 4:3 because no
aspect-ratio choice is recorded in the port's docs; 16:9 is one line. A bad line is an error
(the harness exits with status 2). `SCFlushAsync` keeps changes in memory. The decomp's
`scapi.c` / `scsystem.c` are not used: they parse SYSCONF from the NAND with native reads.

### What is linked, and what is stubbed

`SYS_SRCS` in the Makefile: `eggHeap`, `eggExpHeap`, `eggFrmHeap`, `eggAllocator`, `eggStream`,
`eggThread`, `eggTaskThread`, `eggSystem`, `eggScene`, `eggSceneManager`; the MEM expanded and
frame heaps, `mem_allocator.c`; `OSArena.c`, `OSAlloc.c`; `dvdfs.c`, `dvdqueue.c`;
`RPSysResourceManager`, `RPSysSaveDataMgr`, `RPSysSaveData`, the party / sports data units,
`RPSysSceneCreator`, `RPSysProjectLocal`, `RPSysSystem`, `RPSysSystem_2`, `RPSysFile`,
`IRPSysHostIOSocket`; with `RPSysMessage` (`tests/rpsysmessage_unit.cpp`), linked with
`--gc-sections`. `PACK_PARTY` is now defined for the whole host build, as for the game's units in
the matching build (no unit linked before depended on it).

`tests/sys_host.c` has what the kept code references but the harness never reaches, each
aborting with its name: the scene classes `RPSysSceneCreator::create` builds (it is virtual, so
its vtable keeps it; the scene vtables are zero placeholders), the system window and controller
calls on the save manager's error paths, `OSReturnToMenu` / `OSShutdownSystem` and two VI calls.
`VFIsAvailable` answers "no VF", so `RPSysSaveDataMgr`'s constructor skips `VFInitEx`.

### The harness

`tests/sys_harness.cpp` follows the game's start-up: the platform calls, then
`EGG::BaseSystem::configure(&RPSysSystem::sConfigData)`, `DVDInit`, `NANDInit`, `SCInit`,
`initMemory`, `EGG::Thread::initialize` with the main thread wrapped, `EGG::DvdFile::initialize`,
the MEM2 root heap current; `RPSysSystem::create` and the parts of `setup` /
`createFromSystemHeap` the tested code reads (the resource / assert / reserve heaps,
`RPSysSceneCreator`, `RPSysProjectLocal`, `setupLocalSettings`, `RPSysResourceManager`, the NAND
and DVD task threads and their end queue). Private members are set through explicit template
instantiation (which may name them), so no header changes for the test. `RPSysSceneMgr` /
`RPSysScene` are not linked yet: the harness defines `getCurrentSceneRP` (an `EGG::Scene` built
on a scene heap) and `isNandAccessEnable` (true). `RPSysConfigData::mDisplay` is a zeroed
stand-in (the `RPSysSystem` constructor sets its frame rate). The message archive's BMG text is
swapped once with `PortSwapBmgText` (§6) before `initBanner` reads it.

```
sys_harness --disc FILES --nand DIR [--sc FILE] [--fst PATH | --fst-tree] [--list PATHS]
            [--sections heap,dvd,res,save,sc] [--save-values SEED]
```

Sections: **heap** (arena bounds, the heap tree, expanded-heap allocations at nine alignments
head and tail, frees through `EGG::Heap::free` / `MEMFindContainHeap` and directly, the frame
heap from both ends, `EGG::Allocator` and `MEMAllocFromAllocator`, `operator new[]` on the
current heap, placement new on a heap, a disposer run by `destroy`); **dvd** (every path of the
list: `DVDConvertPathToEntrynum`, `DVDOpen` and `DVDFastOpen`, `DVDReadPrio` in 32 KB chunks
hashed; failing, case-insensitive and `..` lookups; three async reads, one cancelled; a
blocking message-queue wait; a read from a second thread); **res** (`LoadCacheArchives`,
`LoadMessageArchive`, `LoadFontArchive`, `LoadKokeshiArchive`, `LoadStaticCommonArchive` /
`LoadStaticLocalArchive`, then for each of the 11 scenes `LoadGameCommonArchive` /
`LoadGameLocalArchive` on a scene heap, which is then destroyed and must take its files out of
the list; every `RPSysFile` of the cache and file lists is printed with every file of its archive);
**save** (above, with seeded values); **sc** (every getter, `setupLocalSettings`).

### What the test checks

`tests/test_sys.py` (`make platform`, part of `make test`; about 30 s) wipes and uses a
scratch directory (`SYS_SCRATCH`) for the path list, the NAND directories and the configs:

| Check | Model | Coverage |
|---|---|---|
| Heaps | the arenas fully handed to the root heaps; root heaps inside their arenas; each child heap inside its parent with the requested block size and kind; the MEM2 root heap current; the harness's allocation checks | 5 heaps (6 on `-m32`, with the assert heap) |
| FST | `sys/fst.bin` parsed in the test: every path's entry number; files' sizes and FNV-1a against the extracted files; directories refuse `DVDFastOpen` | 126 paths |
| FST from the tree | `--fst-tree` must give the same entry numbers | 126 paths |
| Async | status WAITING before the CPU waits, no callback before; cancel first, then the reads in order; the bytes; the blocking wait; the other thread's read | 1 scenario each |
| Resources | cached files as read from the disc rounded to 32 bytes; expanded archives as `wiiarc.yaz0`; each archive's files, sizes and FNV-1a as `wiiarc.u8` lists them, in order; `GetMessageResource`; `IsExist` | 24 cached, 27 expanded, 384 archive files |
| Save | the steps on a fresh NAND (exist, memory, create, load, save, reload) and on the same NAND again (exist, load, save, reload); `RPParty.dat` decoded in the test, every printed value and the checksum; `banner.bin` header, text against `fmt_text_font.bmg_messages`, images against `banner.tpl`; the second run loads the first run's save; `/tmp` empty after the move | 2 runs |
| SC | the defaults; a config with every key; a bad value fails | 3 runs |
| 64 / 32 | `--compare`: the other build's lines (heap addresses aside) and save files byte-identical | all but the heap lines |

A mutation check while writing the test (the tree FST sorted case-sensitively) produced 125
errors.

### Still open

- The rest of `RPSysSystem::initialize` / `setup` (GraphicsFifo, video, XFB, display, the
  exception handler, the scene manager): with the GX / VI layer.
- Heap budgets with 64-bit headers in real scenes.
- `RPSysSceneMgr` / `RPSysScene` (the harness stands in for the two calls the save code makes).
- The older loader harnesses keep their arena allocators; `audio_harness` runs on this layer's
  EGG heaps since the Phase 7 integration (§21).
- The Mii database on the NAND (`/shared2/menu/FaceLib/RFL_DB.dat`, read through RFL's
  private NAND calls): done in §40. Loading a save taken from Dolphin.
- `EGG::UnitHeap` is not in Wii Play (no `eggUnitHeap.cpp`, no MEM unit heap in the binary).
- The BMG swap for the message archive belongs in the Phase 7 loader (§6).

## 23. The input platform layer: a host WPAD under the real KPAD and the game's controller code

Input follows the decision in `docs/PLAN.md` (mouse + gamepad first, real Wii Remotes later). The
game's own read path runs unmodified: the decomp's KPAD (`src/revolution/KPAD/KPAD.c`, matching)
and the game's controller classes sit on a host WPAD backend with four virtual remotes; a mapping
layer turns a pointer position, buttons, a tilt and a stick into what a remote would report, so
that KPAD's own math gives back the requested pointer position. No device I/O yet: tests feed a
scripted input file, and `PortInputState` is the entry point a window backend (SDL) will fill.
`check_matching.py`: no decomp file changed; 974 units depending on touched files, all identical
(2026-10-30).

### How the game reads input

| Layer | Unit | What it does |
|---|---|---|
| WPAD | `WPAD.c`, `WPADHIDParser.c`, `WPADEncrypt.c`, `WPADMem.c` (decompiled) | Bluetooth (WUD / BTE) reports to `WPADStatus`: buttons, accelerometer counts minus the calibration zero, the camera's dots (y flipped, missing dots `x 0, y 767, size 0`). Replaced on the host (below). |
| KPAD | `KPAD.c` (decompiled, linked unmodified) | `KPADiSamplingCallback` (WPAD's sampling callback, every report) buffers 16 reports and switches the DPD mode / data format for the device; `KPADRead` turns them into `KPADStatus`: buttons with repeat, acc in g, the pointer (`pos`), horizon, distance, `acc_vertical`, the Nunchuk stick and acc. Uses 13 WPAD calls: `WPADInit`, `GetStatus`, `Probe`, `Read`, `SetSamplingCallback`, `Get/SetDataFormat`, `ControlDpd`, `IsDpdEnabled`, `ControlMotor`, `GetAccGravityUnit`, `GetDpdSensitivity`, `GetSensorBarPosition`. |
| EGG | `eggController.cpp` | `CoreControllerMgr` (WPAD allocator heap, `KPADInit`, connect callbacks, extension changes in `endFrame`), `CoreController::beginFrame` (`KPADRead` into 16 `CoreStatus`, the Nunchuk stick as buttons `1 << 16..19`, the posture matrix), `ControllerRumbleMgr` / `Unit` (rumble patterns, `WPADControlMotor`). |
| Game | `RPSysController.cpp`, `RPSysControllerMgr.cpp`, `RPSysPairingMgr.cpp` | `RPSysCoreController::initialize` (sensor height, button repeat 1/1.5 s then 1/12 s, a rumble manager each), `applySetting` (sensor height 0.15 with the bar above the TV, -0.35 below), per frame the KPAD parameters from its stacks (pointer play radius 0.06, sensitivity 1; the others 0 / 1), DPD on / off, cursor auto-repeat, the extended acc filters. `RPSysCoreControllerMgr::beginFrame`: newly connected remotes are matched to controllers by channel or address (`WPADGetAddress`), rumble `"***-***---*"`, the connect sound; disconnects reset the player data. |
| Cursor | `RPSysCursorDrawMgr::preUpdate` | `pos` clamped to -1..1, then `ConvertFromNormalCC(pos.x, -pos.y)` on the cursor screen (an `RPGrpScreen` in centre-canvas mode, `RPSysCommonObject`; 608 x 456 in 4:3). |

Other WPAD users, not on this path yet: the remote speaker (`RPSndSpeakerMgr`,
`nw4r::snd::RemoteSpeaker`: `WPADControlSpeaker`, `WPADSendStreamData`), the HOME Menu
(`HBMController`, `HBMRemoteSpk`, `HBMBase`: info, motor, speaker, sync, disconnect) and RFL
(`WPADReadFaceData`: Miis stored on a remote).

### Files

| File | What |
|---|---|
| `include/port/input.h` | **New.** The backend (`PortWPADRemote`, `PortWPADSetRemote`, `PortWPADSample`, `PortWPADRunFrame`, motor / speaker callbacks), the mapping layer (`PortInputConfig`, `PortInputState`, `PortInputToRemote`, `PortInputPointerFromWindow`, `PortInputDefaultBindings`) and the script reader. |
| `src/host_wpad.cpp` | **New.** The WPAD library over four virtual remotes. |
| `src/host_input.cpp` | **New.** The mapping layer and the script reader. |
| `tests/input_harness.cpp`, `tests/input_host.cpp` | **New.** The harness; what it links but does not run (below). |
| `tests/test_input.py`, `tests/input/*.txt` | **New.** The test and its four scripts. |
| `Makefile` | `INPUT_*`, target `input` (part of `test`). |

No decomp file changed.

### The WPAD backend

- **Reports.** `PortWPADRunFrame` makes the 200 Hz reports of one NTSC frame (1001/60000 s: 3 or 4,
  from an exact integer accumulator; KPAD counts button repeat in 1/200 s). Each report
  (`PortWPADSample`) first finishes the asynchronous commands queued since the last one (their
  callbacks), then delivers connection changes (the connect callback, `WPAD_ERR_OK` or
  `WPAD_ERR_NO_CONTROLLER`; on connect the data format is `WPAD_FMT_CORE_BTN`, DPD off, motor off)
  and extension changes (the extension callback), then calls each connected channel's sampling
  callback, which is KPAD's. They run on the calling thread, where the Wii runs them in the
  Bluetooth interrupt; the game only sees them through KPAD's buffer.
- **`WPADRead`** writes the status struct of the current format's family (`WPADStatus`,
  `WPADFSStatus`, `WPADCLStatus`): buttons (Z / C only in an extension report from a Nunchuk), acc
  only in an `_ACC` format, the dots only in a `_DPD` format with DPD enabled and the sensor bar
  powered (else what the parser stores for a missing dot), the Nunchuk's stick and acc.
- **Commands.** `WPADControlDpd`, `WPADGetInfoAsync` (battery, low battery at level 1 or below, DPD,
  speaker, extension, LED `1 << chan`), `WPADControlSpeaker` (on / off / play; mute and unmute
  are accepted), `WPADControlLed` succeed and call back at the next report. `WPADSetDataFormat`
  applies from the next report. With KPAD this gives the same start-up as on the Wii: the first
  report after a connect asks for DPD, the next ones switch the format to
  `WPAD_FMT_CORE_BTN_ACC_DPD` (or the Nunchuk's).
- **Calibration.** `WPADGetAccGravityUnit`: 106 counts per g for the remote and 204 for the
  Nunchuk, the SDK's own defaults (`WPADHIDParser.c`, used when a remote's calibration block is
  bad).
- **Settings** from SC (§22): sensor bar position, pointer sensitivity (KPAD's
  `kp_err_dist_max` = 1 + sensitivity), rumble on / off, speaker volume.
- **Rumble**: `WPADControlMotor` (and `WPADEnableMotor`) call the host's motor callback when a
  remote's motor turns on or off. **Speaker**: `WPADSendStreamData` hands each accepted packet to
  the host's speaker callback while the speaker is on and playing (decoding the remote's 4-bit
  ADPCM is the window backend's job).
- **Pairing / addresses**: the virtual remotes are always paired (`WPADStart*Sync` succeed, a clear
  device request reports done); `WPADGetAddress` gives `00:19:1D:50:4F:0n` for channel n-1 when
  connected, zero otherwise. `WPADGetWorkMemorySize` is 0 (the game then uses its 1 MB minimum).

### The mapping layer

**Pointer.** KPAD maps the two dots p0, p1 (centre coordinates, -1..1 across the camera's 1024
pixels) to

```
n    = (p1 - p0) / |p1 - p0|              horizon = (n.x, -n.y)
dist = dist_vv1 / |p1 - p0|               dist_vv1 = 0.2 / 0.383864     (KPADSetObjInterval)
pos  = (center_org - R * (p0 + p1) / 2) * dpd2pos_scale
```

with `center_org = (0, -level)` (`KPADSetSensorHeight(level)`), `R` the rotation that undoes the
roll read from `n`, and `dpd2pos_scale = 1.25 / min(1, 0.75 - |level|)`. The mapping inverts this:
for a wanted `pos` P, roll r and distance D the exact dots are `m -/+ (L/2) n` with
`n = (cos r, -sin r)`, `L = dist_vv1 / D` and `m = R(-r) (center_org - P / dpd2pos_scale)`. The
camera reports integer pixels (`x = c.x * 512 + 511.5`, `y = c.y * 512 + 383.5`), so the pixels
are chosen: the pair's offset depends only on the roll and distance (rounded, or within two pixels
of it with a length within 0.3 pixels), because KPAD drops a pair whose distance moves more than
0.04 between reports (`kp_err_dist_speed`), and one pixel of spacing is more than that beyond 3 m;
then of those offsets and the positions within three pixels of the exact first dot, the one for
which KPAD's formulas (evaluated in `kpadFromDots`) give the position closest to P. Plain rounding
is off by up to 0.003; a one-pixel tilt of the pair, which KPAD's roll correction turns into a
small shift, gets most of that back (largest error over the tests 0.0042, 1.1 pixels on a
608-pixel screen). Dots outside the camera's frame (0..1022 x 0..766) are not reported; KPAD then
tracks one dot or loses the pointer, as with a real remote. The sensor level must be the one the
game gives KPAD: `PortInputDefaultConfig` takes Wii Play's (`applySetting`) from the SC sensor bar
position. Window pixels map to `pos` with `PortInputPointerFromWindow` (`2 px / width - 1`), which
the game's cursor maps back to the same pixel.

**Accelerometer.** At rest the sensor reads the up vector in the remote's axes. In KPAD's acc axes
(`acc = (-accX, -accZ, accY) / counts per g`) a remote at roll r (clockwise as the player sees it,
`horizon = (cos r, sin r)`) and pitch p (nose up) reads

```
acc = (-cos p sin r, -cos p cos r, sin p) + extra
```

(level: `(0, -1, 0)`); `acc_horizon` tends to `(cos r, sin r)`, matching the dots, and
`acc_vertical` to `(cos p, -sin p)`; KPAD rejects the pointer when `acc_vertical.x <= 0.7`
(pitched beyond about 45 degrees). Counts are `round(g * 106)`; the remote's Y and Z are even (the
HID parser keeps 9 significant bits), raw values stay within 10 bits. The Nunchuk uses the same
model with 204 counts per g, all 10 bits.

**Nunchuk stick.** KPAD maps counts 15..71 to 0..1 per axis (`clamp_stick_cross`); a stick value v
becomes `15 + round(|v| * 56)` counts (0 for v = 0), so KPAD reads v back to 1/56 and the
gamepad's own dead zone is not doubled.

**Buttons.** `PortInputState.buttons` holds `WPAD_BUTTON_*`; `PortInputButtonFromName` and
`PortInputDefaultBindings` (left mouse button and Space: A; right button and Left Shift: B; `1`,
`2`, `=` / keypad + for +, `-`, Escape for HOME, the arrows for the D-pad, Z, C; names as SDL
gives them) are for the window backend. Z and C need the Nunchuk.

### Scripted input

A text file; `#` starts a comment. `window W H` (default 608 456) sets what `mouse` positions refer
to, `end N` the number of frames. Every other line is `FRAME COMMAND ARGS`, in frame order, applied
before that frame's reports, to channel 0 or the channel of the last `chan`: `chan N`, `connect`,
`disconnect`, `nunchuk 0|1`, `battery 0-4`, `mouse PX PY`, `pointer X Y` (KPAD units),
`offscreen`, `dist M`, `press NAMES`, `release NAMES`, `roll DEG`, `pitch DEG`, `accel X Y Z`
(g, KPAD's axes), `stick X Y`, `fsroll DEG`, `fspitch DEG`, `fsaccel X Y Z`, and for test runs of
the game (§31) `aim 0|1` (each frame a game-side probe, if linked, puts the pointer on a target)
and `fire PERIOD` (B pressed for the first half of every PERIOD frames; 0 stops); since §33
`play 0|1` (each frame a game-side player, if linked, sets the whole remote: pointer, buttons,
roll ("twist"), pitch ("tilt"), acceleration, stick) and `shake X Y Z N` (an acceleration of
X, Y, Z g on top of `accel` for N frames: a flick, a lift or a jerk); since §34 `hbm PANE`
(each frame the HOME Menu probe, if linked, puts the pointer on the centre of the HOME Menu's
pane PANE, by the library's own pane rectangles; `hbm none` stops); since §36 `goal N`
(how far the game-side player of the channel goes: stages, missions, rallies or levels; 0 keeps
its default). Each of `aim`, `fire`, `play`, `shake`, `hbm` and `goal` applies to the channel
of the last `chan`: two-player runs drive remote 2 with `chan 1` (§36). Defaults: no
remote connected, battery 4, distance 2 m, level.

### The harness

```
input_harness --script FILE [--sc FILE] [--mode game|kpad]
```

`game` (default) boots the EGG heaps (`ConfigurationData::initMemory` on a configuration with no
video), `RPGrpScreen::Initialize` with `RPGrpRenderer`'s limits (608 / 832 x 456) and the cursor
screen as `RPSysCommonObject` makes it, then `RPSysCoreController::initialize`, `applySetting`,
`initScene`, and each frame: script, mapping, `PortWPADRunFrame`,
`RPSysCoreControllerMgr::beginFrame`, print, `endFrame`. `kpad` runs `KPADInit` /
`KPADSetSensorHeight` / `KPADRead` alone (KPAD's default parameters: no smoothing, no repeat).
Per frame and connected channel a `K` line has the newest `KPADStatus` (pos, horizon, dist, acc,
acc_vertical, dpd_valid_fg, buttons, Nunchuk stick and acc, device type, format, number of
reports and of samples read) and, in game mode, the cursor as `RPSysCursorDrawMgr::preUpdate`
computes it; `M` lines are motor changes, `E` lines the calls into the stand-ins below, `S` lines
self-checks of the WPAD calls KPAD does not make (info, speaker, gravity units, probe).

Linked for real: `KPAD.c`, `eggController.cpp`, `RPSysController.cpp`, `RPSysControllerMgr.cpp`,
`RPSysPairingMgr.cpp`, `eggScreen.cpp`, `eggFrustum.cpp`, `RPGrpScreen.cpp`, the EGG heap / math
units, MEM, `OSArena` / `OSAlloc`, `host_os.cpp`, `host_sc.cpp`. `tests/input_host.cpp` has the
rest: `RPSndStaticMgr::startSe`, `RPSndSpeakerMgr::setSpeakerOn/OffCallback` and
`RPSysPlayerMgr::resetOldData` print `E` lines (their instances are zeroed stand-ins the printing
functions do not read); `RPSysProjectLocal` has no instance (only the address search asks, and
checks for NULL); the GX / g3d camera calls that `Frustum`'s and `Screen`'s vtables keep abort with
their name. KPAD's float math, the mapping and the controller code are built with SSE on `-m32`,
as in §17.

### What the test checks

`tests/test_input.py` (`make input`, part of `make test`; a few seconds) runs the four scripts
(`pointer`: a 9 x 7 grid of window positions, then grids at rolls 40 and -30 and distances 1.2 and
3.5 with ramps between, positions off the window and off the camera, the pointer away, small
moves; `buttons`: every button, combinations, one-frame presses, long holds; `tilt`: roll and
pitch ramps to +-90 / +-60 degrees, a combined tilt, accelerations on top of gravity beyond the
clamps; `nunchuk`: attach, stick, Z / C, its tilt, a second remote, disconnects, a reconnect) in
both modes and checks every status against a model computed from the script:

| Check | Model | Tolerance (largest seen) |
|---|---|---|
| Pointer | `pos` against the script's position whenever KPAD tracks both dots and both are well inside KPAD's frame (KPAD mode always; game mode after a move beyond the 0.06 play radius, else within the play radius) | 0.0045 (0.0042) |
| Cursor | `ConvertFromNormalCC` of the clamped `pos` on a 608 x 456 screen; back on the mouse pixel | 1e-3; 1.4 px (1.1 px) |
| Tracking | `dpd_valid_fg` 2 when both dots are in view and the remote has settled (40 frames after a roll / distance / pitch change); 0 when pointing away, when both dots are out of view, when pitched beyond 50 degrees; never 2 with one dot out | exact |
| Horizon, distance | `(cos r, sin r)`, D | 4.3 / 4.5 pixels over the dots' spacing (2.5, 0.73) |
| Acc | the counts the mapping must produce, through KPAD's scale (f32), clamped at 3.4 g; and the unquantized model | 1e-6; one count (0.0094 g) |
| `acc_vertical` | `(cos p, -sin p)` once settled | 0.02 (0.0093) |
| Nunchuk | stick from the counts (1e-5), acc as above with 204 counts per g and the 2.1 g clamp; device type | exact |
| Buttons | `read_kpad_button` / `calc_button_repeat` (repeat after 133 reports, then every 17 in game mode; none in KPAD mode) and EGG's stick buttons | exact |
| Rumble | `ControllerRumbleUnit::calc` on `"***-***---*"` from each connect frame; motor off at disconnect | exact |
| Events | connect: speaker on, connect sound (player = channel); disconnect: speaker off, player data reset | exact |
| Reports | 3 or 4 a frame, `floor(200 * 1001 * (f + 1) / 60000)` in total | exact |
| WPAD | the `S` lines | exact |
| 64 / 32 | `--compare`: the other build prints the same | all 5234 lines |

Mutation checks while writing the test: the dots' roll sign flipped (961 errors), the remote's Y /
Z counts not even (956), the stick scale off by one count (16).

### Still open

- The window backend: SDL mouse / keyboard / gamepad into `PortInputState` (bindings above), the
  motor callback to gamepad rumble, the speaker callback to an ADPCM decoder and the mixer, and
  calling `PortWPADRunFrame` from the frame loop (or a VI-retrace device).
- Per-game tilt bindings: which gamepad axes become roll / pitch / extra acceleration for games
  held sideways (Charge!) or swung (Fishing, Laser Hockey's twist), and gyro input.
- `WPADReadFaceData` / `WPADReadMemoryAsync` (Miis on a remote, RFL) and the Classic Controller
  (Wii Play does not use it; the backend passes the device type only).
- HOME Menu and speaker paths: done in the Phase 7 integration. `hbm_harness` sends the HOME
  Menu's speaker sounds through `RemoteSpk`, the decomp's WENC and this backend to the speaker
  callback (§16), and `audio_harness` links this backend under `RPSndSpeakerMgr` and nw4r's
  remote speaker code (§21). nw4r's own remote speaker output stays silent while the host AX
  makes no remote speaker samples (`AXRmt*`) and its alarms do not fire.
- `RPSysCursorDrawMgr` itself (it needs the renderer, layouts and effects); the harness repeats
  its two lines.
- Real Wii Remotes (Bluetooth HID into `PortWPADRemote`, or the decomp's own WPAD over a host
  Bluetooth layer).

## 24. Streams: StrmPlayer plays a BRSTM from the disc

`StrmFile.h`'s structs are big-endian now, and the real `StrmPlayer` streams a BRSTM from the
disc on the AX mixer of §21. No sound archive has a stream sound (§21, ASSETS.md §2.6), so
`audio_harness --strm FILE` drives the player the way `SoundArchivePlayer::PrepareStrmImpl` and
`StrmSound::Prepare` do: a `StrmBufferPool` of 0xA000 bytes a channel
(`GetRequiredStrmBufferSize`'s figure) on `RPSndAudioMgr`'s sound heap (M0, so the DSP can read
it), a `ut::DvdFileStream` on the file, `StrmPlayer::Setup` / `Prepare(stream, 1 voice, sample
offset 0)`; the header and the first blocks load on the task thread, and `Start` is called at the
first game frame where `IsPrepared` holds. From then on the player's own sound-thread callbacks
refill the ring buffer and move the loop addresses. The rest of the harness (taps, events,
render) is as in §21. `check_matching.py`: 974 units depending on touched files, all identical
(2026-10-30).

### Changes to the decomp sources

Each the same object on the Wii (`be_u16` / `be_u32` are `u16` / `u32` there):

| File | Change | Why |
|---|---|---|
| `include/nw4r/snd/snd_StrmFile.h` | `StrmDataInfo` and `Header`: every 16- / 32-bit field `be_u16` / `be_u32` | `StrmFileReader::ReadStrmInfo` and `StrmFileLoader::LoadFileHeader` read them straight from the file (§13 had converted only the `DataRef` / `Table` they use). |
| `src/nw4r/snd/snd_StrmFile.cpp` | `ReadAdpcmInfo` copies the channel's `AdpcmInfo` (24 `u16`) through `be_u16` under `#if PORT_BE_WRAPPED`; `ReadAdpcBlockData`'s read buffer is `be_u16` | The coefficients, predictor / scale and history go to the voice and the DSP in host order, as `ReadWaveParam`'s (§21); the ADPC block's history pairs are big-endian in the file. |

Both files are in `check_matching.py`'s list. `StrmPlayer.cpp` itself needed nothing: the data
blocks are copied as bytes (the DSP reads DSP-ADPCM nibbles), and the loop predictor / scale is
the block's first byte.

### The test

`test_audio.py` scenario `wly-strm-go`: `Sound/RPWlyScene/sound/RP_WLY_BGM_GO.brstm` (32 kHz
stereo DSP-ADPCM, 80000 samples in 6 blocks of 14336, one-shot), 900 AX frames, tapped:

| Check | Result |
|---|---|
| Voices | one per channel, running in consecutive frames from game frame 0 until AX frame 835 |
| Decode | each voice's accelerator output is exactly one channel of `dspadpcm.py`'s `decode_brstm` (80000 samples each, across the ring buffer's wrap and the block refills), then zeros (the zero buffer after the last block) until the player stops it |
| SRC, envelope | the §21 models over the decoded channel: bit for bit in every frame |
| Mix | the rendered L/R from the two tapped voices: 900 of 900 frames exact |
| Regression, determinism | `audio_expected.txt` line `wly-strm-go`; twice identical; 64 / 32 identical |

### Still open

- Looping streams: the loop path (`UpdateDataLoopAddress`, the loop block's history from the
  ADPC block via `ReadAdpcBlockData`) is converted but not tested; on this disc the ADPC
  entries hold the encoder's source-PCM history, not the decoded one (`dspadpcm.py` records the
  distance), so a model has to decide what the DSP does with it first. Start offsets other than
  0 (`CalcStartOffset`) likewise.
- PCM16 streams (`hokke_goll_me*_strm.brstm`, `result.ry.44.c4.brstm`: 44.1 kHz PCM16, 4096-sample
  blocks) are not played by the test.
- `StrmSound` / the `SoundArchivePlayer` stream path (`detail_OpenFileStream`, the stream sound
  pool) stay unexercised: no archive on the disc has a stream sound.

## 25. Aurora: build, patches, headless test

Aurora (MIT; GX on WebGPU via Dawn) is the graphics backend (docs/PLAN.md). Milestone 1 builds
it from the submodule, isolates it behind a small C library, and renders a disc texture and a
real material's display lists headless, checked against an independent model.

### Layout

| Path | What |
|---|---|
| `port/extern/aurora` | submodule: the fork, branch `wiiplay` (upstream `encounter/aurora` 505b5eb) plus the patches below |
| `port/host/aurora/CMakeLists.txt` | builds Aurora as a subdirectory (Release, PIC, 64-bit only) and `libwiiplay_gx.so`; pins the unhashed downloads |
| `port/host/aurora/wpgx.h`, `wpgx.cpp`, `wpgx.map` | the glue library's C API (`wpgx_*`) and its export list |
| `port/host/tests/gx_harness.cpp` | the bring-up render (below) |
| `port/host/tests/test_gx.py` | its checks |
| `port/host/build/aurora/` | CMake build tree (ignored with the rest of `build/`) |

### Build

`make gx` (and `make test` on 64-bit) configures `build/aurora` once (stamp
`build/aurora/.configured`, log `configure.log`) and then always hands the build to CMake, which
rebuilds only what changed (`build.log`; one `cmake --build` for the library and the tests, §26). The Makefile links `libwiiplay_gx.so` into the GX
targets only (`gx_harness`, with an rpath to `build/aurora`); nothing else changes, and the
matching build is untouched. `M32=1` prints `gx: skipped (the GX layer is 64-bit only)`;
`NO_GX=1` skips it on 64-bit. `make clean` keeps `build/aurora` (`make distclean` removes it).

What CMake builds: Aurora with GX only (DVD/nod, CARD, THP, RmlUi, examples off), Dawn from the
prebuilt package, SDL3 from source (static), and the other dependencies Aurora fetches. PATH is
not used to find packages, so a Python/conda environment on PATH cannot supply libraries (it
did: its zstd/zlib). First build: about 8.5 minutes with 16 jobs (most of it SDL3, abseil,
Dawn's headers, Aurora); `build/aurora` is about 390 MB, `libwiiplay_gx.so` 26.5 MB (Dawn and
everything else linked statically; it needs only libc, libm and libz).

**Pinned downloads.** Aurora fetches Dawn, SDL3, abseil and nlohmann_json without a hash.
`FetchContent` keeps the first declaration of a name, so `port/host/aurora/CMakeLists.txt`
declares them first with the same names and URLs plus `URL_HASH SHA256`, and forces the version
variables to the hashed ones; no Aurora patch is needed. A submodule bump that changes a version
fails at configure with a hash mismatch. (Dawn is pinned for linux-x86_64 only; other platforms
get a warning.) Everything else Aurora fetches (fmt, xxHash, zlib-ng, libpng, Freetype, SQLite,
zstd, Tracy, imgui, googletest) already carries a hash upstream.

| Download | Version | SHA-256 |
|---|---|---|
| Dawn prebuilt `dawn-linux-x86_64.tar.gz` | v20260807.225922 | `2a4523426ab9b3a8970b4e151c3b005304ad37c2c01bcff80c78fffe486fc768` |
| SDL3 source | release-3.4.10 | `0dc11d980ba17250200718fa4e28011da293f27ed92f92203afffe396811f307` |
| abseil-cpp | 20240722.0 | `f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3` |
| nlohmann_json `json.tar.xz` | v3.12.0 | `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa` |

**Symbol isolation.** `libwiiplay_gx.so` exports only `wpgx_*` (`wpgx.map`,
`--exclude-libs,ALL`, hidden visibility), so Aurora's `GX*`/`OS*`/`VI*` cannot clash with the
decomp's revolution SDK in one process. The C++ runtime is linked statically and hidden too:
the port's executables define `operator new`/`delete` (the game's heaps; the loader library's
arena), and with a shared libstdc++ Aurora's allocations from its worker threads were routed
there ("port arena: free of a corrupt block").

### Aurora patches (submodule working tree, not committed yet)

| # | Commit message | Files |
|---|---|---|
| 1 | `gfx: add aurora_read_framebuffer() for synchronous EFB readback` | `include/aurora/gfx.h`, `lib/gfx/framebuffer_readback.cpp` (new), `cmake/aurora_gx.cmake` |
| 2 | `Add AuroraConfig.disableShaderCache` | `include/aurora/aurora.h`, `lib/gfx/pipeline_cache.cpp`, `lib/webgpu/gpu_cache.cpp` |
| 3 | `Add AuroraConfig.pipelineCacheKey to discard pipelines cached by other builds` | `include/aurora/aurora.h`, `lib/gfx/pipeline_cache.cpp` |

No patch was needed for the rest of the spike's list at 505b5eb:

- **Headless**: upstream now has `AuroraConfig.surface.type = AURORA_SURFACE_HEADLESS` (no SDL
  video, offscreen EFB); the spike's env-var patch is obsolete. Works on lavapipe.
- **Synchronous pipelines**: upstream `AuroraSetDrawsBlocking(true)` makes draws wait for their
  pipeline. Without it, the first frames skip draws (seen: `--async --frames 2` differs).
- **Stale caches**: upstream already versions cached pipeline configs
  (`GXPipelineConfigVersion`, size check, schema version); patch 3 adds an application key on
  top, so our own pipeline changes cannot reuse old entries, and patch 2 turns the caches off
  for tests. With `data_dir`, `userPath`/`cachePath`/`resourcesPath` all point into a directory
  the caller chooses; nothing goes to `~/.local/share` (the test checks it).

### The glue API (`wpgx.h`)

`wpgx_init(&cfg)` (size, headless, `data_dir` (required), `disk_cache`, `cache_key`,
`blocking_pipelines`, `allow_cpu_adapter`, `log_level`), `wpgx_begin_frame` /
`wpgx_end_frame`, `wpgx_read_frame` (RGBA8, top row first), `wpgx_shutdown`,
`wpgx_adapter_name`. Milestone 1 also has `wpgx_test_*` helpers that drive Aurora's own GX API
(2D setup, `GXInitTexObj`/`GXLoadTexObj` with Wii-format texels, TEV REPLACE,
`GXCallDisplayList`, a quad); from milestone 2 the game's GX calls reach Aurora through the
FIFO instead and these stay for tests.

### The test

`gx_harness <disc> <out.rgba> <data-dir> [--frames N] [--window] [--async] [--disk-cache]
[--cache-key N] [--log-level N]` loads `banner.tpl` (`TPLBind`/`TPLGet`) and
`Common/RPFshScene/common.carc` → `G3D/fsh_lake.brres` (EGG DVD/Decomp, ARC,
`ResFile::Init/Bind`), finds material `a_a_lake_mat_v` (2 TEV stages), and draws for 3 frames:
the top quad is the 192x64 RGB5A3 image at exactly 3x (GX_NEAR) with TEV REPLACE, blended over
the clear colour; the bottom quad has the same texture on maps 0 and 1 and the material's
`ResMatPix`, `ResMatTevColor` and `ResTev` display lists (big-endian, from the BRRES) through
`GXCallDisplayList`. It prints what it submitted and writes the frame read back with
`aurora_read_framebuffer`.

`test_gx.py` (run by `make gx` with `VK_ICD_FILENAMES` = lavapipe, `MESA_SHADER_CACHE_DISABLE`,
scratch `GX_SCRATCH`, default `$TMPDIR/wiiplay-gx` or `/tmp/wiiplay-gx`; `--gpu` uses the real
driver) checks:

| Check | Result |
|---|---|
| Inputs | the texture parameters and the three display lists the harness found through our loaders are the bytes the script finds itself (TPL header; MDL0 material dictionary) |
| Top quad | `gxtex.py` decode, GX_NEAR, SRCALPHA blend over the clear colour: 110592 pixels, max difference 0 |
| Bottom quad | a TEV model fed by the BP commands of the display lists (BP mask register, tev orders, colour/alpha combiners, konst selections, colour and konst registers, swap tables, alpha compare, blend/z mode; anything else it meets is an error): 110592 pixels, max difference 0.47 (tolerance 2) |
| Background | the clear colour, exact (86016 pixels) |
| Caches | no files in the data directory without `--disk-cache`; with it, three runs (keys 1, 1, 2) give the same frame, and only the key change discards the cache |
| Home | `~/.local/share/wiiplay` (and `Aurora`) unchanged |

No image is stored in the repo; the expected frame is computed from the disc at test time.
`make gx` also builds and runs Aurora's own `gx_fifo_tests` (googletest; FIFO encode/decode,
display lists, readback; 232 tests, under a second).

`gxtex.py` fix found by this test: 3/4/5/6-bit channels were widened by shifting
(`v << 3`); the GX texture unit replicates the high bits (`v << 3 | v >> 2`), as Aurora and
Dolphin do. RGB565, RGB5A3 and IA4 alpha now match the hardware (CMPR's interpolation is
unchanged).

Expected output:

```
gx_fifo_tests: [  PASSED  ] 241 tests.
test_gx: 640x480 after 3 frames on llvmpipe (LLVM 21.1.8, 256 bits); inputs match the disc (banner.tpl 192x64 format 5, a_a_lake_mat_v: 2 TEV stages)
  top: 110592 pixels, max diff 0.00
  bottom: 110592 pixels, max diff 0.47, 0 of 12288 texels alpha-tested out
  background: 86016 pixels, max diff 0.00
  pipeline cache: 3 runs (key 1, 1, 2) same frame, key change discarded: [False, False, True]
errors: 0
OK
```

### Next (milestone 2, done: §26)

The game's own GX SDK as the encoder, Aurora as command processor and renderer: redirect
WGPIPE writes into Aurora's FIFO; host `GXInit`/Fifo/Misc/FrameBuf on top of it; an address
resolver from emulated Wii (physical/cached) addresses to host pointers for CP array bases
(0xA0-0xAF, rejected by Aurora's command processor today), nested `CALL_DL` and texture/TLUT
loads (`GXLoadTexObj`/`GXLoadTlut` words → a side table of texture data); target: the health
screen layout drawn through `lyt`.

## 26. Aurora milestone 2: the game's GX SDK drives Aurora

The decomp's own GX SDK (`src/revolution/GX`, the matching code) is now the command encoder on
the host: its write-gather pipe stores go, big-endian, into Aurora's FIFO, and Aurora is the
command processor and renderer. The Wii-layout GX objects (§11) and the SDK's shadow state
(`gxdt`) stay authoritative; nothing calls Aurora's own GX API on this path. Target reached: the
health and safety screen (`RPSysBootScene`'s layout) is built and drawn by the real `nw4r::lyt`
code through the real SDK, read back headless and matched pixel for pixel against an
independent model.

### The write-gather pipe

On the Wii every GX command is a store to `WGPIPE` (0xCC008000). Those stores now go through
`WGPIPE_WRITE(member, value)`:

| Build | Header | `WGPIPE_WRITE(c, v)` |
|---|---|---|
| Wii | `include/port/wgpipe.h` | `(WGPIPE.c = (v))`: the store it replaces |
| host | `port/host/include/port/wgpipe.h` | appends `v` big-endian to the current CPU FIFO (`port_wgpipe_u8/u16/u32/f32`); member `p` sends the pointer's 32-bit emulated address (`port_vaddr`) |

The current CPU FIFO (`src/host_wgpipe.cpp`, in the loader library so every unit that includes
the GX headers links) is either

- the **stream**: when the CPU FIFO is the GP FIFO (as after `GXInit`). Bytes are gathered in a
  64 KiB buffer and handed in order to a *sink* (`port/gxsink.h`: `write(data, size)`,
  `sync(kind)`). The SDK only synchronises between commands, so every sync point is a command
  boundary: `PORT_GX_SYNC_FLUSH` (GXFlush), `PORT_GX_SYNC_DRAWDONE` (GXDrawDone,
  GXSetDrawSync, GXAbortFrame), `PORT_GX_SYNC_DISPLAY` (GXCopyDisp). Without a sink (every
  harness that does not draw) the stream is counted and dropped;
- **memory**: any other CPU FIFO, i.e. `GXBeginDisplayList`'s buffer. The bytes land in that
  Wii buffer exactly as the gather pipe would write them; leaving the FIFO pads to 32 bytes with
  `GX_NOP` (the pipe's burst size); writing past the end sets the FIFO's wrap flag, so
  `GXEndDisplayList` returns 0 as on the Wii.

The decomp side is mechanical: every `WGPIPE.x = y;` in the GX headers (`GXHardware.h` macros,
`GXVert.h` inline vertex writers, `GXDisplayList.h`'s `GXFastCallDisplayList`) and the compiled
SDK units became `WGPIPE_WRITE(x, y);`. The paired-single writers of `GXTransform.c` (matrix and
projection loads) and `GXLight.c` (`WriteLightObj`) are opened with
`ASM_VOLATILE_PS(name)` (Wii: `ASM_VOLATILE`); their host forms in
`port/host/include/decomp/compat.h` write the same words in the same order.

### Host replacements for the hardware-facing SDK

| Unit | Host form |
|---|---|
| `GXInit.c` | **Real.** It runs and programs every register through the stream (Aurora's own shadow registers start at zero, so this matters). Its register pointers (`__piReg`, `__cpReg`, `__peReg`, `__memReg`) come from `OSPhysicalToUncached(0x0C00xxxx)`, which the host `OSAddress.h` maps to a plain 64 KiB block (`port_hw_regs`, writes stick). Two `PORT_GP_STATIC(p, size)` wraps (Wii: `(p)`) give its static default texture and placeholder array base an M0 copy (`PortArenaStatic`), so they have emulated addresses. |
| `GXFifo.c` | `src/host_gxfifo.cpp`: `GXInitFifoBase`, `GXSet/GetCPUFifo`, `GXSet/GetGPFifo`, `GXGetFifoPtrs/Count/Wrap`, `GXGetGPStatus` (always idle), breakpoints (none), `__GXFifoInit`, `__GXIsGPFifoReady`, `__GXCleanGPFifo`, `__GXSaveFifo`, `GXGetCurrentGXThread`. The FIFO objects are `GXFifoObjImpl` runtime objects (native pointers). |
| `GXMisc.c` | `src/host_gxmisc.cpp`: the same register writes (`GXSetDrawSync`'s BP 0x48/0x47, `GXDrawDone`'s BP 0x45, `GXFlush`'s 32 bytes of padding, `GXPixModeSync`); the waiting is synchronous: the sink runs everything up to the sync point, then the PE token register (`__peReg[7]`) is set and the game's token / draw-done callback is called before returning, as the interrupt handlers would. `GXPoke*` are no-ops (no CPU EFB access yet). Also: the SDK's four render modes (`GXNtsc480IntDf` etc.: GXFrameBuf data not decompiled yet) and weak `OSDisableInterrupts`, `OSRestoreInterrupts`, `OSGetTime`, `OSRegisterVersion`, `OSRegisterShutdownFunction`, `VIGetTvFormat` (NTSC), `PPCMfhid2/Mthid2/Mtwpar/Sync`. |
| `GXFrameBuf.c` | **Real** (copy source/destination, clamp, filter, clear colour encoders). `GXCopyDisp` ends with a host-only `PortGXDisplayCopied(dest, clear)` (`#if defined(PORT_HOST)`): the frame is complete. The GX harness's sink maps it to `wpgx_end_frame()`; the harness calls `wpgx_begin_frame()` at the start of the next frame. There is no XFB or VI scan-out on the host: `VISetNextFrameBuffer`/`VIFlush`/`VIWaitForRetrace` stay host stubs, and Aurora's display-copy BP write is a no-op ("STUB: display copy", a warning). Aurora clears the EFB at each frame start to the copy clear colour, which matches `GXCopyDisp(..., GX_TRUE)`; a copy without clear is not emulated yet. |
| `GXAttr.c`, `GXBump.c`, `GXDisplayList.c`, `GXGeometry.c`, `GXLight.c`, `GXPixel.c`, `GXTev.c`, `GXTexture.c`, `GXTransform.c`, `GXPerf.c` | **Real**, built with `-fgnu89-inline` (the SDK's plain `inline` helpers also get an out-of-line copy, as with CodeWarrior). |

The GX harnesses link these objects (`GX_SDK_OBJS` in the Makefile) before the loader library;
the no-op GX functions of `src/host_gx.cpp` and the CPU-side texture object functions of
`src/host_gxtexobj.cpp` are now weak, so the real SDK replaces them there and every other
harness is unchanged.

### Decomp changes for the host (all byte-identical on the Wii, `check_matching`)

- **Texture and TLUT objects keep the Wii byte order** (the §11 contract): `GXTexObjImpl` and
  `GXTlutObjImpl` (`GXInternal.h`) have `be_u32`/`be_u16`/`BE_ENUM` members and
  `RES_PTR(void) userData`, and every field access in `GXTexture.c`'s object functions is
  `BE_GET`/`BE_SET`. The decomp's `GXInitTexObj` etc. therefore produce the same words as
  `src/host_gxtexobj.cpp`, and `GXLoadTexObj` sends them. `GX_PHY_ADDR` (`GXTexture.c`,
  `GXFrameBuf.c`) and `GXSetArray` convert with `PORT_PTR_TO_U32`. `GXGetTexObjAll` returns
  the image as a host pointer on the host (`OSPhysicalToCached`; the Wii returns the physical
  address cast to a pointer).
- **Colours sent as words**: `*(u32*)&color` (a `GXColor` read as RGBA) is
  `BE_GET(*(be_u32*)&color)` in `GXTev.c` (`GXSetTevColor`, `GXSetTevKColor`),
  `GXGeometry.c` and `GXLight.c` (ambient/material colours, including the stores into
  `gxdt`), `GXPixel.c` (`GXSetFog`) and `g3d_gpu.h` (`GDSetChanAmbColor`/`MatColor`).
  `GXSetTevColorS10` reads its two words from the fields on the host (`#if defined(PORT_HOST)`).
  Found by the test: on a little-endian host the colour channels came out reversed.
- `GXGeometry.c`: `__GXSetGenMode` is `static inline` after `GXGeometry.h` declared it extern;
  GCC rejects that, so the host form drops `static`.
- `GXInit.c`: prototypes for `GXInitFifoBase`/`GXSetGPFifo` under `#if defined(PORT_HOST)`
  (implicit declarations in the original; adding them to `GXFifo.h` changed CodeWarrior's
  numbering of local statics in every unit including it, so they stay local).
- `port/host/include/port/resptr.h`: the C++ part is `extern "C++"` (the GX headers are
  included inside `extern "C"` blocks).
- `M_SQRT3` (MSL's `math.h`) is defined in the host `compat.h`.

### Aurora patches (submodule working tree, not committed; series in the job's scratch)

Generic (no project names), each with a one-line message; `gx_fifo_tests` 239 pass.

| # | Commit message | What |
|---|---|---|
| 4 | `gx: add AuroraSetAddressResolver for addresses in the command stream` | `GXAurora.h`: a resolver `(u32 address, u32* size, void* user) -> const void*` for raw hardware streams; `fifo::resolve_address(address, minSize)` |
| 5 | `gx: resolve CP array base registers through the address resolver` | CP 0xA0-0xAF (was "not supported"): the array points at the resolved memory, big-endian. Vertex arrays have no size on the hardware, so each draw's indices are scanned and the uploaded range grows to the largest one (`AttrArray::guest`, `guestAvailable`); matrix/light arrays (indexed XF loads) cover what is mapped |
| 6 | `gx: run GX_CMD_CALL_DL display lists through the address resolver` | `CALL_DL` (was ignored) runs the resolved list, recursively, at most 8 deep; a draw done inside a list does not end the caller's batch |
| 7 | `gx: load BP texture images and TLUTs through the address resolver` | below |
| 8 | `gx: decode the scissor box offset as the hardware does` | BP 0x59 holds (offset + 342) / 2 and is subtracted (Aurora read it as a signed value and added it; its own encoder is changed to match). With the SDK's encoding everything was clipped away |
| 9 | `gx: use the hardware viewport origin offset of 342` | XF viewport origin: 342 (Aurora used 340 in both its encoder and decoder; with the SDK's encoding the frame was 2 pixels off) |
| 10 | `tests: cover the address resolver, CALL_DL, TLUT loads and the scissor box offset` | 7 googletest cases |

**Textures and TLUTs: resolver, not object overrides.** Two designs were possible: resolve the
BP texture registers in Aurora, or override `GXLoadTexObj`/`GXLoadTlut` on the host and feed
Aurora's `GX_AURORA_LOAD_TEXOBJ` from a side table keyed by the Wii object's address. The
resolver is the cleaner one: the real `GXLoadTexObj`/`GXLoadTlut` run unchanged (the TMEM
region callbacks, the `gxdt` texture-size shadow that `__GXSetSUTexRegs` needs), loads recorded
into display lists or written straight to the FIFO work, and no identity is needed from the
object: `lyt::Material::SetupGX` and `ut::CharWriter` build their `GXTexObj` on the stack every
draw, so an object address is no stable key at all. In Aurora (patch 7): a write to a texture
map's mode/image/TLUT registers marks it; at the next draw the image address (BP 0x94-0x97 /
0xB4-0xB7), size and format (image0) and mip levels (mode0 filter, mode1 max LOD) give the
bytes (`GXGetTexBufferSize`), resolved; each distinct (address, image0, levels) gets a stable id,
and its version changes when an xxHash of the bytes changes, checked again after every texture
cache invalidation (BP 0x66, `GXInvalidateTexAll`), which is when the hardware re-reads texture
memory. TLUT loads (BP 0x64 address, BP 0x65 TMEM offset and size, the hardware encoding) fill
a TLUT slot per TMEM offset; a CI texture finds its TLUT by the TMEM offset and format in its
BP 0x98-0x9B / 0xB8-0xBB register. Textures loaded through Aurora's own API
(`GX_AURORA_LOAD_TEXOBJ`) are unaffected. Not covered: memory changed by the CPU without a
following `GXInvalidateTexAll` (the hardware would show stale cache contents too, but not
always).

### libwiiplay_gx additions (`wpgx.h`)

`wpgx_fifo_write(data, size)` (append to Aurora's FIFO), `wpgx_fifo_sync(WPGX_SYNC_FLUSH |
WPGX_SYNC_DRAWDONE)` (publish / drain; outside a frame the commands wait for the next frame,
because Aurora runs them against the frame's render target), `wpgx_set_address_resolver`.
`wpgx_init` configures Aurora's VI with the EFB size, so GX viewports and scissors map 1:1
(Aurora scales GX coordinates from the VI frame buffer size, 640x480 by default). The pipeline
cache key is 2.

### Makefile

`make gx` built Aurora twice in parallel under `-j` (the glue library and `gx_fifo_tests` were
separate `cmake --build` runs); after a submodule change both re-ran the configure step in the
same tree and failed ("Configuring incomplete"). Both targets are now built by one
`cmake --build --target wiiplay_gx gx_fifo_tests`, and `gx-aurora-tests` only runs the tests.

### The health and safety screen (`gxlyt_harness`, `tests/test_gxlyt.py`)

The game draws it in `RPSysBootScene` (Wii Play is the "PartyPack"): the archive is not on the
disc but in `main.dol` (`Health_strapArchive`, the whole of `.data6` at 0x802E9E60, Yaz0, 132352
bytes; the symbol's size of 0xA440 is too small), holding `strap.arc` with `strap_00_<lang>`
layouts, animations and TPLs; `RPSysLayout` draws it in the 2D view.

`gxlyt_harness <disc files> <out.rgba> <data dir> [--frames N] [--layout NAME] [--clear 0xRRGGBB]
[--display-list] [--dump-stream FILE] [--log-level N]` does `RPSysBootScene::Init`'s loading on
the real code (`EGG::Decomp`, `EGG::Archive::mount`, `getFile("strap.arc")`,
`lyt::ArcResourceAccessor::Attach(arc, "")`, `Layout::Build("strap_00_U.brlyt")`), sets up the
`DrawInfo` as `RPSysLayout::create`/`calc` do (view rect = layout rect, identity view matrix,
location adjust, root scale 1 at 4:3), and per frame: `GXSetCopyClear` (white, the boot scene's
clear colour), viewport and scissor over the 608x456 EFB, the 2D view's orthographic projection
(`RPGrpRenderer`: the layout rectangle, near -10000, far 10000; set with `C_MTXOrtho` +
`GXSetProjection` here, not through `EGG::Screen`), `RPSysLayout::drawInit`'s cull/z/clip state,
`Layout::CalculateMtx` and `Layout::Draw`, `GXDrawDone`, `GXCopyDisp`. With `--display-list`
the draw is recorded with `GXBeginDisplayList`/`GXEndDisplayList` into M0 and run with
`GXCallDisplayList` (the CALL_DL and in-list texture load paths). The stream goes to Aurora
through the sink, with a resolver over M0 (MEM1 above the reserved low block, MEM2).

`test_gxlyt.py` builds the expected frame independently: `wiiarc.py` for the DOL section, Yaz0
and U8; `fmt_layout.py` for the pane tree (sizes, translate/rotate/scale, base positions,
visibility, alpha and its inheritance, vertex colours, texture coordinates); the material records
read from the format (TEV colours, texture map, wrap modes, identity texture SRT, texgen, alpha
compare; anything else is a model error); `gxtex.py` for the TPLs; a rasteriser of the picture
quads (pixel centres into each pane, bilinear filtering at texel centres with clamping, lyt's
one-texture TEV stage `C0 + (C1 - C0) * tex` for colour and alpha, vertex colour times inherited
alpha, SRCALPHA/INVSRCALPHA over the clear colour, depth-first order). Three runs (white clear,
blue clear, display list) must match within 3 per channel. Result on lavapipe: **every pixel
identical after rounding** (max difference 0, mean 0.001 in all three runs). The layout is drawn
at its file values, which are frame 0 of `strap_00_U.brlan` (it only cross-fades the two
Nunchuk pictures); `P_mask_00` has TEV colours that make it transparent. No image is stored in
the repository.

`gxtex.py` fixes found by this test (Dolphin and Aurora agree with the new forms):

- I4 and I8 replicate the intensity into alpha (they returned alpha 255): the Nintendo logo
  is an I8 alpha mask.
- CMPR blends its endpoints in eighths, `(5a + 3b) >> 3` and `(3a + 5b) >> 3` (it used exact
  thirds), and the transparent entry of the 3-colour mode is the midpoint colour with alpha 0
  (it was black).

Expected output (part of `make test` on 64-bit, after `test_gx`):

```
test_gxlyt: strap_00_U.brlyt 608x456 after 2 frames on llvmpipe (LLVM 21.1.8, 256 bits); layout rect (-304, 228)-(304, -228); GX stream 11686 bytes
  pictures drawn: P_back_00 (ht_strapSquare_00.tpl, format 0), P_strap_00 (ht_strap_00.tpl, format 4), P_nunchuck_01 (ht_nunchuck_01.tpl, format 14), P_example_02 (ht_example_02_U.tpl, format 14), P_example_00 (ht_example_00_U.tpl, format 5), P_nintendo_00 (ht_strapNintendo_00.tpl, format 1), P_mask_00 (ht_strapSquare_00.tpl, format 0)
  white clear: 277248 pixels, max diff 0, mean 0.001, 0 over 3
  --clear 0x0000FF: 277248 pixels, max diff 0, mean 0.001, 0 over 3
  --display-list: 277248 pixels, max diff 0, mean 0.001, 0 over 3
errors: 0
OK
```

The milestone-1 test (`test_gx`) is unchanged and still passes: it drives Aurora's own GX API
through the `wpgx_test_*` helpers, which share the FIFO with the new path.

### Not covered yet (milestone 3)

- CP array bases are only exercised by Aurora's unit tests: lyt draws with direct vertices.
  The first g3d model drawn will exercise them (and indexed XF matrix loads).
- EFB copies: `GXCopyTex` into Wii memory (bloom, DOF, shadows, light textures, captures; Aurora
  keys copies by host pointer, which the resolver now provides) and the CPU-visible ones (the
  Kokeshi icon into a `ResTIMG`, RFL faces: the copy must be written back in the Wii texture
  format); a display copy without clear.
- `GXSetZTexture`, logic ops (OR in DOF), fog range adjustment, the copy filter (anti-aliasing
  and vertical filter), `GXPoke*`/`GXPeek*`.
- `EGG::Screen`/`StateGX` for the projection and state set-up (they build on the host; the
  harness sets the equivalent GX state directly), then one minigame's 3D scene.

## 27. Aurora milestone 3a: a minigame's 3D scene through g3d and EGG

Laser Hockey's table (`RPHkyScene`, 4:3) is loaded, built, lit and drawn by the game's own
`nw4r::g3d` and EGG code through the GX SDK into Aurora, headless on lavapipe, in the order
`RPGrpRenderer` uses for a 3D view. Nothing on the draw path is a host stand-in: g3d's
material, shape and matrix commands (display lists from the BRRES, its direct FIFO writes,
indexed XF matrix loads) reach Aurora as hardware commands with Wii addresses.

### Why Laser Hockey

The candidates were Shooting Range (`RPDucScene`, ASSETS.md's fallback for the first playable)
and the other fields. Shooting Range's scene needs more than is there yet: 7 of `sht_field`'s
materials are lit, with the scene light from a SCN0 (`LoadScnLight`) and `scene_Light.plight`;
the scene sets up the light-map path (`RPDucScene::LoadLight`: light textures, which EGG draws
and captures with EFB copies) and draws through bloom (`scene_Bloom.pblm`). Laser Hockey's table is one model (`Hck_Field_2`, 14 materials, 4218
opaque and 6188 translucent triangles) plus the puck (`Hck_pack`): a fixed top-down orbit
camera, one light and an ambient colour set in code (`RPHkyGame::Create`), no fog, no light
maps, no SCN0, and its look does not depend on its two draw paths (bloom and the puck's shadow
volume, both EFB-copy work). Its materials are all unlit (lighting off in every channel
control), so the light manager runs but does not change a pixel; its TEV setups go up to three
stages with konstant colours, CLR0-animated registers, two-texture blends and additive
translucent glows.

### What runs (`tests/gx3d_harness.cpp`)

- `RPGrpRenderer::Initialize(608, 456)`: `EGG::GfxEngine::initialize` (StateGX, the screen
  sizes, G3DUtility, DrawGX's display lists recorded with `GXBeginDisplayList`, light textures,
  texture buffers), RGBA6_Z24, `g3d::G3dInit(false)`, `RPGrpCamera::Initialize`,
  `RPGrpScreen::Initialize`.
- `RPGrpModelScene`'s constructor, by hand (the `RPGrpModel*` layer does not build on the host
  yet, §20): `ScnRoot::Construct(0x400, 0x400, 16, 8)`, 32 identity cameras, an
  `EGG::LightManager(16, 8, 1)` with every light set selecting ambient 0 and light 0, an
  `EGG::FogManager`, an `EGG::ScnRenderer` with both. The scene's draw-path flags (0x28: shadow
  volume, bloom) are not created.
- `RPHkyGame`: `RPGrpScreen` (fovy 40, CANVAS_CC), `RPGrpCamera` (orbit, target 0, distance
  -745, rotation (-90, 0, 0)), light 0 at (0, 745, 0), ambient (100, 100, 100).
- `RPHkyField::Load` / `Reset` and `RPHkyPuck::Load`: `Common/RPHkyScene/common.carc` through
  `EGG::DvdFile` / `DvdRipper` / `Decomp` and ARC; `ResFile::Init` / `Bind`; `ScnMdl` with
  buffer option 0x108 (as `RPGrpModelG3D` builds it); the six CLR0 of `Hck_anim.brres` in an
  `AnmObjMatClrOverride`, each at its last frame with rate 0. The puck's own CLR0 is not
  applied (it animates when the puck is hit).
- Per frame: `GfxEngine::beginDraw`'s part (`StateGX::frameInit`, `initGXCache`, the clear
  colour, `G3DUtility::reset`), the draw list, `UpdateFrame` / `CalcWorld` (light and fog
  managers' `Calc`), `CalcVtx` / `CalcMaterial`, the camera's `CalcMatrix` /
  `SaveCameraMatrix`, `SetCurrentCamera` with the screen as a perspective screen, `CalcView`
  (light manager `CalcView`, fog screen clip, g3d's view matrices),
  `PreConditionForRenderingGX` (`Screen::SetProjectionGX`, ambient, `LoadObjIndxGX`, fog 0),
  `GatherDrawScnObj`, `ZSort`, `DrawOpa`, `DrawXlu`, `finishDraw`, `GXDrawDone`, `GXCopyDisp`.
  The harness adds a `GXDrawDone` before `DrawOpa` and after `DrawXlu` so that it can attribute
  what the model draw resolved (checked: the frame is identical without them).

Options: `--no-xlu` (skip `DrawXlu`), `--no-puck`, `--puck-first` (enter the puck before the
table), `--clear 0xRRGGBB` (default magenta), `--dump-stream FILE`, `--log-level N`.

Ordering note: the game runs `CalcWorld` in `PostCalculate` before `CalcDrawList` pushes the
frame's models into the cleared root (its models calculate themselves through `RPGrpModel`);
the harness pushes first and lets `ScnRoot::CalcWorld` calculate them. Same matrices.

### Gaps the scene hit, and their fixes

| Gap | Fix |
|---|---|
| g3d's view matrix arrays (position, normal, environment) are read by the GP through indexed XF loads (`GXLoadPosMtxIndx` / `GXLoadNrmMtxIndx3x3`, and `LOAD_INDX_A` in skinned shapes' display lists); the CPU writes them in native byte order. Aurora (correctly) reads guest arrays as big-endian | `DrawResMdlDirectly` (`g3d_draw.cpp`, `#if defined(PORT_HOST)`) calls `PortGXSetViewMtxArrays(numViewMtx, pos, nrm, env)` after `SetViewPosNrmMtxArray`: big-endian copies in GP scratch memory replace the three array bases. The CPU keeps using the native arrays (`GetViewPosMtxPtr`, Z sort, environment matrices). |
| `EGG::LightManager::LoadObjIndxGX` points `GX_LIGHT_ARRAY` at its `GXLightObj` runtime objects (native floats) | `PORT_GP_LIGHT_OBJS(p, 8)` (`eggLightManager.cpp`; `(p)` on the Wii): a copy with the 12 floats big-endian (the colour is bytes already). |
| Static data the GP reads: `StateGX::resetTexture`'s null texture, DrawGX's Z-clear texture (u32 texels) and vertex arrays (s16), the light texture quad (s16) | `PORT_GP_STATIC` (bytes) and new `PORT_GP_STATIC_BE16` / `PORT_GP_STATIC_BE32` (one M0 copy, stored big-endian; `(p)` on the Wii). Without them `GXInitTexObj` aborted on a non-M0 pointer. |
| GP scratch memory | `PortGPScratch(size)` (`host_wgpipe.cpp`): a 4 MiB M0 block, emptied at every `GXDrawDone` / `GXCopyDisp` (everything sent so far has run). A frame that fills it before a drain aborts with a message. Copies recorded into a display list and replayed after a drain would be stale; nothing records model draws. |
| `ut::LC::QueueWait` (`DrawResMdlDirectly`) | `LCQueueWait` is a no-op in `host_gxmisc.cpp`: the game runs g3d with the locked cache off, so nothing is ever queued. |
| `host_gfx_stubs.c`'s entries were strong, so the real GX SDK and g3d FIFO writes could not be linked next to them | every entry is weak now (the stubs' behaviour is unchanged where nothing real is linked); new weak entries `LCEnable`, `PortGXSetViewMtxArrays`, `PortGPLightObjs`. |
| GPU driver threads: the executables' `operator new` / `delete` (the M0 arena) are exported, so a Vulkan driver's C++ code allocates there from its own threads. With Mesa's Intel driver the arena's free list was corrupted ("free of a corrupt block", crashes) about one run in three | the arena allocator takes a lock (`host_arena.cpp`). Driver allocations still land in M0, so emulated addresses can differ between runs on a GPU (not the frame). Routing foreign threads to `malloc` is a follow-up. |
| `GXDraw.c` (`GXDrawSphere`, referenced by EGG's DrawGX) | added to the GX SDK units (`GX_SDK_SRCS`). |
| Unhandled registers were invisible in Aurora's Release build (`#ifndef NDEBUG`) | Aurora patch 11 (below). |

Nothing else was needed in Aurora: indexed XF loads of matrices and light objects already work
through resolved arrays, and the scene uses no fog, logic op, Z texture or EFB copy.

### Aurora patches (submodule working tree, not committed; series in the job's scratch)

| # | Commit message | What |
|---|---|---|
| 11 | `gx: log each unhandled BP, CP and XF register once, in release builds too` | debug-level messages, once per register (bitset); CP registers without a decoder now report too (CP 0x40, the matrix index mirror, is explicitly ignored); unhandled XF memory writes and unimplemented indexed loads report in every build (the indexed-load message's format string is fixed) |
| 12 | `tests: cover indexed position and normal matrix loads from resolved arrays` | `LOAD_INDX_A` / `LOAD_INDX_B` from a big-endian guest array into PNMTX1; `gx_fifo_tests` 241 pass |

### The test (`tests/test_gx3d.py`)

No reference renderer exists, so the test checks what an independent reading of the disc can
check, from four runs (default, again, `--no-xlu`, `--puck-first`) at log level debug:

| Check | How | Verified |
|---|---|---|
| Inputs | the printed camera against RPGrpCamera's orbit formula in double precision (1e-4); the projection against EGG::Frustum's perspective (fovy 40, 608/456, near 10, far 100000; 1e-6 relative); the viewport; every world matrix against `test_scene.py`'s CalcWorld model | yes |
| Resolver | every address Aurora resolved during the model draw is an exact start, from the file, of a material display list (pix, TEV colour, indirect, texcoord-gen, TEV), a shape's pre-primitive (or its array part, +32, which `CallPrePrimitiveDisplayList` uses when the vertex descriptor is cached) or primitive list, a vertex array, or a texture image of the model's own BRRES; or inside the model's own `ScnMdl` buffers (copied materials); or the host's big-endian view-matrix copies; or StateGX's null texture (texture maps the draw does not use, resolved by Aurora at the next draw). Before the draw: only GX/EGG static copies and the light-object copy. Every shape's primitive list was called | yes |
| Log | no Aurora warning or error except the display-copy stub (§26) and the device-destroyed message at shutdown; no unresolved address, unhandled XF memory write or unimplemented indexed load; every unhandled register is in the documented list (below) | yes |
| Coverage | the triangles of the opaque byte code (decoded from the shapes' display lists and vertex arrays, transformed with the checked matrices, rasterised on the CPU with a depth buffer): pixels at least 1.5 px from any triangle edge must not show the clear colour where covered, and must where not. The table covers the whole screen, so only the first half is exercised | yes (covered half) |
| Depth | `--puck-first` (the puck's commands before the table's: the streams differ) gives the identical frame | yes |
| Materials | at pixels where the CPU depth buffer has `shade_L` / `shade_R` in front, 1.5 px from any edge (222151 pixels): the pixel lies within the 2x2 texel neighbourhood (decoded CMPR, `gxtex.py`) at the perspective-correct texture coordinate (TEV: TEXC x material colour white), within 5 per channel; result: every pixel inside, 0 off. `black` is never in front (the CPU depth buffer puts the shade quads over it everywhere). The corner lines `line1`, `line4`, `line6` (3 TEV stages, konstant colours, CLR0): the mean colour's dominant channel is the CLR0 colour's (R, B, G) | shade: yes; lines: hue only |
| Determinism | two runs byte-identical; `--gpu-compare` also runs on the Intel GPU (Mesa) and compares: max difference 2, mean 0.05 | yes |

Mutation checks while writing the test: projecting with y flipped fails the shade and hue checks
(21845 and 20723 pixels off, three hue errors); swapping the two shade textures fails 85000
pixels each.

Visually reviewed only (no independent model): the line materials' exact colours (three TEV
stages with an environment texture), the translucent glows (`line2`, `line5`, `line_C/L/R`:
additive, konst alpha), the puck's material, the overall look against the game. Without bloom
the neon glow is the baked `shade` textures and the translucent tubes only.

Documented unhandled registers (none affects this scene): BP 0x01-0x04 (copy filter sample
pattern), 0x49-0x4E and 0x53-0x54 (EFB copy source, destination, stride, scale, vertical
filter), 0x44 / 0x68 (field mask and mode), 0x45 (draw done; synchronous on the host), 0x46 /
0x69 (bus clocks), 0x23 / 0x24 / 0x67 (performance counters), 0x55 / 0x56 (bounding box reset),
0x58 (PE control bits), 0xF4 / 0xF5 (Z texture, disabled); CP 0x20 (performance counter
select); XF 0x1000 (error register), 0x1006 (performance counter select), 0x1012 (dual texture
transform enable: always on from `GXInit`; Aurora always applies the post-transform matrices).

Expected output (part of `make test` on 64-bit, after `test_gxlyt`):

```
test_gx3d: 608x456 after 2 frames on llvmpipe (LLVM 21.1.8, 256 bits); GX stream 17026 bytes
  inputs: camera, projection (fovy 40, 608x456, near 10, far 100000), viewport, 2 world matrices match the models
  resolver: draw GX/EGG static data 1, draw host big-endian copies 6, draw model buffers 19, draw model data 107, setup GX/EGG static data 1, setup host big-endian copies 1
  log: 3 allowed warnings (Device lost: Device was destroyed., STUB: display copy is not implemented); 28 unhandled registers, all documented
  geometry: 4218 opaque + 6188 translucent triangles; coverage judged on 240936 pixels (0 uncovered); puck-first frame identical: True
  materials: line1 2237 px; line4 2237 px; line6 2237 px; shade_L 111187 px; shade_R 110964 px; hue: line1 (248, 169, 145), line4 (185, 185, 254), line6 (188, 237, 123)
  determinism: two runs identical: True
errors: 0
OK
sys_harness: 6 heaps; FST 59 files 67 directories (sys/fst.bin and the tree agree); resources: 24 cached, 27 expanded, 384 archive files; save written, reloaded and reloaded by a second run; SC defaults and config, compared with host32
```

### Decomp changes (all byte-identical on the Wii, `check_matching`: 974 units identical)

| File | Change |
|---|---|
| `include/port/wgpipe.h` | `PORT_GP_STATIC_BE16`, `PORT_GP_STATIC_BE32`, `PORT_GP_LIGHT_OBJS`: `(p)` |
| `src/egg/gfxe/eggStateGX.cpp`, `eggDrawGX.cpp`, `eggLightTexture.cpp` | the static texture and vertex arrays through those macros (same lines: `eggDrawGX.cpp`'s `DECOMP_FORCEACTIVE` uses `__LINE__`) |
| `src/egg/gfxe/eggLightManager.cpp` | `LoadObjIndxGX`'s array base through `PORT_GP_LIGHT_OBJS` |
| `src/nw4r/g3d/g3d_draw.cpp` | `#if defined(PORT_HOST)`: `PortGXSetViewMtxArrays` after `SetViewPosNrmMtxArray` |

`check_matching.py`'s touched list has `eggStateGX.cpp`, `eggDrawGX.cpp` and
`eggLightTexture.cpp` (the other two were already there).

### Still open (milestone 3b and later)

- EFB copies: `GXCopyTex` into Wii memory (bloom, DOF, shadow textures and volumes, light
  textures, captures) and the CPU-visible ones (the Kokeshi icon into a `ResTIMG`, RFL faces:
  written back in the Wii texture format); the copy filter (anti-aliasing, vertical filter); a
  display copy without clear. Then this scene's bloom and the puck's shadow volume, and Shooting
  Range's light maps.
- `GXSetZTexture`, logic ops (DOF), fog range adjustment, lit materials (the light path runs
  here but no material uses it), `GXPoke*` / `GXPeek*`.
- `RPGrpModel*` on the host (§20's template-specialisation errors), so the harness can use the
  game's model layer instead of repeating its g3d calls; the paddles; then the full game frame
  loop (`RPGrpRenderer` views, layouts, effects) from the scene creator.
- Foreign-thread allocations (GPU drivers) out of the M0 arena.

## 28. Aurora milestone 3b: render to texture (Laser Hockey's bloom and shadow volume)

The EFB copy features are in: `GXCopyTex` into Wii memory through the address resolver, read back
in the Wii texture format, the copy filter, a display copy without clear, `GXSetZTexture` and logic
ops. `gx3d_harness` now draws Laser Hockey with the draw paths `RPHkyGame` creates (bloom with
`Environment/neon.pblm`, the shadow volume path), and the GPU drivers no longer allocate from M0.

### GPU-driver allocations out of M0

The executables define the global `operator new` / `delete` (the M0 arena). The linker exported
them, because `libstdc++.so` defines the same symbols, so every shared library in the process
bound to them, including a Vulkan driver's C++ code on its own threads (§27). Now every host
executable is linked with a version script, `port/host/exe.map` (`EXE_LDFLAGS`, in `CFLAGS` and
`CXXFLAGS`; compile-only runs ignore it), that makes `_Znw*`, `_Zna*`, `_Zdl*`, `_Zda*` local: the
executable's own code (the decomp and the port) still binds to them, every shared library keeps
libstdc++'s (malloc). `nm -D` shows no exported `operator new`/`delete`. Because libstdc++ can now
allocate a block (a `std::string` buffer, say) that an inlined destructor frees in the executable,
the port's `operator delete` (`host_sdk.cpp`) returns pointers outside the arena to `free`.

`PortArenaOtherThreadAllocs()` (`port/arena.h`) counts allocations made off the thread that created
the arena; `gx3d_harness` prints it (`ARENA_OTHER_THREADS`). Before: 7876 per run on Mesa's Intel
driver, 234457 on lavapipe (LLVM). After: 146, the harness resolver's own `std::map` entries (it
runs on Aurora's FIFO thread; the test requires at most the number of resolved addresses). Six runs
each on lavapipe and the Intel GPU: no crash, identical frames per driver. The arena keeps its lock.

### EFB copies (Aurora patches 13-14)

The SDK's `GXCopyTex` / `GXCopyDisp` reach Aurora as BP writes; with a resolver installed the
command processor decodes them (Aurora's own `GXCopyTex` API, which sets a host destination first,
is unchanged):

| BP | Use |
|---|---|
| 0x49 / 0x4A | source origin, size (minus one) |
| 0x4B | destination (address >> 5), resolved |
| 0x4D, 0x4E | stride, display copy y scale (shadow only: the copy size comes from 0x4A) |
| 0x52 | trigger: clamp top/bottom (bits 0-1), copy format (bit 3 high bit, 4-6), half size (box filter, bit 9), clear (11), display copy (14), intensity conversion (15) |
| 0x53 / 0x54 | vertical filter, seven 6-bit coefficients |
| 0x43 | PE control: format Z24 while the SDK makes a depth copy (`GXCopyTex` switches it); the colour format before it is the copy's source format |

Formats: colour RGBA8, RGB5A3, RGB565, CTF R4/R8/RA4/RA8/A8/G8/B8/RG8/GB8; intensity I4/I8/IA4/IA8;
depth Z8/Z16/Z24X8 and CTF Z4/Z8M/Z8L/Z16L (Aurora's existing conversion shaders). YUVA8 is
rejected with an error (unused). A copy is a GPU texture keyed by the resolved destination:
a later texture load (BP image registers, §26) from that address samples it. It stays
authoritative until another copy replaces it or the bytes at the address change: the hash of
the memory taken at the copy (and after its writeback) is checked at each texture cache
invalidation (`GXInvalidateTexAll`), as for any guest texture. Half-size copies sample the 2x2
corner of each block with linear filtering (the box average). The vertical filter samples the row
above, the row and the row below (one destination row apart, so two source rows for a half-size
copy) with the coefficients summed per row (2, 3, 2) over 64, clamped to the source rectangle
per the clamp bits, saturated; the SDK's "no filter" (0, 0, 21, 22, 21, 0, 0) is the identity and
skipped. Depth copies are not box filtered (nearest). The anti-aliasing sample pattern (BP
0x01-0x04) is ignored: the EFB is not multisampled.

Display copies: there is no XFB; the frame is presented from the EFB. A display copy that does not
clear makes the next frame load the EFB instead of clearing it. Its vertical filter (the render
mode's deflicker) and gamma are not applied to the presented image (a progressive host display;
the only gamma the game sets is 1.0, in the HOME menu).

**Writeback (CPU-visible copies).** `wpgx_set_copy_writeback(1, capture_sources, observer, user)`
(the harness turns it on): every texture copy to a resolved address is also read back, encoded in
the Wii texture format by Aurora's copy pack shader (tiled, big-endian; RGBA6 quantised), and
written to its destination in M0 when the frame ends (`wpgx_end_frame`, i.e. at `GXCopyDisp`), in
copy order, so the newest copy to an address wins, as on the hardware. Policy: the CPU sees a
copy's bytes from the end of the frame that made it. A copy that the CPU reads in the same frame
(after its own `GXDrawDone`) sees the old bytes: Aurora records a frame and runs it at the frame's
end, so a mid-frame readback would need a frame split (not needed so far: the RFL faces and the
Kokeshi icons are drawn as textures, which sample the GPU copy at once; their CPU readers, if any,
run in later frames). Aurora API: `AuroraSetCopyWriteback(cb, user, captureSources)` and
`AuroraFlushCopyWriteback()` (called by the glue after `aurora_end_frame`); the callback gets an
`AuroraCopyWritebackInfo` (destination, format, size, source rectangle, half, clear, depth, filter
weights, EFB format, the bytes, and with source capture the whole EFB colour just before the copy).

### Z textures, logic ops, fog range

- `GXSetZTexture` (BP 0xF4 bias, 0xF5 type and op; Aurora's own `GXSetZTexture` now encodes them):
  the last TEV stage's texel (sampled even when its colour inputs do not use it) as an 8-bit
  (`r`), 16-bit (`r << 8 | a`) or 24-bit (`r, g, b`) value plus the bias replaces the depth, or is
  added to it (`@builtin(frag_depth)`). Used by EGG's DrawGX Z clear, DOF, XluSnap and the shadow
  volume path's fog variant.
- Logic ops: WebGPU has none. Each op is the blend that gives the bitwise result for channel
  values 0 and 255, some with the shader writing `1 - colour` or `1`: CLEAR, AND (min), REVAND,
  COPY, INVAND, NOOP, XOR, OR (`s(1-d) + d`: also exact whenever either side is 0, the DOF case),
  NOR, EQUIV, INV, INVCOPY, INVOR, SET. REVOR and NAND have no such blend and are drawn as COPY and
  INVCOPY. The game uses COPY (EGG's process meter) and OR (DOF).
- Fog range adjustment (BP 0xE8-0xED) was already decoded and applied by Aurora (upstream test
  `FogRangeAdj_DecodesCenterAndCoefficientTable`); nothing was needed. Laser Hockey has no fog.

### Laser Hockey's draw paths in `gx3d_harness`

As `RPHkyGame` sets them up: model scene 0 with path flags 0x28 (`ScnRenderer::createPath` with
the shadow volume and bloom flags, as `RPGrpModelScene` maps them), the shadow volume path's flag
2 because bloom is on (`RPGrpDrawPathManager`), shadow colour (0, 0, 0, 0xC0), and the bloom
parameters from `Common/RPHkyScene/common.carc` → `Environment/neon.pblm` (`RPHkyGame::Load`,
`SetBinary`). The clear colour's alpha is 0, as `RPGrpRenderer::SetClearColor` sets it (the
shadow volume path counts in the EFB alpha). Per frame the bloom path makes 8 copies (EFB capture
at half size with the vertical filter, the mask (IA8), blur and gather buffers, the result) and
the shadow volume path 2 (the EFB alpha as CTF_A8, the stencil-like alpha with the blur filter):
11 per frame, all written back.

Found: Laser Hockey draws no shadow volumes. The path runs (steps at scene priorities 26, 51,
76), but nothing draws in its volume step (`RPGrpDrawPathManager`'s draw hook step 4: no Laser
Hockey object draws there; the party games that draw volumes are Table Tennis's ball, Fishing's hook
and Charge!'s goal), and
every Laser Hockey model is in draw group 0 (`Opa4th`, after the path), so it could not receive a
shadow anyway. The real frame is therefore the table with bloom. To exercise the path,
`--puck-shadow` adds a volume the way the party games draw one (`RP_drawShadowVolume`, `RPKcl_3.cpp`,
now linked: a 32-sided cylinder, here radius 26, height 200, at (30, 0, 30) beside the puck) in a
scene proc at the draw hook's step-4 priority, and puts the table in draw group 1 (`Opa1st`, before
the path) as a receiver; `--table-group1` is the same without the volume.

New options: `--no-bloom`, `--no-sv`, `--puck-shadow`, `--table-group1`, `--copy-dir DIR` (each
copy's bytes, and with `--capture-sources` the EFB before it). New output: `COPY` lines (index,
frame, destination, format, size, source, half, clear, depth, filter, bytes, EFB format, filter
weights), `COPIES_WRITTEN`, `WRITEBACK ok bad` (M0 against the newest copy to each destination
after the last frame), `SHADOWVOL`, `TIMING`, `PBLM`, `ARENA_OTHER_THREADS`.

### The tests

`test_gx3d.py` keeps every §27 check, on runs without the draw paths (`--no-bloom --no-sv`), and
adds:

| Check | How | Verified |
|---|---|---|
| Copies | frame 1's 11 copies, each decoded with `gxtex.py` and compared with an independent model built from the EFB read back just before it: source rectangle (clamped), 2x2 box for half size, the vertical filter with the decoded weights, the format's channels (BT.601 intensity + 16 for IA8), the RGBA6 EFB's 6-bit precision. A texel passes when it is the 6-bit value of an 8-bit value within 1 of the model's (the GPU filters with 8-bit weights) or within 2 | yes: every texel of every copy (max 1) |
| Writeback | every copy written back; M0 at each destination equals the newest copy to it | yes |
| Source capture | the frame with `--capture-sources` (a render pass split at every copy) equals one without | yes |
| Log | no Aurora warning or error but the device-destroyed one; unhandled registers all documented (the copy registers and 0xF4/0xF5 left the list; 19 remain) | yes |
| Bloom | the full frame against the frame without the draw paths: 143219 pixels brighter, none darker (beyond 4) | property only |
| Shadow volume | `--puck-shadow` against `--table-group1` (bloom on, no translucent pass, no puck): the table pixels whose surface point (CPU raster of the table's triangles with world positions) lies inside the 32-sided prism are the reference times 63/255 (shadow alpha 0xC0), within 5; every other pixel is unchanged; pixels within 2.5 units of the prism's outline (the blur filter widens the stencil by a row) are not judged | yes: 974 inside (max 0.5), 240238 outside identical |
| Arena | allocations off the main thread at most the resolver's map entries | yes |
| GPU (`--gpu-compare`, Intel UHD) | the frame without the paths: max 2, mean 0.05 (as §27); the full frame: max 27, mean 1.41 (the bloom's blurs and filtered copies amplify the drivers' filtering precision; limit mean 2) | tolerance only |

Aurora's googletests: `gx_fifo_tests` (246: copy register decoding, formats, half size, clamp,
filter weights, display copy, Z texture registers) and, new in `make gx`, `gfx_readback_tests` (24,
on lavapipe: a real device): half-size copy box filter and writeback bytes, the vertical filter's
rows and top clamp, a copy sampled by a later texture load until the CPU writes the memory and
invalidates, a display copy without clear keeping the EFB, Z texture replace and add read back
with `GXPeekZ`'s readback, all 14 exact logic ops on binary channels plus OR onto zero, an
alpha-only clearing copy.

Visual only: the bloom's look against the game (the composite's blend and colour are not
modelled; `full.png`), and how a real shadow would look (no Laser Hockey asset has one). The SV
path with bloom off (flag 2 clear, used by scenes without bloom) relies on receivers being drawn
after its first step with alpha updates off; with the table in group 1 the materials' alpha
(about 90) is everywhere, so the whole table is "shadowed" in both runs: not a Laser Hockey case
and not checked.

`gxtex.py` fix found by the IA8 copy: IA8 keeps alpha in the first (high) byte and intensity in the
second, like IA4's nibbles (Dolphin and Aurora decode it so); it had them swapped (texture and TLUT).

### Aurora patches (submodule working tree, not committed; series in the job's scratch)

| # | Commit message | What |
|---|---|---|
| 13 | `gfx: copy vertical filter, kept EFB after a display copy, GPU sync helpers` | `resolve_pass_into(..., CopyFilter)`, filtered copies always go through a conversion shader; `keep_efb_next_frame`; `finish_gpu_work` / `poll_gpu_events`; `capture_color_now`; test stubs |
| 14 | `gx: EFB copies, copy writeback, Z textures and logic ops for raw command streams` | above; `GXPipelineConfigVersion` 14 |
| 15 | `tests: cover EFB copies, copy writeback, Z textures and logic ops` | `gx_fifo_tests` +5, `gfx_readback_tests` +7 |

The glue's pipeline cache key is 3.

Expected output (part of `make test` on 64-bit; `make gx` prints the googletest lines first):

```
gx_fifo_tests: [  PASSED  ] 246 tests.
gfx_readback_tests: [  PASSED  ] 24 tests.
...
test_gx3d: 608x456 after 2 frames on llvmpipe (LLVM 21.1.8, 256 bits); GX stream 17026 bytes
  ...
  full frame (bloom, shadow volume): 22 EFB copies in 2 frames, all written back (8 destinations equal M0); arena allocations from other threads: 146 (the resolver's)
  copies (frame 1) vs model from the EFB before each: #11 0x27 608x456 max 0; #12 0x27 608x456 vf max 0; #13 0x6 304x228 half vf max 0; #14 0x6 152x114 max 0; #15 0x3 152x114 max 1; #16 0x6 152x114 max 0; #17 0x6 152x114 max 0; #18 0x6 152x114 max 0; #19 0x6 76x57 half max 1; #20 0x6 76x58 max 0; #21 0x6 152x114 max 0
  bloom (full frame vs without the draw paths): 143219 pixels brighter, 0 darker
  shadow volume: 974 table pixels inside darkened (max diff 0.5), 240238 outside unchanged
errors: 0
OK
```

### Still open (toward the game's frame loop)

- `RPGrpModel*` on the host (§20's template-specialisation errors), so the harness can use the
  game's model layer (`RPSimpleModel::LoadModel`, draw groups) instead of repeating its g3d calls;
  then the paddles and the puck's CLR0 animation.
- `RPGrpRenderer`'s views (3D view, two 2D views with layouts: the HUD, score and timer),
  `RPScnProcDrawHook` with the effect manager (the puck trail and paddle glows: `nw4r::ef`
  through GX), then `RPSysSceneMgr`'s frame loop driven from the scene creator: the health screen
  (§26), the title, the menus, a minigame.
- A copy the CPU reads within the frame that made it (a frame split at `GXDrawDone`), if a reader
  turns up; Shooting Range's light maps (`RPGrpDrawPathLightMap`, light textures captured with
  copies) as the next scene; lit materials; `GXPoke*` / `GXPeek*` on Aurora's readback.

## 29. Milestone 4: the game's own boot and frame loop, to the title screen

`build/host64/boot` is the game: the decomp's `main` (`src/main.cpp`, compiled as `PortGameMain`)
runs `RPSysSystem::initialize`, `create`, `setup` and `mainLoop` on the host platform layers. No
harness repeats game set-up any more: `RPSysSceneCreator` makes the boot scene
(`RPSysBootScene`: the health and safety screen, the static resources loaded on its task thread),
a scripted A press makes the scene manager fade to the Party Pack title scene (`RPCommonMessage`
in this project: the save-data check and `RPTitleLyt`), and the title screen runs with its music.
Headless on lavapipe, 420 frames (7 s of game time) take about 4 s.

### The host executable

| File | What |
|---|---|
| `port/host/src/host_main.cpp` | **New.** The entry point: arguments; what the IPL / apploader / OSInit leave (`PortOSInit`, the disc, the NAND, SC, `NANDInit`, `SCInit`); `Health_strapArchive` read from the disc's `sys/main.dol` (.data6); scripted remotes; the VI clock; Aurora (640x456 EFB, headless or `--window`), the address resolver over M0, copy writeback, the GX sink; then `PortGameMain`. Each `GXCopyDisp` ends an Aurora frame: the host prints a `FRAME` line, saves / hashes the frame read back, and ends the run after `--frames`. Host code here uses `malloc` only (`operator new` is the game's `EGG::Heap`). |
| `port/host/src/host_vi.cpp` | **New.** VI, time and alarms on a virtual clock (below). |
| `port/host/src/host_boot.cpp` | **New.** The stand-ins that remain (below). |
| `port/host/src/host_thread.cpp` | `PortOSSetClock` (time passes only when every thread is blocked and no device has work), `PortOSSpinWait` (a polling thread lets the others, the devices and the clock run), `OSSetThreadPriority` / `OSGetThreadPriority`. |
| `port/host/src/host_ax.cpp` | `PortAXIsInitialised`. |
| `port/host/src/host_text.cpp` | `PortSwapBmgText` marks a swapped BMG (last reserved header byte, `PORT_BMG_SWAPPED`) and returns at once for a marked one; `malloc` for its work flags. |
| `port/host/include/revolution/OS/OSMemory.h` | **New** shadow: `OSIsMEM1Region` / `OSIsMEM2Region` test the emulated address (`port_vaddr`). |
| `port/host/include/decomp/compat.h` | `PORT_TEMP_PTR(x)`: the address of a temporary through a const reference. |
| `host_sdk.cpp`, `host_snd.cpp`, `host_gfx_stubs.c` | `OSReport`, `VIWaitForRetrace`, `PortOSSpinWait` (a no-op), host_snd's DVD / NAND stand-ins, `RP_randSeedX/Y`, `RP_vecZero`: weak, so the real definitions win where they are linked. The boot therefore uses the game's own `OSReport` (empty, "stubbed for release"). |

`make boot` (64-bit, part of `make test`; `M32=1` and `NO_GX=1` print "skipped") builds the
executable and runs `tests/test_boot.py`. The link takes every unit of `RPKernel`, `RPSystem`,
`RPGraphics`, `RPAudio`, `RPUtility` and `RPParty/RPPartyCommon`, all of EGG, `nw4r` (ut, math,
ef, g3d, lyt, snd), the HOME Menu library, RVLFaceLib, AXFX, the GX SDK units of §26, the AX / MEM
/ OSArena / OSAlloc / DVD fs / KPAD / WENC units, `RPWlyGameConfig.cpp` (the party menus' Mii
entries) and the loader library, with `--gc-sections`: what the reached code needs stays.

```
boot --disc FILES --nand DIR --data-dir DIR [--sc FILE] [--frames N] [--input SCRIPT]
     [--save FRAME:PATH.rgba]... [--hash] [--audio PATH.raw] [--window] [--log-level N] [--trace]
```

Output: `BOOT` (adapter, EFB size), one `FRAME` line per displayed frame (index, retrace count,
game time in 1/60000 s, `RPSysSceneMgr`'s current scene ID, AX frames so far, with `--hash` an
FNV-1a hash of the frame), `SCENE` lines (with `--trace`) when the scene changes, `END`.
`--audio` writes the mixer's output (s16 stereo, 32 kHz).

### Time: the VI clock

Game time is virtual and deterministic, in units of 1/60000 s. It passes only when every
cooperative thread is blocked and no device (DVD, NAND) has work: the clock then jumps to the next
event and runs it in interrupt context: a **retrace** every 1001 units (NTSC; the host's retrace
hook applies the input script and `PortWPADRunFrame`, then the pre-retrace callback, the
`VIWaitForRetrace` sleepers, the post-retrace callback, i.e. `EGG::AsyncDisplay`'s frame tick),
an **AX frame** every 180 units (3 ms) once `AXInit` has run (`PortAXRunFrame`: the sound thread
wakes; 96 stereo samples to the audio sink), and **alarms** (`OSSetAlarm`, periodic alarms: the
HOME Menu, the remote speaker, RFL's retries). `OSGetTime` is the clock in bus ticks plus one
tick per call since the last event (busy-wait loops see time move); game time 0 is
2007-01-01 00:00. `OSTicksToCalendarTime` is the Gregorian conversion. VI records the frame
buffer, black and render mode; DTV status 0, interlaced scan: the NTSC 640x456 render mode.
The frame loop is the game's: `AsyncDisplay::beginFrame` sleeps on the retrace, copies the EFB
(`GXCopyDisp`: the host's frame boundary), the scene manager calculates and draws, audio and
controller managers run.

### Decomp changes (all byte-identical on the Wii; `check_matching`: 974 units identical)

| File | Change | Why |
|---|---|---|
| `RPGrpModelImplement.cpp` | `template <>` before `RPGrpModelAnmG3D::Construct` | GCC requires the explicit-specialisation syntax (§20's blocker) |
| `RPGrpModelAnmImplement.cpp` | `#if defined(PORT_HOST)`: declarations of the 11 specialisations used before their definitions | GCC needs a specialisation declared before its first use; any such declaration changes CodeWarrior's output (it places a specialisation by its first declaration), so the Wii build keeps none |
| `RPGrpModelMaterial.cpp` | `ReadEffectMtx`: on the host the effect matrix pointer is a native copy (`new MTX34(mtx)`) | the resource's matrix is big-endian; `SyncEffectMtx` writes the copy back through `ResTexSrt::SetEffectMtx` |
| `RPSysSystem.cpp` | `mMemorySize` read through `OSPhysicalToCached(0x28)` | the boot information is in M0, not at host address 0x80000028 |
| `eggProcessMeter.cpp` | the tick in an `OSMessage` read through `uintptr_t` | 64-bit cast |
| `eggMsgRes.cpp` | `#if defined(PORT_HOST)`: the constructor calls `PortSwapBmgText` (idempotent) | every BMG the game reads, whichever unit loads it, gets host-order text once (the title's "Press A and B." showed as "?" before) |
| `RFL_System.c` | `#if defined(PORT_HOST)`: `RFLWaitAsync` calls `PortOSSpinWait` while busy | a pure polling loop: nothing preempts it on the host, so the NAND callbacks never ran |
| `ef_drawbillboardstrategy.cpp`, `ef_drawdirectionalstrategy.cpp`, `ef_drawfreestrategy.cpp` | the static texture-coordinate arrays through `PORT_GP_STATIC` | the GP reads them (the cursor's particle trail is the first ef draw) |
| `RPBodyJoints.cpp`, `RPPlayerSettingPanel.cpp` | the `extern "C"` block around static member definitions is Wii-only | GCC rejects C linkage on members |
| `RPCollisionShape.cpp`, `RPDoll.cpp` | `&T(...)` is `PORT_TEMP_PTR(T(...))` (defined in the unit for the Wii, `(&(x))`, because these units use the decomp's precompiled header) | address of a temporary |
| `RPDebugGraph.cpp` | the `extern` redeclaration of `RPRumblePattern::sSlots` is Wii-only | GCC reads it as a second definition |
| `RPSysTagParms.cpp` | `writeString(const_cast<char*>(mpValue))` | `TagType` is `const char*` under `NONMATCHING` |
| `RPSceneInfo.h` | `#if defined(PORT_HOST)`: `extern "C" void RP_createSceneInfo();` before the friend declaration | the definition has C linkage |

Note on matching checks: Pack units are compiled with the decomp checkout's precompiled header
(`PartyPack.mch`), so a macro added to a header in that PCH is not seen by CodeWarrior; host-only
additions to such headers are guarded with `PORT_HOST`, and Wii-side macros go into the unit.

### What runs for real now

Everything on the boot path: `RPSysSystem` (initialize: heaps, `EGG::GraphicsFifo` / `GXInit`,
Video, XfbManager, AsyncDisplay, ProcessMeter, threads, Exception; setup: resource / assert /
reserve heaps, every singleton of `createFromSystemHeap`, the effect and sound work, save data,
HOME Menu, NWC24 manager, fonts, controllers), `RPSysSceneMgr` / `RPSysSceneCreator` / `EGG::SceneManager`
with fades, `RPSysBootScene` (its task thread loads the static archives, the message window,
system windows, the Kokeshi / RFL resources, effects, pause menu, HOME Menu button info, the
banner, the cache archives), `RPCommonMessage` (title), `RPGrpRenderer` (2D and 3D views,
`RPGrpDrawPathManager`, `RPScnProcDrawHook`), the `RPGrpModel*` layer (linked; the title draws no
model), `RPSysEffectMgr` / `RPSysEffectCreator` with `nw4r::ef` drawn through the real strategies,
`RPSysLayout`, `RPSysCursorDrawMgr` (the pointer and its trail), `RPSndAudioMgr` on the software AX
mixer, `RPSysCoreControllerMgr` / KPAD on the scripted WPAD backend, NAND saves. The harnesses'
stand-ins for these (§26-§28: hand-made `RPGrpModelScene`, `RPSysSystem::setup` parts, audio
manager set-up, cursor) are not used by the boot.

### Stand-ins that remain (`host_boot.cpp`)

| Kind | What |
|---|---|
| Data not attributed to a unit | `lbl_80316CB8` (EGG::Exception's pad combination), `lbl_80316CC8` ("timeStamp.txt", "Now Loading"), `Health_strapArchive` (filled from main.dol), `RPSndAudioActorBase::sActorList`, `AudioSound3DActor` / `AudioSoundActorBaseWithCamera<4>` statics, the `RPDollMgr` / `RPPlayerConfirm` / `RPSceneInfo` singletons, `ScnMdl1Mat1Shp`'s type name |
| No device | WiiConnect24 (`NWC24*` fail with `NWC24_ERR_FATAL`; the game records "not available"), the IPL ROM font (`OSInitFont` fails; drawing with it aborts), VF (none), `WPADReadFaceData` (no controller), reset / power callbacks recorded only, `OSReturnToMenu` / `OSShutdownSystem` exit (since §34 cleanly: the END line with `exit=menu` / `shutdown`, the window and audio closed) |
| Called by CodeWarrior name | `EGG_Vector2f_operator_minus/plus`, `__mi__`, `__pl__`, `__ml__`, `cross__` of `EGG::Vector3f`, `SetBrightnessColor__19RPSysKokeshiManager...`: forwarded to the real arithmetic / function |
| Not linked | the minigame scenes but Shooting Range (§31): their vtables are tables of a function that aborts with "a minigame scene is not linked" |

Not exercised yet: `AsyncDisplay::clearEFB` (its static Z texture would need `PORT_GP_STATIC_BE32`;
the boot always copies), the HOME Menu, the ROM font, Mii drawing (no Miis on a fresh NAND).

### The test (`tests/test_boot.py`)

Three runs: A (`tests/boot/title.txt`: remote 0 connects, A at retrace 200; 420 frames, hashes,
two frames and the audio saved), B (the same; determinism), C (no input, 300 frames).

| Check | How | Verified |
|---|---|---|
| Scenes | `RPSysSceneMgr`'s IDs against `RPSysSceneCreator.h`'s enum (parsed with `PACK_PARTY`): -1, 0 (boot), 25 (Party Pack title), the title within the 30-frame fade after the press; without input the boot scene stays | yes |
| Health screen | frame 150 against `test_gxlyt.py`'s model of `strap_00_U.brlyt` (independent decoding of main.dol / Yaz0 / U8 / BRLYT / TPL), with the layout's 608 units across the 640-pixel EFB: every pixel within 3 | yes: max diff 0 |
| Frames and time | consecutive frames, game time = retraces x 1001 units, retraces increasing | yes |
| Audio | AX frames = game time / 3 ms (from AXInit on), the raw file's size, sound after the title starts | yes (counts); the music itself is not compared |
| Log | Aurora warnings only the documented ones (device destroyed, a readback pending at shutdown), every unhandled register in test_gx3d's list plus BP 0x47 / 0x48 (draw sync token: synchronous on the host), nothing unresolved; no `boot:` / thread / arena message; the END line (no aborting stand-in reached) | yes |
| Determinism | 420 frame hashes and the scene trace identical over two runs | yes |
| Title | differs from the health screen; shows the orange "Play" logo (pixel count) | property only; the look is visual (`title.png` with `--png`) |

Expected output (part of `make test` on 64-bit, after `test_gx3d`):

```
test_boot: 420 frames on lavapipe; scenes -1 -> 0 (boot) -> 25 (title) at retrace 233, A pressed at 200
  health screen (frame 150) vs the strap layout model at 640/608: max diff 0, mean 0.043, 0 over 3
  title (frame 419): 220054 pixels differ from the health screen, 8971 orange logo pixels (visual check: title.png)
  time: 423 retraces, 2335 AX frames; audio after the title starts: peak 12831, rms 1853.5
  log: 1 allowed warnings (Device lost: Device was destroyed.); 21 unhandled registers, all documented
  determinism: 420 frame hashes identical over two runs: True; without input the boot scene stays: True
errors: 0
OK
```

No Aurora change was needed for this milestone.

### Next steps

- Menus, then each minigame: press A and B on the title (the save check, `RPPartyRootScene`,
  `RPAvatarScene` and the party menus with `RPDoll`, `RPPlayerSettingPanel`, Mii drawing through
  RFL), then link the minigame scenes one by one in place of the aborting vtables (their units'
  remaining GCC errors: §29's sweep found 18 in RPBil/Bom/Cow/Duc/Fsh/Hky/Pnp/Tnk/Wly units).
- A window and real input / audio devices: `--window` opens Aurora's SDL window but nothing
  presents it at the game's pace yet; SDL input into the §23 mapping layer; the AX output to an
  audio device.
- Pacing: game time runs as fast as the host can draw; a real-time mode should sleep to the
  next retrace and keep AX frames on the audio device's clock.
- The ROM font (a substitute font for `RomFont`), `AsyncDisplay::clearEFB`'s Z texture, the
  HOME Menu, NWC24 left unavailable.

## 30. Milestone 5: playable in a window; the menus after the title

`build/host64/boot --disc DIR --window` plays: an Aurora window, real-time pacing at the
NTSC 59.94 Hz, the AX mixer on the audio device, keyboard / mouse / gamepad input into the
§23 mapping layer. The deterministic headless mode stays the default and the test mode. The
game now runs past the title: the save check (creating the save file), Select Players, Select
Mii, and the party menu (`RPAvatarScene`) with its welcome message. How to build and run it
for a user: [RUNNING.md](RUNNING.md).

### Files

| File | What |
|---|---|
| `port/host/aurora/wpgx_platform.cpp` | **New**, in the Aurora glue library. SDL3 is linked statically into `libwiiplay_gx.so` and hidden, so the window, input devices and audio device are reached through new `wpgx_*` functions (`wpgx.h`): display aspect and window -> picture mapping, fullscreen, cursor; SDL events translated into a small `wpgx_event` queue (keys by SDL name, mouse, gamepads by slot 0-3, focus, resize, quit); gamepad gyro / accelerometer and rumble; an SDL audio stream (`SDL_OpenAudioDeviceStream`, s16 stereo 32 kHz, SDL resamples); test helpers that push synthetic SDL events and attach an SDL virtual gamepad (gyro, accelerometer, a rumble callback). |
| `port/host/aurora/wpgx.cpp`, `wpgx.h`, `CMakeLists.txt` | `wpgx_config` gains the window size, fullscreen, vsync and title. The EFB keeps the game's size in every mode (640x528 with a 640x456 picture, see the Mii drawing below): `VISetFrameBufferScale(1)` and `AURORA_VIEWPORT_FIT` (Aurora would otherwise size the EFB to the window); the window only scales the presented picture. `wpgx_begin_frame` queues the window events instead of dropping them. |
| `port/host/src/host_gxmisc.cpp` | `GXPixModeSync` runs the GX stream up to it (Mii drawing below). |
| `port/host/src/host_live.cpp`, `include/port/live.h` | **New.** The live layer: the pacer, audio output, input bindings and the config file, the synthetic-event script reader (tests). Uses wpgx only. |
| `port/host/src/host_vi.cpp`, `include/port/host.h` | `PortVISetPacer`: called before the virtual clock jumps to its next event (retrace, AX frame, alarm). Without a pacer the clock jumps at once, as before. |
| `port/host/src/host_main.cpp` | Modes and options (below); `Finish` (END and `LIVE` lines, shutdown) shared by `--frames`, the window's close button and quit; a window that refuses a frame (minimised) is retried while events keep being read. |
| `port/host/src/host_boot.cpp` | The ROM font stand-in (below) replaces the aborting `OSGetFontTexture` / `OSGetFontWidth`. |
| `port/extern/aurora` | One generic patch (below). |

### Modes and options

`--headless` (default): the virtual clock of §29, scripted remotes (`--input`), no devices;
every run is identical. `--window`: an SDL window and with it `--realtime`, `--audio-out` and
`--live-input`, which can also be given on their own (`--no-realtime`, `--no-audio` turn
them off). The test runs the live parts headless. Other options: `--fullscreen` (F11 /
Alt+Enter toggle), `--scale N` (initial window N x 640x456, 16:9: N x 853x456), `--no-vsync`,
`--config FILE`, `--speed F`, `--events FILE` (synthetic SDL events, tests). `--disc` takes the
extracted disc's root or its `files/`. `--nand` and `--data-dir` default to
`$XDG_DATA_HOME/wiiplay/{nand,aurora}`, the config to `$XDG_CONFIG_HOME/wiiplay/input.cfg`.
Writable paths (NAND, data, `--save`, `--audio`) inside the source tree (found from
`/proc/self/exe`: the directory above `port/host/build/host64` with `port/host/Makefile`) or
inside the disc are refused.

### Real time

The pacer anchors wall time to game time at its first call and sleeps (`sleep_until`) until
the wall clock reaches each event's game time / speed. Retraces therefore come every
1001/60000 s and the AX frames every 3 ms of wall time, and game time (`OSGetTime`) advances
in real time; the order of events is that of the headless mode. If the host falls more than
150 ms behind (a load, a stall), the pacer re-anchors instead of rushing (`late` in the
`LIVE` line). With audio, the device's clock paces the game: every AX frame moves the anchor
by 1 % of the audio queue's distance from its 50 ms target (a proportional controller with a
time constant of about 0.3 s), so over time the game produces exactly what the device
consumes. Presentation: each `GXCopyDisp` ends an Aurora frame, which presents to the window
with vsync (FIFO relaxed / FIFO) unless `--no-vsync`.

### Audio

The AX frame's 96 stereo samples go to the SDL stream as they are made. The stream starts
with 50 ms of silence. An empty queue when a frame arrives counts as an underrun (SDL plays
silence meanwhile); a queue over 300 ms (the device stalled) is dropped (overrun). `volume`
and `mute_unfocused` in the config set the stream gain. The remote speaker stays muted: its
ADPCM packets (§23's speaker callback) are not decoded yet; routing them to the same stream
at a low volume is the plan.

### Input

Each retrace, `PortLivePoll` pumps the window system and applies the events: remote 0 is the
keyboard and mouse (always connected) and gamepad 0; gamepads 1-3 connect remotes 1-3. The
mouse position goes through `wpgx_window_to_picture` (the letterbox Aurora presents with) and
`PortInputPointerFromWindow`. Keys and mouse buttons are matched by SDL name against the
bindings (§23's defaults plus Return = A, Backspace = B, H = HOME); held keys are tracked by
name and cleared when the window loses focus. Gamepads (SDL's gamepad API): south A, east B,
west 1, north 2, Start +, Back -, Guide HOME, D-pad and (without a Nunchuk) the left stick
the D-pad, right shoulder and right trigger B, left trigger Z, left shoulder C; the right
stick moves the pointer (1.5 screens per second) and the gyro too (yaw and pitch rates, 2
screens per radian); the accelerometer gives the remote's roll and pitch from gravity
(the pad lying flat = the remote pointing at the screen; shakes on top of gravity are not
mapped yet). Rumble: §23's motor callback starts / stops gamepad rumble (renewed every
retrace while on). The config file format is in RUNNING.md.

### Aspect

The picture is 640x456. 4:3 (SC) presents it with square pixels (640:456, as the 456 visible
lines of a 4:3 TV); 16:9 presents it 4/3 wider (853:456), as a widescreen TV stretches the
anamorphic picture. Aurora letterboxes / pillarboxes into any window size. The Aurora patch
`aurora_set_present_aspect` (in `include/aurora/gfx.h`, `lib/webgpu/gpu.cpp`): an optional
display aspect for `calculate_present_viewport`; 0 keeps Aurora's behaviour.

### The menus

| Screen | Scene | What it needed |
|---|---|---|
| Save check: "Create a save file for Wii Play?", "A save file has been created." | 25 (title, `RPCommonMessage`) | nothing new: the save file is written to the NAND directory |
| Select Players | 1 (`RPSysPlayerSelectScene`) | `RPSysLayout::forceBindAnmObjToPane` read the BRLAN's animation-content offset table as native `u32`: now `be_u32`, as `lyt_animation.cpp` does (a no-op on the Wii) |
| Select Mii (guest Miis), "You can't save My Record data...", "Play with this Mii." | 1 | stored Mii data (below) |
| Welcome message, party menu with the nine games | 27 (`RPPartyMainScene`: `RPAvatarScene`) | nothing new once the Miis load (`RPFaceCtrl::InitBlink` indexed its table with an uninitialised expression when `RFLInitCharModel` had failed) |

**Stored Mii data.** `RFLiCharData` / `RFLiHiddenCharData` (the NAND databases, the
remotes, the default table of guest Miis) are big-endian with bit-fields allocated from the
most significant bit (CodeWarrior). On the host `RFLi_Types.h` declares each 16-bit word's
fields in reverse order (GCC allocates from the least significant bit), and
`RFL_Database.c`'s converters (`convertRaw2InfoCore_`, `convertInfo2RawCore_`, the creator
name, `birthPadding`) work on a copy with every 16-bit word in host order except the bytes
0x16-0x1F (height, build, create ID). The stored data stays big-endian in memory, so NAND files
and remote data need no conversion; names in `RFLiCharInfo` are host-order `wchar_t`. Before
this every guest Mii failed `RFLiCheckValidInfo` (`RFLErrcode_Broken`).

**Mii drawing.** Three host problems, none in the decomp:

- *The EFB had 456 lines; the Wii's has 528.* EGG's light-texture pass
  (`LightTextureManager::drawAndCaptureTexture`) draws the Mii lightmaps below the picture
  (y from `efbHeight` to 528) and copies them from there; on the host those draws fell outside
  the EFB and the lightmaps came back white, saturating every head. `wpgx_init` now makes the
  EFB `max(height, 528)` lines with the picture (`xfbHeight`) the configured height, frame
  buffer scale 1 and the fit viewport policy in every mode; Aurora patch 0003 presents and
  reads back only the picture (the top fbWidth x min(xfbHeight, efbHeight)), copying it out at
  the frame's end when it is smaller than the EFB. Render modes whose EFB is the picture are
  unchanged. `wpgx_read_frame` still returns 640x456.
- *TEV register colour / alpha tracking* (Aurora patch 0002, `lib/gx/shader_info.cpp`): a
  stage writing a register's colour marked the whole register written, so a later stage reading
  its alpha (set with `GXSetTevColor`) got 0. The Mii body material writes C2 in stage 0 and
  reads A2 in stage 4: the bodies were black. Colour and alpha are now tracked apart, and a
  register is loaded when a part is read before any stage writes it. The pipeline cache key
  goes 3 -> 4.
- *The GP fell behind the CPU.* `RFLiMakeTexture` draws one Mii's face parts from temporary
  buffers, then frees and refills them for the next Mii with only `GXCopyTex` and
  `GXPixModeSync` in between; on the host the draws waited in the gather pipe until a later
  sync and read the next Mii's parts. The host `GXPixModeSync` (the SDK's pixel-pipeline
  flush, `host_gxmisc.cpp`) now runs the stream up to it (`PortWGPipeSync(PORT_GX_SYNC_DRAWDONE)`).

Aurora patches (generic, applied uncommitted in the submodule on top of `wiiplay` 047cddb):
0001 present aspect, 0002 TEV register components, 0003 picture of a taller EFB.

### The test (`tests/test_boot.py`)

New runs: **D** (first, alone: real time) runs `tests/boot/live.txt` headless with
`--realtime --audio-out --events`, SDL's dummy audio driver and no display variables: Space
key, mouse click, a virtual SDL gamepad (buttons, right stick, accelerometer), A+B on the title
from the pad, the pointer onto "Yes" (the game rumbles), then a quit event. Checks: the INPUT
trace (A from Space and the left button, pointer from the window centre and from (195, 348),
roll 30 from the accelerometer, the stick's pointer motion, A+B from south + east), the run
ends at the quit, wall time never shorter than game time (and within 8 % unless the machine
fell behind and the pacer re-anchored), audio from the dummy device without overruns, rumble
received by the virtual gamepad, the title reached. **M1 / M2** run `tests/boot/menus.txt`
(4900 frames) deterministically: scenes -1, 0, 25, 1, 27; the scene of eight saved screens;
the save file; eight distinct screens; identical frame hashes over both runs; no `boot:`
message (no aborting stand-in). A, B, C, M1, M2 run three at a time with `LP_NUM_THREADS=4`.
With `--png DIR` the menu screens are written as `menu_*.png` (a visual check: the Miis).

```
  menus: scenes -1 -> 0 -> 25 -> 1 -> 27 over 4900 frames; 8 screens (save_check, save_created,
    select_players, select_mii, guest_warning, play_with_mii, welcome, party_menu); save file
    written; hashes identical over two runs: True
  live (headless, dummy audio, synthetic SDL events): 12.361 s wall for 11.929 s game time; audio
    underruns 0, overruns 0, 2002 frames queued, 0 re-anchors; 26 events; rumble 2 on the virtual gamepad
```

The dummy audio device consumes about 3 % slower than its nominal rate, and the audio
controller follows it (the device paces the game); under `make -j` load the pacer re-anchors.

### The ROM font

The console's IPL ROM font is not on the disc. The game has one `RomFont`
(`RPSysFontManager`), used only by `RPSysTextWriter`: the title's build time stamp (after a
secret button sequence), "Now Loading" (unreachable: every scene loads with
`ELoadType_Blank`), the disc-error screen (unreachable: the host disc does not fail), the boot
scene's placeholder (unreachable in Party Pack) and Table Tennis' debug values. In normal play
only its metrics are read: Table Tennis and Laser Hockey call `SetFontSize(24)`. The stand-in
(`host_boot.cpp`): `OSInitFont` succeeds with a header of the ANSI ROM font's cell size (24 x 24,
ascent 20, descent 4; approximate) and a blank I4 glyph sheet in the game's buffer;
`OSGetFontTexture` / `OSGetFontWidth` step one character with half a cell's width. Metrics
work and nothing aborts; the debug text is invisible. If visible text is ever wanted, the
disc's `pac_nRodDb_32_I4.brfnt` (`Font/font.carc`) covers ASCII and Latin-1, but it is UTF-16
while `RPSysTextWriter` writes `char` strings: a CP1252 wrapper font would be needed.

### Verified headless vs. to try by hand

Verified: pacing arithmetic and real-time sleeping, audio stream feeding and the device
controller (dummy driver), the SDL event path for keyboard, mouse and a gamepad with sensors
and rumble, the bindings, quit, the menus' determinism and look (PNGs). Not verifiable here
(no display may be opened by tests): the window itself, vsync presentation, resizing and the
letterbox, fullscreen toggling, the cursor hiding, sound on a real device, gamepad feel (gyro
sensitivity and signs, tilt axes on real pads), mouse mapping in a scaled window.

### Next steps

- Each minigame: link its scene in place of the aborting vtable (§29's 18 GCC errors), then
  its own GX / data issues; Shooting Range first (the party menu's first game).
- The remote speaker: decode its ADPCM into the mixer's output.
- Gamepad shakes as remote acceleration (per-game tilt / swing bindings, §23).
- Mii names in host order everywhere they are drawn (the NAND Mii database, remote Miis); the
  eye whites on some guest Miis to compare against a Wii.
- The HOME Menu, `AsyncDisplay::clearEFB`'s Z texture, NWC24 (unavailable).

## 31. Milestone 6: the first minigame, Shooting Range

From the party menu, Shooting Range (`RPDucScene`, scene 18) runs with the game's own code:
the explanation screen, five rounds of targets (balloons, clay pigeons, bullseye and Mii-face
targets, cans, UFOs, ducks; the Miis walk in on the last round), the "Finished!" banner, the medal, the results
menu, and "Play a different game" back to the party menu. Headless and deterministic in the
test; playable with `--window` (aim with the mouse, a gamepad's right stick or gyro; fire with
B). No Aurora change was needed.

### Files

| File | What |
|---|---|
| `port/host/Makefile` | `RPParty/RPDucScene` joins the boot's units (all 36 build and link); `host_probe.cpp`, `host_wprintf.cpp`; the boot links with `--defsym=spCurrentScreen__13RPGrpRenderer=...` (`RPTextWriter.cpp` names `RPGrpRenderer::spCurrentScreen`, a private static, by its CodeWarrior name) |
| `port/host/src/host_boot.cpp` | Shooting Range's vtable stand-in removed (eight remain) |
| `port/host/include/decomp/compat.h` | `__fabsf` (the CodeWarrior intrinsic `RPDucAvatar.cpp` calls) |
| `port/host/src/host_wprintf.cpp`, `include/port/wide.h` | **New.** `swprintf` / `vswprintf` for the 16-bit `wchar_t` (below) |
| `port/host/src/host_probe.cpp` | **New**, boot only. Test probes: `PortGameProbe` (the FRAME line's `game=duc:STATE,ROUND,SCORE,HITS` from `RPDucGame` / `RPDucScoreMgr`) and `PortInputAimHook` (below). Reads only. |
| `port/host/src/host_input.cpp`, `include/port/input.h` | Script commands `aim 0|1` and `fire PERIOD` (§23); `PortInputAimHook` is weak, so the other harnesses link without a probe |
| `port/host/src/host_main.cpp` | `game=` in FRAME lines; up to 256 `--save`; `--dump-copies FIRST:LAST:DIR` (debugging: every EFB copy of those frames as written to memory, and the EFB before it) |
| `port/host/tests/test_boot.py`, `tests/boot/shooting.txt` | The menus runs continue into Shooting Range (below) |

### Decomp changes (byte-identical on the Wii; `check_matching`: 974 units identical)

| File | Change | Why |
|---|---|---|
| `RPDucField_2.cpp` | the byte-stepped walker over `mpGrass` steps `sizeof(RPDucShtGrass*)` instead of 4 (two rows) | 4 is the 32-bit pointer size; on the host the walker read between pointers (crash in `RPDucField::Reset` on entering the scene) |
| `RPDucHud_5.cpp` | `#if defined(PORT_HOST)`: `GetView(1)->GetScreen()` for the read of `RPGrpView` at offset 0x10 | 0x10 is `mpScreen`'s offset in the 32-bit layout; the Wii form is kept for its register allocation (NONMATCHING-DEBT note in the unit) |

### Wide printf

glibc's `swprintf` reads its format and writes its output in 4-byte characters; with
`-fshort-wchar` every number the game formats came out wrong: "Level %d" on the round banner,
"??" for scores and hit counts, the party menu's "Record: ... points". `port/wide.h` now
redirects `swprintf` / `vswprintf` (including `std::` and `nw4r::ut::TextWriterBase`'s
`vswprintf`) to `port_swprintf` / `port_vswprintf`: each conversion of the (ASCII) format is
formatted by the narrow `vsnprintf` and widened; `%s` takes a `char` string, `%ls` a
`wchar_t` string, as in C99 and MSL. The game's formats are `%d`, `%02d` and `%%%d.%df`.

### Aiming in the test (`host_probe.cpp`)

With `aim 1`, each retrace the probe picks a live target of `RPDucTargetRegistry`
(`mAge >= 0`, active, not yet hit by player 0; not the Mii avatars, manager type 6), keeps it
until it is gone, and projects its hit-sphere centre (`mPos + mHitOffset`) with the game's own
`RPDucCamera::ProjectToScreen`; the pointer goes there (y negated: the projection's y is up,
KPAD's down). Only while `RPDucGame::mInPlay`. `fire 6` presses B for 3 of every 6 frames.
The game then does its own ray test, hits, scoring, effects and sounds.

### The test

`tests/boot/menus.txt` followed by `tests/boot/shooting.txt` (runs M1 and M2, 16200 frames):
the party menu as before (§30), the Shooting Range icon, "A: Start", `aim 1` and `fire 6`
through the five rounds, then "Play a different game". Checks, in addition to §30's:

| Check | How | Verified |
|---|---|---|
| Scenes | -1, 0, 25, 1, 27, 18 (`ESceneID_RPDucScene`), 27 | yes |
| Game states | the (state, round) sequence of `RPDucGame` from the FRAME lines: intro, then start / play / end for rounds 1-5, the score wait, the medal, the results menu | yes |
| Score | changes only in play (state 3) or at a round's end (streak bonuses, state 4); goes up in play in every round; final score and hits > 0 | yes (the game's own state) |
| Sound | round 1's part of the mixer output (BGM, shots, hits) is not silent | rms only; the sounds themselves are not compared |
| Screens | eight frames (explanation, round start, a hit, targets, "Finished!", the medal, the results menu, back on the party menu) in the right scene and all distinct; PNGs with `--png` (`shoot_*.png`) | scene / distinctness; the look is visual |
| Log | Aurora warnings only the documented ones, every unhandled register documented, no `boot:` message (no aborting stand-in) over the whole run | yes |
| Determinism | every frame hash and the scene trace identical over M1 and M2 | yes |

M1 / M2 now take about 10 minutes each on lavapipe (16200 frames); `test_boot` as a whole about
13 minutes. Expected output (the new lines):

```
  menus: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 18 -> 27 over 16200 frames; 8 screens (...); save file
    written; hashes identical over two runs: True
  shooting range: states 1/1 -> 2/1 -> 3/1 -> 4/1 -> 2/2 -> ... -> 4/5 -> 5/5 -> 6/5 -> 7/5; score 534
    (179 hits) over 204 score changes in play, every round scoring: True; sound in play: rms 4248.6;
    8 distinct screens; log: 1 allowed warnings, 21 unhandled registers, all documented
```

The script is not a good player on purpose only in one way: it shoots whatever it finds first,
including the targets that cost points (the -3 popups), so its score is not a high score.
Back on the party menu the game unlocks stage two ("Stage two is now open!").

### Rendering checks (one-off, not in the test)

- *Lit materials* (`sht_field`'s grass, ground and trees, the targets, cans, balloons, UFOs:
  lighting on in channel 0, ambient and material colour from registers, diffuse clamp, spot
  attenuation, light set 0). At run time g3d's light set 0 has one light (white, at a far
  position: directional; cosine and distance attenuation (1, 0, 0)) and an ambient of
  (110, 110, 110). An instrumented Aurora build (shader output: ambient, the sum of
  attenuation x diffuse, the number of lights in the mask) gave, on a bullseye target facing
  the camera: one light in the mask, ambient 0.37, diffuse 0.53; the normal build draws the
  white ring at 220 of the texture's 239 (0.92, clamp(0.37 + 0.53) = 0.90). (Darker targets
  in a frame of an earlier build, at about 0.35, were not re-examined.)
- *The "black ball"* in the clay rounds is a clay pigeon seen from below: `m_clay_ura`'s
  texture is dark grey.
- *Light textures (EFB copies).* With `--dump-copies`, each frame of the scene captures two
  32x32 RGBA8 light textures from below the picture (y 456), and two more later in the frame;
  the diffuse one is a lit sphere, the specular one a highlight at the upper left.
- *Fog*: no `RPDuc` unit configures fog; the scene's `EGG::FogManager` keeps its default.

### The Mii faces' bright side (from §30's PNGs)

Some guest Miis in Select Mii have a pale, nearly white patch on one side of the face. The
face material (`RPGrpModelRfl`, material type 0/1) is: stage 1 `lightmap0 x RASA + RASC`
(RASC: the ambient, the colour channel has no lights; RASA: the alpha channel's lights),
stage 2 that x C2 (the face and skin colour), stage 3 `+ lightmap1 x RASA` (specular). The
texture coordinates are the view-space normal: s = 0.5 + 0.5 nx, t = 0.5 - 0.5 ny.

- An independent model of the light-texture pass (`eggLightTexture.cpp`: the normal map read
  through the indirect stage, ITF_8 with the -128 bias, the matrix -dir x 0.484375 at scale
  2^-1, the 64x4 pattern ramp) against Aurora's shader: the same ramp texel to within 0.01
  texel over a whole row; the texgen of the face material matches too.
- The captured light textures (Select Mii, frame 2890): diffuse up to 204 (0.80, brightest at
  the top), specular up to 101 (0.40, a highlight at the upper left); neither saturates.
- So a light-skinned face gets `min(1, 0.8 + ambient) x skin + 0.4` at the upper left: white.
  The pale patch is what this material computes from these light textures; nothing in the
  Aurora path was found to differ from the hardware. Not compared with a capture from a
  Wii (none available here): the light colours and the specular pattern come from
  `RFL_Res`/Kokeshi light data that was not checked against hardware.

### Verified headless vs. to try by hand

Verified: the scene's units on the host, the whole game flow from the party menu and back,
scoring from hits (the game's own state), determinism, the log, sound being produced during
play. Visual only: the look of every Shooting Range screen (PNGs), effects (the hit bursts,
feathers, can and target pieces drawn by `nw4r::ef`), the HUD (score, hit counter, bonus,
"Level N"), the medal and results. By hand with `--window`: aiming with the mouse and gyro,
the feel of the B trigger on a gamepad, the sounds and music on a device.

### Next steps

- The other eight minigames: link each scene in place of its aborting vtable; §29's GCC
  errors in their units first (RPBil/Bom/Cow/Fsh/Hky/Pnp/Tnk/Wly), then their 32-bit layout
  assumptions (pointer-size strides, hard-coded offsets: the two found here were found by
  running, a sweep of `reinterpret_cast` with constants in each unit is cheaper).
- The test's runtime: M1 / M2 replay the menus before Shooting Range; a NAND save state
  would not help (the game state is in RAM). Running fewer lavapipe threads per run and
  more runs at once may.

## 32. Milestone 7: every minigame linked; Laser Hockey plays; a fast `make test`

All nine minigame scenes now link into the boot with the game's own code; the aborting
vtable stand-ins are gone. Laser Hockey (`RPHkyScene`, scene 17) is the second game played
headless from the party menu to the results and back. The other seven enter, start and draw
their first gameplay frames (smoke runs, below). `make test` is fast again (the full games
moved to `make playtest`).

### Tests: `make test` and `make playtest`

| Target | Runs | Wall time (lavapipe, 12 cores) |
|---|---|---|
| `make test` | `test_boot.py`: A / B (the title, twice: determinism), C (no input), D (real time, SDL events), **M** (fresh NAND: menus, then a short Shooting Range: round 1 until hits score, + and "Quit" back to the party menu), **H** (a save with every game open: menus, then a short Laser Hockey: play until the paddle meets the puck, + and "Quit") | about 7 minutes for `test_boot` alone; 8 minutes for the whole `make -j8 test` (measured with one other boot run on the machine) |
| `make playtest` | `test_play.py`: **P1 / P2** (fresh NAND: menus and the whole Shooting Range, §31), **Q1 / Q2** (a save with every game open: menus and a whole Laser Hockey game, the results, "Play a different game"); every frame hash and the scene trace identical over each pair | 35 minutes (measured with another boot run on the machine; P1 / P2 are 16200 frames, Q1 / Q2 14600, four at once with `LP_NUM_THREADS=3`) |

The coordinator runs `make playtest` before committing a game milestone. `M32=1` and `NO_GX=1`
print "playtest: skipped". Scripts: `tests/boot/menus.txt` + `shooting_smoke.txt` (M),
`menus_saved.txt` + `hockey_smoke.txt` (H), `menus.txt` + `shooting.txt` (P),
`menus_saved.txt` + `hockey.txt` (Q).

**A save with every game open.** Wii Play opens the games one by one (each finished game
opens the next), so Laser Hockey is the fifth. `test_boot.party_save` writes `RPParty.dat` in
the game's format (`RPSysSaveData::write`: "RPPT" "0000", `RPPartySystemData` with its game
flags at 0x20, the player lists, then the 16-bit sum / inverted-sum checksum of the written
part at 0x50A0) and an all-zero `banner.bin` (the game only checks that it exists; it writes
a real one whenever it saves). Run M checks the format: the save the game itself writes on a
fresh NAND is byte-identical to `party_save` with only the first game open. With the save the
title goes straight to Select Players, and the party menu opens with "The final stage is now
open!" for the last game.

### The other minigames: build sweep (identical on the Wii; `check_matching`: 974 units identical)

§29's GCC errors (18 units) and the link:

| File | Change | Why |
|---|---|---|
| `RPSndObject.h` | `this->getIndexedHandle(index)` in the `RP_SND_INDEXED_HANDLE*` macros | a member of a dependent base (C++ two-phase lookup) |
| `RPBilSound.cpp`, `RPFshFish.cpp`, `RPWlySound.cpp` | the explicit instantiations name `ui32` instead of `unsigned int` | `ui32` is `unsigned int` on the Wii (its overloads' other type); on the host u32 is `unsigned int`, so the old spelling instantiated the u32 overload twice |
| `RPBilBall.cpp` | its dead-stripped stand-in (`extern "C" static`, kept for .sdata2 order) is Wii-only | GCC rejects `extern "C" static`; `extern "C" { static ... }` changes CodeWarrior's label numbering |
| `RPBilPlayer.cpp` | `PORT_TEMP_PTR` for three `&` of temporaries; under `PORT_HOST` the `ChangeState` specialization is declared before `RPBilPlayer.h` uses it | C++ rules; the declaration shifts CodeWarrior's label numbers, hence host-only |
| `RPBomScore.cpp` | `nw4r::ut::Max<s32>(score, 0)` instead of `Max(score, 0L)` | `0L` is s32 only where s32 is `long` |
| `RPHkyPaddle.cpp` | `PORT_TEMP_PTR(puckPos - sPrevPuckPos)` | the address of a temporary |
| `RPPnpBall.cpp` | braces around `case 7` | a jump past an initialisation |
| `RPTnkBlock.cpp` | under `PORT_HOST` the local union gets a name and an empty constructor | a union member with a constructor (MTX34's) |
| `RPTnkScene.cpp` | under `PORT_HOST` a constructor function in place of `__declspec(section ".ctors")` | GCC has no such declspec |

Link (`host_boot.cpp`, `Makefile`): Pose Mii's nine singletons (`.sbss`, no defining unit
yet); `PADControlMotor` (no GameCube ports); CodeWarrior-named calls forwarded to the real
code: `EGG::Vector3f` unary minus and division, `EGG_Vector2f_operator_mul`,
`EGG::Matrix34f::calcRPY`, `EGG::CoreStatus::getHorizon` / `getDpdPos`,
`EGG::CoreController::getAccel` / `getDpdValidFlag`, `EGG::ShadowTexture::BeginDrawQuad` /
`DrawQuad`, `RPTnkHideExMsg__Fv`; `RPBilTable::GetPocketPos(s32)` over its explicit ABI entry;
data by CodeWarrior name through `--defsym` (`EGG::Screen::sTVModeWidths`,
`EGG::DrawGX::s_cameraMtx`).

### 32-bit and big-endian assumptions found (identical on the Wii)

The sweep (`reinterpret_cast` and byte offsets, `u32` casts, strides, `sizeof`) over the eight
games' units, then the runs:

| File | Change | Found by |
|---|---|---|
| `RPBilResult.cpp` | `PORT_HOST`: `pMgr->GetPlayer(player)` for the read at `pMgr + 0x10 + player * 4` | sweep (32-bit offset and pointer stride) |
| `RPFshResultLayout.cpp` | `PORT_HOST`: `fish->GetScreenPos()` for the read at offset 0x70 | sweep (32-bit offset) |
| `RPPathFollower_2.cpp` | `PORT_HOST`: `rot = matrix.calcRPY()` for the call that passes the result vector as a hidden first argument | sweep of `extern "C"` declarations with two signatures (the PowerPC ABI's struct return; x86-64 returns 12 bytes in registers) |
| `RPActorList.h` | the list head as `static_cast<RPActorListHead*>(this)` before the cast to `RPActorLink*` | Charge! crashed creating its actors: with GCC the vtable pointer comes before the non-polymorphic base, CodeWarrior puts the base first |
| `eggAnalizeDL.cpp` | display-list and vertex reads through `be_u16` / `be_s16` / `be_f32` | Table Tennis hung computing a model's bounds (`ModelEx::getShapeMinMax`: a byte-swapped primitive count) |
| `RPTnkAi_2.cpp`, `RPTnkAi_4.cpp`, `RPTnkTank.cpp`, `RPTnkPlayerController.cpp` | the per-tank records of `param/TnkGameParam.bin` as `be_s32` / `be_f32` | Tanks! (Phase 6's pass converted only the stage records) |
| `RPTnkAi_2.cpp`, `RPTnkAi_4.cpp` | `PORT_PPC_MOD(x, d)` (compat.h) for the random interval `x % range` | Tanks!: a tank type that lays no mines has a zero range; PowerPC's `divw` does not trap and the remainder comes out as x, x86's `idiv` raises SIGFPE |

Also: `-fno-aggressive-loop-optimizations` for the host build (`RPWlyStartCountLyt::Reset`
runs over four animation pointers through a three-entry array and the member after it, as the
original does; GCC warned it would assume the fourth iteration unreachable).

The hand-made vtables in `RPTnkScene.cpp` (and their `- 0x24` thunks) are CodeWarrior-named
data that GCC's own `_ZTV10RPTnkScene` replaces; nothing references them on the host.

### Aurora: a copy writeback the CPU has overwritten is dropped

Laser Hockey crashed in `RPHkyField::Reset`: its model object had been overwritten by an
EFB-copy writeback. A texture copy reaches memory when its frame ends (§28's policy); the party
menu's last frame copied its light textures into memory that the scene change freed and the
new scene's loading thread reused for the rink model, all before that frame ended. On the
hardware the copy reaches memory when the GPU executes it, so the CPU's later writes win.
Aurora now hashes a copy's destination when it records the copy and, at the frame's end,
drops the writeback of every destination whose bytes have changed since (checked for all of
the frame's copies before any is written: two copies to one address still give the newest).
Patch: `gx: drop a copy writeback whose destination the CPU wrote after the copy`
(command_processor.cpp, +21 lines); `gx_fifo_tests` and `gfx_readback_tests` pass.

### Laser Hockey in the test (`host_probe.cpp`)

The probe reports `game=hky:STATE,TIMER,SCORE0,SCORE1,PUCKX,PUCKZ,DIST` while the scene is
running (states: 0 intro, 1 play, 2 goal, 3 time up, 4 wait, 5 result; the timer counts down
from 7200; DIST is the puck's distance from player 0's paddle). The probes now check the
current scene (`RPSysSceneMgr`), so a game's singleton is never read after its scene ends.

`aim 1` in Laser Hockey: `RPHkyPaddle::UpdateControl` maps the pointer to the rink through
`RPGrpCamera::GetScreenToPlane` (then halves x, moves it to player 0's side, scales z). The
probe evaluates the same mapping and solves it for a target with a few Newton steps (finite
differences, from the last solution): the target is the puck's centre plus one puck radius in
the direction from the paddle, so the paddle strikes through the puck. The game does its own
collision, scoring and sounds.

| Check (Q1 / Q2; H for the short run) | How | Verified |
|---|---|---|
| Scenes | -1, 0, 25, 1, 27, 17 (`ESceneID_RPHkyScene`), 27 | yes |
| Game states | Q: intro, play, goals, time up, wait, result (H: intro, play) | yes |
| Clock | starts at 7200 in play and counts down one a frame (or stands while paused) | yes |
| Contacts | the paddle reaches the puck (DIST <= 40) and the puck travels away from the centre line | yes (the game's own positions) |
| Goals | a score changes only by one, as a goal starts (state 2); player 0 scores (Q) | yes |
| Screens | start, rally, goal, Game Over, My Record, the results menu, back on the party menu: in the right scene, all distinct | scene / distinctness; the look is visual |
| Log, determinism | as §31 | yes |

Expected output (`make playtest`):

```
test_play: 4 runs on lavapipe in 2103 s wall
    shooting range: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 18 -> 27 over 16200 frames; states 1/1 ->
      2/1 -> 3/1 -> 4/1 -> 2/2 -> 3/2 -> 4/2 -> 2/3 -> 3/3 -> 4/3 -> 2/4 -> 3/4 -> 4/4 -> 2/5 ->
      3/5 -> 4/5 -> 5/5 -> 6/5 -> 7/5; score 534 (179 hits) over 204 score changes in play, every
      round scoring: True; sound in play: rms 4248.6; 8 distinct screens; log: 1 allowed warnings,
      21 unhandled registers, all documented
        hashes and scenes identical over two runs: True
    laser hockey: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 17 -> 27 over 14600 frames; states 0 -> 1 ->
      2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2
      -> 1 -> 2 -> 1 -> 2 -> 1 -> 3 -> 4 -> 5; 7202 frames in play, the clock counting down from
      7200; 105 paddle-puck contacts, the puck up to 510 from the centre line; goals 12-0 (final
      12-0); 7 distinct screens; log: 1 allowed warnings, 21 unhandled registers, all documented
        hashes and scenes identical over two runs: True
errors: 0
OK
```

Expected output of `make test`'s `test_boot` (the new lines):

```
    menus: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 18 -> 27 over 6450 frames; 8 screens (save_check,
      save_created, select_players, select_mii, guest_warning, play_with_mii, welcome,
      party_menu); save file written (the format party_save writes)
    shooting range (short): states 1/1 -> 2/1 -> 3/1; score 13 (10 hits) over 11 score changes in
      play, round 1 scoring: True; sound in play: rms 3288.1; 5 distinct screens; log: 1 allowed
      warnings, 21 unhandled registers, all documented
    laser hockey (short, a save with every game open): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 17 ->
      27; states 0 -> 1; 318 frames in play, the clock counting down from 7200; 3 paddle-puck
      contacts, the puck up to 56 from the centre line; goals 0-0 (final 0-0); 5 distinct screens;
      log: 1 allowed warnings, 21 unhandled registers, all documented
```

### The other minigames (smoke runs, not in the test)

With the all-games save, each game from its party-menu icon ("A: Start", then about 700
frames, pointer at the screen centre, no other input):

| Game (scene) | Result |
|---|---|
| Shooting Range (18) | plays (§31; `make playtest`) |
| Laser Hockey (17) | plays (above; `make playtest`) |
| Find Mii (22) | "Level 1: Find 2 look-alikes!", the crowd and the timer; + and Quit back to the party menu |
| Table Tennis (19) | "Rally 100 times!", the table, paddles and HUD; back to the party menu. Visual: the net is drawn in the first frames of play and missing in later ones (not examined) |
| Pose Mii (24) | "Match that pose!", the Mii and the bubbles; back to the party menu (after the `RPBomScore` fix below). Visual: thin white horizontal lines at the bottom of one frame of the intro |
| Billiards (20) | the cue ball with the Mii face, the racked balls, the aiming help; back to the party menu |
| Fishing (16) | the pond, fish, the rod, Bonus fish; back to the party menu |
| Charge! (21) | the cow and rider, "Tilt the Wii Remote on its side"; back to the party menu |
| Tanks! (23) | "Mission 1", the arena, the player and enemy tanks moving; back to the party menu |

None crashes now; the crashes found on the way are the fixes listed above (Charge!: the actor
list head; Table Tennis: the display-list analyser; Tanks!: the parameter records and the zero
divisor; Laser Hockey: the copy writeback). Pose Mii crashed when quitting: `RPBomScore`'s
reset loops write word offsets counted from `this` (the 32-bit layout, kept for the original's
register allocation), and on the host the first one overwrote the vtable pointer;
`BOM_SCORE_WORDS(this)` (`PORT_HOST`: past the host's longer vtable pointer) fixes it,
identical on the Wii.

### Verified headless vs. to try by hand

Verified: every minigame unit builds and links (64-bit); Laser Hockey's flow from the party
menu and back, its clock, contacts and goals from the game's own state, the log, determinism;
the short Shooting Range and Laser Hockey runs in `make test`; the save format. Visual only:
the look of Laser Hockey (the rink's neon and bloom, the puck, the HUD, Game Over, My Record)
and of the seven other games' first frames. By hand with `--window`: Laser Hockey with the
mouse (the paddle follows the pointer; twisting needs a gamepad's gyro or the D-pad bindings
of §30), and the other games (some need tilt or a Nunchuk: Charge! asks to hold the remote on
its side; Tanks! uses the Nunchuk or the D-pad).

### Next steps

- Each remaining game played through headless (Find Mii, Table Tennis, Pose Mii, Billiards,
  Fishing, Charge!, Tanks!) with a probe and an input strategy like these, then into
  `make playtest`.
- The smoke-run visual oddities (Table Tennis's net, Pose Mii's lines) against a capture.
- Laser Hockey's twist (the paddle angle from the remote's roll) is not driven by the test.
- `-fno-aggressive-loop-optimizations` hides one class of out-of-bounds indexing; a sweep for
  the others (arrays indexed into the next member) would let it go.

## 33. Milestone 8: every minigame played through; the net and the lines

All nine minigames are now played headless from the party menu to their results and back by
`make playtest`, each by a game-side player that reads the game's own state and works the
virtual remote through the §23 input layer (pointer, buttons, twist, tilt, acceleration). The
two visual oddities of §32's smoke runs were examined against the game's logic: Pose Mii's
lines were a 64-bit bug (fixed); Table Tennis's net is covered by the game's own message
window (not a bug). Playing Charge! found two more 64-bit bugs (fixed).

### Files

| File | What |
|---|---|
| `port/host/src/host_probe_games.cpp` | **New**, boot only. `PortGameProbeMore` (the FRAME line's `game=` for the seven games: `wly:`, `bom:`, `pnp:`, `bil:`, `fsh:`, `cow:`, `tnk:`) and `PortInputPlayHook` (a player per game, below). Reads only. |
| `port/host/src/host_probe.cpp` | `PortGameProbe` falls through to `PortGameProbeMore` |
| `port/host/src/host_input.cpp`, `include/port/input.h` | Script commands `play 0|1` (the weak `PortInputPlayHook` sets the whole remote each retrace) and `shake X Y Z N` (§23) |
| `port/host/tests/play_games.py` | **New.** The seven games' runs (frames, screens) and checks |
| `port/host/tests/test_play.py` | Runs them twice each with Shooting Range and Laser Hockey; `--only`, `--jobs`; `make playtest PLAYTEST_ARGS=...` |
| `port/host/tests/boot/{findmii,posemii,tabletennis,billiards,fishing,charge,tanks}.txt` | **New.** The icon, "A: Start", `play 1` |
| `port/host/tests/layout_asserts.cpp` | Run-time check: `RPColliderList`'s head lies over an `RPCollider`'s links (below) |
| `port/host/tests/test_input.py`, `tests/input/tilt.txt` | `shake` in the script model and the tilt script |
| `port/host/Makefile` | `host_probe_games.cpp` in the boot; `PLAYTEST_ARGS` |

### Decomp changes (byte-identical on the Wii; `check_matching`: 974 units identical)

| File | Change | Why |
|---|---|---|
| `RPBomBorder.cpp` | `SLOT_MODEL(p, i)` / `SLOT_OFFSET_Z(p, i)`: `p->mpModels[0]` / `p->mOffsetZ[0]` on the Wii, `mpModels[i]` / `mOffsetZ[i]` under `PORT_HOST` | the loops walk a pointer from `this` one pointer at a time, which is one `mOffsetZ` (f32) and one `mpModels` element only with 4-byte pointers. On the host the walk read `mOffsetZ[0]`, `[2]` and the first word of `mNodeMtx[0]`: Pose Mii's three borders were misplaced (below) |
| `RPColliderList.h` | `PORT_HOST`: a pointer-sized first member | the list head doubles as a sentinel `RPCollider` (`AsCollider`): `mpHead` / `mpTail` / `unk8` must be where an `RPCollider` has `mpNext` / `mpPrev` / `unk8`. CodeWarrior puts `RPCollider`'s vtable pointer after its members, GCC first: an `Append` to an empty list wrote the collider over `mpTail` and the head stayed empty, so Charge!'s cow met no scarecrow, hurdle, tree or goal (and a later `Remove` wrote outside the list) |
| `RPCowGoal.cpp`, `RPCowHud_3.cpp`, `RPCowHud_4.cpp`, `RPCowHud_8.cpp` | `PORT_HOST`: `RP_Mtx34_getColumn(pOut, mtx, col)` through the definition's own form (`EGG::Vector3f RP_Mtx34_getColumn(const EGG::Matrix34f&, int)`) | these units declare it in the PowerPC ABI form of that definition (a returned struct through a hidden first argument, the matrix by address); on x86-64 the call passed garbage: Charge! crashed in `RPCowPerfectMsg::Show` on the first group knocked down. A sweep of every `extern "C"` function declared with two return types found only these (and `Cow_stopBgm`, bool / void, whose result is never used) |

### The two visual oddities of §32

**Pose Mii, thin white lines in the intro.** They are the game's three miss-counter borders
(`RPBomBorder`, `border_model`), which drop in one after another during the intro and then lie
16 units apart at the bottom (one breaks on each miss). With the slot-walk bug above, border 0
used its own offset, border 1 `mOffsetZ[2]`, border 2 a matrix element: they dropped at the wrong
times and settled as two lines. Fixed: three lines 16 apart (frames compared before and after).
*Check:* the `bom:` probe reports the three borders' z from their models' local matrices
(`ScnObj::GetMtx(MTX_LOCAL)`); `check_posemii` requires 160 / 160 (x10) in every frame of play.

**Table Tennis, the net missing after the first frames.** Frame by frame (every 10 frames from
the explanation screen): the net is drawn until the "Hold your paddle." window appears, then
only its two ends show, at the window's edges. The window (`pingpongMsg_01.brlyt`,
`N_msgWidow_00` at y +20 in the layout's 608 x 456 centre space, `W_msgBg_01` 452 x 52) covers
y 182-234 of the 640 x 456 EFB, where the net is in the serve-wait camera; the layout's own
data, read independently with `fmt_layout.py`, puts it there. Once the paddle is held the window
goes and the net is drawn in every frame of play. Nothing in the game starts the table's
`pin_table_a` / `_b` animation or hides a model before a net hit (`RPPnpTable::HandleBallCollision`
only, from `StepPhysics(TRUE)`). Not a bug; no change. *Check:* `net_drawn` in `play_games.py`:
the share of dark (mesh) pixels in the net's band, about 0.45 in play and 0.07 under the window;
the two play screens of the Table Tennis run must have more than 0.3.

### The players (`host_probe_games.cpp`)

Each retrace with `play 1`, the player of the scene running sets remote 0 from the game's own
state; on every game's results (`RPMedalResult`, `RPResultMenu`) it points at "Play a different
game" (window 320, 348) and presses A every 150 frames. The games' KPAD settings damp small
pointer moves (`pos_play_radius` 0.06), so where accuracy matters the pointer is servoed: the
error between the commanded position and what KPAD reported last frame is added back.

| Game | How it plays (the game's own data) | The end |
|---|---|---|
| Find Mii | The targets are `mpAvatars[0 .. mTargetNum-1]` (copies of avatar 0); the pointer goes to `RPWlyAvatar::mScreenPos` (projected by the game each frame), A when the game's own hover (`mHoverIdx`) is on it; look-alikes stay hovered until all are picked | after 5 stages it picks wrong Miis (each takes 5 s off) until the time is up |
| Pose Mii | The lowest falling bubble (`RPBomPanel`, largest z): the pointer at its next position (`mPos + mVel` over the cursor's range), the twist `(mAngle + mRotSpeed) / 4` (the Mii turns by four times the roll), A / B along the pose order 1 -> 2 -> 0 with the game's cooldown; the start bubble while waiting | at level 2 it stops matching; the third miss ends the game (in practice the misses come in level 2's first waves) |
| Table Tennis | The racket waits on the plane z = 165; x where the ball, moving straight in x (`RPPnpBall::Integrate`), crosses it; the pointer from the game's screen-to-plane mapping solved by Newton steps; A to have the CPU serve | after 30 rallies the racket stays away |
| Billiards | The ghost-ball point to pot the lowest ball in the pocket with the smallest cut whose first contact (`RPBilBallMgr::RaycastObjectBalls`) is that ball; the D-pad orbits the camera onto that heading; the pointer on the cue ball (`mCueBallScreenPos`), B, pull back (distance 1.2 -> 1.35), thrust (acceleration -z for 10 frames), release B; ball in hand: A at a few spots until allowed | all nine object balls off the table |
| Fishing | Point at the rod's grip to cast; then the hook to 40 units in front of the nearest fish's mouth (not the "suka", which costs points), still while it comes to bite; on a bite (FIGHT) a yank: acceleration y -1.2 (KPAD acc y about -2.2) for 3 frames | the clock |
| Charge! | Pointer off the screen, the remote on its side: roll 45 (full speed); steer by pitch toward the next scarecrow group along the course (the groups line it), then its nearest standing scarecrow, then the goal; a jerk (acceleration y -2.5 for 3 frames) about 16 frames before a hurdle | the goal (time bonus) |
| Tanks! | D-pad (no Nunchuk) along a breadth-first path over the stage's cells to a cell that sees the nearest enemy; then still, the turret on the enemy (the pointer from `RPTnkCamera::ProjectToScreen`), B when the turret's line passes within 12 of it | after mission 2 it sits still: the enemies of mission 3 take its lives |

### The test (`make playtest`)

Each game twice from the all-games save (menus as `menus_saved.txt`, then the game's script),
four runs at a time; Shooting Range and Laser Hockey as before. Per game: the scene trace
(-1, 0, 25, 1, 27, the game, 27), the frame count, five or six screens (start, play, a scoring
moment, the results, back on the party menu: each in its scene, all distinct; PNGs with
`--png`), the log (documented Aurora warnings only, no `boot:` message: no stand-in reached),
every frame hash and the scene trace identical over the two runs, and the game's own state:

| Game | Checked (the game's own state) |
|---|---|
| Find Mii | states intro, question, search, clear ... result; at least 5 stages cleared (stage 6 reached); finds only from the search and never more than the stage's targets; wrong picks seen (the penalty) and taking time off |
| Pose Mii | states 0, 1, 2, 3, 6, 7, 9, 11, 12; a level cleared and at least 50 bubbles matched; the score changes only with a match (or the level bonus); all misses used at the end; the borders 16 apart in every frame of play |
| Table Tennis | states 0-5; at least 20 rallies; the hit count changes only in the rally, by one; the net drawn in play (screens) |
| Billiards | states intro, start, play, score, ... game over; all 9 object balls off; balls leave the table only during a shot; the lowest ball only goes up |
| Fishing | start states intro, wait, count, play; player states aim, wait, fight, land; at least 3 fish, points > 0, changing only with a catch; the clock runs out; the result reaches the medal |
| Charge! | states 1, 4-8, the goal (9-14), 22, 24; points from scarecrows, only in play (and the time bonus at the goal); the cow runs over 3000 along the course |
| Tanks! | mission 1 cleared (its enemy destroyed, then stage 1); kills only in play, one at a time; lives lost one at a time; failed (9) and the end menu (12) |

`make test` keeps its short Shooting Range and Laser Hockey runs only: it takes about 8
minutes (474 s measured, `make -j8 test`), and each further game would add a run of at least
4000 frames (the menus alone are 3400), past the 10-minute budget.

Expected output of `make playtest` (the new lines; four runs at a time on 12 cores, each at
about 5 frames a second; 5697 s, about 95 minutes, for the 18 runs; the shooting and hockey lines as in §32):

```
test_play: 18 runs on lavapipe in 5697 s wall
  find mii: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 22 -> 27 over 8600 frames; states 0 -> 1 -> 2 ->
    3 -> 0 -> 1 -> 2 -> 3 ... 4; 5 stages cleared (stage 6 reached); 10 wrong picks, each taking
    time off; time up; 5 distinct screens; log: 1 allowed warnings, 21 unhandled registers, all
    documented
    hashes and scenes identical over two runs: True
  tanks: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 23 -> 27 over 8900 frames; states 0 -> 1 -> 2 -> 4
    -> 6 -> 7 -> 2 -> 4 -> 5 -> 7 -> 2 -> 4 -> 6 -> 7 ...; missions cleared 2, 2 enemy tanks
    destroyed, lives 0 at the end; 5 distinct screens; log: 1 allowed warnings, 21 unhandled
    registers, all documented
    hashes and scenes identical over two runs: True
  billiards: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 20 -> 27 over 11100 frames; states 0 -> 1 -> 2
    -> 3 -> 2 -> 3 -> 2 -> 4 -> 2 -> 3 -> 2 -> 4 -> 2 -> 4 -> 2 -> 3 -> 2 -> 3 -> 2 -> 3 -> 2 ->
    3 -> 5; 10 shots, 3 fouls, all 9 object balls off: True; 5 distinct screens; log: 1 allowed
    warnings, 21 unhandled registers, all documented
    hashes and scenes identical over two runs: True
  fishing: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 16 -> 27 over 12900 frames; start states 1 -> 2 ->
    3 -> 4; 7 fish caught, 960 points; the clock ran out (6059 frames); result state 8; 5
    distinct screens; log: 1 allowed warnings, 21 unhandled registers, all documented
    hashes and scenes identical over two runs: True
  table tennis: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 19 -> 27 over 7400 frames; states 0 -> 1 -> 2
    -> 3 -> 4 -> 5; 30 returns by player 0, 30 rallies (final hit count 61); the net drawn in
    play (dark share 0.45, 0.45); 5 distinct screens; log: 1 allowed warnings, 21 unhandled
    registers, all documented
    hashes and scenes identical over two runs: True
  pose mii: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 24 -> 27 over 8750 frames; states 0 -> 1 -> 2 ->
    3 -> 6 -> 7 -> 8 -> 3 -> 4 -> 3 -> 4 -> 3 ...; levels 2, 187 bubbles matched, score 192, 3/3
    misses; borders 16 apart in all 2621 frames of play; 5 distinct screens; log: 1 allowed
    warnings, 21 unhandled registers, all documented
    hashes and scenes identical over two runs: True
  charge: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 21 -> 27 over 10300 frames; states 0 -> 1 -> 2 -> 4
    -> 5 -> 6 -> 7 -> 8 -> 9 -> 10 -> 11 -> 12 -> 13 -> 14 -> 22 -> 24; 193 points from
    scarecrows, 17 groups knocked; ran 16297 along the course; goal: True; 6 distinct screens;
    log: 1 allowed warnings, 21 unhandled registers, all documented
    hashes and scenes identical over two runs: True
errors: 0
OK
```

### Verified headless vs. visual only

Verified (the game's own state, the scene trace, the log, determinism over two runs): every
minigame entered from the party menu, played by input to its results and back; the progress
and score changes listed above; Pose Mii's borders in place; Table Tennis's net drawn in play
(a pixel measure); the collider list and the column call fixed (Charge! scores, jumps hurdles,
reaches the goal). Visual only (PNGs `<script>_<screen>.png` with `--png`): the look of every
game's screens, effects, HUDs and results; what the window over Table Tennis's net looks like
on a Wii was not compared with a capture (the layout data puts it there). By hand with
`--window`: the games with a real remote or a gamepad (twist, tilt and the Nunchuk mappings of
§30; Tanks! with the D-pad or the left stick).

### Next steps

- The players are deliberately simple (Pose Mii loses in level 2, Table Tennis stops at 30
  rallies, Tanks! stops after mission 2); longer runs would cover the later levels and
  missions (Tanks!' 20, Find Mii's 99 stages) at the cost of run time.
- Two-player modes are not driven.
- `make playtest` is long (four runs at a time); running the second run of each pair with
  hashes only, or fewer lavapipe threads and more runs at once, may shorten it.
- Sweeps done here: `reinterpret_cast<T*>(this)` over `include/` and `src/` (only
  `RPColliderList::AsCollider` casts a class to an unrelated polymorphic type; the others are
  vector types) and two-signature `extern "C"` declarations (above). Other 64-bit layout
  assumptions may remain in code no player reaches (two-player modes, later levels).

## 34. Milestone 9: the HOME Menu

HOME on the title, the menus or in a minigame opens the game's own HOME Menu:
`RPSysHomeMenuMgr` (RPKernel) driving homebuttonMiniLib (`HBMInit` / `HBMCalc` / `HBMDraw`,
its layouts, pointer hit tests, the Wii Remote Settings panel, the Yes / No dialogs), drawn
over the paused game, with its sounds through the AX mixer. Close (or HOME) returns to the
game, which goes on where it was paused; Reset is the game's own soft reset; Wii Menu shuts the
game down and ends the program. Nothing in the decomp or in Aurora had to change: the earlier
layers cover the whole path (§16 the HOME Menu files, §21 the mixer and its aux callbacks, §23
the WPAD backend, §26-§28 GX, §29 the alarms on the virtual clock). New on the host are a clean
exit for `OSReturnToMenu` / `OSShutdownSystem`, a probe of the HOME Menu's state, a script
command that points at a HOME Menu pane, and the tests.

### Files

| File | What |
|---|---|
| `port/host/src/host_probe_hbm.cpp` | **New**, boot only. `PortHomeProbe`: the FRAME line's `home=` field (below). `PortInputHomeHook`: the `hbm PANE` script command. Reads only. |
| `port/host/src/host_input.cpp`, `include/port/input.h` | Script command `hbm PANE` / `hbm none` (§23) and the weak `PortInputHomeHook` |
| `port/host/src/host_main.cpp` | `home=` in FRAME lines; `PortBootExit(reason)`: the END line gains `exit=menu` / `exit=shutdown`, then the usual shutdown (live statistics, audio device, Aurora) |
| `port/host/src/host_boot.cpp` | `OSReturnToMenu` / `OSShutdownSystem` call `PortBootExit` (they printed a `boot:` line and `_Exit`ed before: no END line, the window and audio device not closed) |
| `port/host/Makefile` | `host_probe_hbm.cpp` in the boot |
| `port/host/tests/test_boot.py` | Runs K, T1 / T2, W; the HOME keys in run D (below) |
| `port/host/tests/boot/home_party.txt`, `home_title.txt`, `home_menu.txt` | **New.** The runs' scripts |
| `port/host/tests/boot/live.txt` | Escape, H and the gamepad's Guide button at the end of run D |
| `port/docs/RUNNING.md` | The HOME Menu for players |

No decomp file changed (`check_matching.py`: all identical, as before). No Aurora change.

### How it runs (the game's own path)

| Step | Code |
|---|---|
| HOME pressed | `RPSysCommonObject::preUpdate` -> `RPSysHomeMenuMgr::update` -> `checkMenuStart`: a `downTrigger` of HOME on any core controller; if the scene allows a scene change now (else it waits: `mIsStarting`) and the menu is not disabled (else the ban icon: `RPMenuScene`, Select Players and Select Mii, disables it; each scene's set-up enables it again through `RPSysCommonObject::init`), menu state Start and `RPSndAudioMgr::enterMenu`: every sound player but the pause player pauses (fading out), the effects are cleared, the remote speakers disconnect. Live mode: Escape, H, a gamepad's Guide (§30); tests: `press HOME`. |
| Start -> Open | when `RPSndSpeakerMgr::isDisconnectAllFinished`: `HBMInit` (the library's sound init: its AXFX reverb on aux A through its own allocator hooks, `AXRegisterAuxACallback`, the aux return volumes), `stopMotorAll`, the DPD and KPAD parameter stacks pushed, the scene's `pauseCallBack(true)` |
| Open | each frame the core controllers' `KPADStatus` are copied for `HBMCalc`; the library maps a pointer to its layout (`HomeButton::update_controller`: x 304, y 228 per unit, times `adjust` for 16:9) and hit-tests each pane with the inverse of its global matrix (`PaneComponent::contain`); `HBMDraw` draws its layout, the four cursors and its fader in the common 2D view 31 at draw step 5 (`RPSysCommonObject::UserDraw`), after the game's own scene, which the game keeps drawing, paused (this game makes no EFB capture of it; in the frames it shows dimmed under the menu's layout) |
| Sounds | the library's `play_sound` calls the game's `soundCallback` -> `RPSndHomeMenuArcMgr::startSe` (`HomeButtonSe.brsar`, nw4r::snd) -> the software AX mixer; the reverb runs as the aux A callback |
| Wii Remote Settings | battery from `WPADGetInfoAsync` (the backend's level, the script's `battery`), volume `WPADGetSpeakerVolume` / `WPADSetSpeakerVolume`, rumble `WPADIsMotorEnabled` / `WPADEnableMotor`, `WPADSaveConfig` (the host keeps settings in memory, §22); Reconnect: `WPADDisconnect` on all four, `WPADStartFastSimpleSync`, the connect window for its own time (the backend's `WPADDisconnect` drops a remote until the input layer sets it again at the next report, so a connected remote is back at once) |
| Close / HOME | `HBM_SELECT_HOMEBTN`: menu state End, `exitMenu` (the sound players resume, the effects are set again), the stacks popped, `pauseCallBack(false)` |
| Reset | the dialog, Yes: `HBM_SELECT_BTN2` -> `RPSysSceneMgr::softReset(true)`, `stopAllSoundOnPauseOff`; the scene manager (soft reset, then guard) changes to the Party Pack title (`RPSysSceneCreator::changeSoftReset`) and `RPSndAudioMgr::resetSystem`. This is Wii Play's own reset, inside the process; the game never calls `OSRestart`, so no re-exec is needed. From the party menu it goes through `RPPartyRootScene` (26) to the title (25). |
| Wii Menu | the dialog, Yes: `HBM_SELECT_BTN1` -> `RPSysSceneMgr::returnToMenu(true)` (`VISetBlack`, saves disabled), the scene manager fades out to a dummy scene change, and `RPSysSceneCreator::destroy` (the current scene ending) calls `RPSysSystem::systemShutDown` (motors off, up to 300 retraces for NAND and the sound shutdown) -> `OSReturnToMenu` -> the host's END line with `exit=menu` and a clean exit (status 0) |

The console's own Reset and Power buttons have no host equivalent (`OSSetResetCallback` /
`OSSetPowerCallback` are recorded only); closing the window still ends the program directly.

### The probe and the `hbm` command (`host_probe_hbm.cpp`)

`home=MENU,ACTIVE,VISIBLE,SEQUENCE,STATE,SELECTED,POINTED,SOUND,BATTERY,VOLUME,RUMBLE,SCENEMGR,BAN`
while the menu is open or opening, a HOME Menu sound is attached, the scene manager is not in
its normal state or the ban icon shows (the library's fields `-` when it is not active):

| Field | Source |
|---|---|
| MENU, ACTIVE, VISIBLE | `RPSysHomeMenuMgr`: menu state (0 end, 1 start, 2 open, 3 reset), `mIsActive`, `mIsVisible` |
| SEQUENCE, STATE, SELECTED | `HomeButton`: `mSequence` (0 normal, 1 Wii Remote Settings, 2 dialog), `mState`, `mSelectBtnNum` (0 Close / HOME, 1 Wii Menu, 2 Reset) |
| POINTED | the pane a press of remote 0 would go to, as `gui::Manager::update` decides: the last component in layout order that the library's hit test has pointed and that takes presses now |
| SOUND | the sound `RPSndHomeMenuArcMgr`'s handle holds (`HBMSound`: 0 HOME, 1 return, 2 Wii Menu, 3 reset, 4 focus, 5 select, 7 / 8 settings open / close, 9 / 10 volume + / -, 14 / 15 rumble on / off); the handle keeps the latest sound only, so two sounds started in one frame show as one |
| BATTERY, VOLUME, RUMBLE | what the settings panel shows for remote 0: `mWpadInfo[0].battery`, `mVolumeNum`, `mVibFlag` |
| SCENEMGR | `RPSysSceneMgr`'s state: 0 normal, 1 guard, 2 soft reset, 3 return to menu, 4 shutdown |
| BAN | the ban icon (`RPSysHomeMenuMgr::BanIcon`): 0 hidden, 1 fading in, 2 shown, 3 fading out |

`hbm PANE`: the pane's rectangle (`GetPaneRect` with the library's `DrawInfo`) through its
global matrix gives its extent in the layout; the bars (`B_btn_00`: Close, `B_bar_10`: Wii
Remote Settings) reach past the screen, so the target is the centre of the part a pointer can
reach (within +-0.95), mapped back through `update_controller` (dividing by 304 / 228 and the
adjust factor). Panes: `B_btnL_00` Wii Menu, `B_btnL_01` Reset, `B_btn_00` Close,
`B_bar_10` the settings bar (open and close), `B_optnBtn_00` / `01` volume - / +,
`B_optnBtn_10` / `11` rumble on / off, `B_optnBtn_20` Reconnect, `B_BtnA` / `B_BtnB` Yes / No.

### The test (`make test`: `tests/test_boot.py`)

| Run | Script | Checked |
|---|---|---|
| K | `menus_saved.txt` + `home_party.txt` (5000 frames) | two menu sessions, on the party menu and in Laser Hockey; at every scripted press the library's own hit test has the pointer on the pane meant (8 presses); the panel opened and closed (sequences 0, 1, 0), volume 7 -> 6 -> 7, rumble on -> off -> on, battery 4 (the script's remote); both sessions end with Close (selection 0); the sounds in order: HOME, settings open, volume -, volume +, rumble off, rumble on, settings close, return, then HOME, return in the game; in the mixer's output every sound after the game's sound has faded peaks over 400 within 15 frames while the game is silent (rms under 50 before the first pointer move), and the game's sound is back after Close (rms over 300); Laser Hockey in play at the press, its clock standing while the menu is open and counting down again after (at least 60); the party menu takes the next input (Laser Hockey starts); five screens in their scenes, all distinct; the log |
| T1, T2 | `home_title.txt` (1800 frames, the save) | on the title: the panel, Close; then Reset: the dialog (sequence 2), selection 2, menu state 3, the scene manager soft reset -> guard -> normal; the sounds HOME, settings open / close, return, reset; then A and B reach Select Players (scenes 0, 25, 1), where HOME opens no menu and the ban icon fades in, shows and fades out; every frame hash and `home=` field identical over the two runs; the log |
| W | `home_menu.txt` | Wii Menu, Yes: selection 1, the scene manager returning to the menu (3), the sounds HOME and Wii Menu, then END with `exit=menu` before the script's end (the game's shutdown, then `OSReturnToMenu`); the log |
| D | `live.txt` | the Escape key, the H key and the gamepad's Guide button each give HOME (synthetic SDL events), and the menu starts after Escape |

PNGs with `--png DIR`: `home_party.png` (the HOME Menu over the party menu),
`home_settings.png` (the Wii Remote Settings panel), `home_party_resumed.png`,
`home_hockey.png` (over Laser Hockey), `home_hockey_resumed.png`, `home_ban.png` (the ban icon on
Select Players).

Expected output (the new lines of `test_boot`; `make -j8 test` took 8 minutes with them,
measured: the three long runs M, K, H go first, three at a time):

```
  home menu over the party menu and Laser Hockey: sessions at retraces [(3001, 3619), (4701,
    4879)]; 8 presses on the panes meant (the library's hit test); sounds home/settings_open/
    volume_minus/volume_plus/rumble_off/rumble_on/settings_close/return then home/return (19
    starts), peaks 519-18612 in the mix, the game's sound while open rms 40.3, after rms 853.3;
    hockey clock 6969 at the press, 123 counted after; 5 distinct screens; log: 1 allowed
    warnings, 21 unhandled registers, all documented
  home menu over the title, then Reset: sessions at retraces [(401, 799), (901, 1192)]; 5 presses
    on the panes meant; sounds home/settings_open/settings_close/return/reset; scene manager 0 ->
    2 -> 1 -> 0; scenes 0 -> 25 -> 1; HOME on Select Players: ban icon 0 -> 1 -> 2 -> 3 -> 0;
    hashes identical over two runs: True
  home menu, Wii Menu: 2 presses on the panes meant; scene manager 0 -> 3; END exit=menu after
    711 frames
  live (headless, dummy audio, synthetic SDL events): 13.256 s wall for 11.929 s game time; audio
    underruns 0, overruns 0, 1522 frames queued, 3 re-anchors; 31 events; rumble 2 on the virtual
    gamepad
```

(Run D has five more events than in §30: the HOME keys.)

### Verified headless vs. visual only vs. by hand

Verified (the game's and library's own state, the mixer's output, the log, determinism): the
menu opening from HOME on the title, the party menu and in Laser Hockey (the ban icon instead
on Select Players); the pointer's hit
tests and presses on Close, the settings bar, volume, rumble, Reset, Wii Menu and Yes; the
settings panel's values from the WPAD backend; the sounds through the mixer while the game is
paused; the game paused and going on (Laser Hockey's clock); Reset back to the title and on to
Select Players; Wii Menu ending the program cleanly; the live keys. Visual only (the PNGs): the
look of the menu, the panel, the dialogs, the cursor. In a development run, not in the test:
Reconnect (the connect window, the remote back after it), the Reset dialog and fade over the
party menu (through `RPPartyRootScene` to the title). By hand with `--window`: the menu with
the mouse and a gamepad, its sounds on a real device, Wii Menu closing the window.

### Next steps

- The remote speaker: the HOME Menu also plays its sounds on the remotes' speakers (the
  connect sounds, `RemoteSpk`); they reach the backend's speaker callback and stay muted like
  the game's (§30).
- 16:9: the library scales its layout by `adjust` (`RPSysHomeMenuMgr::update`); the `hbm`
  mapping divides by it, but no test runs the HOME Menu in 16:9 yet.
- HOME while a scene cannot change (a load, a fade) is deferred by the game (`mIsStarting`);
  not driven by a test.
- Several remotes in the HOME Menu (four cursors, the settings panel's batteries) and the
  Classic Controller path (`HBMKPadData::use_devtype`) are not driven.

## 35. Performance: the tracker, live graphics settings, copy writeback, pacing, audio, the cursor

The windowed game was reported "extremely laggy" (then: the NVIDIA driver broken, so the
default adapter was the integrated GPU or the CPU), and after a reboot (RTX 3070 Laptop GPU) still
slow, with sound at the wrong speed and a cursor trailing the mouse. This milestone adds a
per-frame performance tracker first, then fixes what it shows. How to use it:
[RUNNING.md](RUNNING.md#performance---perf-and-f3).

### Files

| File | What |
|---|---|
| `port/host/src/host_perf.cpp`, `include/port/perf.h` | **New.** The tracker: per displayed frame the wall time and its phases, pipelines, audio, the cursor; the CSV, the summary, the overlay text. |
| `port/host/src/host_main.cpp` | `--perf`, `--perf-out`, `--gpu`, `--live-gfx` / `--test-gfx`, `--async-pipelines` / `--blocking-pipelines`, `--mailbox` / `--fifo`, `--writeback on|off`; the GX sink and frame boundary timed; the KPAD cursor sample; no `FRAME` lines in a window unless traced or scripted. |
| `port/host/src/host_live.cpp`, `include/port/live.h` | The pacer's sleep timed; F3; the audio controller bounded, re-priming after an underrun, skipping AX frames over a high mark, the device format logged; `pointer_stabilise` and the `KPADSetPosParam` wrapper. |
| `port/host/aurora/wpgx.cpp`, `wpgx.h`, `wpgx_platform.cpp` | Adapter choice and list, adapter type and backend, present mode, mailbox; the performance counters (`wpgx_get_perf`); the ImGui overlay (`wpgx_overlay`); the pipeline cache key from the library's build ID; the audio device's format. |
| `port/host/Makefile` | `host_perf.cpp`; `boot` linked with `-Wl,--wrap=KPADSetPosParam`. |
| `port/extern/aurora` | Three generic patches (below). |

### The tracker (`--perf`, F3)

A frame runs from one Aurora frame begun to the next (each `GXCopyDisp` ends one). Its wall time
splits into phases, each timed where it happens; only clocks are read on the game's path, so the
deterministic runs are unchanged:

| Phase | Where |
|---|---|
| `gx` | the GX sink: `wpgx_fifo_write`, flushes, draw-done syncs (a draw waiting for its pipeline lands here) |
| `end` | `wpgx_end_frame` up to the writeback: FIFO drain, recording, hand-over to the render thread |
| `gpu` | waiting for the GPU: the copy writeback (`AuroraFlushCopyWriteback`), `wpgx_read_frame` |
| `present` | `wpgx_begin_frame`: waiting for a free frame slot (the GPU or the present queue is behind) |
| `sleep` | the pacer's `sleep_until` |
| `input` | `PortLivePoll` (window-system events) |
| `calc` | the rest: the game's code, the AX mixer, the clock |

Per frame also: pipelines compiled, compile time (any thread), draws that waited and their wait,
draws skipped (async pipelines), draw calls, copies written back, the audio queue (ms), underruns,
skipped AX frames, overruns, pacer re-anchors, the audio controller's correction, and the cursor:
the input layer's target for remote 0 (the mouse) against the KPAD position the game read
(`EGG::CoreControllerMgr`, controller 0, newest sample). Counters from Aurora come through
`wpgx_get_perf` (cumulative; the tracker takes differences), Aurora's from patch 1 below.

At exit (`--frames`, closing the window, Wii Menu) with `--perf`: the CSV (one row per frame)
and a summary file next to it (`.txt`), the summary also on stderr. Default path
`$XDG_STATE_HOME/wiiplay/perf-YYYYmmdd-HHMMSS.csv` (`~/.local/state` without the variable);
`--perf-out FILE.csv` elsewhere (refused inside the source tree or the disc). The summary: the
settings line (adapter, type, backend, present mode, pipelines, disk cache, writeback, mode,
audio driver), frames and fps, min / avg / p50 / p95 / p99 / max of every phase, frames over
16.7, 20 and 33.3 ms, busy time (wall - sleep), pipeline totals, audio totals, the cursor
(below) and the ten worst frames with frame number, scene, wall and busy time, the dominant
phase and the pipelines compiled in it. Note: paced at 59.94 Hz a frame is 16.68 ms and jitter
puts about half of them "over 16.7"; over 20 ms is the count of frames that missed.

The overlay (window only; F3 toggles it, also without `--perf`, which then writes nothing):
Dear ImGui through Aurora (its context exists with an SDL window; `wpgx_overlay` draws a
borderless window between `aurora_begin_frame`'s `NewFrame` and `aurora_end_frame`), the numbers
averaged over the last 120 frames and a bar graph of their wall times (scale 50 ms, a line at
16.7). The window always tracks (a few clock reads per frame, 100 bytes per frame of memory).

Cursor: lag = the shift k (0-15 frames) for which the KPAD position best matches the target k
frames earlier, over frames where the target moves; the error there and at k = 0; at rest (the
target unchanged for 30 frames) the mean and largest distance. Units: half the picture width
(KPAD's -1..1).

### Measurements (before / after)

Headless, SDL's dummy audio driver, no display variables, the test scripts (menus: `menus.txt`;
Shooting Range: `menus.txt` + `shooting.txt`; Laser Hockey, Fishing, Charge!: `menus_saved.txt`
+ the game's script, from a save with every game open), `--perf-out`. Two kinds of run:

- *Virtual time* (no pacer: as fast as the host goes): the work per frame, and the pipeline
  hitches.
- *Real time* (`--realtime --audio-out`, the window's pacing and audio path): what a player gets.
  Menus 4900 frames, Shooting Range 7000, the others 6000.

"Before" is the window's old settings (`--test-gfx`: no disk cache, blocking pipelines, the CPU
adapter allowed, copy writeback on); "after" the new window defaults (`--live-gfx`: disk cache
warm, writeback off). The driver's own shader cache (`$XDG_CACHE_HOME`) was warm in both.

**Real time** (wall p99 ms / frames over 20 ms / busy avg ms / worst frame ms):

| Scene | RTX 3070 before | RTX 3070 after | Intel UHD before | Intel UHD after |
|---|---|---|---|---|
| Menus (4900) | 18.8 / 17 / 8.5 / 107 | 16.9 / 3 / 5.4 / 50 | 21.6 / 257 / 9.0 / 98 | 16.9 / 4 / 5.5 / 50 |
| Shooting Range (7000) | 39.8 / 315 / 9.4 / 178 | 17.0 / 4 / 6.6 / 50 | 27.5 / 979 / 11.2 / 152 | 17.0 / 5 / 6.3 / 50 |
| Laser Hockey (6000) | 19.5 / 33 / 10.9 / 250 | 17.0 / 5 / 6.6 / 50 | 22.3 / 447 / 12.8 / 188 | 16.9 / 3 / 6.8 / 50 |
| Fishing (6000) | 32.2 / 363 / 12.1 / 179 | 17.0 / 5 / 7.1 / 50 | 23.2 / 483 / 13.7 / 129 | 17.0 / 6 / 7.7 / 50 |
| Charge! (6000) | 20.0 / 64 / 11.4 / 418 | 16.9 / 5 / 8.3 / 59 | 28.9 / 858 / 13.2 / 345 | 17.9 / 27 / 7.3 / 59 |

Before, `gpu` (the copy writeback waiting for the GPU) averaged 3.8-7.4 ms a frame on the RTX and
5.8-10.4 ms on the Intel GPU (p99 up to 28 ms), and every first use of a pipeline stalled the
frame (blocking) for 20-50 ms on these drivers. After, the frames that still take over 20 ms are
the few scene loads (the 50 ms worst frame is a load). Audio in all of these: 0 underruns and 0
overruns after; before, the frames the host fell behind on cost pacer re-anchors (up to 14 in
Shooting Range on the Intel GPU) and skipped AX frames. Paced, the laptop's CPU and GPU clock
down: `calc` and `gx` are 3-4 times their virtual-time values.

**Virtual time, pipelines** (RTX 3070; pipelines compiled / draws that waited / draws skipped /
worst frame ms; the menus and the four games in one data directory, in that order):

| Settings | Menus | Shooting Range | Laser Hockey | Fishing | Charge! |
|---|---|---|---|---|---|
| no disk cache, blocking (before) | 94 / 94 / 0 / 407 | 168 / 168 / 0 / 533 | 153 / 153 / 0 / 535 | 184 / 184 / 0 / 369 | 226 / 226 / 0 / 1772 |
| disk cache, first run | 94 / 94 / 0 / 152 | 168 / 74 / 0 / 169 | 197 / 29 / 0 / 185 | 253 / 56 / 0 / 142 | 347 / 94 / 0 / 395 |
| disk cache, warm | 347 / 0 / 0 / 43 | 347 / 0 / 0 / 84 | 347 / 0 / 0 / 42 | 347 / 0 / 0 / 51 | 347 / 0 / 0 / 42 |
| async, first run | 94 / 0 / 13751 / 44 | 168 / 0 / 10097 / 74 | 197 / 0 / 1678 / 44 | 253 / 0 / 3221 / 50 | 347 / 0 / 16763 / 42 |

A single pipeline took up to 640 ms to compile on the NVIDIA driver (Charge!: 1.8 s for one
frame). With the disk cache warm, Aurora compiles all 347 known pipelines in the background at
start and no draw waits. Async pipelines avoid the first-run stalls but skip draws, and the
game renders some textures once (the Mii faces, `RFLiMakeTexture`; the light textures): a
skipped draw there stays wrong. So the window keeps blocking pipelines with the disk cache: the
first time a scene is seen it can hitch, never again for the same build.

**The cursor** (real time, `--events`: the mouse circling at 25 px radius, then 4 px steps; RTX):

| | lag | error moving (at that lag) | error at rest avg / max |
|---|---|---|---|
| game stabilisation (before; `pointer_stabilise 1`) | 12 frames | 0.0061 | 0.0081 / 0.0142 |
| radius 0 for the mouse (after) | 1 frame | 0.0012 | 0.0015 / 0.0018 |

(Half the picture width = 320 px: 0.008 is 2.6 px, 0.0015 half a pixel, the DPD's resolution.)

### Fixes

**Live graphics settings (`--live-gfx`, the default with `--window`).** The window used the test
settings. Now: Aurora's pipeline and Dawn caches on disk in the data directory
(`$XDG_DATA_HOME/wiiplay/aurora`: `pipeline_cache.db`, `dawn_cache.db`), keyed by build:
`wpgx` derives the default `pipelineCacheKey` from our patch version and the GNU build ID of
`libwiiplay_gx.so` (read from its own `PT_NOTE` through `dl_iterate_phdr`), so every rebuild
starts a fresh cache and no stale pipeline is ever loaded; blocking pipelines (above;
`--async-pipelines` for the other trade); no CPU adapter unless `--gpu cpu`; mailbox
presentation; copy writeback off. `--test-gfx` gives the old settings with a window; headless
runs (the tests) keep them by default.

**Copy writeback off in the window.** §28's writeback reads back every texture copy, encodes it
and writes it to emulated memory at the frame's end, which makes the CPU wait for the GPU every
frame (13 copies a frame in Shooting Range and Fishing). No CPU reader of a copy changes any
frame: with `--writeback off` against on (`--test-gfx --hash`, RTX), every frame hash is
identical over the whole of the menus (4900 frames), Shooting Range (16200), Laser Hockey (14600)
and the other seven games' test scripts (Find Mii 8600, Tanks! 8900, Billiards 11100, Fishing
12900, Table Tennis 7400, Pose Mii 8750, Charge! 10300). The copies stay authoritative on the GPU
(§28: keyed by destination until the memory changes) either way. Tests keep writeback on
(`--writeback on|off` chooses). A writeback deferred by a frame (deliver frame N's copies when
N+1 ends, without waiting) was tried and dropped: Aurora's GPU events may only be polled with
its render thread idle, so the flush still synchronised most frames and real time got worse
(RTX, frames over 20 ms: 92 / 984 / 1498 / 1327 / 1432 in the five scenes).

**Adapter.** Aurora asks for the high-performance adapter (the discrete GPU on a hybrid laptop);
patch 2 adds a choice by name, index or type and the list. `--gpu NAME|N|discrete|integrated|cpu`,
`WIIPLAY_GPU`, `--gpu list`; the chosen adapter, its type, backend and the present mode are
printed at start in a window (`boot: gpu: ...`) and head the perf summary. With the NVIDIA driver
broken the old settings could fall back to the Intel GPU or to llvmpipe (`allow_cpu_adapter =
1`) silently: a window now refuses the CPU adapter (and says so) unless asked.

**Pacing and presentation.** The pacer sleeps to each event's game time (59.94 Hz retraces) and
was combined with FIFO vsync. On a 60 Hz display that is mostly harmless (the game is slower
than the display, FIFO rarely blocks; a frame repeats every 16.7 s), but on a display at or
below 59.94 Hz, or a present path that takes more than a refresh (PRIME offload on a hybrid
laptop, a compositor), FIFO blocks the game thread in the frame-slot wait (`present`) on top of
the pacer, the game falls behind real time, the audio runs dry and the cursor trails. Now the
pacer is the only clock and presentation does not block: mailbox (patch 3,
`aurora_set_vsync_mailbox`: with vsync, mailbox where the surface offers it, else FIFO relaxed /
FIFO), so frames are still shown whole at a refresh. `--fifo` restores FIFO; `--no-vsync`
gives mailbox or immediate (tearing). `present` in the tracker shows any remaining wait.

**Audio clock controller.** It moved the pacer's anchor by 1 % of the queue error per AX frame
without a bound: a device consuming slowly (or stalling) slowed the game in proportion, a
device that stopped stopped it. Now: gain 0.001 s per s of error per AX frame (10 ms of error ->
0.33 %, time constant about 3 s), clamped at ±0.5 % of real time; above a high mark (150 ms
queued) an AX frame (3 ms) is skipped instead of queued (counted); above 300 ms the queue is
dropped (overrun, a stalled device); an empty queue (underrun: the game fell behind) is
re-primed with 50 ms of silence so one late frame is not followed by a run of underruns. The
SDL dummy driver consumes about 3 % slow: the controller sits at -0.5 % and the queue settles
(no skips in 100 s runs, up to 145 ms queued). Pitch: the stream is declared as s16 stereo at
32000 Hz (96 samples per 3 ms AX frame of game time, the DSP's rate) and SDL resamples to the
device (the device format, buffer and the correction bound are printed: `audio: pipewire, device
48000 Hz 2 ch, buffer N frames ...`). The pitch is exact whenever game time runs at real time; a
game falling behind (the frame time over 16.7 ms) produces sound slower than the device plays
it, which underruns and sounded like wrong speed before. Every AX frame is mixed in the clock
event that makes it (§29), never by a cooperative thread, so the mixer cannot be starved; the
sound thread's commands (new sounds, parameters) run when the game thread yields, as before.

**The cursor.** The game sets KPAD's pointer play radius to 0.06 (sensitivity 1) every frame
(`RPSysCoreController::beginFrame`, `KPADSetPosParam`); under that distance KPAD moves the
pointer by (distance / 0.06)^4 of the way per sample, holding a real remote's jitter still. A
mouse has no jitter: small moves crept (12 frames behind) or stopped short (2.6 px). `boot` is
linked with `--wrap=KPADSetPosParam`; with live input the wrapper gives remote 0 (keyboard,
mouse, gamepad 0) radius 0, so KPAD's position is the pointer's (`pointer_stabilise 1` in the
config file keeps the game's). The decomp is unchanged. Other latency: the input is applied at
each retrace and the game reads it in the next frame (the measured lag of 1 frame); the window
position goes through `wpgx_window_to_picture` (the letterbox of the presented picture, then
-1..1 over the 640x456 picture, as the remote's DPD mapping of §23 expects; error at rest half a
pixel, so no offset); mailbox shows the newest frame at the next refresh (at most two frames in
flight: Aurora's frame slots).

**Per-frame work.** In a window the `FRAME` lines are no longer printed (60 lines a second to a
terminal; `--trace` or a script keeps them, their probes drive scripted players). Frame reads,
hashing and PNG work happen only with `--hash` / `--save`; Aurora logging stays at warnings; the
address resolver is a range check (no map). The copy writeback above was the per-frame
O(heavy) item.

### Aurora patches

Generic, uncommitted in the submodule on top of the `wiiplay` branch (18df730); the series is kept
outside the repository with the job's notes (PUBLIC-safe: no paths, no names).

| # | Subject | Files |
|---|---|---|
| 1 | `gfx: pipeline compile and wait statistics` | `include/aurora/gfx.h` (`AuroraStats.pipelineWaits`, `skippedDraws`, `pipelineCompileNs`, `pipelineWaitNs`), `lib/gfx/pipeline_cache.cpp` |
| 2 | `webgpu: choose the adapter by name, index or type` | `include/aurora/aurora.h` (`AuroraConfig.adapterName`), `include/aurora/gfx.h` (`aurora_get_adapter_list`), `lib/webgpu/gpu.cpp` |
| 3 | `gfx: mailbox presentation with vsync, present mode query` | `include/aurora/gfx.h` (`aurora_set_vsync_mailbox`, `aurora_get_present_mode`), `lib/gfx/present_target.cpp`, `.hpp`, `lib/webgpu/gpu.cpp` |

### Verified headless vs. by hand

Verified: the tracker's numbers and files (all runs above), adapter choice and listing, the disk
cache across runs and its build key, writeback off frame-identical, the audio controller with
the dummy device, the cursor wrapper through synthetic SDL mouse events. Not verifiable here (no
window may be opened): the overlay's look, mailbox on the user's display (`present` in the
summary), sound on a real device, the mouse by hand. What to run and send back: RUNNING.md.

## 36. Two players, and later levels

All nine minigames now also run in their two-player modes, headless, from a two-player party
menu: a second virtual remote (channel 1) is connected from the boot, Select Players gets "2",
and each player picks a Mii with their own remote (Guest A, then Guest B). In each game both
remotes are worked by game-side players (the §33 players, now one per remote, each reading its
own player's objects), to the results ("P1 n wins" / "P2 n wins", "Play a different game")
and back. The players also go further alone where it was cheap (Tanks! past mission 3; and in
`make playtest2`, Find Mii to stage 13, Pose Mii to level 5, Table Tennis to the end of its
100-rally game). Playing two players found two 64-bit bugs the one-player runs never reached,
and a sweep for the same pattern found two more that had been quietly wrong all along (one of
them is why the one-player Pose Mii run used to lose in level 2). All are fixed, identical on
the Wii.

### Files

| File | What |
|---|---|
| `port/host/tests/boot/menus_saved2.txt` | **New.** The menus with two players: remote 2 connected at frame 0, "2" on Select Players, Guest A (remote 1, the guest warning, Yes), Guest B (remote 2: "Select a Mii for Player 2."), OK. The timings are `menus_saved.txt`'s, so the games' scripts follow it unchanged |
| `port/host/tests/boot/play2.txt` | **New.** `chan 1`, `play 1` at frame 3850: remote 2's player plays player 2 |
| `port/host/tests/boot/shooting2.txt`, `hockey2.txt` | **New.** Shooting Range and Laser Hockey with `aim 1` (and `fire 6`) on both remotes, then "Play a different game" |
| `port/host/src/host_probe_games.cpp` | One player state per remote (`Begin(chan, frame)`; `play 1` on channel n plays player n); every game's player takes its player's objects by index (below); the probes append player 2's fields when two play; `goal N` (`PortInputPlayGoal`); Tanks! plays on until its lives run out (three missions with two players) |
| `port/host/src/host_probe.cpp` | Shooting Range: the aim per remote (each player its own target, not yet hit by that player; player 2 searches from the end of the target lists); `duc:` gains player 2's score and hits. Laser Hockey: the aim per paddle (paddle 1: the +x half of the mapping), a two-player strategy (below); `hky:` gains paddle 1's position and distance |
| `port/host/src/host_input.cpp`, `include/port/input.h` | Script command `goal N` (§23) and `PortInputPlayGoal(chan)` |
| `port/host/tests/play_games.py` | `GAMES2` (the nine two-player runs and their checks), `LONG` (the later-level runs); the one-player Tanks! and Pose Mii runs' frames and checks (below) |
| `port/host/tests/test_play.py` | `--set 2`: the two-player and later-level runs |
| `port/host/Makefile` | `make playtest2` (`PLAYTEST2_ARGS`, scratch `$TMPDIR/wiiplay-play2`) |
| `port/host/tools/check_matching.py` | The decomp files below in `TOUCHED` |
| `port/docs/RUNNING.md` | Tests; two players in the known limits |

The input layer needed no change for a second remote: the script's `chan N` already sent the
following commands to channel N, `aim` / `fire` / `play` were per channel, and the boot feeds all
four states to the WPAD backend every retrace. Only the game-side players were player-0 only.

### Decomp changes (byte-identical on the Wii; `check_matching`: 974 units identical)

| File | Change | Found by |
|---|---|---|
| `RPMedalResult_5.h` | `PORT_HOST`: `ResultMenuSceneWins` / `Streak` / `Winner` / `ClearRetryFlag` use `RPSceneInfo`'s members (`unk8[player]`, `unk10`, `unk14`, `unk2C`) instead of word 2 + player, 4, 5 and byte 0x2C | the first two-player result: the wins counter (word 2 = `unk8[0]` with a 4-byte vtable pointer) is `mPlayerNum` on the host; the winner's win made the player count 3, and the party menu (`RPSelectScene::EnterStart`) read a third player's data: crash. Every two-player game hit it on its results. The retry flag cleared a byte of `unk28` |
| `RPBomPanel_3.h` | `RPBomPanelPool::GetPos` / `GetRot` / `GetUnk40` add `MODEL_OBJ_SHIFT` (`sizeof(void*) - 4` under `PORT_HOST`, else 0) to `RPModelObj`'s 32-bit offsets 0x4 / 0x1C / 0x40 | sweep (`reinterpret_cast` + byte offset). On the host the bubbles' z read `mPos.y` (and the high half of the vtable pointer): the pool's in-range count (wave pacing, the wait for bubbles, the BGM), spawn occupancy, the front bubble a miss takes and the Mii's in-bubble pose were all computed from wrong positions. The one-player run used to take its three misses early in level 2 while the player was still matching; now it clears level 2 and loses in level 3, where it stops matching (as intended) |
| `RPGrpModelMaterial.cpp` | `SyncChanColor`'s walker over `mpChanColors` steps `sizeof(ChanColor*)` instead of 4 | sweep: the second channel read half of each pointer (no material in the runs had a first channel colour, or it would have crashed) |
| `RPMedalResult.h`, `RPMedalResult_2.cpp` | `PORT_HOST`: `RP_getPairFromField1C` / `20` as local functions reading `RPRankGetLyt`'s player and rank position panes' global translation (`GetPlayerPosPane()->GetGlobalMtx()._03 / _13`; host-only accessors) | sweep (an `extern "C"` declaration with another parameter type): on the Wii these are `RPSceneInfo_2.cpp`'s functions written for `RPHkyGame` (word 0x1C / 0x20, then 0x90 / 0xA0 of that object, which for the result is a pane's `mGlbMtx`); on the host they read `RPRankGetLyt`'s word at `RPHkyGame::mpPuck`'s host offset (an animation object) and floats from it: the one-player result's Mii and medal positions came from garbage |

### Each game with two players (the game's own rules, as read in the code)

| Game | Two-player mode | How the players play (player 2 = remote 2) |
|---|---|---|
| Shooting Range | versus (`RPDucGame::mMode` 1); either player can hit any target; per-player scores; versus point values and two-face patterns | each remote aims at a live target its player has not hit (player 1 the first found, player 2 the last), B every 6 frames |
| Laser Hockey | paddle 1 human (`IsCpu()` only in one-player); no clock, first to 8 goals | each goal is given away on purpose by one side (player 2 two goals in three): that paddle knocks the puck into its own goal when the puck is on its half, otherwise waits by the side wall; the other gets behind the puck (going round it) and strikes it at the other goal. Two paddles simply chasing the puck pinned it against a wall, and two good defenders scored once in 15000 frames |
| Find Mii | both pick at once (`mHoverIdx[p]`, `mPenaltyTimers[p]`); finds scored per player by how many were already found; two minutes, no time bonus | as alone, player 2 searching the list from the end; after 5 stages player 2 picks wrong Miis (its penalty, -5 each) and player 1 waits |
| Pose Mii | one screen, shared bubbles coloured per player (`mColorType` 0 / 1, 2 special), shared misses | each its own colour (player 1 also the specials) |
| Table Tennis | points game to 11 (`RPPnpPointPhase`), serve changes every 2 points, the camera turns at court changes; side 1 plays -z | player 2's mapping uses `sHumanMinX`; the racket waits at z -160 and meets the ball on z -165; both hold the paddle, then the server presses A on its own remote and keeps still (the ball falls through the racket, `CheckServeHit`); the receiver lets a rally go after 3 to 6 strokes |
| Billiards | turns (`RPBilPlayerMgr::mCurrentPlayer`), only the current remote is read; scores (pocketed numbers) and fouls per player; ends when the table is clear | the player whose turn it is plays as alone; the other points away |
| Fishing | one pond and camera, a rod each (grips at x -100 / +100), shared fish | as alone with its own rod and hook |
| Charge! | split screen, two cows on the two-player course; groups belong to the first player to hit them; time up ends it | as alone, skipping groups the other player has claimed |
| Tanks! | co-op, one shared life count (one life), kills per player | as alone; neither fires when the other's tank is within 30 of the line of fire before the enemy; three missions, then they sit still |

### The tests

`make playtest2` (new; `test_play.py --set 2`): each two-player game twice (determinism) and
the three later-level runs once (their games' code is the same as in `make playtest`'s pairs),
four at a time. Per run: the scene trace, the frame count, five screens (each in its scene, all
distinct; PNGs with `--png`), the log (documented Aurora warnings only, no `boot:` line), and
per game the game's own state:

| Run | Checked |
|---|---|
| Shooting Range (2P) | rounds 1-5 played, the end state; both players scoring in every round; scores only change in play |
| Laser Hockey (2P) | intro, play, goals, the end; first to 8 with both scoring; a score changes only by one as a goal starts; at least 3 paddle-puck contacts for each paddle; the clock never runs |
| Find Mii (2P) | the clock from 7200 (two minutes); 5 stages cleared; finds credited to both players; wrong picks by player 2 |
| Pose Mii (2P) | the states through the results; level 3 reached; at least 30 bubbles matched by each player; all misses used; the borders 16 apart in play |
| Table Tennis (2P) | states 0-5; the game to 11 with both scoring, points one at a time; at least 10 strokes by each player |
| Billiards (2P) | the turn changes; both players shot and scored; all 9 object balls off; balls leave the table only during a shot |
| Fishing (2P) | each player caught at least 2 fish and has points; player 2 through aim, wait, fight, land; the clock ran out; the versus result |
| Charge! (2P) | the states through the two-player end (time up 19, scores 20, winner 23, results 24); both scored and ran over 3000 |
| Tanks! (2P) | at least 2 missions cleared; kills by both, one at a time in play; the shared lives equal |
| Find Mii (12 stages) | the §33 checks, stage 13 reached |
| Pose Mii (4 levels) | the §33 checks, level 5 reached |
| Table Tennis (100 rallies) | the §33 checks, the game ends at 100 rallies |

`make playtest` changes: Tanks! (9900 frames) must clear 3 missions (it clears 3 and loses in
mission 4); Pose Mii (11400 frames) must clear 2 levels (it loses in level 3).

Expected output of `make playtest2` (four runs at a time on 12 cores, lavapipe: 7258 s, about 2 hours, for the 21 runs):

```
test_play (two players, later levels): 21 runs on lavapipe in 7258 s wall
  shooting range (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 18 -> 27 over 14800 frames; states 0 
-> 1 -> 2 -> 3 -> 4 -> 2 -> 3 -> 4 -> 2 -> 3 -> 4 -> 2 -> 3 -> 4 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7; 
scores 375 (149 hits) / 302 (106 hits), both scoring in every round; 5 distinct screens; log as 
§33
    hashes and scenes identical over two runs: True
  laser hockey (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 17 -> 27 over 9300 frames; states 0 -> 
1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 
2 -> 1 -> 2 -> 1 -> 3 -> 4 -> 5; goals 8-3; paddle-puck contacts 86 / 115; no clock; 5 distinct 
screens; log as §33
    hashes and scenes identical over two runs: True
  find mii (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 22 -> 27 over 14400 frames; states 0 -> 1 
-> 2 -> 3 -> 0 ... 4; 5 stages cleared; finds credited to player 1 / 2: 6 / 4 (best scores 45 / 
30); 79 wrong picks by player 2; time up; 5 distinct screens; log as §33
    hashes and scenes identical over two runs: True
  pose mii (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 24 -> 27 over 11300 frames; states 0 -> 1 
-> 2 -> 3 -> 5 -> 3 -> 5 -> 3 -> 5 -> 3 ...; levels 3; matched 258 / 58, scores 288 / 58; 3/3 
misses; 5 distinct screens; log as §33
    hashes and scenes identical over two runs: True
  table tennis (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 19 -> 27 over 12700 frames; states 0 -> 
1 -> 2 -> 3 -> 4 -> 2 ...; points 4-11; strokes by player 1 / 2: 30 / 33; 5 distinct screens; 
log as §33
    hashes and scenes identical over two runs: True
  billiards (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 20 -> 27 over 12100 frames; states 0 -> 1 
-> 2 -> 3 -> 2 -> 3 -> 2 -> 4 ...; 7 turn changes; shots 8 / 5, fouls 2 / 1, scores 24 / 14; 
all 9 object balls off: True; 5 distinct screens; log as §33
    hashes and scenes identical over two runs: True
  fishing (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 16 -> 27 over 13500 frames; caught 8 / 4 
fish, 770 / 360 points; the clock ran out; result state 8; 5 distinct screens; log as §33
    hashes and scenes identical over two runs: True
  charge (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 21 -> 27 over 9300 frames; states 0 -> 1 -> 2 
-> 4 -> 5 -> 6 -> 7 -> 8 -> 19 -> 20 -> 21 -> 23 -> 24; scores 107 / 97; ran 14285 / 14356 
along the course; winner player 1; 5 distinct screens; log as §33
    hashes and scenes identical over two runs: True
  tanks (2P): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 23 -> 27 over 8600 frames; states 0 -> 1 -> 2 
-> 4 -> 6 -> 7 -> 2 -> 4 -> 6 -> 7 -> 2 -> 4 ...; missions cleared 3; kills 4 / 1; 5 distinct 
screens; log as §33
    hashes and scenes identical over two runs: True
  find mii (12 stages): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 22 -> 27 over 14900 frames; states 0 
-> 1 -> 2 -> 3 -> 0 -> 1 -> 2 -> 3 ... 4; 12 stages cleared (stage 13 reached); 12 wrong picks, 
each taking time off; time up; 5 distinct screens; log as §33
  pose mii (4 levels): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 24 -> 27 over 15700 frames; states 0 
-> 1 -> 2 -> 3 -> 6 -> 7 -> 8 -> 3 -> 6 -> 7 -> 8 -> 3 ...; levels 5, 493 bubbles matched, 
score 543, 3/3 misses; borders 16 apart in all 7959 frames of play; 5 distinct screens; log as 
§33
  table tennis (100 rallies): scenes -1 -> 0 -> 25 -> 1 -> 27 -> 19 -> 27 over 11200 frames; 
states 0 -> 1 -> 2 -> 3 -> 4 -> 5; 100 returns by player 0, 100 rallies (final hit count 200); 
5 distinct screens; log as §33
errors: 0
OK
```

`make playtest` with the changes: 6164 s (about 103 minutes) for its 18 runs, all passing
(Tanks! "missions cleared 3, 5 enemy tanks destroyed"; Pose Mii "levels 3, 260 bubbles
matched, score 275, 3/3 misses"). `make -j8 test`: 7 min 58 s (unchanged: no new run in it).
`make M32=1 test` passes. With `--gpu` (the machine's discrete GPU instead of lavapipe) the two
targets took 11 and 15 minutes (six runs at a time, all passing); the hashes were not
compared across drivers (each pair runs on one).

### Not a stage: Shooting Range

Shooting Range has no stages: "Stage two is now open!" on the party menu after the first game
is the party menu opening its second game (Find Mii), and the all-games save opens them all.
What the one-player Shooting Range does have and no run drives yet is the co-op join:
`RPDucScene::Configure` starts a one-player game in mode 2 (co-op), where A or B on remote 2
adds player 2 (`mP2Joined`), scoring into player 1's total.

### Verified headless vs. visual only

Verified (the games' own state, the scene trace, the log, determinism over two runs): the
two-player menus; every game's two-player mode played to its results by both remotes, with
both players' scores or progress moving; the two-player results menu (the win counts) and back
to the party menu; Tanks! to mission 4, Find Mii to stage 13, Pose Mii to level 5, Table Tennis
to its 100-rally end. Visual only (the PNGs, `<key>_<screen>.png`): Charge!'s split screen,
Table Tennis's turned camera at court changes, Billiards' "P1 Wins" with the totals, Fishing's
two point counts, the HUDs with P1 / P2. Not tried: two people by hand in a window; the
Shooting Range co-op join; three or four remotes (Wii Play has two players at most, but the
HOME Menu shows four cursors).

### Next steps

- The Shooting Range co-op join (one-player game, remote 2 presses A).
- A sweep of the remaining raw-offset reads (`RPTnkField.cpp`'s `+ 0x20` wall walker is
  `sizeof(RPTnkWall)` of floats, fine on the host; no others found in `src/Pack` and
  `include/Pack` beyond those fixed or already under `PORT_HOST`).
- `make playtest2` is two hours on lavapipe; the later-level runs could share the two-player
  runs' slots if a machine with more cores runs six at a time.

## 37. Phase 8 input: live-play latency, motions for keyboard / mouse and gamepad, the remote speaker

Three things a player in the window needed, and two problems found in the first play-tests.
No decomp file changed (`check_matching.py`: all identical, nothing added to TOUCHED).

### Files

| File | What |
|---|---|
| `src/host_live.cpp`, `include/port/live.h` | Bindings to motion actions as well as buttons; the gesture layer (twist, tilt, shake, pull / thrust, push); the per-game hold; merge mode over a test script; presses shorter than a frame kept for one frame; the window geometry logged; the speaker in the output; new config keys |
| `src/host_probe_games.cpp` | `PortInputHoldHook` (how each game holds the remote), `PortMotionProbe` (the FRAME line's `motion=`), the players' `play 2` |
| `src/host_input.cpp`, `include/port/input.h` | `play N` kept per channel (`PortInputPlayMode`); `PORT_HOLD_*`; the speaker control callback type |
| `src/host_wpad.cpp` | `PortWPADSetSpeakerControlCallback`: the speaker commands as the remote carries them out |
| `src/host_speaker.cpp`, `include/port/speaker.h` | **New.** The WENC decoder and the speaker mixer |
| `src/host_ax.cpp` | The remote speaker outputs (`AXRmt*`) |
| `src/host_record.cpp`, `include/port/record.h` | **New.** `--record DIR[:N]`, `--latency-marker` |
| `src/host_perf.cpp`, `include/port/perf.h` | `gpu_latency_ms`, `in_flight` columns; the window notes, the pointer range and the frames-in-flight line in the summary |
| `src/host_main.cpp` | `--frames-ahead`, `--record`, `--latency-marker`, `--test-gpu-load`; motions applied on the remote's copy; `motion=`; END `inflight_*`, `gpu_latency_*`; LIVE `speaker_*` |
| `aurora/wpgx.cpp`, `wpgx.h`, `wpgx_platform.cpp` | The mouse mapping from the window's current geometry and Aurora's last viewport; `wpgx_window_info`, `wpgx_fit_window`; the frames-in-flight limit and statistics; the latency marker; wheel events; test helpers (window geometry, wheel) |
| `port/extern/aurora` | Two generic patches (below) |
| `tests/test_gestures.py`, `tests/boot/gestures_hockey.txt` | **New.** The motion tests (run by `make boot` and `make test`; `make gestures` alone) |
| `tests/test_boot.py`, `tests/boot/live.txt` | Run D: the mouse mapping across window geometries, a click shorter than a frame; run L: the frames-in-flight limit with a slowed GPU |
| `tests/hbm_harness.cpp`, `tests/test_hbm.py` | The HOME Menu's speaker sounds decoded (`HD` lines) and compared |

### Play-test findings: what "the game plays in the background" was

The user's reports: the picture lagging seconds behind the game and the inputs (about 10 s with
the default mailbox presentation, 4-5 s and 23 fps with `--fifo`), sound feeling late at
times, a hard-to-reach pointer and lost clicks. What the session data showed:

- **The display was a remote desktop.** The session (`loginctl`: `Remote=yes`, `Type=wayland`)
  is GNOME Remote Desktop over RDP (port 3389) from another machine; Mutter's only monitor is
  `Meta-0`, "Virtual remote monitor", 1920x1080. Everything the user sees and hears is encoded
  and streamed: a 1280x912 window changing 60 times a second is far more than the stream
  carries, and its queue grows to seconds. The game itself kept real time: the perf files show
  59.9 fps, `present_ms` 0.04 ms (mailbox never waited), no audio underruns, the cursor one
  frame behind the mouse. With FIFO the compositor paces the window by the stream's frame
  callbacks: presents of 0.5-3.7 s and 198 pacer re-anchors. The audio goes through the same
  RDP connection. Nothing in the port can make an RDP stream carry full-motion video in real
  time; play on the machine's own display (or use a streaming tool made for games).
- **Frames in flight were unbounded on the GPU side.** Aurora's two frame slots bound the CPU
  only up to the render worker's submission; its five staging buffers let the GPU fall up to
  about five frames behind (measured headless on lavapipe with a slowed GPU: 5 frames in
  flight, 95 ms average from a frame's end to its GPU work done). Not the cause of seconds of
  lag, but the picture could trail the game. Now limited (below).
- **Clicks shorter than a frame were lost.** Keys and buttons were sampled once per frame: a
  press and release between two polls (a quick click; a remote desktop delivers them together)
  never reached the game. Now a press is held for at least one frame (3-4 WPAD reports).
- **The pointer range.** The perf CSV's pointer never went left of -0.42 or below 0.59: the
  mouse could not reach part of the window, consistent with the RDP client showing only part
  of the session. The mapping itself was right for the window the game saw; it now follows the
  window's current geometry and Aurora's last viewport, logs them, and a window larger than the
  display's usable area is shrunk at start.

### Frames ahead: the picture never falls behind the game

`aurora_set_max_frames_in_flight(N)` (Aurora patch 0002): `aurora_begin_frame` waits until
fewer than N ended frames have GPU work outstanding, their presentation's included (Dawn's
`Queue::OnSubmittedWorkDone` after the frame's submissions). `--frames-ahead K` sets N = K + 1;
the default in real time is 0 (as on the Wii, each frame starts when the last one's GPU work and
presentation are done: the CPU frame is about 2 ms, overlap buys nothing), headless 1;
`--frames-ahead 1` or `2` for a slow machine. The pacer still sleeps to the next retrace and
re-anchors when late, so the game never runs ahead of the display.

Measured (`--perf` columns `gpu_latency_ms`, `in_flight`; END `inflight_max`,
`gpu_latency_avg_ms`):

| Setup | Frames in flight (max) | Frame end -> GPU work and presentation done |
|---|---|---|
| lavapipe, headless real time, no limit | 3 | 17.1 ms avg |
| the same, `--frames-ahead 0` | 1 | 6.3 ms avg |
| lavapipe, GPU slowed (`--test-gpu-load 64:4`), no limit | 5 | 94.5 ms avg, 120 max |
| the same, `--frames-ahead 0` / `2` | 1 / 2 | 20.2 / 37.8 ms avg |
| RTX 3070, a real Wayland window (private headless gnome-shell), mailbox, `--frames-ahead 0` | 1 | 4.5 ms avg; input applied -> its frame done 5.7 ms avg, 9.2 max |
| Intel UHD, the same | 1 | 3.4 ms avg; input -> frame done 3.6 ms avg, 5.4 max |

Input-to-present here is from the poll that applies an input to the end of that frame's GPU
work (`--record`: `input.csv` against `frames.csv`); the compositor's scan-out adds about a
refresh. In the private compositor `--fifo` stalled for up to 60 s and hung once the
compositor stopped sending frame callbacks, on both GPUs: FIFO on Wayland depends on the
compositor repainting the window (hidden, on another workspace, or behind a remote stream it
waits). Mailbox with the limit has the lowest latency and stays the default.

The surface's present modes are logged at configure (Aurora's log: `surface configured: ...,
present mode ..., (surface supports: ...)`); Dawn does not expose the swapchain's image count.

### Recording a run: `--record`, `--latency-marker`

`--record DIR[:N]`: `frames.csv` (frame, wall ns, game time, retrace, GPU latency, frames in
flight, marker shown), `input.csv` (every window-system event as applied: wall ns, frame,
event), and every Nth frame as a half-size PPM. `--latency-marker`: a white square in the
picture's top-left corner on each frame where a key, mouse button or gamepad button is pressed:
a phone filming the screen and the keyboard measures input-to-photon latency. The window's
geometry (logical and pixel size, density, display, usable area, video driver, the presented
viewport and the mouse mapping) is printed at start and on every change, and kept in the
`--perf` summary with the pointer's range.

### Motions: twist, tilt, shake, pull and thrust, push

How each game reads the remote's motion (the decomp):

| Game | Reads | Detector |
|---|---|---|
| Laser Hockey | `horizon` (`RPHkyPaddle::UpdateControl`) | paddle angle = 3 x atan2(horizon), at most 8 degrees a frame (`LimitTilt`) |
| Pose Mii | `horizon` (`RPBomCursor`) | the Mii turns by 4 x the remote's roll |
| Charge! | acc, averaged over the frame's samples (`RPCowPlayer::CalcInput`), the remote on its side | run: the angle of acc.x (the roll) below -0.2 (about 18 degrees); steer: acc.z (the pitch); jump: \|acc.xy\|^2 >= 6.25 (2.5 g) with \|acc.z\| <= 3; a z flick (+-1 g then the other way within 10 frames) swerves |
| Fishing | acc.y (`RPFshPlayer::IsRemoteSteady` in the fight state) | acc.y < -1.4 g, none of the 20 samples before above -0.5 g: the lift hooks the fish |
| Billiards | `dist` and acc while B is held (`RPBilPlayer::AddressCalc`, `StrokeCalc`) | the remote 0.1 m farther than at B: the stroke starts; back nearer than at B: the shot, its power from the distance's speed and the acc along -z |
| Find Mii | `dist` (`RPWlyCursor::Calculate`, mode 0) | the distance ratio over a 0.3 m window moves the camera (closer: in) |
| Shooting Range, Table Tennis, Tanks! | the pointer, buttons, the D-pad / Nunchuk stick | none (Table Tennis swings from the pointer's motion; Shooting Range's acc history is debug drawing only) |

The gesture layer (`host_live.cpp`) keeps per remote: a **twist** (degrees, persistent: the
twist keys turn it at `twist_speed`, the wheel steps it by `wheel_twist`, `TWIST_RESET` levels
it; within +-90), the **tilt** keys as springs to +-`tilt_angle` (at `tilt_speed`) plus the
left stick's deflection, a **shake** pulse of `shake_strength` g along the remote's up vector
(what lifting it sharply reads, whatever its roll and pitch) for `shake_frames` frames per
press, a **pull** (held: the remote draws back to `pull_distance` at 0.6 m/s with a little +z
acceleration; released: a thrust toward the screen at 1.5-4 m/s by how far it was drawn, to
0.1 m nearer than the start, with -z acceleration of 1-3.5 g, then back) and a **push** (held:
`push_distance` nearer at 0.6 m/s). `PortLiveApplyMotion` adds them to a copy of the remote's
state just before the WPAD backend gets it, so they never accumulate in a script's state.

The meaning of tilt follows how the game holds the remote (`PortInputHoldHook`, from the scene):
pointing at the screen, forward / back is the pitch (nose down / up) and left / right the roll;
held sideways (Charge!), forward / back is the roll (the game's run / slow) and left / right the
pitch (left turns left). In the games that read tilt or twist (Laser Hockey, Pose Mii, Charge!)
a gamepad's left stick tilts the remote (`left_stick auto`); elsewhere it stays a D-pad (Tanks!,
Billiards' camera). Default bindings: RUNNING.md.

Gamepad buttons the game never reads changed meaning: the shoulders were Nunchuk C and a second
B; now the right shoulder shakes, the left shoulder pulls (hold) and thrusts (release), the left
trigger pushes and the left stick's click levels the twist. B stays on east and the right
trigger.

### The test (`tests/test_gestures.py`)

The live layer on top of a test script (`--input` with `--live-input`: merge mode, the live
layer adds its buttons and motions); the game-side players in `play 2` navigate and aim as in
the playtests but press the default motion keys through SDL's queue (`PortLiveTestKey`) instead
of setting the motion themselves; Laser Hockey uses an events file. The FRAME line's `motion=`
shows what each game's own detector made of it. Six runs, three at a time, about 10 minutes on
lavapipe:

```
  hockey     paddle twist +30 / -0 / -30 / -1 / +30 degrees (E, wheel, Q, middle button, left stick)
  posemii    twist keys: KPAD roll over 49 degrees; 129 bubbles matched
  charge     W: KPAD roll 60 degrees, top speed 1.19; A / D: yaw over 3.50 rad; F: 2 jumps
  fishing    F on a bite: 4 fish hooked by the lift, 4 caught
  billiards  R held and released (B held): 5 shots
  findmii    G held: the cursor's distance ratio 0.00..1.00
```

### The Wii Remote speaker

The games and the HOME Menu encode the speaker's sound with WENC (`wenc.c`, a 4-bit adaptive
ADPCM: 40 samples of 6 kHz in 20 bytes a packet, every 6.67 ms) and send it with
`WPADSendStreamData`. `PortWencDecode` (`host_speaker.cpp`) is its decoder: from the nibble it
redoes the encoder's own reconstruction step (`qn` from `dl`, `dl/2`, `dl/4`, `dl/8`, the
clamps, `dl *= table[index]` truncated), so it gives back exactly the `xn` the encoder tracked.
The decoder starts from xn 0, dl 127 when the speaker is turned on (`WPAD_SPEAKER_ON`, through
the new speaker control callback), as both encoders do (`RemoteSpk::SpeakerOnCallback`,
`RemoteSpeaker::SpeakerOnCallback`). The rate: WPAD's speaker set-up byte pair `0xD0 0x07` is
the divider 2000 of 12 MHz (6000 Hz; the decomp's comment says 3000).

The games' remote sound comes from the AX mixer's remote outputs, which the host AX had left
empty: now each voice with `remote` on mixes into four remote buses with its `rmtMix` main
volumes (ramped like the main mix, after the voice's volume envelope, before the main filters),
and each 3 ms frame goes down to 18 samples at 6 kHz (box average) into a ring that
`AXRmtGetSamples` / `AXRmtAdvancePtr` read. Not modelled: the remote aux sends and the remote
IIR. The main mix is unchanged (every audio test's output is identical).

The live mixer queues each remote's decoded samples (20 ms of prefill), resamples them to
32 kHz (linear) and adds them to both channels of the main output, times `speaker_volume`
(config; default 0.5, 0 mutes) times the remote speaker volume the game set (HOME Menu
settings, SC; 85 of 127 by default). A Shooting Range smoke run sends 827 packets (the
gunshots); the HOME Menu's connect and volume sounds play through the same path. Not mixed
into `--audio` files (the tests' main mix).

The test: `hbm_harness` decodes each of the HOME Menu's five speaker sounds (`HD`: samples and
hash of the decoded PCM); `test_hbm.py` models the encoder, takes its reconstruction and requires
the harness's decoded sound to equal it sample for sample, then reports the codec's error
against the PCM the encoder was given (SNR 15.0-18.7 dB, peak errors 1046-2425 of peaks
5771-10375; the test requires 10 dB).

### Aurora patches (submodule working tree, not committed; series in the job's scratch)

- 0001 `gfx: query the last presentation viewport`: `aurora_get_present_viewport` (the
  viewport, the surface in pixels, the window in points of the last presentation).
- 0002 `gfx: frames-in-flight limit, GPU latency statistics, present mode log`:
  `aurora_set_max_frames_in_flight`, `aurora_get_frame_stats`, `aurora_test_set_gpu_load`; the
  configured present mode and the surface's modes in the log.

### Verified headless vs. to try by hand

Verified: the mouse mapping across window geometries (logical / pixel sizes, density 1.25 and
2, letterbox and pillarbox, a change under a still mouse), a click within one frame, the
frames-in-flight limit with a slowed GPU on lavapipe, real Wayland windows on both GPUs in a
private headless compositor (latency, present modes), every motion through SDL events to the
game's own detector in the six games that read motion, the speaker decoder bit-exact against
the encoder, the game's speaker packets reaching the mixer. By hand: the feel of the default
twist / tilt speeds, Charge!'s steering direction on a real pad's stick, the gyro-pad shakes
(not mapped: the pad's accelerometer still gives only the tilt), how loud the speaker sound
should be against the main mix, the latency marker on a real display, and any of it outside a
remote desktop session.

## 38. Tanks! against the original (Dolphin)

A play-test found Tanks! easier on the port than in Dolphin. The question: does the port's
Tanks! behave differently from the original (enemy parameters, AI, random numbers, hits read
or used wrongly on a little-endian 64-bit host), or is the difference in the input? Answer: the
game logic is the original's, frame for frame; the difference is the pointer. Nothing changed
in the code.

### Method

Dolphin 2609a (a headless `dolphin-emu-nogui` build, `-p headless -v Null`, the disc image,
its own user directory) ran the original game next to the port's `boot` (headless, lavapipe).
Both were driven to the same states and read at the same point of every frame:

- **Input.** Dolphin's emulated remote reads a named pipe (Dolphin's pipe input device); the
  menus are `tests/boot/menus_saved.txt` and the start of `tests/boot/tanks.txt`, timed from each
  scene's first frame, with the all-games save of `test_boot.party_save` in Dolphin's NAND. The
  port runs the same script.
- **Reading.** Dolphin through its GDB stub (`General.GDBPort`, `Interface.DebugModeEnabled`):
  breakpoints at `RPTnkScene::Calculate`, `RPTnkGameManager::Start` and
  `RPSysSceneMgr::calcCurrentScene`, memory read at the addresses of `config/RHAE01/symbols.txt`
  and the offsets in the headers' `// at 0x..` comments. The port through gdb at the same two
  functions, by member name.
- **The same random numbers.** `RP_randSeedX` / `RP_randSeedY` are written (0x12345678,
  0xBEEF) in both: at `RPTnkGameManager::Start` for mission 1 (before its `PickStageVariant` and
  the enemies' random timers), and for a later mission in every frame of the fade before its
  `PickStageVariant`.
- **A later mission.** In mission 1's play state both write `mStage = N - 1` and
  `RPTnkTankManager::mEnemyAliveNum = 0`: the game clears the mission and loads mission N+1
  through its own code.
- **Per frame** (from the mission's start state to its end, a game over or the frame limit):
  the game state, mission, map variant, both seeds, enemies alive, the player's position and
  alive flag; for each enemy slot its age, alive flag, type, position, fire interval and aim
  direction. At the first frame the parameters each tank was given: the player controller's
  (speeds, shells, bounces, shell speed, cooldowns, mines) and each enemy's attack and move
  parameters (`RPTnkAiAttack`: shells, bounces, shell speed and its `Params`;
  `RPTnkAiMove::Params`).
- **Two player policies.** Idle (the enemies hunt a still player); and active: every frame the
  cursor's world position is set to the first live enemy and every 60 frames B is set in the
  remote's trigger bits (the shells go out, ricochet, are dodged or shot down, and kill).

### Results

| Mission | Screen | Player | Frames compared | Seeds | Enemy state | Enemy positions (max difference) | Lives lost, kills |
|---|---|---|---|---|---|---|---|
| 1 | 4:3 | idle | 1500 | identical | identical | 0 | same frames |
| 3 | 4:3 | idle / active | 2500 / 2115 | identical | identical | 0.005 / 0.001 | same frames |
| 6 | 4:3 | idle | 1324 | identical | identical | 0.001 | same (game over) |
| 6 | 16:9 | idle | 1412 | identical | identical | 0.001 | same (game over) |
| 10 | 4:3 | idle / active | 1565 / 1950 | identical | identical | 0.001 / 0.001 | same; active: both enemies killed, mission cleared on the same frame |
| 13 | 16:9 | active | 972 | identical | identical | 0.001 | same frames |
| 15 | 4:3 | idle / active | 1878 / 1879 | identical | identical | 0.004 / 0.004 | same frames |
| 20 | 4:3 | idle / active | 1885 / 1970 | identical | identical | 0.001 / 0.001 | same frames |

"Enemy state": age, alive, type, fire interval, the map variant, the game state; the aim
directions agree within 0.0001 (the last digit printed). The random number sequence is
identical in every frame of every run, so every random draw (fire and mine intervals, aim
deviations, wander directions) happened at the same frame with the same value, and every AI
decision that consumes one was taken the same way. The positions differ by at most 0.005
units (a tank's hit radius is 15): print rounding and single-precision rounding differences
(the Wii fuses multiply-adds; the host does not), which never changed a decision. The
parameters given to the player and to each enemy type met (types 1-6 and 8 across these
missions) are identical, and `TnkGameParam.bin`'s 42 fields per tank agree with the `be_s32` /
`be_f32` views in `RPTnkTank.cpp`, `RPTnkAi_2.cpp`, `RPTnkAi_4.cpp` and
`RPTnkPlayerController.cpp` (integers where the data are integers, floats where floats).

### Why it plays easier

- **The pointer.** The game sets KPAD's pointer play radius to 0.06 every frame (§35). In
  Dolphin, as on a Wii, KPAD's stabilisation runs on the emulated remote's pointer: the cursor,
  and with it the turret, trails a moving pointer by up to 12 frames and creeps over the last
  few pixels. The port gives the mouse radius 0 (§35): the turret follows the mouse within a
  frame, to half a pixel. In Tanks! that is a large advantage: leading a moving tank, and
  shooting incoming shells and tanks down at once. `pointer_stabilise 1` in the input config
  keeps the game's stabilisation for a play like the original's (the delay of the remote
  itself, its jitter and Dolphin's own latency are not modelled).
- **Screen shape.** Dolphin's default console setting is 16:9; the port's is 4:3 (`--sc`,
  `aspect = 16:9`). Tanks! has a map per screen shape (22 columns wide on 16:9, 16 on 4:3; the
  same enemies, a different arena), so the same mission is a different layout. Both shapes are
  covered above and play identically on the port and in Dolphin.
- **Speed.** The game ran at full speed on the port in the play-test's recording
  (`--perf`: 45544 Tanks! frames, 16.70 ms per frame on average); the game was not slowed down.

## 39. Frame drops: the display's pace, the GX translation, first-use pipelines

Play-tests in a window (RTX 3070 Laptop GPU, a 60 Hz monitor on the NVIDIA GPU next to the
laptop's 144 Hz panel on the Intel GPU, GNOME on Wayland, the default "balanced" power profile)
dropped frames: Charge! ran at 54.2 fps (`--frames-ahead 0`; frames over 20 ms: 4825 of 15979,
`gx` 7.6 ms average, `present` 7.2 ms, 176 pacer re-anchors and late sound), and even the light
games had 5-14 % of their frames over 20 ms (`gx` 1.6 ms, but `present` 6.5 ms average, 17.8 ms at
p95). Earlier windowed runs on the same machine, before the frames-in-flight limit of §37, had
almost none (26 frames over 20 ms in 7345). This milestone finds where the time went and removes
it. Nothing in the decomp changed; every picture is the same (`make test`).

### Files

| File | What |
|---|---|
| `port/extern/aurora` | Three patches (below): the frames-in-flight limit counts rendering, not presentation; cheaper per-draw index work; render-thread timing. |
| `port/host/aurora/wpgx.cpp`, `wpgx.h` | Drain-mode GX processing; the pipeline cache key without the build ID; the new frame statistics. |
| `port/host/src/host_main.cpp` | `--frames-ahead` defaults to 1; draw-done syncs timed. |
| `port/host/src/host_perf.cpp`, `include/port/perf.h` | New columns: `gx_sync_ms`, `syncs`, `queue_latency_ms`, `render_latency_ms`, `acquire_ms`, `present_call_ms`. |

### Where the time went

The user's per-frame CSVs (`--perf`), split by scene, and new measurements: headless real time
and virtual time on the NVIDIA GPU (`--gpu nvidia --live-gfx`, the test scripts), and real
Wayland windows in a private headless gnome-shell (as in §37: Mutter composites on the Intel GPU,
the game renders on the NVIDIA GPU, as on the user's desktop), with a sampling profiler
(`SIGPROF` and `backtrace`, preloaded) where `perf` is not allowed.

1. **The game thread waited for the compositor.** With `--frames-ahead 0` (the default of §37)
   a frame began only when the previous one's GPU work *and presentation* were done. The
   presentation includes acquiring the window's next image, and on this driver that waits until
   the compositor releases one, up to a refresh: the new `acquire_ms` column shows 2-5 ms on
   average and up to 8 ms, `present_ms` the same wait on the game thread. The game's own clock
   (59.94 Hz retraces) and the compositor's (60 Hz) drift, so every so often the wait pushed a
   frame past its retrace: in the user's run of the light games every sixth frame or so took
   31-35 ms with 18-22 ms of it in `present`, and the next one 5-6 ms to catch up. With
   `--frames-ahead 1` the same coupling came one frame later (run B: frames over 20 ms 10165 of
   60491). In the private compositor (menus, 2400 frames): limit on presentation, frames ahead 0:
   186 frames over 20 ms; 1: 32; no limit: 2.
2. **Charge! translates 700 draws a frame, and waits for the translation 9 times.** Aurora
   translated the GX stream on its FIFO thread as it arrived (one hand-over and wake-up per
   draw), and the game thread waits for that thread at every draw-done sync (`GXDrawDone`,
   `GXSetDrawSync`, `GXPixModeSync` after copies; the frame's end): Charge! syncs 9 times a
   frame (`syncs`), `gx` is almost all of it waiting (`gx_sync_ms`). So the thread overlapped
   little and cost a hand-over per draw. Profile of the translation (virtual time): reading
   every index of every draw to size the guest vertex arrays (about a fifth), building
   triangle-list indices one checked append at a time (a tenth), hashing textures after each
   texture-cache invalidation, copying vertices and arrays.
3. **The CPU clocks down when paced.** On the default power profile (`powersave` governor,
   `balance_performance`) the cores stay at a low clock (0.8 GHz at idle) when the game sleeps
   most of a frame: in real time the same frame's `calc` and `gx` are 2-4 times their
   virtual-time values, and they shrink again when something keeps a core busy (the old
   frames-in-flight wait polled every 100 us and did: Charge! headless real time, the same
   build, `calc` 1.0 / `gx` 1.4 ms with `--frames-ahead 0`, 2.7 / 5.0 ms with 1). The work per frame has to fit at the low clock; it now does with room
   (below).
4. **First-use pipelines.** A pipeline compiled the first time a draw needs it blocks that frame
   (up to 1 s in the user's runs). The disk cache of §35 removes that from the second visit on,
   but it was keyed by the library's build ID, so every new build of `libwiiplay_gx.so` started
   cold (run C: 576 pipelines compiled, 576 draws waited).
5. **The sound lag** followed from 1-3: a game falling behind re-anchors the pacer (176 times in
   run A) and the queue sat at 100 ms with the controller pinned at its bound.

### Fixes

**Frames ahead count rendering, not presentation** (Aurora patches 1 and 3). The limit now counts
a frame done when its rendering is (a work-done callback registered after the frame's command
buffer, before the presentation), and the render thread reports that before it acquires the
window's image (polling Dawn's events every 200 us until the frame's rendering is done; the
GPU would draw the presentation's blit after it anyway). The frame slot and staging buffer are
released before presenting too. The game thread now never waits for the compositor; the render
thread does, as before, and still presents every frame (mailbox: the newest at each refresh).
`--frames-ahead` defaults to 1 in every mode: the game works on a frame while the GPU draws the
previous one, as the Wii's CPU and GP overlap. A frame's input is read at its retrace, so this
adds no latency while the GPU draws a frame in less than a frame's time (here 3-7 ms,
`render_latency_ms`); a slower GPU makes the game wait (`present`) instead of queueing frames.
`--frames-ahead 0` still waits for the previous frame's rendering (not its presentation).
`gpu_latency_ms` (end to presentation done) is now as seen by the render thread, which notices it
only at its next frame: about a frame more than the real time.

A frame acquired early (at the frame's start, so that the compositor's wait overlaps the game's
frame) was tried and dropped: the render thread then waits there while the frame's passes queue
behind it, and rendering finished later (menus: 173 frames over 20 ms with frames ahead 0).

**GX translated at the syncs** (wpgx). Aurora's drain mode: the stream is translated on the game
thread when it syncs or the frame ends. Same output (the tests' hashes and pictures are
unchanged), no hand-overs: Charge! `gx` in virtual time 1.70 -> 1.43 ms on an idle machine,
6.8 -> 3.6 ms under load; headless real time (low clocks) 6.5 -> 5.0 ms.

**Cheaper per-draw index work** (Aurora patch 2). Strip, fan and quad indices are written into
the index buffer directly. The guest arrays' sizes come from the largest index a draw uses:
`calc_vtx_size` records where the indices sit, and the largest indices are cached by the hash of
the draw's vertex bytes (with the count and layout; direct-mapped, 4096 entries), since the
models draw the same vertices every frame. Uploads and pictures are unchanged. Together with the
drain mode: Charge! `gx` (virtual time, idle machine) 1.70 -> 1.43 ms, the whole 7000-frame run
14.9 -> 13.5 s.

**The pipeline cache survives a new build** (wpgx). `pipeline_cache.db` holds pipeline
*configurations* (Aurora checks each row's size and version and compiles them itself at start;
Dawn's `dawn_cache.db` is keyed by contents), so a stale file cannot give a wrong pipeline. The
key is now our patch version only (`kPipelineCacheKey`, 5): a rebuilt game compiles its known
pipelines in the background at start instead of one blocking first use at a time. The first
visit to a scene on a machine still compiles its new pipelines (blocking, as §35 chose).

### Measurements

Real Wayland windows (1280x912) in the private compositor, 60 Hz virtual monitor, real time,
SDL's dummy audio, the test scripts, pipeline caches warm; "before" is the user's build of the
day (frames ahead 0, the limit on presentation, the FIFO thread), "after" this one (defaults).
The games from frame 3900 (in play), the menus from frame 300; the idle machine on the default
power profile:

| Scene (frames) | wall p95 / p99 ms | frames over 20 / 33.3 ms | `gx` avg / p95 ms | `calc` ms | `present` ms |
|---|---|---|---|---|---|
| Menus (4599) | 17.7 / 18.8 -> 16.8 / 17.0 | 15 / 1 -> 3 / 1 | 0.91 / 1.21 -> 2.77 / 4.20 | 0.88 -> 2.55 | 5.81 -> 0.08 |
| Charge! (3099) | 17.2 / 18.5 -> 16.9 / 17.1 | 5 / 0 -> 0 / 0 | 1.94 / 2.23 -> 4.70 / 7.53 | 1.04 -> 2.52 | 7.40 -> 0.06 |
| Shooting Range (4099) | 18.0 / 19.5 -> 16.9 / 17.1 | 19 / 0 -> 1 / 0 | 1.37 / 1.73 -> 3.03 / 4.98 | 1.27 -> 2.87 | 6.44 -> 0.07 |
| Laser Hockey (3099) | 18.3 / 19.3 -> 16.9 / 17.0 | 13 / 0 -> 0 / 0 | 0.99 / 1.33 -> 2.45 / 3.35 | 0.89 -> 2.44 | 5.42 -> 0.08 |
| Fishing (3099) | 17.4 / 18.6 -> 16.9 / 17.1 | 3 / 0 -> 0 / 0 | 1.45 / 1.73 -> 3.83 / 5.57 | 1.06 -> 2.74 | 6.34 -> 0.07 |
| Tanks! (3099) | 17.4 / 18.4 -> 16.8 / 17.0 | 10 / 0 -> 0 / 0 | 1.32 / 1.53 -> 3.96 / 5.06 | 0.77 -> 2.37 | 6.18 -> 0.08 |
| Billiards (3099) | 17.2 / 18.6 -> 16.8 / 17.0 | 3 / 0 -> 0 / 0 | 2.04 / 2.47 -> 4.46 / 6.81 | 0.85 -> 2.37 | 7.03 -> 0.07 |
| Find Mii (3099) | 17.9 / 19.2 -> 16.9 / 17.1 | 21 / 2 -> 5 / 4 | 0.98 / 1.81 -> 2.43 / 5.04 | 1.07 -> 2.75 | 5.94 -> 0.10 |
| Pose Mii (3099) | 17.2 / 18.3 -> 16.8 / 17.0 | 2 / 0 -> 0 / 0 | 1.34 / 1.63 -> 3.59 / 5.05 | 0.97 -> 2.84 | 6.67 -> 0.07 |
| Table Tennis (3099) | 17.4 / 18.8 -> 16.8 / 17.0 | 3 / 0 -> 0 / 0 | 1.47 / 1.92 -> 4.35 / 5.77 | 0.91 -> 2.70 | 6.63 -> 0.08 |

No pacer re-anchors, underruns or skipped AX frames in any run; the audio queue 26-34 ms before,
40-44 ms after (the game no longer waits for the compositor, so the controller sees a steadier
producer). The remaining frames over 33 ms are scene loads and Find Mii's Mii setup.

`present` (the game thread waiting) fell from 5-7 ms a frame to under 0.1 ms. `gx` and `calc`
*rose*: not more work, a slower clock. The old wait polled every 100 us, which kept a core busy
enough for the governor to hold a high clock; the game now sleeps through the slack and the cores
stay at a low clock. Measured at equal clocks the work fell (Charge!, virtual time on an idle
machine: `gx` 1.70 -> 1.43 ms; headless real time with `--frames-ahead 1` for both builds: 6.5 -> 5.0 ms).
Charge!'s busiest frames (p95) now take about 10 ms of the 16.7 at that clock.

The private compositor is kinder than the user's desktop (two monitors on two GPUs): the old
build dropped 0.3 % of frames here against 5-30 % there. The mechanism removed (the game thread
waiting for the compositor's release) is what dominated there (`present` 6.5-7.2 ms on average,
p95 16-18 ms, in every scene).

### Aurora patches

On top of the `wiiplay` branch (2f8c5fd), committed in the submodule on a local branch; the
series is exported as patch files next to the worktree for the fork:

| # | Subject | Files |
|---|---|---|
| 1 | `gfx: frames-in-flight limit counts rendering, not presentation; render-thread timing` | `include/aurora/gfx.h` (`AuroraFrameStats.lastQueueLatencyNs`, `lastRenderLatencyNs`, `acquireNs`, `presentNs`), `lib/gfx/frame.cpp`, `lib/gfx/present_target.cpp`, `.hpp` |
| 2 | `gx: cheaper per-draw index work` | `lib/gx/command_processor.cpp`, `lib/internal.hpp` (`ByteBuffer::append_uninitialized`) |
| 3 | `gfx: report a frame's rendering done before blocking in the acquire` | `lib/gfx/frame.cpp` |

### The power profile

Not changed by the port. With `powerprofilesctl set performance` (or a governor that keeps the
cores up) `calc` and `gx` fall to their virtual-time values; the defaults are enough now
(Charge!: about 7 ms of the 16.7 ms frame on average at the low clock).

### Verified headless vs. by hand

Verified: `make test` (64-bit) and `make M32=1 test`, `make playtest`; the per-game runs above,
in real Wayland windows of a private compositor on the same GPUs. Not verifiable here: the
user's own desktop (two monitors on two GPUs, the 144 Hz panel), whose compositor timing is
worse than the private one's (the old build, menus: 15 frames over 20 ms of 4599 here against
5-14 % in the user's runs); `acquire_ms` and `render_latency_ms` in a new `--perf` run there
show what remains.

## 40. The Mii database: your own Miis, their records

Until now only the six guest Miis appeared in Select Mii: the port's NAND had no Mii database,
and the game keeps no My Record for guests. Wii Play reads the console's Miis the way every
Wii game with RVLFaceLib does; this milestone makes that path work on a little-endian 64-bit
host and gives the player a way to bring Miis made in Dolphin's Mii Channel.

### How the game reads the database

- **Boot.** `RPSysKokeshiManager` calls `RFLInitRes`, which boots RFL; `RFLiBootLoadDatabaseAsync`
  opens `/shared2/menu/FaceLib/RFL_DB.dat` read-only (`NANDPrivateSafeOpenAsync`), reads
  `RFLiDatabase` (0x1F1E0 bytes) whole into RFL's heap and checks the CRC over all of it
  (`RFLiCheckHeaderCRCAsync`: CRC-16/CCITT, 0 for a good file). A missing file is an empty
  database (`RFLiFileBrokenType_DBNotFound`, guests only); a bad CRC marks it broken.
- **The file.** `RFLiDatabase`: `"RNOD"`, 100 `RFLiCharData` records (0x4A bytes each, the
  Miis of the Mii Channel), the isolation flag (bit 31 of the word at 0x1CEC), 13 bytes of
  special-invitation data and a WiiConnect24 month and day, then `RFLiHiddenDB` at 0x1D00: `"RNHD"`, the head and tail of a
  linked list and 10000 12-byte entries (create ID, `sex:1 next:15`, `prev:15`), the CRC at
  0x1F1DE. After it come the 10000 hidden records (`RFLiHiddenCharData`, 0x40 bytes: Miis seen on
  other consoles' remotes) and a padding tail: 779968 bytes as `RFLiFormatAsync` writes it.
- **Use.** `RFLIsAvailableOfficialData` / `RFLiGetCharData` for each slot (a create ID, the
  character data valid, not a temporary ID); `RFLSearchOfficialData` by create ID (the save's
  player list, `RPSysPlayerMgr`); the hidden list for the hidden middle databases
  (`RPSysMiddleDB`, `RFLiGetHiddenNext` / `Prev`, `RFLiLoadHiddenDataAsync` reads one hidden
  record from the file at `0x1F1E0 + index * 0x40`).
- **Writes.** Wii Play never calls `RFLiSaveDatabaseAsync` itself. RFL writes the file only when
  it copies Miis from a remote's memory into the hidden database (`RFLLoadControllerAsync` ->
  `RFLiWriteCtrlToHiddenDB`, which formats a missing database first), and only when the
  isolation flag is clear. The host's `WPADReadFaceData` reports no controller, so on the
  host the game never writes it.
- **Records.** The save (`RPParty.dat`) has a player list of 100 entries, one per database
  slot: the Mii's create ID, its records and medals. Picking a Mii the first time registers it
  ("Added ... to the player list"); `RPSysPlayer::loadData` resets an entry whose create ID is
  not the Mii's (the Mii was erased or replaced).

### What was wrong on the host, what changed

Before this, a database Mii was rejected: `RFLiIsSpecialID` read the first word of the create
ID natively, so the "not special" bit (bit 31 of the big-endian word) was tested on the wrong
byte; a Mii Channel Mii (first byte 0x8x) looked special and, not being local-only, failed
`RFLiCheckValidInfo`. Select Mii showed only the guests. The fixes (all `PORT_HOST`, the
matching build unchanged: `check_matching.py`, 974 units identical):

| File | Change | Why |
|---|---|---|
| `src/RVLFaceLib/RFL_Database.c` | `RFLiIsSpecialID`, `RFLiIsTemporaryID`, `RFLiSetTemporaryID`: the create ID's first word through `PORT_CREATE_ID_WORD` (`PORT_BE32` on the host, the identity on the Wii); the CRC stored with `BE_SET` | the flags are the top bits of a big-endian word; the CRC is checked over the bytes, so it must be stored big-endian for a written database to load again |
| `include/RVLFaceLib/RFLi_HiddenDatabase.h` | `RFLiHiddenDB::head`, `tail` `be_s16`, `crc` `be_u16` | big-endian in the file and in memory (the database is read and written whole) |
| `include/RVLFaceLib/RFLi_Format.h` | host `RFLiTableData`: the two bit-field words `be_u16` (`sexNext`, `prevWord`), read and written through `RFLi_TABLE_GET` / `RFLi_TABLE_SET` (inline accessors on the host; `(d)->field` on the Wii) | CodeWarrior allocates `sex:1 next:15` from the most significant bit of a big-endian word; GCC on a little-endian host would put `sex` in bit 0 of the first byte |
| `include/RVLFaceLib/RFLi_Database.h` | host: `isolation` is bit 7 of the byte at 0x1CEC (`u8 padding1:7; u8 isolation:1; u8 padding1b[3]`) | the same allocation order |
| `src/RVLFaceLib/RFL_HiddenDatabase.c` | the list code through the accessors; the stored hidden record's `sex`, `localonly` and `favorite` read / cleared by byte (`PORT_STORED_SEX`, `PORT_STORED_LOCALONLY`, `PORT_CLEAR_FAVORITE`) | `RFLiHiddenCharData` stays big-endian in memory (§30), while the host declares its bit-fields for a word in host order |
| `src/RVLFaceLib/RFL_Format.c`, `RFL_MiddleDatabase.c` | `RFLiClearTableData`, `RFLiClearDBBuffer` and the hidden list's head / tail through the accessors | as above |
| `port/host/src/host_nand.cpp`, `include/port/host.h` | **New** `PortNANDImportMiiDB(src)`: checks the file (`"RNOD"`, at least 0x1F1E0 bytes, RFL's CRC over the header) and copies it to `<nand>/shared2/menu/FaceLib/RFL_DB.dat` (a temporary file, then a rename) | the user-facing import |
| `port/host/src/host_main.cpp` | **New** `--mii-db FILE` / `--mii-db dolphin`: imports before the game starts; `dolphin` takes the newest of the Flatpak's, the XDG and the old user folder's `Wii/shared2/menu/FaceLib/RFL_DB.dat` | RUNNING.md, "Your own Miis" |
| `port/host/tools/check_matching.py` | `RFLi_Database.h`, `RFLi_Format.h` in the touched list | |

The character records needed nothing new: §30 already converts `RFLiCharData` /
`RFLiHiddenCharData` through a host-order copy (the bit-fields, the name), and the create ID
is compared byte for byte. RFL's NAND access (`RFL_NANDAccess.c`) goes through the host NAND
of §22 unchanged: `/shared2/...` maps to `<nand>/shared2/...`.

The source file of `--mii-db` is only read. Writing Dolphin's NAND would race a running
Dolphin, and the port does not write the database anyway; the copy in the port's NAND stays
until the next import. The port does not look for Dolphin's database on its own: a run
without `--mii-db` uses what its NAND has, so the tests (scratch NANDs) never see a user's
Miis.

### The test (`make miitest`)

`tests/mii_db.py` writes a database from scratch: the bit layout of `RFLiCharData`
(MSB-first fields per 16-bit word), a Mii named "Tester" made from parameters in the middle of
each range (`RFLi_Config.h`), a create ID as the Mii Channel makes them (`0x8A000000`, then four
console bytes), an empty hidden list (head / tail -1, every entry's next / prev -1), the
isolation flag set as `RFLiClearDBBuffer` leaves it, the CRC, and the 779968-byte size. No
console or disc data goes into it. `tests/test_mii.py` (`make miitest`, about 25 minutes,
not part of `make test`) runs `boot` three times:

| Run | Input | Checks |
|---|---|---|
| bad | `--mii-db` with one bit flipped | exit status 1 with the checksum message; no database in the NAND |
| first | a fresh NAND, `--mii-db`, `tests/boot/menus_mii.txt` (Select Mii opens on the "Wii Console" tab: the Mii, "Added Tester to the player list", "Play with this Mii.", OK) then `tests/boot/shooting.txt`, 16200 frames | scenes -1, 0, 25, 1, 27, 18, 27; the NAND's `RFL_DB.dat` is the file given, byte for byte; save slot 0 has the Mii's create ID, the registered flag and the Shooting Range record equal to the game's final score (`host_probe`); no other slot used |
| again | the same NAND, `tests/boot/menus_mii_saved.txt` (the save exists: no save prompt; the Mii again, no "Added" message), 2400 frames | scenes -1, 0, 25, 1, 27; slot 0 unchanged (create ID, flags, records); the party menu's "Record: N points" box differs from the first run's (0 points); `RFL_DB.dat` still unchanged |

Faces: in Select Mii, "Play with this Mii." and the party menu (both runs) the head's box has
more than 1500 skin-coloured pixels (a dialog over it has none). `--png DIR` writes the screens.

```
test_mii:
  bad database refused: status 1
  first: scenes -1 -> 0 -> 25 -> 1 -> 27 -> 18 -> 27 over 16200 frames; Mii 8a0000000c0ffee0 on the player list (slot 0); Shooting Range score 534 saved as its record
  again: scenes -1 -> 0 -> 25 -> 1 -> 27 over 2400 frames; same slot, create ID and records; the record shown on the party menu; RFL_DB.dat never written
  faces (skin-coloured pixels): again_party_menu 2820, again_play_with_mii 4613, again_select_mii 3699, first_party_menu 2832, first_play_with_mii 4613, first_select_mii 3699
test_mii: OK
```

The same Mii without the fixes: Select Mii opened on the Guest tab (the Mii rejected as special).
`--mii-db dolphin` was checked with a scratch `HOME`: the Flatpak's file taken over an older one
in `~/.local/share/dolphin-emu`, then the XDG one alone, then none (status 1, the message).

### Against the original (Dolphin)

The same generated `RFL_DB.dat` in the NAND of a headless Dolphin 2609a (`-p headless`, a fresh
user directory, the Wii Remote through Dolphin's pipe input, the GDB stub as in §38), the
menus of `tests/boot/menus_mii.txt` timed from each scene's first frame:

- RFL loaded it: at Select Mii, `sRFLManager`'s broken type 0 and last error 0, the database
  in RFL's heap with the file's bytes (the Mii at slot 0, the isolation word, the CRC).
- With the software renderer (the frame read from the XFB that `VISetNextFrameBuffer` is given)
  Select Mii opens on the "Wii Console" tab with the same face and name as the port draws, and
  picking it shows "Added Tester to the player list. ...".
- After the party menu (`-v Null`, faster), Dolphin's `RPParty.dat` has slot 0 with create ID
  `8a0000000c0ffee0`, flags 6 and empty records, as the port's after the same menus. The two
  saves differ in 15 bytes, all expected: the last remote's index and address (0x8-0xE), the
  registration date (0x1E), the debut time (0x34) and the checksum. Dolphin did not change the
  database file either.

The pipe's pointer does not map linearly to the picture at the edges: Dolphin needed x = 148 for
the Mii the port's script points at with 115.

### Verified

`make test` (64-bit) and `make M32=1 test`, `make miitest`, `make playtest` (the guest path
unchanged), `check_matching.py` (974 units identical).

### Left

- Miis on a Wii Remote (the "Wii Remote" tab): `WPADReadFaceData` / `WPADReadMemoryAsync` on
  the host WPAD, the controller buffer's 16-bit fields (`RFL_Controller.c`: `secretFlag`,
  `deleted`, `checksum`) and then the hidden-database writes this milestone prepared.
- The hidden list (Miis copied from other remotes) is read through the new accessors when the
  game builds its hidden middle databases, but the test's database has an empty list, so only
  the empty case runs; a database from a console that saw other remotes' Miis would exercise
  the rest.

## 41. Windows: a MinGW-w64 build, cross-compiled on Linux

The port builds for Windows x86-64 from Linux with MinGW-w64 GCC (`x86_64-w64-mingw32-g++-posix`,
GCC 13, posix threads) rather than MSVC: the layout fixes of §3 onward assume GCC and the
Itanium C++ ABI. `make WIN64=1` builds every harness and the game into `build/win64/` as
`NAME.exe`; `make WIN64=1 dist` packages the game (RUNNING.md, "Windows"). Linux builds are
unchanged.

### Files

- `include/port/platform.h`, `src/host_platform.cpp`: the operating system under the host code,
  one Linux and one Windows half: directories (create, test, resolve, compare paths; a rename
  that replaces), the per-user folders (XDG; `%APPDATA%` / `%LOCALAPPDATA%`), Dolphin's user
  folders for `--mii-db dolphin`, reserved and committed memory (mmap / mprotect; VirtualAlloc),
  fibers for the cooperative OS threads (ucontext; Win32 fibers), the monotonic clock and a
  precise sleep. `host_arena.cpp`, `host_thread.cpp`, `host_nand.cpp`, `host_record.cpp`,
  `host_live.cpp` and `host_main.cpp` call it instead of POSIX.
- `windows/mingw_cc.py`: the compiler driver for the Windows build (below: weak definitions,
  unreachable references).
- `windows/wiiplay.manifest`, `windows/wiiplay.rc`: linked into every executable.
- `windows/README-WINDOWS.txt`, `windows/play.bat`: the package's.
- `tests/libm_compare.c`, `make WIN64=1 libm`: the game's math (§42) on both builds, compared
  through a pipe (below, "Floats").
- `aurora/mingw-w64.cmake`: the CMake toolchain for Aurora; `aurora/CMakeLists.txt` takes
  Dawn's Windows package and builds `wiiplay_gx.dll`.

### Build

`make WIN64=1` sets the MinGW driver, `build/win64/` and these flags on top of the Linux ones:

| Flag | Why |
|---|---|
| `-mno-ms-bitfields` | MinGW lays out bit-fields as MSVC does by default; the decomp's bit-field structs (RFL's Mii data, GX registers) need GCC's layout. `types.h` checks it with a `static_assert`. |
| `-D__USE_MINGW_ANSI_STDIO=1` | MinGW's own printf / scanf: `%lld`, `%zu`, `long double` and float formatting as glibc's, not msvcrt's |
| `-fno-asynchronous-unwind-tables` | no SEH unwind data (`.pdata`), which PE's linker script keeps, and with it every function: `--gc-sections` would remove nothing (below) |
| `-fno-jump-tables` | GCC puts a switch's jump table in the unit's shared `.rdata`, which every string of the unit keeps, and with it every function that has a switch |
| `binmode.o` | files open in binary mode by default (and `host_platform.cpp` sets stdin / stdout / stderr binary): no CR before LF in the FRAME lines, as on Linux |
| the manifest | UTF-8 as the process code page (Windows 10 1903+): `fopen`, `stat`, `opendir`, `argv` and `getenv` take and give UTF-8, so paths with any characters work through the C library; long paths |
| `--stack,8388608` | the main thread's stack as on Linux (the fibers have their own 1 MiB stacks, as the ucontext stacks) |

`libstdc++-6.dll` stays a DLL, as `libstdc++.so` is on Linux: the executables' operator new /
delete (the game's heaps, §28) serve only the executable's own code, never the runtime's
internal allocations. Windows needs no `exe.map`: a DLL never binds to an executable's symbols.
The exe's operator new comes from `host_sdk.cpp`'s weak default (or `eggHeap.cpp`'s), not the
runtime's (`mingw_cc.py` applies those defaults first, as an ELF link takes an archive's
definition before a shared library's).

### Traps found, and their fixes

- **Weak definitions** (the host's defaults in `host_sdk.cpp`, `host_gx.cpp`, `host_gfx_stubs.c`,
  `host_snd.cpp`, ...): on PE/COFF GCC emits a weak definition of W as a weak external whose
  default is the global `.weak.W.ANCHOR`. The GNU ld in use (2.45.90) never lets it satisfy
  another object's reference, and an archive's index does not list W, so no default was ever
  linked. `mingw_cc.py` runs the link; for each symbol left undefined that some input defines
  weakly it adds `-u .weak.W.ANCHOR --defsym W=.weak.W.ANCHOR` and links again. A strong
  definition still wins, as on ELF.
- **Weak data in headers** (`DECL_ADDRESS`: `OS_BOOT_INFO` and the other low-memory globals, the
  hardware register blocks): the same variable weak in many units collided (each default is
  named after its unit's first global, often the same inline function). On Windows
  `DECL_ADDRESS` makes it a COMDAT "select any" definition (`selectany`, zero) instead.
  `OS_BUS_CLOCK_SPEED`'s strong value (`host_os_clock.c`) is stored by a constructor there.
- **References from unreachable code**: with `--gc-sections` ELF's ld ignores undefined
  references in removed sections, which the harnesses rely on (they link whole units); PE's
  reports them and has no `--unresolved-symbols`. Once unwind data and jump tables no longer
  keep every function (the flags above), `mingw_cc.py` relinks with `--print-gc-sections` and,
  if every remaining reference is in a removed section, aliases those symbols to `abort`. Any
  reference from kept code still fails the link.
- **One definition for each inline / out-of-line pair**: the decomp defines some functions both
  in a header (inline) and in one unit (out of line), or specializes a template that no header
  declares (matching tricks). ELF linkers let the strong copy win; PE/COFF reports two
  definitions. Under `PORT_HOST` the headers declare those (`eggIBinary.h`: `IBinary<LightTexture>::GetVersion`;
  `RPSndObject.h`: three `TRPSndObject<8>` members; `RPActor.h`: `VF_0x3C`, `VF_0x40`,
  `IsEnabled`) and two units' inline copies of `RPSndObject2D`'s destructor go
  (`RPBilCamera_2.cpp`, `RPWlySound_3.cpp`). `check_matching.py`: 974 units identical.
- **`__declspec`**: MinGW predefines it; `decomp/compat.h` keeps its own mapping and adds the
  forms MinGW's headers use.
- **GCC 13's libstdc++** has no `std::sinf` and the other float math names (GCC 14 added them);
  `port/msl_math.h` (§42) declares them, as the game's own math.
- **LLP64** (`long` is 32-bit on Windows): no host or decomp code converts pointers through
  `long` (`-Werror=pointer-to-int-cast`, and C++ rejects narrowing pointer casts, so the build
  would fail); `%ld` is used only with `long`; file offsets through `ftell` / `fseek` are
  within the disc's files (under 2 GiB). Nothing else needed.
- **Paths**: `--record DIR:N` took a drive's colon for N; `--perf-out`, `--save` and the other
  file options find the directory with `\` too; the writable-location check compares resolved
  paths case-insensitively. Directories are created with `PortMakeDir`, which accepts an
  existing one (and `C:`). NAND renames replace the destination (`MoveFileEx`), as POSIX
  `rename` does.
- **Frame pacing** (§39): Windows' sleeps end at the next scheduler tick (up to 15.6 ms).
  `PortSleepUntilNs` waits on a high-resolution waitable timer (Windows 10 1803+) until 1 ms
  before the deadline and spins the rest; Linux keeps `sleep_until`.
- **The fibers**: `host_thread.cpp`'s threads were ucontext coroutines; they are now
  `PortFiber`s (ucontext on Linux, unchanged behaviour; `CreateFiberEx` / `SwitchToFiber` on
  Windows, with the floating-point state switched too).

### Aurora and Dawn on Windows

Dawn's prebuilt Windows package (`dawn-windows-amd64.tar.gz` of the same release as Linux's,
pinned by hash) is `webgpu_dawn.dll` built with MSVC, its import library and `dxcompiler.dll` /
`dxil.dll`. Its C API (`webgpu.h`, which the C++ wrapper `webgpu_cpp.h` is inline over) links
from GCC; its C++ API (`dawn::native`) does not (MSVC's mangling and standard library). Aurora
used the latter in two places; the fork (commit 36eb6cb) builds the
`DawnInstanceDescriptor` from its C layout and lists adapters for `--gpu` through
`RequestAdapter` (both power preferences, with and without the fallback adapter) when built by
MinGW against a Dawn DLL (`AURORA_DAWN_C_API_ONLY`). It also lower-cases two Windows SDK header
names and uses MinGW's import libraries. Aurora, SDL3 (static), abseil, fmt, zstd and the
C++ runtime go into `wiiplay_gx.dll`, which exports only the `wpgx_*` functions of `wpgx.h` (a
`.def` file made from the header at configure time). The toolchain file keeps the build
machine's pkg-config libraries out and sets `NTDDI_VERSION` to Windows 10 (abseil 20240722
otherwise uses WinRT time-zone headers that do not compile with this MinGW).

`webgpu_dawn.dll` and `dxcompiler.dll` need the Visual C++ 2015-2022 runtime (`MSVCP140.dll`,
`VCRUNTIME140(_1).dll`), which is not shipped: most Windows installations have it, else
Microsoft's `vc_redist.x64.exe` installs it.

Aurora tries Direct3D 12 first on Windows, then Vulkan (then D3D11). `--backend NAME` (or
`WIIPLAY_BACKEND`) puts one first.

### Running the tests under Wine

`$(OUT)/NAME` is a script that runs `NAME.exe` under Wine without a display (`DISPLAY` unset:
Wine opens no window), in its own prefix (`$TMPDIR/wiiplay-wine`), with the MinGW runtime and
Aurora's DLLs on `WINEPATH` and `WIIPLAY_BACKEND=vulkan` (Wine offers no Direct3D 12 for this
machine's GPU). So `make WIN64=1 unit`, `archives`, ... `boot` run the same test scripts on the
Windows executables. Dawn's Vulkan backend under Wine goes through winevulkan to the host's
Vulkan loader, so `VK_ICD_FILENAMES` selects lavapipe as on Linux, and the `--compare` tests
check the Windows harnesses' output against `build/host64`'s.

### Windows x64 passes arguments by position

On the PowerPC and on x86-64 System V, floats and integers / pointers take separate register
sequences, so an `extern "C"` declaration whose float and pointer parameters are in another
order than the definition's still works. Windows x64 assigns registers by position. Charge!'s
units declare six of `RPCollisionShape.cpp`'s vector helpers (`RP_clampVec3Abs`,
`RP_isVec3WithinAbs`, `RP_isVec3DiffWithinAbs`, `RP_dampVec3AndApply`, `RP_springStep`,
`RP_wrapAngleNear`) float first: `RP_clampVec3Abs` wrote through a stale register and the game
crashed at its end. Under `PORT_HOST` those units keep their calls through inline wrappers over
the definitions' form (an asm label binds the real symbol). A sweep of every C-linkage function
the game defines against every declaration of it found no others (`RP_Mtx34_getColumn`'s
struct return was fixed in §32).

### Floats: the C library's math functions

MinGW-w64's math functions (and msvcrt's, which some of them call, and whose results
differ again between Windows and Wine) are not glibc's: `make WIN64=1 scene` and
`particles` reported differences against `build/host64`, and some frames of animated
layouts (the title, the HOME Menu) hashed differently. The first fix (commit 283dabf)
vendored glibc's implementations (ARM optimized-routines' `sinf`, `cosf`, `sincosf`,
`powf`, `pow`; CORE-MATH's `tanf`, `asinf`, `acosf`, `atan2f`, `sincos`; exact `fmaf` /
`fma`, `hypotf`, `modff`, `sqrtf`, `sqrt`, `fminf`, `fmaxf`) into `windows/libm/`, so the
Windows build computed what the Linux build computed.

§42 replaced that: neither build uses the C library's math for the game any more. The
game's math is the Wii's own (the decomp's MSL math, compiled for the host), the fused
operations go to the port's exact `port_fmaf` / `port_fma` (`src/host_fma.c`: the vfmadd
instructions, or `windows/libm`'s exact software forms, moved there), and the estimate
instructions to Broadway's tables; all of it is the same code on Linux and Windows.
`windows/libm/` is gone (and with it `LICENSE-math.txt` in the package). The C library's
math is left only to host code that does not touch the game: `hypotf` (the `--perf`
summary), `fminf` / `fmaxf` (the HOME Menu probe's bounds), `lround` (exact in both) and
Aurora.

`make WIN64=1 libm` now compares the game's math between the builds: `tests/libm_compare.c`
under Wine writes each function's results (raw bits) to a pipe, the Linux build of it
(linked with `build/host64`'s library) reads and compares them; 2^24 inputs per function
by default (`LIBM_N=4294967296`: every float for the one-argument ones). The functions:
MSL's `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sqrt`, `pow`, `fmod`,
`floor`, `ceil`, `ldexp` and their float forms (`sinf` = `(float)sin((double)x)`, ...,
`modff`), `port_fmaf`, `port_fma`, Broadway's `fres` and `frsqrte` (and the double
estimate's low bits) and the refined `FInv` / `FrSqrt` forms; then, in one process, the
software fma forms against the dispatched ones (`libm_compare self`; the instructions
on a CPU with FMA). On Linux, `make libm` runs the latter.

### Verified under Wine (Wine 10, headless, lavapipe through winevulkan)

- `make WIN64=1 unit`: all unit tests (`test_be`, `test_be_c`, `test_port` with `wchar_t` 2,
  `test_port_c`, `test_layout`, `test_math`, `test_ef`, `test_polyline`).
- `archives`, `resources`, `layouts`, `models`, `anims`, `effects`, `efcurves`, `sound`,
  `collision`, `data`, `bti`, `rfl` (RFL's Mii data: the bit-field layout), `hbm`: OK.
- `platform` (heaps, DVD, NAND saves), `audio`, `input`: OK and identical to `build/host64`.
- `gx` (`test_gx`, `test_gxlyt`, `test_gx3d`): OK.
- `boot` (`test_boot`): boot, title, menus, save, a short Shooting Range and Laser Hockey, the
  HOME Menu (Close, Reset, Wii Menu), the real-time live run (dummy audio, synthetic SDL
  events, the pacer's sleeps) and the latency runs: OK. `gestures`: OK after the fix above.
- `scene` and `particles`: identical to `build/host64` (with `windows/libm`; since §42 with
  the game's own math, see §42 "Verified"). `libm`: OK.
- Frame hashes and the game state of every frame, Linux against Wine: the HOME Menu from
  the title (`home_title.txt`, 1800 frames) and the menus with a Shooting Range
  (`menus.txt`, 6450 frames): identical.
- `make WIN64=1 dist`'s zip, unpacked and run with nothing else on Wine's DLL path: 300 frames
  hash-identical to the Linux build.
- `--gpu list` on the machine's real GPUs through winevulkan (D3D12 refused by Wine's adapter).

### Needs real Windows

Direct3D 12 (and D3D11), a window with vsync / mailbox presentation, fullscreen, WASAPI sound,
real keyboard / mouse / gamepad input and rumble, the high-resolution sleep's pacing, the
`%APPDATA%` / Dolphin folder detection with a real Dolphin install, non-ASCII paths, and
whether the Visual C++ runtime is present. Run, from the unpacked zip:

    wiiplay.exe --gpu list
    wiiplay.exe --disc C:\path\to\disc --window --perf
    wiiplay.exe --disc C:\path\to\disc --window --backend vulkan
    wiiplay.exe --disc C:\path\to\disc --window --mii-db dolphin

and play the nine games, the HOME Menu and a save; the `--perf` summary (in
`%LOCALAPPDATA%\wiiplay`) shows the pacing.

## 42. The game's own math: MSL's functions, Broadway's estimates

Until now the game's math functions were the host C library's: glibc on Linux, a vendored copy
of glibc's implementations on Windows (§41, "Floats"). The Wii's are not glibc's. The game
calls MSL's (CodeWarrior's C library, fdlibm underneath; `src/MSL/`), and its paired-single
code starts from Broadway's estimate instructions. Both builds now compute the Wii's bits.

### Which functions the game calls, and where they come from now

On the Wii, `include/MSL/internal/math_double.h` makes every float function an inline over the
double one: `sinf(x)` is `(float)sin((double)x)`, and so are `cosf`, `tanf`, `asinf`,
`acosf`, `atan2f`, `sqrtf`, `floorf`, `ceilf`, `fmodf`, `ldexpf`, `modff` (via `modf`).
MSL's `<cmath>` has only the double overloads in `std::` (`std::sin(float)` returns a double),
but for `std::sqrt(float)` (`sqrtf`) and `std::fabs(float)`; there is no `powf` (the game calls
`pow`). `fabs` is the instruction. The DOL (`config/RHAE01/symbols.txt`, `splits.txt`) has
these MSL units: `e_acos`, `e_asin`, `e_atan2`, `e_fmod`, `e_pow`, `e_rem_pio2`, `e_sqrt`
(Broadway has no `fsqrt`: `sqrt` is fdlibm's bit-by-bit loop), `k_cos`, `k_rem_pio2`, `k_sin`,
`k_tan`, `s_atan`, `s_ceil`, `s_copysign`, `s_cos`, `s_floor`, `s_frexp`, `s_ldexp`,
`s_modf`, `s_sin`, `s_tan`, the `w_` wrappers (`acos`, `asin`, `atan2`, `fmod`, `pow`,
`sqrt`: 4-byte branches, `_IEEE_LIBM`) and `math_sun` (`scalbn`). Their objects have no
`fmadd` (the MSL library is built with `-fp_contract off`), so the host computes the same
double operations in the same order.

The host now:

| Before (game code on the host) | Now |
|---|---|
| glibc's (Linux) / `windows/libm`'s (Windows) `sinf`, `cosf`, `sincosf` (GCC merged sine and cosine), `tanf`, `asinf`, `acosf`, `atan2f`, `sqrtf`, `powf`, `fmodf`, `ldexpf`, `modff`, `atan2`, `pow`, `fmod`, `sqrt` | MSL's, compiled for the host: `port_msl_sin` ... (`src/MSL/*.c` under `-DPORT_MSL_MATH_IMPL`), the float forms as MSL's inlines |
| `std::sin(float)`, `std::pow(float, float)`, ... the float overloads | the double function, as MSL's `<cmath>` (Laser Hockey's, Charge!'s and the 3D sound's `std::atan2` / `std::pow` on floats) |
| `M_PI` a double (glibc) | MSL's float `3.141592653589793f`: `NW4R_MATH_PI`, `NW4R_MATH_DEG_TO_RAD` / `RAD_TO_FIDX`, `GXInitLightSpot`, `C_MTXPerspective`, `GXDraw`, `AXGetLpfCoefs` compute in single precision as on the Wii |
| `fres`, `frsqrte` (`FInv`, `FrSqrt`, `PSVECNormalize`, `PSVECMag`, `PSQUATNormalize`, `VEC2Normalize`, `MTX34InvTranspose`, `GetModelLocalAxisY2/3`): the exact `1 / x`, `1 / sqrt(x)`; `FInv` divided | Broadway's estimate (below), refined by the same Newton-Raphson steps |
| `fmaf` / `fma` (the fused host forms): the C library's | `port_fmaf` / `port_fma` (`src/host_fma.c`): `vfmadd` with FMA, else exact software forms; the same bits |

How (`port/host/include/port/msl_math.h`, force-included in every host compile after the C
library's `<math.h>` / `<cmath>`):

- **MSL units** (`MSL_MATH_SRCS` in the Makefile, `build/*/mslmath/`): the decomp's files
  unchanged, compiled with `-DPORT_MSL_MATH_IMPL -D_IEEE_LIBM` (the matching build's
  `cflags_runtime`), SSE double arithmetic also on `-m32`, `-ffp-contract=off`, `-fwrapv`.
  The header renames their definitions to `port_msl_*` (and `__ieee754_*`, `__kernel_*`), so
  they never interpose on the C library (Aurora and the GPU drivers keep theirs). One edit in
  the decomp, under `PORT_HOST`: `include/MSL/internal/fdlibm.h` declares the word order
  (`__LITTLE_ENDIAN`) on a little-endian host (its own test knows `i386`, not `x86_64`, and
  glibc's `<endian.h>` may define the macro anyway). `check_matching.py` lists it.
- **Decomp units** (`-DPORT_MSL_GAME_MATH`, the `decomp/` rules): `sin`, `sinf`, `sqrt`, ...
  are object-like macros naming the `port_msl_*` functions, with MSL's `<cmath>` overloads
  and MSL's `M_PI`.
- **Host code** (`src/host_*.cpp`, the tests): the same functions with the usual C++
  overloads (`std::sin(float)` is MSL's `sinf`), so the host's own stand-ins for SDK code
  (`PSMTXRotRad`, `C_MTXLookAt`, `GXInitLightSpot`) and the input code (`host_input.cpp`'s
  motions) compute the same on both systems. Functions MSL does not have (`hypot`, `fmin`,
  `fmax`, `lround`) stay the C library's; only host code calls them, none of it game state.
- Every host compile has `-ffp-contract=off`: the host fuses only where a host form says so.
- `types.h`'s `std::sinf` names for GCC 13's libstdc++ went (the header declares them).

Afterwards `nm` of `boot` lists no C library math function but `fminf`, `fmaxf`, `hypotf`
(host diagnostics) and `lround` (exact); `boot.exe` imports only MinGW's own `ceil` (its
runtime's).

### Broadway's rounding

- **Estimates.** `fres` and `frsqrte` (`port_fres`, `port_frsqrte` in `port/ps.h`) are
  Dolphin's lookup tables and interpolation (`Common::ApproximateReciprocal` /
  `ApproximateReciprocalSquareRoot`, which Dolphin took from a console). Checked against
  Dolphin's own code (compiled on the host) for every float: 0 differences (fres: all
  4278190082 non-NaN floats; frsqrte: every positive finite float). `frsqrte`'s result is a
  double with up to 26 fraction bits, not always a single.
- **The C operand of a single-precision multiply** (`fmuls`, `fmadds`, `ps_mul`,
  `ps_muls0`, ...) is rounded to 25 fraction bits first (Dolphin's `Force25Bit`). That is a
  no-op on a single, so compiled code (whose floats are always singles in their registers;
  CodeWarrior converts with `frsp`) never sees it; in the game's asm it matters where the
  C operand is a double: `frsqrte`'s estimate squared in `FrSqrt`, `VEC2Normalize`,
  `PSVECNormalize`, `PSVECMag` (`fmuls work, r, r`), and `PSQUATNormalize`'s
  double-precision scale in `ps_muls0`. `port_force25` / `port_fmuls_d` do it there; the
  product of the rounded operands is exact in double and is rounded once.
- **Fused single-precision operations** round once when their operands are singles
  (Dolphin's `NI_madd_msub` comment, with its Mario Strikers Charged example): the host forms'
  `port_fmaf` is that. Unchanged, but no longer the C library's `fmaf`.
- **Not modelled:** out-of-range float-to-integer conversions (PowerPC saturates, x86
  returns `0x80000000`; undefined in C, not met in the traces) and the `NI` (non-IEEE)
  FPSCR bit (the SDK leaves it clear).

### CodeWarrior's contraction: the difference that is left

The game's own units (`cflags_game`) and EGG are compiled with `-fp_contract on`:
CodeWarrior fuses `a * b + c` into `fmadds` (1819 fused instructions in the 572 RP and EGG
units' asm; nw4r, the SDK, RFL, HBM and MSL are built with it off). The host does not fuse
(no FMA without `-mfma`, and now `-ffp-contract=off`), so those sites round twice where the
Wii rounds once: a last-bit difference in some results, every frame.

An experiment (not committed): the RP and EGG units built with `-mfma -ffp-contract=fast`.
GCC then fuses 2012 multiply-adds in those units (CodeWarrior: 1819); per function the counts
agree in 167 functions and differ in 224 (inlining and GCC fusing across statements; the
`#pragma fp_contract off` regions of `RPFshFish.cpp` and `RPBilCamera_4.cpp` are ignored by
GCC). Even so, Tanks! missions 1 and 3 (below) matched Dolphin in every frame (mission 1 bit for bit). Making
it the default would need an FMA CPU (x86-64 since 2013: Haswell, Piledriver; the build would
not start on older ones without a second, non-fused build) and a site-by-site check; that is
the user's decision, not made here.

### Against the original (Dolphin)

**The functions themselves.** `dolphin_oracle.py` (job scratch) boots the disc in headless
Dolphin, stops in a frame, and calls the game's own functions through the GDB stub
(arguments in `f1`/`f2`/`r3`/`r4`, the return address a breakpoint on `nan()`, which the game
never calls): 20000 inputs each, then the port's old forms (the C library's float function,
the exact estimates) and the new ones on the same inputs:

| Function (address) | Inputs | Old port ≠ Wii (as float) | New port ≠ Wii |
|---|---|---|---|
| `sin` (800C1230), range ±70 | 20000 | 286 (1 ulp) | 0 (the double result, bit for bit) |
| `cos` (800C0D28) | 20000 | 233 (1 ulp) | 0 |
| `pow` (800C1390), [0,10]^[-8,8] | 20000 | 13 (1 ulp) | 0 |
| `tan`, `atan`, `atan2`, `asin`, `acos`, `sqrt`, `fmod`, `floor`, `ceil` | 20000 each | 0 | 0 |
| `nw4r::math::FrSqrt` (8002F9AC) | 20000 | 7662 (up to 2 ulp) | 0 |
| `PSVECMag` (800E8290) | 20000 | 6607 (up to 4 ulp) | 0 |
| `PSVECNormalize` (800E824C) | 20000 | 7591 (up to 4 ulp) | 0 |
| `nw4r::math::VEC2Normalize` (8002FCC4) | 20000 | 7613 (up to 4 ulp) | 0 |
| `nw4r::math::MTX34InvTranspose` (8002FDAC) | 20000 | 3820 (up to 2 ulp) | 0 |

(10 of the 340000 calls first came back wrong through the stub, a stop out of step; called
again one by one, all 10 equal the new port's.) Over 10^8 random game-range inputs, glibc's
`sinf` / `cosf` / `powf` differ from MSL's float forms in 0.16%, 0.12% and 0.008% of inputs;
`tanf`, `asinf`, `acosf`, `atan2f`, `sqrtf` (correctly rounded in glibc) agree with them.

**A game, frame by frame** (§38's method, the traces now with every position and aim
direction as float bits): Tanks! mission 1, idle player, seeds written at
`RPTnkGameManager::Start`, 1500 frames; mission 3 likewise, 2500 frames:

| Build | Mission 1: frames whose positions / aim bits differ from Dolphin | first | Mission 3 |
|---|---|---|---|
| before (C library math, exact estimates) | 1481 of 1500 | frame 0 (the enemy's aim -1.0 where the Wii has -0.99999994, `0xbf7fffff`: `PSVECNormalize`'s estimate) | 2497 of 2500, from frame 0 |
| now | 843 | frame 206 (1 ulp in an aim direction; CodeWarrior's contraction) | 1891, from frame 174 |
| now + contraction (the experiment above) | 0 | - | none numerically; 508 frames from frame 658 have +0 where the Wii has -0 (an aim direction's unused y: a fused negation's sign of zero) |

The game state (seeds, ages, alive flags, types, fire intervals) is identical to Dolphin's
in every frame of every run, before and after.

An idle Laser Hockey run was also traced (scene-relative inputs from the port's own
scripts, `RPHkyGame`'s state, timer, scores, the puck's position and velocity, both
paddles' positions and angles every frame, 4008 frames): seeds, state, timer and scores
equal Dolphin's throughout, but the player's paddle differs from frame 1 in both builds,
before and after (it follows the pointer: Dolphin's emulated IR pointer and the port's mouse
put it a fraction of a unit apart), so the floats there say nothing about the math. Games
driven by the playtest's game-side player (`host_probe_games.cpp`) cannot be replayed in
Dolphin, which has no such player.

### Before and after on the host

`make playtest`'s nine scripts, once each with the old and the new `boot` (`--trace --hash`,
the FRAME lines' `game=` state compared):

| Game | Game state | Why |
|---|---|---|
| Shooting Range, Find Mii, Fishing, Pose Mii | identical in every frame | their state does not reach the changed functions' last bits, or never feeds a decision |
| Laser Hockey | first differs at frame 4265 (the puck one unit off at the first rally); the game-side player wins 14-0 instead of 12-0 | physics in 16 sub-steps a frame (`atan2`, normalizations) under a player that reacts to every position: a last-bit difference grows into another rally |
| Billiards | from frame 4863 (the cue ball one unit off during the first shot); after that another game: 13 shots and 6 fouls, lowest ball 3 at the end, against 10 shots, 3 fouls, the rack cleared to ball 9 | the break and every collision amplify any last-bit difference |
| Table Tennis | from frame 4235 (the ball one unit off); the same 30 rallies and 61 hits | ball physics with the game-side player |
| Charge! | from frame 4832 (speed 0.72 against 0.73); 194 points against 193 | the cow's motion (sines, normalizations) |
| Tanks! | from frame 7877, mission 3: the player's shot kills its fifth tank a little earlier; the same 5 kills and the same game over | the game-side player aims at moving tanks |

Picture hashes differ from frame 656 (the menus: layout animations, `FInv` and the float
`M_PI` in the projection), as they should. Two scripts needed new timings, because the
game-side player's game is a different one: Laser Hockey's 14-0 has two more goals, each
a 170-frame pause, so time is up at frame 13747 (was 13407) and the results menu presses
moved 350 frames later (`hockey.txt`; `HOCKEY_FRAMES` 15000); in Billiards the player now
needs 21 shots and 7 fouls to clear the table (the result at frame 16239, was about 9950),
so the run is 17200 frames (`play_games.py`). The two-player Billiards of `make playtest2`
(§36) has not been rerun. None of this makes a game play differently in kind: the changes are
last-bit roundings that a chaotic game (billiards, hockey, a bot reacting each frame)
amplifies into a different but equally valid game; Dolphin comparisons above say the new
roundings are the Wii's.

### Linux and Windows

Both builds compile the same MSL objects and host forms; `make WIN64=1 libm` compares every
function above bit for bit: 33 functions, 2^24 inputs each, and the software fma forms
against the instructions: 0 differences. A whole Tanks! run of `make playtest` (menus,
three missions, the results; 9900 frames, the game-side player) under Wine and on Linux:
the game state, every frame's picture hash and the sound identical. So did a whole Billiards run
(17200 frames, the game-side player clearing the table in 21 shots): every frame identical.

### Verified

- `make test` (64-bit) and `make M32=1 test` (from a fresh `build/host32`: the Makefile now
  creates `$(OUT)` itself): OK. `test_math` checks `FrSqrt(4) == 0.49999997` and
  `FrSqrt(1) == 0.99999994`, the values the game's own `FrSqrt` returns in Dolphin;
  `test_anims.py` models `FInv` as Broadway's; `test_particles.py` allows 2^-20 where a
  gravity field went through the emitter's matrices (they carry `FrSqrt`'s estimate:
  `MtxGetScale`), 279 steps.
- Under Wine: `unit`, `archives`, `resources`, `layouts`, `models`, `anims`, `effects`,
  `efcurves`, `sound`, `collision`, `data`, `bti`, `rfl`, `hbm`, `platform`, `audio`, `input`,
  `gx`, `scene`, `particles` (identical to `build/host64`), `boot`, `gestures`, `libm`: OK.
- `make libm` (Linux): the software fma forms against the instructions: OK.
- `make playtest`: OK (the nine games twice each, with the new Laser Hockey and Billiards
  timings).
- `check_matching.py`: 1001 units identical (the 974 and the MSL math units, which now
  depend on `fdlibm.h`).
- `make WIN64=1 dist`: the package has no `LICENSE-math.txt` any more.
