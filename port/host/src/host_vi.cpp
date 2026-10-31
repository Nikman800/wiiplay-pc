/**
 * Video interface, time and alarms on the host (port/docs/HOST_BUILD.md §29).
 *
 * Game time is virtual and deterministic. It only passes when every thread is
 * blocked and no device has work left (host_thread.cpp's clock, PortOSSetClock):
 * the clock then jumps to the next timed event and runs it in interrupt context:
 *
 *   retrace    every 1001/60000 s (NTSC): the retrace count goes up, the host's
 *              retrace hook runs (scripted input, PortWPADRunFrame), the
 *              pre-retrace callback, the threads waiting in VIWaitForRetrace
 *              wake, the post-retrace callback (EGG::AsyncDisplay's frame tick).
 *   AX frame   every 3 ms once AXInit has run: PortAXRunFrame (the AI DMA
 *              interrupt; the sound thread wakes); the 96 stereo samples go to
 *              the host's audio sink.
 *   alarms     OSSetAlarm / OSSetPeriodicAlarm handlers at their time.
 *
 * A live run (--realtime, §30) sets a pacer that sleeps before each jump until
 * the wall clock reaches the event; the sequence of events is the same.
 *
 * Events due at the same time run in that order. Time is kept in units of
 * 1/60000 s (a retrace is 1001 units, an AX frame 180); OSGetTime converts to bus
 * ticks (60.75 MHz) and adds one tick per call made since the last event, so
 * busy-wait loops on the clock still see it move, and every run sees the same
 * values. Game time 0 is 2007-01-01 00:00:00.
 *
 * VI has nothing to scan out: the frame is presented from the EFB at GXCopyDisp
 * (§26). VIConfigure / VISetNextFrameBuffer / VISetBlack / VIFlush record what
 * they are given; the DTV status is 0 (no component cable) and the scan mode
 * interlaced, as the SC defaults (§22) describe.
 */

#include <types.h>

#include <revolution/OS.h>
#include <revolution/VI.h>

#include <port/host.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const s64 RETRACE_UNITS = 1001; // 1/59.94 s
const s64 AX_FRAME_UNITS = 180; // 3 ms
// Bus timer ticks (243 MHz / 4) per second; 1012.5 per unit
const s64 TICKS_PER_SEC = 60750000;
// Game time 0: 2007-01-01 00:00:00 (Wii time counts from 2000-01-01)
const s64 BOOT_SECONDS = 2557LL * 86400;

s64 sUnits = 0;     // now
s64 sCallTicks = 0; // OSGetTime calls since the last event
s64 sNextRetrace = RETRACE_UNITS;
s64 sNextAx = -1; // -1: AX not running yet
s32 sRetraceCount = 0;
u64 sAxFrames = 0;

VIRetraceCallback sPreRetrace = NULL;
VIRetraceCallback sPostRetrace = NULL;
OSThreadQueue sRetraceQueue;
bool sQueueInit = false;
void* sNextFb = NULL;
void* sCurrentFb = NULL;
BOOL sBlack = TRUE;

PortVIRetraceHook sRetraceHook = NULL;
void* sRetraceHookUser = NULL;
PortVIAudioSink sAudioSink = NULL;
void* sAudioSinkUser = NULL;
PortVIPacer sPacer = NULL;
void* sPacerUser = NULL;

// Pending alarms, sorted by fire time, then by the order they were set
struct AlarmEntry {
    OSAlarm* alarm;
    s64 fireUnits;
    u64 order;
};
const int MAX_ALARMS = 128;
AlarmEntry sAlarms[MAX_ALARMS];
int sAlarmCount = 0;
u64 sAlarmOrder = 0;

s64 BaseTicks() {
    return BOOT_SECONDS * TICKS_PER_SEC;
}

s64 UnitsToTicks(s64 units) {
    return units * 2025 / 2;
}

// The first unit at or after the absolute tick value
s64 TicksToUnits(s64 ticks) {
    s64 rel = ticks - BaseTicks();
    if (rel <= 0) {
        return 0;
    }
    return (rel * 2 + 2024) / 2025;
}

void RemoveAlarm(OSAlarm* alarm) {
    for (int i = 0; i < sAlarmCount; i++) {
        if (sAlarms[i].alarm == alarm) {
            std::memmove(&sAlarms[i], &sAlarms[i + 1], (sAlarmCount - i - 1) * sizeof(AlarmEntry));
            sAlarmCount--;
            return;
        }
    }
}

void InsertAlarm(OSAlarm* alarm, s64 fireTicks) {
    RemoveAlarm(alarm);
    if (sAlarmCount == MAX_ALARMS) {
        std::fprintf(stderr, "host_vi: too many alarms\n");
        std::abort();
    }
    AlarmEntry e = {alarm, TicksToUnits(fireTicks), sAlarmOrder++};
    if (e.fireUnits <= sUnits) {
        e.fireUnits = sUnits + 1;
    }
    alarm->end = fireTicks;
    int i = sAlarmCount;
    while (i > 0 && sAlarms[i - 1].fireUnits > e.fireUnits) {
        sAlarms[i] = sAlarms[i - 1];
        i--;
    }
    sAlarms[i] = e;
    sAlarmCount++;
}

void Retrace() {
    sRetraceCount++;
    sNextRetrace += RETRACE_UNITS;
    if (sRetraceHook != NULL) {
        sRetraceHook(static_cast<u32>(sRetraceCount), sRetraceHookUser);
    }
    if (sPreRetrace != NULL) {
        sPreRetrace(static_cast<u32>(sRetraceCount));
    }
    sCurrentFb = sNextFb;
    if (sQueueInit) {
        OSWakeupThread(&sRetraceQueue);
    }
    if (sPostRetrace != NULL) {
        sPostRetrace(static_cast<u32>(sRetraceCount));
    }
}

void AxFrame() {
    sNextAx += AX_FRAME_UNITS;
    short out[PORT_AX_FRAME_SAMPLES * 2];
    PortAXRunFrame(out);
    sAxFrames++;
    if (sAudioSink != NULL) {
        sAudioSink(out, PORT_AX_FRAME_SAMPLES, sAudioSinkUser);
    }
}

void FireAlarm() {
    OSAlarm* alarm = sAlarms[0].alarm;
    std::memmove(&sAlarms[0], &sAlarms[1], (sAlarmCount - 1) * sizeof(AlarmEntry));
    sAlarmCount--;
    OSAlarmHandler handler = alarm->handler;
    if (alarm->period > 0) {
        InsertAlarm(alarm, alarm->end + alarm->period);
    }
    static OSContext sContext;
    if (handler != NULL) {
        handler(alarm, &sContext);
    }
}

// The clock: advance to the next event and run every event due then.
int Clock(void) {
    if (sNextAx < 0 && PortAXIsInitialised()) {
        sNextAx = sUnits + AX_FRAME_UNITS;
    }
    s64 next = sNextRetrace;
    if (sNextAx >= 0 && sNextAx < next) {
        next = sNextAx;
    }
    if (sAlarmCount > 0 && sAlarms[0].fireUnits < next) {
        next = sAlarms[0].fireUnits;
    }
    if (sPacer != NULL) {
        sPacer(next, sPacerUser);
    }
    sUnits = next;
    sCallTicks = 0;
    if (sNextRetrace == sUnits) {
        Retrace();
    }
    if (sNextAx == sUnits) {
        AxFrame();
    }
    while (sAlarmCount > 0 && sAlarms[0].fireUnits <= sUnits) {
        FireAlarm();
    }
    return 1;
}

} // namespace

extern "C" {

/* Host control (port/host.h) */
void PortVIInitClock(void) {
    PortOSSetClock(Clock);
}

void PortVISetRetraceHook(PortVIRetraceHook hook, void* user) {
    sRetraceHook = hook;
    sRetraceHookUser = user;
}

void PortVISetAudioSink(PortVIAudioSink sink, void* user) {
    sAudioSink = sink;
    sAudioSinkUser = user;
}

void PortVISetPacer(PortVIPacer pacer, void* user) {
    sPacer = pacer;
    sPacerUser = user;
}

unsigned long long PortVIAudioFrames(void) {
    return sAxFrames;
}

long long PortVIGameTimeUnits(void) {
    return sUnits;
}

int PortVIIsBlack(void) {
    return sBlack ? 1 : 0;
}

/* Time */
s64 OSGetTime(void) {
    return BaseTicks() + UnitsToTicks(sUnits) + ++sCallTicks;
}

u32 OSGetTick(void) {
    return static_cast<u32>(OSGetTime());
}

s64 __OSGetSystemTime(void) {
    return OSGetTime();
}

void OSTicksToCalendarTime(s64 time, OSCalendarTime* cal) {
    s64 secs = time / TICKS_PER_SEC;
    s64 rem = time % TICKS_PER_SEC;
    if (rem < 0) {
        rem += TICKS_PER_SEC;
        secs--;
    }
    const s64 ticksPerMs = TICKS_PER_SEC / 1000;
    cal->msec = static_cast<s32>(rem / ticksPerMs);
    cal->usec = static_cast<s32>(rem % ticksPerMs * 1000 / ticksPerMs);
    s64 days = secs / 86400;
    s64 daySecs = secs % 86400;
    cal->hour = static_cast<s32>(daySecs / 3600);
    cal->min = static_cast<s32>(daySecs / 60 % 60);
    cal->sec = static_cast<s32>(daySecs % 60);
    cal->wday = static_cast<s32>((days + 6) % 7); // 2000-01-01 was a Saturday
    s32 year = 2000;
    for (;;) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        s32 len = leap ? 366 : 365;
        if (days < len) {
            break;
        }
        days -= len;
        year++;
    }
    cal->year = year;
    cal->yday = static_cast<s32>(days);
    static const s32 kMonth[2][12] = {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
                                      {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
    int leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 1 : 0;
    s32 m = 0;
    while (days >= kMonth[leap][m]) {
        days -= kMonth[leap][m];
        m++;
    }
    cal->month = m;
    cal->mday = static_cast<s32>(days) + 1;
}

/* Alarms */
void OSCreateAlarm(OSAlarm* alarm) {
    std::memset(alarm, 0, sizeof(*alarm));
}

void OSSetAlarm(OSAlarm* alarm, s64 tick, OSAlarmHandler handler) {
    alarm->handler = handler;
    alarm->period = 0;
    alarm->start = OSGetTime();
    InsertAlarm(alarm, alarm->start + tick);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, s64 tick, s64 period, OSAlarmHandler handler) {
    alarm->handler = handler;
    alarm->period = period;
    alarm->start = tick;
    s64 now = OSGetTime();
    s64 first = tick;
    if (first <= now && period > 0) {
        first += ((now - first) / period + 1) * period;
    }
    InsertAlarm(alarm, first);
}

void OSCancelAlarm(OSAlarm* alarm) {
    RemoveAlarm(alarm);
    alarm->handler = NULL;
}

void OSSetAlarmTag(OSAlarm* alarm, u32 tag) {
    alarm->tag = tag;
}

void OSSetAlarmUserData(OSAlarm* alarm, void* userData) {
    alarm->userData = userData;
}

void* OSGetAlarmUserData(const OSAlarm* alarm) {
    return alarm->userData;
}

/* VI */
void VIInit(void) {
    if (!sQueueInit) {
        OSInitThreadQueue(&sRetraceQueue);
        sQueueInit = true;
    }
}

void VIWaitForRetrace(void) {
    VIInit();
    s32 count = sRetraceCount;
    while (sRetraceCount == count) {
        OSSleepThread(&sRetraceQueue);
    }
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback) {
    VIRetraceCallback old = sPreRetrace;
    sPreRetrace = callback;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback) {
    VIRetraceCallback old = sPostRetrace;
    sPostRetrace = callback;
    return old;
}

void VIConfigure(const GXRenderModeObj* rmo) {
    (void)rmo;
}

void VIConfigurePan(u16 x, u16 y, u16 w, u16 h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void VIFlush(void) {}

void VISetNextFrameBuffer(void* fb) {
    sNextFb = fb;
}

void* VIGetCurrentFrameBuffer(void) {
    return sCurrentFb;
}

void* VIGetNextFrameBuffer(void) {
    return sNextFb;
}

void VISetBlack(BOOL black) {
    sBlack = black;
}

s32 VIGetRetraceCount(void) {
    return sRetraceCount;
}

u32 VIGetCurrentLine(void) {
    return 0;
}

VIScanMode VIGetScanMode(void) {
    return VI_SCANMODE_INT;
}

u32 VIGetDTVStatus(void) {
    return 0;
}

BOOL VIEnableDimming(BOOL enable) {
    (void)enable;
    return TRUE;
}

void VISetTrapFilter(BOOL enable) {
    (void)enable;
}

} // extern "C"
