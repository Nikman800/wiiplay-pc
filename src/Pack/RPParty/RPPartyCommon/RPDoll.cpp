// Player dolls on the game-select screen. Definitions follow reverse address order.
#include <Pack/RPParty/RPPartyCommon/RPDoll.h>

#include <Pack/RPParty/RPPartyCommon/RPFaceCtrl.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>
#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPGraphics/RPGrpModelAnm.h>
#include <Pack/RPGraphics/RPGrpRenderer.h>
#include <Pack/RPGraphics/RPGrpScreen.h>
#include <Pack/RPGraphics/RPGrpView.h>
#include <Pack/RPKernel/RPSysKokeshi.h>
#include <Pack/RPKernel/RPSysKokeshiGenInfo.h>
#include <Pack/RPKernel/RPSysKokeshiManager.h>
#include <Pack/RPUtility/RPUtlRandom.h>

// The address of a temporary (CodeWarrior accepts `&T(...)`; the host form is in
// port/host/include/decomp/compat.h)
#if !defined(PORT_TEMP_PTR)
#define PORT_TEMP_PTR(x) (&(x))
#endif

f32 RPDoll::sAngles[2][2] = {{5.0f, -5.0f}, {5.0f, -5.0f}};
f32 RPDoll::sScale[2] = {15.5f, 16.5f};
f32 RPDoll::sLookDistance = 500.0f;
f32 RPDoll::sBillboardFactor = 2.5f;
EGG::Vector2f RPDoll::sOffsets1P[2] = {
    EGG::Vector2f(0.0f, -40.0f), EGG::Vector2f(0.0f, -44.0f)};
EGG::Vector2f RPDoll::sOffsets2P[2][2] = {
    {EGG::Vector2f(25.0f, -44.0f), EGG::Vector2f(-25.0f, -44.0f)},
    {EGG::Vector2f(8.0f, -44.0f), EGG::Vector2f(-8.0f, -44.0f)}};
inline f32 DollLookSpeed() {
    return NW4R_MATH_DEG_TO_RAD(18.0f);
}
// Both small-data objects reserve eight bytes in the original.
struct DollLookSpeedData {
    f32 value;
    u32 padding;
};
DollLookSpeedData RPDoll::sLookSpeed ALIGN(8) = {DollLookSpeed(), 0};
// Preserve the original singleton pointer's eight-byte storage without changing
// the pointer declaration used by the other translation units.
// NONMATCHING-DEBT: RPDollMgr* RPDollMgr::sInstance gives a 4-byte .sbss object (unit .sbss 0xC, not 0x10).
extern "C" {
RPDollMgr* sInstance__9RPDollMgr[2] ALIGN(8);
}

struct DollReaction {
    s32 state;
    s32 anm;
    f32 blendFrames;
    RFLExpression expression;
};
const DollReaction RPDoll::sTableReactions[6] = {
    {9, 10, 8.0f, RFLExp_Blink},
    {10, 11, 8.0f, RFLExp_Surprise},
    {11, 12, 20.0f, RFLExp_Normal},
    {12, 13, 8.0f, RFLExp_Smile},
    {13, 14, 8.0f, RFLExp_OpenMouth},
    {14, 15, 8.0f, RFLExp_Smile}
};

struct DollAnmNames {
    const char* names[20];
};

struct DollScaleFactors {
    f32 factors[2][3];
};

struct DollStateFuncs {
    RPDollSeq::StateFunc funcs[18][2];
};

struct DollStateList3 {
    s32 values[3];
};
struct DollWeightList3 {
    s32 values[3];
};
struct DollStateList4 {
    s32 values[4];
};
struct DollWeightList4 {
    s32 values[4];
};

extern const char lbl_8031E160[];

inline bool DollResultEquals(s32 result, s32 expected) {
    if (result == expected) {
        return true;
    }
    return false;
}

inline f32 DollScreenScale(RPGrpView* pView) {
    f32 width = pView->GetScreen()->GetWidth();
    return width / static_cast<f32>(EGG::Screen::GetSizeXMax(EGG::Screen::TV_MODE_STD));
}

inline s32 DollSceneDifference() {
    return RPSceneInfo::spInstance->unk10;
}

inline f32 DollFloat(f32 value) { return value; }

inline u32 DollRandomChoice() {
    u32 seed;
    u32 mask;
    seed = RPUtlRandom::sRandom.mSeed * RPUtlRandom::MULT + 1;
    RPUtlRandom::sRandom.mSeed = seed;
    mask = RPUtlRandom::RANDF_MAX;
    f32 value = static_cast<f32>(static_cast<u16>(mask & (seed >> RPUtlRandom::RANDF_SHIFT))) /
                static_cast<f32>(RPUtlRandom::RANDF_MAX + 1);
    return 2.0f * value;
}

inline void DollPlayWakeUp(RPSndAudioMgr* audio, const char* pool, s32 offset) {
    const char* const name = pool + offset;
    audio->startSystemSeWithRemote(name, 15);
}


RPDoll::~RPDoll() {}

RPDoll::RPDoll(s32 playerNum, s32 index) : RPDollBase(index) {
    mPlayerNum = playerNum;
    mpStateMachine = new RPDollSeq(this, 18);
    const DollStateFuncs stateFuncs = {{
        {&RPDoll::EnterIdle, &RPDoll::CalcIdle},
        {&RPDoll::EnterFace1, &RPDoll::CalcAnim1Loop},
        {&RPDoll::EnterFace3, &RPDoll::CalcAnim2Loop},
        {&RPDoll::EnterResultLoop, &RPDoll::CalcResultLoop},
        {&RPDoll::EnterAnim5, &RPDoll::CalcAnim5},
        {&RPDoll::EnterAnim6, &RPDoll::CalcAnim6},
        {&RPDoll::EnterDoze, &RPDoll::CalcDoze},
        {&RPDoll::EnterSleep, &RPDoll::CalcSleep},
        {&RPDoll::EnterWakeUp, &RPDoll::CalcWakeUp},
        {&RPDoll::EnterTableReaction, &RPDoll::CalcTableReaction},
        {&RPDoll::EnterTableReaction, &RPDoll::CalcTableReaction},
        {&RPDoll::EnterTableReaction, &RPDoll::CalcTableReaction},
        {&RPDoll::EnterTableReaction, &RPDoll::CalcTableReaction},
        {&RPDoll::EnterTableReaction, &RPDoll::CalcTableReaction},
        {&RPDoll::EnterTableReaction, &RPDoll::CalcTableReaction},
        {&RPDoll::EnterLookAtOther, &RPDoll::CalcLookAtOther},
        {&RPDoll::EnterPairReact, &RPDoll::CalcPairReact},
        {&RPDoll::EnterPairAnim, &RPDoll::CalcPairAnim}
    }};
    for (s32 i = 0; i < 18; i++) {
        mpStateMachine->SetStateFuncs(stateFuncs.funcs[i][0], stateFuncs.funcs[i][1], i);
    }
    RPSysKokeshiGenInfo info;
    info.SetGenType(RPSysKokeshiManager::GenType_Player);
    info.SetPlayerNo(index);
    info.SetResolution(RFLResolution_256);
    info.SetBodyType(4);
    info.SetExpressionFlag(0x7f);
    mpKokeshi = new RPSysKokeshi(&info);
    mpFaceCtrl = new RPFaceCtrl;
}

void RPDoll::Load() {
    RPDollBase::Load();
    mChestJointId = mpKokeshi->GetBodyModel()->GetJointIndex(lbl_8031E160 + 0x118);
    mpKokeshi->GetBodyModel()->SetCallback(this);
    mpKokeshi->GetBodyModel()->SetCallbackJointIndex(mChestJointId);
    const DollAnmNames names = {{
        lbl_8031E160 + 0x00, lbl_8031E160 + 0x0a,
        lbl_8031E160 + 0x13, lbl_8031E160 + 0x1d,
        lbl_8031E160 + 0x27, lbl_8031E160 + 0x31,
        lbl_8031E160 + 0x3c, lbl_8031E160 + 0x47,
        lbl_8031E160 + 0x54, lbl_8031E160 + 0x5f,
        lbl_8031E160 + 0x6b, lbl_8031E160 + 0x7b,
        lbl_8031E160 + 0x8b, lbl_8031E160 + 0x9a,
        lbl_8031E160 + 0xa9, lbl_8031E160 + 0xb8,
        lbl_8031E160 + 0xc7, lbl_8031E160 + 0xd3,
        lbl_8031E160 + 0xdf, lbl_8031E160 + 0xeb
    }};
    RPGrpModelAnm* pAnm = mpKokeshi->GetBodyModel()->GetModelAnm();
    pAnm->CreateBuffer(RPGrpModelAnm::Anm_Chr, 20);
    for (s32 i = 0; i < 20; i++) {
        pAnm->RegistFromResFile(RPGrpModelAnm::Anm_Chr, i, 1, names.names[i], 0);
    }
}

void RPDoll::Reset() {
    RPDollBase::Reset();
    const RPSceneInfo* pInfo = RPSceneInfo::spInstance;
    if (mPlayerNum == 1) {
        if (DollResultEquals(pInfo->unk18, 2) || DollResultEquals(pInfo->unk18, 3)) {
            mpStateMachine->Reset(1);
        } else {
            mpStateMachine->Reset(0);
        }
    } else if (pInfo->unk14 == -1) {
        mpStateMachine->Reset(3);
    } else if (mIndex == pInfo->unk14) {
        mpStateMachine->Reset(1);
    } else {
        mpStateMachine->Reset(2);
    }
    mPos = EGG::Vector3f::zero;
    mAxisX = EGG::Vector3f(1.0f, 0.0f, 0.0f);
    mAxisY = EGG::Vector3f(0.0f, 1.0f, 0.0f);
    mAxisZ = EGG::Vector3f(0.0f, 0.0f, 1.0f);
    mScale = sScale[EGG::Screen::GetTVMode()];
    mPointerActive = false;
    mDozeTimer = 0;
    mIdleExitAllowed = true;
    mDozeAllowed = false;
}

void RPDoll::Calculate() {
    mScale = sScale[EGG::Screen::GetTVMode()];
    if (mPlayerNum == 2) {
        s32 difference;
        const DollScaleFactors scales = {{{0.92f, 0.81f, 0.65f},
                                          {0.91f, 0.79f, 0.61f}}};
        difference = DollSceneDifference();
        if (mIndex == 0) {
            difference = -difference;
        }
        if (difference > 0) {
            difference--;
            mScale *= scales.factors[EGG::Screen::GetTVMode()][difference];
        }
    }
    if (RPDollMgr::GetInstance()->GetAlpha() < 255) {
        mpKokeshi->GetBodyModel()->SetDrawGroup(1);
        mpKokeshi->GetNigaoeModel()->SetDrawGroup(1);
    } else {
        mpKokeshi->GetBodyModel()->SetDrawGroup(0);
        mpKokeshi->GetNigaoeModel()->SetDrawGroup(0);
    }
    if (!mPointerActive) {
        if (mIdleTimer < 0x7fffffff) {
            mIdleTimer++;
        }
        if (mDozeTimer < 0x7fffffff) {
            mDozeTimer++;
        }
    } else {
        mIdleTimer = 0;
        mDozeTimer = 0;
    }
    mpStateMachine->Calculate();
    if (mPlayerNum == 2) {
        EGG::Matrix34f rotation;
        EGG::Vector3f axis;
        f32 one = 1.0f;
        f32 zero = 0.0f;
        axis.y = one; axis.x = zero; axis.z = zero;
        rotation.setAxisRotation(axis,
                                 DollFloat(sAngles[EGG::Screen::GetTVMode()][mIndex]) *
                                     DollFloat(NW4R_MATH_DEG_TO_RAD(1.0f)));
        rotation.getBase(0, mAxisX);
        rotation.getBase(1, mAxisY);
        rotation.getBase(2, mAxisZ);
    }
    RPDollBase::Calculate();
    UpdateModelMatrices();
}

void RPDoll::EnterTableReaction() {
    for (s32 i = 0; i < 6; i++) {
        const DollReaction& r = sTableReactions[i];
        if (r.state == mpStateMachine->GetState()) {
            RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), r.expression);
            PlayAnm(r.anm, r.blendFrames);
            break;
        }
    }
}

void RPDoll::CalcTableReaction() {
    for (s32 i = 0; i < 6; i++) {
        const DollReaction& r = sTableReactions[i];
        if (r.state == mpStateMachine->GetState()) {
            if (IsAnmDone(static_cast<u16>(r.anm))) {
                mpStateMachine->ChangeState(0);
            }
            mIsLooking = false;
            break;
        }
    }
}

void RPDoll::EnterIdle() {
    mIdleTimer = 0;
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
    mpFaceCtrl->Reset(mpKokeshi);
    PlayAnm(0, 30.0f);
    mStateEndTime = RPUtlRandom::getU32(600) + 400;
    mLookSpeed = GetTurnSpeed();
}

void RPDoll::CalcIdle() {
    if (mpStateMachine->GetTimer() > 60) {
        if (mIdleTimer > mStateEndTime) {
            if (mPlayerNum == 1) {
                const DollStateList3 states = {{6, 4, 5}};
                const DollWeightList3 weights = {{30, 30, 30}};
                s32 total = 0;
                for (s32 i = 0; i < 3; i++) {
                    total += weights.values[i];
                }
                s32 random = RPUtlRandom::getU32(total);
                s32 cumulative = 0;
                for (s32 i = 0; i < 3; i++) {
                    cumulative += weights.values[i];
                    if (random < cumulative) {
                        mpStateMachine->ChangeState(states.values[i]);
                        break;
                    }
                }
            } else {
                const DollStateList4 states = {{6, 4, 15, 5}};
                const DollWeightList4 weights = {{20, 20, 40, 20}};
                s32 total = 0;
                for (s32 i = 0; i < 4; i++) {
                    total += weights.values[i];
                }
                s32 random = RPUtlRandom::getU32(total);
                s32 cumulative = 0;
                for (s32 i = 0; i < 4; i++) {
                    cumulative += weights.values[i];
                    if (random < cumulative) {
                        mpStateMachine->ChangeState(states.values[i]);
                        break;
                    }
                }
            }
        } else if (mDozeTimer >= static_cast<s32>(RPUtlRandom::getU32(1000) + 10800) &&
                   mDozeAllowed) {
            mpStateMachine->ChangeState(7);
        }
    }
    mpFaceCtrl->Calculate(mpKokeshi);
}

// Keep the original contiguous pool after the local state/weight initializers.
extern const char lbl_8031E160[0x120] =
    "cmn_waitR\0cmn_winR\0cmn_loseR\0cmn_drawL\0cmn_drawR\0"
    "cmn_kyoroR\0cmn_thinkR\0cmn_stretchR\0cmn_sleepR\0cmn_wakeupR\0"
    "cmn_click_headR\0cmn_click_bodyR\0cmn_click_armL\0cmn_click_armR\0"
    "cmn_click_legL\0cmn_click_legR\0cmn_nudge1L\0cmn_nudge1R\0"
    "cmn_nudge2L\0cmn_nudge2R\0RP_COMMON_SE_Party_DOLL_WakUp_01\0chest";

void RPDoll::EnterFace1() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Smile);
    mpFaceCtrl->Reset(mpKokeshi);
}

void RPDoll::CalcAnim1Loop() {
    if (mpStateMachine->GetTimer() > 300 && mIdleExitAllowed) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    PlayAnm(1, 10.0f);
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterFace3() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Sorrow);
    mpFaceCtrl->Reset(mpKokeshi);
}

void RPDoll::CalcAnim2Loop() {
    if (mpStateMachine->GetTimer() > 300) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    PlayAnm(2, 10.0f);
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterResultLoop() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
    mpFaceCtrl->Reset(mpKokeshi);
}

void RPDoll::CalcResultLoop() {
    if (mpStateMachine->GetTimer() > 300) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    if (mIndex == 0) {
        PlayAnm(3, 10.0f);
    } else {
        PlayAnm(4, 10.0f);
    }
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterAnim5() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
    mpFaceCtrl->Reset(mpKokeshi);
    PlayAnm(5, 20.0f);
}

void RPDoll::CalcAnim5() {
    if (IsAnmDone(5)) {
        if (RPUtlRandom::getU32(2) == 0 && mPlayerNum == 2) {
            mpStateMachine->ChangeState(15);
        } else {
            mpStateMachine->ChangeState(0);
        }
    }
    mIsLooking = false;
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterAnim6() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Blink);
    mpFaceCtrl->Reset(mpKokeshi);
    PlayAnm(6, 50.0f);
}

void RPDoll::CalcAnim6() {
    if (IsAnmDone(6)) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterDoze() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Blink);
    mpFaceCtrl->Reset(mpKokeshi);
    PlayAnm(7, 40.0f);
}

void RPDoll::CalcDoze() {
    if (IsAnmDone(7)) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterSleep() {
    PlayAnm(8, 60.0f);
    mStateEndTime = RPUtlRandom::getU32(1000) + 7200;
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Blink);
}

void RPDoll::CalcSleep() {
    if (mpStateMachine->GetTimer() > mStateEndTime && mDozeAllowed) {
        mpStateMachine->ChangeState(8);
    }
    mIsLooking = false;
    mDozeTimer = 0;
}

void RPDoll::EnterWakeUp() {
    PlayAnm(9, 20.0f);
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
    DollPlayWakeUp(RPSndAudioMgr::instance(), lbl_8031E160, 0xf7);
}

void RPDoll::CalcWakeUp() {
    if (IsAnmDone(9)) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterLookAtOther() {
    if (mpStateMachine->mPrevState == 17) {
        if (RPUtlRandom::getU32(2) == 0) {
            RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Sorrow);
        } else {
            RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Anger);
        }
        mLookSpeed = GetTurnSpeed() * 0.5f;
    } else {
        RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
        mLookSpeed = GetTurnSpeed();
    }
    mpFaceCtrl->Reset(mpKokeshi);
    mStateEndTime = RPUtlRandom::getU32(180) + 180;
}

void RPDoll::CalcLookAtOther() {
    if (mpStateMachine->GetTimer() < mStateEndTime - 20) {
        LookAtTarget(RPDollMgr::GetInstance()->GetDollPos(1 - mIndex), sLookSpeed.value);
    } else {
        mIsLooking = false;
    }
    if (mpStateMachine->GetTimer() > mStateEndTime) {
        const u32 random = DollRandomChoice();
        if (random == 0) {
            mpStateMachine->ChangeState(0);
        } else {
            mpStateMachine->ChangeState(16);
        }
    }
    PlayAnm(0, 30.0f);
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterPairReact() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
    mpFaceCtrl->Reset(mpKokeshi);
    if (mIndex == 0) {
        PlayAnm(16, 20.0f);
    } else {
        PlayAnm(17, 20.0f);
    }
    RPDollMgr::GetInstance()->RequestReaction(7, 1 - mIndex);
}

void RPDoll::CalcPairReact() {
    if ((mIndex == 0 && IsAnmDone(16)) || (mIndex == 1 && IsAnmDone(17))) {
        mpStateMachine->ChangeState(0);
    }
    mIsLooking = false;
    mpFaceCtrl->Calculate(mpKokeshi);
}

void RPDoll::EnterPairAnim() {
    RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Normal);
    mpFaceCtrl->Reset(mpKokeshi);
}

void RPDoll::CalcPairAnim() {
    if (mpStateMachine->GetTimer() == 27) {
        RPSysKokeshiManager::ChangeExpression(mpKokeshi->GetNigaoeModel(), RFLExp_Surprise);
        mpFaceCtrl->Reset(mpKokeshi);
        if (mIndex == 0) {
            PlayAnm(18, 20.0f);
        } else {
            PlayAnm(19, 20.0f);
        }
    } else if (mpStateMachine->GetTimer() > 27) {
        if ((mIndex == 0 && IsAnmDone(18)) || (mIndex == 1 && IsAnmDone(19))) {
            mpStateMachine->ChangeState(15);
        }
        mIsLooking = false;
    }
    mpFaceCtrl->Calculate(mpKokeshi);
}

bool RPDoll::RequestReaction(s32 reaction) {
    switch (reaction) {
    case 1:
        if (!mpStateMachine->IsStateOrNext(9)) {
            mpStateMachine->ChangeState(9);
        } else {
            return false;
        }
        break;
    case 0:
        if (!mpStateMachine->IsStateOrNext(1)) {
            mpStateMachine->ChangeState(1);
        } else {
            return false;
        }
        break;
    case 2:
        if (!mpStateMachine->IsStateOrNext(10)) {
            mpStateMachine->ChangeState(10);
        } else {
            return false;
        }
        break;
    case 3:
        if (!mpStateMachine->IsStateOrNext(11)) {
            mpStateMachine->ChangeState(11);
        } else {
            return false;
        }
        break;
    case 4:
        if (!mpStateMachine->IsStateOrNext(12)) {
            mpStateMachine->ChangeState(12);
        } else {
            return false;
        }
        break;
    case 5:
        if (!mpStateMachine->IsStateOrNext(13)) {
            mpStateMachine->ChangeState(13);
        } else {
            return false;
        }
        break;
    case 6:
        if (!mpStateMachine->IsStateOrNext(14)) {
            mpStateMachine->ChangeState(14);
        } else {
            return false;
        }
        break;
    case 7:
        mpStateMachine->ChangeState(17);
        return true;
    }
    return true;
}

void RPDoll::SetScreenPos(const nw4r::math::VEC2& rPos) {
    RPGrpView* pView = RPGrpRenderer::GetCurrent()->GetView(2);
    nw4r::math::VEC2 pos(rPos);
    f32 ratio = DollScreenScale(pView);
    if (mPlayerNum == 1) {
        pos.x += ratio * sOffsets1P[EGG::Screen::GetTVMode()].x;
        pos.y += sOffsets1P[EGG::Screen::GetTVMode()].y;
    } else {
        pos.x += ratio * sOffsets2P[EGG::Screen::GetTVMode()][mIndex].x;
        pos.y += sOffsets2P[EGG::Screen::GetTVMode()][mIndex].y;
    }
    pView->GetScreenToWorld(&mPos, pos);
}

void RPDoll::LookAtPointer(const nw4r::math::VEC2& rPos) {
    if (mpStateMachine->GetState() == 0) {
        LookAt(rPos, sLookDistance, sLookSpeed.value);
    }
}

void RPDoll::SetPointerActive(bool active) {
    mPointerActive = active;
    if (active) {
        mIdleTimer = 0;
    }
}

extern "C" EGG::Vector3f __ml__Q23EGG8Vector3fCFf(const EGG::Vector3f*, f32);
extern "C" EGG::Vector3f __pl__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
    const EGG::Vector3f*, const EGG::Vector3f&);
extern "C" EGG::Vector3f __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
    const EGG::Vector3f*, const EGG::Vector3f&);
extern "C" EGG::Vector3f cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
    const EGG::Vector3f*, const EGG::Vector3f&);

void RPDoll::EditJointWorldMtx(RPGrpModel*, u16, nw4r::math::MTX34* pMtx) {
    EGG::Vector3f y(pMtx->_01, pMtx->_11, pMtx->_21);
    EGG::Vector3f x(pMtx->_00, pMtx->_10, pMtx->_20);
    y.normalise();
    x.normalise();
    EGG::Vector3f direction = __pl__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(
        &y, __ml__Q23EGG8Vector3fCFf(
                PORT_TEMP_PTR(__ml__Q23EGG8Vector3fCFf(
                    static_cast<const EGG::Vector3f*>(&mLookDir), mLookWeight)),
                sBillboardFactor));
    EGG::Vector3f projection = __ml__Q23EGG8Vector3fCFf(&x, direction.dot(x));
    EGG::Vector3f up = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&direction, projection);
    if (up.normalise() > 0.0f) {
        EGG::Vector3f forward = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&x, up);
        if (forward.normalise() > 0.0f) {
            up *= mScale;
            forward *= mScale;
            pMtx->_01 = up.x;
            pMtx->_11 = up.y;
            pMtx->_21 = up.z;
            pMtx->_02 = forward.x;
            pMtx->_12 = forward.y;
            pMtx->_22 = forward.z;
        }
    }
}

// Emit this instantiation's methods in the original order.
#pragma defer_codegen off
#pragma auto_inline off
template bool RPStateMachine<RPDoll>::IsStateOrNext(s32) const;
template void RPStateMachine<RPDoll>::Calculate();
template void RPStateMachine<RPDoll>::ChangeState(s32);
template void RPStateMachine<RPDoll>::Reset(s32);
template void RPStateMachine<RPDoll>::SetStateFuncs(const StateFunc&, const StateFunc&, s32);
template RPStateMachine<RPDoll>::RPStateMachine(RPDoll*, s32);
template void RPStateMachine<RPDoll>::CallEnter();
template void RPStateMachine<RPDoll>::CallCalc();
template void RPStateMachine<RPDoll>::CallExit();

#pragma defer_codegen reset
#pragma auto_inline reset
