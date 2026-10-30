/**
 * Host stand-ins for the SDK calls nw4r::snd, the ut file streams and
 * EGG::ArcPlayer link (POINTERS.md step 7, port/docs/HOST_BUILD.md §13).
 *
 * Sound archives are opened, read and their groups loaded with the real
 * decompiled code; nothing is played. So:
 *
 *   AX, AI   no voices, no DSP, no audio interface: AXAcquireVoice returns
 *            NULL, the aux callback registrations are remembered and returned,
 *            everything else is a no-op. Phase 7's mixer replaces this.
 *   OS       threads are never started (the sound thread is created only by
 *            SoundSystem::InitSoundSystem, which the loader path does not
 *            call; OSResumeThread aborts); OSSleepThread aborts because nothing
 *            could wake it; interrupts are a flag; OSGetTick counts calls.
 *   DVD      DVDCancel* / DVDGetDriveStatus for the ut DVD streams (reads are
 *            synchronous in host_sdk.cpp, so there is never anything to cancel).
 *   NAND,    every open fails (no NAND / channel content on the host); only
 *   CNT      ArcPlayer::openNandArchive / openCntArchive reach them.
 */

#include <types.h>

#include <revolution/AI.h>
#include <revolution/AX.h>
#include <revolution/CNT.h>
#include <revolution/DVD.h>
#include <revolution/NAND.h>
#include <revolution/OS.h>

#include <cstdio>
#include <cstdlib>

namespace {
AXAuxCallback sAuxCallback[3];
void* sAuxContext[3];
BOOL sInterrupts = TRUE;
u32 sTick = 0;
} // namespace

extern "C" {

/* AI */
AIDMACallback AIRegisterDMACallback(AIDMACallback callback) {
    (void)callback;
    return NULL;
}

/* AX */
AXOutCallback AXRegisterCallback(AXOutCallback callback) {
    (void)callback;
    return NULL;
}
AXVPB* AXAcquireVoice(u32 prio, AXVoiceCallback callback, uintptr_t userContext) {
    (void)prio;
    (void)callback;
    (void)userContext;
    return NULL;
}
void AXFreeVoice(AXVPB* vpb) {
    (void)vpb;
}
s32 AXGetMaxVoices(void) {
    return 0;
}
void AXRegisterAuxACallback(AXAuxCallback callback, void* context) {
    sAuxCallback[0] = callback;
    sAuxContext[0] = context;
}
void AXRegisterAuxBCallback(AXAuxCallback callback, void* context) {
    sAuxCallback[1] = callback;
    sAuxContext[1] = context;
}
void AXRegisterAuxCCallback(AXAuxCallback callback, void* context) {
    sAuxCallback[2] = callback;
    sAuxContext[2] = context;
}
void AXGetAuxACallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[0];
    *context = sAuxContext[0];
}
void AXGetAuxBCallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[1];
    *context = sAuxContext[1];
}
void AXGetAuxCCallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[2];
    *context = sAuxContext[2];
}
void AXGetLpfCoefs(u16 freq, u16* a, u16* b) {
    (void)freq;
    *a = 0;
    *b = 0;
}
void AXSetAuxAReturnVolume(u16 volume) {
    (void)volume;
}
void AXSetAuxBReturnVolume(u16 volume) {
    (void)volume;
}
void AXSetAuxCReturnVolume(u16 volume) {
    (void)volume;
}
void AXSetMasterVolume(u16 volume) {
    (void)volume;
}
void AXSetMaxDspCycles(u32 num) {
    (void)num;
}
void AXSetMode(u32 mode) {
    (void)mode;
}
void AXSetVoiceAddr(AXVPB* vpb, AXPBADDR* addr) {
    (void)vpb;
    (void)addr;
}
void AXSetVoicePriority(AXVPB* vpb, u32 prio) {
    (void)vpb;
    (void)prio;
}
void AXSetVoiceState(AXVPB* vpb, u16 state) {
    (void)vpb;
    (void)state;
}

/* OS */
BOOL OSCreateThread(OSThread* thread, OSThreadFunc func, void* funcArg, void* stackBegin,
                    u32 stackSize, s32 prio, u16 flags) {
    (void)thread;
    (void)func;
    (void)funcArg;
    (void)stackBegin;
    (void)stackSize;
    (void)prio;
    (void)flags;
    return TRUE;
}
s32 OSResumeThread(OSThread* thread) {
    (void)thread;
    std::fprintf(stderr, "host_snd: OSResumeThread: threads are not started on the host\n");
    std::abort();
}
BOOL OSJoinThread(OSThread* thread, void* val) {
    (void)thread;
    (void)val;
    return TRUE;
}
void OSInitThreadQueue(OSThreadQueue* queue) {
    (void)queue;
}
void OSSleepThread(OSThreadQueue* queue) {
    (void)queue;
    std::fprintf(stderr, "host_snd: OSSleepThread: nothing can wake a single-threaded host\n");
    std::abort();
}
void OSWakeupThread(OSThreadQueue* queue) {
    (void)queue;
}
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage mesg, u32 flags) {
    // Single-threaded ring buffer (host_sdk.cpp): the same as sending.
    return OSSendMessage(queue, mesg, flags);
}
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
u32 OSGetTick(void) {
    return ++sTick;
}

/* DVD */
s32 DVDCancel(DVDCommandBlock* block) {
    (void)block;
    return 0;
}
BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCommandCallback callback) {
    if (callback != NULL) {
        callback(0, block);
    }
    return TRUE;
}
s32 DVDGetDriveStatus(void) {
    return DVD_STATE_IDLE;
}

/* NAND */
s32 NANDOpen(const char* path, NANDFileInfo* info, u8 mode) {
    (void)path;
    (void)info;
    (void)mode;
    return NAND_RESULT_NOEXISTS;
}
s32 NANDClose(NANDFileInfo* info) {
    (void)info;
    return NAND_RESULT_OK;
}
s32 NANDRead(NANDFileInfo* info, void* buf, u32 len) {
    (void)info;
    (void)buf;
    (void)len;
    return NAND_RESULT_FATAL_ERROR;
}
s32 NANDSeek(NANDFileInfo* info, s32 offset, NANDSeekMode whence) {
    (void)info;
    (void)offset;
    (void)whence;
    return NAND_RESULT_FATAL_ERROR;
}
s32 NANDReadAsync(NANDFileInfo* info, void* buf, u32 len, NANDAsyncCallback callback,
                  NANDCommandBlock* block) {
    (void)info;
    (void)buf;
    (void)len;
    (void)callback;
    (void)block;
    return NAND_RESULT_FATAL_ERROR;
}
s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 len) {
    (void)info;
    (void)buf;
    (void)len;
    return NAND_RESULT_FATAL_ERROR;
}
s32 NANDWriteAsync(NANDFileInfo* info, const void* buf, u32 len, NANDAsyncCallback callback,
                   NANDCommandBlock* block) {
    (void)info;
    (void)buf;
    (void)len;
    (void)callback;
    (void)block;
    return NAND_RESULT_FATAL_ERROR;
}
s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    (void)info;
    *length = 0;
    return NAND_RESULT_FATAL_ERROR;
}
s32 NANDGetCurrentDir(char* out) {
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
