#include <Pack/RPParty/RPTnkScene/RPTnkStageMap.h>
#include <Pack/RPParty/RPPartyCommon/RPTnkMapData.h>
#include <Pack/RPParty/RPTnkScene/RPTnkBlock.h>
#include <Pack/RPParty/RPTnkScene/RPTnkField.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkLyt.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>

#include <port/be.h>

extern nw4r::math::VEC3 RP_vecZero;
extern u32 RP_randSeedX;
extern u32 RP_randSeedY;
extern RPTnkMapData* RP_tnkMapDataInstance;
extern "C" s32 RP_TnkMapData_getCell(const RPTnkMapData*, s32, s32, s32, s32);

RPTnkStageMap* RPTnkStageMap::sInstance;

namespace {
inline u32 RandomU32() {
    RP_randSeedX = RP_randSeedX * 0x41C64E6D + 12345;
    if (RP_randSeedY & 1) RP_randSeedY ^= 0x11020;
    RP_randSeedY >>= 1;
    return RP_randSeedX ^ RP_randSeedY;
}

inline s32 ChooseEnemyType(s32 type) {
    if (type < 10) return type;
    s32 lo = type / 10;
    s32 hi = type % 10;
    u32 count = hi - (lo - 1);
    return lo + RandomU32() % count;
}

struct EnemyTypes { be_s32 values[8]; };

struct StageParam {
    be_s32 header[2];
    EnemyTypes enemies[2];
    be_s32 tail[4];
};

// param/TnkGameParam.bin: lives, one 0xA8 record per enemy type, then the stage records.
struct GameParam {
    be_s32 lives;
    be_s32 enemyParams[10][0x2A];
    StageParam stages[100];
};
}

void RPTnkStageMap::CreateInstance() {
    sInstance = new RPTnkStageMap;
}

void RPTnkStageMap::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPTnkStageMap::~RPTnkStageMap() {}

// The mode is read as an enum (GetMode): an int puts it in r0 instead of r3.
void RPTnkStageMap::ReadSpawnPositions(s32 stage) {
    RPTnkGameManager::EMode mode = RPTnkGameManager::sInstance->GetMode();
    RPTnkField* grid = RPTnkField::sInstance;
    s32 cols = grid->GetCols();
    s32 rows = grid->GetRows();
    BOOL playerIdx = 0;
    if (mode == 1) playerIdx = 1;
    s32 x;
    s32 y = 0;
    do {
        x = 0;
        do {
            s32 cell = RP_TnkMapData_getCell(mpMapData, stage, x, y, playerIdx);
            switch (cell) {
            case 300:
                mPlayerPos[0] = RPTnkField::sInstance->GetCellPos(x, y);
                break;
            case 301:
                mPlayerPos[1] = RPTnkField::sInstance->GetCellPos(x, y);
                break;
            case 400: case 401: case 402: case 403:
            case 404: case 405: case 406: case 407: {
                s32 enemyIndex = cell - 400;
                mEnemyPos[enemyIndex] = RPTnkField::sInstance->GetCellPos(x, y);
                break;
            }
            }
        } while (++x < cols);
    } while (++y < rows);
}

void RPTnkStageMap::ReadPlayerAngles(s32 stage) {
    if (RPTnkGameManager::sInstance->GetModeIdx() == 1) {
        switch (mpMapData->GetUnk08(stage, 1)) {
        case 0: mPlayerAngles[0] = 0.0f; break;
        case 1: mPlayerAngles[0] = 180.0f; break;
        case 2: mPlayerAngles[0] = 90.0f; break;
        case 3: mPlayerAngles[0] = 270.0f; break;
        }
        switch (mpMapData->GetUnk0C(stage, 1)) {
        case 0: mPlayerAngles[1] = 0.0f; break;
        case 1: mPlayerAngles[1] = 180.0f; break;
        case 2: mPlayerAngles[1] = 90.0f; break;
        case 3: mPlayerAngles[1] = 270.0f; break;
        }
    } else {
        switch (mpMapData->GetUnk08(stage, 0)) {
        case 0: mPlayerAngles[0] = 0.0f; break;
        case 1: mPlayerAngles[0] = 180.0f; break;
        case 2: mPlayerAngles[0] = 90.0f; break;
        case 3: mPlayerAngles[0] = 270.0f; break;
        }
    }
}

// The mode is read as an enum (GetMode): an int puts it in r0 instead of r3.
void RPTnkStageMap::SpawnBlocks(s32 stage) {
    RPTnkField* grid = RPTnkField::sInstance;
    s32 cols = grid->GetCols();
    s32 rows = grid->GetRows();
    nw4r::math::VEC3 pos(0.0f, 0.0f, 0.0f);
    RPTnkGameManager::EMode mode = RPTnkGameManager::sInstance->GetMode();
    BOOL playerIdx = 0;
    if (mode == 1) playerIdx = 1;
    RPTnkBlockPool::sInstance->KillAllBlocks();
    s32 x;
    s32 y = 0;
    do {
        x = 0;
        do {
            s32 cell = RP_TnkMapData_getCell(mpMapData, stage, x, y, playerIdx);
            switch (cell) {
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                pos = RPTnkField::sInstance->GetCellPos(x, y);
                RPTnkBlockPool::sInstance->Spawn(pos, (cell - 100) << 4);
                break;
            case 200: case 201: case 202: case 203:
            case 204: case 205: case 206: case 207:
                pos = RPTnkField::sInstance->GetCellPos(x, y);
                RPTnkBlockPool::sInstance->Spawn(pos, ((cell - 200) << 4) | 1);
                break;
            }
        } while (++x < cols);
    } while (++y < rows);
}

void RPTnkStageMap::Create() {}

void RPTnkStageMap::Load() {
    RPTnkMapData::CreateInstance();
    mpMapData = RP_tnkMapDataInstance;
}

void RPTnkStageMap::Reset() {
    unk8 = 0;
    mPlayerPos[0].x = RP_vecZero.x;
    mPlayerPos[0].y = RP_vecZero.y;
    mPlayerPos[0].z = RP_vecZero.z;
    mPlayerPos[1].x = RP_vecZero.x;
    mPlayerPos[1].y = RP_vecZero.y;
    mPlayerPos[1].z = RP_vecZero.z;
    s32 i = 0;
    do {
        mEnemyPos[i].x = RP_vecZero.x;
        mEnemyPos[i].y = RP_vecZero.y;
        mEnemyPos[i].z = RP_vecZero.z;
        mEnemyTypes[i] = 0;
        mEnemyActive[i] = false;
    } while (++i < 8);
}

void RPTnkStageMap::Exit() {
    RPTnkMapData::DestroyInstance();
}

void RPTnkStageMap::LoadStage(s32 stage, s32 mapStage, BOOL spawnEnemies, BOOL twoPlayers) {
    ReadSpawnPositions(mapStage);
    ReadPlayerAngles(mapStage);
    SpawnBlocks(mapStage);
    if (twoPlayers) mSwapPlayers = (RandomU32() >> 4) & 1;
    else mSwapPlayers = false;
    if (!mSwapPlayers) {
        RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[0], 0, s32(0), TRUE);
        RPTnkLayoutManager::sInstance->GetHud()->PlaceBalloonP1(mPlayerAngles[0]);
        if (twoPlayers) {
            RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[1], 1, s32(0), TRUE);
            RPTnkLayoutManager::sInstance->GetHud()->PlaceBalloonP2(mPlayerAngles[1]);
        }
    } else {
        RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[1], 0, s32(0), TRUE);
        RPTnkLayoutManager::sInstance->GetHud()->PlaceBalloonP1(mPlayerAngles[1]);
        if (twoPlayers) {
            RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[0], 1, s32(0), TRUE);
            RPTnkLayoutManager::sInstance->GetHud()->PlaceBalloonP2(mPlayerAngles[0]);
        }
    }
    if (spawnEnemies) {
        void* param = RPTnkGameManager::sInstance->GetParam();
        StageParam stageParam = static_cast<GameParam*>(param)->stages[stage];
        s32 types[8];
        s32 a0 = stageParam.enemies[0].values[0];
        s32 a1 = stageParam.enemies[0].values[1];
        s32 a2 = stageParam.enemies[0].values[2];
        s32 a3 = stageParam.enemies[0].values[3];
        s32 a4 = stageParam.enemies[0].values[4];
        s32 a5 = stageParam.enemies[0].values[5];
        s32 a6 = stageParam.enemies[0].values[6];
        s32 a7 = stageParam.enemies[0].values[7];
        s32 b0 = stageParam.enemies[1].values[0];
        s32 b1 = stageParam.enemies[1].values[1];
        s32 b2 = stageParam.enemies[1].values[2];
        s32 b3 = stageParam.enemies[1].values[3];
        s32 b4 = stageParam.enemies[1].values[4];
        s32 b5 = stageParam.enemies[1].values[5];
        s32 b6 = stageParam.enemies[1].values[6];
        s32 b7 = stageParam.enemies[1].values[7];
        if (twoPlayers) {
            types[0] = b0; types[1] = b1; types[2] = b2; types[3] = b3;
            types[4] = b4; types[5] = b5; types[6] = b6; types[7] = b7;
        } else {
            types[0] = a0; types[1] = a1; types[2] = a2; types[3] = a3;
            types[4] = a4; types[5] = a5; types[6] = a6; types[7] = a7;
        }
        for (s32 i = 0; i < 8; i++) {
            s32 type = types[i];
            if (type > 0) {
                type = ChooseEnemyType(type);
                mEnemyTypes[i] = type;
                RPTnkTankManager::sInstance->SpawnNextEnemy(mEnemyPos[i], type, i, TRUE);
                mEnemyActive[i] = true;
            } else {
                mEnemyTypes[i] = 0;
                mEnemyActive[i] = false;
            }
        }
    }
}

void RPTnkStageMap::ReloadStage(s32 stage, BOOL spawnEnemies, BOOL twoPlayers) {
    ReadSpawnPositions(stage);
    ReadPlayerAngles(stage);
    SpawnBlocks(stage);
    if (!mSwapPlayers) {
        RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[0], 0, s32(0), TRUE);
        if (twoPlayers)
            RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[1], 1, s32(0), TRUE);
    } else {
        RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[1], 0, s32(0), TRUE);
        if (twoPlayers)
            RPTnkTankManager::sInstance->SpawnPlayer(mPlayerPos[0], 1, s32(0), TRUE);
    }
    if (spawnEnemies) {
        for (s32 i = 0; i < 8; i++) {
            if (mEnemyTypes[i] > 0 && mEnemyActive[i])
                RPTnkTankManager::sInstance->SpawnNextEnemy(mEnemyPos[i], mEnemyTypes[i], i, TRUE);
        }
    }
}
