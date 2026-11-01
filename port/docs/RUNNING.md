# Running the PC port

The port runs the decompiled game's own `main` on a PC (Linux, 64-bit) with Aurora
(WebGPU through Dawn) for graphics and SDL3 for the window, input and sound. It plays
the boot, the title, the save check, player and Mii selection, the party menu and the
minigames. All nine are linked and played from start to finish in the tests (by game-side
players reading the game's state); for example Shooting Range (aim with the mouse, a
gamepad's right stick or gyro; fire with B: the right mouse button, a gamepad's right
shoulder or trigger) and Laser Hockey (the paddle follows the pointer). As on the Wii, the
games open one at a time: finishing one opens the next. The HOME Menu works as on the Wii
(below). Technical notes: [HOST_BUILD.md](HOST_BUILD.md) §29-§34.

## What you need

- Your own Wii Play disc, extracted with Dolphin ("Extract Entire Disc") or `wit` / `nodtool`,
  so that the directory has `files/` and `sys/main.dol`. The port reads it and never writes
  to it.
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

Closing the window ends the game (the save file is written by the game whenever it saves,
as on the Wii).

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
| `present` | waiting for the display / GPU to take another frame |
| `sleep` | idle, waiting for the next 1/59.94 s |
| `pipe_compile` | compiling new GPU pipelines (the first time a material is seen; cached on disk afterwards) |
| `audio_queue` | sound queued for the device (about 50 ms in steady state) |

The summary also counts frames over 16.7, 20 and 33.3 ms, audio underruns (the device ran out
of sound: crackles) and skipped audio frames, and the cursor's lag (frames) and error against
the mouse. A healthy run on a desktop GPU: `wall` p99 close to 16.7, `busy` well under 16.7,
no underruns, cursor lag 0-1.

Settings that change performance: `--gpu` (above); `--async-pipelines` (draws whose pipeline
is still compiling are skipped instead of waited for: no hitches the first time something is
drawn, but a frame may miss an object, and a texture the game renders once, such as a Mii
face, may come out incomplete); `--fifo` (classic vsync instead of mailbox presentation);
`--no-vsync`.

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
| B | right button, Left Shift, Backspace | east (B / circle), right shoulder, right trigger |
| 1 / 2 | 1 / 2 | west / north |
| + / - | = or keypad + / - or keypad - | Start / Back |
| HOME | Escape, H | Guide |
| D-pad | arrow keys | D-pad, left stick (without a Nunchuk) |
| Nunchuk stick | - | left stick (with `nunchuk 1`) |
| Nunchuk Z / C | Z / C | left trigger / left shoulder |
| Tilt | - | the pad's accelerometer: lay the pad flat = remote pointing at the screen |
| Rumble | - | gamepad rumble |

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
```

Buttons: `A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C none`.

## Known limits

- Minigames: Charge! needs tilt (a gamepad's accelerometer), Tanks! the Nunchuk stick or the
  D-pad. Two players: connect a second gamepad (remote 2) before Select Players, choose "2",
  and each player picks a Mii with their own remote; all nine games' two-player modes run in
  the tests (HOST_BUILD.md §36), not yet tried by hand with two people.
- Miis: see HOST_BUILD.md §30 for the state of Mii drawing.
- The Wii's ROM font is not on the disc; the game uses it only for debug text and for a
  font-size query in Table Tennis and Laser Hockey (HOST_BUILD.md §30).
- WiiConnect24 and the remote speaker are not available. The remote speaker's sounds (the game's
  and the HOME Menu's) are muted.

## Tests

- `make -C port/host test`: every harness, then the boot headless on lavapipe: the title, the
  menus, a short Shooting Range, a short Laser Hockey, and the HOME Menu over the title, the
  party menu and Laser Hockey with Close, Reset and Wii Menu (about 8 minutes).
- `make -C port/host playtest`: whole games, twice each for determinism: all nine minigames
  from the party menu to the results and back, played by game-side players (HOST_BUILD.md
  §31-§33; about 105 minutes). Run it before committing a game milestone. A subset:
  `make -C port/host playtest PLAYTEST_ARGS="--only tanks,fishing"` (names: shooting, hockey,
  findmii, posemii, tabletennis, billiards, fishing, charge, tanks); `--png DIR` saves the
  screens checked.
- `make -C port/host playtest2`: the two-player modes of all nine games (two remotes, both
  played; twice each) and three later-level runs (Find Mii to stage 13, Pose Mii to level 5,
  Table Tennis to its 100 rallies; HOST_BUILD.md §36; about 120 minutes). Keys for
  `PLAYTEST2_ARGS="--only ..."`: shooting2, hockey2, findmii2, posemii2, tabletennis2,
  billiards2, fishing2, charge2, tanks2, findmii_long, posemii_long, tabletennis_long.

Both need the extracted disc (`DISC=/path/to/files`, default `~/wii-play/extracted/files`).
