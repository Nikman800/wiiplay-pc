/**
 * Host WPAD backend (docs/HOST_BUILD.md §23).
 *
 * The WPAD library as KPAD, EGG::CoreController and the game's RPSys
 * controller code use it, over four virtual remotes whose state the host sets
 * (PortWPADSetRemote; the mapping layer in host_input.cpp computes it from a
 * pointer / tilt / buttons). No Bluetooth: a report is what WPADRead returns
 * after the SDK's HID parser (calibrated accelerometer counts, the camera's
 * dots with y already flipped, buttons), produced at 200 Hz by
 * PortWPADRunFrame.
 *
 * Asynchronous commands (WPADControlDpd, WPADGetInfoAsync, WPADControlSpeaker,
 * WPADControlLed) succeed and call their callback at the next report, the way
 * the remote answers a command a few milliseconds later. A data format set
 * with WPADSetDataFormat applies from the next report.
 */

#include <port/input.h>

#include <revolution/OS.h>
#include <revolution/SC.h>
#include <revolution/WPAD.h>

#include <cstring>

namespace {

enum PendingKind {
    PENDING_NONE,
    PENDING_DPD,
    PENDING_INFO,
    PENDING_SPEAKER,
    PENDING_LED,
};

struct Pending {
    PendingKind kind;
    WPADCallback callback;
    u32 arg;
};

const int MAX_PENDING = 8;

struct Channel {
    PortWPADRemote remote;   // what the host set
    bool connected;          // as the game has been told
    u8 dev;                  // as the game has been told
    u32 format;
    u32 dpdCommand;          // 0: off
    bool motor;
    bool speaker;
    bool speakerPlaying;
    WPADSamplingCallback samplingCallback;
    WPADConnectCallback connectCallback;
    WPADExtensionCallback extensionCallback;
    WPADInfo* info;
    Pending pending[MAX_PENDING];
    int pendingCount;
};

Channel sChannels[WPAD_MAX_CONTROLLERS];
WPADLibStatus sStatus = WPAD_LIB_STATUS_0;
WPADAllocFunc sAllocFunc = NULL;
WPADFreeFunc sFreeFunc = NULL;
WPADSyncDeviceCallback sSyncCallback = NULL;
WPADClearDeviceCallback sClearCallback = NULL;
bool sClearPending = false;
BOOL sMotorEnabled = TRUE;
BOOL sSensorBarPower = TRUE;
u8 sSpeakerVolume = 0;
u32 sSamples = 0;
u32 sFrameAccum = 0;

PortWPADMotorFunc sMotorFunc = NULL;
void* sMotorUser = NULL;
PortWPADSpeakerFunc sSpeakerFunc = NULL;
PortWPADSpeakerControlFunc sSpeakerControlFunc = NULL;
void* sSpeakerControlUser = NULL;
void* sSpeakerUser = NULL;

bool validChan(s32 chan) {
    return chan >= 0 && chan < WPAD_MAX_CONTROLLERS;
}

void setMotor(s32 chan, bool on) {
    Channel& c = sChannels[chan];
    if (c.motor == on) {
        return;
    }
    c.motor = on;
    if (sMotorFunc != NULL) {
        sMotorFunc(chan, on ? 1 : 0, sMotorUser);
    }
}

s32 queue(s32 chan, PendingKind kind, WPADCallback callback, u32 arg) {
    Channel& c = sChannels[chan];
    if (!c.connected) {
        return WPAD_ERR_NO_CONTROLLER;
    }
    if (c.pendingCount == MAX_PENDING) {
        return WPAD_ERR_BUSY;
    }
    Pending& p = c.pending[c.pendingCount++];
    p.kind = kind;
    p.callback = callback;
    p.arg = arg;
    return WPAD_ERR_OK;
}

void fillInfo(s32 chan, WPADInfo* info) {
    const Channel& c = sChannels[chan];
    std::memset(info, 0, sizeof(*info));
    info->dpd = c.dpdCommand != 0;
    info->speaker = c.speaker;
    info->attach = c.dev != WPAD_DEV_CORE;
    info->lowBat = c.remote.battery <= 1;
    info->nearempty = c.remote.battery == 0;
    info->battery = c.remote.battery;
    info->led = static_cast<u8>(1 << chan);
}

void finishPending(s32 chan) {
    Channel& c = sChannels[chan];
    // Callbacks may queue more commands (KPAD issues the next DPD command from
    // its sampling callback, not from this one); those wait for the next report.
    int count = c.pendingCount;
    Pending done[MAX_PENDING];
    std::memcpy(done, c.pending, sizeof(Pending) * count);
    c.pendingCount = 0;
    for (int i = 0; i < count; i++) {
        const Pending& p = done[i];
        switch (p.kind) {
        case PENDING_DPD:
            c.dpdCommand = p.arg;
            break;
        case PENDING_INFO:
            if (c.info != NULL) {
                fillInfo(chan, c.info);
                c.info = NULL;
            }
            break;
        case PENDING_SPEAKER:
            switch (p.arg) {
            case WPAD_SPEAKER_OFF:
                c.speaker = false;
                c.speakerPlaying = false;
                break;
            case WPAD_SPEAKER_ON:
                c.speaker = true;
                break;
            case WPAD_SPEAKER_PLAY:
                c.speakerPlaying = c.speaker;
                break;
            default:
                break;
            }
            if (sSpeakerControlFunc != NULL) {
                sSpeakerControlFunc(chan, p.arg, sSpeakerControlUser);
            }
            break;
        default:
            break;
        }
        if (p.callback != NULL) {
            p.callback(chan, WPAD_ERR_OK);
        }
    }
}

u32 formatFamily(u32 format) {
    switch (format) {
    case WPAD_FMT_FS_BTN:
    case WPAD_FMT_FS_BTN_ACC:
    case WPAD_FMT_FS_BTN_ACC_DPD:
        return WPAD_DEV_FREESTYLE;
    case WPAD_FMT_CLASSIC_BTN:
    case WPAD_FMT_CLASSIC_BTN_ACC:
    case WPAD_FMT_CLASSIC_BTN_ACC_DPD:
        return WPAD_DEV_CLASSIC;
    default:
        return WPAD_DEV_CORE;
    }
}

bool formatHasAcc(u32 format) {
    return format != WPAD_FMT_CORE_BTN && format != WPAD_FMT_FS_BTN &&
           format != WPAD_FMT_CLASSIC_BTN;
}

bool formatHasDpd(u32 format) {
    return format == WPAD_FMT_CORE_BTN_ACC_DPD || format == WPAD_FMT_FS_BTN_ACC_DPD ||
           format == WPAD_FMT_CLASSIC_BTN_ACC_DPD || format == WPAD_FMT_BTN_ACC_DPD_EXTENDED;
}

} // namespace

extern "C" {

/* ------------------------------------------------------------------------
 * Host side
 * ---------------------------------------------------------------------- */

void PortWPADSetRemote(s32 chan, const PortWPADRemote* remote) {
    if (validChan(chan)) {
        sChannels[chan].remote = *remote;
    }
}

void PortWPADGetRemote(s32 chan, PortWPADRemote* remote) {
    if (validChan(chan)) {
        *remote = sChannels[chan].remote;
    }
}

void PortWPADSetMotorCallback(PortWPADMotorFunc func, void* user) {
    sMotorFunc = func;
    sMotorUser = user;
}

void PortWPADSetSpeakerCallback(PortWPADSpeakerFunc func, void* user) {
    sSpeakerFunc = func;
    sSpeakerUser = user;
}

void PortWPADSetSpeakerControlCallback(PortWPADSpeakerControlFunc func, void* user) {
    sSpeakerControlFunc = func;
    sSpeakerControlUser = user;
}

u32 PortWPADSampleCount(void) {
    return sSamples;
}

void PortWPADSample(void) {
    sSamples++;
    if (sStatus != WPAD_LIB_STATUS_3) {
        return;
    }
    if (sClearPending) {
        sClearPending = false;
        if (sClearCallback != NULL) {
            sClearCallback(WPAD_SYNC_DONE);
        }
    }
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        Channel& c = sChannels[chan];
        bool connected = c.remote.connected != 0;

        if (c.connected) {
            finishPending(chan);
        }

        if (connected != c.connected) {
            c.connected = connected;
            c.dev = connected ? c.remote.dev : static_cast<u8>(WPAD_DEV_NOT_FOUND);
            c.format = WPAD_FMT_CORE_BTN;
            c.dpdCommand = 0;
            c.speaker = c.speakerPlaying = false;
            c.pendingCount = 0;
            c.info = NULL;
            setMotor(chan, false);
            if (c.connectCallback != NULL) {
                c.connectCallback(chan, connected ? WPAD_ERR_OK : WPAD_ERR_NO_CONTROLLER);
            }
        } else if (connected && c.remote.dev != c.dev) {
            c.dev = c.remote.dev;
            if (c.extensionCallback != NULL) {
                c.extensionCallback(chan, c.dev);
            }
        }

        if (c.connected && c.samplingCallback != NULL) {
            c.samplingCallback(chan);
        }
    }
}

int PortWPADRunFrame(void) {
    // NTSC: a frame is 1001/60000 s; 200 reports a second.
    int n = 0;
    sFrameAccum += PORT_WPAD_SAMPLE_RATE * 1001;
    while (sFrameAccum >= 60000) {
        sFrameAccum -= 60000;
        PortWPADSample();
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------------
 * WPAD
 * ---------------------------------------------------------------------- */

void WPADInit(void) {
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
        Channel& c = sChannels[chan];
        PortWPADRemote remote = c.remote;
        std::memset(&c, 0, sizeof(c));
        c.remote = remote;
        c.dev = WPAD_DEV_NOT_FOUND;
        c.format = WPAD_FMT_CORE_BTN;
    }
    sMotorEnabled = SCGetWpadMotorMode() != SC_MOTOR_OFF;
    sSpeakerVolume = SCGetWpadSpeakerVolume();
    sStatus = WPAD_LIB_STATUS_3;
}

void WPADShutdown(void) {
    sStatus = WPAD_LIB_STATUS_0;
}

WPADLibStatus WPADGetStatus(void) {
    return sStatus;
}

void WPADRegisterAllocator(WPADAllocFunc pAllocFunc, WPADFreeFunc pFreeFunc) {
    sAllocFunc = pAllocFunc;
    sFreeFunc = pFreeFunc;
}

// No Bluetooth stack: no work memory.
u32 WPADGetWorkMemorySize(void) {
    return 0;
}

u8 WPADGetSensorBarPosition(void) {
    return SCGetWpadSensorBarPosition();
}

u8 WPADGetDpdSensitivity(void) {
    return static_cast<u8>(SCGetBtDpdSensibility());
}

void WPADGetAccGravityUnit(s32 chan, u32 type, WPADAccGravityUnit* pAcc) {
    if (!validChan(chan) || !sChannels[chan].connected) {
        return;
    }
    s16 unit = type == WPAD_ACC_GRAVITY_UNIT_FS
                   ? static_cast<s16>(PORT_WPAD_FS_ACC_1G - PORT_WPAD_FS_ACC_ZERO)
                   : static_cast<s16>(PORT_WPAD_CORE_ACC_1G - PORT_WPAD_CORE_ACC_ZERO);
    if (type == WPAD_ACC_GRAVITY_UNIT_FS && sChannels[chan].dev != WPAD_DEV_FREESTYLE) {
        return;
    }
    pAcc->x = pAcc->y = pAcc->z = unit;
}

void WPADGetAddress(s32 chan, BD_ADDR_PTR pAddr) {
    // A fixed address per channel (Nintendo's 00:19:1D prefix), zero when
    // nothing is connected, as on the Wii.
    std::memset(pAddr, 0, WPAD_ADDR_LEN);
    if (validChan(chan) && sChannels[chan].connected) {
        static const u8 addr[WPAD_ADDR_LEN] = {0x00, 0x19, 0x1D, 0x50, 0x4F, 0x00};
        std::memcpy(pAddr, addr, WPAD_ADDR_LEN);
        pAddr[WPAD_ADDR_LEN - 1] = static_cast<u8>(chan + 1);
    }
}

void WPADDisconnect(s32 chan) {
    if (validChan(chan)) {
        sChannels[chan].remote.connected = 0;
    }
}

s32 WPADProbe(s32 chan, s32* pDevType) {
    if (!validChan(chan) || !sChannels[chan].connected) {
        if (pDevType != NULL) {
            *pDevType = WPAD_DEV_NOT_FOUND;
        }
        return WPAD_ERR_NO_CONTROLLER;
    }
    if (pDevType != NULL) {
        *pDevType = sChannels[chan].dev;
    }
    return WPAD_ERR_OK;
}

WPADSamplingCallback WPADSetSamplingCallback(s32 chan, WPADSamplingCallback pCallback) {
    WPADSamplingCallback old = sChannels[chan].samplingCallback;
    sChannels[chan].samplingCallback = pCallback;
    return old;
}

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback pCallback) {
    WPADConnectCallback old = sChannels[chan].connectCallback;
    sChannels[chan].connectCallback = pCallback;
    return old;
}

WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback pCallback) {
    WPADExtensionCallback old = sChannels[chan].extensionCallback;
    sChannels[chan].extensionCallback = pCallback;
    return old;
}

u32 WPADGetDataFormat(s32 chan) {
    return sChannels[chan].format;
}

s32 WPADSetDataFormat(s32 chan, u32 format) {
    if (!sChannels[chan].connected) {
        return WPAD_ERR_NO_CONTROLLER;
    }
    sChannels[chan].format = format;
    return WPAD_ERR_OK;
}

s32 WPADGetInfoAsync(s32 chan, WPADInfo* pInfo, WPADCallback pCallback) {
    if (!validChan(chan)) {
        return WPAD_ERR_NO_CONTROLLER;
    }
    s32 result = queue(chan, PENDING_INFO, pCallback, 0);
    if (result == WPAD_ERR_OK) {
        sChannels[chan].info = pInfo;
    }
    return result;
}

void WPADControlMotor(s32 chan, u32 command) {
    if (!validChan(chan) || !sChannels[chan].connected) {
        return;
    }
    setMotor(chan, command == WPAD_MOTOR_RUMBLE && sMotorEnabled);
}

void WPADEnableMotor(BOOL enable) {
    sMotorEnabled = enable;
    if (!enable) {
        for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
            setMotor(chan, false);
        }
    }
}

BOOL WPADIsMotorEnabled(void) {
    return sMotorEnabled;
}

s32 WPADControlLed(s32 chan, u8 flags, WPADCallback pCallback) {
    (void)flags;
    return validChan(chan) ? queue(chan, PENDING_LED, pCallback, flags) : WPAD_ERR_NO_CONTROLLER;
}

BOOL WPADSaveConfig(WPADSaveCallback pCallback) {
    if (pCallback != NULL) {
        pCallback(SC_STATUS_OK);
    }
    return TRUE;
}

void WPADRead(s32 chan, WPADStatus* pStatus) {
    Channel& c = sChannels[chan];
    const PortWPADRemote& r = c.remote;
    u32 family = formatFamily(c.format);

    // The status struct of the format's family (WPADStatus, WPADFSStatus or
    // WPADCLStatus), as the SDK copies it.
    size_t size = family == WPAD_DEV_FREESTYLE ? sizeof(WPADFSStatus)
                  : family == WPAD_DEV_CLASSIC ? sizeof(WPADCLStatus)
                                               : sizeof(WPADStatus);
    std::memset(pStatus, 0, size);

    pStatus->dev = c.dev;
    pStatus->err = c.connected ? WPAD_ERR_OK : WPAD_ERR_NO_CONTROLLER;
    if (!c.connected) {
        return;
    }
    // The Nunchuk's buttons only come with an extension report.
    pStatus->button = r.button;
    if (c.dev != WPAD_DEV_FREESTYLE || family != WPAD_DEV_FREESTYLE) {
        pStatus->button &= static_cast<u16>(~(WPAD_BUTTON_FS_Z | WPAD_BUTTON_FS_C));
    }

    if (formatHasAcc(c.format)) {
        pStatus->accX = r.acc[0];
        pStatus->accY = r.acc[1];
        pStatus->accZ = r.acc[2];
    }

    bool dpd = formatHasDpd(c.format) && c.dpdCommand != 0 && sSensorBarPower;
    for (int i = 0; i < WPAD_MAX_DPD_OBJECTS; i++) {
        if (dpd && r.obj[i].size != 0) {
            pStatus->obj[i] = r.obj[i];
        } else {
            // What the HID parser stores for a missing dot.
            pStatus->obj[i].x = 0;
            pStatus->obj[i].y = WPAD_MAX_DPD_Y;
            pStatus->obj[i].size = 0;
        }
        pStatus->obj[i].traceId = static_cast<u8>(i);
    }

    if (family == WPAD_DEV_FREESTYLE && c.dev == WPAD_DEV_FREESTYLE) {
        WPADFSStatus* fs = reinterpret_cast<WPADFSStatus*>(pStatus);
        fs->fsAccX = r.fsAcc[0];
        fs->fsAccY = r.fsAcc[1];
        fs->fsAccZ = r.fsAcc[2];
        fs->fsStickX = r.fsStick[0];
        fs->fsStickY = r.fsStick[1];
    }
}

void WPADSetAutoSamplingBuf(s32 chan, void* pBuffer, u32 len) {
    (void)chan;
    (void)pBuffer;
    (void)len;
}

BOOL WPADIsDpdEnabled(s32 chan) {
    return validChan(chan) && sChannels[chan].dpdCommand != 0;
}

s32 WPADControlDpd(s32 chan, u32 command, WPADCallback pCallback) {
    return validChan(chan) ? queue(chan, PENDING_DPD, pCallback, command) : WPAD_ERR_NO_CONTROLLER;
}

BOOL WPADSetSensorBarPower(BOOL enable) {
    sSensorBarPower = enable;
    return TRUE;
}

void WPADRecalibrate(s32 chan) {
    (void)chan;
}

BOOL WPADIsSpeakerEnabled(s32 chan) {
    return validChan(chan) && sChannels[chan].speaker;
}

s32 WPADControlSpeaker(s32 chan, u32 command, WPADCallback pCallback) {
    return validChan(chan) ? queue(chan, PENDING_SPEAKER, pCallback, command) : WPAD_ERR_NO_CONTROLLER;
}

u8 WPADGetSpeakerVolume(void) {
    return sSpeakerVolume;
}

void WPADSetSpeakerVolume(u8 volume) {
    sSpeakerVolume = volume;
}

BOOL WPADCanSendStreamData(s32 chan) {
    return validChan(chan) && sChannels[chan].connected && sChannels[chan].speakerPlaying;
}

s32 WPADSendStreamData(s32 chan, void* pData, u16 len) {
    if (!WPADCanSendStreamData(chan)) {
        return WPAD_ERR_BUSY;
    }
    if (sSpeakerFunc != NULL) {
        sSpeakerFunc(chan, pData, len, sSpeakerUser);
    }
    return WPAD_ERR_OK;
}

// Pairing: the virtual remotes are always paired.
BOOL WPADStartSimpleSync(void) {
    return TRUE;
}

BOOL WPADStartFastSimpleSync(void) {
    return TRUE;
}

BOOL WPADStopSimpleSync(void) {
    return TRUE;
}

BOOL WPADStartClearDevice(void) {
    sClearPending = true;
    return TRUE;
}

WPADSyncDeviceCallback WPADSetSimpleSyncCallback(WPADSyncDeviceCallback pCallback) {
    WPADSyncDeviceCallback old = sSyncCallback;
    sSyncCallback = pCallback;
    return old;
}

WPADClearDeviceCallback WPADSetClearDeviceCallback(WPADClearDeviceCallback pCallback) {
    WPADClearDeviceCallback old = sClearCallback;
    sClearCallback = pCallback;
    return old;
}

} // extern "C"
