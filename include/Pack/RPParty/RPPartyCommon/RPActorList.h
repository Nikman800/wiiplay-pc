#ifndef RP_PARTY_COMMON_ACTOR_LIST_H
#define RP_PARTY_COMMON_ACTOR_LIST_H
#include <Pack/types_pack.h>

#include <Pack/RPParty/RPPartyCommon/RPActor.h>

class RPGrpCamera;
class RPGrpScreen;

//! @addtogroup rp_party
//! @{

/**
 * @brief Priority-sorted intrusive list of RPActor objects, driven by RPActorScene
 * @details vtable 80357640 (abstract: slots 0xC-0x14 are pure). Size 0x1C [H]: members end at
 * 0x18 and the vtable pointer comes last (no virtual declared before the members). The Cow game
 * manager (vtable 8035CE18) derives from it; RPActorScene keeps the instance in
 * RPActorScene::spActorList.
 * @note Wii Play-only framework class; the name is a project convention.
 */
/**
 * @brief List ends and two counters, constructed before RPActorList's vtable pointer is stored
 * @note The name is a project convention (the RPActorList constructor's store order shows a base)
 */
struct RPActorListHead {
    RPActorListHead() : mpHead(NULL), mpTail(NULL), unk8(0), unkA(0) {}

    RPActor* mpHead; // at 0x0
    RPActor* mpTail; // at 0x4
    u16 unk8;        // at 0x8 [L] count
    u16 unkA;        // at 0xA [L]
};

/**
 * @brief Reverse iterator over actor links; the post-increment returns the old link
 * @note Name is a project convention (RPActorList::InsertActor walks the list with it)
 */
struct RPActorLinkRevIt {
    RPActorLink* mp;
    RPActorLinkRevIt(RPActorLink* p) : mp(p) {}
    RPActorLink* operator++(int) {
        RPActorLink* pOld = mp;
        if (mp != NULL) {
            mp = mp->mpPrev;
        }
        return pOld;
    }
};

class RPActorList : public RPActorListHead {
public:
    RPActorList();

    /**
     * @brief Creates a 3D view with a new camera and returns the camera (ledger
     * Gfx_createView3DWithCamera) [H]
     */
    static RPGrpCamera* CreateView3D(u8 viewNo, RPGrpScreen* pScreen);
    /**
     * @brief Creates a 2D view (ledger Gfx_createView2D) [H]
     */
    static void CreateView2D(u8 viewNo, RPGrpScreen* pScreen);
    /**
     * @brief RPGrpModelResManager::CreateResourceList; this is unused (ledger
     * Gfx_createResourceList) [H]
     */
    void CreateResourceList(u16 num);

    /**
     * @brief Stores the capacity and allocates two zeroed RPActor* tables [H]
     */
    void AllocTables(u16 capacity);

    /**
     * @brief Sets the start flags (0xB8) and calls RPActor::OnStart [H]
     */
    void StartActor(RPActor* pActor);
    /**
     * @brief Keeps only the low three flag bits and calls RPActor::OnStop [H]
     */
    void StopActor(RPActor* pActor);
    /**
     * @brief StopActor for every actor with flag 0x80 [H]
     */
    void StopFlaggedActors();
    /**
     * @brief One bubble pass by sort key, then activates pending (0x20) actors [H]
     */
    void SortAndActivate();
    /**
     * @brief RPActor::Calculate for actors flagged active (0x40) and calc enabled (0x10) [H]
     */
    void CalcActive();
    /**
     * @brief RPActor::LoadResource on every actor [H]
     */
    void LoadResourceAll();
    /**
     * @brief RPActor::DrawPass on every active actor [H]
     */
    void DrawPassAll(u8 pass);
    /**
     * @brief RPActor::DrawScreen on every active actor [L]
     * @note Unreferenced (dead-stripped); only its weak RPActor::DrawScreen copy is left
     */
    void DrawScreenAll(u8 pass);

protected:
    /**
     * @brief Links an actor after pPos (NULL: after the tail, or as the head); the list head
     * doubles as the sentinel link. Inlined into the Cow game's actor creation [M]
     * @note The head is the RPActorListHead base (at offset 0 with CodeWarrior; after the
     * vtable pointer with other compilers)
     */
    void LinkActor(RPActorLink* pPos, RPActor* pActor) {
        if (pActor != NULL) {
            if (pPos == NULL) {
                if (mpTail != NULL) {
                    pPos = mpTail;
                } else {
                    pPos = reinterpret_cast<RPActorLink*>(static_cast<RPActorListHead*>(this));
                }
            }
            if (pPos->mpNext != NULL) {
                pPos->mpNext->mpPrev = pActor;
                pActor->mpNext = pPos->mpNext;
            } else {
                pActor->mpNext = NULL;
            }
            if (pActor->mpNext == NULL) {
                mpTail = pActor;
            }
            pPos->mpNext = pActor;
            if (pPos != reinterpret_cast<RPActorLink*>(static_cast<RPActorListHead*>(this))) {
                pActor->mpPrev = static_cast<RPActor*>(pPos);
            } else {
                pActor->mpPrev = NULL;
            }
            pActor->mSortKey = pPos->mSortKey;
            pActor->unkA = 0;
        }
    }
    /**
     * @brief Links an actor after the last one whose sort key is not above key [M]
     */
    void InsertActor(RPActor* pActor, u16 key) {
        RPActorLink* pPos = NULL;
        if (pActor != NULL) {
            RPActorLinkRevIt it(mpTail);
            while (it.mp != NULL) {
                pPos = it++;
                if (pPos->mSortKey <= key) {
                    break;
                }
            }
            LinkActor(pPos, pActor);
            pActor->mSortKey = key;
            pActor->unkA = 0;
        }
    }
    /**
     * @brief Sets the actor's group and index, inserts it by key and calls VF_0x3C [M]
     */
    void RegistActor(RPActor* pActor, u16 group, u16 index, u16 key) {
        pActor->mPriority = group;
        pActor->unk1A = index;
        InsertActor(pActor, key);
        pActor->VF_0x3C();
    }
    /**
     * @brief RegistActor with index 0 (single actors); the zero index is an enum constant,
     * which keeps the original scheduling of its store [M]
     */
    void RegistActorNoIndex(RPActor* pActor, u16 group, u16 key) {
        enum ZeroIndex { Index0 = 0 };
        pActor->mPriority = group;
        ZeroIndex index = Index0;
        pActor->unk1A = index;
        InsertActor(pActor, key);
        pActor->VF_0x3C();
    }

    u32 mCapacity;         // at 0xC
    RPActor** mpTableA;    // at 0x10 [L] mCapacity entries
    s32* mpTableNum;       // at 0x14 actor count of each mpTableA slot

public:
    /**
     * @brief Frees both tables [H]
     */
    virtual ~RPActorList(); // at 0x8
    /**
     * @brief Called by RPActorScene::LoadResource before LoadResourceAll [H]
     */
    virtual void LoadResource() = 0; // at 0xC
    /**
     * @brief Called by RPActorScene::Reset after StopFlaggedActors [H]
     */
    virtual void Reset() = 0; // at 0x10
    /**
     * @brief Called by RPActorScene::Calculate before SortAndActivate [H]
     */
    virtual void Calculate() = 0; // at 0x14
    /**
     * @brief Called by RPActorScene::Calculate after the rumble update, before CalcActive [H]
     */
    virtual void CalculatePost() {} // at 0x18
    /**
     * @brief Cow draws text writers on passes 2, 3 and 4 [M]
     */
    // Wii Play: defined out of line (normal function in RPActorList.cpp)
    virtual void DrawScreen(u8 pass); // at 0x1C
    /**
     * @brief Called by RPActorScene::DebugDraw after DrawPassAll [H]
     */
    virtual void DrawPass(u8 pass) {} // at 0x20
    /**
     * @brief Called by RPActorScene::UserDraw unless paused [H]
     */
    virtual void Draw() {} // at 0x24
};

//! @}

#endif
