#include <Pack/RPParty/RPPartyCommon/RPPlayerSettingPanel.h>
#include <Pack/RPParty/RPPartyCommon/RPAnimButton.h>
#include <Pack/RPParty/RPPartyCommon/RPMenuFrame.h>
#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPKernel/RPSysController.h>
#include <Pack/RPKernel/RPSysCursorDrawMgr.h>
#include <Pack/RPKernel/RPSysLytTextBox.h>
#include <Pack/RPKernel/RPSysLytWindow.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysLytAnmObj.h>
#include <Pack/RPKernel/RPSysLytResAccessor.h>
#include <Pack/RPKernel/RPSysMessage.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <egg/gfxe/eggScreen.h>
#include <egg/core/eggController.h>

#pragma auto_inline off

struct PlayerSettingMessageId {
    u32 group;
    u32 index;
};
extern const PlayerSettingMessageId sOptionMessages[3];
inline const PlayerSettingMessageId& PlayerSettingMessage(s32 index) {
    return sOptionMessages[index];
}
const PlayerSettingMessageId sOptionMessages[3] = {{320, 0}, {321, 0}, {330, 0}};
static const PlayerSettingMessageId sTitleMessages[] = {
    {300, 0}, {301, 0}, {302, 0}, {303, 0}, {304, 0}, {305, 0},
    {310, 0}, {311, 0}, {312, 0}, {313, 0}, {314, 0}, {315, 0}
};
static const char* sButtonNames[][3] = {
        {"P_LR_00", "B_LR_00", "T_LR_00"},
        {"P_LR_01", "B_LR_01", "T_LR_01"},
        {"P_LR_02", "B_LR_02", "T_LR_02"},
        {"P_LR_03", "B_LR_03", "T_LR_03"},
        {"P_LR_04", "B_LR_04", "T_LR_04"},
        {"P_LR_05", "B_LR_05", "T_LR_05"},
        {"P_LR_06", "B_LR_06", "T_LR_06"},
        {"P_LR_07", "B_LR_07", "T_LR_07"},
        {"P_LR_08", "B_LR_08", "T_LR_08"},
        {"P_LR_09", "B_LR_09", "T_LR_09"},
        {"P_LR_10", "B_LR_10", "T_LR_10"},
        {"P_LR_11", "B_LR_11", "T_LR_11"},
        {"P_okButton_00", "B_okButton_00", "T_ok_00"}
};
static const char* sRowNames[] = {
    "P_LRbg_00", "P_LRbg_01", "P_LRbg_02", "P_LRbg_03", "P_LRbg_04", "P_LRbg_05"
};
static const char* sButtonAnimations[] = {
    "playerSelect_01_activeLR_00.brlan", "playerSelect_01_activeLR_01.brlan",
    "playerSelect_01_pushLR_00.brlan", "playerSelect_01_pushLR_01.brlan",
    "playerSelect_01_activeButton_00.brlan", "playerSelect_01_pushButton_00.brlan",
    "playerSelect_01_offButton_00.brlan", NULL
};
// GCC does not accept a C linkage block around static member definitions
#if !defined(PORT_HOST)
extern "C" {
#endif
const char* RPPlayerSettingPanel::sRowAnimName = "playerSelect_01_activeBar_00.brlan";
const char* RPPlayerSettingPanel::sSportsAnimName = "playerSelect_01_onOffSports_00.brlan";
#if !defined(PORT_HOST)
}
#endif
static const char* sTitleNames[] = {
    "T_handSelect", "T_tennis_00", "T_baseball_00", "T_bowling_00", "T_golf_00", "T_boxing_00",
    "T_title_00", "T_title_01", "T_title_02", "T_title_03", "T_title_04", "T_title_05"
};
static const char* sSportsPanes[] = {
    "N_tennis_00", "N_baseball_00", "N_bowling_00", "N_golf_00", "N_boxing_00"
};
static const char* sRowPaneNames[] = {
    "N_tennis_00", "N_baseThrow_00", "N_baseHit_00", "N_bowling_00", "N_golf_00",
    "N_boxing_00", "N_baseball_00"
};

extern "C" {
u32 RP_playerIndexToMask(u32);
}

inline RPSysCoreController* RPPlayerSettingPanel::GetController() {
    return RP_GET_CORE_CTRL(mController);
}

inline void RPPlayerSettingPanel::PlayCursorSe(const char* pName) {
    if (mDpadMode == true) {
        EGG::Vector2f pos;
        RP_getPointerScreenPos(pos, mCPadCursor);
        RPSndAudioMgr::instance()->startPointCursorSe(pName, pos, 0.5f,
            RP_playerIndexToMask(mController));
    } else {
        RPSndAudioMgr::instance()->startPointCursorSe(pName, mNormPointerPos, 0.5f,
            RP_playerIndexToMask(mController));
    }
}

inline void RPPlayerSettingPanel::DisableRow(s32 row, s32 anim) {
    s32 idx = row * 2;
    mpOptionButtons[idx]->SetDefaultAnim(anim);
    mpOptionButtons[idx + 1]->SetDefaultAnim(anim);
    mpOptionButtons[idx]->PlayDefaultAnim(mpLayout);
    mpOptionButtons[idx + 1]->PlayDefaultAnim(mpLayout);
}

RPPlayerSettingPanel* RPPlayerSettingPanel::spInstance = NULL;

void RPPlayerSettingPanel::CreateInstance() {
    spInstance = new RPPlayerSettingPanel;
}

void RPPlayerSettingPanel::DestroyInstance() {
    delete spInstance;
    spInstance = NULL;
}

RPPlayerSettingPanel::~RPPlayerSettingPanel() {}

void RPPlayerSettingPanel::Init() {
    mpResAccessor = RPSysLytResAccessor::create(NULL);
    mController = 0;
    mPlayer = 0;
    mMode = 5;
}

void RPPlayerSettingPanel::Load(EGG::Archive* pArchive, EGG::Archive* pCursorArchive) {
    void* binary = RPSysResourceManager::GetFileFromArchive(pArchive, "playerSelect.arc", NULL);
    mpResAccessor->mountArchive(binary, NULL);
    mpLayout = RPSysLayout::create(NULL, mpResAccessor, "playerSelect_01.brlyt");
    mpInAnmObj = mpLayout->createAnmObj(NULL, mpResAccessor, "playerSelect_01_inFrame_00.brlan");
    mpInAnmObj->setPlayMode(RPSysFrameCtrl::EPlayMode_OneTime);
    mpLayout->bindAnmObjToPane(mpInAnmObj, "N_scene_00", true);
    RPSysFrameCtrl* ctrl = mpInAnmObj->getFrameCtrl();
    mTimer = ctrl->getEndFrame() - ctrl->getStartFrame();
    CreateButtons(mpLayout);
    mCPadCursor.Load(pCursorArchive);
    mWCursor.Load(pCursorArchive);
    mpScenePane = mpLayout->findPane("N_scene_00");
    void* message = RPSysResourceManager::GetMessageResource("common_message.bmg");
    mpMessage = new RPSysMessage(message, NULL);
}

void RPPlayerSettingPanel::Reset(RPMenuFrame* pFrame) {
    RPSysFrameCtrl* ctrl = mpInAnmObj->getFrameCtrl();
    mTimer = ctrl->getEndFrame() - ctrl->getStartFrame();
    ctrl->restart();
    mState = 3;
    mCursorId = 0;
    mHoverId = 0;
    mLastId = 0;
    BindButtons(mpLayout);
    mPointerValid = true;
    mDpadMode = false;
    mCPadCursor.Reset();
    mWCursor.Reset();
    mpTargetPane = NULL;
    if (pFrame != NULL) {
        mpFrame = pFrame;
    }
    RefreshButtons();
    mpScenePane->SetVisible(false);
    SetTexts();
    mController = 0;
    mPlayer = 0;
    mHoverSound = false;
    unk608 = 2;
}

void RPPlayerSettingPanel::Calculate() {
    mPointerValid = UpdatePointer();
    CalcMain();
    mpLayout->calc();
    if (mpTargetPane != NULL) {
        mCPadCursor.SetTarget(mpTargetPane);
        mWCursor.SetTarget(mpTargetPane);
    }
    mCPadCursor.Calculate();
    mWCursor.Calculate();
}

void RPPlayerSettingPanel::Draw() {
    mpLayout->draw();
}

void RPPlayerSettingPanel::DrawCursors() {
    mWCursor.Draw();
    mCPadCursor.Draw();
}

void RPPlayerSettingPanel::Exit() {}

void RPPlayerSettingPanel::CalcMain() {
    switch (mState) {
    case 0:
        if (mTimer < 0) {
            mState = 1;
            mCursorId = BUTTON_OK;
            mHoverId = -1;
            mLastId = BUTTON_OK;
            mPointerValid = false;
            mDpadMode = true;
            UpdatePointer();
            s32 id = HitTest(mPointerPos);
            if (id == -1) {
                if (!mpFrame->IsPointOnBackButton(mPointerPos)) {
                    mDpadMode = true;
                    SetHover(mCursorId);
                }
            } else if (id != -1) {
                SetHover(id);
                mCPadCursor.SetActive(0);
                OnHoverChange(id, mNormPointerPos);
                mHoverId = id;
                if (id != -1) {
                    mLastId = id;
                }
            }
            --mTimer;
        } else {
            mpScenePane->SetVisible(true);
            --mTimer;
        }
        break;
    case 1:
        CalcSelect();
        break;
    case 2:
        return;
    case 3:
        break;
    }
}

inline void RPPlayerSettingPanel::SetHoverId(s32 id) {
    mHoverId = id;
    if (id != -1) {
        mLastId = id;
    }
}

inline s32 RPPlayerSettingPanel::GetNextRowButton(s32 row) {
    if (mMode != 5 && mMode != 1) {
        return BUTTON_OK;
    }
    s32 column = row % 2;
    s32 next = row / 2;
    next = ClampRowForMode(next + 1);
    return next == 6 ? next * 2 : column + next * 2;
}

inline void RPPlayerSettingPanel::PlayPushAnim(s32 id) {
    s32 anim;
    if (id >= BUTTON_OK) {
        anim = 3;
    } else {
        anim = 7;
        if (mOptions[0][id] == 1) {
            anim = 3;
        }
    }
    mpOptionButtonTable->PlayAnimById(id, anim, mpLayout);
    mTimer = mpOptionButtonTable->GetAnimLengthById(id);
}

void RPPlayerSettingPanel::CalcSelect() {
    if (mSubState == 6) {
        if (--mTimer < 0) {
            mState = 3;
            mpScenePane->SetVisible(false);
            mCPadCursor.SetActive(0);
        }
    } else if (mSubState == 5) {
        if (--mTimer < 0) {
            mSubState = 0;
            s32 id = GetNextRowButton(mHoverId);
            SetHover(id);
            mCursorId = id;
            mHoverId = id;
            if (id != -1) {
                mLastId = id;
            }
            if (HitTest(mPointerPos) != -1) {
                mDpadMode = false;
            }
        }
    } else if (mSubState == 4) {
        if (--mTimer < 0) {
            mSubState = 0;
            s32 id = HitTest(mPointerPos);
            if (id == mHoverId) {
                mHoverId = -1;
                SetHover(id);
            } else {
                mDpadMode = true;
                SetHover(id);
            }
            mHoverId = id;
        }
    } else if (mSubState == 2) {
        if (mpFrame->IsBackPushDone()) {
            mState = 2;
        }
    } else {
        if (GetController()->getCoreStatus()->downTrigger(0x1400) && mpFrame->IsBackButtonIdle()) {
            SetHover(-1);
            mCPadCursor.SetActive(0);
            mWCursor.SetActive(0);
            mSubState = 2;
            mpFrame->PushBackButton(RP_playerIndexToMask(mController));
        } else {
            mCPadCursor.SetActive(0);
            mWCursor.SetActive(0);
            if (GetController()->getCoreStatus()->downTrigger(0x800) && mHoverId != -1) {
                if (mHoverId == BUTTON_OK) {
                    PlayCursorSe("RP_COMMON_SE_KEY_A_02_CTSP");
                    PlayCursorSe("RP_COMMON_SE_KEY_A_02_TVSP");
                    mSubState = 6;
                } else {
                    PlayCursorSe("RP_COMMON_SE_KEY_A_03_CTSP");
                    PlayCursorSe("RP_COMMON_SE_KEY_A_03_TVSP");
                    if (mDpadMode == false) {
                        mSubState = 4;
                    } else {
                        mSubState = 5;
                    }
                    s32 id = mHoverId;
                    s32 step = 1;
                    if (id % 2 == 1) {
                        step = -1;
                    }
                    s32 other = id + step;
                    mOptions[0][id] = 1;
                    mOptions[0][other] = 0;
                    mpOptionButtons[id]->SetDefaultAnim(2);
                    mpOptionButtons[other]->SetDefaultAnim(6);
                    mpOptionButtons[other]->PlayDefaultAnim(mpLayout);
                    mpOptionButtonTable->PlayAnimById(id, 1, mpLayout);
                }
                PlayPushAnim(mHoverId);
            } else {
                GetController();
                s32 id = HitTest(mPointerPos);
                if (id != -1) {
                    mPointerMoved = true;
                    if (id != BUTTON_OK) {
                        mWCursor.SetActive(1);
                    }
                    if (id != mHoverId || mDpadMode == true) {
                        GetController()->rumble(RPSysCoreController::ERumblePattern_Cursor);
                        OnHoverChange(id, mNormPointerPos);
                    }
                    mDpadMode = false;
                    SetHover(id);
                    mBackHover = 2;
                } else if (id == -1) {
                    if (mpFrame->IsPointOnBackButton(mPointerPos)) {
                        mPointerMoved = true;
                        mDpadMode = false;
                        SetHover(id);
                        if (GetController()->getCoreStatus()->downTrigger(0x800) &&
                            mpFrame->IsBackButtonIdle()) {
                            mSubState = 2;
                            mpFrame->PushBackButton(RP_playerIndexToMask(mController));
                        } else if (mBackHover == 2) {
                            mBackHover = 0;
                            GetController()->rumble(RPSysCoreController::ERumblePattern_Cursor);
                            mpFrame->SetBackButtonActive(true, -1);
                            OnHoverChange(0, mNormPointerPos);
                        }
                    } else {
                        mBackHover = 2;
                        mpFrame->SetBackButtonActive(false, -1);
                        id = NavigateDpad(id);
                        if (mDpadMode == true) {
                            mCPadCursor.SetActive(1);
                            if (id != BUTTON_OK) {
                                mWCursor.SetActive(1);
                            }
                        }
                        SetHover(id);
                        if (id != mHoverId && id != -1) {
                            EGG::Vector2f pos;
                            RP_getPointerScreenPos(pos, mCPadCursor);
                            OnHoverChange(id, pos);
                        }
                    }
                }
                mHoverId = id;
                if (id != -1) {
                    mLastId = id;
                }
            }
        }
    }
}

void RPPlayerSettingPanel::SetHover(s32 id) {
    s32 old = mHoverId;
    if (old != id) {
        if (old != -1) {
            s32 anim;
            if (old >= BUTTON_OK) {
                anim = 2;
            } else {
                anim = 6;
                if (mOptions[0][old] == 1) {
                    anim = 2;
                }
            }
            mpOptionButtonTable->PlayAnimById(old, anim, mpLayout);
        }
        if (id != -1) {
            s32 anim;
            if (id >= BUTTON_OK) {
                anim = 1;
            } else {
                anim = 5;
                if (mOptions[0][id] == 1) {
                    anim = 1;
                }
            }
            mpOptionButtonTable->PlayAnimById(id, anim, mpLayout);
            mpTargetPane = mpOptionButtonTable->GetPaneById(id);
            if (mpTargetPane != NULL) {
                mCPadCursor.SetTarget(mpTargetPane);
                mWCursor.SetTarget(mpTargetPane);
                if (id == BUTTON_OK) {
                    mCPadCursor.SelectStyle(0);
                } else {
                    mCPadCursor.SelectStyle(1);
                }
            }
            RPSysLayout::becomeYoungestChild(mpRowPanes[id / 2]);
            if (static_cast<u32>(id / 2 - 1) <= 1) {
                RPSysLayout::becomeYoungestChild(mpRowPanes[6]);
            }
        }
        old = mHoverId;
        if (old != -1 && old < BUTTON_OK) {
            if ((old + 2) / 2 != (id + 2) / 2) {
                mpRowButtonTable->PlayAnimById(old / 2, 2, mpLayout);
            }
        }
        if (id != -1 && id < BUTTON_OK) {
            if ((mHoverId + 2) / 2 != (id + 2) / 2) {
                mpRowButtonTable->PlayAnimById(id / 2, 1, mpLayout);
            }
        }
    }
}

void RPPlayerSettingPanel::RefreshButtons() {
    for (s32 i = 0; i < BUTTON_OK; i++) {
        if (mOptions[0][i] == 1) {
            mpOptionButtons[i]->SetDefaultAnim(2);
        } else {
            mpOptionButtons[i]->SetDefaultAnim(6);
        }
        mpOptionButtons[i]->PlayDefaultAnim(mpLayout);
    }
    mpOptionButtons[BUTTON_OK]->SetDefaultAnim(2);
    mpOptionButtons[BUTTON_OK]->PlayDefaultAnim(mpLayout);
    for (s32 i = 0; i < ROW_NUM; i++) {
        mpRowButtons[i]->SetDefaultAnim(2);
        mpRowButtons[i]->PlayDefaultAnim(mpLayout);
    }
    mCPadCursor.SetActive(0);
    mWCursor.SetActive(0);
}

void RPPlayerSettingPanel::StartIn() {
    RPSysFrameCtrl* ctrl = mpInAnmObj->getFrameCtrl();
    mTimer = ctrl->getEndFrame() - ctrl->getStartFrame();
    ctrl->restart();
    mpInAnmObj->setFrame(0.0f);
    mState = 3;
    mCursorId = BUTTON_OK;
    mHoverId = -1;
    mLastId = BUTTON_OK;
    mPointerValid = false;
    mDpadMode = true;
    mpTargetPane = NULL;
    RefreshButtons();
    mpScenePane->SetVisible(false);
    mSubState = 0;
    mBackHover = 2;
    mHoverSound = false;
    mColumn = 0;
}

void RPPlayerSettingPanel::CreateButtons(RPSysLayout* pLayout) {
    mpOptionButtonTable = new RPAnimButtonTable;
    for (s32 i = 0; i < BUTTON_NUM; i++) {
        mpOptionButtons[i] = new RPAnimButton;
        if (i != BUTTON_OK) {
            mpOptionButtons[i]->SetAnim(5,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[0]));
            mpOptionButtons[i]->SetAnim(1,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[1]));
            mpOptionButtons[i]->SetAnim(7,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[2]));
            mpOptionButtons[i]->SetAnim(3,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[3]));
            mpOptionButtons[i]->SetAnim(6,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[0]));
            mpOptionButtons[i]->SetAnim(2,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[1]));
            mpOptionButtons[i]->SetAnim(0,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[6]));
            mpOptionButtons[i]->SetInheritFrame(true);
        } else {
            mpOptionButtons[i]->SetAnim(2,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[4]));
            mpOptionButtons[i]->SetAnim(1,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[4]));
            mpOptionButtons[i]->SetAnim(3,
                pLayout->createAnmObj(NULL, mpResAccessor, sButtonAnimations[5]));
            mpOptionButtons[i]->SetInheritFrame(true);
        }
        mpOptionButtons[i]->SetRefreshFlag(1);
    }
    mpRowButtonTable = new RPAnimButtonTable;
    for (s32 i = 0; i < ROW_NUM; i++) {
        mpRowButtons[i] = new RPAnimButton;
        mpRowButtons[i]->SetAnim(2,
            pLayout->createAnmObj(NULL, mpResAccessor, sRowAnimName));
        mpRowButtons[i]->SetAnim(1,
            pLayout->createAnmObj(NULL, mpResAccessor, sRowAnimName));
        mpRowButtons[i]->SetRefreshFlag(0);
    }
    for (s32 i = 0; i < 5; i++) {
        mpSportsAnmObjs[i] = mpLayout->createAnmObj(NULL, mpResAccessor, sSportsAnimName);
    }
}

void RPPlayerSettingPanel::BindButtons(RPSysLayout* pLayout) {
    mSubState = 0;
    for (s32 i = 0; i < BUTTON_NUM; i++) {
        mpOptionButtons[i]->SetLayout(pLayout->findPane(sButtonNames[i][0]));
        mpOptionButtons[i]->SetPane(pLayout->findPane(sButtonNames[i][1]));
        mpOptionButtonTable->Add(i, mpOptionButtons[i]);
    }
    for (s32 i = 0; i < ROW_NUM; i++) {
        mpRowButtons[i]->SetLayout(pLayout->findPane(sRowNames[i]));
        mpRowButtonTable->Add(i, mpRowButtons[i]);
    }
    for (s32 i = 0; i < ROW_NUM; i++) {
        mOptions[i][1] = 1;
        mOptions[i][0] = 0;
    }
    for (s32 i = 0; i < 7; i++) {
        mpRowPanes[i] = pLayout->findPane(sRowPaneNames[i]);
    }
}

void RPPlayerSettingPanel::ApplyRowMode() {
    if (mMode != 5) {
        for (s32 i = 0; i < 5; i++) {
            if (mMode != i) {
                nw4r::lyt::Pane* pane = mpLayout->findPane(sSportsPanes[i]);
                RPSysLytAnmObj* anim = mpSportsAnmObjs[i];
                mpLayout->unbindAnmObj(anim);
                anim->setFrame(anim->getFrameCtrl()->getEndFrame());
                RPSysLayout::bindAnmObjToPane(anim, pane, true);
                if (i == 0) {
                    DisableRow(i, 0);
                } else if (i == 1) {
                    DisableRow(i, 0);
                    DisableRow(i + 1, 0);
                } else {
                    DisableRow(i + 1, 0);
                }
            }
        }
        mpLayout->findWindow("W_baseLine_02")->applyPlayerColor(mPlayer, false);
        mpLayout->findWindow("W_baseLine_01")->applyPlayerColor(mPlayer, false);
    }
}

void RPPlayerSettingPanel::SetTexts() {
    for (s32 i = 0; i < BUTTON_NUM; i++) {
        if (i == BUTTON_OK) {
            mpTextBoxes[i] = mpLayout->findTextBox(sButtonNames[i][2]);
            mpTextBoxes[i]->setMessage(mpMessage,
                PlayerSettingMessage(2).group, PlayerSettingMessage(2).index, 0);
        } else {
            mpTextBoxes[i] = mpLayout->findTextBox(sButtonNames[i][2]);
            mpTextBoxes[i]->setMessage(mpMessage,
                PlayerSettingMessage(i % 2).group, PlayerSettingMessage(i % 2).index, 0);
        }
    }
    for (s32 i = 0; i < 12; i++) {
        mpTextBoxes[i] = mpLayout->findTextBox(sTitleNames[i]);
        mpTextBoxes[i]->setMessage(mpMessage, sTitleMessages[i].group, sTitleMessages[i].index, 0);
    }
}

s32 RPPlayerSettingPanel::HitTest(nw4r::math::VEC2 pos) {
    EGG::Vector2f copy(pos);
    return FilterButtonForMode(mpOptionButtonTable->HitTest(copy, mpLayout));
}

s32 RPPlayerSettingPanel::FilterButtonForMode(s32 id) {
    if (id == BUTTON_OK || mMode == 5) {
        return id;
    }
    s32 first;
    s32 end;
    s32 mode = mMode;
    if (mode == 0) {
        first = mode * 2;
        end = (mode + 1) * 2;
    } else if (mode == 1) {
        first = mode * 2;
        end = (mode + 2) * 2;
    } else {
        first = (mode + 1) * 2;
        end = (mode + 2) * 2;
    }
    if (first <= id && id < end) {
        return id;
    }
    return -1;
}

inline bool PlayerSettingDirection(u32 buttons, u32 low, u32 high) {
    return (buttons & low) | (buttons & high);
}

s32 RPPlayerSettingPanel::NavigateDpad(s32 id) {
    if (mDpadMode == true) {
        s32 column = mColumn;
        s32 row = mCursorId / 2;
        if (PlayerSettingDirection(GetController()->getCursorHold(), 8, 0x10000)) {
            --row;
        }
        if (PlayerSettingDirection(GetController()->getCoreStatus()->getTrigger(), 1, 0x40000) &&
            column == 1) {
            --column;
        }
        if (PlayerSettingDirection(GetController()->getCoreStatus()->getTrigger(), 2, 0x80000) &&
            column == 0) {
            ++column;
        }
        if (PlayerSettingDirection(GetController()->getCursorHold(), 4, 0x20000)) {
            ++row;
        }
        if (mPointerMoved == false && mCursorId == BUTTON_OK && row != mCursorId / 2) {
            s32 selected;
            if (mOptions[0][0] == 1) {
                selected = 1;
                mColumn = selected;
            } else {
                selected = 0;
                mColumn = selected;
            }
            mCursorId = selected;
            mPointerMoved = true;
            return selected;
        } else {
            column = (column + 2) % 2;
            row = (row + 7) % 7;
            if (row != 6) {
                mColumn = column;
            }
            row = GetNextRow(row, mCursorId / 2);
            if (row == 6) {
                column = 0;
            }
            s32 selected = row * 2 + column;
            mCursorId = selected;
            return selected;
        }
    } else {
        if (PlayerSettingDirection(GetController()->getCursorHold(), 8, 0x10000) ||
            PlayerSettingDirection(GetController()->getCursorHold(), 4, 0x20000) ||
            PlayerSettingDirection(GetController()->getCursorHold(), 1, 0x40000) ||
            PlayerSettingDirection(GetController()->getCursorHold(), 2, 0x80000)) {
            s32 selected = mLastId;
            mDpadMode = true;
            mCursorId = selected;
            if (selected != BUTTON_OK) {
                mColumn = selected % 2;
            }
            return selected;
        }
    }
    return id;
}

s32 RPPlayerSettingPanel::GetNextRow(s32 row, s32 oldRow) {
    s32 mode = mMode;
    if (mode == 5 || row == oldRow) {
        return row;
    }
    if (mode == 1) {
        if (oldRow == 6) {
            return row == 0 ? mode : mode + 1;
        }
        return ClampRowForMode(row);
    }
    if (oldRow == 6) {
        return mode == 0 ? mode : mode + 1;
    }
    return 6;
}

s32 RPPlayerSettingPanel::ClampRowForMode(s32 row) {
    s32 mode = mMode;
    if (mode == 5) {
        return row;
    }
    s32 first;
    s32 end;
    if (mode == 0) {
        first = mode;
        end = mode + 1;
    } else if (mode == 1) {
        first = mode;
        end = mode + 2;
    } else {
        first = mode + 1;
        end = mode + 2;
    }
    if (first <= row && row < end) {
        return row;
    }
    return 6;
}

bool RPPlayerSettingPanel::UpdatePointer() {
    EGG::CoreStatus status = *GetController()->getCoreStatus();
    mPointerPos.x = sOffScreenPos;
    mPointerPos.y = sOffScreenPos;
    if (status.getDPDValidFlag() > 0) {
        mPointerPos = RPSysCursorDrawMgr::instance()->getPlayerCursorPos(
            static_cast<RPSysCursorDrawMgr::EPlayer>(mPlayer));
        mNormPointerPos = ToNormalizedPos(mPointerPos);
        return true;
    }
    return false;
}

bool RPPlayerSettingPanel::IsConfirmed() {
    return mState == 3;
}

bool RPPlayerSettingPanel::IsCancelled() {
    return mState == 2;
}

void RPPlayerSettingPanel::Open() {
    StartIn();
    ApplyRowMode();
    mPointerMoved = false;
    mState = 0;
}

void RPPlayerSettingPanel::SetOption(s32 row, u32 choice) {
    if (choice == 1) {
        mOptions[row][0] = 1;
        mOptions[row][1] = 0;
    } else {
        mOptions[row][0] = 0;
        mOptions[row][1] = 1;
    }
}

u32 RPPlayerSettingPanel::GetOption(s32 row) {
    return mOptions[row][0];
}

void RPPlayerSettingPanel::OnHoverChange(s32 id, nw4r::math::VEC2 pos) {
    EGG::Vector2f copy(pos);
    if (mHoverSound == true) {
        s32 oldRow = mLastId / 2;
        s32 newRow = id / 2;
        if (oldRow != newRow) {
            RPSndAudioMgr::instance()->startSystemSe("RP_COMMON_SE_KEY_CURSOR_02");
        } else {
            RPSndAudioMgr::instance()->startSystemSe("RP_COMMON_SE_KEY_CURSOR_01");
        }
    } else {
        mHoverSound = true;
    }
}

inline u16 PlayerSettingHalfSize(s32 value) {
    return static_cast<u16>(static_cast<u32>(value) >> 1);
}

EGG::Vector2f RPPlayerSettingPanel::ToNormalizedPos(nw4r::math::VEC2 pos) {
    EGG::Vector2f result;
    result.x = pos.x / static_cast<u32>(PlayerSettingHalfSize(EGG::Screen::GetSizeXMax()));
    result.y = pos.y / static_cast<u32>(PlayerSettingHalfSize(EGG::Screen::GetSizeYMax()));
    return result;
}

void RPPlayerSettingPanel::SetPlayer(u32 controller, u32 player) {
    mController = controller;
    mPlayer = player;
}

const f32 RPPlayerSettingPanel::sOffScreenPos = -5000.0f;
