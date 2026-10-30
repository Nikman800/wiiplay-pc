#ifndef RP_PARTY_COMMON_TNK_MAP_DATA_H
#define RP_PARTY_COMMON_TNK_MAP_DATA_H
#include <Pack/types_pack.h>

#include <port/be.h>

//! @addtogroup rp_party
//! @{

/**
 * @brief Tanks! stage maps: MapData/TnkMapData_P%d_%02d_%d.bin (players 1-2, stage 0-29,
 * variant 0-1)
 * @details vtable 80357448. Size 0x1E4, verified (RP_createTnkMapData). Singleton sInstance
 * (8043C640). The constructor copies every map file out of the scene archive into
 * mpMaps[variant][players - 1][stage]. The accessors take the variant from the global at
 * 8043BBA8.
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPTnkMapData {
public:
    /**
     * @brief Map file header [M]
     */
    struct Map {
        be_s32 width;    // at 0x0
        be_u32 unk4;     // at 0x4 [L]
        be_s32 unk8;     // at 0x8 [L]
        be_s32 unkC;     // at 0xC [L]
        be_s32 cells[1]; // at 0x10 cells[y * width + x]
    };

public:
    static void CreateInstance();
    static void DestroyInstance();

    RPTnkMapData();
    virtual ~RPTnkMapData(); // at 0x8

    s32 GetCell(s32 stage, s32 x, s32 y, s32 playerIdx) const;
    s32 GetUnk08(s32 stage, s32 playerIdx) const;
    s32 GetUnk0C(s32 stage, s32 playerIdx) const;

private:
    Map* mpMaps[2][2][30]; // at 0x4

    static RPTnkMapData* sInstance;
};

//! @}

#endif
