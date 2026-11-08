# Wii Play PC port

A native PC port of Wii Play (USA, Rev 1) for Linux and Windows, built from the matching
decompilation (https://github.com/Nikman800/wiiplay). The game's own code runs on the PC: the boot,
the menus, the save, Select Mii and all nine minigames, the HOME Menu, one or two players. There is no
emulator: the Wii's hardware and SDK are replaced by a platform layer (`port/host`), and graphics go
through Aurora (WebGPU).

**You need your own Wii Play disc.** This repository contains no Nintendo data, and no game files are
downloaded. Extract the disc with Dolphin ("Extract Entire Disc") or `wit` / `nodtool`.

## Faithful to the Wii

- The game's floating-point math rounds as the Wii's does: its own math library, the Wii CPU's
  estimate instructions, and multiply-adds fused exactly where the Wii's compiler (CodeWarrior)
  fused them. Traced Tanks! missions match Dolphin bit for bit in every frame.
- Linux and Windows play identically, on any x86-64 CPU (with or without FMA).
- 59.94 frames per second (NTSC), the game's own timing, sound mixed as the Wii's DSP does it.

## Build and run (Linux)

Debian / Ubuntu packages: a Vulkan driver, `gcc-NN-plugin-dev` (for your GCC version), `libgmp-dev`,
`cmake`, `ninja-build`, `python3`. The first build downloads Dawn, SDL3 and a few libraries, pinned by
hash.

```sh
git clone https://github.com/Nikman800/wiiplay-pc && cd wiiplay-pc
git submodule update --init port/extern/aurora
make -C port/host -j$(nproc) $PWD/port/host/build/host64/boot
port/host/build/host64/boot --disc /path/to/extracted-disc --window
```

## Build and run (Windows)

The Windows version is cross-compiled on Linux (or WSL, untested) with MinGW-w64, then copied to the
Windows PC as a zip. The build commands run in a Linux shell, not PowerShell or the command prompt.

1. On Linux (Debian / Ubuntu):

   ```sh
   sudo apt install mingw-w64 g++-mingw-w64-x86-64-posix libgmp-dev cmake ninja-build python3 zip
   git clone https://github.com/Nikman800/wiiplay-pc && cd wiiplay-pc
   git submodule update --init port/extern/aurora
   make -C port/host -j$(nproc) WIN64=1 dist
   ```

   This writes `port/host/build/win64/wiiplay-windows-x64.zip` (`wiiplay.exe`, its DLLs, `play.bat`,
   `README-WINDOWS.txt`; no Nintendo data), and beside it the source zips that go with it if you
   share it (README, Legal).

2. On Windows (10 version 1903 or later, 64-bit; a GPU with Direct3D 12 or Vulkan drivers; the
   Microsoft Visual C++ Redistributable 2015-2022 x64): unzip it anywhere, put your extracted disc
   in a folder named `disc` beside `wiiplay.exe` (Dolphin's `DATA` folder, or the folder holding
   it, works too) and double-click `play.bat`. Or from a command prompt:

   ```
   wiiplay.exe --disc C:\path\to\extracted-disc --window
   ```

Saves go to `%APPDATA%\wiiplay\nand`, the config file to `%APPDATA%\wiiplay\input.cfg`. More:
[port/docs/RUNNING.md](port/docs/RUNNING.md#windows).

## Controls

Mouse = the Wii Remote's pointer, left button = A, right button = B, Escape = HOME, `1` / `2`, `=` /
`-` for + / -. Gamepads work as Wii Remotes (gyro aiming, rumble, a second gamepad for player 2).
Every motion (twist, tilt, shake, ...) has a key and a gamepad button. Keys, buttons and settings
can be changed in `input.cfg`, globally or per minigame:
[port/docs/RUNNING.md](port/docs/RUNNING.md#the-config-file). Your own Miis can be imported from a
Mii database (`--mii-db`).

## Documentation

- [port/docs/RUNNING.md](port/docs/RUNNING.md): running, options, controls, the config file, Miis,
  known limits, tests.
- [port/docs/HOST_BUILD.md](port/docs/HOST_BUILD.md): how the port works, section by section.
- [docs/DECOMP.md](docs/DECOMP.md): the decompilation this is built on (layout, naming, status).

Known limits: Miis stored on Wii Remotes and WiiConnect24 are not available; a gamepad's swing or
shake is not read (motions have buttons instead). See RUNNING.md, "Known limits".

## Decompilation updates

The port starts from the public decompilation. Decompilation fixes go there first and are merged here
(`git remote add upstream https://github.com/Nikman800/wiiplay`, then `git pull upstream main`); port
changes stay here.

## Legal

This repository contains only source code, tools and documentation written for this project (plus the
CC0 and Apache-2.0 third-party code credited below). It contains no Nintendo code, assets or binaries.
To build, you need your own copy of Wii Play (USA, Rev 1); `tools/reproduce.sh` extracts the files the
build needs from your disc image. Wii Play is a trademark of Nintendo; this project is not affiliated
with or endorsed by Nintendo.

Licence: CC0-1.0 (see `LICENSE`), except:
- `src/revolution/BTE`, which keeps its Apache-2.0 licence;
- three PC port files that come from the Dolphin emulator and are GPL-2.0-or-later
  (`LICENSES/GPL-2.0-or-later.txt`): `port/host/include/port/broadway_fp.h` (the Wii CPU's `fres` /
  `frsqrte` estimate tables), `port/host/tools/gen_ax_coefs.py` and the `port/host/src/host_ax_coefs.h`
  it generates (audio resampling tables). A PC build includes them, so the built program is
  GPL-2.0-or-later as a whole. The decompilation itself does not use them.

## How this was made

Most of the decompiled source, the headers, the tools and the PC port were written with AI coding
assistants:
Anthropic's Claude (through Claude Code), with OpenAI Codex and GLM models as additional worker agents,
directed and reviewed by the project's maintainer. Every function counted as matching was checked
mechanically: it is compiled with the original CodeWarrior compiler and must produce the original
machine code, and the whole build must reproduce the original DOL. The rules for acceptable source
("natural C") are in `tools/MATCHING.md`; places where the source still departs from them are listed in
`docs/NONMATCHING.md`. The port is checked by its test suite (`make -C port/host test`, `playtest`) and,
for its floating-point results, against Dolphin.

## Credits

The library sources under `src/nw4r`, `src/egg`, `src/revolution`, `src/RVLFaceLib`,
`src/homebuttonMiniLib`, `src/MSL`, `src/runtime` and `src/MetroTRK`, and the library unit layout,
come from Rhae (https://github.com/akiramusic000/Rhae), a decompilation of the same DOL dedicated
to the public domain under CC0-1.0. Thanks to its authors.

Several library units were matched with help from other CC0-1.0 decompilations:
Petari (https://github.com/SMGCommunity/Petari, Super Mario Galaxy), pik1wii
(https://github.com/projectPiki/pik1wii), Forecast (https://github.com/doldecomp/forecast, the Forecast
Channel; the CalcRandomSeed shape in `src/nw4r/ef/ef_animcurve.cpp`) and bba-wd
(https://github.com/vabold/bba-wd, Big Brain Academy: Wii Degree; EGG const-ness). The Bluetooth stack under `src/revolution/BTE` is
Petari's decompilation of Broadcom's BTE. Those files carry Broadcom's Apache-2.0 notices (see
`src/revolution/BTE/LICENSE-APACHE-2.0` and `NOTICE`), with changes by Petari's contributors
(muff1n1634) and small build adjustments here (compatibility headers, prototype casts).
Thanks to their authors.

The PC port reproduces the Wii CPU's floating-point estimate instructions with the tables and
interpolation from the Dolphin emulator (https://github.com/dolphin-emu/dolphin, Copyright 2008 Dolphin
Emulator Project, GPL-2.0-or-later), whose developers measured them on a console, and generates its
audio resampling tables the way Dolphin's free DSP ROM does. Its documentation of the Wii's DSP, GPU and
CPU behaviour guided much of the port. Thanks to the Dolphin team.

Graphics, windowing, input and sound go through Aurora (https://github.com/encounter/aurora, MIT,
Copyright Luke Street; this port uses a fork, `port/extern/aurora`), with Dawn (WebGPU) and SDL3.
