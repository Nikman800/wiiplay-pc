#include <Pack/RPParty/RPPartyCommon/RPKokeshiMgr.h>

#include <Pack/RPGraphics.h>
#include <Pack/RPKernel/RPSysKokeshiCtrlMgr.h>
#include <Pack/RPKernel/RPSysKokeshiIterater.h>
#include <Pack/RPKernel/RPSysKokeshiManager.h>
#include <Pack/RPSystem/RPSysProjectLocal.h>
#include <Pack/RPSystem/RPSysResourceManager.h>
#include <Pack/RPSystem/RPSysSaveDataMgr.h>

#include <egg/core.h>
#include <egg/gfxe.h>
#include <egg/math.h>

#include <nw4r/lyt.h>

// Out-of-line EGG copies (the calls stay calls in the original)
extern "C" EGG::Vector3f cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f*,
                                                                   const EGG::Vector3f&);
// The original passes the member's address without the by-value copy
extern "C" void SetBrightnessColor__19RPSysKokeshiManagerFP10RPGrpModel8_GXColor(
    RPSysKokeshiManager*, RPGrpModel*, const GXColor*);
extern "C" EGG::Vector3f __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f*,
                                                                  const EGG::Vector3f&);

namespace {

inline bool IsValidCreateID(const RFLCreateID& rID) {
    bool valid = false;
    for (int i = 0; i < RFL_CREATEID_LEN; i++) {
        if (rID.data[i] != 0) {
            valid = true;
        }
    }
    return valid;
}

#if defined(PORT_HOST)
// EGG::Frustum::mFlags (protected), named through a derived class. The Wii code
// writes byte offset 0x34; the host's layout differs (vtable pointer first,
// 64-bit pointers; port/docs/POINTERS.md §5.1).
struct FrustumFlags : EGG::Frustum {
    static u16 EGG::Frustum::*Member() {
        return &FrustumFlags::mFlags;
    }
};
#endif

inline void SetScreenFlags(RPGrpScreen* pScreen, u16 flags) {
#if defined(PORT_HOST)
    pScreen->*FrustumFlags::Member() |= flags;
#else
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(pScreen) + 0x34) |= flags;
#endif
}

} // namespace

// FUNCTIONAL (matching abandoned, user decision 2026-10-29): behaviour equals the original; the
// differences below are register allocation / instruction order only (docs/NONMATCHING.md).
// NONMATCHING: two loop-bound reloads in the unrolled clear loops are missing.
// Tried: const-ref bound local (78.5%), *(headMax + i), volatile/u32/const table, u32 j,
// reversed compare, static inline clear helpers with reference parameters (97.0-97.6%).
// Also tried (2026-10-28): u32/u8/s16 outer index (u32 fixes the subi/cmpwi order of the first
// clear loop, 99.0%, but not the reload), pointer bound (*(headMax + i), a const s32* local),
// ++j, while loop.
// Also tried (2026-10-28, second pass): #pragma opt_common_subs / opt_propagation /
// opt_loop_invariants off (the reload stays missing), i and j declared at the top, headMax before
// the mHeadNum loop, j += 1, int headMax[], (int) casts on the bound, chained or swapped NULL
// stores, a literal 6 outer bound: 8 differing words or worse.
// 2026-10-28 (3, mwcc_trace): the PCode before allocation already holds headMax[i] in one vreg
// for the guard, the >= 8 unroll test and n - 8, so the reload is decided before register
// allocation (unroller/CSE). No effect: const s32 headMax, headMax[i] > j, j <= headMax[i] - 1.
// Also no effect (4): the bound through an accessor inline (by value, const-ref return, array
// reference, const-ref index).
// 2026-10-28 (5, mwcc_trace --frontend): LoopUnroller emits @a = (T)headMax[i], @b = headMax[i] - 8,
// if (@a > 8); pass-0 CommonSubs merges both loads in that block and ExpressionPropagation puts
// the merged value into the > 8 test. The table is a local: the original copies the 6 words of
// .rodata lbl_80319590 {100, 6, 10, 10, 10, 10} to 0x38(r1) exactly as ours (@29121), so no
// static/member form; letting headMax's address escape (a call or a global store) disables the
// unroll entirely, so the original's bound was not aliased either. No effect: s32/int j with
// int/s32 table, u32 i, a value-local bound (96 words), ++j/++i, chained NULL stores,
// opt_lifetimes/dead_code/loop_invariants/vectorize pragmas.
// 2026-10-29 (--frontend --notes): the pass-0 CommonSubs note "Replacing common sub at" lists the
// guard copy (558), @a (572), @b (585) and the unroller's two overflow tests (12, 24) as one
// expression, so all five use one load. In the original the guard, @b (subi r10,r11,8) and the
// overflow tests share r11 and only @a (the > 8 test) reloads (lwz r0,0(r4), the same
// strength-reduced pointer), so @a alone was not equal or not available. No effect: every
// combination of s32/int/u32 table, int/s32 j and none/(int)/(s32) cast on the bound (the types
// do not separate the trees), a Slot& per i, u32 i for both clear loops, separate int j
// declaration, *&headMax[i], (&headMax[0])[i], headMax[i] + 0.
// 2026-10-29 (join search): the PCode has the guard (lwz; li j; cmpi; bf) and the > 8 test in
// adjacent blocks, the second with one predecessor, exactly the original's layout, so the
// original's join (if any) left no branch. No effect (8): an if (headMax[i] > 0) / != 0 wrapper
// around the inner loop, a continue guard, j declared in the outer body. Worse: do-while outer
// (89), split pane/scale inner loops (106), chained NULL (21), for(;;) with break / while(true)
// (95), if + do-while inner (96), a const s32* table pointer or pointer walk (157, no unroll), j
// outside both loops (17), Slot* per i (32), a ?: bound or || in a wrapper (95, 17).
// 2026-10-29 (type sweep, both clear loops): every signed 32-bit spelling is identical (8): j long,
// register int or signed long with an s32/int/long table. Every other counter type is far worse:
// j s16/short (164), u16/u8 (168), unsigned/u32/size_t/unsigned long with an s32 or u32 table (147),
// an unsigned long table with long j (160), j != headMax[i] (161). The unroller runs only in pass 0
// (later passes see no new unrollable loop), so @a is always built before CommonSubs merges it.
RPKokeshiMgr::RPKokeshiMgr(u8 viewNo, u8 sceneNo)
    : IRPSysHostIOSocket("KokeshiMgr"),
      mVisible(false),
      mAlpha(0),
      mRecordsChanged(false),
      mViewNo(viewNo),
      mSceneNo(sceneNo) {
    mHighlightColor.r = 130;
    mHighlightColor.g = 140;
    mHighlightColor.b = 150;
    mHighlightColor.a = 0;

    mpCamera = new RPGrpCamera();
    mpCamera->SetCameraType(RPGrpCamera::ECameraType_LookAt);

    RPGrpScreen* pScreen = new RPGrpScreen();
    pScreen->SetCanvasMode(EGG::Frustum::CANVAS_CC);
    pScreen->SetFovy(5.0f);
    SetScreenFlags(pScreen, 7);

    RPGrpRenderer::GetCurrent()->CreateView3D(mViewNo, mpCamera, pScreen);
    RPGrpRenderer::GetCurrent()->GetView(mViewNo)->SetDrawScene(mSceneNo);
    RPGrpRenderer::GetCurrent()->GetView(mViewNo)->SetFlag(8);

    for (int i = 0; i < SOURCE_NUM; i++) {
        mHeadNum[i] = 0;
    }

    s32 headMax[SOURCE_NUM] = {100, 6, 10, 10, 10, 10};

    for (int i = 0; i < SOURCE_NUM; i++) {
        mSlots[i].mpPanes = new nw4r::lyt::Pane*[headMax[i]];
        mSlots[i].mpScalePanes = new nw4r::lyt::Pane*[headMax[i]];
    }

    for (int i = 0; i < SOURCE_NUM; i++) {
        for (int j = 0; j < headMax[i]; j++) {
            mSlots[i].mpPanes[j] = NULL;
            mSlots[i].mpScalePanes[j] = NULL;
        }
    }

    for (int i = 0; i < SOURCE_NUM; i++) {
        mpHeads[i] = new RPKokeshiHead*[headMax[i]];
    }

    for (int i = 0; i < SOURCE_NUM; i++) {
        for (int j = 0; j < headMax[i]; j++) {
            mpHeads[i][j] = NULL;
        }
    }

    RFLDataSource sources[2] = {RFLDataSource_Official, RFLDataSource_Default};

    for (int i = 0; i <= 1; i++) {
        RPSysKokeshiIterater iter(sources[i]);

        RPSysKokeshiLocation location;
        location.SetDataSource(sources[i]);

        RPSysKokeshiGenInfo genInfo;
        genInfo.SetGenType(RPSysKokeshiManager::GenType_Direct);
        genInfo.SetBodyType(9);

        for (int j = 0; j < 100; j++) {
            if (!iter.Update()) {
                break;
            }

            location.SetIndex(iter.GetIndex());
            genInfo.SetLocation(location);

            mpHeads[i][j] = new RPKokeshiHead(&genInfo);
            mHeadNum[i]++;
        }
    }

    mpCtrlMgr = new RPSysKokeshiCtrlMgr(this, EGG::Heap::getCurrentHeap(), 0x100000);
}

void RPKokeshiMgr::onCreateResource(s32 chan, u8 index) {
    s32 source = chan + 2;

    RPSysKokeshiLocation location;
    location.SetIndex(index);
    location.SetDataSource(static_cast<RFLDataSource>(chan + RFLDataSource_Controller1));

    RPSysKokeshiGenInfo genInfo;
    genInfo.SetGenType(RPSysKokeshiManager::GenType_Direct);
    genInfo.SetBodyType(9);
    genInfo.SetLocation(location);

    mpHeads[source][mHeadNum[source]] = new RPKokeshiHead(&genInfo);

    RPSysKokeshiOverloadInfo overloadInfo;
    overloadInfo.SetClothesType(4);

    mpHeads[source][mHeadNum[source]]->LoadResource(&overloadInfo);
    mpHeads[source][mHeadNum[source]]->GetNigaoeModel()->SetDrawScene(mSceneNo);
    mpHeads[source][mHeadNum[source]]->GetBodyModel()->SetDrawScene(mSceneNo);
    mpHeads[source][mHeadNum[source]]->ApplyLightTexture(mSceneNo);
    mpHeads[source][mHeadNum[source]]->ResetTransform();
    mHeadNum[source]++;
}

RPKokeshiMgr::~RPKokeshiMgr() {}

void RPKokeshiMgr::onReleaseResource(s32 chan, u8 index) {
    s32 source = chan + 2;
    mpHeads[source][index] = NULL;
    mHeadNum[source] = 0;
}

void RPKokeshiMgr::Load(EGG::Archive* pArchive) {
    RPSysKokeshiManager::CreateLightTexture(mSceneNo);

    void* pLight = RPSysResourceManager::GetFileFromArchive(
        pArchive, "Environment/scene_Light.plight", NULL);
    RPGrpModelManager::GetCurrent()->GetLightManager(mSceneNo)->SetBinary(pLight);

    void* pLightMap = RPSysResourceManager::GetFileFromArchive(
        pArchive, "Environment/scene_Lm.plmap", NULL);
    RPGrpDrawPathLightMap* pDrawPath =
        RPGrpModelManager::GetCurrent()->GetDrawPathManager(mSceneNo)->GetDrawPathLightMap();
    pDrawPath->GetLightTextureManager()->onFlag2();
    pDrawPath->GetLightTextureManager()->SetBinary(pLightMap);

    RPSysKokeshiOverloadInfo overloadInfo;
    overloadInfo.SetClothesType(4);

    for (int i = 0; i < SOURCE_NUM; i++) {
        for (int j = 0; j < mHeadNum[i]; j++) {
            if (mpHeads[i][j] != NULL) {
                mpHeads[i][j]->LoadResource(&overloadInfo);
                mpHeads[i][j]->GetNigaoeModel()->SetDrawScene(mSceneNo);
                mpHeads[i][j]->GetBodyModel()->SetDrawScene(mSceneNo);
                mpHeads[i][j]->ApplyLightTexture(mSceneNo);
            }
        }
    }

    SortMiis();
}

void RPKokeshiMgr::SortMiis() {
    int start = 0;

    if (RP_GET_INSTANCE(RPSysProjectLocal)->getPack() == RPSysSceneCreator::EPackID_SportsPack) {
        FixPlayerMiiIndices();

        for (int i = 0; i < mHeadNum[0] - 1; i++) {
            for (int j = i + 1; j < mHeadNum[0]; j++) {
                u16 idxJ = mpHeads[0][j]->GetIndex();
                RPTime32 timeI = RP_GET_INSTANCE(RPSysSaveDataMgr)
                                     ->getSportsPlayerData(mpHeads[0][i]->GetIndex())
                                     .getDebutTime();
                RPTime32 timeJ =
                    RP_GET_INSTANCE(RPSysSaveDataMgr)->getSportsPlayerData(idxJ).getDebutTime();

                if (timeI < timeJ) {
                    RPKokeshiHead* pTemp = mpHeads[0][i];
                    mpHeads[0][i] = mpHeads[0][j];
                    mpHeads[0][j] = pTemp;
                }
            }
        }

        for (int i = 0; i < mHeadNum[0]; i++) {
            if (RP_GET_INSTANCE(RPSysSaveDataMgr)
                    ->getSportsPlayerData(mpHeads[0][i]->GetIndex())
                    .getDebutTime() == 0) {
                start = i;
                break;
            }
        }
    } else {
        FixPartyMiiIndices();

        for (int i = 0; i < mHeadNum[0] - 1; i++) {
            for (int j = i + 1; j < mHeadNum[0]; j++) {
                u16 idxJ = mpHeads[0][j]->GetIndex();
                RPTime32 timeI = RP_GET_INSTANCE(RPSysSaveDataMgr)
                                     ->getPartyPlayerData(mpHeads[0][i]->GetIndex())
                                     .getDebutTime();
                RPTime32 timeJ =
                    RP_GET_INSTANCE(RPSysSaveDataMgr)->getPartyPlayerData(idxJ).getDebutTime();

                if (timeI < timeJ) {
                    RPKokeshiHead* pTemp = mpHeads[0][i];
                    mpHeads[0][i] = mpHeads[0][j];
                    mpHeads[0][j] = pTemp;
                }
            }
        }

        for (int i = 0; i < mHeadNum[0]; i++) {
            if (RP_GET_INSTANCE(RPSysSaveDataMgr)
                    ->getPartyPlayerData(mpHeads[0][i]->GetIndex())
                    .getDebutTime() == 0) {
                start = i;
                break;
            }
        }
    }

    for (int i = start; i < mHeadNum[0] - 1; i++) {
        if (mpHeads[0][i]->GetAdditionalInfo().favorite) {
            continue;
        }

        for (int j = i + 1; j < mHeadNum[0]; j++) {
            if (mpHeads[0][j]->GetAdditionalInfo().favorite) {
                RPKokeshiHead* pTemp = mpHeads[0][i];
                mpHeads[0][i] = mpHeads[0][j];
                mpHeads[0][j] = pTemp;
            }
        }
    }
}

void RPKokeshiMgr::ResetAll() {
    mVisible = false;
    mAlpha = 0;
    mOffsetY = -2.0f;
    mScale = 14.0f;
    mpCamera->SetPosition(nw4r::math::VEC3(0.0f, 0.0f, 50.0f));

    for (int i = 0; i < SOURCE_NUM; i++) {
        for (int j = 0; j < mHeadNum[i]; j++) {
            mpHeads[i][j]->ResetTransform();
            DetachKokeshi(i, j);
        }
    }

    ApplyPlayerHighlights(0);
}

void RPKokeshiMgr::Calculate() {
    if (mVisible) {
        mAlpha += 30;
        if (mAlpha > 255) {
            mAlpha = 255;
        }
    } else {
        mAlpha -= 30;
        if (mAlpha < 0) {
            mAlpha = 0;
        }
    }

    u8 alpha = GetAlpha();
    RPGrpModelManager::GetCurrent()
        ->GetDrawPathManager(1)
        ->GetDrawPathXluSnap()
        ->GetXluSnap()
        ->setAlpha(alpha);

    mpCamera->CalcMatrix();

    for (int i = 0; i < SOURCE_NUM; i++) {
        for (int j = 0; j < mHeadNum[i]; j++) {
            if (mSlots[i].mpPanes[j] != NULL) {
                EGG::Vector3f scale(mScale, mScale, mScale);
                if (mSlots[i].mpScalePanes[j] != NULL) {
                    scale = CalcPaneScale(mSlots[i].mpScalePanes[j]);
                }

                mpHeads[i][j]->SetTransform(CalcPaneMtx(mSlots[i].mpPanes[j]), scale);
                mpHeads[i][j]->Calculate(mAlpha != 255);
            }
        }
    }

    mpCtrlMgr->calc();
}

void RPKokeshiMgr::SetViewMode(s32 mode, s32 layout) {
    f32 params[][2] = {
        {-2.0f, 14.0f}, {6.0f, 18.0f}, {6.0f, 18.0f}, {5.0f, 17.0f}, {2.0f, 16.0f},
    };

    s32 idx = mode == 2 ? layout : 0;
    mOffsetY = params[idx][0];
    mScale = params[idx][1];

    RPGrpScreen* pScreen = RPGrpRenderer::GetCurrent()->GetView(mViewNo)->GetScreen();

    if (mode == 0) {
        if (EGG::Screen::GetTVMode() == EGG::Screen::TV_MODE_STD) {
            pScreen->SetPositionX(38.0f);
            pScreen->SetSizeX(532.0f);
        } else {
            pScreen->SetPositionX(51.0f);
            pScreen->SetSizeX(734.0f);
        }
    } else {
        pScreen->SetPositionX(0.0f);
        pScreen->SetSizeX(EGG::Screen::GetSizeXMax());
    }
}

void RPKokeshiMgr::AttachKokeshi(nw4r::lyt::Pane* pPane, nw4r::lyt::Pane* pScalePane,
                                 s32 source, s32 idx) {
    mpHeads[source][idx]->GetNigaoeModel()->SetVisible(true);
    mpHeads[source][idx]->GetBodyModel()->SetVisible(true);
    mSlots[source].mpPanes[idx] = pPane;
    mSlots[source].mpScalePanes[idx] = pScalePane;
}

void RPKokeshiMgr::DetachKokeshi(s32 source, s32 idx) {
    mpHeads[source][idx]->GetNigaoeModel()->SetVisible(false);
    mpHeads[source][idx]->GetBodyModel()->SetVisible(false);
    mSlots[source].mpPanes[idx] = NULL;
    mSlots[source].mpScalePanes[idx] = NULL;
}

void RPKokeshiMgr::DetachSource(s32 source) {
    for (int i = 0; i < mHeadNum[source]; i++) {
        mpHeads[source][i]->GetNigaoeModel()->SetVisible(false);
        mpHeads[source][i]->GetBodyModel()->SetVisible(false);
        mSlots[source].mpPanes[i] = NULL;
        mSlots[source].mpScalePanes[i] = NULL;
    }
}

void RPKokeshiMgr::SetPlayerSlot(s32 source, u16 idx, s32 player) {
    mPlayers[player].mpHead = mpHeads[source][idx];
    mPlayers[player].mSource = source;
    mPlayers[player].mIndex = idx;

    switch (source) {
    case 0:
        mPlayers[player].mType = 0;
        break;
    case 1:
        mPlayers[player].mType = 6;
        break;
    case 2:
        mPlayers[player].mType = 2;
        break;
    case 3:
        mPlayers[player].mType = 3;
        break;
    case 4:
        mPlayers[player].mType = 4;
        break;
    case 5:
        mPlayers[player].mType = 5;
        break;
    }

    mPlayers[player].mMiiIndex = mPlayers[player].mpHead->GetIndex();
}

void RPKokeshiMgr::SetPlayerSlotByType(s8 type, u16 miiIndex, s32 player) {
    mPlayers[player].mType = type;
    mPlayers[player].mMiiIndex = miiIndex;

    switch (type) {
    case 0:
        mPlayers[player].mSource = 0;
        break;
    case 6:
        mPlayers[player].mSource = 1;
        break;
    case 2:
        mPlayers[player].mSource = 2;
        break;
    case 3:
        mPlayers[player].mSource = 3;
        break;
    case 4:
        mPlayers[player].mSource = 4;
        break;
    case 5:
        mPlayers[player].mSource = 5;
        break;
    }

    for (int i = 0; i < mHeadNum[mPlayers[player].mSource]; i++) {
        if (miiIndex == mpHeads[mPlayers[player].mSource][i]->GetIndex()) {
            mPlayers[player].mpHead = mpHeads[mPlayers[player].mSource][i];
            mPlayers[player].mIndex = i;
            return;
        }
    }
}

RPKokeshiMgr::Player* RPKokeshiMgr::GetPlayer(s32 player) {
    return &mPlayers[player];
}

bool RPKokeshiMgr::IsInPlayers(s32 source, u16 idx, s32 num) const {
    for (int i = 0; i < num; i++) {
        if (source == mPlayers[i].mSource && idx == mPlayers[i].mIndex) {
            return true;
        }
    }
    return false;
}

void RPKokeshiMgr::ApplyPlayerHighlights(s32 num) {
    RPSysKokeshiManager* pKokeshiMgr = RP_GET_INSTANCE(RPSysKokeshiManager);

    for (int i = 0; i < SOURCE_NUM; i++) {
        for (int j = 0; j < mHeadNum[i]; j++) {
            RPGrpModel* pBody = mpHeads[i][j]->GetBodyModel();
            pKokeshiMgr->SetMaterialType(mpHeads[i][j]->GetNigaoeModel(),
                                         RPGrpModelRfl::MaterialType_0);
            pKokeshiMgr->SetMatTevColorEffect(pBody, 255, 0, 0, 0);
        }
    }

    for (int i = 0; i < num; i++) {
        RPKokeshiHead* pHead = mpHeads[mPlayers[i].mSource][mPlayers[i].mIndex];
        RPGrpModel* pNigaoe = pHead->GetNigaoeModel();
        RPGrpModel* pBody = pHead->GetBodyModel();
        pKokeshiMgr->SetMaterialType(pNigaoe, RPGrpModelRfl::MaterialType_2);
        SetBrightnessColor__19RPSysKokeshiManagerFP10RPGrpModel8_GXColor(pKokeshiMgr, pNigaoe,
                                                                       &mHighlightColor);
        pKokeshiMgr->SetMatTevColorEffect(pBody, mHighlightColor.a, mHighlightColor.r,
                                          mHighlightColor.g, mHighlightColor.b);
    }
}

void RPKokeshiMgr::ReleaseRemoteMii(s32 chan) {
    mpCtrlMgr->notifyReleaseResource(chan);
}

bool RPKokeshiMgr::IsRemoteMiiReady(s32 chan) {
    return mpCtrlMgr->isIdle(chan) != FALSE;
}

void RPKokeshiMgr::RequestRemoteMii(s32 chan) {
    mpCtrlMgr->resetResource(chan);
}

BOOL RPKokeshiMgr::IsRemoteLoadError(s32 chan) const {
    return mpCtrlMgr->isLoadError(chan);
}

bool RPKokeshiMgr::IsRecordsChanged() const {
    return mRecordsChanged;
}

void RPKokeshiMgr::ClearRecordsChanged() {
    mRecordsChanged = false;
}

#pragma push
#pragma auto_inline off
void RPKokeshiMgr::FixPlayerMiiIndices() {
    mRecordsChanged = false;
    RPSysSaveDataMgr* pSaveDataMgr = RP_GET_INSTANCE(RPSysSaveDataMgr);

    for (int i = 0; i < 100; i++) {
        RPSportsPlayerData data = pSaveDataMgr->getSportsPlayerData(i);
        RFLCreateID createID;
        data.getCreateID(&createID);

        if (IsValidCreateID(createID) &&
            i != RPSysKokeshiManager::GetOfficialDBIndexFromCreateID(&createID)) {
            mRecordsChanged = true;
            data.reset();
            pSaveDataMgr->setSportsPlayerData(data, i);
        }
    }
}

void RPKokeshiMgr::FixPartyMiiIndices() {
    mRecordsChanged = false;
    RPSysSaveDataMgr* pSaveDataMgr = RP_GET_INSTANCE(RPSysSaveDataMgr);

    for (int i = 0; i < 100; i++) {
        RPPartyPlayerData data = pSaveDataMgr->getPartyPlayerData(i);
        RFLCreateID createID;
        data.getCreateID(&createID);

        if (IsValidCreateID(createID) &&
            i != RPSysKokeshiManager::GetOfficialDBIndexFromCreateID(&createID)) {
            mRecordsChanged = true;
            data.reset();
            pSaveDataMgr->setPartyPlayerData(data, i);
        }
    }
}

#pragma pop

void RPKokeshiMgr::SetVisible(bool visible) {
    mVisible = visible;
}

u8 RPKokeshiMgr::GetAlpha() const {
    return mAlpha;
}

const wchar_t* RPKokeshiMgr::GetMiiName(s32 source, s32 idx) const {
    return mpHeads[source][idx]->GetAdditionalInfo().name;
}

EGG::Vector3f RPKokeshiMgr::CalcPaneScale(const nw4r::lyt::Pane* pPane) {
    nw4r::math::VEC2 paneScale = pPane->GetScale();
    EGG::Vector3f scale(paneScale.x, paneScale.y, paneScale.x);
    f32 factor = mScale;
    scale.x *= factor;
    scale.y *= factor;
    scale.z *= factor;
    return scale;
}

// (Matches since 2026-10-28, when its Pane* parameter lost a guessed const; history below.)
// Was NONMATCHING: initial matrix-copy register allocation and instruction order.
// The original saves r27-r31 (_savegpr_27), loads words 3 and 7 first and holds words 0-2 in
// r27-r29. A plain `MTX34 paneMtx = pPane->GetGlobalMtx();` is 80.6% (r28-r31 saved with stw);
// passing the matrix BY VALUE to a static inline returning VEC2(m._03, m._13) reproduces the
// _savegpr / words-3-and-7-first pattern (82.0%) but adds a VEC2 temporary copy and one more
// saved register (r26). Out-parameter helpers, Vector2f set(), y-first stores, view first, and
// the VEC2 temporary passed straight to GetScreenToWorld did not help.
// Also tried (2026-10-28): ascending/descending word copies, member-wise VEC2 fills (x or y
// first), EGG::Vector2f, MTX34 operator[] / .mtx access, a VEC2 temporary argument, the view
// pointer first or between, by-value inlines with VEC2&/VEC2* out parameters or f32 returns,
// an inline that calls GetScreenToWorld itself (69-82%).
// Also tried (2026-10-28, second pass): const/_MTX34/assigned/copy-constructed locals, an inline
// returning the MTX34 by value, a VEC2 assigned from a by-value inline, y-first member fills,
// f32 locals for x and y, the view pointer in a local: all 35 differing words or worse. The
// plain copy below replaces a u32 word-by-word copy helper (same 35 words, natural C).
// 2026-10-28 (3, mwcc_trace): the PCode the allocator sees is already prepass-scheduled: ours
// interleaves the paneMtx stores with the screenPos lfs/stfs, so only 2 callee-saved registers
// are needed; the original loads all ten remaining words before the first lfs (hence r27-r29).
// A scheduling difference, not a colouring one. No effect: pos or screenPos declared before
// paneMtx, a const VEC2, VEC2 = VEC2(...), screenPos assigned after the copy.
// 2026-10-28 (4): in the original the screenPos lfs wait until all ten other words are loaded (and
// addi r5 comes right after the last load), y before x; ours issues lfs x as soon as word 3 is
// stored. No effect: copy or screenPos fill in a reference/pointer helper, a by-value MTX34 helper
// filling the VEC2 (53 words), a const-ref helper returning VEC2 (94), Matrix34f::copyFrom (51).
EGG::Matrix34f RPKokeshiMgr::CalcPaneMtx(nw4r::lyt::Pane* pPane) {
    nw4r::math::MTX34 paneMtx = pPane->GetGlobalMtx();
    nw4r::math::VEC2 screenPos(paneMtx._03, paneMtx._13);

    EGG::Vector3f pos;
    RPGrpRenderer::GetCurrent()->GetView(mViewNo)->GetScreenToWorld(&pos, screenPos);
    pos.y += mOffsetY;

    nw4r::math::MTX34 cameraMtx = mpCamera->GetViewMatrix();
    EGG::Vector3f cameraPos(cameraMtx._03, cameraMtx._13, cameraMtx._23);

    EGG::Vector3f dir = __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&cameraPos, pos);
    EGG::Vector3f up(0.0f, 1.0f, 0.0f);
    EGG::Vector3f right = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&up, dir);
    up = cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(&dir, right);

    right.normalise();
    up.normalise();
    dir.normalise();

    EGG::Matrix34f mtx;
    mtx.setBase(0, right);
    mtx.setBase(1, up);
    mtx.setBase(2, dir);
    mtx.setBase(3, pos);
    return mtx;
}
