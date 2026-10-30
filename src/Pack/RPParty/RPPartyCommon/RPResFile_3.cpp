#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>
#include <Pack/RPParty/RPPartyCommon/RPResFile_2.h>

// Ledger name, kept: linked RPPathFollower calls it through extern "C"
extern "C" const RPPathPoint* RP_getPathPoint(const RPPathRes* pRes, u16 idx) {
    return &pRes->pPoints[idx];
}

static inline bool RPResFile_IsMatch(u8 type, s32 filter) {
    bool match = false;
    if (type == 0 || type == filter) {
        match = true;
    }
    return match;
}

inline void* RPResFile::FindFilteredEntry(u16 idx) const {
    RPResData* pData = static_cast<RPResData*>(mpData);
    RPResEntry* pFound = NULL;
    u16 num = 0;

    for (u16 i = 0; i < pData->numEntries; i++) {
        RPResEntry* pEntry = &pData->pEntries[i];
        if (RPResFile_IsMatch(pEntry->type, mFilter)) {
            if (num == idx) {
                pFound = pEntry;
                break;
            }
            num++;
        }
    }

    return pFound;
}

void RPResFile::Relocate() {
    if (static_cast<RPResData*>(mpData)->flags & 1) {
        return;
    }

    {
        RPResData* pData = static_cast<RPResData*>(mpData);
        if (pData->numEntries != 0) {
            pData->pEntries = PORT_U32_TO_PTR(RPResEntry*,
                PORT_PTR_TO_U32(pData) + PORT_PTR_TO_U32(pData->pEntries));
        } else {
            pData->pEntries = NULL;
        }
    }
    {
        RPResData* pData = static_cast<RPResData*>(mpData);
        if (pData->numPaths != 0) {
            pData->pPaths = PORT_U32_TO_PTR(RPPathRes*,
                PORT_PTR_TO_U32(pData) + PORT_PTR_TO_U32(pData->pPaths));
        } else {
            pData->pPaths = NULL;
        }
    }
    {
        RPResData* pData = static_cast<RPResData*>(mpData);
        if (pData->numPoints != 0) {
            pData->pPoints = PORT_U32_TO_PTR(RPPathPoint*,
                PORT_PTR_TO_U32(pData) + PORT_PTR_TO_U32(pData->pPoints));
        } else {
            pData->pPoints = NULL;
        }
    }

    u16 numEntries[1] = {static_cast<RPResData*>(mpData)->numEntries};
    for (u16 i = 0; i < numEntries[0]; i++) {
        RPResEntry* pEntry = static_cast<RPResEntry*>(FindFilteredEntry(i));

        if (pEntry != NULL) {
            RPPathRes* pPaths = static_cast<RPResData*>(mpData)->pPaths;
            u32 path = RES_PTR_RAW(pEntry->pPath);
            if (path == 0xFFFFFFFF) {
                pEntry->pPath = NULL;
            } else {
                pEntry->pPath = &pPaths[path];
            }
        }
    }

    RPResData* pData = static_cast<RPResData*>(mpData);
    u16 numPaths = pData->numPaths;
    u16 i = 0;
    if (numPaths != 0) {
        for (; i < numPaths; i++) {
            RPPathRes* pPath = &static_cast<RPResData*>(mpData)->pPaths[i];
            pPath->pPoints = &static_cast<RPResData*>(mpData)
                                  ->pPoints[RES_PTR_RAW(pPath->pPoints)];
        }
        pData = static_cast<RPResData*>(mpData);
    }

    pData->flags |= 1;
}

RPResFile* RPResFile::Create(void* pData, EGG::Heap* pHeap) {
    RPResFile* pFile = new (pHeap, 4) RPResFile(pData);
    pFile->Relocate();
    return pFile;
}
