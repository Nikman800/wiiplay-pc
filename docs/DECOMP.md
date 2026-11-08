# The decompilation

This is the README of the matching decompilation this port is built on
(https://github.com/Nikman800/wiiplay), kept here for the decomp tooling in `tools/`, `dtk/` and `config/`.

A matching decompilation of Wii Play (USA, Rev 1, RHAE01).

## Layout

| Path | What |
|---|---|
| `extracted/` | Data partition from the RVZ (`dtk disc extract`). `sys/main.dol` is the only code; no RELs. |
| `dtk/` | decomp-toolkit analysis: `symbols.txt` (13,068 functions, 301 named SDK/MSL/TRK), `splits.txt`, `split/` asm per auto unit, `units.tsv` (code unit ranges). |
| `main.elf` | `main.dol` converted to ELF32 BE PowerPC ET_EXEC with dtk symbols and `_SDA_BASE_`/`_SDA2_BASE_` (`tools/dol2elf.py`). |
| `src/`, `include/` | Decompiled C/C++ source and headers; `config/RHAE01/` holds the split and symbol ledgers. |
| `decomp/` | Optional local Ghidra pseudocode (not committed; see docs/SETUP.md). |
| `tools/` | Build, matching and analysis scripts (see tools/MATCHING.md and docs/SETUP.md). |

Key facts: entry `0x80006124`, r13 (`_SDA_BASE_`) `0x80442AC0`, r2 (`_SDA2_BASE_`) `0x80444B00`.
Language `PowerPC:BE:32:Gekko_Broadway` from Cuyler36/Ghidra-GameCube-Loader 1.3.1.

## Build

The repository holds only our own work; game-derived files are rebuilt from your own disc image
(Wii Play USA Rev 1, RHAE01). On Linux, with Python 3.9+ and ninja:

```sh
bash tools/reproduce.sh "/path/to/Wii Play (USA) (Rev 1).rvz"   # extracts the disc to extracted/
python3 configure.py
ninja                                                            # downloads dtk, wibo and the compilers
sha1sum -c config/RHAE01/build.sha1                              # build/RHAE01/main.dol: OK
```

Requirements, details and the matching workflow: `docs/SETUP.md`. Status and roadmap: `docs/PLAN.md`.

## Naming (game code + EGG)

`dtk/names.tsv` is the ledger: address, original dtk name, new name, module, confidence, evidence.
`dtk/symbols.orig.txt` is dtk's untouched output; `config/RHAE01/symbols.txt` carries the new names.

Rebuild: `build_index.py .` (asm index) → `extract_literals.py .` (needs `decomp/`) → `name_game.py .` →
re-apply to `symbols.txt` → `dol2elf.py` → `export_decomp.sh`.

Name shapes (address suffix keeps names unique and traceable):

- `Hky_80216A24` — module only
- `Hky_HkyGoalLeftWd_80217750` — module + the function's most distinctive string literal
- `Tnk_BasicTank_8035B100_vf14` — virtual slot 14 of the class whose vtable is at `8035B100`; the class tag is the
  most distinctive literal used by its methods. Inherited methods are named on the most-base vtable.
- `Wly_..._8035CC80_init_80291828` — non-virtual function that loads that vtable (constructor/destructor candidates)
- `eggHeap_800A3420` — EGG function inside the run of `eggHeap.cpp` assert anchors

Modules: minigames `Fsh` Fishing, `Hky` Laser Hockey, `Pnp` Table Tennis, `Duc` Shooting Range, `Tnk` Tanks!,
`Wly` Find Mii, `Cow` Charge!, `Bil` Billiards, `Bom` Pose Mii; framework `Sys`, `Snd`, `Gfx`, `Lyt` (layout widgets),
`Menu`, `Party` (Wii Play title/results), `Mii`, `Health` (strap screen), `HomeMenu`, `Mpe` (unidentified scene),
`RP` (framework, subsystem unknown).

Confidence: `anchor` (uses a module-unique string) > `bounded` (between two same-module anchors; link-order
contiguity) > `class` (own vtable methods agree) > `callgraph` (≥2/3 neighbours agree) > `position` (nearest anchor).
Module codes come from the game's own asset/sound names; the minigame titles are inferred from those assets.

Known tooling issues: `dtk dol split` cannot re-run on this DOL (it names two sections `.rodata`); the index uses the
first split's asm. REA's Ghidra session does not seed r2/r13, so REA pseudocode shows raw SDA offsets — use `decomp/`
for resolved small-data variables.

## Method names (pass 2)

Every game function now has a method-level name such as `Hky_Puck_calc`, `Tnk_Shell_ctor` or
`Party_Common_calc`, proposed by per-module review of the pseudocode (13 batches) and merged by
`tools/merge_proposals.py apply`.

- `dtk/methods.tsv` — final ledger: address, dtk name, pass-1 module name, method name, module, confidence
  (`high`/`medium`/`low`/`keep`), evidence note, batch. `keep` rows retain the pass-1 name.
- `dtk/classes.tsv` — 398 classes by vtable address with evidence; `dtk/proposals/` holds the raw batch files.
- Conventions the reviewers followed: `tools/NAMING_CONVENTIONS.md`. Inspect any function with
  `tools/fn_info.py show <addr>` (callers/callees, literals, vtable slots, pseudocode).

Treat `low` names as hypotheses. State-machine handlers were mapped from member-function-pointer tables in
`.rodata`; virtual-slot meanings for framework bases (e.g. `Sys_Scene` slots 15–20: configure, loadResource,
reset, calculate, exit, async load) were inferred from how callers use them. Shared helpers duplicated per
source file carry an `_<address>` suffix. `Mpe` turned out to be the RP audio manager (`Mpe_AudioMgr`), not a scene.

## Types, headers and typed pseudocode (Phase 3)

Every game class has a C++ header in `include/Pack/` whose size is checked against Wii Play's
allocations with the original compiler (`tools/sizeof_probe.py`, ledgers `dtk/types/*_sizes.tsv`):
framework classes in `include/Pack/RPParty/RPPartyCommon/` and `include/Pack/RPAudio/`, minigames in
`include/Pack/RPParty/RP<Game>Scene/`. Global data has names and types (`dtk/types/*_globals.tsv`).
The headers are turned into Ghidra types through CodeWarrior's debug info, and
`tools/export_typed.sh` writes typed pseudocode for all code to `decomp_typed/all/` (see
`docs/SETUP.md`). Every field and name carries a confidence tag ([H]/[M]/[L]).

`include/hky.h` is the earlier plain-C Laser Hockey layout, still used by the matched unit
`src/Hky/FieldWallIter.cpp` and the compiler probes until they move to the C++ headers.

## Status

- Game code (Phase 4): 487 of 491 units are matching C/C++ that rebuilds byte-identical code; the other
  4 are functionally equivalent source with notes (`docs/NONMATCHING.md`).
- Libraries (Phase 5): 636 of 637 units match (EGG, the RVL SDK, BTE, RVLFaceLib, HBM, MSL, runtime,
  MetroTRK, and all of nw4r except `ef_animcurve.cpp`, which is functional and marked).
- The build reproduces the original `main.dol` byte for byte. See `docs/PLAN.md` for the roadmap.
