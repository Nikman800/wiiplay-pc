#ifndef RP_PARTY_COMMON_RES_FILE_2_H
#define RP_PARTY_COMMON_RES_FILE_2_H

#include <Pack/RPParty/RPPartyCommon/RPResFile.h>
#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>

/**
 * @brief Header of the data an RPResFile wraps [M] (fields from RPResFile::Relocate)
 */
struct RPResData {
    u8 unk00[0x8];
    be_u16 flags;                  // at 0x8 bit 0: relocated
    u8 unk0A[0x10 - 0xA];
    be_u16 numEntries;             // at 0x10
    be_u16 numPaths;               // at 0x12
    be_u16 numPoints;              // at 0x14
    u8 unk16[0x40 - 0x16];
    RES_PTR(RPResEntry) pEntries;  // at 0x40 offset from the data until relocated
    RES_PTR(RPPathRes) pPaths;     // at 0x44 offset from the data until relocated
    RES_PTR(RPPathPoint) pPoints;  // at 0x48 offset from the data until relocated
};

#endif
