#ifndef RVL_FACE_LIBRARY_INTERNAL_FORMAT_H
#define RVL_FACE_LIBRARY_INTERNAL_FORMAT_H
#include <types.h>

#include <RVLFaceLib/RFLi_Types.h>
#include <port/be.h>
#ifdef __cplusplus
extern "C" {
#endif

#if defined(PORT_HOST)
// Host: the hidden database stays big-endian in memory (it is read, checked against
// its CRC and written back as a whole), so the list entry's two bit-field words are
// stored big-endian and read through RFLi_TABLE_GET / RFLi_TABLE_SET: word 0x8 is
// sex (bit 15) and next (bits 0-14), word 0xA prev (bits 0-14), as CodeWarrior
// allocates them.
typedef struct RFLiTableData {
    RFLCreateID createID; // at 0x0
    be_u16 sexNext;       // at 0x8
    be_u16 prevWord;      // at 0xA
} RFLiTableData;

static inline s16 RFLiTableGet_sex(const RFLiTableData* data) {
    return BE_GET(data->sexNext) >> 15;
}
static inline s16 RFLiTableGet_next(const RFLiTableData* data) {
    return (s16)(BE_GET(data->sexNext) << 1) >> 1;
}
static inline s16 RFLiTableGet_prev(const RFLiTableData* data) {
    return (s16)(BE_GET(data->prevWord) << 1) >> 1;
}
static inline void RFLiTableSet_sex(RFLiTableData* data, int sex) {
    BE_SET(data->sexNext, (BE_GET(data->sexNext) & 0x7FFF) | (sex & 1) << 15);
}
static inline void RFLiTableSet_next(RFLiTableData* data, int next) {
    BE_SET(data->sexNext, (BE_GET(data->sexNext) & 0x8000) | (next & 0x7FFF));
}
static inline void RFLiTableSet_prev(RFLiTableData* data, int prev) {
    BE_SET(data->prevWord, (BE_GET(data->prevWord) & 0x8000) | (prev & 0x7FFF));
}

#define RFLi_TABLE_GET(data, field) RFLiTableGet_##field(data)
#define RFLi_TABLE_SET(data, field, value) RFLiTableSet_##field(data, value)
#else
typedef struct RFLiTableData {
    RFLCreateID createID; // at 0x0

    // at 0x8
    u16 sex : 1;
    s16 next : 15;

    // at 0xA
    s16 padding2 : 1;
    s16 prev : 15;
} RFLiTableData;

#define RFLi_TABLE_GET(data, field) ((data)->field)
#define RFLi_TABLE_SET(data, field, value) ((data)->field = (value))
#endif

void RFLiClearTableData(RFLiTableData* data);
void RFLiClearDBBuffer(void);
RFLErrcode RFLiFormatAsync(RFLiCallback cb);

#ifdef __cplusplus
}
#endif
#endif
