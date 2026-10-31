#ifndef RP_PARTY_COMMON_COLLIDER_LIST_H
#define RP_PARTY_COMMON_COLLIDER_LIST_H
#include <Pack/types_pack.h>

#include <egg/math.h>

class IRPActorPriority;

//! @addtogroup rp_party
//! @{

/**
 * @brief Abstract collision primitive linked into an RPColliderList
 * @details Abstract: no vtable of its own exists; RPCollisionShape (vtable 803575B8) and
 * RPCollisionModel (sub-vtable 80357588) share this slot layout and RPColliderList calls
 * through it. Only Draw has a body (empty, 801C5E24). There is no virtual destructor.
 * Size 0x14 [H].
 * @note Name is a project convention.
 */
class RPCollider {
public:
    RPCollider* mpNext; // at 0x0
    RPCollider* mpPrev; // at 0x4 [L]
    u16 unk8;           // at 0x8 [L]
    u16 mMask;          // at 0xA tested against the query mask
    /**
     * @brief Object whose virtual at 0x8 confirms a hit (returns bool) [M]
     */
    void* mpOwner; // at 0xC

public:
    virtual EGG::Vector3f& GetPos() = 0;            // at 0x8
    virtual EGG::Vector3f& GetRot() = 0;            // at 0xC
    virtual f32 GetRadius() const = 0;               // at 0x10
    /**
     * @brief Sphere test; on a hit fills the push-out position/rotation offsets and the hit
     * flags (any output may be NULL) [M]
     */
    virtual bool CheckHit(f32 radius, const EGG::Vector3f& rPos,
                          const EGG::Vector3f& rPrevPos, EGG::Vector3f* pPosOfs,
                          EGG::Vector3f* pRotOfs, u16* pFlags) = 0; // at 0x14
    virtual void Draw() {}                                         // at 0x18
};

/**
 * @brief Singly walked list of RPCollider
 * @details No vtable. Size 0xC [M]: Charge!'s static initializer (RPCowGame.cpp __sinit)
 * constructs three of them back to back (0x8043A8E8/F4/900), zeroing a word at 0x0 and 0x4
 * and halfwords at 0x8 and 0xA.
 * @note Wii Play-only framework class; the name is a project convention.
 */
class RPColliderList {
public:
#if defined(PORT_HOST)
    RPColliderList() : mHostVptrSlot(NULL), mpHead(NULL), mpTail(NULL), unk8(0), unkA(0) {}
#else
    RPColliderList() : mpHead(NULL), mpTail(NULL), unk8(0), unkA(0) {}
#endif

    /**
     * @brief RPCollider::Draw on every collider [H]
     */
    void DrawAll();
    /**
     * @brief Tests every collider whose mask matches (0 = all) and whose owner confirms; sums
     * the push-out offsets through RPBBox and ORs the flags. Returns the hit count [M]
     */
    s32 CheckHit(IRPActorPriority* pSelf, u32 mask, const EGG::Vector3f& rPos, f32 radius,
                 const EGG::Vector3f& rPrevPos, EGG::Vector3f* pPosOfs,
                 EGG::Vector3f* pRotOfs, u16* pFlags);

    /**
     * @brief Unlinks a collider; NULL is ignored (inlined into every Cow object's OnStop) [H]
     */
    void Remove(RPCollider* pCollider) {
        if (pCollider != NULL) {
            RPCollider* pNext = pCollider->mpNext;
            if (pNext != NULL) {
                pNext->mpPrev = pCollider->mpPrev;
                pCollider->mpNext = NULL;
            } else {
                mpTail = pCollider->mpPrev;
            }
            RPCollider* pPrev = pCollider->mpPrev;
            if (pPrev != NULL) {
                pPrev->mpNext = pNext;
                pCollider->mpPrev = NULL;
            } else {
                mpHead = pNext;
            }
        }
    }

    /**
     * @brief Links a collider after the tail (or as the head) with the given mask
     * (inlined into the Cow objects' OnStart) [M]
     */
    void Append(RPCollider* pCollider, u16 mask) {
        if (pCollider != NULL) {
            RPCollider* pLast = GetLast();
            if (pLast->mpNext != NULL) {
                pLast->mpNext->mpPrev = pCollider;
                pCollider->mpNext = pLast->mpNext;
            } else {
                pCollider->mpNext = NULL;
            }
            if (pCollider->mpNext == NULL) {
                mpTail = pCollider;
            }
            pLast->mpNext = pCollider;
            if (pLast != AsCollider()) {
                pCollider->mpPrev = pLast;
            } else {
                pCollider->mpPrev = NULL;
            }
            pCollider->unk8 = pLast->unk8;
            pCollider->mMask = mask;
        }
    }

private:
    /**
     * @brief The list head doubles as a sentinel collider (mpHead/mpTail at mpNext/mpPrev)
     */
    RPCollider* AsCollider() {
        return reinterpret_cast<RPCollider*>(this);
    }
    RPCollider* GetLast() {
        if (mpTail != NULL) {
            return mpTail;
        }
        return AsCollider();
    }

#if defined(PORT_HOST)
    // The sentinel cast above needs mpHead / mpTail / unk8 where RPCollider has mpNext / mpPrev /
    // unk8. CodeWarrior puts RPCollider's vtable pointer after its members (at 0x10); GCC puts
    // it first, so on the host a pointer-sized slot comes first here. (Without it, an Append to
    // an empty list wrote the new collider over mpTail: Charge!'s cow met no scarecrow, hurdle
    // or goal.) Never read; only the sentinel's vtable slot.
    void* mHostVptrSlot;
#endif
    RPCollider* mpHead; // at 0x0
    RPCollider* mpTail; // at 0x4
    u16 unk8;           // at 0x8 [L]
    u16 unkA;           // at 0xA [L]
};

//! @}

#endif
