#ifndef RP_PARTY_BOM_PANEL_POOL_3_H
#define RP_PARTY_BOM_PANEL_POOL_3_H

#include <Pack/types_pack.h>

#include <nw4r/math.h>

#include <Pack/RPParty/RPBomScene/RPBomObjPool.h>

//! @addtogroup rp_bom
//! @{

/**
 * @brief Pool of the 16 RPBomPanel bubbles (singleton RPBomPanelPool::sInstance)
 * @details vtable 8035F620 (dtk sizes it 0x58: one padding word). Size 0x18 (adds nothing to
 * RPBomObjPool). Destroy (RPBomPanel.cpp) calls DestroyInstance.
 */
class RPBomPanelPool : public RPBomObjPool {
public:
    static RPBomPanelPool* sInstance; // 8043CAD0

    virtual ~RPBomPanelPool(); // at 0x8

    virtual RPBomPoolObj* CreateItem(); // at 0x24 new RPBomPanel
    virtual void Destroy();             // at 0x2C DestroyInstance
    virtual void PostCalcItems();       // at 0x3C

    f32 GetMaxFallingZ();
    s32 CountSpecialInRange();
    s32 CountActiveInRange();
    u8 GetPanelTransform(nw4r::math::VEC3& rPos, RPAxes& rRot, s32& rColorType,
                         s32 index);
    bool IsPositionOccupied(const nw4r::math::VEC3& rPos, f32 radius);
    void MissFrontPanel();
    void MissAll();
    RPBomPoolObj* SpawnPanel(const nw4r::math::VEC3& rPos, u32 flags,
                             RPBomSpawnInfo info);

    static void CreateInstance();
    static void DestroyInstance();

private:
#if defined(PORT_HOST)
    // The offsets below are RPModelObj's 32-bit layout (a 4-byte vtable pointer first); the
    // host's 8-byte vtable pointer moves every member by 4
    static const u32 MODEL_OBJ_SHIFT = sizeof(void*) - 4;
#else
    static const u32 MODEL_OBJ_SHIFT = 0;
#endif

    // RPModelObj's protected members, read by value as the original does
    static nw4r::math::VEC3 GetPos(const RPBomPoolObj* pObj) {
        return *reinterpret_cast<const nw4r::math::VEC3*>(
            reinterpret_cast<const u8*>(pObj) + MODEL_OBJ_SHIFT + 0x4);
    }

    static RPAxes GetRot(const RPBomPoolObj* pObj) {
        return *reinterpret_cast<const RPAxes*>(reinterpret_cast<const u8*>(pObj) + MODEL_OBJ_SHIFT + 0x1C);
    }

    static u8 GetUnk40(const RPBomPoolObj* pObj) {
        return *(reinterpret_cast<const u8*>(pObj) + MODEL_OBJ_SHIFT + 0x40);
    }
};

//! @}

#endif
