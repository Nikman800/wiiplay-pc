/**
 * HOME Menu harness (homebuttonMiniLib, port/docs/HOST_BUILD.md §16).
 *
 * Loads the two archives that hold the HOME Menu files the way
 * RPSysResourceManager does (disc_walk.h: EGG::DvdFile, EGG::DvdRipper,
 * EGG::Decomp, EGG::Archive), fills an HBMDataInfo the way
 * RPSysHomeMenuMgr::RPSysHomeMenuMgr and initHomeButtonInfo do (the same
 * archive paths, sizes and flags), and runs the real HBMCreate
 * (src/homebuttonMiniLib) once per layout archive on the disc:
 *
 *   config.txt         HomeButton::set_config (layout and animation names,
 *                      button count, dialog flags)
 *   home_nosave.csv    HomeButton::set_text (UTF-16, big-endian on the disc;
 *                      converted in place on the host, by its byte-order mark)
 *   homeBtn_*.arc      the HOME Menu layout, its animations and groups, built
 *                      by nw4r::lyt; init_msg puts the region's messages in
 *                      the text boxes
 *   SpeakerSe.arc      RemoteSpk (ARC); every .bwav is played through
 *                      RemoteSpk::Play and the real UpdateSpeaker, which
 *                      scales the big-endian PCM into the encoder's buffer
 *
 * Each language gets a new instance (the library has no HBMDelete), so the
 * message buffer is seen in native order the second and third time (HBMCreate
 * must leave it alone).
 * HomeButtonSe.brsar is the snd harness's, homeBtnIcon.bti the BTI harness's.
 *
 * Lines (tab separated):
 *   HC  layout archive, layout name, animation prefix, button count,
 *       animation count, dialog flags
 *   HT  layout archive, language, message index, text (hex UTF-16 units,
 *       up to the closing quote), as set_text found it
 *   HB  layout archive, region, text pane name, the TextBox's string (hex)
 *   HS  speaker sound: archive entry name, volume, samples sent, hash of the
 *       16-bit samples handed to WENCGetEncodeData (big-endian bytes)
 *
 * tests/test_hbm.py builds the same lines from the bytes.
 */
#include <homebuttonMiniLib.h>

#include <nw4r/lyt.h>
#include <nw4r/snd.h>
#include <nw4r/ut.h>

#include <revolution/ARC.h>
#include <revolution/SC.h>

#include <port/arena.h>
#include <port/be.h>

#include "disc_walk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/******************************************************************************
 * Read access to HomeButton's private members (the result, not the code under
 * test): a pointer to member formed in an explicit instantiation, which the
 * language exempts from access checking.
 ******************************************************************************/
template <typename Tag, typename Tag::type M> struct PrivateMember {
    friend typename Tag::type Get(Tag) {
        return M;
    }
};

#define HBM_PRIVATE(name, type_)                                                           \
    struct Tag_##name {                                                                    \
        using type = type_;                                                                \
        friend type Get(Tag_##name);                                                       \
    };                                                                                     \
    template struct PrivateMember<Tag_##name, &homebutton::HomeButton::name>;

HBM_PRIVATE(mpLayout, nw4r::lyt::Layout* homebutton::HomeButton::*)
HBM_PRIVATE(mpText, wchar_t* (homebutton::HomeButton::*)[7][6])
HBM_PRIVATE(mpLayoutName, char* homebutton::HomeButton::*)
HBM_PRIVATE(mpAnmName, char* homebutton::HomeButton::*)
HBM_PRIVATE(mButtonNum, int homebutton::HomeButton::*)
HBM_PRIVATE(mAnmNum, int homebutton::HomeButton::*)
HBM_PRIVATE(mDialogFlag, int (homebutton::HomeButton::*)[4])
HBM_PRIVATE(scFuncTextPaneName, const char* (*)[homebutton::res::eFuncTextPane_Max])

// RemoteSpk::UpdateSpeaker is private; RemoteSpk::Start hands it to
// OSSetPeriodicAlarm (tests/hbm_host.cpp), which records it here.
extern "C" OSAlarmHandler gHbmHostPeriodicHandler;
// tests/hbm_host.cpp: every WENCGetEncodeData input.
extern std::vector<s16> gHbmHostEncoded;

namespace {

int sCreates = 0, sTexts = 0, sBoxes = 0, sSounds = 0;

void Error(const std::string& what, const char* pMsg) {
    std::fprintf(stderr, "ERROR: %s: %s\n", what.c_str(), pMsg);
    disc_walk::Errors()++;
}

u32 Fnv(const void* p, u32 size, u32 h = 0x811C9DC5) {
    const u8* b = static_cast<const u8*>(p);
    for (u32 i = 0; i < size; i++) {
        h = (h ^ b[i]) * 0x01000193;
    }
    return h;
}

std::string Hex16(const wchar_t* p, int len) {
    std::string s;
    char buf[8];
    for (int i = 0; i < len; i++) {
        std::snprintf(buf, sizeof(buf), "%04x", static_cast<u16>(p[i]));
        s += buf;
    }
    return s;
}

// RPSysResourceManager: load, expand and mount one archive.
struct Mounted {
    u8* pData;
    EGG::Archive* pArchive;
};

Mounted Mount(const char* pPath) {
    Mounted m = {NULL, NULL};
    s32 bufSize = 0, expandSize = 0;
    u32 trueSize = 0;
    u8* pFile = disc_walk::LoadFromDVD(pPath, &bufSize, &trueSize);
    if (pFile == NULL) {
        Error(pPath, "cannot load");
        return m;
    }
    m.pData = disc_walk::ExpandCompressed(pFile, static_cast<s32>(trueSize), &expandSize);
    delete[] pFile;
    m.pArchive = EGG::Archive::mount(m.pData, NULL, 4);
    if (m.pArchive == NULL) {
        Error(pPath, "mount failed");
    }
    return m;
}

void* GetFile(const Mounted& rArc, const char* pName) {
    EGG::Archive::FileInfo info;
    void* p = rArc.pArchive->getFile(pName, &info);
    if (p == NULL) {
        Error(pName, "not in the archive");
    }
    return p;
}

void TestSpeaker(const std::string& name, void* pSpkSeBuf) {
    homebutton::RemoteSpk* pSpk = homebutton::RemoteSpk::GetInstance();
    if (pSpk == NULL || gHbmHostPeriodicHandler != NULL) {
        Error(name, "no RemoteSpk, or its alarm is already set");
        return;
    }
    pSpk->Start();
    if (gHbmHostPeriodicHandler == NULL) {
        Error(name, "RemoteSpk::Start set no alarm");
        return;
    }

    ARCHandle handle;
    ARCDir dir;
    ARCDirEntry entry;
    ARCInitHandle(pSpkSeBuf, &handle);
    ARCOpenDir(&handle, "/", &dir);
    const s8 vol = 7;
    while (ARCReadDir(&dir, &entry)) {
        if (entry.isDir) {
            continue;
        }
        gHbmHostEncoded.clear();
        pSpk->Play(0, entry.entryNum, vol);
        for (int frame = 0; pSpk->isPlaying(0) && frame < 100000; frame++) {
            gHbmHostPeriodicHandler(NULL, NULL);
        }
        u32 h = 0x811C9DC5;
        for (s16 v : gHbmHostEncoded) {
            u8 be[2] = {static_cast<u8>(static_cast<u16>(v) >> 8), static_cast<u8>(v)};
            h = Fnv(be, 2, h);
        }
        std::printf("HS\t%s\t%d\t%zu\t%08x\n", entry.name, vol, gHbmHostEncoded.size(), h);
        sSounds++;
    }
    ARCCloseDir(&dir);
    pSpk->Stop();
    gHbmHostPeriodicHandler = NULL;
}

void TestLanguage(const Mounted& rCommon, const Mounted& rLocal, const char* pArc, int region,
                  bool speaker) {
    HBMDataInfo info;
    std::memset(&info, 0, sizeof(info));

    // RPSysHomeMenuMgr::RPSysHomeMenuMgr
    const u32 workSize = 512 * 1024; // HBM_WORK_SIZE
    info.mem = PortArenaAlloc(workSize, 32);
    std::memset(info.mem, 0, workSize);
    info.sound_callback = NULL;
    info.backFlag = FALSE;
    info.cursor = FALSE;
    info.configBufSize = 0x23; // HBM_CONFIG_SIZE
    info.memSize = workSize;
    info.frameDelta = 60.0f / 60.0f;
    info.pAllocator = NULL;
    info.adjust.x = 1.0f;
    info.adjust.y = 1.0f;

    // RPSysHomeMenuMgr::initHomeButtonInfo
    info.region = region;
    info.layoutBuf = GetFile(rLocal, (std::string("/HomeMenu/") + pArc).c_str());
    info.spkSeBuf = GetFile(rCommon, "/HomeMenu/SpeakerSe.arc");
    info.msgBuf = GetFile(rCommon, "/HomeMenu/home_nosave.csv");
    info.configBuf = GetFile(rCommon, "/HomeMenu/config.txt");
    if (info.layoutBuf == NULL || info.spkSeBuf == NULL || info.msgBuf == NULL ||
        info.configBuf == NULL) {
        return;
    }

    HBMCreate(&info);
    sCreates++;
    homebutton::HomeButton* pHB = homebutton::HomeButton::getInstance();

    const int* pFlags = pHB->*Get(Tag_mDialogFlag());
    std::printf("HC\t%s\t%s\t%s\t%d\t%d\t%d,%d,%d,%d\n", pArc, pHB->*Get(Tag_mpLayoutName()),
                pHB->*Get(Tag_mpAnmName()), pHB->*Get(Tag_mButtonNum()),
                pHB->*Get(Tag_mAnmNum()), pFlags[0], pFlags[1], pFlags[2], pFlags[3]);

    wchar_t* (&rText)[7][6] = pHB->*Get(Tag_mpText());
    for (int lang = 0; lang < 7; lang++) {
        for (int msg = 0; msg < 6; msg++) {
            const wchar_t* p = rText[lang][msg];
            if (p == NULL) {
                continue;
            }
            int len = 0;
            while (p[len] != L'\"' && p[len] != L'\0') {
                len++;
            }
            std::printf("HT\t%s\t%d\t%d\t%s\n", pArc, lang, msg, Hex16(p, len).c_str());
            sTexts++;
        }
    }

    nw4r::lyt::Layout* pLayout = pHB->*Get(Tag_mpLayout());
    const char* const* pPaneNames = *Get(Tag_scFuncTextPaneName());
    for (int i = 0; i < homebutton::res::eFuncTextPane_Max; i++) {
        nw4r::lyt::TextBox* pBox = nw4r::ut::DynamicCast<nw4r::lyt::TextBox*>(
            pLayout->GetRootPane()->FindPaneByName(pPaneNames[i], true));
        if (pBox == NULL || pBox->GetString() == NULL) {
            Error(pArc, "text pane missing");
            continue;
        }
        const wchar_t* p = pBox->GetString();
        std::printf("HB\t%s\t%d\t%s\t%s\n", pArc, region, pPaneNames[i],
                    Hex16(p, static_cast<int>(std::wcslen(p))).c_str());
        sBoxes++;
    }

    if (speaker) {
        TestSpeaker(pArc, info.spkSeBuf);
    }

    // This version of the library has no HBMDelete (and ~HomeButton is not in
    // the game); the game creates the HOME Menu once. The next language
    // creates a new instance in fresh work memory; this one is left as is.
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir>\n", argv[0]);
        return 2;
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    // RPSysResourceManager's static common and local archives (USA disc).
    Mounted common = Mount("/Common/PartyStatic/common.carc");
    Mounted local = Mount("/US/PartyStatic/local.carc");
    if (common.pArchive == NULL || local.pArchive == NULL) {
        return 1;
    }

    // initHomeButtonInfo's archive and region per language (the USA disc has
    // ENG, FRA and SPA).
    struct Lang {
        const char* pArc;
        int region;
    };
    const Lang langs[] = {{"homeBtn_ENG.arc", SC_LANG_EN}, {"homeBtn_FRA.arc", SC_LANG_FR},
                          {"homeBtn_GER.arc", SC_LANG_DE}, {"homeBtn_ITA.arc", SC_LANG_IT},
                          {"homeBtn_SPA.arc", SC_LANG_SP}, {"homeBtn_NED.arc", SC_LANG_NL},
                          {"homeBtn.arc", SC_LANG_JP}};
    std::vector<std::string> present;
    ARCHandle handle;
    ARCDir dir;
    ARCDirEntry entry;
    if (ARCInitHandle(local.pData, &handle) && ARCOpenDir(&handle, "/HomeMenu", &dir)) {
        while (ARCReadDir(&dir, &entry)) {
            present.push_back(entry.name);
        }
        ARCCloseDir(&dir);
    }
    bool speaker = true;
    for (const Lang& rLang : langs) {
        if (std::find(present.begin(), present.end(), rLang.pArc) == present.end()) {
            continue;
        }
        TestLanguage(common, local, rLang.pArc, rLang.region, speaker);
        speaker = false;
    }

    std::fprintf(stderr,
                 "hbm_harness: %d HBMCreate, %d messages, %d text boxes, %d speaker sounds; "
                 "%d errors (pointer size %zu)\n",
                 sCreates, sTexts, sBoxes, sSounds, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 && sCreates > 0 ? 0 : 1;
}
