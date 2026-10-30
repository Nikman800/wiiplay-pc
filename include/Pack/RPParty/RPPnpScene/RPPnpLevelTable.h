#ifndef RP_PARTY_PNP_LEVEL_TABLE_H
#define RP_PARTY_PNP_LEVEL_TABLE_H
#include <Pack/types_pack.h>

#include <port/be.h>

//! @addtogroup rp_pnp
//! @{

/**
 * @brief CPU level and ball speed tables (singleton Pnp_levelTableInstance)
 * @details vtable 80359A08 (destructor only). Size 0x1C, verified (Pnp_createLevelTable). The
 * constructor copies leveltable.bin, rallyleveluptable.bin (single player) and
 * rallyleveluptable2.bin (two players) from the scene archive; every entry is 0xC bytes.
 */
class RPPnpLevelTable {
public:
    struct LevelEntry {
        be_u32 unk0; // at 0x0 [L]
        be_u32 unk4; // at 0x4 [L]
        be_u32 unk8; // at 0x8 [L]
    };

    struct RallyEntry {
        be_s32 mRally;     // at 0x0 first rally count of this step
        be_s32 mCpuLevel;  // at 0x4
        be_f32 mBallSpeed; // at 0x8
    };

public:
    RPPnpLevelTable();
    virtual ~RPPnpLevelTable(); // at 0x8

    static void CreateInstance();
    static void DestroyInstance();
    static RPPnpLevelTable* GetInstance() {
        return sInstance;
    }

    const LevelEntry* GetLevelEntry(s32 idx) const;
    /**
     * @brief Last step whose mRally <= rally, in the table of the current mode
     */
    void LookupRally(s32 rally, f32* pBallSpeed, s32* pCpuLevel) const;

private:
    LevelEntry* mpLevels;         // at 0x4
    s32 mLevelNum;                // at 0x8
    RallyEntry* mpRallyTables[2]; // at 0xC indexed by the RPPnpGame multi-player flag
    s32 mRallyNums[2];            // at 0x14

    static RPPnpLevelTable* sInstance; // 8043C830 (Pnp_levelTableInstance)
};

//! @}

#endif
