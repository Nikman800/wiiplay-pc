#include <Pack/RPParty/RPPartyCommon/RPResFile_4.h>

#include <Pack/RPSystem/RPSysResourceManager.h>

RPResTable::RPResTable(const char* pResName) {
    EGG::Archive* pArchive =
        RPSysResourceManager::LoadGameCommonArchive(-1, 0);
    void* pData =
        RPSysResourceManager::GetFileFromArchive(pArchive, pResName, 0);
    mpFile = RPResFile::Create(pData, EGG::Heap::getCurrentHeap());
}

#pragma push
#pragma scheduling off
void* RPResTable::FindNthEntryOfType(u32 type, u16 idx) const {
    u16 count = mpFile->CountFilteredEntries();

    for (int i = 0; i < count; ++i) {
        void* pEntry = mpFile->GetFilteredEntry(i);
        if (type == *static_cast<be_u32*>(pEntry) && idx-- == 0) {
            return pEntry;
        }
    }

    return 0;
}
#pragma pop

#pragma push
#pragma scheduling off
u16 RPResTable::CountEntriesOfType(u32 type) const {
    u16 found = 0;
    u16 count = mpFile->CountFilteredEntries();

    for (int i = 0; i < count; ++i) {
        void* pEntry = mpFile->GetFilteredEntry(i);
        if (type == *static_cast<be_u32*>(pEntry)) {
            found = static_cast<u16>(found + 1);
        }
    }

    return found;
}
#pragma pop
