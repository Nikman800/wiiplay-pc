# Running the PC port

The port runs the decompiled game's own `main` on a PC (Linux or Windows, 64-bit) with Aurora
(WebGPU through Dawn) for graphics and SDL3 for the window, input and sound. It plays
the boot, the title, the save check, player and Mii selection, the party menu and the
minigames. All nine are linked and played from start to finish in the tests (by game-side
players reading the game's state); for example Shooting Range (aim with the mouse, a
gamepad's right stick or gyro; fire with B: the right mouse button, a gamepad's right
shoulder or trigger) and Laser Hockey (the paddle follows the pointer). As on the Wii, the
games open one at a time: finishing one opens the next. The HOME Menu works as on the Wii
(below). Technical notes: [HOST_BUILD.md](HOST_BUILD.md) §29-§37.

## What you need

- Your own Wii Play disc, extracted with Dolphin ("Extract Entire Disc") or `wit` / `nodtool`,
  so that the directory has `files/` and `sys/main.dol`. Dolphin puts them in a `DATA/`
  folder (beside `UPDATE/`, a system update the port does not need); `--disc` may name the
  folder above it. The port reads it and never writes to it.
- Linux x86-64 with a Vulkan driver, GCC, CMake, Ninja (or make), Python 3. The first build
  downloads Dawn, SDL3 and a few libraries (pinned by hash).

## Build

```
git submodule update --init port/extern/aurora
make -C port/host -j$(nproc) $PWD/port/host/build/host64/boot
```

## Run

```
port/host/build/host64/boot --disc /path/to/extracted-disc --window
```

- `--disc DIR`: the extracted disc (its root, or its `files/` directory).
- `--window`: a window, real time (59.94 frames per second, NTSC), sound and live input.
  Without it the executable runs headless on a virtual clock (that is what the tests use).
- `--fullscreen`: start fullscreen. F11 or Alt+Enter toggles it.
- `--scale N`: initial window size, N times 640x456 (default 2). The window can be resized
  freely: the picture keeps its aspect and gets bars.
- `--no-vsync`: present without waiting for the display.
- `--nand DIR`: where the save file goes (default `$XDG_DATA_HOME/wiiplay/nand`, usually
  `~/.local/share/wiiplay/nand`). Directories inside the source tree or the disc are refused.
- `--mii-db FILE` or `--mii-db dolphin`: copy a Mii database into the NAND before the game
  starts, so that your own Miis appear in Select Mii (below).
- `--data-dir DIR`: Aurora's data and caches (default `$XDG_DATA_HOME/wiiplay/aurora`).
- `--sc FILE`: console settings: language, 4:3 or 16:9, sensor bar position, sound mode.
  The format is in HOST_BUILD.md §22 (`key = value` lines). With `aspect = 16:9` the game draws for a widescreen TV
  and the window shows it stretched to 16:9, as the TV would.
- `--config FILE`: input bindings and settings (default `$XDG_CONFIG_HOME/wiiplay/input.cfg`,
  usually `~/.config/wiiplay/input.cfg`, if it exists).
- `--no-audio`, `--speed F`, `--frames N`: no sound; a different game speed; stop after N
  frames.
- `--gpu NAME`: the graphics adapter: part of its name (`--gpu nvidia`, `--gpu intel`), its
  index, or `discrete` / `integrated` / `cpu`. `--gpu list` prints the adapters and exits.
  Default: the environment variable `WIIPLAY_GPU` if set, else the discrete GPU. A window
  refuses the CPU (software) adapter unless asked for with `--gpu cpu`. The adapter in use is
  printed at start (`boot: gpu: ...`).
- `--perf`: performance tracking (below).
- `--frames-ahead N`: how many frames the game may run ahead of the GPU's drawing (default
  1: the game works on a frame while the GPU draws the last one, as on the Wii; with a GPU
  faster than a frame this never waits and adds no latency. 0: each frame waits until the
  last one is drawn. 2 can smooth a slow GPU at the cost of latency). The game never waits for
  the display itself (HOST_BUILD.md §39).
- `--record DIR[:N]`: write frame times, the input events (and every Nth frame as a picture)
  to DIR, for a bug report about lag. `--latency-marker`: a white square in the top-left
  corner on every frame where a key or button is pressed (film the screen to measure
  input-to-picture latency).

Play on the computer's own display. Through a remote desktop (GNOME Remote Desktop, RDP, VNC)
the picture and the sound arrive seconds late: the stream cannot carry a full-motion window at
60 frames a second (HOST_BUILD.md §37).

Closing the window ends the game (the save file is written by the game whenever it saves,
as on the Wii).

## Windows

The Windows build is cross-compiled on Linux with MinGW-w64 (HOST_BUILD.md §41) and packaged
as a zip to copy to the Windows PC.

Build (on Linux; Debian / Ubuntu packages `mingw-w64 g++-mingw-w64-x86-64-posix`, plus CMake,
Ninja, Python 3, zip):

```
git submodule update --init port/extern/aurora
make -C port/host -j$(nproc) WIN64=1 dist
```

This writes `port/host/build/win64/wiiplay-windows-x64.zip` (the first build downloads Dawn's
Windows package, SDL3 and a few libraries). It holds `wiiplay.exe`, the DLLs it loads,
`play.bat`, `README-WINDOWS.txt` and `LICENSE-math.txt`; no Nintendo data.

On Windows (10 version 1903 or later, 64-bit; a GPU with Direct3D 12 or Vulkan drivers; the
Microsoft Visual C++ Redistributable 2015-2022 x64, which Dawn's DLLs use): unzip it anywhere,
put the extracted disc in a folder `disc` beside `wiiplay.exe` and double-click `play.bat`, or
from a command prompt:

```
wiiplay.exe --disc C:\path\to\extracted-disc --window
```

Every option above works the same. The folders differ:

| What | Linux | Windows |
|---|---|---|
| Saves (`--nand`) | `~/.local/share/wiiplay/nand` | `%APPDATA%\wiiplay\nand` |
| Input config (`--config`) | `~/.config/wiiplay/input.cfg` | `%APPDATA%\wiiplay\input.cfg` |
| Graphics caches (`--data-dir`) | `~/.local/share/wiiplay/aurora` | `%LOCALAPPDATA%\wiiplay\aurora` |
| `--perf` files | `~/.local/state/wiiplay` | `%LOCALAPPDATA%\wiiplay` |

Graphics use Direct3D 12, else Vulkan; `--backend vulkan` (or `d3d12`, `d3d11`; the
environment variable `WIIPLAY_BACKEND`) picks the API to try first, and `--gpu list` shows the
adapters. `--mii-db dolphin` looks in Dolphin's Windows user folder: the one in Dolphin's
registry setting (`HKCU\Software\Dolphin Emulator\UserConfigPath`), `Documents\Dolphin
Emulator` and `%APPDATA%\Dolphin Emulator`, and takes the newest `Wii\shared2\menu\FaceLib\RFL_DB.dat`;
a portable Dolphin keeps it in the `User` folder next to `Dolphin.exe` (give that file with
`--mii-db FILE`).

The program is a console program: it prints its messages to the console it was started from
(double-clicking `wiiplay.exe` opens one).

## Performance: `--perf` and F3

```
port/host/build/host64/boot --disc /path/to/extracted-disc --window --perf
```

Play as usual, then close the window. At exit the program prints a summary and writes two
files to `$XDG_STATE_HOME/wiiplay/` (usually `~/.local/state/wiiplay/`):
`perf-DATE-TIME.csv` (one row per frame) and `perf-DATE-TIME.txt` (the summary). Paste the
`.txt` file (or the summary from the terminal, from `=== wiiplay perf summary ===` to the
`worst frames` list) into a bug report. `--perf-out FILE.csv` writes elsewhere (outside the
source tree and the disc).

F3 shows or hides an overlay with the same numbers, averaged over the last 120 frames, and a
graph of the frame times (the red line is 16.7 ms). F3 works without `--perf` too (nothing is
written then).

What the numbers mean (milliseconds per displayed frame):

| Column | Time spent |
|---|---|
| `wall` | the whole frame: 16.7 at full speed (59.94 frames per second) |
| `calc` | the game's own code, the sound mixer |
| `gx` | handing the frame's drawing to the renderer; includes draws waiting for a new pipeline |
| `end` | finishing the frame for the GPU |
| `gpu` | waiting for the GPU (only with `--writeback on`: the game's render-to-texture copies read back every frame) |
| `present` | waiting for the GPU to finish drawing an earlier frame (`--frames-ahead`) |
| `sleep` | idle, waiting for the next 1/59.94 s |
| `pipe_compile` | compiling new GPU pipelines (the first time a material is seen; cached on disk afterwards) |
| `audio_queue` | sound queued for the device (about 50 ms in steady state) |
| `gx_sync` | part of `gx`: the game waiting at its draw-done syncs while the renderer translates what it sent (`syncs`: how many) |
| `render_latency` | from a frame's end until the GPU has drawn it |
| `acquire` | the renderer waiting for the window system to hand back an image to draw into (it does not hold up the game) |

The summary also counts frames over 16.7, 20 and 33.3 ms, audio underruns (the device ran out
of sound: crackles) and skipped audio frames, and the cursor's lag (frames) and error against
the mouse. A healthy run on a desktop GPU: `wall` p99 close to 16.7, `busy` well under 16.7,
no underruns, cursor lag 0-1.

Settings that change performance: `--gpu` (above); `--async-pipelines` (draws whose pipeline
is still compiling are skipped instead of waited for: no hitches the first time something is
drawn, but a frame may miss an object, and a texture the game renders once, such as a Mii
face, may come out incomplete); `--fifo` (classic vsync instead of mailbox presentation);
`--no-vsync`.

## Your own Miis

Without a Mii database only the six guest Miis appear in Select Mii, and the game keeps no
My Record for guests. Wii Play reads the Miis from the Wii Menu's Mii Channel database,
`/shared2/menu/FaceLib/RFL_DB.dat` on the console's NAND; the port reads the same file from
its NAND directory (`--nand`), `<nand>/shared2/menu/FaceLib/RFL_DB.dat`. The game never writes
it, and records are kept for each Mii in the save file as on the Wii.

To make Miis with Dolphin's Mii Channel:

1. Install Dolphin: the Flatpak from Flathub (`flatpak install flathub
   org.DolphinEmu.dolphin-emu`) or your distribution's package.
2. Tools -> Perform Online System Update (pick your region; it downloads the Wii Menu from
   Nintendo's update servers).
3. Tools -> Load Wii System Menu, then open the Mii Channel, make your Miis and leave the Mii
   Channel (back to the Wii Menu: that saves the database). Close Dolphin.
4. Start the port once with `--mii-db dolphin`:

   ```
   port/host/build/host64/boot --disc /path/to/extracted-disc --window --mii-db dolphin
   ```

   It prints `boot: Mii database: PATH` and copies that file into the port's NAND. Dolphin's
   file is only read. The copy stays, so later runs need no option; give it again after you
   change your Miis in Dolphin.

`--mii-db dolphin` looks for Dolphin's database in these places and takes the newest:

| Dolphin | Database |
|---|---|
| Flatpak | `~/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/Wii/shared2/menu/FaceLib/RFL_DB.dat` |
| Distribution package | `~/.local/share/dolphin-emu/Wii/shared2/menu/FaceLib/RFL_DB.dat` (`$XDG_DATA_HOME/dolphin-emu/...`) |
| Older versions | `~/.dolphin-emu/Wii/shared2/menu/FaceLib/RFL_DB.dat` |

`--mii-db FILE` takes any `RFL_DB.dat` instead (another Dolphin user folder, a NAND backup of
your own Wii). The file is checked first (its header and checksum); a damaged one is refused
and the NAND's copy is left as it was. Copying the file by hand to
`~/.local/share/wiiplay/nand/shared2/menu/FaceLib/RFL_DB.dat` does the same.

A Mii is on Wii Play's player list from the first time you pick it ("Added ... to the player
list"); if you erase it in the Mii Channel and copy the database again, its records go, as on
the Wii. Miis stored on a Wii Remote ("Wii Remote" tab) are not available.

## The HOME Menu

HOME (Escape or H on the keyboard, the Guide button on a gamepad) opens the game's own HOME
Menu on the title, the party menu and in a minigame: the game pauses underneath, and the menu
has its pointer, buttons and sounds. On Select Players and Select Mii the game does not allow
it: a crossed-out HOME icon shows instead, as on the Wii.

- **Close** (or HOME again): back to the game, which goes on where it was paused.
- **Wii Remote Settings**: the battery of each connected remote, the speaker volume and
  rumble (they start from the console settings, `--sc`; changes last until the program
  ends and are not written back), and Reconnect (the remotes disconnect and connect again).
- **Reset**: after "Yes", the game resets itself to the title screen, as Wii Play does on
  the Wii (without the health screen).
- **Wii Menu**: after "Yes", the game shuts down as it would on the Wii (it finishes any save
  and fades out its sound) and the program ends. There is no Wii Menu to return to.

## Controls

Remote 1 is the keyboard and mouse, and also the first gamepad. Gamepads 2-4 are remotes
2-4.

| Wii Remote | Mouse / keyboard | Gamepad |
|---|---|---|
| Pointer | mouse position over the picture | right stick; gyro (if the pad has one) |
| A | left button, Space, Enter | south (A / cross) |
| B | right button, Left Shift, Backspace | east (B / circle), right trigger |
| 1 / 2 | 1 / 2 | west / north |
| + / - | = or keypad + / - or keypad - | Start / Back |
| HOME | Escape, H | Guide |
| D-pad | arrow keys | D-pad; left stick in the games without tilt |
| Nunchuk stick | - | left stick (with `nunchuk 1`) |
| Nunchuk Z / C | Z / C | - (Wii Play does not use them) |
| Twist (turn the remote like a key) | Q / E (turns while held, stays), mouse wheel, middle button: level again | left stick left / right in Laser Hockey, Pose Mii; left stick click: level |
| Tilt | W / S forward / back, A / D left / right (while held) | left stick in Laser Hockey, Pose Mii, Charge!; the pad's accelerometer (pad flat = remote pointing at the screen) |
| Shake (lift, jerk) | F, mouse side button (back) | right shoulder |
| Pull back and thrust | R or mouse side button (forward): hold to draw the remote back, release to thrust | left shoulder |
| Push (move closer) | G (while held) | left trigger |
| Rumble | - | gamepad rumble |
| Remote speaker | the main sound output, quieter (`speaker_volume`) | the same |

Per game:

| Game | What to do | Mouse / keyboard | Gamepad |
|---|---|---|---|
| Shooting Range | aim, shoot | mouse, right button | right stick or gyro, east / right trigger |
| Find Mii | point, pick; zoom in (some stages): move the remote closer | mouse, left button; hold G | right stick, south; hold left trigger |
| Table Tennis | the racket follows the pointer | mouse | right stick or gyro |
| Pose Mii | move the Mii, A / B change the pose, twist to turn it | mouse, left / right button, Q / E or the wheel | right stick, south / east, left stick left / right |
| Laser Hockey | the paddle follows the pointer; twist it | mouse, Q / E or the wheel (middle button: straight) | right stick, left stick left / right |
| Billiards | aim with the D-pad, point at the cue ball; hold B, pull back, thrust | arrows, mouse; hold Left Shift or the right button, hold R, release R | D-pad, right stick; hold east or right trigger, hold left shoulder, release it |
| Fishing | move the rod, lift when a fish bites | mouse; F | right stick; right shoulder |
| Charge! (remote on its side) | tip forward to run, back to slow, tilt left / right to steer, lift to jump | W / S, A / D, F | left stick up / down / left / right, right shoulder |
| Tanks! | move, aim, shoot, mines | arrows, mouse, right button, left button | D-pad or left stick, right stick, east, south |

The title screen asks for A and B together: left and right mouse buttons, or Space and Left
Shift, or the gamepad's south and east buttons.

## The config file

Plain text, `#` starts a comment. Every line is optional.

```
# keyboard and mouse: key NAME BUTTON (SDL key names; quote names with spaces)
key Return A
key "Left Shift" B
key Mouse2 HOME          # middle mouse button
key Escape none          # unbind
# gamepad: pad NAME BUTTON (south east west north back guide start leftstick rightstick
# leftshoulder rightshoulder dpup dpdown dpleft dpright misc1 lefttrigger righttrigger)
pad rightshoulder 1
# clear                  # drop every binding (default ones included) before your own
pointer_speed 1.5        # right stick: screens per second
gyro_sensitivity 2.0     # gyro pointer: screens per radian; 0 turns it off
tilt 1                   # gamepad accelerometer -> remote tilt
rumble 1
volume 1.0
mute_unfocused 0         # 1: silence while the window has no focus
nunchuk 0                # 1: every remote has a Nunchuk (left stick = its stick)
stick_deadzone 0.2
pointer_stabilise 0      # 1: keep the game's pointer stabilisation for the mouse (a real remote's)
# motions (HOST_BUILD.md §37)
key Q TWIST_LEFT         # bind any key or gamepad button to a motion
pad rightshoulder SHAKE
left_stick auto          # auto: tilt in the games that read tilt, else the D-pad; or dpad / tilt
twist_speed 120          # degrees per second while Q / E is held
wheel_twist 10           # degrees per mouse wheel step
tilt_angle 60            # degrees of a tilt key held, or the stick pushed all the way
tilt_speed 360           # degrees per second the tilt keys move
shake_strength 2.5       # g
shake_frames 4
pull_distance 0.3        # metres the remote draws back while PULL is held
push_distance 0.3        # metres closer while PUSH is held
speaker_volume 0.5       # the Wii Remote speaker's sound in the output; 0 mutes it
```

Buttons: `A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C none`. Motions: `TWIST_LEFT
TWIST_RIGHT TWIST_RESET TILT_FORWARD TILT_BACK TILT_LEFT TILT_RIGHT SHAKE PULL PUSH`.
Mouse buttons are `Mouse1` (left), `Mouse2` (middle), `Mouse3` (right), `Mouse4` / `Mouse5`
(side); the wheel always twists (`wheel_twist 0` turns it off).

## Known limits

- Minigames: every motion has a key and a gamepad button (above); a real swing or shake of a
  gamepad is not read yet (its accelerometer gives the tilt only). Two players: connect a second gamepad (remote 2) before Select Players, choose "2",
  and each player picks a Mii with their own remote; all nine games' two-player modes run in
  the tests (HOST_BUILD.md §36), not yet tried by hand with two people.
- Miis: your own Miis come from a Mii database (above); Miis on a Wii Remote are not
  available. HOST_BUILD.md §30 has the state of Mii drawing.
- The Wii's ROM font is not on the disc; the game uses it only for debug text and for a
  font-size query in Table Tennis and Laser Hockey (HOST_BUILD.md §30).
- WiiConnect24 is not available. The remote speaker's sounds (the game's and the HOME Menu's)
  play through the computer's sound output, mixed in at `speaker_volume`.

## Tests

- `make -C port/host test`: every harness, then the boot headless on lavapipe: the title, the
  menus, a short Shooting Range, a short Laser Hockey, and the HOME Menu over the title, the
  party menu and Laser Hockey with Close, Reset and Wii Menu, then the motions in the six
  games that read them (about 20 minutes; `make -C port/host gestures` runs those alone).
- `make -C port/host playtest`: whole games, twice each for determinism: all nine minigames
  from the party menu to the results and back, played by game-side players (HOST_BUILD.md
  §31-§33; about 105 minutes). Run it before committing a game milestone. A subset:
  `make -C port/host playtest PLAYTEST_ARGS="--only tanks,fishing"` (names: shooting, hockey,
  findmii, posemii, tabletennis, billiards, fishing, charge, tanks); `--png DIR` saves the
  screens checked.
- `make -C port/host miitest`: a Mii from a database the test writes (HOST_BUILD.md §40): the
  menus and a whole Shooting Range with it, its record in the save, then the menus again with
  the record shown (about 25 minutes).
- `make -C port/host playtest2`: the two-player modes of all nine games (two remotes, both
  played; twice each) and three later-level runs (Find Mii to stage 13, Pose Mii to level 5,
  Table Tennis to its 100 rallies; HOST_BUILD.md §36; about 120 minutes). Keys for
  `PLAYTEST2_ARGS="--only ..."`: shooting2, hockey2, findmii2, posemii2, tabletennis2,
  billiards2, fishing2, charge2, tanks2, findmii_long, posemii_long, tabletennis_long.

Both need the extracted disc (`DISC=/path/to/files`, default `~/wii-play/extracted/files`).

With `WIN64=1` the same targets build the Windows executables and run them under Wine
(`wine`, headless; HOST_BUILD.md §41), for example `make -C port/host WIN64=1 unit boot`.
