#ifndef RP_PARTY_COMMON_MEDAL_RESULT_5_H
#define RP_PARTY_COMMON_MEDAL_RESULT_5_H

#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPResultMenu.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>

// Access the result fields without changing the shared scene/state-machine headers.
// The word and byte offsets are RPSceneInfo's 32-bit layout (a 4-byte vtable pointer, then
// mPlayerNum at 0x4, unk8 at 0x8, unk10, unk14, ... unk2C). PORT_HOST: the members themselves,
// since the host's 8-byte vtable pointer moves them (word 2 is then mPlayerNum: a two-player
// result overwrote the player count, and the party menu then read a third player).
inline u32& ResultMenuSceneWins(RPSceneInfo* pInfo, s32 player) {
#if defined(PORT_HOST)
    return pInfo->unk8[player];
#else
    return reinterpret_cast<u32*>(pInfo)[2 + player];
#endif
}

inline s32& ResultMenuSceneStreak(RPSceneInfo* pInfo) {
#if defined(PORT_HOST)
    return reinterpret_cast<s32&>(pInfo->unk10);
#else
    return reinterpret_cast<s32*>(pInfo)[4];
#endif
}

inline s32& ResultMenuSceneWinner(RPSceneInfo* pInfo) {
#if defined(PORT_HOST)
    return pInfo->unk14;
#else
    return reinterpret_cast<s32*>(pInfo)[5];
#endif
}

inline void ResultMenuClearRetryFlag(RPSceneInfo* pInfo) {
#if defined(PORT_HOST)
    pInfo->unk2C = false;
#else
    reinterpret_cast<bool*>(pInfo)[0x2C] = false;
#endif
}

inline s32 ResultMenuPreviousState(const RPResultMenuSeq* pSeq) {
    return reinterpret_cast<const s32*>(pSeq)[1];
}

#endif
