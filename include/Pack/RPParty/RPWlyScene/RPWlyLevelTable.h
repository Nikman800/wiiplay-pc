#ifndef RP_PARTY_WLY_LEVEL_TABLE_H
#define RP_PARTY_WLY_LEVEL_TABLE_H
#include <Pack/types_pack.h>

#include <port/be.h>

#include <nw4r/math.h>

//! @addtogroup rp_wly
//! @{

/**
 * @brief A position in a level record: plain floats (sDefaultLevel is a static aggregate) copied
 * member by member (the default-level copy loads it as floats)
 */
struct RPWlyLevelVec3 {
    be_f32 x, y, z;

    RPWlyLevelVec3& operator=(const RPWlyLevelVec3& rOther) {
        x = rOther.x;
        y = rOther.y;
        z = rOther.z;
        return *this;
    }
};

/**
 * @brief One record of wanted_1p_leveltable.bin / wanted_2p_leveltable.bin
 * @details Size 0x40 (the file size is divided by 0x40). RPWlyLevelTable::sDefaultLevel replaces every
 * record when RPWlyLevelTable::sUseDefaultLevel is set.
 */
struct RPWlyLevelData {
    be_s32 mAvatarNum;              // at 0x0
    be_u32 unk4;                    // at 0x4
    be_s32 mQuestionType;           // at 0x8 0..0x16 (message 0x960 index, target count)
    be_s32 mFieldType;              // at 0xC 0-4 (wtd_field01..09 with the spotlight flag)
    be_f32 unk10;                   // at 0x10 [L]
    be_f32 unk14;                   // at 0x14 [L] != unk10: the stage has an extra message
    be_f32 mCameraTarget;           // at 0x18 camera target height (y or z, by field type)
    be_f32 mSpeed;                  // at 0x1C [M] x1.368 on region 1
    be_f32 unk20;                   // at 0x20 [L] 6.25 on field type 3
    be_f32 mSpotlight;              // at 0x24 non-zero: spotlight stage
    be_f32 mFoundScale;             // at 0x28 [M] avatar scale while found
    RPWlyLevelVec3 mSpotPos;     // at 0x2C
    be_f32 mPlaneY;                 // at 0x38 [M]
    be_f32 mPlaneZ;                 // at 0x3C [M]
};

/**
 * @brief Level tables for one and two players (singleton RPWlyLevelTable::sInstance)
 * @details vtable 8035CB08 (destructor only). Size 0x1C, verified (Wly_LevelTable_createGlobal).
 * The level index is the stage number - 1, remapped in two-player mode by the tier offsets of
 * RPWlyGameConfig, modulo the table's record count.
 */
class RPWlyLevelTable {
public:
    RPWlyLevelTable();
    virtual ~RPWlyLevelTable(); // at 0x8

    void Setup(s32 stageNo);
    // Not const: a const this lets the loads move above the r31 save in the prologue
    s32 GetLevelIndex();
    RPWlyLevelData* GetCurrentLevel() const;
    bool IsSpotlightStage() const;
    bool HasAltMessage() const;

    static void CreateInstance();  // Wly_LevelTable_createGlobal
    static void DestroyInstance(); // Wly_LevelTable_destroyGlobal

public:
    s32 mTable;                 // at 0x4 0 single player, 1 two players
private:
    s32 mStageNo;               // at 0x8
    RPWlyLevelData* mpLevels[2]; // at 0xC copies of the two .bin files
    s32 mLevelNum[2];           // at 0x14

public:
    static RPWlyLevelTable* sInstance; // 8043C988
    static u16 sUseDefaultLevel; // nonzero: every record becomes sDefaultLevel
    static RPWlyLevelData sDefaultLevel;
};

//! @}

#endif
