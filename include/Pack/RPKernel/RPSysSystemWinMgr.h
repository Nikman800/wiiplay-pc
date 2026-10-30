#ifndef RP_KERNEL_SYSTEM_WINDOW_MGR_H
#define RP_KERNEL_SYSTEM_WINDOW_MGR_H
#include <Pack/types_pack.h>

#include <Pack/RPSingleton.h>

//! @addtogroup rp_kernel
//! @{

// Forward declarations
class RPCPadCursor;
class RPSysLayout;
class RPSysLytAnmObj;
class RPSysLytBounding;
class RPSysLytResAccessor;
class RPSysLytTextBox;
class RPSysMessage;
namespace nw4r {
namespace lyt {
class Pane;
} // namespace lyt
} // namespace nw4r

/**
 * @brief System window manager
 * @details Wii Play: vtable 8034F8CC, size 0xE0 (verified, Lyt_createSingleton8043C530; ledger
 * Lyt_SystemWindow). Members below 0x90 and above 0xB0 are from Wii Play's code (messageWindow.arc,
 * systemWindow_00.brlyt, two Yes/No buttons, an A-button prompt and an RPCPadCursor).
 */
class RPSysSystemWinMgr {
    RP_SINGLETON_DECL_EX(RPSysSystemWinMgr);
    friend class RPSysTagProcessor; // sets mSeType from a message tag

public:
    enum E_ALIGN {};

    enum E_BUTTON {
        E_BUTTON_YES,
        E_BUTTON_NO,
    };

    enum E_WINDOW_TYPE {
        E_WINDOW_TYPE_TEXT,
        E_WINDOW_TYPE_ERROR,
    };

    enum E_RESULT {
        RESULT_NONE,
        RESULT_WAIT,
        RESULT_YES,
        RESULT_NO,
        RESULT_4,
        RESULT_5,
        RESULT_CONTINUE,
    };

    enum ESound {};

public:
    void setSystemWindow(E_WINDOW_TYPE type, u32 group, RPSysMessage* pMessage,
                         int argc, ...);

    // Wii Play: returns true (r3 = 1)
    bool setCursors();

    void setDefaultCursor(E_BUTTON button) {
        mNextButton = 0;
        mSelButton = button;
        mHoverButton[0] = 0;
        mHoverButton[1] = 0;
        mHoverButton[2] = 0;
        mHoverButton[3] = 0;
    }

    E_RESULT getResult() {
        return mResult;
    }

    s32 getState() const {
        return mState;
    }

    // Wii Play: window code
    void load();
    void reset();
    void calc();
    void draw();
    void closeSystemWindow();
    void setMessagePage(s32 page);
    // Wii Play: returns TRUE (r3 = 1)
    BOOL setPlayerInput(s32 player);

private:
    void resetButtons();
    void updateButtons();
    void startAButtonIn();
    void resetInputState();
    void enableCursors();
    void updateAButtonOnly();
    void updateFocus();
    void checkAPress();
    bool hitTestPointer(u16 index);
    bool checkDpadInput(u16 index) DECOMP_DONT_INLINE;
    void resetButtonAnims();
    void decideButton(s32 channel);

    /**
     * @brief One Yes/No button of the window (Wii Play)
     */
    struct Button {
        nw4r::lyt::Pane* mpPane;         // at 0x0 N_choicesL/R_00
        RPSysLytBounding* mpBounding;    // at 0x4 B_choicesL/R_00
        RPSysLytTextBox* mpTextBox;      // at 0x8 T_choicesL/R_00
        RPSysLytAnmObj* mpAnmObjs[3];    // at 0xC active, active, push
        u8 mAnmState;                    // at 0x18 0 idle, 1 active, 2 pushed
        bool mSelected;                  // at 0x19
    };

    /**
     * @brief Player whose cursor may answer the window (Wii Play)
     */
    struct InputPlayer {
        s32 mPlayer;  // at 0x0 RPSysCursorDrawMgr::EPlayer, 4 = active cursor
        s32 mChannel; // at 0x4 controller channel, -1 = unknown
    };

    // Wii Play: members up to 0x90 from Wii Play's code
    RPSysLytResAccessor* mpResAccessor; // at 0x8
    RPSysLayout* mpLayout;              // at 0xC systemWindow_00.brlyt
    Button mButtons[2];                 // at 0x10
    nw4r::lyt::Pane* mpAButtonPane;     // at 0x48 N_AButton_00
    u32 unk4C;                          // at 0x4C
    u32 unk50;                          // at 0x50
    RPSysLytAnmObj* mpAButtonAnmObjs[2]; // at 0x54 inAButton, blinkAButton
    u32 unk5C;                          // at 0x5C
    char unk60[0x64 - 0x60];
    nw4r::lyt::Pane* mpCenterPane;      // at 0x64 N_center_00
    RPSysLytTextBox* mpTextBoxes[4];    // at 0x68 T_message_02, _00, _03, _01
    RPSysLytAnmObj* mpInAnmObj;         // at 0x78
    RPSysLytAnmObj* mpOutAnmObj;        // at 0x7C
    s32 mTimer;                         // at 0x80 in/out/push animation frames left

public:
    // Wii Play: public, written by RPSysTagProcessor's window tags
    u16 mCloseDelay;                    // at 0x84 0 = wait for an answer

private:
    s32 mFocus;                         // at 0x88 button animation frames left
    s32 mState;                         // at 0x8C 0 = closed
    E_RESULT mResult;                   // at 0x90
    u16 mWindowType;                    // at 0x94 E_WINDOW_TYPE
    u16 mMessageId;                     // at 0x96 message attribute (0, 2, 888, 998, 999)
    s32 mPage;                          // at 0x98 Wii Play
    RPSysMessage* mpMessage;            // at 0x9C Wii Play: common_message.bmg
    u32 mGroup;                         // at 0xA0 Wii Play: message group
    RPCPadCursor* mpCursor;             // at 0xA4 Wii Play
    bool mPointerHit;                   // at 0xA8 Wii Play
    bool mDpadInput;                    // at 0xA9 Wii Play
    bool mUseCPadCursor;                // at 0xAA Wii Play: message has a button choice [L]

public:
    // Wii Play: public, written by RPSysTagProcessor's window tags
    u8 mHoverButton[4];                 // at 0xAB per input player, 2 = none
    u8 mNextButton;                     // at 0xAF
    u8 mSelButton;                      // at 0xB0

private:
    // Wii Play: members from here from Wii Play's code
    bool mInputSet;                     // at 0xB1
    bool mUseActiveCursor;              // at 0xB2
    u8 mInputPlayerNum;                 // at 0xB3
    InputPlayer mInputPlayers[4];       // at 0xB4
    u8 mSeType[2];                      // at 0xD4 per button: 0 decide, 1 back
    bool mCursorMoved;                  // at 0xD6
    nw4r::math::VEC2 mCursorPos;        // at 0xD8

public:
    static const char* sButtonAnimNames[3];
    static const char* sAButtonAnimNames[2];
    static const char* sButtonPaneNames[2];
    static const char* sButtonBoundingNames[2];
    static const char* sButtonTextNames[2];
};

//! @}

#endif
