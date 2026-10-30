# Game data formats

This note describes the `.plight`, `.plmap`, `.pblm`, `.pmmisc`, and selected `.bin` files used by Wii Play. All numeric fields are big-endian. Offsets are relative to the containing file unless stated otherwise. The parser is `port/assets/tools/fmt_game_data.py`.

## Common EGG binary envelope

Every EGG settings file starts with `EGG::IBinary<T>::Bin` (`include/egg/core/eggIBinary.h`):

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `char[4]` | Magic: `LGHT`, `LMAP`, `PBLM`, or `MMSC` |
| `0x04` | `u32` | Total file size, including this 16-byte header |
| `0x08` | `u8` | Format version |
| `0x09` | `u8[3]` | Padding |
| `0x10` | varies | `BinData`, described below |

`SetBinary` checks the magic, version, and size before dispatching to `SetBinaryInner`. No field in these files is a file offset that the game changes to a pointer. Nested records use their internal size to advance to the next record. A loader must bounds-check every size and record count.

## EGG settings

### `.plight` — `LGHT`

- Version 0: `u16 lightNum` at `0x10`, two `GXColor` ambient colors at `0x14` and `0x18`, padding, then `LOBJ` records at `0x30`. The loader uses only two ambient colors and forces the light-texture color to black. (`src/egg/gfxe/eggLightManager.cpp:317-336`)
- Version 1: `u16 lightNum` at `0x10`, eight `GXColor` ambient colors beginning at `0x14`, `GXColor lightTexColor` at `0x34`, padding, then `LOBJ` records at `0x48`. (`src/egg/gfxe/eggLightManager.cpp:338-360`)
- Version 2: `u16 lightNum` at `0x10`, `u16 ambientNum` at `0x12`, `GXColor lightTexColor` at `0x14`, padding, `LOBJ` records at `0x28`, then `ambientNum` ambient records. Each ambient record is a `GXColor` followed by four padding bytes. (`src/egg/gfxe/eggLightManager.cpp:362-390`)
- `LOBJ` uses the common EGG envelope, version 1, record size `0x50`: modes and light type at `0x10`–`0x13`, `u16 ambLightIdx` at `0x14`, `u16 flags` at `0x16`, `f32[3] pos` at `0x18`, `f32[3] target` at `0x24`, `f32 intensity` at `0x30`, `GXColor` brightness and darkness at `0x34` and `0x38`, and cutoff/attenuation floats at `0x3C`–`0x44`. (`include/egg/gfxe/eggLightObject.h:16-31`; loader `src/egg/gfxe/eggLightObject.cpp:393-430`)

### `.plmap` — `LMAP`

The outer envelope is version 0. Its data begins with a 16-byte prefix; `u16 textureNum` is at `0x10`, and `LTEX` records start at `0x20`. The loader reads the count, then advances each record by its own `header.size`. (`include/egg/gfxe/eggLightTextureManager.h:18-20`; loader `src/egg/gfxe/eggLightTextureManager.cpp:263-278`)

Each nested `LTEX` uses the common envelope, version 0. Its minimum size is `0x3C`:

| Offset from `LTEX` | Type | Meaning |
|---:|---|---|
| `0x10` | `u16` | `lightNum` |
| `0x14` | `char[32]` | NUL-terminated texture name |
| `0x34` | `u8` | `drawType` |
| `0x38` | `u32` | `lightMask` |
| `0x3C` | `u32` | First light entry |

Each light entry is eight bytes: `f32 effectScale`, `u8 pttrn`, and three padding bytes. (`include/egg/gfxe/eggLightTexture.h:17-29`; loader `src/egg/gfxe/eggLightTexture.cpp:516-534`)

### `.pblm` — `PBLM`

Versions 0 and 1 share a body whose minimum useful size is `0x94`:

| Offset | Type | Meaning |
|---:|---|---|
| `0x10` | `f32` | `mThreshold` |
| `0x14` | `GXColor` | `mMaskColor` |
| `0x18` | `GXColor` | `mColor` |
| `0x1C` | `u16` | `mFlags` |
| `0x1E` | `u16` | Unknown |
| `0x20`, `0x40`, `0x60` | `BlurParam[3]` | `mRadius`, `mIntensity`, then 24 unknown/padding bytes |
| `0x80` | `u8` | `mBlendModeIdx` |
| `0x81` | `u8` | `mNumBlur` |
| `0x8C` | `f32[2]` | `mGatherScale0`, `mGatherScale1` |

Version 0 forces both gather scales to zero in the loader; version 1 uses the stored values. (`include/egg/gfxe/eggDrawPathBloom.h:25-45`; loader `src/egg/gfxe/eggDrawPathBloom.cpp:281-292`)

### `.pmmisc` — `MMSC`

The outer envelope is version 0. It contains `u16 targetNum` at `0x10` and `targetNum` records beginning at `0x14`. Each record is `0x28` bytes: `char name[32]`, `u8 unk20`, three padding bytes, and `f32 unk24`. The current Wii Play decompilation has an empty `SetBinaryInner`; the format is therefore validated against the declared structure, not behavior. (`include/Pack/RPGraphics/RPGrpModelMisc.h:18-30`; stub loader `src/Pack/RPGraphics/RPGrpModelManager.cpp:51-58`)

## Game tables

All fields in these tables are 32-bit `s32`, `u32`, or `f32`. The files have no headers or magic.

### Duck Hunt parameters

Six `param/Duc*.bin` files decode directly as their decompiled parameter structures. Each has eight pattern records:

- `DucBalloonParam.bin`: `0xBC`; header through `mSwayAmp` at `0x24`, eight `0x10` patterns at `0x28`, then `f32 mStringSegs[5]` at `0xA8`. (`include/Pack/RPParty/RPDucScene/RPDucBalloon.h:20-44`)
- `DucCanParam.bin`: `0x2E0`; header through `mSpinDecay` at `0x1C`, then eight `0x58` patterns at `0x20`. (`include/Pack/RPParty/RPDucScene/RPDucCan.h:21-54`)
- `DucCircleMatoParam.bin`: `0x140`; parameter header through `mBaseRadius` at `0x34`, `f32 mGrid[2][5]` at `0x38`, then eight `0x1C` patterns at `0x60`. (`include/Pack/RPParty/RPDucScene/RPDucCircleTarget.h:19-50`)
- `DucClayParam.bin`: `0x1E4`; header through `mSpin` at `0x20`, then eight `0x38` patterns at `0x24`. (`include/Pack/RPParty/RPDucScene/RPDucClay.h:31-61`)
- `DucDuckParam.bin`: `0x21C`; header through `mForwardSpeed` at `0x58`, four `0x08` motion records at `0x5C`, then eight `0x34` patterns at `0x7C`. (`include/Pack/RPParty/RPDucScene/RPDucDuck.h:24-78`)
- `DucUfoParam.bin`: `0x4A8`; header through `mAbductRadius` at `0x44`, then eight `0x8C` patterns at `0x48`. (`include/Pack/RPParty/RPDucScene/RPDucUfo.h:23-80`)

The named loaders are `RPDucBalloon::Load` (`src/Pack/RPParty/RPDucScene/RPDucBalloon.cpp:225`), `RPDucCan::Load` (`RPDucCan.cpp:515`), `RPDucCircleTarget::LoadParams` (`RPDucCircleTarget.cpp:55`), `RPDucClay::Load` (`RPDucClay.cpp:247`), `RPDucDuck::LoadParams` (`RPDucDuck.cpp:76`), and `RPDucUfo::LoadParams` (`RPDucUfo.cpp:74`). The loaders copy the data into private objects; no offsets are relocated.

### Pose Mii level tables

- `leveltable.bin`: 13 records of 12 bytes. `LevelEntry` has three `u32` fields at `0x00`, `0x04`, and `0x08`: the CPU's near, centre and far target weights, summed as `u32` by `PnpPlayerPickTargetIndex`. (`include/Pack/RPParty/RPPnpScene/RPPnpLevelTable.h:16-20`; `src/Pack/RPParty/RPPnpScene/RPPnpPlayer.cpp:376-379`)
- `rallyleveluptable.bin`: 217 records; `rallyleveluptable2.bin`: 101 records. `RallyEntry` is `s32 mRally`, `s32 mCpuLevel`, `f32 mBallSpeed`. (`include/Pack/RPParty/RPPnpScene/RPPnpLevelTable.h:22-26`)
- `RPPnpLevelTable` loads each file into a private buffer with `memcpy`; there are no relocations. (`src/Pack/RPParty/RPPnpScene/RPPnpLevelTable.cpp:21-42`)

### Find Mii level tables

`wanted_1p_leveltable.bin` and `wanted_2p_leveltable.bin` contain `RPWlyLevelData` records of `0x40` bytes. The fields are `mAvatarNum` (`s32`), `unk4` (`u32`), `mQuestionType` (`s32`), `mFieldType` (`s32`), and 12 `f32` fields; see the parser schema for the exact names and order. The constructor scales `mSpeed` by `1.3684211f` on 16:9 and sets `unk20` to `6.25f` on field type 3. The 99/80 record count is derived from file size. (`include/Pack/RPParty/RPWlyScene/RPWlyLevelTable.h:20-35`; loader `src/Pack/RPParty/RPWlyScene/RPWlyLevelTable.cpp:39-47`)

### Tank map data

`MapData/TnkMapData_P{players}_{stage}_{tvMode}.bin` contains `RPTnkMapData::Map`:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `s32` | `width`: 16 for 4:3 (`tvMode=0`), 22 for 16:9 (`tvMode=1`) |
| `0x04` | `u32` | `unk4` |
| `0x08` | `s32` | `unk8` |
| `0x0C` | `s32` | `unkC` |
| `0x10` | `s32[]` | `width * 17` grid cells |

The loader copies the complete file into a private buffer and indexes `cells[y * width + x]`. (`include/Pack/RPParty/RPPartyCommon/RPTnkMapData.h:22-28`; loader `src/Pack/RPParty/RPPartyCommon/RPTnkMapData.cpp:71-82`)

### Tank game parameters

`param/TnkGameParam.bin` is `0x28F4`:

| Offset | Type | Meaning |
|---:|---|---|
| `0x0000` | `s32` | `lives` |
| `0x0004` | `0xA8[10]` | One enemy parameter record per tank type (fields below) |
| `0x0694` | `s32[100][0x16]` | 100 stage records |

Each `0xA8` enemy record contains 42 big-endian 32-bit words. The type pattern comes from the complete plain-type views: `RPTnkPlayerParams` (`src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:21-64`) and `AttackEnemyParamsData` (`src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:63-69`). Named AI destinations are defined in `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:317-336`; the field loads are in `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:512-538`. `RPTnkTank::SpawnEnemy` reads `stealth` (`src/Pack/RPParty/RPTnkScene/RPTnkTank.cpp:446-460`), and `RPTnkPlayerController::Reset` reads the player-named fields (`src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:128-145`).

| Record offset | Type | Field | Direct source evidence |
|---:|---|---|---|
| `0x00` | `s32` | `stealth` | `src/Pack/RPParty/RPTnkScene/RPTnkTank.cpp:25,459` |
| `0x04` | `f32` | `mMineClearDist` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:63,532`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:331` |
| `0x08` | `s32` | `maxMines` (`mMineMax`) | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:25,144`; `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:530`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:329` |
| `0x0C` | `s32` | `mMineIntervalMax` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:535`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:334` |
| `0x10` | `s32` | `mMineIntervalMin` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:534`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:333` |
| `0x14` | `f32` | `mMineEnemyDist` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:533`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:332` |
| `0x18` | `f32` | `mMineChanceNearBlock` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:537`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:336` |
| `0x1C` | `f32` | `mMineChance` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:536`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:335` |
| `0x20` | `s32` | `mFireUnk74` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:529`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:328` |
| `0x24` | `s32` | `mFireUnk78` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:531`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:330` |
| `0x28` | `f32` | `acceleration` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:33,133` |
| `0x2C` | `f32` | `friction` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:34,134` |
| `0x30` | `f32` | `unk30` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:35` |
| `0x34` | `s32` | `unk34` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:36` |
| `0x38` | `s32` | `unk38` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:37` |
| `0x3C` | `f32` | `unk3C` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:38` |
| `0x40` | `f32` | `unk40` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:39` |
| `0x44` | `f32` | `unk44` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:40` |
| `0x48` | `f32` | `unk48` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:41` |
| `0x4C` | `s32` | `unk4C` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:42` |
| `0x50` | `f32` | `unk50` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:43` |
| `0x54` | `s32` | `unk54` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:44` |
| `0x58` | `f32` | `maxSpeed` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:45,135` |
| `0x5C` | `f32` | `inputMin` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:46,131` |
| `0x60` | `f32` | `inputMax` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:47,132` |
| `0x64` | `f32` | `turnSpeed` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:48,137` |
| `0x68` | `f32` | `moveAngle` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:49,136` |
| `0x6C` | `s32` | `unk6C` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:50` |
| `0x70` | `f32` | `mAimAngle` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:520`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:319` |
| `0x74` | `s32` | `maxShells` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:52,139`; `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:516` |
| `0x78` | `f32` | `mSelfAngle` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:525`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:324` |
| `0x7C` | `f32` | `mAllyAngle` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:527`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:326` |
| `0x80` | `f32` | `mPlayerAngle` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:526`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:325` |
| `0x84` | `s32` | `shellBounces` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:56,140`; `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:517` |
| `0x88` | `s32` | `mFireIntervalMax` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:524`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:323` |
| `0x8C` | `s32` | `mFireIntervalMin` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:523`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:322` |
| `0x90` | `s32` | `fireCooldown` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:59,138`; `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:515` |
| `0x94` | `f32` | `shellSpeed` | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:60,141`; `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:518` |
| `0x98` | `f32` | `mAimStep` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:521`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:320` |
| `0x9C` | `s32` | `mAimTime` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:522`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:321` |
| `0xA0` | `f32` | `mAllyDist` | `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:528`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:327` |
| `0xA4` | `s32` | `fireMoveDelay` (`mMineUnk78`) | `src/Pack/RPParty/RPTnkScene/RPTnkPlayerController.cpp:64,142`; `src/Pack/RPParty/RPTnkScene/RPTnkAi_4.cpp:519`; `include/Pack/RPParty/RPTnkScene/RPTnkAi.h:318` |

Each stage record is `0x58`: `bonus[2]`, `enemyTypes[2][8]`, and `variants[2][2]`. (`src/Pack/RPParty/RPTnkScene/RPTnkGameManager.cpp:32-40`) The resource manager exposes the archive buffer directly; game code reads it in place. No field is an offset or pointer.

### Pose Mii question parameters

`param/BomQuestionParam.bin` is `0x4B34` and is read in place by `RPBomQuestionParam::Load` (`src/Pack/RPParty/RPBomScene/RPBomQuestionParam.cpp:348-354`):

| Offset | Size | Meaning |
|---:|---:|---|
| `0x0000` | `0x3C` | Header: 13 named 32-bit fields (`mMissZ` through `mBubbleWait`) |
| `0x003C` | `101 * 0x28` | `RPBomQuestionData mQuestions[101]` |
| `0x1004` | `250 * 0x38` | `RPBomPatternData mPatterns[250]` |
| `0x46B4` | `16 * 0x44` | `RPBomLevelData mLevels[16]` |
| `0x4AF4` | `10 * 4` | `s32 mComboPoints[10]` |

Question fields are `mMissNext`, `mCost`, `mPoseEnabled[3]`, `mWaveInterval`, `mWaveNum[2]`, `mSpeed[2]`. Pattern fields are `mSegmentsMax`, `mSegmentsMin`, `mRadiusMax`, `mRadiusMin`, `unk10`, `unk14`, `mQuestion`, `mRotDirMode`, rotation speed range, angle range, and spawn-Z range. Level fields are `unk0`, `mSpecialChance[11]`, `mWaveIntervalScale`, `mTime`, `mStartQuestion`, `mBgFrame`, and `mSpecialGap`. (`include/Pack/RPParty/RPBomScene/RPBomQuestionParam.h:17-87`) No field is an offset or pointer.

## PC loader requirements

- **Little-endian, 32-bit:** swap every 16- and 32-bit integer before interpreting it; reinterpret swapped `u32` words as IEEE-754 `f32` where the structure says `f32`. `GXColor` is four independent bytes and needs no swap. Preserve padding bytes as padding.
- **Little-endian, 64-bit:** use the same byte swaps as 32-bit. None of these layouts contain native pointers, `long`, or pointer-sized fields; alignment differences do not change file offsets.
- **Relocation:** none is required. These formats contain no file offsets converted to pointers. Loaders should instead validate magic, version, declared size, record count, and every nested record boundary before use.
