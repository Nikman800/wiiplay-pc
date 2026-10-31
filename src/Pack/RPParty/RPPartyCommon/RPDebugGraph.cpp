#include <Pack/RPParty/RPPartyCommon/RPRumblePattern.h>
#include <egg/core/eggController.h>

#if !defined(PORT_HOST)
// (GCC reads this redeclaration as a second definition)
extern RPRumblePattern RPRumblePattern::sSlots[4][4];
#endif

inline void RPRumblePattern::ClearAll() {
    for (int channel = 0; channel < 4; ++channel) {
        for (int slot = 0; slot < 4; ++slot) {
            sSlots[channel][slot].mLength = 0;
            sSlots[channel][slot].mRemaining = 0;
        }
    }
}

extern "C" void RP_clearRumblePatterns() {
    RPRumblePattern::ClearAll();
}

inline void RPRumblePattern::UpdateAll() {
    int channel;
    RPRumblePattern* channelSlots = sSlots[0];
    for (channel = 0; channel < 4; ++channel, channelSlots += 4) {
        int slot;
        bool active;
        RPRumblePattern* pattern;
        pattern = channelSlots;
        active = false;
        for (slot = 0; slot < 4; ++pattern, ++slot) {
            if (pattern->mLength > 0) {
                --pattern->mLength;
                if ((1 << (pattern->mLength & 31)) &
                    pattern->mBits[pattern->mLength / 32]) {
                    active = true;
                }
            }
        }
        if (active) {
            EGG::CoreControllerMgr::instance()->getNthController(channel)->startMotor();
        } else {
            EGG::CoreControllerMgr::instance()->getNthController(channel)->stopMotor();
        }
    }
}

extern "C" void RP_updateRumblePatterns() {
    RPRumblePattern::UpdateAll();
}

void RPRumblePattern::Add(s32 channel, const RPRumblePattern& rPattern) {
    for (int slot = 0; slot < 4; ++slot) {
        if (sSlots[channel][slot].mLength <= 0) {
            sSlots[channel][slot] = rPattern;
            sSlots[channel][slot].mLength = rPattern.mRemaining;
            return;
        }
    }
}

#include <Pack/RPParty/RPPartyCommon/RPDebugGraph.h>

// Array allocation makes CodeWarrior emit the shared inline constructor.
// This unreferenced allocator is discarded when the unit is linked.
template <typename T> T* RP_createDebugGraphs(unsigned count) {
    return new T[count];
}

template RPDebugGraph* RP_createDebugGraphs<RPDebugGraph>(unsigned count);
