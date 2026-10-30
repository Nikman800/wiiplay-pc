#ifndef RP_AUDIO_SND_OBJECT_H
#define RP_AUDIO_SND_OBJECT_H
#include <Pack/types_pack.h>

#include <Pack/RPAssert.h>
#include <Pack/RPAudio/RPSndMoveParam.h>
#include <Pack/RPAudio/RPSndUtility.h>

#include <egg/audio.h>
#include <egg/core.h>

#include <nw4r/snd.h>
#include <nw4r/ut.h>

//! @addtogroup rp_audio
//! @{

/******************************************************************************
 *
 * RPSndAudioActorBase
 *
 ******************************************************************************/

/**
 * @brief Base class for managed audio actors
 */
class RPSndAudioActorBase : public EGG::Disposer {
    friend class RPSndObjMgr;

public:
    // Wii Play: the flags are assigned after mVolume is constructed (RPHkyPaddle::Create)
    RPSndAudioActorBase(UNKWORD arg0) {
        mIsSilenced = false;
        WORD_0x14 = arg0;
        nw4r::ut::List_Append(&sActorList, this);
    }

    virtual ~RPSndAudioActorBase() { // at 0x8
        nw4r::ut::List_Remove(&sActorList, this);
    }

    virtual void stopSound(u32 id, int frames) = 0;            // at 0xC
    virtual void stopSound(ui32 id, int frames) = 0;   // at 0x10
    virtual void stopSound(const char* pName, int frames) = 0; // at 0x14
    virtual void stopAllSound(int frames) = 0;                 // at 0x18

    virtual void setActorVolume(f32 volume, int frames) = 0; // at 0x1C
    virtual void update() = 0;                               // at 0x20

    static void initList() {
        NW4R_UT_LIST_INIT(sActorList, RPSndAudioActorBase);
    }

    static nw4r::ut::List& getActorList() {
        return sActorList;
    }

protected:
    bool mIsSilenced;  // at 0x10
    UNKWORD WORD_0x14; // at 0x14

public:
    NW4R_UT_LIST_LINK_DECL(); // at 0x18

protected:
    RPSndMoveValueF32 mVolume; // at 0x20

private:
    static nw4r::ut::List sActorList;
};

/******************************************************************************
 *
 * TRPSndAudioHandles
 *
 ******************************************************************************/

template <int N> class TRPSndAudioHandles {
public:
    TRPSndAudioHandles() {
        mHandleCount = N;
        unkC = N;
    }

    ~TRPSndAudioHandles() {
        mHandleCount = 0;
        unkC = 0;
    }

    virtual f32 getOuterVolume() {
        return 1.0f;
    } // at 0x8

    virtual f32 getOuterPitch() {
        return 1.0f;
    } // at 0xC

protected:
    s32 mHandleCount;
    s32 unkC;
    nw4r::snd::SoundHandle mSoundHandles[N];
};

/******************************************************************************
 *
 * TRPSndAudioActorBaseWithHandles
 *
 ******************************************************************************/

template <int N1, int N2>
class TRPSndAudioActorBaseWithHandles : public RPSndAudioActorBase,
                                        public TRPSndAudioHandles<N1 + N2> {
public:
    TRPSndAudioActorBaseWithHandles(UNKWORD arg0) : RPSndAudioActorBase(arg0) {}

    // Wii Play: declared before get3DPosition, so a derived sound actor's destructor thunk
    // comes before the get3DPosition thunk (RPFshFish 0x80205A54/5C, RPWlySound)
    virtual ~TRPSndAudioActorBaseWithHandles() {}

    // Wii Play: appended after both base tables, i.e. at 0x34 from the primary vtable pointer
    // (0x10 in the TRPSndAudioHandles part); vtables 80358A40 (<4, 0>) and 8035C798 (<4, 4>)
    virtual const nw4r::math::VEC3* get3DPosition(int /* idx */) { // at 0x10
        return NULL;
    }

    // Wii Play: the stopping indexed handle lookup of TRPSndObject's start / prepare overloads
    // and RPSndObject2D's (RPWlySound_2), inlined; one weak copy of its assertion strings
    // (RPFshFish's, 0x80358AAC). No named handle local (registers in RPSndObject2D).
    nw4r::snd::SoundHandle* getIndexedHandleStop(int index) {
#line 159
        RP_ASSERT(index<mHandleCount);
        if (index < this->mHandleCount) {
            if (this->mSoundHandles[index].IsAttachedSound()) {
                this->mSoundHandles[index].Stop(0);
            }
            return &this->mSoundHandles[index];
        }
        return NULL;
    }

protected:
    // The assertion names the member without this->; the base is dependent.
    using TRPSndAudioHandles<N1 + N2>::mHandleCount;
};

/******************************************************************************
 *
 * TRPSndAudio3DActor
 *
 ******************************************************************************/

template <int N>
class TRPSndAudio3DActor : public EGG::AudioSoundActorBaseWithCamera<N> {
protected:
    // The assertion names the member without this->; the base is dependent.
    using EGG::AudioSoundActorBaseWithCamera<N>::smCommon3DManager;

public:
    TRPSndAudio3DActor(nw4r::snd::SoundArchivePlayer& rPlayer) {
#ifdef RP_FSH_PLAYER_SOUND_PLAYER_TEMP
        // Wii Play: keep the inlined player's sound constructor's original registers.
        struct PlayerReference {
            nw4r::snd::SoundArchivePlayer& player;
            operator nw4r::snd::SoundArchivePlayer&() const {
                nw4r::snd::SoundArchivePlayer& result = player;
                return result;
            }
        };
        PlayerReference rP = {rPlayer};
#endif
        for (int i = 0; i < N; i++) {
#line 375
            RP_ASSERT(smCommon3DManager[i]);

            mSound3DActors[i] =
#ifdef RP_FSH_PLAYER_SOUND_PLAYER_TEMP
                new nw4r::snd::Sound3DActor(rP, *smCommon3DManager[i]);
#else
                new nw4r::snd::Sound3DActor(rPlayer, *smCommon3DManager[i]);
#endif
        }
    }

protected:
    nw4r::snd::Sound3DActor* mSound3DActors[N];
};

/******************************************************************************
 *
 * TRPSndObject
 *
 ******************************************************************************/

/**
 * @details Wii Play: four 3D actors (one per remote) and RP_MAX_CONTROLLERS + N sound handles.
 * RPSndObject = TRPSndObject<4> (8 handles, vtable 8035C6BC, 0x84 bytes) is the same as in Wii
 * Sports; Wii Play also instantiates TRPSndObject<0> (4 handles, vtable 80358964, 0x74 bytes,
 * the minigame sound actor of Fishing, Hockey, Table Tennis, Shooting and Tanks) and
 * TRPSndObject<8> (12 handles, vtable 8035E074, 0x94 bytes, Billiards). All three share the
 * slot layout below and the RPSndObject.h assertions (smCommon3DManager[i], line 375;
 * index<mHandleCount).
 * The class has no primary base (the 3D actor part has no vtable), so CodeWarrior adds its own
 * vtable pointer after the bases (0x10 + sizeof(TRPSndAudioActorBaseWithHandles)) and the
 * vtable block holds the base copies first: RPSndAudioActorBase part at 0x8, handles part at
 * 0x2C, then every virtual declared here from 0x40 (offsets below are from the block start,
 * as the code calls them through the own vtable pointer). Wii Play's block is 0xDC bytes
 * (Wii Sports' __vt__15TRPSndObject<4> is 0xD0).
 */
/**
 * @brief Wii Play: the two bases of TRPSndObject (name is a project convention)
 * @details Inferred from the destructors: after TRPSndObject's body, the base part is
 * null-checked at offset 0 before TRPSndAudioActorBaseWithHandles at 0x10 (out-of-line
 * Bil_Sound3DBase_dtor 802C7A88, RPHkyPuckTrail / RPHkyPaddleTrail). No virtuals of its own; its
 * vtable is never stored, so the linker drops it.
 */
template <int N>
class TRPSndObjectBase : public TRPSndAudio3DActor<RP_MAX_CONTROLLERS>,
                         public TRPSndAudioActorBaseWithHandles<RP_MAX_CONTROLLERS, N> {
public:
    TRPSndObjectBase(nw4r::snd::SoundArchivePlayer& rPlayer)
        : TRPSndAudio3DActor<RP_MAX_CONTROLLERS>(rPlayer),
          TRPSndAudioActorBaseWithHandles<RP_MAX_CONTROLLERS, N>(0) {}
};

template <int N> class TRPSndObject : public TRPSndObjectBase<N> {
public:
    enum RemoteFlag {
        FLAG_REMOTE_1 = 1 << 0,
        FLAG_REMOTE_2 = 1 << 1,
        FLAG_REMOTE_3 = 1 << 2,
        FLAG_REMOTE_4 = 1 << 3,

        FLAG_REMOTE_ALL =
            FLAG_REMOTE_1 | FLAG_REMOTE_2 | FLAG_REMOTE_3 | FLAG_REMOTE_4
    };

public:
    // Wii Play: base arguments are <RP_MAX_CONTROLLERS> and <RP_MAX_CONTROLLERS, N> (was <N> and
    // <N, N>; identical for N = 4)
    TRPSndObject(nw4r::snd::SoundArchivePlayer& rPlayer,
                 nw4r::snd::Sound3DManager& /* rManager */,
                 u32 remoteFlag = FLAG_REMOTE_ALL)
        : TRPSndObjectBase<N>(rPlayer),
          BOOL_0x7C(false),
          mRemoteFlag(remoteFlag) {}

    // Wii Play: slot order checked against vtables 80358964, 8035C6BC and 8035E074 (Wii Sports
    // had calc before update and a shorter list); parameter names are inferred

    virtual ~TRPSndObject(); // at 0x40

    virtual const nw4r::math::VEC3* get3DPosition(int idx);   // at 0x44 sound 3D actor position
    virtual void set3DPosition(const nw4r::math::VEC3& rPos); // at 0x48 all four actors
    virtual void update();                                    // at 0x4C track volumes

    virtual nw4r::snd::SoundHandle*
    startSoundWithRemotePlayer(u32 id, u32 remoteFlag, s32 player); // at 0x50
    virtual nw4r::snd::SoundHandle*
    startSoundWithRemotePlayer(ui32 id, u32 remoteFlag, s32 player); // at 0x54
    virtual nw4r::snd::SoundHandle*
    startSoundWithRemotePlayer(const char* pLabel, u32 remoteFlag, s32 player); // at 0x58

    // Wii Play: the second argument is the 3D actor (player) index
    virtual nw4r::snd::SoundHandle* startSound(u32 id, u32 handle = 0); // at 0x5C
    virtual nw4r::snd::SoundHandle* startSound(ui32 id,
                                               u32 handle = 0); // at 0x60
    virtual nw4r::snd::SoundHandle* startSound(u32 id, int idx,
                                               u32 handle); // at 0x64 into mSoundHandles[idx]
    virtual nw4r::snd::SoundHandle* startSound(const char* pLabel,
                                               u32 handle = 0); // at 0x68
    virtual nw4r::snd::SoundHandle* startSound(const char* pLabel, int idx,
                                               u32 handle); // at 0x6C

    virtual nw4r::snd::SoundHandle*
    holdSoundWithRemotePlayer(u32 id, u32 remoteFlag, s32 player); // at 0x70
    virtual nw4r::snd::SoundHandle*
    holdSoundWithRemotePlayer(ui32 id, u32 remoteFlag, s32 player); // at 0x74
    virtual nw4r::snd::SoundHandle*
    holdSoundWithRemotePlayer(const char* pLabel, u32 remoteFlag, s32 player); // at 0x78

    virtual nw4r::snd::SoundHandle* holdSound(u32 id, u32 handle = 0);          // at 0x7C
    virtual nw4r::snd::SoundHandle* holdSound(ui32 id, u32 handle = 0); // at 0x80
    virtual nw4r::snd::SoundHandle* holdSound(u32 id, int idx, u32 handle);     // at 0x84
    virtual nw4r::snd::SoundHandle* holdSound(const char* pLabel,
                                              u32 handle = 0); // at 0x88
    virtual nw4r::snd::SoundHandle* holdSound(const char* pLabel, int idx,
                                              u32 handle); // at 0x8C

    virtual nw4r::snd::SoundHandle* prepareSound(u32 id, u32 handle = 0); // at 0x90
    virtual nw4r::snd::SoundHandle* prepareSound(ui32 id,
                                                 u32 handle = 0);           // at 0x94
    virtual nw4r::snd::SoundHandle* prepareSound(u32 id, int idx, u32 handle); // at 0x98
    virtual nw4r::snd::SoundHandle* prepareSound(const char* pLabel,
                                                 u32 handle = 0); // at 0x9C
    virtual nw4r::snd::SoundHandle* prepareSound(const char* pLabel, int idx,
                                                 u32 handle); // at 0xA0

    virtual void stopSound(u32 id, int frames);            // at 0xA4
    virtual void stopSound(ui32 id, int frames);   // at 0xA8
    virtual void stopSound(const char* pName, int frames); // at 0xAC
    virtual void stopAllSound(int frames);                 // at 0xB0

    virtual void setActorVolume(f32 volume, int frames); // at 0xB4

    virtual f32 getOuterVolume(); // at 0xB8 mVolume's current value

    // Wii Play: names below are a project convention
    virtual void detailStartSound(nw4r::snd::SoundHandle* pHandle, u32 id,
                                  u32 handle); // at 0xBC
    virtual void detailHoldSound(nw4r::snd::SoundHandle* pHandle, u32 id,
                                 u32 handle); // at 0xC0
    virtual void detailPrepareSound(nw4r::snd::SoundHandle* pHandle, u32 id,
                                    u32 handle); // at 0xC4
    virtual void detailStartSoundWithRemotePlayer(nw4r::snd::SoundHandle* pHandle, u32 id,
                                                  u32 remoteFlag,
                                                  s32 player); // at 0xC8
    virtual void detailHoldSoundWithRemotePlayer(nw4r::snd::SoundHandle* pHandle, u32 id,
                                                 u32 remoteFlag,
                                                 s32 player); // at 0xCC

    virtual u32 getSoundId(u32 id, int offset); // at 0xD0 id + offset
    virtual const char* getSoundLabel(const char* pLabel,
                                      int offset); // at 0xD4 label of (id of pLabel) + offset
    virtual u32 getSoundId(ui32 id, int offset); // at 0xD8 calls the u32 overload

public:
    // Wii Play: the indexed overloads' handle lookup, inlined; their assertion strings are
    // the .data of these weak copies (after the vtables). This one is used by holdSound; the
    // stopping one (startSound / prepareSound) is TRPSndAudioActorBaseWithHandles'.
    nw4r::snd::SoundHandle* getIndexedHandle(int index) {
#line 172
        RP_ASSERT(index<mHandleCount);
        if (index < this->mHandleCount) {
            return &this->mSoundHandles[index];
        }
        return NULL;
    }

protected:
    // Wii Play: at 0x6C / 0x7C / 0x8C for N = 0 / 4 / 8
    bool BOOL_0x7C;
    u32 mRemoteFlag; // RemoteFlag mask passed to RPSndSpeakerMgr::setRemoteSend

    // The assertion names the member without this->; the base is dependent.
    using TRPSndObjectBase<N>::mHandleCount;
};

// Wii Play: inlined into the derived sound actors' destructors (RPHkyPaddleTrail). The player is
// RPSndUtility's (8043C5E8), as in Wii Sports; Wii Play's EGG has no AudioSound3DActor player.
template <int N> inline TRPSndObject<N>::~TRPSndObject() {
    for (u32 i = 0; i < RPSndUtility::getPlayer()->GetSoundPlayerCount(); i++) {
        RPSndUtility::getPlayer()->GetSoundPlayer(i).StopAllSound(0);
    }
}

//! Shorthand for default specialization
typedef TRPSndObject<RP_MAX_CONTROLLERS> RPSndObject;

/**
 * @brief Wii Play: unidentified data-less polymorphic class whose inline constructor and
 * destructor are generated by TRPSndGameObject's constructor (name and slots are placeholders)
 * @details Reconstructed from .data alone: units that construct a TRPSndGameObject<N> (Tanks
 * RPTnkMine_2, Bomb player / BomPanel_2) carry an extra 0x90 bytes of zero-filled duplicate weak
 * data between TRPSndAudioHandles and RPSndAudioActorBase, which RPDucGame (TRPSndObject<0>
 * directly) lacks. A non-template class declared after RPSndAudioActorBase puts its weak vtable
 * exactly there; 0x90 = header 8 + destructor + 33 virtuals. It is never stored, so the linker
 * drops the first copy. Its real identity and virtuals are unknown.
 * FAKEMATCH: the DOL matches, but this class is invented to fill the 0x90 bytes; no surviving
 * copy exists in Wii Play or Wii Sports (see docs/PHASE4_PENDING.md). Replace it if found.
 */
class RPSndGameObjectHelper {
public:
    RPSndGameObjectHelper() {}
    virtual ~RPSndGameObjectHelper() {} // at 0x8
    virtual void unk0C() {}
    virtual void unk10() {}
    virtual void unk14() {}
    virtual void unk18() {}
    virtual void unk1C() {}
    virtual void unk20() {}
    virtual void unk24() {}
    virtual void unk28() {}
    virtual void unk2C() {}
    virtual void unk30() {}
    virtual void unk34() {}
    virtual void unk38() {}
    virtual void unk3C() {}
    virtual void unk40() {}
    virtual void unk44() {}
    virtual void unk48() {}
    virtual void unk4C() {}
    virtual void unk50() {}
    virtual void unk54() {}
    virtual void unk58() {}
    virtual void unk5C() {}
    virtual void unk60() {}
    virtual void unk64() {}
    virtual void unk68() {}
    virtual void unk6C() {}
    virtual void unk70() {}
    virtual void unk74() {}
    virtual void unk78() {}
    virtual void unk7C() {}
    virtual void unk80() {}
    virtual void unk84() {}
    virtual void unk88() {}
    virtual void unk8C() {}
};

/**
 * @brief Wii Play: empty level between TRPSndObject and a minigame's sound actor (name is a
 * project convention)
 * @details Inferred from the destructors: RPHkyPuckTrail, RPHkyPaddleTrail and Bil_Sound3D_dtor
 * (802C7C40) null-check three times before TRPSndObject's body. Its vtable is never stored.
 */
template <int N> class TRPSndGameObject : public TRPSndObject<N> {
public:
    TRPSndGameObject(nw4r::snd::SoundArchivePlayer& rPlayer, nw4r::snd::Sound3DManager& rManager,
                     u32 remoteFlag = TRPSndObject<N>::FLAG_REMOTE_ALL)
        : TRPSndObject<N>(rPlayer, rManager, remoteFlag) {
        RPSndGameObjectHelper helper; // FAKEMATCH: placeholder, see RPSndGameObjectHelper
    }
};

/******************************************************************************
 *
 * TRPSndObject members
 *
 ******************************************************************************/

// Wii Play: defined in the header, so every unit that emits a TRPSndObject<N> vtable instantiates
// weak copies (the linker keeps the first, RPFshFish's for N = 0, and zero-fills the others' data).
#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPAudio/RPSndSpeakerMgr.h>

namespace {

inline nw4r::snd::SoundHandle* FindFishSoundHandle(nw4r::snd::SoundHandle* pHandles,
                                                 int count, u32 id) {
    for (int i = 0; i < count; i++) {
        if (pHandles[i].IsAttachedSound() && pHandles[i].GetId() == id) {
            return &pHandles[i];
        }
    }
    return NULL;
}

inline nw4r::snd::SoundHandle* FindFreeFishSoundHandle(nw4r::snd::SoundHandle* pHandles,
                                                     int count) {
    for (int i = 0; i < count; i++) {
        if (!pHandles[i].IsAttachedSound()) {
            return &pHandles[i];
        }
    }
    return NULL;
}

inline nw4r::snd::SoundHandle* FindPriorityFishSoundHandle(nw4r::snd::SoundHandle* pHandles,
                                                         const s32& count, u32 id) {
    int priority;
    int index;
    int i;
    const nw4r::snd::SoundArchive& rArchive = RPSndUtility::getPlayer()->GetSoundArchive();
    nw4r::snd::SoundArchive::SoundInfo info;
    rArchive.ReadSoundInfo(pHandles[0].GetId(), &info);
    priority = info.playerPriority;
    index = 0;
    for (i = 1; i < count; i++) {
        rArchive.ReadSoundInfo(pHandles[i].GetId(), &info);
        if (priority > info.playerPriority) {
            priority = info.playerPriority;
            index = i;
        }
    }
    rArchive.ReadSoundInfo(id, &info);
    if (priority > info.playerPriority) {
        return &pHandles[index];
    }
    return NULL;
}

// Wii Play: the named local matters; it puts the label/id in r31 (index/handle below it) in the
// const char* start/prepare overloads and the indexed hold overload (RPFshFish).
inline u32 LabelToSoundId(const char* pLabel) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    return id;
}

template <bool Stop>
inline nw4r::snd::SoundHandle* GetIndexedFishSoundHandle(nw4r::snd::SoundHandle* pHandles,
                                                       const s32& count, int index) {
    static char message[] = "index<mHandleCount";
    static char file[] = "RPSndObject.h";
    if (index >= count) {
        system_halt(file, Stop ? 159 : 172, message);
    }
    if (index < count) {
        nw4r::snd::SoundHandle* pHandle = &pHandles[index];
        if (Stop && pHandle->IsAttachedSound()) {
            pHandle->Stop(0);
        }
        return pHandle;
    }
    return NULL;
}

} // namespace

// Wii Play: only RPFshFish.cpp (the unit holding the binary's single copy of the weak
// @STRING@getIndexedHandle* literals, 0x80358AAC) uses the inline members; every other unit
// keeps the file-local helper, whose literals are dropped with its dead weak copies (the
// member form zero-fills a 0x40-byte duplicate in RPDucGame's .data).
#ifdef RP_SND_OBJECT_INDEXED_HANDLE_MEMBERS
#define RP_SND_INDEXED_HANDLE(index) getIndexedHandle(index)
#define RP_SND_INDEXED_HANDLE_STOP(index) getIndexedHandleStop(index)
#else
#define RP_SND_INDEXED_HANDLE(index) \
    GetIndexedFishSoundHandle<false>(this->mSoundHandles, this->mHandleCount, index)
#define RP_SND_INDEXED_HANDLE_STOP(index) \
    GetIndexedFishSoundHandle<true>(this->mSoundHandles, this->mHandleCount, index)
#endif

template <int N>
f32 TRPSndObject<N>::getOuterVolume() {
    return this->mVolume.getValue();
}

template <int N>
void TRPSndObject<N>::setActorVolume(f32 volume, int frames) {
    this->mVolume.moveValue(volume, frames);
}

template <int N>
void TRPSndObject<N>::update() {
    if (this->mVolume.doUpdate()) {
        for (int i = 0; i < this->mHandleCount; i++) {
            if (this->mSoundHandles[i].IsAttachedSound()) {
                nw4r::snd::SeqSoundHandle handle(&this->mSoundHandles[i]);
                handle.SetTrackVolume(0xFFFF, this->mVolume.getValue());
            }
        }
    }
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSound(u32 id, u32 handle) {
    nw4r::snd::SoundHandle* pHandle =
        FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    if (pHandle == NULL) {
        if (this->mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle != NULL) {
        detailHoldSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSound(ui32 id, u32 handle) {
    nw4r::snd::SoundHandle* pHandle =
        FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    if (pHandle == NULL) {
        if (this->mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle != NULL) {
        detailHoldSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSound(const char* pLabel, u32 handle) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle =
            FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        if (pHandle == NULL) {
            if (this->mIsSilenced) {
                return NULL;
            }
            pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
        }
        if (pHandle != NULL) {
            detailHoldSound(pHandle, id, handle);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSoundWithRemotePlayer(u32 id, u32 remoteFlag, s32 player) {
    nw4r::snd::SoundHandle* pHandle =
        FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    if (pHandle == NULL) {
        if (this->mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle != NULL) {
        detailHoldSoundWithRemotePlayer(pHandle, id, remoteFlag, player);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSoundWithRemotePlayer(ui32 id, u32 remoteFlag, s32 player) {
    nw4r::snd::SoundHandle* pHandle =
        FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    if (pHandle == NULL) {
        if (this->mIsSilenced) {
            return NULL;
        }
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle != NULL) {
        detailHoldSoundWithRemotePlayer(pHandle, id, remoteFlag, player);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSoundWithRemotePlayer(const char* pLabel, u32 remoteFlag, s32 player) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle =
            FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        if (pHandle == NULL) {
            if (this->mIsSilenced) {
                return NULL;
            }
            pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
        }
        if (pHandle != NULL) {
            detailHoldSoundWithRemotePlayer(pHandle, id, remoteFlag, player);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSound(u32 id, int index, u32 handle) {
    nw4r::snd::SoundHandle* pHandle =
        RP_SND_INDEXED_HANDLE(index);
    if (pHandle != NULL) {
        detailHoldSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::holdSound(const char* pLabel, int index, u32 handle) {
    u32 id = LabelToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle =
            RP_SND_INDEXED_HANDLE(index);
        if (pHandle != NULL) {
            detailHoldSound(pHandle, id, handle);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSound(u32 id, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailStartSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSound(ui32 id, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailStartSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSound(const char* pLabel, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    u32 id = LabelToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle = NULL;
        if (!BOOL_0x7C) {
            pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        }
        if (pHandle == NULL) {
            pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
        }
        if (pHandle == NULL) {
            pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        }
        if (pHandle != NULL) {
            detailStartSound(pHandle, id, handle);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSoundWithRemotePlayer(u32 id, u32 remoteFlag, s32 player) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailStartSoundWithRemotePlayer(pHandle, id, remoteFlag, player);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSoundWithRemotePlayer(ui32 id, u32 remoteFlag, s32 player) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailStartSoundWithRemotePlayer(pHandle, id, remoteFlag, player);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSoundWithRemotePlayer(const char* pLabel, u32 remoteFlag, s32 player) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    u32 id = LabelToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle = NULL;
        if (!BOOL_0x7C) {
            pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        }
        if (pHandle == NULL) {
            pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
        }
        if (pHandle == NULL) {
            pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        }
        if (pHandle != NULL) {
            detailStartSoundWithRemotePlayer(pHandle, id, remoteFlag, player);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSound(u32 id, int index, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    nw4r::snd::SoundHandle* pHandle =
        RP_SND_INDEXED_HANDLE_STOP(index);
    if (pHandle != NULL) {
        detailStartSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::startSound(const char* pLabel, int index, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    u32 id = LabelToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle =
            RP_SND_INDEXED_HANDLE_STOP(index);
        if (pHandle != NULL) {
            detailStartSound(pHandle, id, handle);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::prepareSound(u32 id, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailPrepareSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::prepareSound(ui32 id, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailPrepareSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::prepareSound(const char* pLabel, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    u32 id = LabelToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle = NULL;
        if (!BOOL_0x7C) {
            pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        }
        if (pHandle == NULL) {
            pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
        }
        if (pHandle == NULL) {
            pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
        }
        if (pHandle != NULL) {
            detailPrepareSound(pHandle, id, handle);
            return pHandle;
        }
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::prepareSound(u32 id, int index, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    nw4r::snd::SoundHandle* pHandle =
        RP_SND_INDEXED_HANDLE_STOP(index);
    if (pHandle != NULL) {
        detailPrepareSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}

template <int N>
nw4r::snd::SoundHandle* TRPSndObject<N>::prepareSound(const char* pLabel, int index, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    u32 id = LabelToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        nw4r::snd::SoundHandle* pHandle =
            RP_SND_INDEXED_HANDLE_STOP(index);
        if (pHandle != NULL) {
            detailPrepareSound(pHandle, id, handle);
            return pHandle;
        }
    }
    return NULL;
}


template <int N>
void TRPSndObject<N>::detailStartSound(nw4r::snd::SoundHandle* pHandle, u32 id, u32 handle) {
    if (pHandle->IsAttachedSound()) {
        pHandle->Stop(0);
    }
    this->mSound3DActors[handle]->StartSound(pHandle, id);
    u32 param = RPSndAudioMgr::instance()->getUserParam(
        RPSndUtility::getPlayer()->GetSoundArchive(), id);
    if (param & 0xFFFF0000) {
        f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
        f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
        f32 remoteOut = (param >> 24) / 127.0f;
        RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag,
                                                   mainOut, remoteOut, muteOut);
    }
}

template <int N>
void TRPSndObject<N>::detailHoldSound(nw4r::snd::SoundHandle* pHandle, u32 id, u32 handle) {
    this->mSound3DActors[handle]->HoldSound(pHandle, id);
    u32 param = RPSndAudioMgr::instance()->getUserParam(
        RPSndUtility::getPlayer()->GetSoundArchive(), id);
    if (param & 0xFFFF0000) {
        f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
        f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
        f32 remoteOut = (param >> 24) / 127.0f;
        RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag,
                                                   mainOut, remoteOut, muteOut);
    }
}

template <int N>
void TRPSndObject<N>::detailPrepareSound(nw4r::snd::SoundHandle* pHandle, u32 id, u32 handle) {
    if (pHandle->IsAttachedSound()) {
        pHandle->Stop(0);
    }
    this->mSound3DActors[handle]->PrepareSound(pHandle, id);
    u32 param = RPSndAudioMgr::instance()->getUserParam(
        RPSndUtility::getPlayer()->GetSoundArchive(), id);
    if (param & 0xFFFF0000) {
        f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
        f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
        f32 remoteOut = (param >> 24) / 127.0f;
        RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag,
                                                   mainOut, remoteOut, muteOut);
    }
}

template <int N>
void TRPSndObject<N>::detailStartSoundWithRemotePlayer(
    nw4r::snd::SoundHandle* pHandle, u32 id, u32 remoteFlag, s32 player) {
    if (pHandle->IsAttachedSound()) {
        pHandle->Stop(0);
    }
    this->mSound3DActors[player]->StartSound(pHandle, id);
    u32 param = RPSndAudioMgr::instance()->getUserParam(
        RPSndUtility::getPlayer()->GetSoundArchive(), id);
    if (param & 0xFFFF0000) {
        f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
        f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
        f32 remoteOut = (param >> 24) / 127.0f;
        RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, remoteFlag,
                                                   mainOut, remoteOut, muteOut);
    }
}

template <int N>
void TRPSndObject<N>::detailHoldSoundWithRemotePlayer(
    nw4r::snd::SoundHandle* pHandle, u32 id, u32 remoteFlag, s32 player) {
    this->mSound3DActors[player]->HoldSound(pHandle, id);
    u32 param = RPSndAudioMgr::instance()->getUserParam(
        RPSndUtility::getPlayer()->GetSoundArchive(), id);
    if (param & 0xFFFF0000) {
        f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
        f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
        f32 remoteOut = (param >> 24) / 127.0f;
        RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, remoteFlag,
                                                   mainOut, remoteOut, muteOut);
    }
}

template <int N>
void TRPSndObject<N>::set3DPosition(const nw4r::math::VEC3& rPos) {
    for (int i = 0; i < RP_MAX_CONTROLLERS; i++) {
        this->mSound3DActors[i]->SetPosition(rPos);
    }
}

template <int N>
const nw4r::math::VEC3* TRPSndObject<N>::get3DPosition(int idx) {
    return &this->mSound3DActors[idx]->GetPosition();
}

template <int N>
u32 TRPSndObject<N>::getSoundId(u32 id, int offset) {
    return id + offset;
}

template <int N>
u32 TRPSndObject<N>::getSoundId(ui32 id, int offset) {
    return getSoundId(static_cast<u32>(id), offset);
}

template <int N>
const char* TRPSndObject<N>::getSoundLabel(const char* pLabel, int offset) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id == static_cast<u32>(-1)) {
        return NULL;
    }
    return RPSndUtility::getPlayer()->GetSoundArchive().GetSoundLabelString(id + offset);
}

template <int N>
void TRPSndObject<N>::stopAllSound(int frames) {
    for (int i = 0; i < this->mHandleCount; i++) {
        if (this->mSoundHandles[i].IsAttachedSound()) {
            this->mSoundHandles[i].Stop(frames);
        }
    }
}

template <int N>
void TRPSndObject<N>::stopSound(u32 id, int frames) {
    for (int i = 0; i < this->mHandleCount; i++) {
        if (this->mSoundHandles[i].IsAttachedSound() && this->mSoundHandles[i].GetId() == id) {
            this->mSoundHandles[i].Stop(frames);
        }
    }
}

template <int N>
void TRPSndObject<N>::stopSound(ui32 id, int frames) {
    for (int i = 0; i < this->mHandleCount; i++) {
        if (this->mSoundHandles[i].IsAttachedSound() && this->mSoundHandles[i].GetId() == id) {
            this->mSoundHandles[i].Stop(frames);
        }
    }
}

template <int N>
void TRPSndObject<N>::stopSound(const char* pLabel, int frames) {
    u32 id = RPSndUtility::getPlayer()->GetSoundArchive().ConvertLabelStringToSoundId(pLabel);
    if (id != static_cast<u32>(-1)) {
        for (int i = 0; i < this->mHandleCount; i++) {
            if (this->mSoundHandles[i].IsAttachedSound() && this->mSoundHandles[i].GetId() == id) {
                this->mSoundHandles[i].Stop(frames);
            }
        }
    }
}

/******************************************************************************
 *
 * RPSndObjMgr
 *
 ******************************************************************************/

/**
 * @brief Sound object/actor manager
 */
class RPSndObjMgr {
public:
    /**
     * @brief Fades out all active sounds in the specified number of frames
     *
     * @param frames Fade out time
     */
    static void stopAllActorSound(int frames);

    /**
     * @brief Updates the state of all active sounds
     */
    static void update() {
        RP_NW4R_LIST_FOREACH(RPSndAudioActorBase, it, RPSndAudioActorBase::sActorList) {
            if (it->mVolume.getFrame() > 0) {
                it->update();
            }
        }
    }
};

//! @}

#endif
