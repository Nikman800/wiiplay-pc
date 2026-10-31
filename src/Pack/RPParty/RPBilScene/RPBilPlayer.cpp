#if defined(PORT_HOST)
// C++ wants the explicit specialization below declared before RPBilPlayer.h's inline
// ChangeState uses it (CodeWarrior does not, and the declaration shifts its label numbers)
#include <Pack/RPParty/RPPartyCommon/RPStateMachine.h>
class RPBilPlayer;
template <> void RPStateMachine<RPBilPlayer>::ChangeState(s32 state);
#endif

#include <Pack/RPParty/RPBilScene/RPBilPlayer.h>

#include <Pack/RPParty/RPBilScene/RPBilBall.h>
#include <Pack/RPParty/RPBilScene/RPBilBallMgr.h>
#include <Pack/RPParty/RPBilScene/RPBilCamera.h>
#include <Pack/RPParty/RPBilScene/RPBilGameMgr.h>
#include <Pack/RPParty/RPBilScene/RPBilHud.h>
#include <Pack/RPParty/RPBilScene/RPBilLyt_4.h>
#include <Pack/RPParty/RPBilScene/RPBilMarker.h>
#include <Pack/RPParty/RPBilScene/RPBilScene.h>
#include <Pack/RPParty/RPBilScene/RPBilTable.h>
#include <Pack/RPParty/RPPartyCommon/RPCommon.h>
#include <Pack/RPParty/RPPartyCommon/RPLeadCursor.h>
#include <Pack/RPParty/RPPartyCommon/RPPauseText.h>
#include <Pack/RPParty/RPPartyCommon/RPStateMachine.h>
#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPGraphics/RPGrpModelAnm.h>
#include <Pack/RPGraphics/RPGrpRenderer.h>
#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPKernel/RPSysEffectBase.h>
#include <Pack/RPSystem/RPSysSceneMgr.h>

#include <egg/core.h>
#include <egg/gfxe.h>

#include <revolution/PAD/Pad.h>

// The address of a temporary (CodeWarrior accepts `&T(...)`; the host form is in
// port/host/include/decomp/compat.h)
#if !defined(PORT_TEMP_PTR)
#define PORT_TEMP_PTR(x) (&(x))
#endif

extern void _savegpr_22();
extern void _restgpr_27();
extern "C" void __construct_array(void*, void (*)(), void (*)(), u32, u32);
extern "C" void __destroy_arr(void*, void (*)(), u32, u32);
extern "C" void __ct__Q23EGG8Vector2fFv();
extern "C" void __dt__Q23EGG8Vector2fFv();
extern "C" EGG::Vector3f getAccel__Q23EGG14CoreControllerCFv(const EGG::CoreController*);
extern "C" s32 getDpdValidFlag__Q23EGG14CoreControllerCFv(const EGG::CoreController*);
extern "C" void RP_PhysSphere_applyImpulse(RPPhysSphere*, const nw4r::math::VEC3&,
    const nw4r::math::VEC3&, f32, f32, f32);
extern "C" EGG::Vector3f __mi__Q23EGG8Vector3fCFv(const nw4r::math::VEC3*);
extern "C" EGG::Vector3f cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
    const EGG::Vector3f*, const EGG::Vector3f&);
extern "C" EGG::Vector3f __ml__Q23EGG8Vector3fCFf(const nw4r::math::VEC3*, f32);
extern "C" EGG::Vector3f __dv__Q23EGG8Vector3fCFf(const nw4r::math::VEC3*, f32);
extern "C" EGG::Vector3f __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
    const nw4r::math::VEC3*, const EGG::Vector3f&);
extern "C" EGG::Vector3f __pl__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
    const nw4r::math::VEC3*, const EGG::Vector3f&);
extern "C" EGG::Vector2f EGG_Vector2f_operator_minus(
    const nw4r::math::VEC2*, const nw4r::math::VEC2&);

template <> void TRPSndObject<8>::set3DPosition(const nw4r::math::VEC3&);
template <> nw4r::snd::SoundHandle* TRPSndObject<8>::startSound(unsigned int, u32);
template <> void TRPSndObject<8>::detailStartSound(nw4r::snd::SoundHandle*, u32, u32);

class BilVector3f : public EGG::Vector3f {
public:
    BilVector3f(const nw4r::math::VEC3& rSrc) {
        set(rSrc.x, rSrc.y, rSrc.z);
    }
};

static inline void Bil_setVector(EGG::Vector3f& rDst, const nw4r::math::VEC3& rSrc) {
    rDst.set(rSrc.x, rSrc.y, rSrc.z);
}

static inline void Bil_setVector2(EGG::Vector2f& rDst, f32 x, f32 y) {
    EGG::Vector2f pos(x, y);
    rDst = pos;
}



static inline void Bil_subVector(EGG::Vector3f& rDst, const nw4r::math::VEC3& rSrc) {
    EGG::Vector3f sub(rSrc.x, rSrc.y, rSrc.z);
    rDst -= sub;
}


static inline bool Bil_isZeroSquared(const EGG::Vector3f& rVec) {
    return rVec.squaredLength() <= EGG::Math<f32>::epsilon();
}

static inline s32 Bil_limitStat(s32 value, s32 limit) {
    if (value > limit) {
        value = limit;
    }
    return value;
}

static inline f32 Bil_clamp(f32 value, f32 min, f32 max) {
    if (value < min) {
        value = min;
    } else if (value > max) {
        value = max;
    }
    return value;
}

static inline u16 Bil_halfWidth() {
    return EGG::Screen::GetSizeXMax() / 2;
}

static inline u16 Bil_halfHeight() {
    return EGG::Screen::GetSizeYMax() / 2;
}

static inline bool Bil_screenToPlane(RPGrpCamera* pCamera, nw4r::math::VEC3* pOut,
    const nw4r::math::VEC3& rPoint, const nw4r::math::VEC3& rNormal,
    const RPGrpScreen* pScreen, const EGG::Vector2f& rPos) {
    return pCamera->GetScreenToPlane(pOut, rPoint, rNormal, pScreen, rPos.x, rPos.y);
}

static inline f32 Bil_abs(f32 value) {
    return value > 0.0f ? value : -value;
}

static inline f32 Bil_min(f32 value, f32 max) {
    if (value > max) value = max;
    return value;
}




static inline f32 Bil_screenDistance(const nw4r::math::VEC2& a,
                                    const nw4r::math::VEC2& b) {
    return EGG_Vector2f_operator_minus(&a, b).length();
}

static inline RPBilBallMgr* Bil_getBallMgr() {
    return RPBilBallMgr::sInstance;
}


static inline bool Bil_startRotateSe(unsigned int id) {
    return RPSndAudioMgr::instance()->startSystemSe(id);
}


static inline f32 Bil_angle(const EGG::Vector3f& a, const EGG::Vector3f& b) {
    f32 dot = a.dot(b);
    return EGG::Math<f32>::atan2(
        cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&a, b).length(), dot);
}

static inline bool Bil_toggleGuide(RPBilTable* pTable, bool show) {
    if (show) {
        if (!pTable->GetUnk80()) {
            pTable->SetUnk80(true);
            RPSndAudioMgr::instance()->startSystemSe(static_cast<unsigned int>(3));
        }
        return true;
    }
    if (pTable->GetUnk80()) {
        pTable->SetUnk80(false);
        RPSndAudioMgr::instance()->startSystemSe(static_cast<unsigned int>(4));
    }
    return false;
}

static inline void Bil_toggleRotateGuide(RPBilTable* pTable, bool show) {
    if (show) {
        if (!pTable->GetUnk80()) {
            pTable->SetUnk80(true);
            RPSndAudioMgr::instance()->startSystemSe(static_cast<unsigned int>(3));
        }
    } else if (pTable->GetUnk80()) {
        pTable->SetUnk80(false);
        RPSndAudioMgr::instance()->startSystemSe(static_cast<unsigned int>(4));
    }
}


static inline f32 Bil_rotateWeight(const EGG::Vector2f& rMotion, f32 one) {
    f32 weight = one;
    if (Bil_abs(rMotion.y) > EGG::Math<f32>::epsilon()) {
        if (Bil_abs(rMotion.y) > EGG::Math<f32>::epsilon()) {
            weight = Bil_min(Bil_abs(rMotion.x) / Bil_abs(rMotion.y), one);
        } else {
            weight = 0.5f;
        }
    }
    return weight;
}

f32 RPBilPlayer::sRemoteParamIdleA = 0.03f;
f32 RPBilPlayer::sRemoteParamIdleB = 0.1f;
f32 RPBilPlayer::sRemoteParamAimA = 0.06f;
f32 RPBilPlayer::sRemoteParamAimB = 0.02f;
f32 RPBilPlayer::sRemoteParamPlaceA = 0.04f;
f32 RPBilPlayer::sRemoteParamPlaceB = 0.05f;
f32 RPBilPlayer::sPointerParamB = 1.0f;
f32 RPBilPlayer::sPointerParamMax = 0.12f;
f32 RPBilPlayer::sRemoteDistNear = 1.0f;
f32 RPBilPlayer::sRemoteDistFar = 2.5f;
f32 RPBilPlayer::sAddressDistOpen = 2.7f;
f32 RPBilPlayer::sAddressDistClose = 2.5f;
f32 RPBilPlayer::sPointerTargetRange = 50.0f;
s32 RPBilPlayer::sAimHintFrames = 100;
f32 RPBilPlayer::sStrokeParam0 = 0.2f;
f32 RPBilPlayer::sStrokeParam1 = 0.5f;
f32 RPBilPlayer::sCuePullRate = 0.1f;
f32 RPBilPlayer::sStrokeMaxPower = 7500.0f;
f32 RPBilPlayer::sStrokeParam2 = 40.0f;
f32 RPBilPlayer::sStrokeParam3 = 0.5f;
f32 RPBilPlayer::sStrokeParam4 = 0.9f;
f32 RPBilPlayer::sStrokeParam5 = 10.0f;
f32 RPBilPlayer::sStrokeParam6 = 150.0f;
f32 RPBilPlayer::sStrokeParam7 = 50.0f;
f32 RPBilPlayer::sStrokeParam8 = 0.3f;
f32 RPBilPlayer::sStrokeParam9 = 0.6f;
f32 RPBilPlayer::sCueDist = 40.0f;
f32 RPBilPlayer::sCueParamA = 0.1f;
f32 RPBilPlayer::sCueParamB = 0.3f;
u8 RPBilPlayer::sAddressByte = 100;
s32 RPBilPlayer::sAfterShotFrames = 80;
f32 RPBilPlayer::sArrowParamA = -1.0f;
f32 RPBilPlayer::sArrowParamB[2] = {2.5f, 0.0f};

f32 RPBilPlayer::sPointerParamMin;
f32 RPBilPlayer::sRotateSpeedX = EGG::Math<f32>::pi() * 0.0001f;
f32 RPBilPlayer::sRotateSpeedY = EGG::Math<f32>::pi() * 0.0001f;
f32 RPBilPlayer::sPlayerConstD4 = 100.0f * RPBilPlayer::sRotateSpeedX;
f32 RPBilPlayer::sPlayerConstD8 = 100.0f * RPBilPlayer::sRotateSpeedY;
f32 RPBilPlayer::sAimTurnSlow = EGG::Math<f32>::pi() * 0.0005f;
f32 RPBilPlayer::sAimTurnMid = EGG::Math<f32>::pi() * 0.002f;
f32 RPBilPlayer::sAimTurnFast = EGG::Math<f32>::pi() * 0.005f;
f32 RPBilPlayer::sStrokeParamD[2];
RPBilPlayerMgr* RPBilPlayerMgr::sInstance;

RPBilPlayer::~RPBilPlayer() {}

#pragma auto_inline off
template <> void RPStateMachine<RPBilPlayer>::CallExit() {
    if (mpExitFuncs[mState] != NULL) {
        (mpOwner->*mpExitFuncs[mState])();
    }
}

template <> void RPStateMachine<RPBilPlayer>::CallCalc() {
    if (mpCalcFuncs[mState] != NULL) {
        (mpOwner->*mpCalcFuncs[mState])();
    }
}

template <> void RPStateMachine<RPBilPlayer>::CallEnter() {
    if (mpEnterFuncs[mState] != NULL) {
        (mpOwner->*mpEnterFuncs[mState])();
    }
}

template <> void RPStateMachine<RPBilPlayer>::Calculate() {
    if (mIsChanging) {
        if (mCallExit) {
            CallExit();
        }

        mPrevState = mState;
        mState = mNextState;
        mIsChanging = false;
        mTimer = 0;
        CallEnter();
    }

    CallCalc();

    if (mTimer < 0x7FFFFFFF) {
        mTimer++;
    }
}

#pragma dont_inline on
template <> void RPStateMachine<RPBilPlayer>::ChangeState(s32 state) {
    mNextState = state;
    mIsChanging = true;
    mCallExit = true;
}
#pragma dont_inline off

template <> void RPStateMachine<RPBilPlayer>::Reset(s32 state) {
    mPrevState = state;
    mState = state;
    mNextState = state;
    mIsChanging = true;
    mCallExit = false;
    mTimer = 0;
}

template <> void RPStateMachine<RPBilPlayer>::SetStateFuncs(
    const StateFunc& rEnter, const StateFunc& rCalc, s32 state) {
    mpEnterFuncs[state] = rEnter;
    mpCalcFuncs[state] = rCalc;
}

#pragma dont_inline on
template <> RPStateMachine<RPBilPlayer>::RPStateMachine(RPBilPlayer* pOwner,
                                                        s32 stateNum) {
    mStateNum = stateNum;
    mpOwner = pOwner;
    mpEnterFuncs = new StateFunc[mStateNum];
    mpCalcFuncs = new StateFunc[mStateNum];
    mpExitFuncs = new StateFunc[mStateNum];

    for (s32 i = 0; i < mStateNum; i++) {
        mpEnterFuncs[i] = NULL;
        mpCalcFuncs[i] = NULL;
        mpExitFuncs[i] = NULL;
    }
}
#pragma dont_inline off
#pragma auto_inline on

RPBilPlayer::RPBilPlayer(s32 index) {
    mIndex = index;
    mpStateMachine = new RPBilPlayerSeq(this, STATE_NUM);
    RPBilStateFuncs<RPBilPlayer> funcs[STATE_NUM] = {
        {NULL, NULL},
        {&RPBilPlayer::PlaceBallEnter, &RPBilPlayer::PlaceBallCalc},
        {&RPBilPlayer::AimEnter, &RPBilPlayer::AimCalc},
        {&RPBilPlayer::AddressEnter, &RPBilPlayer::AddressCalc},
        {&RPBilPlayer::StrokeEnter, &RPBilPlayer::StrokeCalc},
        {&RPBilPlayer::RotateViewEnter, &RPBilPlayer::RotateViewCalc},
        {&RPBilPlayer::AfterShotEnter, &RPBilPlayer::AfterShotCalc},
        {&RPBilPlayer::WaitEnter, &RPBilPlayer::WaitCalc},
        {NULL, NULL},
    };
    for (s32 i = 0; i < STATE_NUM; i++) {
        mpStateMachine->SetStateFuncs(funcs[i].mEnter, funcs[i].mCalc, i);
    }

    RPGrpRenderer::GetCurrent()->AppendDrawObject(
        static_cast<IRPGrpDrawObject&>(*this));
    mpSound = new RPBilSound3D();
    const char* effectNames[] = {"Bil_camera_arrow_2d"};
    for (int i = 0; i < 1; i++) {
        mpArrowEffect = new RPSysEffect(effectNames[i], 3, 0);
    }
}

void RPBilPlayer::Load() {
    mpCueModel = RPGrpModel::Construct(1, "bad_cue", 1, 0, 0);
    mpCueModel->SetDrawScene(0);

    for (s32 i = 0; i < ARROW_MODEL_NUM; i++) {
        const char* arrowNames[ARROW_MODEL_NUM] = {"bad_arrow_00", "bad_arrow_01"};
        mpArrowModels[i] = RPGrpModel::Construct(7, arrowNames[i], 1, 0, 0);
        mpArrowModels[i]->SetDrawScene(2);
        mpArrowModels[i]->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Chr, 1);
        mpArrowModels[i]->GetModelAnm()->RegistFromResFile(RPGrpModelAnm::Anm_Chr, 0, 7,
                                                  arrowNames[i], 0);
        mpArrowModels[i]->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, 1.0f);
    }

    const char* cursorNames[CURSOR_MODEL_NUM] = {
        "bad_cursor3", "bad_cursor4", "bad_cursor5", "bad_cursor1", "bad_cursor2",
    };
    const u8 cursorGroups[CURSOR_MODEL_NUM] = {1, 1, 2, 2, 2};
    const u8 cursorScenes[CURSOR_MODEL_NUM] = {4, 4, 1, 1, 1};
    for (s32 i = 0; i < CURSOR_MODEL_NUM; i++) {
        RPGrpModel* pModel = RPGrpModel::Construct(6, cursorNames[i],
                                                    cursorScenes[i], 0, 0);
        mpCursorModels[i] = pModel;
        pModel->SetDrawScene(cursorGroups[i]);
    }

    mpCursorModels[4]->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Chr, 1);
    mpCursorModels[4]->GetModelAnm()->RegistFromResFile(
        RPGrpModelAnm::Anm_Chr, 0, 6, "bad_cursor2", 0);
    mpCursorModels[4]->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0, 1.0f);
}

void RPBilPlayer::Reset() {
    mpStateMachine->Reset(STATE_NONE);
    mPointerValid = false;
    mPointerPos.x = 0.0f;
    mPointerPos.y = 0.0f;
    mPointerDist = 1.0f;
    mPrevPointerValid = false;
    mPrevPointerPos = EGG::Vector2f(0.0f, 0.0f);
    mPrevPointerDist = 1.0f;
    mPos = EGG::Vector3f::zero;
    mScore = 0;
    mShotCount = 0;
    mFoulCount = 0;
    mMaxStat = 0;
    mStatusHead = 0;
    mStatusNum = 0;
    mCuePos = EGG::Vector3f::zero;
    mTargetBall = -1;
}

void RPBilPlayer::Exit() {
    PADControlMotor(mChannel, 0);
}

#pragma auto_inline off
template <> void TRPSndObject<8>::set3DPosition(const nw4r::math::VEC3& rPos) {
    for (int i = 0; i < RP_MAX_CONTROLLERS; i++) {
        this->mSound3DActors[i]->SetPosition(rPos);
    }
}
#pragma auto_inline on

void RPBilPlayer::Calculate() {
    s32 state = mpStateMachine->GetState();
    if (state != STATE_NONE && mIndex == RPBilPlayerMgr::sInstance->GetCurrentPlayer()) {
        if (state == STATE_AIM && unk6C == 0) {
            EGG::CoreControllerMgr::instance()->getNthController(mChannel)->setPosParam(
                sRemoteParamAimA, sRemoteParamAimB);
        } else if (state == STATE_PLACE_BALL) {
            EGG::CoreControllerMgr::instance()->getNthController(mChannel)->setPosParam(
                sRemoteParamPlaceA, sRemoteParamPlaceB);
        } else {
            EGG::CoreControllerMgr::instance()->getNthController(mChannel)->setPosParam(
                sRemoteParamIdleA, sRemoteParamIdleB);
        }
        f32 param = sPointerParamMin + (sPointerParamMax - sPointerParamMin) *
            ((mPointerDist - sRemoteDistNear) / (sRemoteDistFar - sRemoteDistNear));
        param = Bil_clamp(param, sPointerParamMin, sPointerParamMax);
        EGG::CoreControllerMgr::instance()->getNthController(mChannel)->setDistParam(
            param, sPointerParamB);
        s32 num = EGG::CoreControllerMgr::instance()->getNthController(mChannel)->getReadLength();
        for (s32 i = 0; i < num; i++) {
            s32 dest = (num + mStatusHead - 1 - i) % STATUS_HISTORY_NUM;
            mStatusHistory[dest] =
                *EGG::CoreControllerMgr::instance()->getNthController(mChannel)->getCoreStatus(i);
        }
        mStatusHead = (mStatusHead + num) % STATUS_HISTORY_NUM;
        mStatusNum += num;
        if (mStatusNum > STATUS_HISTORY_NUM) {
            mStatusNum = STATUS_HISTORY_NUM;
        }
        mPrevPointerValid = mPointerValid;
        mPrevPointerPos = mPointerPos;
        mPrevPointerDist = mPointerDist;
        if (getDpdValidFlag__Q23EGG14CoreControllerCFv(
                EGG::CoreControllerMgr::instance()->getNthController(mChannel)) > 0) {
            mPointerValid = true;
            EGG::Vector2f pos = EGG::CoreControllerMgr::instance()->getNthController(mChannel)->getDpdPos();
            mPointerPos.x = pos.x * Bil_halfWidth();
            mPointerPos.y = -pos.y * Bil_halfHeight();
            mPointerDist = EGG::CoreControllerMgr::instance()->getNthController(mChannel)->getDpdDistance();
        } else {
            mPointerValid = false;
        }
    }
    if (mIndex == RPBilPlayerMgr::sInstance->GetCurrentPlayer()) {
        mpStateMachine->Calculate();
    }
    const bool enabled[STATE_NUM] = {false, true, true, true, false, false, false, false, false};
    RPCommon::GetInstance()->GetLeadCursor()->SetPlayerEnabled(
        mIndex, enabled[mpStateMachine->GetState()]);
    mpSound->set3DPosition(mCuePos);
}

void RPBilPlayer::PlaceBallEnter() {
    RPBilCameraMgr::sInstance->GetCamera()->MoveToDefaultView();
    RPBilBallMgr* pBallMgr = RPBilBallMgr::sInstance;
    pBallMgr->ShowCueGhost(RPBilTable::GetCenter(), false);
    pBallMgr->GetBall(0)->SetUnk70(false);
    RPBilNextBallMarker::sInstance->Show();
    RPBilHud* pHud = RPBilHud::sInstance;
    pHud->ShowPrompt();
    pHud->SetInstruction(1);
    if (mIndex == 0) {
        RPBilMusic::sInstance->GetState()->SetState(0);
    } else {
        RPBilMusic::sInstance->GetState()->SetState(1);
    }
}

void RPBilPlayer::PlaceBallCalc() {
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    if (pCamera->IsActive()) {
        return;
    }
    RPBilHud::sInstance->ShowTurnPanel(mIndex);
    if (!mPointerValid) {
        return;
    }
    EGG::Vector3f pos;
    nw4r::math::VEC3 planePos(0.0f, RPBilTable::GetHeight(), 0.0f);
    nw4r::math::VEC3 planeNormal(0.0f, 1.0f, 0.0f);
    RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(4)->GetScreen();
    EGG::Vector2f screenPos;
    screenPos.x = Bil_clamp(mPointerPos.x, -Bil_halfWidth(), Bil_halfWidth());
    screenPos.y = Bil_clamp(mPointerPos.y, -Bil_halfHeight(), Bil_halfHeight());
    if (Bil_screenToPlane(pCamera, &pos, planePos, planeNormal,
            pScreen, screenPos)) {
        RPBilBallMgr* pBallMgr = RPBilBallMgr::sInstance;
        if (pBallMgr->CanPlaceBall(0, pos)) {
            pBallMgr->ShowCueGhost(pos, true);
            if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->downTrigger(0x400) ||
                EGG::CoreControllerMgr::instance()->getNthController(mChannel)->downTrigger(0x800)) {
                pBallMgr->PlaceCueBall(pos);
                mpStateMachine->ChangeState(STATE_AIM);
                pBallMgr->HideCueGhost();
                pBallMgr->GetBall(0)->PlayPlaceSound();
            }
        } else {
            pBallMgr->ShowCueGhost(pos, false);
        }
    }
}

void RPBilPlayer::AimEnter() {
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    EState prevState = GetPreviousState();
    if (prevState != STATE_ADDRESS && prevState != STATE_STROKE &&
        prevState != STATE_ROTATE_VIEW) {
        RPBilBallMgr* pBallMgr = RPBilBallMgr::sInstance;
        RPBilBall* pCueBall = pBallMgr->GetBall(0);
        RPBilBall* pTarget = pBallMgr->GetBall(pBallMgr->GetLowestBall());
        pCamera->MoveToAimAt(static_cast<const EGG::Vector3f&>(pCueBall->GetPos()),
                             static_cast<const EGG::Vector3f&>(pTarget->GetPos()));
        EGG::Vector3f dir = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
            &pTarget->GetPos(), static_cast<const EGG::Vector3f&>(pCueBall->GetPos()));
        RPBilTable* pTable = RPBilTable::sInstance;
        pTable->SetGuideLine(static_cast<const EGG::Vector3f&>(pCueBall->GetPos()), dir);
    }
    mTargetBall = -1;
    RPBilNextBallMarker::sInstance->Show();
    RPBilHud* pHud = RPBilHud::sInstance;
    pHud->ShowPrompt();
    pHud->SetInstruction(0);
    RPBilHint::sInstance->Request(true);
    prevState = GetPreviousState();
    if (GetPreviousState() == STATE_NONE || GetPreviousState() == STATE_PLACE_BALL ||
        GetPreviousState() == STATE_WAIT || GetPreviousState() == STATE_INACTIVE) {
        RPBilHint::sInstance->SetIdleTimer(80);
    }
    RPBilStrokeGuide::sInstance->SetMode(RPBilStrokeGuide::MODE_NONE);
    RPBilBallNumberTag::sInstance->Hide();
    if (mpStateMachine->GetPrevState() != STATE_ROTATE_VIEW) {
        if (mIndex == 0) {
            RPBilMusic::sInstance->GetState()->SetState(0);
        } else {
            RPBilMusic::sInstance->GetState()->SetState(1);
        }
    }
    unk6864 = 0;
}

void RPBilPlayer::AimCalc() {
    RPBilBallMgr* pBallMgr = RPBilBallMgr::sInstance;
    bool requestHint = true;
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    UpdatePointerTarget();
    if (mPointerValid) {
        switch (unk6C) {
        case 0: mTargetBall = 0; break;
        case 1: mTargetBall = 2; break;
        case 2: mTargetBall = 3; break;
        }
    }
    UpdateAimMarker();
    RPBilTable* pTable;
    if (!pCamera->IsActive()) {
        pTable = RPBilTable::sInstance;
        if (Bil_toggleGuide(pTable,
                EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x800))) {
            requestHint = false;
        }
    }
    if (!pCamera->IsActive()) {
        pCamera->SaveView();
        s32 ball = -1;
        EGG::Vector3f hit;
        if (mPointerValid) {
            RPBilBallMgr::sInstance->RaycastBalls(
                // NONMATCHING-DEBT: mPointerRayDir/mPointerRayOrigin are read as one RPBilSegment;
                // an RPBilSegment member emits __dt__12RPBilSegmentFv, which the original lacks.
                reinterpret_cast<const RPBilSegment&>(mPointerRayDir),
                                     &ball, &hit, 0.0f);
        }
        if (mPointerValid && unk6C == 0 &&
            EGG::CoreControllerMgr::instance()->getNthController(mChannel)->downTrigger(0x400)) {
            if (ball == 0) {
                EGG::Vector3f segment[2];
                segment[0] = mPointerRayOrigin;
                segment[1] = hit;
                if (!RPBilTable::sInstance->SweepHitsKcl(segment, 0.0f)) {
                    mpStateMachine->ChangeState(STATE_ADDRESS);
                    unkC4 = mPointerDist;
                    mpSound->startSound(static_cast<unsigned int>(1), 0);
                }
            }
        } else if (mPointerValid && unk6C == 2 &&
            EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x400)) {
            mpStateMachine->ChangeState(STATE_ROTATE_VIEW);
        }
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(3)) {
            if (static_cast<s32>(unk6864) < sAimHintFrames) {
                unk6864++;
            }
        } else {
            unk6864 = 0;
        }
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(1)) {
            if (static_cast<s32>(unk6864) < sAimHintFrames) {
                pCamera->OrbitYaw(sAimTurnSlow);
            } else {
                pCamera->OrbitYaw(sAimTurnFast);
            }
        }
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(2)) {
            if (static_cast<s32>(unk6864) < sAimHintFrames) {
                pCamera->OrbitYaw(-sAimTurnSlow);
            } else {
                pCamera->OrbitYaw(-sAimTurnFast);
            }
        }
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(8)) {
            pCamera->OrbitPitch(-sAimTurnMid);
        }
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(4)) {
            pCamera->OrbitPitch(sAimTurnMid);
        }
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(15)) {
            requestHint = false;
        }
        pTable = RPBilTable::sInstance;
        pTable->SetGuideLine(static_cast<const EGG::Vector3f&>(pBallMgr->GetBall(0)->GetPos()),
                            BilVector3f(pCamera->GetTargetRef() - pCamera->GetPosition()));
        UpdateCursorModel();
        RPBilHud::sInstance->ShowTurnPanel(mIndex);
        if (ball != -1 && ball != 0 && !RPBilPlayerMgr::sInstance->GetUnkE()) {
            RPBilBallNumberTag::sInstance->Show(ball);
        } else {
            RPBilBallNumberTag::sInstance->Hide();
        }
    }
    RPBilHint* pHint = RPBilHint::sInstance;
    if (requestHint) {
        pHint->Request(false);
    } else {
        pHint->Hide();
    }
    RPBilHud* pHud = RPBilHud::sInstance;
    if (mPointerDist > sAddressDistOpen) {
        pHud->SetInstruction(2);
    } else if (pHud->GetInstruction() != 2 || mPointerDist < sAddressDistClose) {
        pHud->SetInstruction(0);
    }
}

void RPBilPlayer::AddressEnter() {
    mCuePull = 0.0f;
    mCursorAlpha = sAddressByte;
    mTargetBall = 1;
    mpArrowModels[0]->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
    RPBilHud::sInstance->SetInstruction(-1);
    RPBilHint::sInstance->Hide();
    RPBilStrokeGuide::sInstance->SetMode(RPBilStrokeGuide::MODE_PULL);
    RPBilMusic::sInstance->GetState()->SetState(2);
}

void RPBilPlayer::AddressCalc() {
    CalcCueFromCamera();
    UpdateAimMarker();
    RPBilTable* pTable = RPBilTable::sInstance;
    Bil_toggleGuide(pTable,
        EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x800));
    if (!EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x400) &&
        !EGG::CoreControllerMgr::instance()->getNthController(mChannel)->upTrigger(0x400)) {
        mpStateMachine->ChangeState(STATE_AIM);
    } else {
        if (mPointerDist > unkC4 + sCuePullRate) {
            mpStateMachine->ChangeState(STATE_STROKE);
        }
        UpdateCueModel();
        UpdateArrowModel__11RPBilPlayerFv(this, 0);
        UpdateCursorModel();
        RPBilHud* pHud = RPBilHud::sInstance;
        if (mPointerDist > sAddressDistOpen) {
            pHud->SetInstruction(2);
        } else if (pHud->GetInstruction() != 2 && mPointerDist < sAddressDistClose) {
            pHud->SetInstruction(-1);
        }
    }
}

#pragma auto_inline off
template <>
void TRPSndObject<8>::detailStartSound(nw4r::snd::SoundHandle* pHandle, u32 id, u32 handle) {
    if (pHandle->IsAttachedSound()) {
        pHandle->Stop(0);
    }
    this->mSound3DActors[handle]->StartSound(pHandle, id);
    u32 param = RPSndAudioMgr::instance()->getUserParam(
        RPSndUtility::getPlayer()->GetSoundArchive(), id);
    if (param & 0xFFFF0000) {
        f32 mainOut = ((param >> 16) & 0x7F) / 127.0f;
        f32 muteOut = ((param >> 8) & 0x7F) / 127.0f;
        f32 remoteOut = (param >> 24) / 127.0f;
        RPSndSpeakerMgr::instance()->setRemoteSend(pHandle, mRemoteFlag,
                                                mainOut, remoteOut, muteOut);
    }
}

template <>
nw4r::snd::SoundHandle* TRPSndObject<8>::startSound(unsigned int id, u32 handle) {
    if (this->mIsSilenced) {
        return NULL;
    }
    RPSndAudioMgr::instance()->getPlayer().IsAvailable();
    nw4r::snd::SoundHandle* pHandle = NULL;
    if (!BOOL_0x7C) {
        pHandle = FindFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle == NULL) {
        pHandle = FindFreeFishSoundHandle(this->mSoundHandles, this->mHandleCount);
    }
    if (pHandle == NULL) {
        pHandle = FindPriorityFishSoundHandle(this->mSoundHandles, this->mHandleCount, id);
    }
    if (pHandle != NULL) {
        detailStartSound(pHandle, id, handle);
        return pHandle;
    }
    return NULL;
}
#pragma auto_inline on

void RPBilPlayer::StrokeEnter() {
    mCursorAlpha = 255;
    mpArrowModels[1]->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
    mpSound->startSound(static_cast<unsigned int>(2), 0);
    RPBilStrokeGuide::sInstance->SetMode(RPBilStrokeGuide::MODE_PUSH);
    RPBilBallNumberTag::sInstance->Hide();
    unk6868 = EGG::Vector3f(0.0f, -1.0f, 0.0f);
}

void RPBilPlayer::StrokeCalc() {
    int frames;
    RPBilBall* pBall;
    CalcCueFromCamera();
    UpdateAimMarker();
    RPBilTable* pTable = RPBilTable::sInstance;
    Bil_toggleGuide(pTable,
        EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x800));
    if (!EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x400) &&
        !EGG::CoreControllerMgr::instance()->getNthController(mChannel)->upTrigger(0x400)) {
        mpStateMachine->ChangeState(STATE_AIM);
    } else {
        EGG::Vector3f accel = getAccel__Q23EGG14CoreControllerCFv(
            EGG::CoreControllerMgr::instance()->getNthController(mChannel));
        f32 magnitude = accel.normalise();
        if (Bil_abs(magnitude - 1.0f) < sStrokeParam0) {
            unk6868 +=
                __ml__Q23EGG8Vector3fCFf(&accel, sStrokeParam1);
            unk6868.normalise();
        }
        bool pointerShot = false;
        bool releaseShot = false;
        if (mPointerDist < unkC4 || (!mPointerValid && mPointerDist < unkC4 + sCuePullRate)) {
            pointerShot = true;
        } else if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->upTrigger(0x400)) {
            releaseShot = true;
        }
        if ((pointerShot || releaseShot) && mStatusNum > 20) {
            s32 distanceNum = 0;
            EGG::Vector3f accels[20];
            f32 distances[20];
            f32* pDistance = distances;
            EGG::Vector3f* pAccel = accels;
            for (s32 i = 0; i < 20; i++) {
                s32 current = (mStatusHead + 199 - i) % STATUS_HISTORY_NUM;
                s32 previous = (mStatusHead + 198 - i) % STATUS_HISTORY_NUM;
                f32 distance = mStatusHistory[previous].dist - mStatusHistory[current].dist;
                if (mStatusHistory[current].getDPDValidFlag() > 0 &&
                    mStatusHistory[previous].getDPDValidFlag() > 0 && distance < 0.03f) {
                    *pDistance++ = distance;
                    distanceNum++;
                }
                *pAccel++ = EGG::Vector3f(mStatusHistory[current].acc.x,
                                        mStatusHistory[current].acc.y,
                                        mStatusHistory[current].acc.z);
            }
            if ((pointerShot && distanceNum >= 3) || releaseShot) {
                f32 pointerPower;
                f32 accelWeight;
                if (pointerShot) {
                    f32 distance = (mPointerDist - sRemoteDistNear) /
                        (sRemoteDistFar - sRemoteDistNear);
                    if (distance < 0.0f) distance = 0.0f;
                    else if (distance > 1.0f) distance = 1.0f;
                    accelWeight = distance * (sStrokeParam4 - sStrokeParam3) + sStrokeParam3;
                    f32 pointerScale = 1.0f - accelWeight;
                    f32 sum = 0.0f;
                    for (s32 i = 0; i < distanceNum; i++) sum += distances[i];
                    f32 mean = sum / distanceNum;
                    f32 filteredSum = 0.0f;
                    for (s32 i = 0; i < distanceNum; i++) {
                        filteredSum += (distances[i] - mean) * 0.2f + mean;
                    }
const f32 filteredPower = filteredSum / distanceNum;
                    pointerPower = pointerScale * (filteredPower * sStrokeMaxPower);
                } else {
                    pointerPower = 0.0f;
                    accelWeight = 1.0f;
                }
                f32 powers[20];
                for (s32 i = 0; i < 20; i++) {
                    powers[i] = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
                        &accels[i], unk6868).length();
                    if (accels[i].z > unk6868.z) {
                        powers[i] = -powers[i];
                    }
                }
                f32 topPowers[10];
                for (s32 i = 0; i < 10; i++) {
                    f32 max = -999.0f;
                    s32 maxIndex = -1;
                    for (s32 j = 0; j < 20; j++) {
                        if (maxIndex == -1 || powers[j] > max) {
                            maxIndex = j;
                            max = powers[j];
                        }
                    }
                    topPowers[i] = max;
                    powers[maxIndex] = -999.0f;
                }
                f32 sum = 0.0f;
                s32 strongNum = 0;
                for (s32 i = 0; i < 10; i++) {
                    sum += topPowers[i];
                    if (topPowers[i] > 3.4f) {
                        strongNum++;
                    }
                }
                sum /= 10.0f;
                sum *= strongNum / 10.0f + 1.0f;
                f32 accelPower = sum * sStrokeParam2;
                accelPower *= accelWeight;
                if (accelPower < 0.0f) {
                    accelPower = 0.0f;
                }
                f32 power = pointerPower + accelPower;
                if (pointerShot && !mPointerValid && power < sStrokeParam7) {
                    goto strokeFinished;
                }
                if (power < sStrokeParam5 * 0.6f) {
                    if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->upTrigger(0x400)) {
                        mpStateMachine->ChangeState(STATE_AIM);
                    } else {
                        mpStateMachine->ChangeState(STATE_ADDRESS);
                    }
                } else {
                    power = Bil_clamp(power, sStrokeParam5, sStrokeParam6);
                    {
                        RPBilBall* const cue = RPBilBallMgr::sInstance->GetBall(0);
                        pBall = cue;
                    }
                    unk88 = __ml__Q23EGG8Vector3fCFf(&mCueDir, power);
                    unk88 *= Bil_abs(mCueDir.y) * 0.6f + 1.0f;
                    EGG::Vector3f up(0.0f, 1.0f, 0.0f);
                    f32 impulseAngle = Bil_abs(
                        unk88.dot(up) /
                        unk88.length());
                    EGG::Vector3f offset = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
                        &mPointerTarget, static_cast<const EGG::Vector3f&>(pBall->GetPos()));
                    f32 offsetAngle = Bil_abs(offset.dot(up) / offset.length());
                    EGG::Vector3f point = mPointerTarget;
                    Bil_subVector(point, RPBilCameraMgr::sInstance->GetCamera()->GetUp() *
                        pBall->GetPhysSphere()->GetRadius() * 0.15f);
                    RP_PhysSphere_applyImpulse(pBall->GetPhysSphere(), point, unk88, 1.0f,
                        (sStrokeParam9 - sStrokeParam8) *
                            (impulseAngle + offsetAngle) / 2.0f + sStrokeParam8,
                        sStrokeParamD[0]);
                    frames = 4.0f + 26.0f *
                        unk88.length() / sStrokeParam6;
                    EGG::CoreControllerMgr::instance()->getNthController(mChannel)->startSimpleRumble(
                        0xFFFFFFFF, frames, 0x20);
                    pBall->PlayCueHitSound(
                        unk88.length(), 1, mIndex);
                    mpStateMachine->ChangeState(STATE_AFTER_SHOT);
                    RPBilPlayerMgr::sInstance->SetUnkE(false);
                }
strokeFinished:;
            }
        }
        UpdateCueModel();
        UpdateArrowModel__11RPBilPlayerFv(this, 1);
        UpdateCursorModel();
    }
}

void RPBilPlayer::RotateViewEnter() {
    unk6808 = 0.0f;
    unk680C = 0.0f;
    unk6810 = 0;
    EGG::Vector2f unused(0.0f, 0.0f);
    for (s32 i = 0; i < ROTATE_HISTORY_NUM; i++) {
        mRotateHistory[i] = EGG::Vector2f::zero;
    }
    RPBilHint::sInstance->Hide();
    RPBilBallNumberTag::sInstance->Hide();
    mTargetBall = 4;
    mpArrowEffect->create();
    if (EGG::Screen::GetTVMode() == 1) {
        mpArrowEffect->setLocalScale(EGG::Vector3f(1.0f, 1.0f, 1.0f));
    } else {
        mpArrowEffect->setLocalScale(EGG::Vector3f(1.0f, 1.0f, 0.753f));
    }
    mpArrowEffect->setPos(EGG::Vector3f::zero);
}

void RPBilPlayer::RotateViewCalc() {
    // The table toggle and camera update use the same storage in successive phases.
    union {
        RPBilTable* table;
        RPBilCamera* camera;
    } view;
    RPBilBallMgr* pBallMgr = Bil_getBallMgr();
    // Reused for the sound manager and the final guide-table update.
    void* pObject;
    UpdateAimMarker();
    {
        view.table = RPBilTable::sInstance;
        if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x800)) {
            if (!view.table->GetUnk80()) {
                view.table->SetUnk80(true);
                pObject = RPSndAudioMgr::instance();
                static_cast<RPSndAudioMgr*>(pObject)->startSystemSe(static_cast<unsigned int>(3));
            }
        } else if (view.table->GetUnk80()) {
            view.table->SetUnk80(false);
            pObject = RPSndAudioMgr::instance();
            static_cast<RPSndAudioMgr*>(pObject)->startSystemSe(static_cast<unsigned int>(4));
        }
    }
    view.camera = RPBilCameraMgr::sInstance->GetCamera();
    if (mPointerValid && mPrevPointerValid) {
        mRotateHistory[unk6810] = EGG::Vector2f(mPointerPos.x - mPrevPointerPos.x,
                                             mPointerPos.y - mPrevPointerPos.y);
        EGG::Vector2f weights = EGG::Vector2f::zero;
        for (s32 i = 0; i < ROTATE_HISTORY_NUM; i++) {
            const f32 weight = Bil_rotateWeight(mRotateHistory[i], 1.0f);
            weights.x += weight;
            weights.y += 1.0f - weight;
        }
        weights /= 10.0f;
        weights *= 2.0f;
        weights.x = Bil_min(weights.x, 1.0f);
        weights.y = Bil_min(weights.y, 1.0f);
        f32 x = weights.x * (sRotateSpeedX * -(mPointerPos.x - mPrevPointerPos.x));
        f32 y = weights.y * (sRotateSpeedY * -(mPointerPos.y - mPrevPointerPos.y));
        Bil_clamp(x, -sPlayerConstD4, sPlayerConstD4);
        Bil_clamp(y, -sPlayerConstD8, sPlayerConstD8);
        view.camera->OrbitYaw(x);
        view.camera->OrbitPitch(y);
        unk6808 = x;
        unk680C = y;
    } else if (EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x400) &&
               !mPointerValid) {
        view.camera->OrbitYaw(unk6808);
        view.camera->OrbitPitch(unk680C);
        mRotateHistory[unk6810] = EGG::Vector2f(unk6808, unk680C);
    }
    unk6810 = (unk6810 + 1) % ROTATE_HISTORY_NUM;
    mpArrowEffect->update();
    if (!EGG::CoreControllerMgr::instance()->getNthController(mChannel)->down(0x400)) {
        mpStateMachine->ChangeState(STATE_AIM);
        mpArrowEffect->forceKill();
        mpCursorModels[4]->GetModelAnm()->Cancel(RPGrpModelAnm::Anm_Chr, 0);
    }
    pObject = RPBilTable::sInstance;
    static_cast<RPBilTable*>(pObject)->SetGuideLine(
        static_cast<const EGG::Vector3f&>(pBallMgr->GetBall(0)->GetPos()),
        BilVector3f(view.camera->GetTargetRef() - view.camera->GetPosition()));
    mCursorPos = EGG::Vector3f(mPointerPos.x, mPointerPos.y, 0.0f);
    mCursorPos.x = Bil_clamp(mCursorPos.x, -Bil_halfWidth(), Bil_halfWidth());
    mCursorPos.y = Bil_clamp(mCursorPos.y, -Bil_halfHeight(), Bil_halfHeight());
    UpdateCursorModel();
}

void RPBilPlayer::AfterShotEnter() {
    RPBilBallMgr::sInstance->HideAimMarkers();
    RPBilTable::sInstance->HideGuideLine();
    RPBilHud* pHud = RPBilHud::sInstance;
    pHud->HidePrompt();
    pHud->HideTurnPanel();
    RPBilTable::sInstance->SetUnk80(false);
    RPBilNextBallMarker::sInstance->SetVisible(false);
    RPBilStrokeGuide::sInstance->SetMode(RPBilStrokeGuide::MODE_NONE);
    RPBilMusic::sInstance->GetState()->SetState(3);
    RPCommon::GetInstance()->GetPauseText()->SetEnabled(false);
}

void RPBilPlayer::AfterShotCalc() {
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    RPBilBall* pBall = RPBilBallMgr::sInstance->GetBall(0);
    EGG::Vector3f dir;
    Bil_setVector(dir, pCamera->GetTargetRef() - pCamera->GetPosition());
    EGG::Vector3f toBall = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
        &pBall->GetPos(), EGG::Vector3f(pCamera->GetPosition().x,
                                     pCamera->GetPosition().y, pCamera->GetPosition().z));
    bool inView = true;
    f32 angle = Bil_abs(Bil_angle(dir, toBall));
    if (angle > EGG::Math<f32>::pi() * 0.1f) {
        inView = false;
    }
    if (!pCamera->IsActive() &&
        (sAfterShotFrames == mpStateMachine->GetTimer() || !inView)) {
        pCamera->MoveToOverview(120.0f *
            (1.0f - 0.6f * (unk88.length() /
                             sStrokeParam6)));
    } else if (mpStateMachine->GetTimer() >= sAfterShotFrames + 60) {
        mpStateMachine->ChangeState(STATE_WAIT);
    }
    if (mpStateMachine->GetTimer() < 20) {
        mCuePos +=
            __ml__Q23EGG8Vector3fCFf(
                PORT_TEMP_PTR(__dv__Q23EGG8Vector3fCFf(
                    PORT_TEMP_PTR(__ml__Q23EGG8Vector3fCFf(
                        PORT_TEMP_PTR(__ml__Q23EGG8Vector3fCFf(
                            &mCueDir, unk88.length())),
                        sCueDist)), sStrokeMaxPower)),
                static_cast<f32>(20 - mpStateMachine->GetTimer()) / 20.0f);
        mCursorAlpha = (20 - mpStateMachine->GetTimer()) * 255 / 20;
        UpdateCueModel();
    }
}

void RPBilPlayer::WaitEnter() {
}

void RPBilPlayer::WaitCalc() {}

void RPBilPlayer::UpdatePointerTarget() {
    if (!mPointerValid) {
        return;
    }
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(4)->GetScreen();
    RPBilBall* pBall = RPBilBallMgr::sInstance->GetBall(0);
    nw4r::math::VEC3 screenPos;
    pCamera->GetWorldToScreen(&screenPos, pBall->GetPos(), pScreen);
    Bil_setVector2(mCueBallScreenPos, screenPos.x, screenPos.y);
    screenPos.z = 0.0f;
    f32 radius = pBall->GetPhysSphere()->GetRadius();
    EGG::Vector3f raised = __pl__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&pBall->GetPos(),
        BilVector3f(pCamera->GetUp() * radius));
    nw4r::math::VEC3 edgePos;
    pCamera->GetWorldToScreen(&edgePos, raised, pScreen);
    mCueBallScreenRadius = Bil_screenDistance(mCueBallScreenPos,
        EGG::Vector2f(edgePos.x, edgePos.y));
    CalcPointerRay__11RPBilPlayerFv(this);
    EGG::Vector3f hit;
    // NONMATCHING-DEBT: the pointer ray read as an RPBilSegment (see RaycastBalls above).
    bool intersects = pBall->IntersectRay(
        reinterpret_cast<const RPBilSegment&>(mPointerRayDir), &hit, NULL, 0.0f);
    if (intersects) {
        EGG::Vector3f toHit = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
            &hit, static_cast<const EGG::Vector3f&>(pBall->GetPos()));
        f32 angle = Bil_angle(mPointerRayDir, toHit);
        if (Bil_abs(angle) < EGG::Math<f32>::pi() * 0.6f) {
            intersects = false;
        }
    }
    if (intersects) {
        mCursorPos = mPointerTarget = hit;
        unk6C = 0;
    } else {
        f32 cursorDist = Bil_screenDistance(mCueBallScreenPos, mPointerPos);
        if (cursorDist < mCueBallScreenRadius + sPointerTargetRange) {
            unk6C = 1;
        } else {
            unk6C = 2;
        }
        mCursorPos =
            EGG::Vector3f(mPointerPos.x, mPointerPos.y, 0.0f);
        mCursorPos.x = Bil_clamp(mCursorPos.x, -Bil_halfWidth(), Bil_halfWidth());
        mCursorPos.y = Bil_clamp(mCursorPos.y, -Bil_halfHeight(), Bil_halfHeight());
    }
}

#pragma auto_inline off
bool CalcPointerRay__11RPBilPlayerFv(RPBilPlayer* pPlayer) {
    if (!pPlayer->mPointerValid) {
        return false;
    }

    RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(4)->GetScreen();
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    nw4r::math::VEC3 pointerPos(pPlayer->mPointerPos.x, pPlayer->mPointerPos.y, 1.0f);
    nw4r::math::VEC3 screenPos;
    pCamera->GetScreenToWorld(&screenPos,
                              pointerPos, pScreen);
    pPlayer->mPointerRayDir = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
        &screenPos, EGG::Vector3f(pCamera->GetPosition().x,
                                pCamera->GetPosition().y, pCamera->GetPosition().z));
    pPlayer->mPointerRayDir.normalise();
    pPlayer->mPointerRayOrigin = static_cast<const EGG::Vector3f&>(pCamera->GetPosition());
    return true;
}
#pragma auto_inline on

const nw4r::math::VEC3& RPBilBall::GetPos() const {
    return mpPhysSphere->GetPos();
}

void RPBilPlayer::UpdateAimMarker() {
    // Retain the original pool order for the stroke-rumble frame limits.
    f32 minRumbleFrames = 4.0f;
    f32 extraRumbleFrames = 26.0f;
    RPBilBall* pCueBall;
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    RPBilBallMgr* pBallMgr = Bil_getBallMgr();
    if (pCamera->IsActive()) {
        return;
    }
    EGG::Vector3f dir;
    Bil_setVector(dir, pCamera->GetTargetRef() - pCamera->GetPosition());
    dir.y = 0.0f;
    dir.normalise();
    if (Bil_isZeroSquared(dir)) {
        return;
    }
    s32 ball;
    EGG::Vector3f hit;
    if (pBallMgr->RaycastObjectBalls(dir, &ball, &hit)) {
        pCueBall = pBallMgr->GetBall(0);
        f32 dist = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
            &hit, static_cast<const EGG::Vector3f&>(pCueBall->GetPos())).length();
        f32 radius = pCueBall->GetPhysSphere()->GetRadius();
        if (dist < 0.2f * radius) {
            pBallMgr->HideAimMarkers();
            return;
        }
        EGG::Vector3f segment[2];
        segment[0] = static_cast<const EGG::Vector3f&>(pCueBall->GetPos());
        segment[1] = hit;
        radius = pCueBall->GetPhysSphere()->GetRadius();
        if (RPBilTable::sInstance->SweepHitsKcl(segment,
            0.98f * radius)) {
            pBallMgr->HideAimMarkers();
        } else {
            pBallMgr->ShowAimMarker(hit);
        }
    } else {
        pBallMgr->HideAimMarkers();
    }
}

void RPBilPlayer::SetChannel(s32 channel) {
    mChannel = channel;
    mpStateMachine->ChangeState(STATE_INACTIVE);
}

void RPBilPlayer::AddScore(s32 score) {
    mScore += score;
}

void RPBilPlayer::IncShotCount() {
    mShotCount = mShotCount + 1;
    mShotCount = Bil_limitStat(mShotCount, 99);
}

void RPBilPlayer::IncFoulCount() {
    mFoulCount = mFoulCount + 1;
    mFoulCount = Bil_limitStat(mFoulCount, 9);
}

void RPBilPlayer::UpdateMaxStat(s32 value) {
    mMaxStat = value > mMaxStat ? value : mMaxStat;
}

#pragma auto_inline off
void RPBilPlayer::CalcCueFromCamera() {
    RPBilCamera* pCamera = RPBilCameraMgr::sInstance->GetCamera();
    mCueDir = pCamera->GetTargetRef() - pCamera->GetPosition();
    static_cast<EGG::Vector3f&>(mCueDir).normalise();
    static_cast<EGG::Vector3f&>(mCueDir) +=
        BilVector3f(pCamera->GetUp() * sCueParamB);
    static_cast<EGG::Vector3f&>(mCueDir).normalise();
    mCuePull = mCuePull * (1.0f - sCueParamA) +
               sCueParamA * (sCueDist * (mPointerDist - unkC4));
    if (mCuePull < 0.0f) {
        mCuePos = mPointerTarget;
    } else {
        mCuePos = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
            &mPointerTarget, __ml__Q23EGG8Vector3fCFf(&mCueDir, mCuePull));
    }
}
#pragma auto_inline on

void RPBilPlayer::UpdateCueModel() {
    EGG::Vector3f up = __mi__Q23EGG8Vector3fCFv(&mCueDir);
    EGG::Vector3f forward(0.0f, 1.0f, 0.0f);
    EGG::Vector3f right = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&forward, up);
    right.normalise();
    forward = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&up, right);
    EGG::Matrix34f mtx;
    mtx[0][0] = right.x;
    mtx[1][0] = right.y;
    mtx[2][0] = right.z;
    mtx[0][1] = forward.x;
    mtx[1][1] = forward.y;
    mtx[2][1] = forward.z;
    mtx[0][2] = up.x;
    mtx[1][2] = up.y;
    mtx[2][2] = up.z;
    mtx[0][3] = mCuePos.x;
    mtx[1][3] = mCuePos.y;
    mtx[2][3] = mCuePos.z;
    if (mpCueModel->GetModelEx()->getScnObj() != NULL) {
        mpCueModel->GetModelEx()->getScnObj()->SetMtx(
            nw4r::g3d::ScnObj::MTX_LOCAL, &mtx);
    }
    mpCueModel->SetBaseScale(1.0f);
    mpCueModel->Calc();
    mpCueModel->Entry();
}

void UpdateArrowModel__11RPBilPlayerFv(RPBilPlayer* pPlayer, s32 index) {
    s32 modelIndex = 0;
    if (index != 0) {
        modelIndex = 1;
    }
    EGG::Vector3f up = __mi__Q23EGG8Vector3fCFv(&pPlayer->mCueDir);
    EGG::Vector3f forward;
    forward.set(0.0f, 1.0f, 0.0f);
    EGG::Vector3f right = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&forward, up);
    right.normalise();
    forward = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&up, right);
    EGG::Matrix34f mtx;
    mtx[0][0] = right.x;
    mtx[1][0] = right.y;
    mtx[2][0] = right.z;
    mtx[0][1] = forward.x;
    mtx[1][1] = forward.y;
    mtx[2][1] = forward.z;
    mtx[0][2] = up.x;
    mtx[1][2] = up.y;
    mtx[2][2] = up.z;
    EGG::Vector3f pos = pPlayer->mPointerTarget;
    pos += __ml__Q23EGG8Vector3fCFf(&pPlayer->mCueDir, RPBilPlayer::sArrowParamA);
    if (index != 0) {
        pos += __ml__Q23EGG8Vector3fCFf(&right, RPBilPlayer::sArrowParamB[0]);
    } else {
        pos -= __ml__Q23EGG8Vector3fCFf(&right, RPBilPlayer::sArrowParamB[0]);
    }
    mtx[0][3] = pos.x;
    mtx[1][3] = pos.y;
    mtx[2][3] = pos.z;
    if (pPlayer->mpArrowModels[modelIndex]->GetScnObj() != NULL) {
        pPlayer->mpArrowModels[modelIndex]->GetScnObj()->SetMtx(
            nw4r::g3d::ScnObj::MTX_LOCAL, &mtx);
    }
    pPlayer->mpArrowModels[modelIndex]->SetBaseScale(1.0f);
    pPlayer->mpArrowModels[modelIndex]->UpdateFrame();
    pPlayer->mpArrowModels[modelIndex]->Calc();
    pPlayer->mpArrowModels[modelIndex]->Entry();
}
void RPBilPlayer::UpdateCursorModel() {
    if (mTargetBall == -1) {
        return;
    }

    RPGrpView* pView = RPGrpRenderer::GetCurrent()->GetView(4);
    RPGrpCamera* pCamera = pView->GetCamera();
    EGG::Vector3f forward;
    Bil_setVector(forward, pCamera->GetPosition() - pCamera->GetTargetRef());
    forward.normalise();
    EGG::Vector3f up(pCamera->GetUp().x, pCamera->GetUp().y, pCamera->GetUp().z);
    EGG::Vector3f right = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&up, forward);
    EGG::Matrix34f mtx;
    mtx[0][0] = right.x;
    mtx[1][0] = right.y;
    mtx[2][0] = right.z;
    mtx[0][1] = up.x;
    mtx[1][1] = up.y;
    mtx[2][1] = up.z;
    mtx[0][2] = forward.x;
    mtx[1][2] = forward.y;
    mtx[2][2] = forward.z;
    if (static_cast<u32>(mTargetBall) <= 1) {
        mtx[0][3] = mPointerTarget.x;
        mtx[1][3] = mPointerTarget.y;
        mtx[2][3] = mPointerTarget.z;
    } else if (static_cast<u32>(mTargetBall - 2) <= 2) {
        nw4r::math::VEC3 worldPos;
        pView->GetScreenToWorld(&worldPos,
            nw4r::math::VEC2(mCursorPos.x, mCursorPos.y));
        mtx[0][3] = worldPos.x;
        mtx[1][3] = worldPos.y;
        mtx[2][3] = worldPos.z;
    }
    if (mpCursorModels[mTargetBall]->GetModelEx()->getScnObj() != NULL) {
        mpCursorModels[mTargetBall]->GetModelEx()->getScnObj()->SetMtx(
            nw4r::g3d::ScnObj::MTX_LOCAL, &mtx);
    }
    mpCursorModels[mTargetBall]->SetBaseScale(1.0f);
    mpCursorModels[mTargetBall]->UpdateFrame();
    mpCursorModels[mTargetBall]->Calc();
    mpCursorModels[mTargetBall]->Entry();
}

void RPBilPlayer::UserDraw() {
    RPBilScene* pScene =
        static_cast<RPBilScene*>(RPSysSceneMgr::instance()->getCurrentSceneRP());
    if (mIndex != RPBilPlayerMgr::sInstance->GetCurrentPlayer()) {
        return;
    }

    s32 state = mpStateMachine->GetState();
    if (state != STATE_ADDRESS && state != STATE_STROKE && state != STATE_AFTER_SHOT) {
        return;
    }

    if (RPGrpRenderer::GetCurrentViewNo() == 0 && RPGrpRenderer::GetDrawStep() == 3) {
        pScene->GetMainCapture()->Capture(-1);
    }

    if (RPGrpRenderer::GetCurrentViewNo() == 7 && RPGrpRenderer::GetDrawStep() == 5) {
        RPGrpScreen* pScreen = RPGrpRenderer::GetCurrentScreen();
        EGG::Matrix34f mtx;
        mtx.makeIdentity();
        mtx(0, 0) = static_cast<u16>(pScreen->GetWidth());
        mtx(1, 1) = static_cast<u16>(pScreen->GetHeight());
        EGG::DrawGX::BeginDrawQuad(EGG::DrawGX::COLORCHAN_COLOR,
                                   EGG::DrawGX::ZMODE_ALWAYS,
                                   EGG::DrawGX::BLEND_NORMAL, true, false);
        pScene->GetMainCapture()->GetCapTexture()->load(GX_TEXMAP0);
        GXColor color = {255, 255, 255, mCursorAlpha};
        EGG::DrawGX::DrawDL(EGG::DrawGX::DL_QUAD_TEXTURE, mtx, color);
    }
}

void RPBilPlayer::Rumble() {
    EGG::CoreControllerMgr::instance()->getNthController(mChannel)->startSimpleRumble(
        0xFFFFFFFF, 6, 0x20);
}

void RPBilPlayer::SetCursorsVisible(bool visible) {
    for (s32 i = 0; i < CURSOR_MODEL_NUM; i++) {
        mpCursorModels[i]->SetVisible(!visible);
    }
}

