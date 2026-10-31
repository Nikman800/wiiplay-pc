#ifndef RP_PARTY_COMMON_SCENE_INFO_H
#define RP_PARTY_COMMON_SCENE_INFO_H
#include <Pack/types_pack.h>

#include <Pack/RPParty/RPWlyScene/RPWlyGameConfig.h>

#include <egg/math.h>
#include <nw4r/math.h>

class RPSysMiddleDB;

//! @addtogroup rp_party
//! @{

/**
 * @brief Scene-info singleton: player count and per-play settings read by every minigame [M name]
 * @details vtable 803581E8. Size 0xD8 (the ledger's 0xD8 allocations, created by
 * RPPartyRootScene::Configure, deleted by its Exit and RP_SceneInfo_destroyInstance).
 * @note Wii Play-only framework class; the name is a project convention (ledger RP_SceneInfo).
 */
#if defined(PORT_HOST)
// RPSceneInfo_2.cpp defines it with C linkage; GCC needs that before the friend declaration
extern "C" void RP_createSceneInfo();
#endif

class RPSceneInfo {
    friend void RP_createSceneInfo();

public:
    RPSceneInfo();
    virtual ~RPSceneInfo(); // at 0x8

    /**
     * @brief Takes the player count from RPSysPlayerMgr and clears the play settings [H]
     */
    void Reset();

    static void DestroyInstance();

    s32 GetPlayerNum() const {
        return mPlayerNum;
    }

    // Wii Play: Table Tennis two-player start side (0 random, 1 swapped, 2 normal; RPPnpGame::Reset)
    u32 GetUnk1C() const {
        return unk1C;
    }
    void SetUnk1C(u32 value) {
        unk1C = value;
    }

    // Wii Play: two-player select screen (RPSelectGameLyt::Load)
    u32 GetUnk8(u32 idx) const {
        return unk8[idx];
    }

    // Wii Play: cleared by RPSysPauseMgr when Find Mii is left from the pause menu [M]
    void ClearUnk1C() {
        unk1C = 0;
    }

    static RPSceneInfo* spInstance; // 8043C6C8 (RP_sceneInfoInstance)

public:
    s32 mPlayerNum;            // at 0x4 [H] RPSysPlayerMgr::mPlayerNum on Reset
    u32 unk8[2];               // at 0x8 [L] per-player numbers (two-player select screen), 0 on Reset
    u32 unk10;                 // at 0x10 [L]
    s32 unk14;                 // at 0x14 [L] -1 on Reset
    u32 unk18;                 // at 0x18 [L]
    u32 unk1C;                 // at 0x1C [L]
    u32 unk20;                 // at 0x20 [L]
    u32 unk24;                 // at 0x24 [L]
    u32 unk28;                 // at 0x28 [L]
    bool unk2C;                // at 0x2C [L] true on Reset
    RPWlyMiiEntry mMiiEntry;   // at 0x30 [H] dtor called from ~RPSceneInfo
    RPSysMiddleDB* mpMiddleDB; // at 0xD4 [M]
};

/**
 * @brief Holder of a collision model (RPKcl, 0x40 bytes) used by the Fishing stage [M name]
 * @details 4 bytes: the ctor allocates the RPKcl; SetResource relocates the KCL resource.
 * @note Wii Play-only; the name is a project convention (ledger RP_KclHolder).
 */
class RPKcl;
class RPKclHolder {
public:
    RPKclHolder();
    void SetResource(void* pResource);
    /**
     * @brief Push-out vector for a sphere at rPos (ledger RP_KclHolder_calcPushOut) [M]
     */
    EGG::Vector3f CalcPushOut(const nw4r::math::VEC3& rPos, f32 radius) const;

private:
    RPKcl* mpKcl; // at 0x0 [H]
};

//! @}

#endif
