#ifndef RVL_FACE_LIBRARY_MIDDLE_DATABASE_H
#define RVL_FACE_LIBRARY_MIDDLE_DATABASE_H
#include <types.h>

#include <RVLFaceLib/RFL_Types.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RFLMiddleDBType_HiddenRandom,
    RFLMiddleDBType_HiddenNewer,
    RFLMiddleDBType_HiddenOlder,
    RFLMiddleDBType_Random,
    RFLMiddleDBType_UserSet,
    RFLMiddleDBType_Reserved1
} RFLMiddleDBType;

typedef struct RFLMiddleDB {
#if defined(PORT_HOST)
    // The members of RFLiMiddleDB, which holds two pointers (larger on a 64-bit
    // host; RFL_MiddleDatabase.c checks the size).
    u32 dummy0;
    void* dummy1[2];
    u16 dummy2[2];
    u32 dummy3[2];
#else
    u8 dummy[0x18];
#endif
} RFLMiddleDB;

u32 RFLGetMiddleDBBufferSize(u16 size);
void RFLInitMiddleDB(RFLMiddleDB* db, RFLMiddleDBType type, void* buffer,
                     u16 size);
RFLErrcode RFLUpdateMiddleDBAsync(RFLMiddleDB* db);
RFLMiddleDBType RFLGetMiddleDBType(const RFLMiddleDB* db);
u16 RFLGetMiddleDBStoredSize(const RFLMiddleDB* db);
void RFLSetMiddleDBRandomMask(RFLMiddleDB* db, RFLSex sex, RFLAge age,
                              RFLRace race);
void RFLSetMiddleDBHiddenMask(RFLMiddleDB* db, RFLSex sex);
RFLErrcode RFLAddMiddleDBStoreData(RFLMiddleDB* db, const RFLStoreData* data);

#ifdef __cplusplus
}
#endif
#endif
