# In-place pointer writes and other pointer-size hazards (design)

Status (2026-10-30): steps 0 to 4 of §7 are implemented in the host build (M0 arena,
`RES_PTR(T)` and the cast macros, the warning flags, `layout_asserts.cpp`, `-fshort-wchar` with
the MSL_C `wcs*`/`wmem*` functions, the `analyzeTag` big-endian read, ResFont, TPL with the
`lyt_texMap.cpp` bound test, the `lyt::res` structs, and the `RPSysTagProcessor` /
`RPPlayerNameTag` offsets); see `port/docs/HOST_BUILD.md` §4 to §10 for what was built and how it
is tested. `swprintf` and `vswprintf` are not routed yet (HOST_BUILD.md §6). Step 5 (g3d) is
implemented too, with g3d-4 (PAT0) brought forward because `ResFile::Bind` runs it: g3d-1
`RES_PTR` slots, g3d-2 under the agreed GX-object contract (Wii-layout `GXTexObj`/`GXTlutObj`
holding big-endian emulated addresses, host `GXInitTexObj`/`GXInitTlutObj`), g3d-3 with no
source change (the host `OSCachedToPhysical` already returns a `u32`), and every g3d resource
struct on `be_*`; all 132 BRRES on the disc set up as the Wii image (HOST_BUILD.md §11).
Step 6 (ef) is implemented: ef-1 with M2 (`RES_LINK`/`RES_LIST` and `RES_LIST_*`, which this
document called `EF_LIST_*`), ef-2 to ef-7 with `RES_PTR`/`PORT_STORE_PTR32`/
`PORT_PTR_TO_U32`/`PORT_U32_TO_PTR`, the ef resource structs on `be_*`, and the
`ForEachParam` cookie (§5.2) as `uintptr_t`; all 13 BREFF/BREFT pairs on the disc registered
in the game's order, each file the Wii image at every step (HOST_BUILD.md §12).
Step 7 is implemented: the §5.2 snd cookies are `uintptr_t`, and the sound archive code runs on
the host with `be_*` file structs (all 11 `.brsar` opened, every group loaded; HOST_BUILD.md
§13). Step 8 is implemented for PMP (§3.5) and KCL (§3.6): `RES_PTR` slots, `be_*` file
structs, every disc file through `RPResTable` / `RPKcl::SetResource` (HOST_BUILD.md §14). The
rest of step 8 (`RPPolyline`, HBM and RFL cookies, `RPKokeshiMgr_3`) is still design.
Line numbers refer to `src/` and `include/` as of 2026-10-30 (identical to the decomp) and do not
account for the step 0-2 edits.

Inputs: `docs/ASSETS.md` §2 and §5 in the decomp repo, `docs/PLAN.md` Phases 6 and 7, and the
existing port conventions: `include/port/be.h` (Wii: macros expand to plain types) paired with
`port/host/include/port/be.h` (host: wrapper types), `PORT_HOST` blocks in `src/` (for example
`src/egg/core/eggDvdFile.cpp:135`), and `port/host/include` being searched before `include/` on
the host.

## 1. Summary

- **What breaks.** The game loads files into heap buffers and uses them in place. Several loaders
  turn 32-bit offsets inside the buffer into absolute pointers and store them **back into the
  same 4-byte slot**. A few loaders also store list links, or pointers to data in other files,
  into file memory. On a 64-bit host a pointer does not fit in those slots. The structs that
  describe the file layout also declare those slots as `T*`, so on 64-bit their `sizeof` and all
  later member offsets change, and even read-only code breaks (for example
  `sizeof(EmitterResource)` locates the emitter descriptor).
- **The sites.** 13 sites write pointers into file memory, in 7 subsystems: TPL, `ut::ResFont`,
  nw4r `ef` (6 sites), g3d (3 sites), PMP and KCL. That is 9 more sites than ASSETS.md §2 lists:
  - `EmitterResource::LocateTracks`
  - `RPSysEffectMgr::setCaptureEfb`
  - the `TextureData*` view of the curve `work` slots in `ef_animcurve.cpp`
  - `ResShp::GXSetArray`, which writes a physical address into a display list in the file
  - the font rebuild that `lyt::TextBox` triggers for fonts inside layout archives
  - the TPL "already bound" test, which compares a pointer against `0x80000000`

  §3 lists each site.
- **Recommended mechanism.** Use two mechanisms and one helper for the 64-bit layer:
  - **M0, emulated Wii addresses.** The host reserves the MEM1 and MEM2 arenas as one host block
    and gives every byte in it a 32-bit "Wii virtual address": `0x80000000 + off` for MEM1 and
    `0x90000000 + off` for MEM2.
  - **M1, `RES_PTR(T)` slots.** Every pointer-typed member of a struct that mirrors file data is
    declared `RES_PTR(T)`. In the Wii build this expands to `T*`, so the build stays
    byte-identical. In the host build it is a 4-byte class that stores the **big-endian** Wii
    virtual address, decodes it on read and encodes it on assignment. u32 slots that hold
    pointers (`work`, track tables) use the matching cast macros `PORT_PTR_TO_U32` and
    `PORT_U32_TO_PTR`.
  - **M2, `RES_LINK`.** This replaces the `ut::Link` stored inside the BREFF and BREFT project
    headers: two `RES_PTR` slots (8 bytes on every build) and a small list helper with the
    `ut::List_*` API.

  The loaders keep relocating in place, exactly as on the Wii, so most sites need no edits or only
  one-line edits, and all readers stay unchanged. A relocated file in host memory is
  byte-for-byte the Wii image of that file, so it can be compared directly with a Dolphin RAM
  dump.
- **Use the same mechanisms in the 32-bit PC build.** M1 is needed on a little-endian 32-bit host
  anyway, because the slots hold big-endian offsets before relocation. The TPL bound test also
  fails with raw host pointers on 32-bit Linux, where heap addresses can be below or above
  `0x80000000` (§3.1). So a 32-bit build and a 64-bit build of these files differ only in the
  host pointer width hidden in M0.
- **`wchar_t`.** Build with `-fshort-wchar` on GCC and Clang, and link the decomp's own MSL_C
  wide-string functions under port names (§6). MSVC already has a 16-bit `wchar_t`. This avoids
  a ~350-line textual change to `char16_t`, and it also fixes three file and NAND layouts that
  embed `wchar_t` arrays (RFL Mii names, `NANDBanner`, and BMG `DAT1`).

## 2. Mechanisms

### M0: emulated Wii address space (host OS layer, Phase 7)

- At startup, `OSInit` on the host reserves one contiguous block of host memory of
  `0x14000000` bytes. MEM1 (24 MiB) is committed at block offset `0`, and MEM2 (64 MiB) at block
  offset `0x10000000`. `OSGetArenaLo/Hi`, `OSGetMEM1ArenaLo/Hi` and `OSGetMEM2ArenaLo/Hi` return
  host pointers into this block. Every EGG heap, and therefore every file buffer
  (`EGG::DvdRipper`, `RPSysResourceManager::LoadFromDVD`) and every heap object, lives inside
  the block.
- `port_vaddr(const void* p)` returns `u32 = 0x80000000 + (p - block)`, and returns 0 for
  `NULL`. `port_hptr(u32 v)` returns `block + (v - 0x80000000)`, and returns `NULL` for 0. Each
  direction is one add or subtract. In debug builds both assert that the address is in range.
- `OSCachedToPhysical` returns `v & 0x3FFFFFFF` and `OSPhysicalToCached` returns
  `v | 0x80000000`. The GX layer decodes **every** address it finds in FIFO or display-list data
  (CP array bases, texture image addresses, TLUT loads, `GXCallDisplayList`) with the same
  functions, so file display lists that the game patched with addresses work unchanged.
- The low 0x3100 bytes of MEM1 hold the OS globals area. It is never a resource, so address 0
  stays free to mean `NULL`.
- **Limit.** A pointer stored through M0 must point into the arena. Every target found in §3 does:
  file data, or heap buffers such as the EFB capture texture in `setCaptureEfb`. Static
  (`.data`/`.bss`) objects are not in the arena. Runtime-only `u32` cookies that may hold such
  pointers use `uintptr_t` instead (§5.2).

### M1: `RES_PTR(T)` slots and pointer cast macros

The headers follow the `be.h` pattern:

- `include/port/resptr.h` (Wii, matching build):

  ```c
  #define RES_PTR(T)                 T*
  #define RES_PTR_RAW(slot)          ((u32)(slot))           /* C++ sites: reinterpret_cast<u32>(slot) */
  #define RES_PTR_GET(slot)          (slot)
  #define RES_PTR_SET(slot, p)       ((slot) = (p))
  #define PORT_PTR_TO_U32(p)         reinterpret_cast<u32>(p) /* C: ((u32)(p)) */
  #define PORT_U32_TO_PTR(T, v)      reinterpret_cast<T>(v)
  #define PORT_STORE_PTR32(u32slot, p) ((u32slot) = PORT_PTR_TO_U32(p))
  ```

  Each macro expands to exactly the expression it replaces, so CodeWarrior compiles the same code.
- `port/host/include/port/resptr.h` (host):
  - `template <class T> class res_ptr { be_u32 v; ... }`, `sizeof == 4`, `alignof == 4`. The class
    is trivially copyable and its default constructor does nothing, so struct copies such as
    `mHeader = *mpHeader` copy the 4 raw bytes.
  - `operator T*()` and `operator->` return `port_hptr(v)`, and `operator[]` works through the
    decoded pointer. `operator=(T*)` stores `port_vaddr(p)` as big-endian. `raw()` returns the
    u32 in host byte order, for both an unrelocated file offset and a relocated address.
    `operator==(NULL)` compares `raw()` with 0.
  - C variant: `typedef struct { be_u32 v; } res_ptr_c;` with the `RES_PTR_*` accessors as
    functions. `RES_PTR(T)` expands to it in C translation units. Only TPL is C, and on the host
    TPL is replaced outright (§3.1).
  - `PORT_PTR_TO_U32(p)` is `port_vaddr(p)` and `PORT_U32_TO_PTR(T, v)` is `(T)port_hptr(v)`.
    Wii code that relocates through u32 arithmetic, such as
    `reinterpret_cast<u32>(pData) + reinterpret_cast<u32>(pData->pEntries)`, stays correct:
    address arithmetic is linear inside the arena, so `vaddr(base) + off == vaddr(base + off)`.
    An overload `PORT_PTR_TO_U32(const res_ptr<T>&)` returns `raw()`, so the same macro covers
    "read the offset that is still stored in a pointer-typed slot".
- Endianness comes with the slot. Before relocation `raw()` swaps the big-endian file offset.
  After relocation the slot holds a big-endian address. Readers of the slot never see the
  difference.
- Hot-path cost: one byte swap, one add and one zero test per dereference (font glyph lookup,
  KCL octree walk, particle textures). This is negligible next to the GX translation.

### M2: `RES_LINK` for list nodes inside file memory

- `ut::Link` is `{void* prev; void* next;}`: 8 bytes on the Wii and 16 bytes on a 64-bit host.
  `ef::Resource` keeps the BREFF and BREFT projects in two `ut::List`s whose nodes are
  `EffectProject::projectlink` and `TextureProject::projectlink`, inside the file at +0x4.
- On the host, declare `projectlink` as `RES_LINK` (`{RES_PTR(void) prev, next;}`, 8 bytes). Back
  it with a `port::ResList` that has the same calls the code uses: `List_Init`, `List_Append`,
  `List_Remove`, `List_GetFirst`, `List_GetLast`, `List_GetNext` and `List_GetPrev`. This is
  about 60 lines in `port/host/src`. `ef_resource.cpp` calls them through macros
  `EF_LIST_*`, which expand to `ut::List_*` on the Wii. (Implemented as `RES_LIST_*` in
  `include/port/reslist.h` / `port/host/include/port/reslist.h`, HOST_BUILD.md §12.)
- In the Wii header, `RES_LINK` is `ut::Link` and `RES_LIST` is `ut::List`.

Alternatives considered and rejected:

- **(a) Self-relative offsets.** These cannot reach pointers into other files or into the heap.
  `relocateCurveTexture` and `RelocateCommand` link across BREFT files, PAT0 can bind textures
  from another BRRES, and `setCaptureEfb` points at a heap buffer.
- **(b) A side table keyed by slot address.** It needs hooks in every reader and cleanup when a
  file is freed, and it hides the data from debuggers.
- **(c) Host-native shadow copies of each structure.** These need a full parser per format and
  diverge from the in-place code.
- **(d) An offline conversion to 8-byte slots.** This changes every later offset, so it is the
  "option 2" that ASSETS.md §2.5 already rejects.
- **(e) A low-4-GiB arena storing raw pointers.** It is not portable: macOS arm64 reserves the low
  4 GiB, and Windows and Linux need `MAP_32BIT` or address hints. M0 gives the same result with
  one add.

### Detecting misses at compile time

- In C++, `reinterpret_cast<u32>(ptr)` is ill-formed on LP64, so every C++ site fails to build
  until it is converted. Use this as the checklist.
- For C, build the host with `-Werror=pointer-to-int-cast -Werror=int-to-pointer-cast`.
- Add `port/host/tests/layout_asserts.cpp`: `static_assert(sizeof(X) == N)` and
  `offsetof(X, f) == 0xNN` for every struct that mirrors file data. The `// at 0xNN` comments in
  the headers already give the expected values, so a script can generate the asserts. Without
  them, a missed `T*` member on 64-bit only shows up as corrupt data at run time.

## 3. Site list (in-place writes into file data)

The columns are:

- **What is written**: the slot (struct, offset) and the value.
- **Readers**: everything that reads the slot afterwards (found with grep).
- **Design**: the change under M0, M1 or M2.
- **Edit size**: lines changed in `src/` and `include/`, every change guarded or macro-expanded so
  the Wii build is unchanged.

### 3.1 TPL (`src/revolution/TPL/TPL.c`)

| | |
|---|---|
| Site | `TPLBind`, TPL.c:14 (`descriptorArray`), :20 (`textureHeader`), :26 (`textureHeader->data`), :36 (`CLUTHeader`), :42 (`CLUTHeader->data`). Also non-pointer flags: `unpacked = TRUE` at :30 and :45 |
| What is written | `TPLPalette.descriptorArray` (+0x8), `TPLDescriptor.textureHeader` (+0x0) and `.CLUTHeader` (+0x4), `TPLHeader.data` (+0x8), `TPLClutHeader.data` (+0x8): each becomes `pal + offset` |
| Readers | `TPLGet` (TPL.c), `lyt::TexMap::Set(TPLPalette*, u32)` (lyt_texMap.cpp:33, **bound test `reinterpret_cast<u32>(descriptorArray) < 0x80000000`**), `TexMap::Set(const TPLDescriptor*)` (:43), `TexMap::SetNoWrap` (:56–76: `data`, `CLUTHeader->data`), `RPSysSaveDataMgr` (RPSysSaveDataMgr.cpp:605–618: `textureHeader->data`) |
| Design | TPL is an SDK module and Phase 7 replaces SDK modules. Shadow `revolution/TPL/TPL.h` in `port/host/include` with `res_ptr` members and `be_*` scalars, and write a host `TPL.c` (about 40 lines) that relocates with M0 addresses. Bound addresses are ≥ `0x80000000`, so the lyt bound test keeps its meaning. With raw host pointers it would fail on 32-bit Linux (heap above or below 2 GiB) and on 64-bit hosts (truncation). |
| Edit size | `lyt_texMap.cpp:33`: one line, `reinterpret_cast<u32>(…)` → `PORT_PTR_TO_U32(…)`. Nothing else in `src/` or `include/`. |

### 3.2 BRFNT (`src/nw4r/ut/ut_ResFont.cpp`)

| | |
|---|---|
| Site | `ResFont::Rebuild` via `ResolveOffset<T>(T*&, void*)` (:7). Calls at :72 (`FontInformation.pGlyph`), :75 (`pWidth`), :79 (`pMap`), :85 (`FontTextureGlyph.sheetImage`), :94 (`FontWidth.pNext`), :102 (`FontCodeMap.pNext`). :120 rewrites the signature to `RFNU` |
| Struct slots | `include/nw4r/ut/ut_ResFontBase.h`: `FontTextureGlyph.sheetImage` +0x14 (:28), `FontWidth.pNext` +0x4 (:34), `FontCodeMap.pNext` +0x8 (:43), `FontInformation.pGlyph/pWidth/pMap` +0x8/+0xC/+0x10 (:53–55) |
| Callers | `RPSysFontManager::LoadResFont` (RPSysFontManager.cpp:77) for the 5 system fonts, and **`lyt::TextBox`** (lyt_textBox.cpp:86) for fonts inside layout archives, including HOME Menu archives. The second load of a shared buffer takes the `RFNU` branch (`SetResource`, :26) and does not rebuild. That stays correct, because the slots keep holding addresses. |
| Readers | `ResFontBase` only: `ut_ResFontBase.cpp:33–53` (`pGlyph->…`), :105–106 (the `pMap` chain), :166–167 (the `pWidth` chain), :183–197 (`sheetImage`). No reader in game code, HBM or lyt. |
| Design | M1: the 6 members become `RES_PTR(...)`. Add a host overload `ResolveOffset(res_ptr<T>&, void*)` that does `slot = base + slot.raw()`. Readers compile unchanged. `mFontInfo` (in the runtime `ResFontBase` object) stays a native `FontInformation*`. |
| Edit size | Header: 6 lines. ut_ResFont.cpp: a 4-line `#if defined(PORT_HOST)` overload, and :9 `reinterpret_cast<s32>(rpPtr)` → `RES_PTR_RAW`. About 12 lines. |

### 3.3 BREFF and BREFT (`src/nw4r/ef/ef_resource.cpp`, `include/nw4r/ef/*.h`)

This is the largest group. All of it is on the Find Mii path: `basic_common`,
`nw4r_defcursor_pack`, `pack_sequence`, and the `RPWlyScene` effects.

| # | Site | What is written | Readers | Design and edit size |
|---|---|---|---|---|
| ef-1 | `Resource::Add` :255–256, `AddTexture` :385–386 | `memset` and `ut::List_Append` write `ut::Link` (prev, next) into `EffectProject.projectlink` / `TextureProject.projectlink` (+0x4, ef_resource.h:18,26), **and into the neighbouring project's link in another file** | `_FindEmitter` (:283–292), `_FindTexture` (:320–329), `_GetEmitterIndexOf` (:356–369), `RemoveEffectProject` / `RemoveTextureProject` (:530–576), `Initialize` (:19–20, `offsetof`) | M2: `RES_LINK projectlink` and `RES_LIST mBREFFList, mBREFTList` in `ef::Resource`. About 14 `ut::List_*` calls → `EF_LIST_*`. `sizeof(ut::Link)` → `sizeof(RES_LINK)`. About 20 lines plus the host helper. |
| ef-2 | `breffRelocate` :78 | `*reinterpret_cast<char**>(pResource) = pName`: `EmitterResource.name` +0x0 (ef_res_emitter.h:265) points into the BREFF name table | `EmitterResource::GetName()` (ef_res_emitter.h:282). **No callers** in `src/`. | M1: `RES_PTR(char) name`. :78 becomes `pResource->name = pName` under `PORT_HOST`, or keep the cast with `RES_PTR(char)*`. 2 lines. The slot must stay 4 bytes because `sizeof(EmitterResource)` (8) locates the descriptor (`GetEmitterDesc`, `SkipEmitterDesc`). |
| ef-3 | `EmitterResource::LocateTracks` (ef_res_emitter.h:360–383), called from `breffRelocate` :80. **Not in ASSETS.md** | `pPtclPtrTbl[i] = reinterpret_cast<u32>(pTail)` (:375) and `pEmitPtrTbl[i]` (:380): the per-track **pointer table** that follows the particle descriptor (4-byte entries, followed by a 4-byte size table) becomes absolute addresses of each track | `GetPtclTrack` / `GetEmitTrack` (header :343–350) via `GetPtclTrackTbl` / `GetEmitTrackTbl` (`u8**`, :327–340). Callers: ef_resource.cpp:500, ef_res_emitter_ac.cpp:7, ef_particlemanager.cpp:261, ef_emitter.cpp:701, RPSysEffectBase.cpp:662,666 (and 681,684 through `ResEmitter::GetPtclTrack`) | M1: the table type `u8**` → `RES_PTR(u8)*` in the two `Get*TrackTbl` functions. :375 and :380 → `PORT_STORE_PTR32(pPtclPtrTbl[i], pTail)`. On the host the u32 tables are `be_u32*`, which also fixes the endianness of the size tables. The table stride must stay 4 (`numPtclTrack * 8 + 4` is hard-coded). About 10 header lines. |
| ef-4 | `breftRelocate` :197–200 | `TextureData.name` +0x0, `.texture` +0x18, `.tlut` +0x1C (ef_res_texture.h:12,25,26): name-table string, `this + 0x20`, `this + 0x20 + dataSize` | `name`: no reader (`breftSearchName` uses the name table). `texture` / `tlut`: `DrawStrategyImpl` ef_drawstrategyimpl.cpp:754,760,775 (`GXInitTlutObj`, `GXInitTexObj[CI]`) | M1: 3 member types. Code unchanged; the assignments go through `operator=`. 3 lines. `sizeof(TextureData)` (0x20) locates the payload, so the slots must stay 4 bytes. |
| ef-5 | `RPSysEffectMgr::setCaptureEfb`, RPSysEffectMgr.cpp:124. **Not in ASSETS.md; game code** | `res.ptr()->texture = pTexture`: the EFB capture buffer (heap) goes into the `dummy_tex` `TextureData.texture` slot of the BREFT file | same as ef-4 (draw strategy) | Covered by ef-4: `operator=` encodes a heap address, which is in the arena. 0 lines. |
| ef-6 | `RelocateCommand` :490 | `ppWork[i] = _FindTexture(...)`: `ParticleParameterDesc.mTexture[3]` +0x68 (ef_particle.h:28) gets `TextureData*` **into another file** (any loaded BREFT) | `Particle::Initialize` copies them into the runtime `ParticleParameter.mTexture` (ef_particle.cpp:47, native pointers; fine), then the draw strategy reads them (ef_drawstrategyimpl.cpp:717–721) | M1: `RES_PTR(TextureData) mTexture[TEX_LAYER_MAX]` in `ParticleParameterDesc` only (the runtime `ParticleParameter` stays native). :483 `TextureData** ppWork` → `RES_PTR(TextureData)* ppWork`. 2 lines. Offsets 0x74–0x88 must not move, because `textureNames` at +0x88 is parsed in place. |
| ef-7 | `relocateCurveTexture` :420, `relocateCurveChild` :459 | `AnimCurveNameTable::Name::work` (u32, ef_res_animcurve.h:139) = `reinterpret_cast<u32>(TextureData* or EmitterResource*)`, cross-file | Texture curves: ef_animcurve.cpp:1645 reads the same table as `AnimCurveTable<TextureData*>` and :1729 `*ppTexture = pNameTbl->datas[name]`. **ASSETS.md misses this second view.** Child curves: ef_animcurve.cpp:1773 `reinterpret_cast<EmitterResource*>(…work)`. Also the error tests at :423 and :462 (`work == 0`) | `u32 work` → `be_u32 work`. :420 and :459 `reinterpret_cast<u32>` → `PORT_PTR_TO_U32`. :1645–1646 `AnimCurveTable<TextureData*>` → `AnimCurveTable<RES_PTR(TextureData)>`. :1773 → `PORT_U32_TO_PTR(EmitterResource*, …)`. 5 lines. |

What ef depends on beyond pointers: the emitter, particle and curve structs are read natively.
They need `be_*` field types (Phase 6 assets work). That is the larger part of the ef port. The
pointer part is about 45 lines.

Lifetime note: `RPSysEffectCreator` (RPSysEffectCreator.cpp:59–89) calls `RemoveEffectProject` /
`RemoveTextureProject` and then `RelocateCommand` again when a scene unloads. Cross-file slots
(ef-6, ef-7) are re-resolved then. M0 addresses go stale in the same way Wii pointers do, so
the behaviour is identical.

### 3.4 g3d (BRRES)

| # | Site | What is written | Readers | Design and edit size |
|---|---|---|---|---|
| g3d-1 | `ResTexPlttInfo::BindTex_` g3d_resmat.cpp:1506, `BindPltt_` :1540, `Release` :1587–1588 | `ResTexPlttInfoData.pTexData` +0x8, `.pPlttData` +0xC (g3d_resmat.h:456–457) inside each MDL0 material: `ResTexData*` / `ResPlttData*` (same file, or another BRRES via `ResFile::Bind`) | **Only as "is bound" flags**: `ResTexPlttInfo::Bind` :1557 and :1567 (`== NULL`), `Release` (stores NULL). No other reader (grep of `pTexData` / `pPlttData` outside g3d_resmat). | M1: `RES_PTR(ResTexData)` / `RES_PTR(ResPlttData)`. Code unchanged. 2 header lines. |
| g3d-2 | `BindTex_` :1520/:1528 `GXInitTexObj[CI]`, `BindPltt_` :1545 `GXInitTlutObj`, into `ResTexObjData.texObj[8]` (+0x4, 0x20 each) and `ResTlutObjData.tlutObj[]` (g3d_resmat.h:158,185), inside the file's material | GX objects that hold the image or palette address | `ResTexObj::CopyTo` and `ResTlutObj::CopyTo` (g3d_resmat.cpp:155,217) into the runtime `MatBuffer`, then `GXLoadTexObj` / `GXLoadTlut` | **GX layer contract (Phase 7)**: public `GXTexObj` stays `u32[8]` (0x20) and `GXTlutObj` `u32[3]` (0xC). The host GX keeps every address in them as an M0 address (u32). The decomp's `GXTexObjImpl` has `void* userData` at +0x10 (GXInternal.h:65); the host GX must not use that layout (store a u32 handle). 0 lines in `src/`. |
| g3d-3 | `ResShp::GXSetArray` g3d_resshp.cpp:146–154, from `ResShp::Init` :195–228. **Listed as "GX-consumed" in ASSETS.md, but it is a write into file memory** | `ResWriteCPCmd` writes `CP ARRAYBASE = reinterpret_cast<u32>(OSCachedToPhysical(pBase))` (BE bytes) into the shape's pre-primitive display list in the file | The GX layer, when the DL is called (`GXCallDisplayList` from the draw code) | M0: `OSCachedToPhysical` returns the physical address of the M0 address. `GXSetArray` decodes it with `OSPhysicalToCached` and `port_hptr`. On the host `OSCachedToPhysical` returns `u32`, so :151 → `PORT_PHYS(pBase)` (Wii: `reinterpret_cast<u32>(OSCachedToPhysical(pBase))`). 1 line. |
| g3d-4 | `ResAnmTexPat::Bind` g3d_resanmtexpat.cpp:141,157; `Release` :175,179. **Not on the Find Mii path** (PAT0 is used by Shooting Range and others) | The PAT0 `toResTexArray` / `toResPlttArray` arrays (g3d_resanmtexpat.h:88–89) hold one `ResTex` / `ResPltt` per entry. `ResCommon` wraps one pointer, so each element is 4 bytes on the Wii and 8 on a 64-bit host | `GetAnmResult` :67–68, :96, :105; `Bind` :126–157 (validity test); `Release` | M1: on the host, read the arrays as `RES_PTR(ResTexData)` / `RES_PTR(ResPlttData)` through a typedef `PAT0_TEX_SLOT` (Wii: `ResTex`). Construction `ResTex tex(slot)` works through the implicit conversion. Stores: `= found` → `= found.ptr()` and `= ResTex(NULL)` → `= NULL` under `PORT_HOST`. About 10 lines. |

Implemented (HOST_BUILD.md §4 and §11): g3d-1 as designed; g3d-2 as the contract above, with
host `GXInitTexObj`/`GXInitTlutObj` storing the Wii words big-endian; g3d-3 needed no edit,
because the host `OSCachedToPhysical` returns a `u32` and `reinterpret_cast<u32>` of a `u32` is
the identity; g3d-4 with `PAT0_TEX_SLOT`/`PAT0_PLTT_SLOT` (a `res_ptr` that assigns from
`ResTex`/`ResPltt`), because `ResFile::Bind` runs `ResAnmTexPat::Bind` for every file.

Not a pointer issue, listed for completeness: `ResLightSet::Bind` (g3d_reslightset.cpp:17)
writes u16 light ids, and the material display-list patch helpers write big-endian bytes.
Neither needs a pointer change.

### 3.5 PMP (`src/Pack/RPParty/RPPartyCommon/RPResFile_3.cpp`, Cow Race only)

| | |
|---|---|
| Site | `RPResFile::Relocate`: :44 `pEntries`, :53 `pPaths`, :62 `pPoints` (offset → `pData + off`), :79 `RPResEntry.pPath` (index → `&pPaths[idx]`, `0xFFFFFFFF` → NULL at :77), :90 `RPPathRes.pPoints` (index → `&pPoints[idx]`), :96 flag bit 0 |
| Slots | `RPResData` +0x40/+0x44/+0x48 (RPResFile_2.h), `RPResEntry.pPath` +0x44 (stride 0x58) and `RPPathRes.pPoints` +0x18 (stride 0x20) (RPPathFollower.h:37,50) |
| Readers | `RPResFile::FindFilteredEntry` (:17–33), RPResFile_2.cpp:4 (`&pData->pEntries[i]`), `RP_getPathPoint` (:5–7), `RPPathFollower_2.cpp:54–56` (`pPath != NULL`, passed on as a pointer), `RPPathFollower.cpp:60` `GetPointPos` |
| Design | M1: 5 members → `RES_PTR`. In `Relocate`: `reinterpret_cast<u32>(pData)` and `reinterpret_cast<u32>(pData->pX)` → `PORT_PTR_TO_U32` (6 casts, :45, :54, :63, :75, :91). Readers compile unchanged. The array strides stay 0x58 and 0x20, which a 64-bit build with `T*` members would break. |
| Edit size | About 12 lines. |

### 3.6 KCL (`src/Pack/RPParty/RPPartyCommon/RPKcl.cpp`)

| | |
|---|---|
| Site | `RPKcl::SetResource` :18–40. `mHeader = *mpHeader` copies 0x3C bytes, then :33–39 store `file + offset` into the **copy** (`RPKclHeader.pPositions` / `pNormals` / `pPrisms` / `pBlocks`, +0x0–0xC, RPKcl.h:29–32). The file itself is not modified. |
| Readers | `RPKcl::CheckPoint` (:42ff), `SearchSphereOctree`, `CheckSphereTri` (RPKcl.cpp:116,233), `RPCollisionModel::CheckSphere` |
| Design | M1 on the 4 members. Then the struct copy copies the raw offsets and the assignments encode addresses, without splitting the struct. :34–39 `reinterpret_cast<u32>(pFile->pX)` → `RES_PTR_RAW`. The 0x38-byte header quirk (ASSETS.md §5.2) keeps exactly the Wii behaviour. |
| Edit size | 4 header lines and 4 code lines. |

### 3.7 Checked, no in-place pointer writes

- **nw4r snd.** File readers hold `const` pointers. `DataRef` (8 bytes) is resolved into
  temporaries (snd_SoundArchiveFile.cpp:578–580 writes a local copy). `REFTYPE_ADDRESS`
  (snd_Util.cpp:104) turns an absolute u32 into a pointer. The disc data never uses it, so on
  the host it should assert. `ChannelParam.dataAddr` and similar members are runtime structs.
- **lyt.** `ConvertOffsToPtr` is only used for reads (lyt_layout, picture, textBox, window,
  group, animation). The `res::` structs have no pointer members. `ResBlockSet`
  (lyt_resources.h:107) is a runtime struct. The two lyt paths into pointer-writing code are TPL
  (§3.1) and ResFont (§3.2).
- **EGG.** `MsgRes`, `GXUtility::getTexObj` (`ResTIMG`) and the `IBinary` loaders read or copy.
  `RPSysKokeshiManager::CreateTexture(void*)` (RPSysKokeshiManager.cpp:417) casts a `ResTIMG*`
  to `RPGrpTexture*`, but its only caller ignores the result (RPSysKokeshiIcon.cpp:741).
- **RFL.** `parseOnmemoryRes_` and `getCachedFile_` (RFL_NANDLoader.c:30,261) read offsets and
  `memcpy` parts into caller buffers. `RFLiCharModelRes` pointers (RFLi_Model.h:123–134) are
  runtime.
- **ARC.** Already converted with `be_u32` (arc.c). The handle stores native pointers.

## 4. Structs that mirror file layout and contain pointers (64-bit layout check)

Every one of these must have only 4-byte members on the host, so each gets a `static_assert`:

| Struct | Pointer members | Size |
|---|---|---|
| `TPLPalette`, `TPLDescriptor`, `TPLHeader`, `TPLClutHeader` (TPL.h) | `descriptorArray`; both members of `TPLDescriptor`; `data` (×2) | 0xC, 0x8, 0x24, 0xC |
| `FontInformation`, `FontTextureGlyph`, `FontWidth`, `FontCodeMap` (ut_ResFontBase.h) | 3, 1, 1, 1 | as in the file |
| `EffectProject`, `TextureProject` (ef_resource.h) | `ut::Link` | 0x14 + name |
| `EmitterResource` (ef_res_emitter.h:263) | `name` | 0x8 (`sizeof` is used for layout) |
| `TextureData` (ef_res_texture.h) | `name`, `texture`, `tlut` | 0x20 (`sizeof` is used for layout) |
| `ParticleParameterDesc` (ef_particle.h:20) | `mTexture[3]` | `textureNames` at 0x88 |
| ef track tables, `AnimCurveTable<TextureData*>` (ef_animcurve.cpp:112) | `u8*` elements, `TextureData*` elements | stride 4 |
| `ResTexPlttInfoData` (g3d_resmat.h:453) | `pTexData`, `pPlttData` | 0x34 |
| PAT0 `ResTex[]` / `ResPltt[]` | `ResCommon::mpData` | stride 4 |
| `ResTexObjData`, `ResTlutObjData` | `GXTexObj` / `GXTlutObj` (opaque; host GX must keep 0x20 / 0xC) | as in the file |
| `RPResData`, `RPResEntry`, `RPPathRes` | 3, 1, 1 | ≥0x4C, 0x58, 0x20 |
| `RPKclHeader` | 4 | 0x3C |
| `EGG::MsgRes` DAT1 block | `wchar_t mContent[]` (eggMsgRes.h:58) | 2-byte elements (§6) |
| `RFLiCharData` / `RFLiHiddenCharData` / `RFLiStoreData` (RFLi_Types.h:246–359), `RFL_DataUtility.h:16–17` | `wchar_t` names (not pointers, but they change size) | 0x4A per Mii, etc. |
| `NANDBanner` (nand.h:150–151) | `wchar_t title[32]`, `subtitle[32]` | Wii layout (0x20 title at +0x20) |

## 5. Other pointer-size hazards in the same code

### 5.1 Hard-coded byte offsets into runtime objects (64-bit only)

These objects contain pointers or vtables, so their layout changes on a 64-bit host. These sites
are not about file data. In each case, use the named member under `PORT_HOST`:

- `RPPlayerNameTag.cpp:144`: `reinterpret_cast<u32>(pHeads[mPlayer]) + 0x68` →
  `RPSysAvatar::mAdditionalInfo` (the Mii name). On the Find Mii path: name tags.
- `RPSysTagProcessor.cpp:96`: `RPSysSystemWinMgr::instance() + idx + 0xD4` → `mSeType[idx]`
  (already marked NONMATCHING-DEBT). On the path: every message window.
- `RPKokeshiMgr_3.cpp:39`: `(u8*)pScreen + 0x34` as `u16` flags. Name the member.
- `RPPolyline.cpp:101`: `offset + reinterpret_cast<u32>(points)` pointer arithmetic →
  `reinterpret_cast<const u8*>(points) + offset`. Wii-identical only under `PORT_HOST`.

### 5.2 Runtime u32 ↔ pointer cookies (64-bit; not file data, so not M0)

Use `uintptr_t` (or `intptr_t`) on the host:

- snd channel callback data: `snd_SeqTrack.cpp:445` and `snd_WsdPlayer.cpp:179` pass
  `reinterpret_cast<u32>(this)`; the callback type is in snd_Channel.
- snd thread and callback messages: `snd_AxVoiceManager.cpp:136`, `snd_SoundThread.cpp:94,120`,
  `snd_AxManager.cpp:356`.
- `snd_AxVoice.cpp:177–221` does AX address math on sample pointers. Phase 7's mixer replaces AX
  voices; keep the addresses as M0 physical addresses if the replacement keeps the API.
- `eggProcessMeter.cpp:250`. `eggDvdFile.cpp:135` is already handled.
- `ef_effect.h` `Effect::Modifier_SetSimpleLightAmbient/SetScale/SetRotate` pass `&x` as a
  `u32` `ForEachParam`. Done in step 6: `ForEachParam` is `uintptr_t` on the host.
- `RFL_MiddleDatabase.c:137,244,311,418`: `(u32)db` callback arguments.
- HBM alarm user data: `HBMRemoteSpk.cpp:166,198`, `HBMController.cpp:59`,
  `HBMBase.cpp:1426,1460`.
- `RPSysSystem.cpp:70–80`: arena rounding through u32, and `*(u32*)0x80000028` (memory size).
  Use `ROUND_UP_PTR` and an OS-layer query.
- `WPAD.c:718` `OSSwitchFiberEx`: Phase 7 replaces WPAD.

### 5.3 Other `sizeof(void*)` and layout assumptions

- `lyt_animation.cpp:349` allocates `fileNum * sizeof(void*)` for a runtime array. This is
  correct on any host.
- `GXTexObjImpl.userData` is `void*` inside a 0x20 object. The host GX must not use it (§3.4
  g3d-2).
- The `ut::Link` / `ut::List` offset-based lists elsewhere are runtime objects. They are fine
  because they never sit in file data, except ef-1.

## 6. `wchar_t` and UTF-16 text

**Where text is typed `wchar_t`** (about 330 lines, outside MSL):

- Library code:
  - `EGG::MsgRes::getMsg` returns `const wchar_t*` into DAT1 (eggMsgRes.cpp:123–131).
    `analyzeTag` (:102–118) reads `*(u8*)tag` and `*(u32*)tag`, and `tag + 2` must advance
    4 bytes.
  - `ut::TextWriterBase<wchar_t>`, `TagProcessorBase<wchar_t>`, `CharStrmReader::Set`.
  - `lyt::TextBox` (`mTextBuf`; `textBufBytes / sizeof(wchar_t)` at lyt_textBox.cpp:43,54,210,219;
    `ConvertOffsToPtr<wchar_t>` at :52; `memcpy … sizeof(wchar_t)` at :256).
  - HBM `mpText` (HBMBase.cpp:522).
  - `NANDInitBanner` and `NANDBanner`.
  - RFL Mii names (`RFL_Database.c:313–420` and `RFL_DataUtility.c:291–295`, using
    `sizeof(wchar_t)`).
  - `OSUTF16to32`.
  - `NWC24SetMsgAltName`.
- Game code: 42 files and 109 lines in `src/Pack`, plus 75 lines in `include/Pack`. There are 45
  `L"…"` literals.
- Wide libc calls: `swprintf` (at least 14 sites), `vswprintf` (RPDebugLog), `wcslen` (8), `wcscpy` (4),
  `wcsncpy` (5, NAND and RFL) and `wcscmp` (2, NAND).

**Recommendation: a 16-bit `wchar_t`.**

- Compile `src/` with `-fshort-wchar` on GCC and Clang. MSVC already has a 16-bit `wchar_t`.
- Every `sizeof(wchar_t)`, every `L"…"` literal (now UTF-16), and every struct with an embedded
  name array (RFL 0x4A records, `NANDBanner`, BMG `DAT1`) then has the Wii layout with **no
  source edits**.
- The cost is that glibc and libc++ wide functions assume a 4-byte `wchar_t`. So:
  1. Build the decomp's MSL_C `wstring.c`, `wmem.c` and `wprintf.c` into the host as
     `port_wcslen`, `port_swprintf` and the others.
  2. In a host-forced header (`-include port/wide.h`), `#define wcslen port_wcslen` and the same
     for the other functions used (`wcscpy`, `wcsncpy`, `wcscmp`, `swprintf`, `vswprintf`,
     `wmemcpy`).
  3. Never pass `wchar_t` across the boundary into `port/host/src` code that is built without
     `-fshort-wchar` (the platform layer converts to UTF-8 at its edge, for example window
     titles and save-file names).
  4. On Arm, the linker warns about mixed `wchar_t` size objects. That is expected; keep all
     `src/` objects on one setting.
- Edits: 0 lines in `src/` and `include/`. There is one host header and a build flag.

**Fallback (`char16_t` everywhere)**, if a toolchain lacks `-fshort-wchar` or a dependency
needs a 4-byte `wchar_t`:

- Add a `wchar16` typedef (Wii: `wchar_t`, host: `char16_t`) and a `W16("…")` literal macro
  (Wii: `L"…"`, host: `u"…"`; CodeWarrior has no `u""`).
- Touch every line counted above, about 350 lines in about 75 files.
- The `TextWriterBase<wchar_t>` and `TagProcessorBase<wchar_t>` explicit instantiations change.

**Endianness is separate.** In either case, BMG DAT1 and BRLYT `txt1` strings are UTF-16BE.
Swap them at load, tag-aware, as described in ASSETS.md §2.7. `analyzeTag` reads the tag's
length byte and `*(u32*)tag & 0xFFFFFF` (group and id) from the swapped buffer, so the load-time
swap must leave tag bytes in an order that still decodes. Simplest: keep tag payloads BE and
make `analyzeTag` use `port_load_be32` under `PORT_HOST` (2 lines).

## 7. Implementation order (Find Mii first-playable path, ASSETS.md §4)

Each step is small and can be tested on its own. Steps marked † apply on both PC builds; the
rest only matter on 64-bit but cost little with M1 already in place.

| Step | Work | Unlocks (ASSETS §4 step) | Size |
|---|---|---|---|
| 0 † | M0 in the host OS layer (arena block, `port_vaddr` / `port_hptr`, `OSCachedToPhysical`); `include/port/resptr.h` and `port/host/include/port/resptr.h`; `-Werror=pointer-to-int-cast`; `layout_asserts.cpp` | everything | about 150 lines host, 0 in `src/` |
| 1 † | `wchar_t`: `-fshort-wchar` plus `port/wide.h` and the MSL_C wide functions; `analyzeTag` BE read | BMG (step 2) | about 40 lines host, 2 in `src/` |
| 2 † | ResFont §3.2 | BRFNT (step 2) | about 12 |
| 3 † | TPL §3.1: shadow header, host TPL.c, `lyt_texMap.cpp:33` | layouts (step 3) | about 50 host, 1 in `src/` |
| 4 | §5.1 `RPSysTagProcessor.cpp:96`, `RPPlayerNameTag.cpp:144` | message windows, name tags | 4 |
| 5 † | g3d-1, g3d-3; GX contract g3d-2 agreed with the Phase 7 GX owner (ASSETS.md §5.4) | BRRES (step 4) | 3 in `src/` |
| 6 † | ef-1 to ef-7 (M2 helper) | BREFF / BREFT (step 6) | about 45 in `src/`, 60 host |
| 7 | §5.2 snd cookies (`uintptr_t` callback data) | sound (step 8) | about 15 |
| 8 | after first-playable: g3d-4 PAT0, PMP §3.5, KCL §3.6, `RPPolyline`, HBM and RFL cookies, `RPKokeshiMgr_3` | other games | about 40 |

Find Mii itself needs no PMP, KCL or PAT0, and its tables (`RPWlyLevelTable`) are plain 32-bit
words without pointers. `RFL_Res.dat` (step 5 in ASSETS.md) has no pointer writes.

Total in `src/` and `include/` for the whole game: about 140 lines, all macro-expanded or under
`PORT_HOST`. Matching-build check after each step: `ninja` plus the byte-identical DOL check
(note `verify.py` can print OK for a stale DOL; check the exit code).

## 8. Open questions (additions to ASSETS.md §5)

1. **Arena size.** M0 assumes the game never allocates file buffers or effect-capture buffers
   outside the MEM1 and MEM2 arenas. That holds for EGG heaps. Confirm with the Phase 7 OS owner
   that host `OSAlloc` and `MEMAllocFromExpHeap` are carved from the same block. Also confirm
   whether a larger MEM2 is wanted for debugging; the encoding allows up to `0x0FFFFFFF` per
   arena.
2. **The GX layer stores M0 addresses.** Decided (2026-10-30): GX objects inside model files
   keep the Wii sizes and layouts and hold big-endian emulated addresses; display lists and
   vertex arrays stay GX-native; the Phase 7 GX layer decodes them (HOST_BUILD.md §11). This is
   the same question as ASSETS.md §5.4. The proposal was that host `GXTexObj` and `GXTlutObj` keep the Wii sizes and hold M0 addresses,
   and that FIFO and display-list address decode goes through `OSPhysicalToCached` and
   `port_hptr`.
3. **Unused slot writes.** `EmitterResource.name` and `TextureData.name` have no readers.
   Keeping the writes costs nothing with M1, but they could be dropped on the host if a site
   turns awkward.
4. **`-fshort-wchar` and the C++ standard library.** Make sure no `src/` file uses
   `std::wstring` or `<cwchar>` functions other than the remapped ones. The current grep finds
   none.
