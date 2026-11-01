#ifndef RP_PARTY_COMMON_MEDAL_RESULT_H
#define RP_PARTY_COMMON_MEDAL_RESULT_H
#include <Pack/types_pack.h>

#include <Pack/RPParty/RPPartyCommon/RPActorBase.h>
#include <Pack/RPParty/RPPartyCommon/RPLytObjBase.h>
#include <Pack/RPParty/RPPartyCommon/RPStateMachine.h>

#include <nw4r/lyt.h>
#include <nw4r/math.h>

//! @addtogroup rp_party
//! @{

// Forward declarations
namespace EGG {
class Vector2f;
}
class RPGrpModel;
class RPPartyPlayerData;
class RPSysEffect;
class RPSysLayout;
class RPSysLytAnmObj;

/**
 * @brief "New rank" banner on the single-player result screen (myRecord.arc,
 * ptRankGet_00.brlyt)
 * @details vtable 803581F8. Size 0x24, verified (Party_MedalResult_ctor). Five states (table
 * 8031BDC0): 0 hidden, 1 in, 2 shown, 3 out, 4 done. Open sets the step and rank texts
 * (party_message.bmg 0x1F4B/0x1F4C).
 * @note Wii Play-only framework class (ledger Party_RankGetLyt); the name is a project
 * convention.
 */
class RPRankGetLyt : public RPLytObjBase {
public:
    RPRankGetLyt();

    virtual void Load(EGG::Archive* pArchive); // at 0x8
    virtual void Reset();                    // at 0xC
    virtual void Calculate();                // at 0x10

    void Open(s32 rank);
    void Close();
    void DrawLayer1C();
    void DrawLayer1A();

#if defined(PORT_HOST)
    // The panes RP_getPairFromField1C / 20 read on the host (RPMedalResult_2.cpp)
    const nw4r::lyt::Pane* GetPlayerPosPane() const {
        return mpPlayerPosPane;
    }
    const nw4r::lyt::Pane* GetRankPosPane() const {
        return mpRankPosPane;
    }
#endif

private:
    void EnterIn();
    void CalcIn();
    void EnterShow();
    void CalcShow();
    void EnterOut();
    void CalcOut();

private:
    s32 mRank;                                     // at 0x8 1-based
public:
    RPStateMachine<RPRankGetLyt>* mpStateMachine;  // at 0xC
private:
    RPSysLayout* mpLayout;                         // at 0x10
    RPSysLytAnmObj* mpAnmObjs[2];                  // at 0x14 in, out
    nw4r::lyt::Pane* mpPlayerPosPane;              // at 0x1C N_pPosition_00
    nw4r::lyt::Pane* mpRankPosPane;                // at 0x20 N_rPosition_00
};

//! Ledger Party_RankGetSeq (size 0x28)
typedef RPStateMachine<RPRankGetLyt> RPRankGetSeq;

/**
 * @brief Top-five record list on the single-player result screen (myRecord.arc,
 * myRecord_00.brlyt)
 * @details vtable 803582E8. Size 0x54, verified (Party_MedalResult_ctor). Seven states (table
 * 8031C3F8): 0 hidden, 1 in, 2 blink, 3 new record, 4 shown, 5 out, 6 done. Animations: in,
 * newRec, outRec, outNewRec, blink.
 * @note Wii Play-only framework class (ledger Party_RecordLyt); the name is a project
 * convention.
 */
class RPRecordLyt : public RPLytObjBase {
public:
    static const int RECORD_NUM = 5;

public:
    RPRecordLyt();

    virtual void Load(EGG::Archive* pArchive); // at 0x8
    virtual void Reset();                    // at 0xC
    virtual void Calculate();                // at 0x10

    void Open(s32 score, s32 rank, const s32* pRecords, bool hasRecords);
    void Close();
    void DrawLayer1C();
    void DrawLayer1A();
    EGG::Vector2f GetPanePos() const; // the latched position, else N_pPosition_00's
    void LatchPanePos();

private:
    void EnterIn();
    void CalcIn();
    void EnterBlink();
    void CalcBlink();
    void EnterNewRec();
    void CalcNewRec();
    void EnterShow();
    void CalcShow();
    void EnterOut();
    void CalcOut();

public:
    RPStateMachine<RPRecordLyt>* mpStateMachine; // at 0x8
private:
    s32 mScore;                                  // at 0xC
    s32 mRank;                                   // at 0x10 -1 = no new record
    s32 mRecords[RECORD_NUM];                    // at 0x14
    bool mHasRecords;                            // at 0x28
    nw4r::math::VEC2 mPanePos;                   // at 0x2C
    bool mPanePosLatched;                        // at 0x34
public:
    bool unk35;                                  // at 0x35
private:
    RPSysLayout* mpLayout;                       // at 0x38
    RPSysLytAnmObj* mpAnmObjs[5];                // at 0x3C in, newRec, outRec, outNewRec, blink
    nw4r::lyt::Pane* mpPlayerPosPane;            // at 0x50 N_pPosition_00

    static const char* sNewRecPaneNames[RECORD_NUM]; // 803582C0 N_newRec_00-04
    static const char* sRecordPaneNames[RECORD_NUM]; // 803582D4 N_tRecord_00-04, shown on Reset
    static const char* sCenterPaneNames[2];          // 8043B408 N_center_00/01
};

//! Ledger Party_RecordSeq (size 0x28)
typedef RPStateMachine<RPRecordLyt> RPRecordSeq;

/**
 * @brief Single-player result: record list, medal award and save (RPCommon + 0x18)
 * @details vtable 80358218 (RPActorBase slots only). Size 0x154, verified
 * (Party_Common_ctor). Minigames call Start(score); five states (table 8031BF80): 0 idle,
 * 1 record (rank, new top five, medal from the per-game thresholds 8031BEF0), 2 medal (medal
 * model appear/wait animations, effects, BGM RP_COM_BGM_GET_*), 3 save, 4 done. SaveResults
 * writes the records and medal, unlocks the next game and records a Message Board post.
 * The actor axes/position (and the second set at 0xB0) place the medal model.
 * @note Wii Play-only framework class (ledger Party_MedalResult); the name is a project
 * convention.
 */
class RPMedalResult : public RPActorBase {
public:
    static const int RECORD_NUM = 5;
    static const int MEDAL_MODEL_NUM = 4;
    static const int EFFECT_NUM = 22;

public:
    RPMedalResult();
    virtual ~RPMedalResult(); // at 0x8

    void Load();
    void Reset();
    void Calculate();
    void Start(s32 score);
    s32 GetState() const {
        return mpStateMachine->GetState();
    }
    void DrawLayer1C();
    void DrawLayer1A();
    s32 GetRankForScore(s32 score) const;
    void SaveResults();

private:
    void EnterRecord();
    void CalcRecord();
    void EnterMedal();
    void CalcMedal();
    void EnterSave();
    void CalcSave();

private:
    RPStateMachine<RPMedalResult>* mpStateMachine; // at 0x60
    s32 mScore;                                    // at 0x64
    s32 mRank;                                     // at 0x68 -1 = not in the top five
    s32 mRecords[RECORD_NUM];                      // at 0x6C saved top five
    s32 mNewRecords[RECORD_NUM];                   // at 0x80 top five including mScore
    bool mHasRecords;                              // at 0x94 a non-zero record from mRank down
public:
    s32 mMedal;                                    // at 0x98 0 none .. 4
private:
    bool mMedalsEnabled;                           // at 0x9C save flag 9
    bool mPostMessage;                             // at 0x9D Message Board post recorded
    RPPartyPlayerData* mpPlayerData;               // at 0xA0 player 0
    bool unkA4;                                    // at 0xA4
    RPRecordLyt* mpRecordLyt;                      // at 0xA8
    RPRankGetLyt* mpRankGetLyt;                    // at 0xAC
    nw4r::math::VEC3 mMedalAxisZ;                  // at 0xB0
    nw4r::math::VEC3 mMedalAxisY;                  // at 0xBC
    nw4r::math::VEC3 mMedalAxisX;                  // at 0xC8
    nw4r::math::VEC3 mMedalPos;                    // at 0xD4
    f32 mMedalScale;                               // at 0xE0 per screen mode
public:
    RPGrpModel* mpMedalModels[MEDAL_MODEL_NUM];    // at 0xE4 bronze, silver, gold, platinum
private:
    f32 mFade;                                     // at 0xF4 0.01 .. 1
    s32 mFadeDir;                                  // at 0xF8 -1, 0, 1
    RPSysEffect* mpEffects[EFFECT_NUM];            // at 0xFC

public:
    static f32 sActorScale[2]; // [TV mode]
    static f32 sAngleA[2];     // [TV mode], degrees
    static f32 sMedalScale[2]; // [TV mode]
    static bool sFlag;
    static f32 sAngleB[2];     // [TV mode], degrees
    static EGG::Vector2f sRecordOffsets[2]; // [TV mode]
    static EGG::Vector2f sModelOffsets[2];  // [TV mode]
};

//! Ledger Party_MedalResultSeq (size 0x28)
typedef RPStateMachine<RPMedalResult> RPMedalResultSeq;

//! @}

#endif
