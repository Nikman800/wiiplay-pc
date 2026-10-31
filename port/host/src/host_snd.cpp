/**
 * Host stand-ins for the SDK calls nw4r::snd, the ut file streams and
 * EGG::ArcPlayer link (POINTERS.md step 7, port/docs/HOST_BUILD.md §13).
 *
 * Sound archives are opened, read and their groups loaded with the real
 * decompiled code; nothing is played. So:
 *
 *   AX, AI,  in src/host_snd_stubs.cpp (no playback) for the loader harnesses,
 *   threads  or src/host_ax.cpp / src/host_thread.cpp for the audio runtime
 *            (HOST_BUILD.md §21).
 *   OS       interrupts are a flag; OSGetTick counts calls.
 *   DVD      DVDCancel* / DVDGetDriveStatus for the ut DVD streams (reads are
 *            synchronous in host_sdk.cpp, so there is never anything to cancel).
 *   NAND,    every open fails (no NAND / channel content on the host); only
 *   CNT      ArcPlayer::openNandArchive / openCntArchive reach them.
 */

#include <types.h>

#include <revolution/CNT.h>
#include <revolution/DVD.h>
#include <revolution/NAND.h>
#include <revolution/OS.h>

#include <cstdio>
#include <cstdlib>

namespace {
BOOL sInterrupts = TRUE;
u32 sTick = 0;
} // namespace

extern "C" {

BOOL OSDisableInterrupts(void) {
    BOOL old = sInterrupts;
    sInterrupts = FALSE;
    return old;
}
BOOL OSRestoreInterrupts(BOOL status) {
    BOOL old = sInterrupts;
    sInterrupts = status;
    return old;
}
__attribute__((weak)) u32 OSGetTick(void) {
    return ++sTick;
}

/* DVD */
__attribute__((weak)) s32 DVDCancel(DVDCommandBlock* block) {
    (void)block;
    return 0;
}
__attribute__((weak)) BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCommandCallback callback) {
    if (callback != NULL) {
        callback(0, block);
    }
    return TRUE;
}
__attribute__((weak)) s32 DVDGetDriveStatus(void) {
    return DVD_STATE_IDLE;
}

/* NAND */
__attribute__((weak)) s32 NANDOpen(const char* path, NANDFileInfo* info, u8 mode) {
    (void)path;
    (void)info;
    (void)mode;
    return NAND_RESULT_NOEXISTS;
}
__attribute__((weak)) s32 NANDClose(NANDFileInfo* info) {
    (void)info;
    return NAND_RESULT_OK;
}
__attribute__((weak)) s32 NANDRead(NANDFileInfo* info, void* buf, u32 len) {
    (void)info;
    (void)buf;
    (void)len;
    return NAND_RESULT_FATAL_ERROR;
}
__attribute__((weak)) s32 NANDSeek(NANDFileInfo* info, s32 offset, NANDSeekMode whence) {
    (void)info;
    (void)offset;
    (void)whence;
    return NAND_RESULT_FATAL_ERROR;
}
__attribute__((weak)) s32 NANDReadAsync(NANDFileInfo* info, void* buf, u32 len, NANDAsyncCallback callback,
                  NANDCommandBlock* block) {
    (void)info;
    (void)buf;
    (void)len;
    (void)callback;
    (void)block;
    return NAND_RESULT_FATAL_ERROR;
}
__attribute__((weak)) s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 len) {
    (void)info;
    (void)buf;
    (void)len;
    return NAND_RESULT_FATAL_ERROR;
}
__attribute__((weak)) s32 NANDWriteAsync(NANDFileInfo* info, const void* buf, u32 len, NANDAsyncCallback callback,
                   NANDCommandBlock* block) {
    (void)info;
    (void)buf;
    (void)len;
    (void)callback;
    (void)block;
    return NAND_RESULT_FATAL_ERROR;
}
__attribute__((weak)) s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    (void)info;
    *length = 0;
    return NAND_RESULT_FATAL_ERROR;
}
__attribute__((weak)) s32 NANDGetCurrentDir(char* out) {
    out[0] = '/';
    out[1] = '\0';
    return NAND_RESULT_OK;
}

/* CNT (channel content) */
s32 contentConvertPathToEntrynumNAND(CNTHandle* handle, const char* path) {
    (void)handle;
    (void)path;
    return -1;
}
s32 contentFastOpenNAND(CNTHandle* handle, s32 entrynum, CNTFileInfo* info) {
    (void)handle;
    (void)entrynum;
    (void)info;
    return -1;
}
s32 contentCloseNAND(CNTFileInfo* info) {
    (void)info;
    return 0;
}
s32 contentReadNAND(CNTFileInfo* info, void* dst, u32 len, s32 offset) {
    (void)info;
    (void)dst;
    (void)len;
    (void)offset;
    return -1;
}
u32 contentGetLengthNAND(const CNTFileInfo* info) {
    (void)info;
    return 0;
}

} // extern "C"
