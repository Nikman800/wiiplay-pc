# Wii Play → PC: reverse-engineering and port plan

Goal: a native PC build of Wii Play that runs from the player's own disc image, built from
reverse-engineered source. This plan takes the project from today's state (named, partly typed
pseudocode) to that build, in phases we work through one step at a time.

## Where we are (2026-10-29)

| Area | State |
|---|---|
| Disc extraction, DOL → ELF, Ghidra/REA setup | Done |
| Game code (6,755 functions) | Method-level names; Phase 3 review: 3,298 high / 3,363 medium / 13 low / 461 keep (`dtk/methods.tsv`) |
| Classes | 398 recovered by vtable (`dtk/classes.tsv`) |
| Struct layouts | Phase 3: C++ headers for every game class (203 new, 42 vendored headers corrected); 530 class sizes verified with the original compiler |
| EGG engine | 380 functions named by source file |
| Other libraries (nw4r, RVL SDK, RFL, HBM, MSL…) | Phase 2: 6,144 / 6,307 library functions named from reference decomps; 644 source-file units (code + ~900 data ranges); 884 shared RP framework functions carry real Wii Sports names |
| Data symbols | 968 game globals named and typed (`dtk/types/*_globals.tsv`); library data named from references |
| Compilable source | Phase 1: `ninja` rebuilds a byte-identical `main.dol`; first matched unit `src/Hky/FieldWallIter.cpp`; compiler GC/3.0a5.2 |
| Reproducibility (Phase 0) | Done: fresh clone + disc → identical outputs (`tools/reproduce.sh`) |
| Typed pseudocode | Phase 3: all 13,099 functions exported with types (`decomp_typed/all/`); 7,661 methods with typed `this`, 843 typed globals |
| Matched game code (Phase 4) | **Done (2026-10-29).** 487 / 491 game units linked from source and matching; the 4 left (RPSysTagProcessor ProcessRuby, RPKokeshiMgr_3 ctor, RPCPadCursor_2/_3 SetTarget) are functional and marked, matching abandoned by decision after compiler-level analysis (docs/NONMATCHING.md). Natural-C cleanup done the same day (no `#define private public`, ~400 casts replaced, placeholder names converted, remaining debt documented). One game function stays inline asm (RPCowKakasiGroup::OnActivate). |
| Matched libraries (Phase 5) | **Done (2026-10-29).** 636 / 637 library units linked from source and matching: nw4r 188/189, EGG 83/83, RVL SDK 193/193, BTE 68/68 (Petari import; uusb_ppc.c rewritten), RVLFaceLib 15/15, HBM 6/6, MSL 54/54, runtime 8/8, MetroTRK 21/21. The last, nw4r/ef/ef_animcurve.cpp (69 differences; non-matching in every other project too), is functional and marked. 460 units came from the CC0 Rhae decomp; library unit layout follows Rhae (`tools/rhae/`). |
| Assets on a PC host (Phase 6) | **Done (2026-10-30).** Every file the game opens loads through the game's own loaders on a little-endian host, 64- and 32-bit, with every touched unit still matching: archives, fonts, BMG, TPL/BTI, layouts, BRRES models and animations, effects, sound archives and streams, collision, placement files, EGG settings, game tables, RFL_Res.dat and the HOME Menu. Formats are documented in `docs/ASSETS.md`. Not on the disc and still open: Mii records from a Wii's NAND database. |

## Strategy

**Decompile to C/C++ that matches the original binary, then port that source.** Matching means
each function, compiled with the original CodeWarrior compiler, produces identical machine
code. It costs more than writing "equivalent" code, but it gives every function an automatic
correctness check (objdiff), which matters when much of the code is written with AI help. It is
the route the existing decomp-based console ports took.

The port then compiles the same source for PC, with the Wii hardware and SDK replaced by a
platform layer. EGG, nw4r and the game code are mostly portable C++; the SDK is not.

Two pragmatic choices, revisitable later:

- **Build 32-bit first.** Game and library structs, and the resource files fixed up in place into
  pointers, assume 4-byte pointers. A 32-bit PC build avoids rewriting all of that up front;
  64-bit becomes a later, separate task.
- **Fall back to functional (non-matching) code where matching stalls,** marked as such, so a
  few stubborn functions never block the port.

## Phases

Each phase lists its goal, steps, deliverable, and the check that says it is done.
Sizes: S = days, M = weeks, L = months of focused work.

### Phase 0 — Project hygiene (S)

- Put the workspace under git. Keep the disc image, `extracted/`, `main.elf`, `decomp*/` and other
  derived or Nintendo-owned files out of it (`.gitignore`); commit tools, configs, headers, ledgers.
- Pin tool versions (dtk 1.8.4, Ghidra 12.1.4, GameCubeLoader 1.3.1, REA 6.7.0) in the README.
- **Done when:** a fresh clone plus the user's own disc image reproduces everything with documented
  commands.

### Phase 1 — Build infrastructure that reproduces the DOL (M)

- Fix the duplicate-section-name problem so `dtk dol split` re-runs (name the second rodata/sbss
  sections distinctly in the config and symbols).
- Create a proper dtk project (`configure.py` + ninja, decomp-toolkit's standard template), with
  objdiff.
- Identify the CodeWarrior compiler version and flags: compare instruction patterns with known
  compilers, and check what related games' decomp projects (e.g. Wii Sports) used.
- Get the build linking a **byte-identical `main.dol` from split assembly**.
- Verify the compiler choice by matching a handful of small functions (Laser Hockey accessors).
- **Done when:** `ninja` rebuilds a DOL whose SHA-1 equals the original's, and the first C functions
  match in objdiff.

### Phase 2 — Identify and name the libraries (M)

- Split the library regions into translation units: RVL SDK (OS, GX, VI, DVD, NAND, AX, WPAD/KPAD,
  SC, NWC24…), MSL, TRK, nw4r (g3d, lyt, snd, ef, ut, math), EGG, RFL (Mii), HBM.
- Name functions by signature matching against existing community decompilations of the same
  library versions (check licences and versions first), then by strings and asserts for the rest.
- **Done when:** every library function has a name and a TU, and no unnamed `fn_` remain in
  library code.

### Phase 3 — Types and headers (M → L)

- Adopt existing community headers for the SDK, nw4r and EGG where available and compatible;
  write the rest.
- RP framework classes first (`RPSimpleModel`, `Sys_Scene`, Party results, layout and sound
  helpers), because every minigame builds on them.
- Then each minigame, repeating the Laser Hockey process (`field_xref.py` → header → apply in
  Ghidra → check sizes against allocations).
- Name and type global data (singletons, parameter tables, state tables).
- Resolve the low-confidence method names as types make the code readable.
- **Done when:** every game class has a header with verified size, and typed pseudocode exists
  for all game code.
- **Status: done (2026-10-19).** Batches: framework (fw_rp, fw_gfx, fw_ui, fw_party, shared
  sound) and all nine minigames. Ledgers `dtk/types/`; tools `sizeof_probe.py`, `alloc_sizes.py`,
  `gen_ghidra_types.py`, `gen_this_map.py`, `ApplyTypes.java`, `apply_globals.py`,
  `apply_method_renames.py`. Typed pseudocode: raw `param_1 + offset` accesses 42,330 → 4,701; 78,760 named field accesses.
  Open: 204 global names wait for data splits (their address sits inside a larger dtk symbol);
  `RPSysKokeshiIterater` and `RPSysPairingMgr` sizes unverifiable (no allocation); member names
  marked [L] remain hypotheses for Phase 4 to confirm.

### Phase 4 — Decompile the game code (L)

**Status: done 2026-10-29** (matched, or functional and marked; see the table above).

- Order: shared framework (Sys, Gfx, Snd, Lyt, Menu, Party, Mii, RP) → minigames one at a time,
  Laser Hockey first as the pilot.
- Work per translation unit; parallel agents can each take one TU, with objdiff as the referee.
- Track progress with objdiff's report (% matched code and data).
- **Done when:** all game TUs are matched, or functional and marked where matching was abandoned.
- **Status (2026-10-26): done apart from 14 parked units (480/494 linked).** Units linked from
  source (`config/RHAE01/matching.txt`); the full DOL still matches byte for byte:

  | Part | Linked |
  |---|---|
  | All nine minigames | 277/277 (RPTnkMine_2 and RPCowFieldObj are accepted FAKEMATCHes) |
  | RPAudio, RPSystem, RPUtility, main | 6/6, 22/22, 2/2, 1/1 |
  | RPKernel | 47/48 (RPSysTagProcessor) |
  | RPGraphics | 25/27 (RPGrpModelMaterial; RPGrpRenderer, which absorbs RPScnProcDrawHook(_2) once its Draw matches) |
  | Party common (RPPartyCommon) | 100/111 |

  The 14 unlinked units each miss on one function's register allocation or scheduling; three
  Claude batches and glm found no further shape, so the user parked them (2026-10-26). They have
  functionally equivalent source with NONMATCHING notes, which satisfies this phase's "done"
  condition; an automatic permuter could still try them later.
  Unit counts changed as matching revealed the original file boundaries: some ranges were split
  (RPBilResult, RPCowField, RPTnkField) and Charge!'s game file absorbed ten weak-copy units.
  Matching lessons accumulate in `tools/MATCHING.md`; status and open items are in
  `docs/PHASE4_PENDING.md`. Workers: Claude agents, Codex (gpt-6.1-sol, xhigh) and GLM-5.3 workers
  via a worker dispatcher (not part of the public release), with the coordinator verifying every link.

### Phase 5 — Decompile the remaining libraries (L)

**Status: done 2026-10-29** (EGG and the SDK match; nw4r matches except ef_animcurve, functional and marked).

- Reuse community decompilations for whatever they already cover; decompile the remainder the
  same way as Phase 4.
- Priority for the port: EGG and nw4r (needed as source); SDK pieces only to understand behaviour,
  since the port replaces them.
- **Done when:** EGG and nw4r compile from source and match; SDK behaviour is documented enough to
  reimplement.

### Phase 6 — Assets and file formats (M, can overlap 3–5)

- Document every format the runtime parses: BRRES (models/animations), BRLYT/BRLAN (layouts),
  BREFF/BREFT (effects), BRSAR/BRSTM/BRWSD (sound), BMG (text), TPL (textures), KCL (collision),
  U8 archives, Yaz0, the game's own `param/*.bin` and level tables.
- Write a loader-side endian/pointer conversion for each (or an offline converter run on the
  user's disc).
- **Done when:** every file the game opens loads correctly on a little-endian host.

### Phase 7 — PC platform layer (L)

Replace the hardware and SDK with PC equivalents:

| Wii subsystem | PC replacement |
|---|---|
| OS (threads, heaps, time, alarms) | Host threads and allocator |
| DVD | Reads from the user's extracted disc |
| NAND, banner, save | Save directory on disk |
| SC (system settings) | Config file: language, aspect, sensor-bar position |
| VI | Window, swap, 60 Hz pacing |
| **GX** (largest item) | Translation to a modern API (Vulkan / D3D12 / Metal via a portable layer); Dolphin's GX behaviour is the reference |
| AX / DSP | Software mixer with DSP-ADPCM decode |
| **WPAD / KPAD** (critical for Wii Play) | Mouse pointer, gamepad gyro/tilt, or real Wii Remotes over Bluetooth; rumble, speaker |
| RFL (Mii) | Mii data from a default set or imported from the user's Dolphin NAND |
| HBM, NWC24, ESP | Stub or replace (pause menu, no Message Board) |
| Paired-single math (PSMTX/PSVEC) | Portable C / SIMD |

- **Done when:** the platform layer compiles and passes unit tests against recorded Dolphin
  behaviour (for example GX state and audio output for known scenes).

### Phase 8 — Bring-up (M → L)

- Boot milestones: health screen → title → menus → each minigame → results → save.
- Compare against Dolphin: screenshots of the same scenes, recorded input replays.
- Fix endianness, pointer-size and timing bugs as they surface.
- **Done when:** all nine minigames are playable start to finish with saves.

### Phase 9 — Port polish (M)

- Controls design and remapping (pointer games, tilt games, two players), resolution and
  widescreen, frame pacing, settings UI, save migration from Dolphin.
- Optional: 64-bit build.

### Phase 10 — Release and legal (S)

- Distribute only our code; the player supplies their own disc image; no Nintendo assets or
  binaries in the repository or releases.
- Publish a fresh squashed snapshot without `tools/workers/`; `tools/release_check.sh <snapshot dir>`
  must report clean (no institution or service names, no personal paths, no disc-derived files).

## Decisions to make

| Decision | Recommendation | When |
|---|---|---|
| Matching vs functional decomp | Matching, with marked functional fallbacks | Before Phase 1 |
| Reuse community library decomps | Yes, after checking licence and version | Phase 2 |
| Graphics API | Aurora (MIT; GX on WebGPU via Dawn, which targets Vulkan/D3D12/Metal) as the backend behind the port's own GX entry points; decided 2026-10-30 | Phase 7 |
| 32- vs 64-bit | 64-bit for the game (Dawn has no 32-bit builds); the host loaders and tests also build and pass as 32-bit | Phase 7 |
| Input model | Mouse + gamepad first, real Wii Remotes later | Phase 7 |
| Open-source licence for our code | Pick before publishing anything | Phase 10 |

## Risks

- **Scale.** ~12,500 functions in total; library reuse is what keeps this feasible.
- **Compiler identification.** Matching depends on finding the exact CodeWarrior build and flags.
- **GX fidelity.** The graphics layer is the largest single engineering task.
- **Input feel.** Wii Play is built around the remote; PC controls need real design work.
- **Low-confidence names and types** can mislead; matching decomp corrects them over time.

## Next steps

1. Phase 5 import (done 2026-10-26, 460 units): every Rhae-matching unit we imported links. Not
   imported: g3d_scnmdl1mat1shp, g3d_resanmfog and the units Rhae names differently
   (runtime/__init_cpp_exceptions.cpp uses dtk section renames our dtk lacks).
2. Phase 5 step 2 (in progress since 2026-10-27, 94 units so far): the library units Rhae leaves
   unmatched, by Claude agents and glm workers. Open: ef_animcurve, ef_particlemanager (Calc's
   field code), ef_drawbillboardstrategy/ef_drawsmoothstripestrategy/ef_drawdirectionalstrategy,
   lyt_window/lyt_material, eggDrawPathDOF/ShadowVolume, eggLightTexture/Manager/Object,
   eggShadowTexture, eggPostEffectMaskDOF, eggCpuTexture, eggQuat (conjugate), eggPlane,
   eggController, eggScreenEffectBase, eggFrustum, NWC24 MBoxCtrl/MsgCommit, WPAD, SIBios, cnt
   (blocked: table placement), mem_TRK, MSL scanf/strtold/ansi_fp, MSL_C printf/wprintf, MetroTRK
   targsupp (needs force_active) and 10 more MetroTRK units, runtime NMWException.
3. Start the SDK behaviour notes the port needs first: RFL (Mii database), NAND saves, WiiConnect24
   message board, HBM, WPAD input.
4. Optional: try an automatic permuter on the parked Phase 4 holdouts (RPGrpRenderer Draw first).
