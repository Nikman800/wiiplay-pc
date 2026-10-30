#include <Pack/RPKernel/RPSysSystem.h>
#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPAudio/RPSndSpeakerMgr.h>
#include <Pack/RPKernel/RPSysController.h>
#include <Pack/RPKernel/RPSysCursorDrawMgr.h>
#include <Pack/RPKernel/RPSysEffectMgr.h>
#include <Pack/RPKernel/RPSysHomeMenuMgr.h>
#include <Pack/RPKernel/RPSysHostIOSocketManager.h>
#include <Pack/RPKernel/RPSysKokeshiManager.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysNWC24Manager.h>
#include <Pack/RPKernel/RPSysSystemWinMgr.h>
#include <Pack/RPKernel/RPSysTagProcessor.h>
#include <Pack/RPKernel/RPSysTextWriter.h>
#include <Pack/RPKernel/RPSysWideTextWriter.h>
#include <Pack/RPParty/RPPartyCommon/RPMessageWindow.h>
#include <Pack/RPSystem/RPSysDvdStatus.h>
#include <Pack/RPSystem/RPSysFontManager.h>
#include <Pack/RPSystem/RPSysGameConfig.h>
#include <Pack/RPSystem/RPSysPauseMgr.h>
#include <Pack/RPSystem/RPSysPlayerMgr.h>
#include <Pack/RPSystem/RPSysProjectLocal.h>
#include <Pack/RPSystem/RPSysQueuedScene.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <Pack/RPSystem/RPSysSaveDataMgr.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>

#include <egg/core/eggAsyncDisplay.h>
#include <egg/util/eggException.h>
#include <egg/core/eggProcessMeter.h>
#include <egg/gfxe/eggScreen.h>
#include <revolution/DVD.h>
#include <revolution/SC.h>
#include <revolution/VI.h>

extern "C" {
extern const u16 lbl_80316CB8[];
extern const char lbl_80316CC8[];
void RP_seedRandomFromTime();
void VISetTrapFilter(BOOL enable);
}

RPSysSystem* RPSysSystem::spInstance;
GXRenderModeObj* RPSysSystem::spRenderModeObj;
RPSysRenderMode::EFormat RPSysSystem::sRenderModeFormat;
u32 RPSysSystem::sRenderModeWaitStart;

// Disc-check result written by RPSysSystem_checkDiskCallback (0 = pending, 1 = ok, 2 = error);
// dtk sizes it eight bytes (the next unit's .sbss starts 8-aligned).
extern "C" {
u32 lbl_8043C450[2];
void RPSysSystem_checkDiskCallback(s32 result, DVDCommandBlock* block);
BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCommandCallback callback);
}

void RPSysSystem::initialize() {
    void* heap2Start;
    void* end1;
    void* start2;
    void* end2;
    void* alloc1;
    EGG::BaseSystem::configure(&sConfigData);
    DVDInit();
    void* mem1Lo = OSGetMEM1ArenaLo();
    void* mem1Hi = OSGetMEM1ArenaHi();
    void* mem2Lo = OSGetMEM2ArenaLo();
    void* mem2Hi = OSGetMEM2ArenaHi();
    alloc1 = OSInitAlloc(mem1Lo, mem1Hi, 2);
    mem2Lo = OSInitAlloc(mem2Lo, mem2Hi, 2);
    void* start1 = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(alloc1) + 31) & ~31);
    end1 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem1Hi) & ~31);
    start2 = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(mem2Lo) + 31) & ~31);
    sConfigData.mCodeEnd = start1;
    sConfigData.mCodeStart = reinterpret_cast<void*>(0x80000000);
    sConfigData.mMem1Start = start1;
    sConfigData.mMem1End = end1;
    sConfigData.mMem2Start = start2;
    end2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem2Hi) & ~31);
    sConfigData.mMem2End = end2;
    sConfigData.mMemorySize = *reinterpret_cast<u32*>(0x80000028);
    OSSetMEM1ArenaLo(end1);
    OSSetMEM1ArenaHi(end1);
    OSSetMEM2ArenaLo(end2);
    OSSetMEM2ArenaHi(end2);
    EGG::Heap::initialize();
    heap2Start = sConfigData.mMem2Start;
    u32 heap2Size = static_cast<u8*>(sConfigData.mMem2End) - static_cast<u8*>(heap2Start);
    sConfigData.mRootHeapMem1 = EGG::ExpHeap::create(
        sConfigData.mMem1Start,
        static_cast<u8*>(sConfigData.mMem1End) - static_cast<u8*>(sConfigData.mMem1Start), 0);
    sConfigData.mRootHeapMem2 = EGG::ExpHeap::create(heap2Start, heap2Size, 0);
    sConfigData.mRootHeapDebug = NULL;
    sConfigData.mSystemHeap = EGG::ExpHeap::create(0x600000, sConfigData.getRootHeapMem1(), 0);
    sConfigData.mSystemHeap->becomeCurrentHeap();
    EGG::GraphicsFifo::create(0x80000, NULL);
    setupTVMode();
    setupRenderMode();
    sConfigData.mVideo = new EGG::Video(spRenderModeObj);
    sConfigData.mXfbMgr = new EGG::XfbManager;
    int i = 0;
    do {
        sConfigData.mXfbMgr->attach(new EGG::Xfb(sConfigData.getRootHeapMem2()));
    } while (++i < 2);
    sConfigData.mDisplay = new EGG::AsyncDisplay(1);
    EGG::Thread::initialize();
    sConfigData.mParentThread = new EGG::Thread(OSGetCurrentThread(), 4);
    sConfigData.mPerfView = new EGG::ProcessMeter(true);
    EGG::DvdFile::initialize();
    EGG::Exception::create(64, 32, 4, NULL);
    EGG::Exception::setPadInfo(lbl_80316CB8);
    sConfigData.getRootHeapMem2()->becomeCurrentHeap();
}
void RPSysSystem::create() { spInstance = new (sConfigData.mSystemHeap) RPSysSystem; }
void RPSysSystem::setup() {
    mpResourceHeap = EGG::FrmHeap::create(0x1800000, sConfigData.getRootHeapMem2(), 0);
    mpAssertHeap = EGG::ExpHeap::create(0x100, sConfigData.getRootHeapMem2(), 0);
    mpAssertHeap->disableAllocation();
    mpReserveHeap = EGG::ExpHeap::create(0x80000, sConfigData.getRootHeapMem1(), 0);
    createFromSystemHeap();
    createEffectWork();
    RPSysSaveDataMgr::CreateInstance(sConfigData.getRootHeapMem2());
    RPSysHomeMenuMgr::CreateInstance(sConfigData.getRootHeapMem2());
    RPSysNWC24Manager::initialize();
    VISetTrapFilter(TRUE);
    VISetTrapFilter(FALSE);
    sConfigData.mPerfView->setVisible(false);
    RP_seedRandomFromTime();
    RPGrpRenderer::SetClearColor(255, 255, 255);
    EGG::SceneManager::setHeapOptionFlg(1);
    RPSysFontManager::instance()->LoadRomFont();
    u8 soundMode = SCGetSoundMode();
    RPSndAudioMgr::instance()->setOutputMode(soundMode);
    RPSysCoreController::applySetting();
    waitRenderModeChange();
    RPSysSceneCreator::instance()->changeSceneAfterFade(0, false);
    if (mFrameRate) {
        OSSetResetCallback(softResetCallBack);
        OSSetPowerCallback(shutdownSystemCallBack);
    } else {
        OSSetResetCallback(NULL);
        OSSetPowerCallback(NULL);
    }
}
void RPSysSystem::mainLoop() {
    while (true) {
        sConfigData.mDisplay->beginFrame();
        sConfigData.mPerfView->measureBeginFrame();
        RPSysDvdStatus::instance()->update();
        RPSysSceneMgr::instance()->getCurrentSceneRP()->getRenderer()->PreDraw();
        sConfigData.mDisplay->beginRender();
        sConfigData.mPerfView->measureBeginRender();
        RPSysSceneMgr::instance()->draw();
        sConfigData.mPerfView->draw();
        sConfigData.mPerfView->measureEndRender();
        EGG::CoreControllerMgr::instance()->beginFrame();
        RPSysSceneMgr::instance()->calc();
        EGG::CoreControllerMgr::instance()->endFrame();
        RPSndAudioMgr::instance()->calc();
        sConfigData.mPerfView->measureEndFrame();
        RPSysDvdStatus::instance()->draw();
        RPSysHomeMenuMgr::instance()->drawBanIcon();
        RPSysSceneMgr::instance()->getCurrentSceneRP()->getRenderer()->PostDraw();
        sConfigData.mDisplay->endRender();
        sConfigData.mDisplay->endFrame();
    }
}
void RPSysSystem::loadFrameWork(bool hide) {
    EGG::CoreControllerMgr::instance()->endFrame();
    RPSysDvdStatus::instance()->draw();
    RPSysHomeMenuMgr::instance()->drawBanIcon();
    sConfigData.mDisplay->endRender();
    sConfigData.mDisplay->endFrame();
    sConfigData.mDisplay->beginFrame();
    RPSysDvdStatus::instance()->update();
    sConfigData.mDisplay->beginRender();
    if (hide) {
        RPSysSceneMgr::instance()->drawMgrFader();
    } else {
        RPGrpRenderer::Begin();
        RPSysTextWriter::instance()->Begin();
        RPSysTextWriter::instance()->SetDrawFlag(0);
        RPSysTextWriter::instance()->SetScale(1.5f, 1.5f);
        RPSysTextWriter::instance()->SetTextColor(nw4r::ut::Color(0xFFFFFFFF));
        RPSysTextWriter::instance()->Printf(10.0f, 410.0f, lbl_80316CC8 + 14);
        RPSysTextWriter::instance()->End();
        RPGrpRenderer::End();
        RPSysSceneMgr::instance()->drawSceneFader();
    }
    EGG::CoreControllerMgr::instance()->beginFrame();
    mLoadCount += unk4D;
    if (RPSysSceneMgr::instance()->isShutDownReserved()) systemShutDown();
}

#pragma push
#pragma auto_inline off
void RPSysSystem::setupTVMode() {
    VIInit();
    for (int i = 0; i < 60; i++) {
        if (SCCheckStatus() == 0) break;
        VIWaitForRetrace();
    }
    if (SCGetAspectRatio() == 1) EGG::Screen::SetTVMode(EGG::Screen::TV_MODE_WIDE);
    else EGG::Screen::SetTVMode(EGG::Screen::TV_MODE_STD);
}
#pragma pop

void RPSysSystem::setupRenderMode() {
    if (VIGetDTVStatus() == 1 && SCGetProgressiveMode() == 1)
        sRenderModeFormat = RPSysRenderMode::EFormat_NtscProg;
    else sRenderModeFormat = RPSysRenderMode::EFormat_Ntsc;
    spRenderModeObj = RPSysRenderMode::getRenderMode(static_cast<RPSysRenderMode::EFormat>(sRenderModeFormat));
    RPSysRenderMode::adjust(&spRenderModeObj->viXOrigin, &spRenderModeObj->viWidth,
                           sRenderModeFormat, EGG::Screen::GetTVMode() == EGG::Screen::TV_MODE_WIDE);
    if (static_cast<u32>(sRenderModeFormat) == RPSysRenderMode::EFormat_NtscProg ||
        static_cast<u32>(sRenderModeFormat) == RPSysRenderMode::EFormat_PalProg) {
        if (static_cast<u32>(VIGetScanMode()) == 2) sRenderModeWaitStart = 0;
        else sRenderModeWaitStart = OS_TICKS_TO_MSEC(OSGetTime());
    } else if (static_cast<u32>(sRenderModeFormat) == RPSysRenderMode::EFormat_Pal60) {
        if (static_cast<u32>(VIGetTvFormat()) == 5) sRenderModeWaitStart = 0;
        else sRenderModeWaitStart = OS_TICKS_TO_MSEC(OSGetTime());
    } else sRenderModeWaitStart = 0;
}

u8 RPSysSystem::getFrameRate() const { return unk4D; }
f32 RPSysSystem::getModifyRatio() const {
    return static_cast<EGG::AsyncDisplay*>(sConfigData.mDisplay)->getTimeScale();
}
u16 RPSysSystem::getFBWidth() const { return spRenderModeObj->fbWidth; }
u16 RPSysSystem::getFBHeight() const { return spRenderModeObj->efbHeight; }

#pragma push
#pragma auto_inline off
extern "C" void Sys_Display_setClearColor(EGG::Display* display, nw4r::ut::Color color) {
    display->setClearColor(color);
}
#pragma pop
void RPSysSystem::setDisplayClearColor(nw4r::ut::Color color) {
    Sys_Display_setClearColor(sConfigData.mDisplay, color);
}

RPSysSceneCreator::ESceneID RPSysSystem::getBootScene() {
    return static_cast<RPSysSceneCreator::ESceneID>(0x19);
}
void RPSysSystem::destroyHeap(EGG::Heap* pHeap) { pHeap->destroy(); }

RPTime32 RPSysSystem::getCalendarTime32() {
    OSCalendarTime time;
    OSTicksToCalendarTime(OSGetTime(), &time);
    return time.min | ((time.hour << 6 | time.mday << 11) | (time.year << 20 | time.month << 16));
}
RPTime16 RPSysSystem::getCalendarTime16() {
    OSCalendarTime time;
    OSTicksToCalendarTime(OSGetTime(), &time);
    return time.mday | ((time.year - 2000) << 9 | time.month << 5);
}
void RPSysSystem::getCalendarTime(const RPTime16* pTime16, u32& rYear, u32& rMon, u32& rMDay) {
    if (pTime16 != NULL) {
        rYear = ((static_cast<u32>(*pTime16) >> 9) & 0x7F) + 2000;
        rMon = ((*pTime16 >> 5) & 15) + 1;
        rMDay = *pTime16 & 31;
    } else {
        OSCalendarTime time;
        OSTicksToCalendarTime(OSGetTime(), &time);
        rYear = time.year;
        rMon = time.month + 1;
        rMDay = time.mday;
    }
}
void RPSysSystem::startLoadCount() { mLoadCount = 0; }
bool RPSysSystem::isLoadCountEnd() const { return mLoadCount > 300; }
void RPSysSystem::createTimeStamp() {
    EGG::DvdRipper::loadToMainRAM(lbl_80316CC8,
        reinterpret_cast<u8*>(mpTimeStampString), NULL,
        EGG::DvdRipper::ALLOC_DIR_HEAD, 0, NULL, NULL);
}
void RPSysSystem::systemShutDown() {
    RPSysCoreController::stopMotorAll();
    VISetBlack(TRUE);
    for (int i = 0; i < 300; i++) {
        bool finish = false;
        VIFlush();
        VIWaitForRetrace();
        if (i < 10) {
            if (RPSysSaveDataMgr::instance()->isNandAccessFinish() &&
                RPSndAudioMgr::instance()->mShutDownStatusRP == RPSndAudioMgr::SHUTDOWN_STATUS_FINISH)
                finish = true;
        } else {
            if (!mpNandThread->isTaskExist()) finish = true;
        }
        if (finish) break;
    }
    if (RPSysSceneMgr::instance()->isShutDownReserved()) {
        RPSysCoreController::shutdownSystem(false);
        OSShutdownSystem();
    } else {
        RPSysCoreController::shutdownSystem(true);
        OSReturnToMenu();
    }
}
void RPSysSystem::setDimming(BOOL dim) { VIEnableDimming(dim); }
EGG::TaskThread* RPSysSystem::getNandThread() { return mpNandThread; }
EGG::TaskThread* RPSysSystem::getDvdThread() { return mpDvdThread; }
EGG::TaskThread* RPSysSystem::getWc24Thread() { return mpWc24Thread; }
// NONMATCHING-DEBT: both getters are const (mangled CFv) and return the message slot's address
// as an OSMessage (void*), so the const is cast away (mutable members would misstate them).
OSMessage RPSysSystem::getNandEndMessage() const { return const_cast<u32*>(&mNandEndMessage); }
OSMessage RPSysSystem::getDvdEndMessage() const { return const_cast<u32*>(&mDvdEndMessage); }
const char* RPSysSystem::getTimeStampString() { return mpTimeStampString; }
RPSysSystem::~RPSysSystem() {}
RPSysSystem::RPSysSystem() {
    mEffectWorkSize = 0;
    mpResourceHeap = NULL;
    mpReserveHeap = NULL;
    mpAssertHeap = NULL;
    HEAP_0x14 = NULL;
    mpCurrentHeap = NULL;
    OSInitMutex(&mCurrentHeapMutex);
    mpNandThread = NULL;
    mpDvdThread = NULL;
    mpWc24Thread = NULL;
    mNandEndMessage = 'nand';
    mDvdEndMessage = 'disk';
    mWc24EndMessage = 'wc24';
    mFrameRate = 1;
    unk4D = 1;
    sConfigData.mDisplay->setFrameRate(1);
    mLoadCount = 0;
    mpTimeStampString = NULL;
    mFrameTime = unk4D / 60.0f;
}
void RPSysSystem::createFromSystemHeap() {
    EGG::Heap* heap = sConfigData.mSystemHeap;
    OSLockMutex(&mCurrentHeapMutex);
    mpCurrentHeap = EGG::Heap::getCurrentHeap();
    heap->becomeCurrentHeap();
    RPSysGameConfig::CreateInstance(sConfigData.mSystemHeap);
    RPSysHostIOSocketManager::CreateInstance();
    RPSysCoreController::initialize();
    sConfigData.mSceneMgr = RPSysSceneMgr::CreateInstance(sConfigData.mSystemHeap);
    RPSysSceneMgr::instance()->setCreator(RPSysSceneCreator::CreateInstance(sConfigData.mSystemHeap));
    RPSysProjectLocal::CreateInstance(sConfigData.mSystemHeap);
    setupLocalSettings();
    RPSysDvdStatus::CreateInstance(sConfigData.mSystemHeap);
    RPGrpRenderer::Initialize(spRenderModeObj->fbWidth, spRenderModeObj->efbHeight);
    RPSysResourceManager::CreateInstance();
    RPSysFontManager::CreateInstance();
    RPSysTextWriter::CreateInstance();
    RPSysTagProcessor::CreateInstance();
    RPSysWideTextWriter::Initialize();
    RPSysLayout::initialize();
    RPSysPauseMgr::CreateInstance(sConfigData.mSystemHeap);
    RPSysPlayerMgr::CreateInstance(sConfigData.mSystemHeap);
    RPSysQueuedScene::CreateInstance(sConfigData.mSystemHeap);
    RPSysEffectMgr::CreateInstance(sConfigData.mSystemHeap);
    RPSysKokeshiManager::CreateInstance();
    RPMessageWindow::CreateInstance(sConfigData.mSystemHeap);
    RPSysSystemWinMgr::CreateInstance(sConfigData.mSystemHeap);
    sConfigData.mAudioMgr = RPSndAudioMgr::CreateInstance(sConfigData.mSystemHeap);
    RPSndSpeakerMgr::CreateInstance(sConfigData.mSystemHeap);
    RPSysCursorDrawMgr::CreateInstance(sConfigData.mSystemHeap);
    mpNandThread = EGG::TaskThread::create(64, 18, 0x4000, NULL);
    mpDvdThread = EGG::TaskThread::create(64, 19, 0x4000, NULL);
    mpWc24Thread = EGG::TaskThread::create(64, 20, 0x4000, NULL);
    OSMessageQueue* queue = EGG::TaskThread::createTaskEndMessageQueue(64, NULL);
    mpNandThread->setEndMessageQueue(queue);
    mpDvdThread->setEndMessageQueue(queue);
    mpWc24Thread->setEndMessageQueue(queue);
    mpTimeStampString = static_cast<char*>(EGG::Heap::alloc(128, 32, NULL));
    mpCurrentHeap->becomeCurrentHeap();
    mpCurrentHeap = NULL;
    OSUnlockMutex(&mCurrentHeapMutex);
}
void RPSysSystem::createEffectWork() {
    EGG::Heap* heap = sConfigData.getRootHeapMem1();
    OSLockMutex(&mCurrentHeapMutex);
    mpCurrentHeap = EGG::Heap::getCurrentHeap();
    heap->becomeCurrentHeap();
    EGG::ExpAudioMgr::ExpAudioMgrArg soundArg;
    soundArg.pHeap = sConfigData.getRootHeapMem1();
    soundArg.soundHeapSize = 0xB00000;
    RPSndAudioMgr::instance()->initialize(&soundArg);
    RPSysEffectMgr::EffectArg effectArg;
    effectArg.pHeap = NULL;
    effectArg.maxEffect = 100;
    effectArg.maxEmitter = 250;
    effectArg.maxParticleManager = 250;
    effectArg.maxParticle = 1500;
    effectArg.pHeap = sConfigData.getRootHeapMem1();
    mEffectWorkSize = RPSysEffectMgr::instance()->createMemoryManager(&effectArg);
    RPSysEffectMgr::instance()->setCreator(new (effectArg.pHeap) RPSysEffectCreator(1), 1);
    RPSysEffectMgr::instance()->setCreator(new (effectArg.pHeap) RPSysEffectCreator(2), 2);
    mpCurrentHeap->becomeCurrentHeap();
    mpCurrentHeap = NULL;
    OSUnlockMutex(&mCurrentHeapMutex);
}
void RPSysSystem::setupLocalSettings() {
    u8 language = SCGetLanguage();
    if (language == 1) {
        RPSysProjectLocal::instance()->setLocale(RPSysProjectLocal::EArea_USA);
        RPSysProjectLocal::instance()->setLanguage(RPSysProjectLocal::EArea_USA);
    } else if (language == 3) {
        RPSysProjectLocal::instance()->setLocale(RPSysProjectLocal::EArea_USA);
        RPSysProjectLocal::instance()->setLanguage(RPSysProjectLocal::EArea_France);
    } else if (language == 4) {
        RPSysProjectLocal::instance()->setLocale(RPSysProjectLocal::EArea_USA);
        RPSysProjectLocal::instance()->setLanguage(RPSysProjectLocal::EArea_Spain);
    } else {
        RPSysProjectLocal::instance()->setLocale(RPSysProjectLocal::EArea_USA);
        RPSysProjectLocal::instance()->setLanguage(RPSysProjectLocal::EArea_USA);
    }
}
#pragma push
#pragma auto_inline off
void RPSysSystem::waitRenderModeChange() {
    if (sRenderModeWaitStart != 0) {
        VISetBlack(TRUE);
        u32 end = sRenderModeWaitStart + 1600;
        for (int i = 0; i < 100; i++) {
            if (static_cast<u32>(OS_TICKS_TO_MSEC(OSGetTime())) > end) break;
            VIFlush();
            VIWaitForRetrace();
        }
    }
}
#pragma pop

BOOL RPSysSystem::checkDisk() {
    DVDCommandBlock block;
    lbl_8043C450[0] = 0;
    DVDCheckDiskAsync(&block, RPSysSystem_checkDiskCallback);
    while (lbl_8043C450[0] == 0) {
        VIWaitForRetrace();
    }
    return lbl_8043C450[0] == 1;
}
void RPSysSystem::setCallBack() {
    if (mFrameRate) {
        OSSetResetCallback(softResetCallBack);
        OSSetPowerCallback(shutdownSystemCallBack);
    } else {
        OSSetResetCallback(NULL);
        OSSetPowerCallback(NULL);
    }
}
void RPSysSystem::softResetCallBack() {
    if (RPSysDvdStatus::instance()->isErrorOccured()) {
        RPSysSceneMgr::instance()->returnToMenu(false);
    } else {
        VIEnableDimming(FALSE);
        if (RPSysSceneMgr::instance()->isNormalState() && !RPSysHomeMenuMgr::instance()->softReset()) {
            RPSysSceneMgr::instance()->softReset(false);
        }
    }
}
void RPSysSystem::shutdownSystemCallBack() { RPSysSceneMgr::instance()->shutdownSystem(false); }
extern "C" void RPSysSystem_checkDiskCallback(s32 result, DVDCommandBlock*) {
    u32 status = 2;
    if (result != 0) {
        status = 1;
    }
    lbl_8043C450[0] = status;
}
