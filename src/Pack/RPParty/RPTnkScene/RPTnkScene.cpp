#include <Pack/RPParty/RPTnkScene/RPTnkScene.h>
#include <Pack/RPParty/RPTnkScene/RPTnkSound.h>

#include <Pack/RPGraphics/RPGrpCamera.h>
#include <Pack/RPGraphics/RPGrpLightManager.h>
#include <Pack/RPGraphics/RPGrpModelManager.h>
#include <Pack/RPGraphics/RPGrpModelResManager.h>
#include <Pack/RPGraphics/RPGrpRenderer.h>
#include <Pack/RPGraphics/RPGrpScreen.h>
#include <Pack/RPGraphics/RPGrpView.h>
#include <Pack/RPKernel/RPSysEffectCreator.h>
#include <Pack/RPKernel/RPSysTextWriter.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPMiiIconSet.h>
#include <Pack/RPParty/RPPartyCommon/RPResSlots.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPParty/RPPartyCommon/RPScreenFade.h>
#include <Pack/RPParty/RPTnkScene/RPTnkBlock.h>
#include <Pack/RPParty/RPTnkScene/RPTnkCamera.h>
#include <Pack/RPParty/RPTnkScene/RPTnkCursor.h>
#include <Pack/RPParty/RPTnkScene/RPTnkField.h>
#include <Pack/RPParty/RPTnkScene/RPTnkGameManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkLyt.h>
#include <Pack/RPParty/RPTnkScene/RPTnkMine.h>
#include <Pack/RPParty/RPTnkScene/RPTnkShell.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTankManager.h>
#include <Pack/RPParty/RPTnkScene/RPTnkTask.h>
#include <Pack/RPParty/RPTnkScene/RPTnkWeaponManager.h>

extern "C" void RP_seedRandomTicks();
extern "C" void RP_ModelObj_setDrawGroup(u32 group);
extern "C" void RP_ResSlots_createInstance();
extern "C" void RP_ResSlots_init(RPResSlots*, u16);
extern "C" RPResSlots* RP_resSlotsInstance;
struct TnkLightPosition {
    nw4r::math::_VEC3 position;
    u32 padding;
};

extern "C" {
TnkLightPosition Tnk_lightPos;
RPTnkScene* __dt__10RPTnkSceneFv(RPTnkScene*, s32);
}

extern "C" RPTnkScene* Tnk_Scene_dtorThunk(void* pObject, s32 deleting) {
    return __dt__10RPTnkSceneFv(
        reinterpret_cast<RPTnkScene*>(static_cast<u8*>(pObject) - 0x24), deleting);
}

extern "C" void Tnk_Scene_drawThunk(void* pObject) {
    reinterpret_cast<RPTnkScene*>(static_cast<u8*>(pObject) - 0x24)->RPTnkScene::UserDraw();
}

extern "C" void Tnk_initLightPosConst() {
    Tnk_lightPos.position.x = 0.0f;
    Tnk_lightPos.position.y = 10000.0f;
    Tnk_lightPos.position.z = 0.0f;
}

#if defined(PORT_HOST)
// GCC has no __declspec(section); a constructor function runs at static initialisation as the
// .ctors entry does
__attribute__((constructor)) static void Tnk_initLightPosHost() {
    Tnk_initLightPosConst();
}
#else
__declspec(section ".ctors") static void (*const sTnkInitLightPos)() = Tnk_initLightPosConst;
#endif

// NONMATCHING-DEBT: compiler-made vtables come out as __vt__10RPTnkScene before
// __vt__15RPTnkSceneSound (whatever the dtor/include order) and the @36@ thunks land after
// RPTnkCamera::ProjectToScreen, so the original's .data/.text order needs these hand tables.
extern "C" {
void calc__10RPSysSceneFv();
void draw__10RPSysSceneFv();
void enter__10RPSysSceneFv();
void exit__10RPSysSceneFv();
void reinit__10RPSysSceneFv();
void incoming_childDestroy__10RPSysSceneFv();
void outgoing_childCreate__10RPSysSceneFv();
void DebugDraw__16IRPGrpDrawObjectFv();
void VF_0x3C__10RPSysSceneFv();
void pauseCallBack__10RPTnkSceneFb();
void Configure__10RPTnkSceneFv();
void LoadResource__10RPTnkSceneFv();
void Reset__10RPTnkSceneFv();
void Calculate__10RPTnkSceneFv();
void Exit__10RPTnkSceneFv();
void taskAsync__10RPSysSceneFv();
void UserDraw__10RPTnkSceneFv();
void __dt__15RPTnkSceneSoundFv();
void initSound3D__15RPTnkSceneSoundFl();

typedef void (*TnkVtableEntry)();
TnkVtableEntry __vt__15RPTnkSceneSound[] = {
    NULL, NULL, __dt__15RPTnkSceneSoundFv, initSound3D__15RPTnkSceneSoundFl
};
TnkVtableEntry __vt__10RPTnkScene[] = {
    NULL, NULL, reinterpret_cast<TnkVtableEntry>(__dt__10RPTnkSceneFv),
    calc__10RPSysSceneFv, draw__10RPSysSceneFv, enter__10RPSysSceneFv,
    exit__10RPSysSceneFv, reinit__10RPSysSceneFv,
    incoming_childDestroy__10RPSysSceneFv, outgoing_childCreate__10RPSysSceneFv,
    NULL, NULL, reinterpret_cast<TnkVtableEntry>(Tnk_Scene_dtorThunk),
    reinterpret_cast<TnkVtableEntry>(Tnk_Scene_drawThunk),
    DebugDraw__16IRPGrpDrawObjectFv, VF_0x3C__10RPSysSceneFv,
    pauseCallBack__10RPTnkSceneFb, Configure__10RPTnkSceneFv,
    LoadResource__10RPTnkSceneFv, Reset__10RPTnkSceneFv,
    Calculate__10RPTnkSceneFv, Exit__10RPTnkSceneFv,
    taskAsync__10RPSysSceneFv, UserDraw__10RPTnkSceneFv
};
}

RPTnkSceneSound::~RPTnkSceneSound() {}

RPTnkScene::~RPTnkScene() {}

void RPTnkScene::CreateCamerasAndScreens() {
    RPGrpRenderer* pRenderer = RPGrpRenderer::GetCurrent();
    mpCamera = new RPGrpCamera;
    RPTnkCamera::CreateInstance();
    mpFieldCamera = RPTnkCamera::sInstance;
    mpFieldCamera->Reset();

    if (EGG::Screen::GetTVMode() == EGG::Screen::TV_MODE_WIDE) {
        mpScreen3D = new RPGrpScreen(0.0f, 0.0f, 624.0f, 494.0f);
    } else {
        mpScreen3D = new RPGrpScreen(0.0f, 0.0f, 468.0f, 494.0f);
    }
    mpScreen3D->SetVpSizeAbsolute();
    mpScreen3D->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    pRenderer->CreateView3D(0, mpCamera, mpScreen3D);
    RPGrpView* pView = pRenderer->GetView(0);
    pView->SetProjType(RPGrpView::EProjType_Ortho);
    pView->SetDrawScene(1);

    mpScreenField = new RPGrpScreen;
    mpScreenField->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    mpScreenField->SetScale(nw4r::math::VEC3(1.125f, 1.125f, 1.125f));
    pRenderer->CreateView3D(1, mpFieldCamera, mpScreenField);
    pView = pRenderer->GetView(1);
    pView->SetProjType(RPGrpView::EProjType_Ortho);
    pView->SetDrawScene(0);
    mpScreenField->SetCanvasMode(EGG::Frustum::CANVAS_CC);

    mpScreen2D = new RPGrpScreen;
    mpScreen2D->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    pRenderer->CreateView2D(2, mpScreen2D);
    pView = pRenderer->GetView(2);
    pView->SetDrawScene(2);
    pView->CreateScreenEffect();
    pRenderer->CreateView2D(3, mpScreen2D);
    pRenderer->GetView(3)->SetDrawScene(3);
    pRenderer->CreateView2D(4, mpScreen2D);
    pRenderer->GetView(4)->SetDrawScene(4);
    pRenderer->LinkViews();
}

void RPTnkScene::UpdateGameplay() {
    RPTnkWeaponManager* pWeapons = mpWeaponManager;
    pWeapons->GetShellPool()->CountShellsPerOwner();
    pWeapons->GetMinePool()->CountByOwner();
    mStepped = FALSE;
    if (!mpGameManager->IsFreeze()) {
        mpTankManager->ApplyVelocities();
        mpWeaponManager->ApplyVelocity();
        mpTankManager->ClearHitInfo();
        mpWeaponManager->ClearHitFlags();
        mpField->GetBlockPool()->ClearForcesAll();
        mpTankManager->ResolveCollisions();
        mpWeaponManager->CheckCollisions();
        mpField->GetBlockPool()->CollideAllWithField();
        mpTankManager->UpdateTreadDistances();
        mpWeaponManager->IntegrateAll();
        mpField->GetBlockPool()->IntegrateAll();
        mpTankManager->LimitAiFlag110();
        mpTankManager->LimitAiFlag118();
        mpTankManager->CalcTanks();
        mpWeaponManager->CalcAll();
        mpField->GetBlockPool()->CalcAll();
        mpTankManager->IncrementTimers();
        mpWeaponManager->TickAge();
        mpField->GetBlockPool()->TickAge();
        mStepped = TRUE;
        ++mStepCount;
    }
    mpFieldCamera->Calculate();
    mpCursorManager->Calculate();
    mpField->Calculate();
    mpGameManager->Calculate();
}

// The original constructs this local sound parameter object without applying it.
void RPTnkSceneSound::initSound3D(s32 arg) {
    switch (arg) {
    case 0: {
        f32 unit;
        f32 maximum;
        f32 interior;
        f32 one;
        one = 1.0f;
        interior = 2400.0f;
        maximum = 2000.0f;
        unit = 1200.0f;
        struct {
            f32 value[28];
            s32 priority;
            u8 padding[12];
        } param;
        param.priority = 32;
        param.value[1] = one;
        param.value[6] = one;
        param.value[11] = one;
        // NONMATCHING-DEBT: word 12 of this never-read local is an s32 (same shape as the
        // Fsh/Bil/Pnp initSound3D locals); the parameter type it belongs to is unidentified.
        reinterpret_cast<s32&>(param.value[12]) = 32;
        param.value[17] = interior;
        param.value[22] = maximum;
        param.value[27] = unit;
        break;
    }
    }
}

void RPTnkScene::Configure() {
    mPlayerNum = 1;
    if (RPSceneInfo::spInstance->GetPlayerNum() == 1) {
        mPlayerNum = 1;
    } else if (RPSceneInfo::spInstance->GetPlayerNum() == 2) {
        mPlayerNum = 2;
    }
    RP_ModelObj_setDrawGroup(0);
    RP_ResSlots_createInstance();
    mpResSlots = RP_resSlotsInstance;
    RP_ResSlots_init(mpResSlots, 100);
    RPMiiIconSet::CreateInstance();
    mpMiiIconSet = RPMiiIconSet::sInstance;
    RPMiiIconSet::Param iconParam;
    iconParam.mNum = 2;
    iconParam.mWidth = 128;
    iconParam.mHeight = 128;
    mpMiiIconSet->CreateIcons(iconParam);

    RPGrpModelManager* pModels = RPGrpModelManager::GetCurrent();
    pModels->CreateModelScene(0, 0, 1, NULL, NULL);
    pModels->CreateModelScene(1, 0, 1, NULL, NULL);
    pModels->CreateModelScene(2, 0, 1, NULL, NULL);
    pModels->CreateModelScene(3, 0, 1, NULL, NULL);
    pModels->CreateModelScene(4, 0, 1, NULL, NULL);
    RPCommonParam commonParam;
    commonParam.mGame = 8;
    commonParam.mPlayerNum = RPSceneInfo::spInstance->GetPlayerNum();
    RPCommon::CreateInstance(commonParam);
    mpCommon = RPCommon::GetInstance();
    CreateCamerasAndScreens();
    RPTnkField::CreateInstance();
    mpField = RPTnkField::sInstance;
    mpField->Create();
    RPTnkTankManager::CreateInstance();
    mpTankManager = RPTnkTankManager::sInstance;
    mpTankManager->CreateTanks(2, 8);
    RPTnkWeaponManager::CreateInstance();
    mpWeaponManager = RPTnkWeaponManager::sInstance;
    mpWeaponManager->Create();
    RPTnkCursorManager::CreateInstance();
    mpCursorManager = RPTnkCursorManager::sInstance;
    mpCursorManager->CreateCursors(2);
    RPTnkLayoutManager::CreateInstance();
    mpLayoutManager = RPTnkLayoutManager::sInstance;
    mpLayoutManager->Create();
    RPTnkGameManager::CreateInstance();
    mpGameManager = RPTnkGameManager::sInstance;
    mpGameManager->Create();
    RPSndAudioMgr::instance()->configureMiniGame(1);
    mpSound = new RPTnkSceneSound;
    mpSound->initSound3D(0);
    RPSndAudioMgr::instance()->setPlayerNum(RPSceneInfo::spInstance->GetPlayerNum());
}

void RPTnkScene::LoadResource() {
    EGG::Archive* pArchive = RPSysResourceManager::LoadGameCommonArchive();
    RPGrpModelResManager* pResources = RPGrpModelResManager::GetCurrent();
    void* pSceneFile = RPSysResourceManager::GetFileFromArchive(pArchive, "G3D/sceneFile.brres");
    RPGrpHandle handle = pResources->CreateData(RPGrpModelResManager::Type_ResFile, pSceneFile);
    RPGrpLightManager* pLights = static_cast<RPGrpLightManager*>(
        RPGrpModelManager::GetCurrent()->GetLightManager(0));
    pLights->LoadScnLight(handle, "RPScene");
    EGG::LightObject* pLight = pLights->GetLightObj(0);
    // NONMATCHING-DEBT: Tnk_lightPos keeps a POD _VEC3 so its hand-written .ctors initializer
    // stays the only one; a VEC3 member makes the compiler add its own (.ctors/.text differ).
    pLight->SetPos(reinterpret_cast<const nw4r::math::VEC3&>(Tnk_lightPos.position));
    pLight->CalcDirDist();
    mpMiiIconSet->Load();
    pArchive = RPSysResourceManager::LoadGameCommonArchive();
    mpField->Load();
    mpTankManager->Load();
    mpWeaponManager->Load();
    mpCursorManager->Load();
    mpLayoutManager->Load();
    mpGameManager->Load();
    mpCommon->LoadResources();
    void* pBreff = RPSysResourceManager::GetFileFromArchive(pArchive, "basic_tank.breff");
    void* pBreft = RPSysResourceManager::GetFileFromArchive(pArchive, "basic_tank.breft");
    getEffectCreator()->setEffectResource(pBreff, pBreft);
    RPSndAudioMgr::instance()->attachArchive("rp_Tnk_sound.brsar", false, NULL);
    RPSndAudioMgr::instance()->loadGroup(0U, NULL, 0);
}

void RPTnkScene::Reset() {
    RP_seedRandomTicks();
    mpGameManager->ResetForMode(mPlayerNum);
    mpCommon->Reset();
    const nw4r::math::VEC3 position(0.0f, 2000.0f, 0.0f);
    const nw4r::math::VEC3 up(0.0f, 0.0f, -1.0f);
    const nw4r::math::VEC3 target(0.0f, 0.0f, 0.0f);
    mpCamera->SetPosition(position);
    mpCamera->SetTarget(target);
    mpCamera->SetUp(up);
    mpFieldCamera->SetMode(1);
    RPSndAudioMgr* pAudio = RPSndAudioMgr::instance();
    nw4r::math::MTX34 view = mpFieldCamera->GetViewMatrix();
    if (pAudio->getCameraCount() > 0) {
        pAudio->setListenerMatrix(0, view);
    }
    mpField->Reset();
    mpField->SetWorldScale(0.742857f);
    mpTankManager->Reset();
    mpWeaponManager->Reset();
    mpCursorManager->Reset();
    mpLayoutManager->Reset();
    mStepped = FALSE;
    mStepCount = 0;
    RPGrpView::ScreenEffect* pEffect = RPGrpRenderer::GetCurrent()->GetView(2)->GetScreenEffect();
    const GXColor color = {255, 255, 255, 0};
    pEffect->SetColorMultiply(color);
    pEffect->SetBlurAlpha(0);
    pEffect->SetBlurOffset(1.0f);
    for (s32 i = 0; i < 4; ++i) {
        EGG::CoreControllerMgr::instance()->getNthController(i)->setPosParam(0.06f, 1.0f);
    }
    RPSysTextWriter::instance()->SetDrawFlag(0);
    mpGameManager->Start();
}

void RPTnkScene::Calculate() {
    EGG::CoreControllerMgr::instance()->getNthController(0);
    EGG::CoreControllerMgr::instance()->func_800A6F50(0);
    UpdateGameplay();
    mpCamera->CalcMatrix();
    mpFieldCamera->CalcMatrix();
    mpField->UpdateModels();
    mpTankManager->CalcModelMtxAll();
    mpWeaponManager->CalcModelMtx();
    mpLayoutManager->Calculate();
    mpCommon->Calculate();
    RPGrpView::ScreenEffect* pEffect = RPGrpRenderer::GetCurrent()->GetView(2)->GetScreenEffect();
    GXColor color = RPCommon::GetInstance()->GetScreenFade()->GetColor();
    pEffect->SetColorMultiply(color);
    pEffect->SetBlurAlpha(color.a);
    if (mpGameManager->IsQuit()) {
        RPSysSceneCreator::instance()->changeSceneAfterFade(0x1B, false);
    } else if (mpGameManager->IsRetry()) {
        RPSysSceneCreator::instance()->changeSceneAfterFade(0x17, false);
    }
}

void RPTnkScene::Exit() {
    RPSndAudioMgr::instance()->changeScene();
    RPResSlots::DestroyInstance();
    RPMiiIconSet::DestroyInstance();
    mpField->Exit();
    RPTnkField::DestroyInstance();
    mpTankManager->Exit();
    RPTnkTankManager::DestroyInstance();
    mpWeaponManager->Exit();
    RPTnkWeaponManager::DestroyInstance();
    mpCursorManager->Exit();
    RPTnkCursorManager::DestroyInstance();
    mpLayoutManager->Exit();
    RPTnkLayoutManager::DestroyInstance();
    mpGameManager->Exit();
    RPTnkGameManager::DestroyInstance();
    RPCommon::DestroyInstance();
    RPTnkCamera::DestroyInstance();
}

void RPTnkScene::UserDraw() {
    if (RPGrpRenderer::GetCurrentViewNo() == 0 && RPGrpRenderer::GetDrawStep() == 3) {
        RPTnkStepTimer* pTimer = RPTnkStepTimer::sInstance;
        if (mStepCount < 10 || pTimer->IsClear()) {
            mpField->DrawClearQuad();
            pTimer->SetClear(false);
        } else if (!pTimer->IsClear()) {
            mpField->DrawCaptureTexture(pTimer->GetSteps());
            mpTankManager->DrawTrackMarks();
        }
        mpField->CaptureTexture();
    }
    if (RPGrpRenderer::GetCurrentViewNo() == 1) {
        if (RPGrpRenderer::GetDrawStep() == 2) {
            mpField->DrawPlainQuad();
        }
    } else if (RPGrpRenderer::GetCurrentViewNo() == 2 && RPGrpRenderer::GetDrawStep() == 5) {
        mpLayoutManager->Draw2D();
    }
}

void RPTnkScene::pauseCallBack(bool enter) {
    if (enter) {
        mpCursorManager->SetDisplayModeAll(0);
    }
}

#pragma defer_codegen off

void RPTnkCamera::ProjectToScreen(nw4r::math::VEC2* pOut, const nw4r::math::VEC3* pPos) const {
    nw4r::math::VEC3 scale =
        RPGrpRenderer::GetCurrent()->GetView(1)->GetScreen()->GetScale();
    nw4r::math::VEC3 viewPos;
    PSMTXMultVec(GetViewMatrix(), *pPos, viewPos);
    pOut->x = viewPos.x / scale.x;
    pOut->y = viewPos.y / scale.y;
}
