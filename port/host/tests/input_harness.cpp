/**
 * input_harness: the input platform layer (docs/HOST_BUILD.md §23).
 *
 * Feeds a scripted input (port/input.h) through the mapping layer into the
 * host WPAD backend and runs the game's own read path over it:
 *
 *   --mode game (default)  RPSysCoreController::initialize (RPSysCoreControllerMgr,
 *                          EGG::CoreControllerMgr, KPADInit, the rumble managers),
 *                          applySetting / initScene, then each frame
 *                          RPSysCoreControllerMgr::beginFrame / endFrame;
 *                          the cursor as RPSysCursorDrawMgr::preUpdate computes
 *                          it, on an RPGrpScreen set up as RPSysCommonObject does.
 *   --mode kpad            KPADInit and KPADRead alone (KPAD's default
 *                          parameters), KPADSetSensorHeight as the game sets it.
 *
 * Output, per frame and connected channel (newest KPAD sample):
 *   K f=F ch=C n=SAMPLES reads=N dev=.. err=.. fmt=.. hold=.. trig=.. rel=..
 *     pos=X,Y hori=X,Y dist=D acc=X,Y,Z accv=X,Y dpd=V fsstick=X,Y fsacc=X,Y,Z
 *     [cursor=X,Y size=W,H]
 * and M f=F ch=C on=0|1 for every motor change, then a few WPAD self-checks
 * (S lines).
 */

#include <Pack/RPGraphics/RPGrpScreen.h>
#include <Pack/RPKernel.h>

#include <egg/core.h>
#include <egg/core/eggSystem.h>
#include <egg/math.h>

#include <revolution/KPAD.h>
#include <revolution/OS.h>
#include <revolution/SC.h>
#include <revolution/WPAD.h>

#include <port/host.h>
#include <port/input.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int gInputFrame = -1;

namespace {

#define sFrame gInputFrame

/* The memory part of EGG's start-up (ConfigurationData::initMemory) on a
 * configuration with no video, display or scenes. */
class HarnessConfig : public EGG::ConfigurationData {
public:
    HarnessConfig() : EGG::ConfigurationData(0x600000) {}
    virtual EGG::Video* getVideo() { return NULL; }
    virtual EGG::Heap* getSystemHeap() { return mSystemHeap; }
    virtual EGG::Display* getDisplay() { return NULL; }
    virtual EGG::XfbManager* getXfbMgr() { return NULL; }
    virtual EGG::PerformanceView* getPerfView() { return NULL; }
    virtual EGG::SceneManager* getSceneMgr() { return NULL; }
    virtual EGG::IAudioMgr* getAudioMgr() { return NULL; }
};

void OnMotor(s32 chan, int on, void*) {
    std::printf("M f=%d ch=%d on=%d\n", sFrame, static_cast<int>(chan), on);
}

void PrintStatus(int chan, int samples, int reads, const KPADStatus& s) {
    std::printf("K f=%d ch=%d n=%d reads=%d dev=%d err=%d fmt=%d hold=%08x trig=%08x rel=%08x "
                "pos=%.6f,%.6f hori=%.6f,%.6f dist=%.6f acc=%.6f,%.6f,%.6f accv=%.6f,%.6f dpd=%d "
                "fsstick=%.6f,%.6f fsacc=%.6f,%.6f,%.6f",
                sFrame, chan, samples, reads, s.dev_type, s.wpad_err, s.data_format,
                static_cast<unsigned>(s.hold), static_cast<unsigned>(s.trig),
                static_cast<unsigned>(s.release), s.pos.x, s.pos.y, s.horizon.x, s.horizon.y,
                s.dist, s.acc.x, s.acc.y, s.acc.z, s.acc_vertical.x, s.acc_vertical.y,
                s.dpd_valid_fg, s.ex_status.fs.stick.x, s.ex_status.fs.stick.y,
                s.ex_status.fs.acc.x, s.ex_status.fs.acc.y, s.ex_status.fs.acc.z);
}

void SpeakerTap(s32 chan, const void* data, u16 len, void*) {
    const u8* p = static_cast<const u8*>(data);
    u32 sum = 0;
    for (u16 i = 0; i < len; i++) {
        sum = sum * 31 + p[i];
    }
    std::printf("S speaker data ch=%d len=%u sum=%08x\n", static_cast<int>(chan),
                static_cast<unsigned>(len), static_cast<unsigned>(sum));
}

int sCallbackResults[8];
int sCallbackCount = 0;
void RecordCallback(s32 chan, s32 result) {
    (void)chan;
    sCallbackResults[sCallbackCount++ & 7] = static_cast<int>(result);
}

/* WPAD calls KPAD never makes: info, speaker, LED, a data format without the
 * camera, the gravity units. Run on channel `chan` (connected) after the script. */
void SelfChecks(s32 chan) {
    WPADInfo info;
    std::memset(&info, 0xFF, sizeof(info));
    sCallbackCount = 0;
    s32 r = WPADGetInfoAsync(chan, &info, RecordCallback);
    std::printf("S info queued=%d callbacks_before=%d\n", static_cast<int>(r), sCallbackCount);
    PortWPADSample();
    std::printf("S info callbacks=%d result=%d battery=%d lowBat=%d dpd=%d attach=%d led=%d\n",
                sCallbackCount, sCallbackResults[0], info.battery, info.lowBat, info.dpd,
                info.attach, info.led);

    PortWPADSetSpeakerCallback(SpeakerTap, NULL);
    u8 packet[20];
    for (int i = 0; i < 20; i++) {
        packet[i] = static_cast<u8>(i * 7 + 1);
    }
    std::printf("S speaker before=%d send=%d\n", WPADCanSendStreamData(chan),
                static_cast<int>(WPADSendStreamData(chan, packet, sizeof(packet))));
    WPADControlSpeaker(chan, WPAD_SPEAKER_ON, RecordCallback);
    WPADControlSpeaker(chan, WPAD_SPEAKER_PLAY, RecordCallback);
    PortWPADSample();
    std::printf("S speaker enabled=%d can=%d\n", WPADIsSpeakerEnabled(chan),
                WPADCanSendStreamData(chan));
    std::printf("S speaker send=%d\n", static_cast<int>(WPADSendStreamData(chan, packet, sizeof(packet))));
    WPADControlSpeaker(chan, WPAD_SPEAKER_OFF, NULL);
    PortWPADSample();
    std::printf("S speaker off can=%d\n", WPADCanSendStreamData(chan));

    WPADAccGravityUnit core = {0, 0, 0}, fs = {0, 0, 0};
    WPADGetAccGravityUnit(chan, WPAD_ACC_GRAVITY_UNIT_CORE, &core);
    WPADGetAccGravityUnit(chan, WPAD_ACC_GRAVITY_UNIT_FS, &fs);
    std::printf("S gravity core=%d,%d,%d fs=%d,%d,%d sensorbar=%d dpdsens=%d\n", core.x, core.y,
                core.z, fs.x, fs.y, fs.z, WPADGetSensorBarPosition(), WPADGetDpdSensitivity());
    s32 type = -1;
    s32 probe = WPADProbe(chan, &type);
    s32 probeOff = WPADProbe(3, &type);
    std::printf("S probe=%d probe3=%d type3=%d\n", static_cast<int>(probe),
                static_cast<int>(probeOff), static_cast<int>(type));
}

int Usage(const char* argv0) {
    std::fprintf(stderr, "usage: %s --script FILE [--sc FILE] [--mode game|kpad]\n", argv0);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    const char* scriptPath = NULL;
    const char* sc = NULL;
    bool game = true;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            scriptPath = argv[++i];
        } else if (std::strcmp(argv[i], "--sc") == 0 && i + 1 < argc) {
            sc = argv[++i];
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (std::strcmp(argv[i], "kpad") == 0) {
                game = false;
            } else if (std::strcmp(argv[i], "game") != 0) {
                return Usage(argv[0]);
            }
        } else {
            return Usage(argv[0]);
        }
    }
    if (scriptPath == NULL) {
        return Usage(argv[0]);
    }
    char err[256];
    PortInputScript* script = PortInputScriptLoad(scriptPath, err, sizeof(err));
    if (script == NULL) {
        std::fprintf(stderr, "%s\n", err);
        return 2;
    }

    PortOSInit();
    if (PortSCLoadConfig(sc) != 0) {
        std::fprintf(stderr, "bad SC config %s\n", sc);
        return 2;
    }
    SCInit();
    static HarnessConfig config;
    EGG::BaseSystem::configure(&config);
    config.initMemory();

    PortInputConfig inputConfig;
    PortInputDefaultConfig(&inputConfig);
    PortWPADSetMotorCallback(OnMotor, NULL);

    f32 winW, winH;
    PortInputScriptWindow(script, &winW, &winH);
    RPGrpScreen* cursorScreen = NULL;

    if (game) {
        // RPGrpRenderer::Initialize's screen limits (608 / 832 wide, the
        // render mode's EFB height) and RPSysCommonObject's cursor screen.
        const u16 maxX[] = {608, 832};
        const u16 maxY[] = {456, 456};
        RPGrpScreen::Initialize(maxX, maxY);
        cursorScreen = new RPGrpScreen();
        cursorScreen->SetCanvasMode(EGG::Frustum::CANVAS_CC);

        RPSysCoreController::initialize();
        RPSysCoreController::applySetting();
        RPSysCoreController::initScene();
    } else {
        KPADInit();
        for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
            KPADSetSensorHeight(chan, inputConfig.sensorLevel);
        }
    }
    std::printf("H mode=%s level=%.6f window=%.0f,%.0f frames=%d\n", game ? "game" : "kpad",
                inputConfig.sensorLevel, winW, winH, PortInputScriptFrames(script));

    PortInputState states[WPAD_MAX_CONTROLLERS];
    for (int i = 0; i < WPAD_MAX_CONTROLLERS; i++) {
        PortInputDefaultState(&states[i]);
    }

    int frames = PortInputScriptFrames(script);
    int lastConnected = -1;
    // KPAD mode: the newest sample of each channel (KPADRead fills nothing
    // when there was no report).
    static KPADStatus last[WPAD_MAX_CONTROLLERS];
    for (sFrame = 0; sFrame < frames; sFrame++) {
        PortInputScriptApply(script, sFrame, states);
        for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
            PortWPADRemote remote;
            PortInputToRemote(&inputConfig, &states[chan], &remote);
            PortWPADSetRemote(chan, &remote);
        }
        int samples = PortWPADRunFrame();

        if (game) {
            RPSysCoreControllerMgr* mgr = RP_GET_INSTANCE(RPSysCoreControllerMgr);
            mgr->beginFrame();
            for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
                RPSysCoreController* c = mgr->findController(chan);
                if (c == NULL || !states[chan].connected) {
                    continue;
                }
                const EGG::CoreStatus& s = *c->getCoreStatus();
                PrintStatus(chan, samples, c->getReadLength(), s);
                // RPSysCursorDrawMgr::preUpdate.
                if (s.getDPDValidFlag() > 0 && c->isCoreBaseDevice()) {
                    EGG::Vector2f pos = s.getDpdPos();
                    pos.x = EGG::Mathf::clamp2(pos.x, -1.0f, 1.0f);
                    pos.y = EGG::Mathf::clamp2(pos.y, -1.0f, 1.0f);
                    f32 cx = 0.0f, cy = 0.0f;
                    cursorScreen->ConvertFromNormalCC(pos.x, -pos.y, &cx, &cy);
                    std::printf(" cursor=%.4f,%.4f size=%.0f,%.0f", cx, cy,
                                cursorScreen->GetSize().x, cursorScreen->GetSize().y);
                }
                std::printf("\n");
                lastConnected = chan;
            }
            mgr->endFrame();
        } else {
            for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; chan++) {
                KPADStatus buf[KPAD_MAX_SAMPLES];
                s32 reads = KPADRead(chan, buf, KPAD_MAX_SAMPLES);
                if (reads > 0) {
                    last[chan] = buf[0];
                }
                if (!states[chan].connected) {
                    continue;
                }
                PrintStatus(chan, samples, reads, last[chan]);
                std::printf("\n");
                lastConnected = chan;
            }
        }
    }

    if (lastConnected >= 0 && states[lastConnected].connected) {
        SelfChecks(lastConnected);
    }
    std::printf("input_harness: %d frames, %u reports (pointer size %d)\n", frames,
                static_cast<unsigned>(PortWPADSampleCount()), static_cast<int>(sizeof(void*)));
    PortInputScriptFree(script);
    return 0;
}
