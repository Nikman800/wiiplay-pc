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
 *             (the "ledger placeholders"): forwarded to the real functions;
 *   not linked    the minigame scenes: each one's vtable is a table of one
 *             function that aborts (the scene creator's switch keeps them).
 */

#include <types.h>

#include <Pack/RPAudio.h>
#include <Pack/RPGraphics.h>
#include <Pack/RPKernel.h>

#include <Pack/RPParty/RPPartyCommon/RPDoll.h>
#include <Pack/RPParty/RPPartyCommon/RPPlayerConfirm.h>
#include <Pack/RPParty/RPPartyCommon/RPSceneInfo.h>

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

void OSReturnToMenu(void) {
    std::printf("boot: OSReturnToMenu\n");
    std::fflush(stdout);
    std::_Exit(0);
}
void OSShutdownSystem(void) {
    std::printf("boot: OSShutdownSystem\n");
    std::fflush(stdout);
    std::_Exit(0);
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
void SetBrightnessColor__19RPSysKokeshiManagerFP10RPGrpModel8_GXColor(RPSysKokeshiManager* pMgr,
                                                                      RPGrpModel* pModel,
                                                                      const GXColor* pColor) {
    pMgr->SetBrightnessColor(pModel, *pColor);
}

} // extern "C"

/* ---- Not linked: the minigame scenes' vtables ------------------------------ */

namespace {

void MinigameSceneNotLinked() {
    NotLinked("a minigame scene");
}

} // namespace

#define PORT_VTABLE_SLOTS 64
#define PORT_SCENE_VTABLE(id, mangled) \
    extern "C" void* boot_vtable_##id[PORT_VTABLE_SLOTS] __asm__(mangled); \
    void* boot_vtable_##id[PORT_VTABLE_SLOTS];
PORT_SCENE_VTABLE(bil, "_ZTV10RPBilScene")
PORT_SCENE_VTABLE(bom, "_ZTV10RPBomScene")
PORT_SCENE_VTABLE(cow, "_ZTV10RPCowScene")
PORT_SCENE_VTABLE(fsh, "_ZTV10RPFshScene")
PORT_SCENE_VTABLE(hky, "_ZTV10RPHkyScene")
PORT_SCENE_VTABLE(pnp, "_ZTV10RPPnpScene")
PORT_SCENE_VTABLE(tnk, "_ZTV10RPTnkScene")
PORT_SCENE_VTABLE(wly, "_ZTV10RPWlyScene")
#undef PORT_SCENE_VTABLE

namespace {

// Offset to top and type info are 0 (no RTTI); every virtual slot aborts.
__attribute__((constructor)) void FillSceneVtables() {
    void** tables[] = {boot_vtable_bil, boot_vtable_bom, boot_vtable_cow,
                       boot_vtable_fsh, boot_vtable_hky,
                       boot_vtable_pnp, boot_vtable_tnk, boot_vtable_wly};
    for (void** t : tables) {
        for (int i = 2; i < PORT_VTABLE_SLOTS; i++) {
            t[i] = reinterpret_cast<void*>(&MinigameSceneNotLinked);
        }
    }
}

} // namespace
