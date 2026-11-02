Wii Play, PC port: Windows x64 build
=====================================

The decompiled game running natively on Windows, with Aurora (the Wii's GX graphics on
WebGPU through Dawn: Direct3D 12 or Vulkan) and SDL3 for the window, sound and input.
This package contains only the program and its libraries: no Nintendo data. You need
your own Wii Play disc (USA, RHAE01).

What you need
-------------
- Windows 10 version 1903 or later, or Windows 11, 64-bit.
- A graphics card with Direct3D 12 or Vulkan drivers.
- The Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64), which
  webgpu_dawn.dll and dxcompiler.dll use. Most PCs have it; if wiiplay.exe says
  MSVCP140.dll or VCRUNTIME140_1.dll is missing, install it from Microsoft
  ("vc_redist.x64.exe").
- Your Wii Play disc, extracted: in Dolphin, right-click the game, Properties,
  Filesystem, right-click the disc, "Extract Entire Disc". The folder must contain
  files\ and sys\main.dol. The port only reads it.

Run
---
Easiest: put the extracted disc in a folder named "disc" next to wiiplay.exe (so that
disc\files and disc\sys exist) and double-click play.bat.

From a command prompt:

    wiiplay.exe --disc C:\path\to\extracted-disc --window

Useful options (wiiplay.exe with no options lists them all):

    --fullscreen          start fullscreen (F11 or Alt+Enter toggles)
    --scale N             window size N x 640x456 (default 2)
    --mii-db dolphin      use the Miis you made in Dolphin's Mii Channel (below)
    --mii-db FILE         use this RFL_DB.dat (a NAND backup of your own Wii)
    --gpu list            list the graphics adapters; --gpu NAME picks one
    --backend vulkan      use Vulkan instead of Direct3D 12 (or d3d11); try it if the
                          picture is wrong or the program cannot start its graphics
    --no-vsync, --fifo    presentation settings
    --perf                write frame timings for a bug report (F3 shows them live)

Where things are kept
---------------------
    Saves (the NAND)        %APPDATA%\wiiplay\nand         (--nand DIR)
    Input bindings          %APPDATA%\wiiplay\input.cfg    (--config FILE; optional)
    Graphics caches         %LOCALAPPDATA%\wiiplay\aurora  (--data-dir DIR)
    --perf logs             %LOCALAPPDATA%\wiiplay

Your own Miis
-------------
Make Miis in Dolphin's Mii Channel (Tools > Perform Online System Update, then Tools >
Load Wii System Menu, Mii Channel), close Dolphin, then start once with
--mii-db dolphin. It copies Dolphin's Wii\shared2\menu\FaceLib\RFL_DB.dat into the
port's NAND, taking the newest of: the folder in Dolphin's registry setting
(HKCU\Software\Dolphin Emulator\UserConfigPath), Documents\Dolphin Emulator and
%APPDATA%\Dolphin Emulator. A portable Dolphin (portable.txt) keeps it in the User
folder next to Dolphin.exe: give that file with --mii-db FILE.

Controls
--------
Mouse = the Wii Remote's pointer, left button = A, right button = B, Escape = HOME,
1 / 2, = / - for + / -; gamepads work as remotes (south = A, east = B, Guide = HOME).
The full tables (motions, every minigame) are in port/docs/RUNNING.md in the source.

Files
-----
    wiiplay.exe              the game (console program: messages appear in the console)
    wiiplay_gx.dll           Aurora, SDL3 and the glue
    webgpu_dawn.dll          Dawn (WebGPU); dxcompiler.dll, dxil.dll: its D3D12 shader compiler
    libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll   the GCC (MinGW-w64) runtime
    play.bat                 runs the disc in .\disc in a window

Licenses: Aurora (MIT), Dawn (BSD 3-clause), SDL3 (zlib), DirectX Shader Compiler
(University of Illinois/NCSA), the GCC runtime (GPL with the GCC Runtime Library
Exception), winpthreads (MIT/BSD); in wiiplay.exe, math functions from ARM
optimized-routines (MIT OR Apache-2.0 WITH LLVM-exception) and CORE-MATH (MIT): their
notices are in LICENSE-math.txt.
