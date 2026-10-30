#include <Pack/RPParty/RPWlyScene/RPWlyLevelTable.h>

#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPParty/RPWlyScene/RPWlyGameConfig.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <Pack/RPUtility/RPUtlRandom.h>

#include <egg/gfxe/eggScreen.h>

#include <cstring>

/******************************************************************************
 *
 * RPWlyLevelTable
 *
 ******************************************************************************/

RPWlyLevelTable* RPWlyLevelTable::sInstance;
u16 RPWlyLevelTable::sUseDefaultLevel;

// Defined with the stage code (RPWlyAvatarMgr_2's .sdata2): unk20 of field type 3 stages
extern const f32 Wly_fieldType3Unk20;

// Replaces every record when RPWlyLevelTable::sUseDefaultLevel is set
// (be_* values: on the PC port the members are big-endian wrappers, whose constructor is
// explicit; on the Wii these are plain casts.)
RPWlyLevelData RPWlyLevelTable::sDefaultLevel = {
    be_s32(8), be_u32(0), be_s32(1), be_s32(0),
    be_f32(-130.0f), be_f32(-130.0f), be_f32(-27.0f), be_f32(8.0f),
    be_f32(15.0f), be_f32(0.0f), be_f32(1.5f),
    {be_f32(1.0f), be_f32(1.0f), be_f32(1.0f)},
    be_f32(0.0f), be_f32(0.0f),
};


void RPWlyLevelTable::CreateInstance() {
    sInstance = new RPWlyLevelTable;
}

void RPWlyLevelTable::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPWlyLevelTable::RPWlyLevelTable() {
    const char* names[2] = {"wanted_1p_leveltable.bin", "wanted_2p_leveltable.bin"};

    for (int i = 0; i < 2; i++) {
        u32 size;
        void* pFile = RPSysResourceManager::GetFileFromArchive(
            RPSysResourceManager::LoadGameCommonArchive(-1, NULL), names[i], &size);
        void* pBuf = new u8[size];
        std::memcpy(pBuf, pFile, size);
        mpLevels[i] = static_cast<RPWlyLevelData*>(pBuf);
        mLevelNum[i] = size / sizeof(RPWlyLevelData);

        for (int j = 0; j < mLevelNum[i]; j++) {
            if (RPWlyLevelTable::sUseDefaultLevel) {
                mpLevels[i][j] = RPWlyLevelTable::sDefaultLevel;
            }

            if (EGG::Screen::GetTVMode() == EGG::Screen::TV_MODE_WIDE) {
                mpLevels[i][j].mSpeed *= 1.3684211f;
            }

            if (mpLevels[i][j].mFieldType == 3) {
                mpLevels[i][j].unk20 = Wly_fieldType3Unk20;
            }
        }
    }
}

RPWlyLevelTable::~RPWlyLevelTable() {}

s32 RPWlyLevelTable::GetLevelIndex() {
    s32 idx = mStageNo - 1;

    if (mTable == 1) {
        RPWlyTierOffsets tiers;
        {
            RPWlyGameConfig config = RPSceneInfo::spInstance->mMiiEntry.GetConfig();
            tiers = config.mTierOffsets;
        }

        if (idx < 5) {
            idx = idx % 5 + tiers.mTier[0] * 5;
        } else if (idx < 10) {
            idx = tiers.mTier[1] * 5 + idx % 5 + 20;
        } else if (idx < 15) {
            idx = tiers.mTier[2] * 5 + idx % 5 + 40;
        } else {
            idx = tiers.mTier[3] * 5 + idx % 5 + 60;
        }
    }

    return idx;
}

// Out of line in RPWlyScene.cpp: stores the progress back (a setter taking the progress by
// value; the real name is unknown)

void RPWlyLevelTable::Setup(s32 stageNo) {
    mStageNo = stageNo;

    if (mTable == 1) {
        RPWlyGameConfig config = RPSceneInfo::spInstance->mMiiEntry.GetConfig();

        if (mStageNo == 1) {
            for (int i = 0; i < 4; i++) {
                s32 prev = config.mTierOffsets.mTier[i];
                if (prev >= 0 && prev < 4) {
                    s32 tier = RPUtlRandom::getU32(3);
                    if (prev == tier) {
                        tier++;
                    }
                    config.mTierOffsets.mTier[i] = tier;
                } else {
                    config.mTierOffsets.mTier[i] = RPUtlRandom::getU32(4);
                }
            }

            RPSceneInfo::spInstance->mMiiEntry.SetConfig(config);
        } else if (mStageNo > 20 && mStageNo % 5 == 1) {
            s32 prev = config.mTierOffsets.mTier[3];
            if (prev >= 0 && prev < 4) {
                s32 tier = RPUtlRandom::getU32(3);
                if (prev == tier) {
                    tier++;
                }
                config.mTierOffsets.mTier[3] = tier;
            } else {
                config.mTierOffsets.mTier[3] = RPUtlRandom::getU32(4);
            }

            RPSceneInfo::spInstance->mMiiEntry.SetConfig(config);
        }
    }
}

RPWlyLevelData* RPWlyLevelTable::GetCurrentLevel() const {
    // NONMATCHING-DEBT: GetLevelIndex only reads, but declaring it const (ledger rename to
    // ...CFv) reorders a store in one of its callers (tested 2026-10-29), so it stays non-const.
    s32 idx = const_cast<RPWlyLevelTable*>(this)->GetLevelIndex();
    return &mpLevels[mTable][idx % mLevelNum[mTable]];
}

bool RPWlyLevelTable::HasAltMessage() const {
    RPWlyLevelData* pLevel = GetCurrentLevel();
    return pLevel->unk14 != pLevel->unk10;
}

bool RPWlyLevelTable::IsSpotlightStage() const {
    RPWlyLevelData* pLevel = GetCurrentLevel();
    return pLevel->mSpotlight != 0.0f;
}
