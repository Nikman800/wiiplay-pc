#ifndef RP_AUDIO_SND_OBJECT_2D_H
#define RP_AUDIO_SND_OBJECT_2D_H
#include <Pack/types_pack.h>

#include <Pack/RPAudio/RPSndObject.h>

//! @addtogroup rp_audio
//! @{

/**
 * @brief Four-handle sound object without 3D actors (BGM and screen sounds)
 * @details Wii Play only. TRPSndAudioActorBaseWithHandles<4, 0> (the same instantiation as
 * TRPSndObject<0>'s, vtable 80358A40, whose get3DPosition 80203628 it keeps) plus a remote
 * speaker mask. No vtable of its own is used: the constructor is inlined into the derived
 * constructors, which store the derived vtable directly (RPWlySoundObj 8035CB48, RPBilSoundActor
 * 8035DD60; both share every slot but the destructor and calc). Layout: RPSndAudioActorBase at
 * 0x0 (mIsSilenced 0x10, WORD_0x14 from the constructor argument, link 0x18, mVolume 0x20),
 * TRPSndAudioHandles<4> at 0x3C (counts 0x40 / 0x44, handles 0x48), mRemoteFlag 0x58.
 * Size 0x5C [M] (RPBilSoundActor and RPWlySoundObj use 0x5C differently).
 * @note Name is a project convention (ledger Wly_SoundObj / Bil_SoundActor).
 */
class RPSndObject2D : public TRPSndAudioActorBaseWithHandles<RP_MAX_CONTROLLERS, 0> {
public:
    RPSndObject2D(UNKWORD arg0)
        : TRPSndAudioActorBaseWithHandles<RP_MAX_CONTROLLERS, 0>(arg0) {}

    virtual ~RPSndObject2D(); // at 0x8

    virtual void stopSound(u32 id, int frames);            // at 0xC
    virtual void stopSound(ui32 id, int frames);   // at 0x10
    virtual void stopSound(const char* pName, int frames); // at 0x14
    virtual void stopAllSound(int frames);                 // at 0x18
    virtual void setActorVolume(f32 volume, int frames);   // at 0x1C
    virtual void update();                                 // at 0x20 track volumes

    /**
     * @brief Applies mVolume to the playing sequences' tracks [M]
     */
    virtual void calc(); // at 0x38

    // Names below follow TRPSndObject's; the handle is the first free one unless idx is given
    virtual nw4r::snd::SoundHandle* startSound(u32 id); // at 0x3C
    virtual nw4r::snd::SoundHandle* startSoundWithRemotePlayer(u32 id,
                                                               u32 remoteFlag); // at 0x40
    virtual nw4r::snd::SoundHandle* startSound(u32 id, int idx);          // at 0x44
    virtual nw4r::snd::SoundHandle* holdSound(u32 id);                    // at 0x48
    virtual nw4r::snd::SoundHandle* holdSound(ui32 id);           // at 0x4C
    virtual nw4r::snd::SoundHandle* holdSound(const char* pLabel);        // at 0x50
    virtual nw4r::snd::SoundHandle* holdSoundWithRemotePlayer(u32 id,
                                                              u32 remoteFlag); // at 0x54
    virtual nw4r::snd::SoundHandle*
    holdSoundWithRemotePlayer(const char* pLabel, u32 remoteFlag);    // at 0x58
    virtual nw4r::snd::SoundHandle* prepareSound(u32 id);            // at 0x5C
    virtual nw4r::snd::SoundHandle* prepareSound(u32 id, int idx);   // at 0x60
    virtual f32 getOuterVolume(); // at 0x64 mVolume's current value

protected:
    u32 mRemoteFlag; // at 0x58 passed to RPSndSpeakerMgr::setRemoteSend; not set by the ctor
};

//! @}

#endif
