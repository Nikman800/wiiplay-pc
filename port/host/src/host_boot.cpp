/**
 * What the boot executable links but the host does not provide elsewhere
 * (port/docs/HOST_BUILD.md §29). Each entry is one of:
 *
 *   data      game data the decomp has not attributed to a unit yet (labels in
 *             RPSysSystem's data; the health and safety archive of main.dol's
 *             .data6, filled by host_main.cpp from the disc's main.dol), and
 *             static members whose defining unit is not decompiled;
 *   no device the hardware or service is not there on a PC: WiiConnect24
 *             (every call fails with NWC24_ERR_FATAL, which the game treats as
 *             "not available"), the IPL ROM font (OSInitFont fails), the
 *             virtual file system (VFIsAvailable: none), Mii data in a remote
 *             (WPADReadFaceData: no controller), power and reset buttons
 *             (callbacks recorded, never called);
 *   placeholders  CodeWarrior-mangled names that some game units call by name
 *             (the "ledger placeholders"): forwarded to the real functions.
 *
 * Every minigame scene is linked (§32); none of them has a stand-in here.
 */

#include <types.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPKernel.h>

#include <Pack/RPParty/RPPartyCommon/RPDoll.h>
#include <Pack/RPParty/RPPartyCommon/RPPlayerConfirm.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>

#include <Pack/RPParty/RPBilScene/RPBilTable.h>
#include <Pack/RPParty/RPBomScene/RPBomBorder.h>
#include <Pack/RPParty/RPBomScene/RPBomCursor.h>
#include <Pack/RPParty/RPBomScene/RPBomLyt.h>
#include <Pack/RPParty/RPBomScene/RPBomPanel.h>
#include <Pack/RPParty/RPBomScene/RPBomPlayer.h>
#include <Pack/RPParty/RPBomScene/RPBomQuestionParam.h>
#include <Pack/RPParty/RPBomScene/RPBomScore.h>
#include <Pack/RPParty/RPBomScene/RPBomScreenFx.h>
#include <Pack/RPParty/RPTnkScene/RPTnkLyt.h>

#include <egg/core/eggController.h>
#include <egg/gfxe/eggShadowTexture.h>
#include <egg/math/eggMatrix.h>

#include <egg/audio.h>
#include <egg/math.h>

#include <nw4r/g3d/g3d_scnmdl1mat1shp.h>

#include <revolution/NWC24.h>
#include <revolution/OS.h>
#include <revolution/WPAD.h>

#include <port/host.h>

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void NotLinked(const char* what) {
    std::fflush(stdout);
    std::fprintf(stderr, "boot: %s is not linked on the host (HOST_BUILD.md §29)\n", what);
    std::abort();
}

} // namespace

/* ---- Data ---------------------------------------------------------------- */

extern "C" {
// RPSysSystem's data (0x80316CB8: EGG::Exception's pad combination; 0x80316CC8:
// "timeStamp.txt", then the loading message RPSysSystem::loadFrameWork prints)
extern const u16 lbl_80316CB8[];
extern const char lbl_80316CC8[];
const u16 lbl_80316CB8[8] = {0x8000, 0x0800, 0x0400, 0x0200, 0x0100, 0x1000, 0x0010, 0x0000};
const char lbl_80316CC8[] = "timeStamp.txt\0Now Loading";

// main.dol's .data6 (RPSysBootScene: the Yaz0 health and safety archive). Filled
// by host_main.cpp from the disc's sys/main.dol before the game starts.
alignas(32) u8 Health_strapArchive[PORT_STRAP_ARCHIVE_MAX];
}

nw4r::snd::SoundArchivePlayer* EGG::AudioSound3DActor::smCommonPlayer = NULL;
nw4r::snd::Sound3DManager* EGG::AudioSound3DActor::smCommon3DManager = NULL;
template <>
nw4r::snd::Sound3DManager* EGG::AudioSoundActorBaseWithCamera<4>::smCommon3DManager[4] = {};
nw4r::ut::List RPSndAudioActorBase::sActorList;
// Singletons of the party menus (their defining units' data is not decompiled yet)
RPDollMgr* RPDollMgr::sInstance = NULL;
RPPlayerConfirm* RPPlayerConfirm::sInstance = NULL;
RPSceneInfo* RPSceneInfo::spInstance = NULL;
// Pose Mii's singletons (.sbss 8043CA88-8043CAE0; no unit defines them yet)
RPBomCursorManager* RPBomCursorManager::sInstance = NULL;
RPBomPlayerManager* RPBomPlayerManager::sInstance = NULL;
RPBomQuestionParam* RPBomQuestionParam::sInstance = NULL;
RPBomScreenFx* RPBomScreenFx::sInstance = NULL;
RPBomScore* RPBomScore::sInstance = NULL;
RPBomPanelCtrl* RPBomPanelCtrl::sInstance = NULL;
RPBomPanelPool* RPBomPanelPool::sInstance = NULL;
RPBomBorder* RPBomBorder::sInstance = NULL;
RPBomLayoutMgr* RPBomLayoutMgr::sInstance = NULL;

namespace nw4r {
namespace g3d {
// ScnMdl1Mat1Shp's unit is not decompiled; G3dObj::DynamicCast needs its type name
NW4R_G3D_RTTI_DEF(ScnMdl1Mat1Shp);
} // namespace g3d
} // namespace nw4r

/* ---- No device ----------------------------------------------------------- */

extern "C" {

s32 NWC24SuspendScheduler(void) {
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24OpenLib(NWC24Work* pWork) {
    (void)pWork;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24CloseLib(void) {
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24GetMyUserId(u64* pId) {
    *pId = 0;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24InitMsgObj(NWC24MsgObj* pMsg, NWC24MsgType type) {
    (void)pMsg;
    (void)type;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24SetMsgToId(NWC24MsgObj* pMsg, u64 id) {
    (void)pMsg;
    (void)id;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24SetMsgText(NWC24MsgObj* pMsg, const char* pText, u32 len, NWC24Charset charset,
                         NWC24Encoding encoding) {
    (void)pMsg;
    (void)pText;
    (void)len;
    (void)charset;
    (void)encoding;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24SetMsgFaceData(NWC24MsgObj* pMsg, const RFLCharData* pData) {
    (void)pMsg;
    (void)pData;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24SetMsgAltName(NWC24MsgObj* pMsg, const wchar_t* pName, u32 len) {
    (void)pMsg;
    (void)pName;
    (void)len;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24SetMsgMBNoReply(NWC24MsgObj* pMsg, BOOL enable) {
    (void)pMsg;
    (void)enable;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24SetMsgMBRegDate(NWC24MsgObj* pMsg, u16 year, u8 month, u8 day) {
    (void)pMsg;
    (void)year;
    (void)month;
    (void)day;
    return NWC24_ERR_FATAL;
}
NWC24Err NWC24CommitMsg(NWC24MsgObj* pMsg) {
    (void)pMsg;
    return NWC24_ERR_FATAL;
}

// The IPL ROM font is not on the disc (HOST_BUILD.md §30). The game draws with it
// only debug text (the title's build time stamp, Table Tennis' debug values) and
// the disc-error screen, and asks it for metrics when Table Tennis and Laser
// Hockey call SetFontSize. The stand-in is a font with the ANSI ROM font's cell
// size (24 x 24, ascent 20, descent 4: approximate) and a blank glyph sheet in
// the buffer the game gives OSInitFont: metrics work, the text is invisible.
namespace {
const u16 FONT_CELL = 24;
const u16 FONT_SHEET = 256; // I4, 10 x 10 cells
OSFontHeader* sFont = NULL;
}
u16 OSGetFontEncode(void) {
    return OS_FONT_ENCODE_ANSI;
}
BOOL OSInitFont(OSFontHeader* font) {
    const u32 sheetBytes = FONT_SHEET * FONT_SHEET / 2;
    std::memset(font, 0, 0x40 + sheetBytes);
    font->type = 0;
    font->firstChar = 0x20;
    font->lastChar = 0xFF;
    font->invalidChar = 0x20;
    font->ascent = 20;
    font->descent = 4;
    font->width = FONT_CELL;
    font->leading = 26;
    font->cellWidth = FONT_CELL;
    font->cellHeight = FONT_CELL;
    font->sheetSize = sheetBytes;
    font->sheetFormat = GX_TF_I4;
    font->sheetNumCol = FONT_SHEET / FONT_CELL;
    font->sheetNumRow = FONT_SHEET / FONT_CELL;
    font->sheetWidth = FONT_SHEET;
    font->sheetHeight = FONT_SHEET;
    font->sheetImageOfs = 0x40; // 32-byte aligned after the header
    font->sheetFullSize = sheetBytes;
    sFont = font;
    return TRUE;
}
static const char* FontNextChar(const char* str) {
    return *str != '\0' ? str + 1 : str;
}
const char* OSGetFontTexture(const char* str, void** texOut, u32* xOut, u32* yOut, u32* widthOut) {
    *texOut = sFont != NULL ? reinterpret_cast<u8*>(sFont) + sFont->sheetImageOfs : NULL;
    *xOut = 0;
    *yOut = 0;
    *widthOut = FONT_CELL / 2;
    return FontNextChar(str);
}
const char* OSGetFontWidth(const char* str, u32* widthOut) {
    *widthOut = FONT_CELL / 2;
    return FontNextChar(str);
}

s32 VFIsAvailable() {
    return 0xB001; // VF_ERROR_B001: no virtual file system
}
void VFInitEx(void* start, u32 size) {
    (void)start;
    (void)size;
    NotLinked("VFInitEx");
}

// No GameCube controller ports (Fishing stops a GameCube pad's rumble)
void PADControlMotor(s32 chan, u32 command) {
    (void)chan;
    (void)command;
}

s32 WPADReadFaceData(s32 chan, void* dst, u16 size, u32 src, WPADCallback cb) {
    (void)chan;
    (void)dst;
    (void)size;
    (void)src;
    (void)cb;
    return WPAD_ERR_NO_CONTROLLER;
}

static OSStateCallback sResetCallback = NULL;
static OSStateCallback sPowerCallback = NULL;
OSStateCallback OSSetResetCallback(OSStateCallback callback) {
    OSStateCallback old = sResetCallback;
    sResetCallback = callback;
    return old;
}
OSStateCallback OSSetPowerCallback(OSStateCallback callback) {
    OSStateCallback old = sPowerCallback;
    sPowerCallback = callback;
    return old;
}

// There is no system menu to return to and no console to power off: both end
// the program cleanly (host_main.cpp's END line with exit=menu / shutdown, the
// window and the audio device closed; §34)
extern "C" [[noreturn]] void PortBootExit(const char* reason);
void OSReturnToMenu(void) {
    PortBootExit("menu");
}
void OSShutdownSystem(void) {
    PortBootExit("shutdown");
}

/* ---- Placeholders called by their CodeWarrior names ------------------------ */

EGG::Vector2f EGG_Vector2f_operator_minus(const EGG::Vector2f* pLhs, const EGG::Vector2f* pRhs) {
    return EGG::Vector2f(pLhs->x - pRhs->x, pLhs->y - pRhs->y);
}
EGG::Vector2f EGG_Vector2f_operator_plus(const EGG::Vector2f* pLhs, const EGG::Vector2f* pRhs) {
    return EGG::Vector2f(pLhs->x + pRhs->x, pLhs->y + pRhs->y);
}
EGG::Vector3f __mi__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f* pLhs,
                                                       const EGG::Vector3f& rRhs) {
    return EGG::Vector3f(pLhs->x - rRhs.x, pLhs->y - rRhs.y, pLhs->z - rRhs.z);
}
EGG::Vector3f __pl__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f* pLhs,
                                                       const EGG::Vector3f& rRhs) {
    return EGG::Vector3f(pLhs->x + rRhs.x, pLhs->y + rRhs.y, pLhs->z + rRhs.z);
}
EGG::Vector3f __ml__Q23EGG8Vector3fCFf(const EGG::Vector3f* pLhs, f32 scale) {
    return EGG::Vector3f(pLhs->x * scale, pLhs->y * scale, pLhs->z * scale);
}
EGG::Vector3f cross__Q23EGG8Vector3fCFRCQ23EGG8Vector3f(const EGG::Vector3f* pLhs,
                                                        const EGG::Vector3f& rRhs) {
    return pLhs->cross(rRhs);
}
EGG::Vector3f __mi__Q23EGG8Vector3fCFv(const EGG::Vector3f* pVec) {
    return EGG::Vector3f(-pVec->x, -pVec->y, -pVec->z);
}
EGG::Vector3f __dv__Q23EGG8Vector3fCFf(const EGG::Vector3f* pLhs, f32 div) {
    return EGG::Vector3f(pLhs->x / div, pLhs->y / div, pLhs->z / div);
}
EGG::Vector2f EGG_Vector2f_operator_mul(const EGG::Vector2f* pLhs, f32 scale) {
    return EGG::Vector2f(pLhs->x * scale, pLhs->y * scale);
}
EGG::Vector3f calcRPY__Q23EGG9Matrix34fCFv(const EGG::Matrix34f* pMtx) {
    return pMtx->calcRPY();
}
EGG::Vector2f getHorizon__Q23EGG10CoreStatusCFv(const EGG::CoreStatus* pStatus) {
    return pStatus->getHorizon();
}
EGG::Vector2f getDpdPos__Q23EGG10CoreStatusCFv(const EGG::CoreStatus* pStatus) {
    return pStatus->getDpdPos();
}
EGG::Vector3f getAccel__Q23EGG14CoreControllerCFv(const EGG::CoreController* pController) {
    return pController->getAccel();
}
s32 getDpdValidFlag__Q23EGG14CoreControllerCFv(const EGG::CoreController* pController) {
    return pController->getDpdValidFlag();
}
void BeginDrawQuad__Q23EGG13ShadowTextureFQ33EGG13ShadowTexture5Blend(int blend) {
    EGG::ShadowTexture::BeginDrawQuad(static_cast<EGG::ShadowTexture::Blend>(blend));
}
void DrawQuad__Q23EGG13ShadowTextureCFRCQ34nw4r4math5MTX34RC8_GXColor(
    const EGG::ShadowTexture* pTexture, const nw4r::math::MTX34& rMtx, const GXColor& rColor) {
    pTexture->DrawQuad(rMtx, rColor);
}
// RPTnkGameManager passes its guide (unused: RPTnkHideExMsg takes no parameter)
class RPTnkExGuide;
void RPTnkHideExMsg__Fv(RPTnkExGuide* pGuide) {
    (void)pGuide;
    RPTnkHideExMsg();
}
void SetBrightnessColor__19RPSysKokeshiManagerFP10RPGrpModel8_GXColor(RPSysKokeshiManager* pMgr,
                                                                      RPGrpModel* pModel,
                                                                      const GXColor* pColor) {
    pMgr->SetBrightnessColor(pModel, *pColor);
}

} // extern "C"

// RPBilTable defines GetPocketPos by its explicit ABI entry (RPBilTable.h); the cameras call
// the member
EGG::Vector3f RPBilTable::GetPocketPos(s32 pocket) const {
    nw4r::math::VEC3 pos;
    GetPocketPos__10RPBilTableCFl(&pos, const_cast<RPBilTable*>(this), pocket);
    return EGG::Vector3f(pos.x, pos.y, pos.z);
}
