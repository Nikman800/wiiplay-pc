/**
 * SDK stand-ins for rfl_harness (port/docs/HOST_BUILD.md §16). Not decompiled
 * code; only what RVLFaceLib's resource path references.
 *
 * NAND: the host has no NAND, so no Mii database. Every open reports
 * NAND_RESULT_NOEXISTS (RFLiBootLoadDatabaseAsync then sets up an empty
 * database and goes on to the resource header, as on a Wii without one). The
 * other file calls are never reached on that path; they abort with their name.
 *
 * GX display lists: RFLiInitShapeRes writes its display lists through the GX
 * FIFO (WGPIPE), which the host does not have yet (Phase 7). Begin is a no-op
 * and End returns 0 bytes.
 *
 * OS: alarms are only set on the NAND retry path (abort); the rest are
 * bookkeeping.
 */
#include <revolution/GX.h>
#include <revolution/NAND.h>
#include <revolution/OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void RflHostAbort(const char* pName) {
    fprintf(stderr, "rfl_host: %s called on the host (no NAND)\n", pName);
    abort();
}

/* NAND */
s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, u8 access, void* buffer,
                             u32 bufferSize, NANDAsyncCallback callback,
                             NANDCommandBlock* block) {
    (void)path, (void)info, (void)access, (void)buffer, (void)bufferSize, (void)callback,
        (void)block;
    return NAND_RESULT_NOEXISTS;
}

s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 mode) {
    (void)path, (void)info, (void)mode;
    return NAND_RESULT_NOEXISTS;
}

void NANDSetUserData(NANDCommandBlock* block, void* data) {
    block->userData = data;
}

void* NANDGetUserData(NANDCommandBlock* block) {
    return block->userData;
}

#define RFL_HOST_ABORT(ret, name, params)                                                  \
    ret name params {                                                                       \
        RflHostAbort(#name);                                                                \
        return 0;                                                                           \
    }

RFL_HOST_ABORT(s32, NANDClose, (NANDFileInfo* info))
RFL_HOST_ABORT(s32, NANDSafeCloseAsync,
               (NANDFileInfo* info, NANDAsyncCallback callback, NANDCommandBlock* block))
RFL_HOST_ABORT(s32, NANDPrivateCreateAsync,
               (const char* path, u8 perm, u8 attr, NANDAsyncCallback callback,
                NANDCommandBlock* block))
RFL_HOST_ABORT(s32, NANDRead, (NANDFileInfo* info, void* buf, u32 len))
RFL_HOST_ABORT(s32, NANDReadAsync,
               (NANDFileInfo* info, void* buf, u32 len, NANDAsyncCallback callback,
                NANDCommandBlock* block))
RFL_HOST_ABORT(s32, NANDSeek, (NANDFileInfo* info, s32 offset, NANDSeekMode whence))
RFL_HOST_ABORT(s32, NANDSeekAsync,
               (NANDFileInfo* info, s32 offset, NANDSeekMode whence,
                NANDAsyncCallback callback, NANDCommandBlock* block))
RFL_HOST_ABORT(s32, NANDGetLengthAsync,
               (NANDFileInfo* info, u32* lengthOut, NANDAsyncCallback callback,
                NANDCommandBlock* block))

/* GX display lists (no FIFO on the host yet). The write-gather pipe is a plain
 * variable here, so the writes go nowhere. */
__typeof__(WGPIPE) WGPIPE;

void GXBeginDisplayList(void* list, u32 size) {
    (void)list, (void)size;
}

u32 GXEndDisplayList(void) {
    return 0;
}

/* OS */
void OSRegisterVersion(const char* ver) {
    (void)ver;
}

BOOL OSDisableInterrupts(void) {
    return TRUE;
}

BOOL OSRestoreInterrupts(BOOL status) {
    return status;
}

void OSCreateAlarm(OSAlarm* alarm) {
    memset(alarm, 0, sizeof(*alarm));
}

void OSCancelAlarm(OSAlarm* alarm) {
    (void)alarm;
}

void OSSetAlarmUserData(OSAlarm* alarm, void* userData) {
    alarm->userData = userData;
}

void OSSetAlarm(OSAlarm* alarm, s64 tick, OSAlarmHandler handler) {
    (void)alarm, (void)tick, (void)handler;
    RflHostAbort("OSSetAlarm");
}
