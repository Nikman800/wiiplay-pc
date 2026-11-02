#ifndef RP_PARTY_COMMON_ACTOR_H
#define RP_PARTY_COMMON_ACTOR_H
#include <Pack/types_pack.h>

#include <Pack/RPKernel/RP_DEBUG_STUB_1.h>

#include <nw4r/math.h>

//! @addtogroup rp_party
//! @{

// Forward declarations
class RPActor;
class RPGrpModel;

/**
 * @brief Intrusive list link of RPActor, the first (non-polymorphic) base
 * @details Not initialised by RPActor's constructor; RPActorList owns the links.
 * @note Name is a project convention.
 */
struct RPActorLink {
    RPActor* mpNext; // at 0x0
    RPActor* mpPrev; // at 0x4
    u16 mSortKey;    // at 0x8 RPActorList::SortAndActivate keeps ascending order
    u16 unkA;        // at 0xA [L]
};

/**
 * @brief Abstract interface at RPActor+0xC (vtable 803576D8: two pure virtuals)
 * @note Name is a project convention.
 */
class IRPActorPriority {
public:
    virtual u16 GetPriority() const = 0; // at 0x8
    virtual u16 GetUnk1A() const = 0;    // at 0xC
};

/**
 * @brief Interface at RPActor+0x10 (vtable 803576C8: one virtual returning true)
 * @details The collision callback (ledger onCollide): RP_ColliderList_checkHit calls it on a
 * hit collider's owner with the other actor's IRPActorPriority part in r4.
 * @note Name is a project convention.
 */
class IRPActorEnable {
public:
#if defined(RP_ACTOR_DEFINE_VF) && !defined(PORT_HOST)
    // RPActor_2.cpp emits this vtable; the out-of-line copy is in RPActor.cpp
    virtual bool IsEnabled(IRPActorPriority* pOther) { return true; } // at 0x8
#else
    // On the host always this declaration (RPActor_2.cpp's inline copy would be a second
    // definition beside RPActor.cpp's for PE/COFF linkers; port/docs/HOST_BUILD.md §41)
    virtual bool IsEnabled(IRPActorPriority* pOther); // at 0x8 returns true
#endif
};

/**
 * @brief Bits of the first byte of RPActor's flags (bit 0x80 first)
 * @note Name is a project convention.
 */
struct RPActorFlags {
    u8 mStop : 1;     // 0x80 RPActorList::StopFlaggedActors stops the actor
    u8 mActive : 1;   // 0x40 activated (OnActivate called)
    u8 mActivate : 1; // 0x20 activate on the next SortAndActivate
    u8 mCalc : 1;     // 0x10 Calculate enabled
    u8 mStarted : 1;  // 0x08
    u8 : 3;
};

/**
 * @brief Base of the Cow minigame objects, run by RPActorList / RPActorScene
 * @details vtable 80357668 (pointer at 0x20). Size 0x24 [H]: the constructor initialises every
 * member up to 0x20 and the vtable pointer comes last.
 * vtable layout (CodeWarrior): RPActor has no primary base, so its own vtable pointer sits after
 * its members and the full vtable is [header] [IRPActorPriority part, slots 0x10/0x14 as
 * thunks -0xC] [IRPActorEnable part, slot 0x20] [RP_DEBUG_STUB_1 part, dtor thunk -0x14 at 0x2C]
 * then the own section from 0x30. mFlags is zeroed as a u16 but read and written by its first
 * byte: 0x80 stop on scene reset, 0x40 active, 0x20 activate pending, 0x10 calc enabled.
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPActor : public RPActorLink,
                public IRPActorPriority,
                public IRPActorEnable,
                public RP_DEBUG_STUB_1 {
    // RPActorList::RegistActor sets the group and index
    friend class RPActorList;

    // Members are declared before the first own virtual so that the vtable pointer comes after
    // them (at 0x20), as in the constructors.
protected:
    s16 mPriority; // at 0x18
    s16 unk1A;     // at 0x1A [L]
    union {
        u16 mFlags;              // at 0x1C [M] zeroed as a whole by the constructor
        RPActorFlags mFlagBits;  // at 0x1C [M] read and written by its first byte
    };

public:
    RPActor();

    RPActorFlags& GetFlags() {
        return mFlagBits;
    }

    // Model helpers defined in RPActorList.cpp (Cow actors call them; this is unused)
    /**
     * @brief RPGrpModel::UpdateFrame (ledger Gfx_updateModelFrame) [H]
     */
    void UpdateModelFrame(RPGrpModel* pModel);
    /**
     * @brief Sets the model's local matrix and base scale (at least 0.01), then calculates it
     * (ledger Gfx_setModelScale) [H]
     */
    void CalcModel(RPGrpModel* pModel, f32 scale, const nw4r::math::MTX34* pMtx);
    /**
     * @brief RPGrpModel::Entry (ledger Gfx_entryModel) [H]
     */
    void EntryModel(RPGrpModel* pModel);

    virtual u16 GetPriority() const; // at 0x30
    virtual u16 GetUnk1A() const;    // at 0x34
    virtual ~RPActor();              // at 0x38

    // Defined out of line in RPActor_2.cpp (global there). Other units keep the inline form so
    // that their weak copies stay as they are.
#if defined(RP_ACTOR_DEFINE_VF) || defined(PORT_HOST)
    // The host has only the out-of-line copies (RPActor_2.cpp): ELF linkers let them win over
    // the inline ones, PE/COFF's report two definitions (port/docs/HOST_BUILD.md §41).
    virtual void VF_0x3C(); // at 0x3C
    virtual void VF_0x40(); // at 0x40
#else
    virtual void VF_0x3C() {} // at 0x3C
    virtual void VF_0x40() {} // at 0x40
#endif
    /**
     * @brief Called for every actor by RPActorScene::LoadResource [H]
     */
    virtual void LoadResource() {} // at 0x44
    /**
     * @brief Called by RPActorList::StartActor after setting the active flags [H]
     */
    virtual void OnStart() {} // at 0x48
    /**
     * @brief Called by RPActorList::StopActor after clearing the flags [H]
     */
    virtual void OnStop() {} // at 0x4C
    /**
     * @brief Called once when a pending actor becomes active [H]
     */
    virtual void OnActivate() {} // at 0x50
    /**
     * @brief Per-frame update of active, calc-enabled actors [H]
     */
    virtual void Calculate() {} // at 0x54
    /**
     * @brief Screen (layout) drawing for one draw pass; Cow overrides draw layouts on pass 2/3
     * [M]
     */
    virtual void DrawScreen(u8 pass) {} // at 0x58
    /**
     * @brief Called for every active actor from RPActorScene::DebugDraw with the current draw
     * pass (byte 8043C5A5); Cow_Player draws the debug log on pass 4 [M]
     */
    virtual void DrawPass(u8 pass) {} // at 0x5C

};

//! @}

#endif
