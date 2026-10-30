/**
 * Audio runtime harness (Phase 7, port/docs/HOST_BUILD.md §21).
 *
 * Plays sounds from the disc's sound archives through the real nw4r::snd
 * runtime (SoundSystem, SoundArchivePlayer, SeqPlayer, WsdPlayer, the sound
 * and task threads) on the host AX DSP (src/host_ax.cpp), and renders the
 * mixed 32 kHz stereo output.
 *
 * Set-up, as the game does it, on the platform layer's EGG heaps (§22):
 * PortOSInit and EGG::ConfigurationData::initMemory (the root, system heaps),
 * WPADInit (KPADInit's, §23 backend), then RPSysSystem::initialize's
 * RPSysProjectLocal / RPSndAudioMgr / RPSndSpeakerMgr::CreateInstance on the
 * system heap and RPSysSystem::createEffectWork's lines: the MEM1 root heap
 * current, RPSndAudioMgr::initialize with the 0xB00000 sound heap from it
 * (ExpAudioMgr::initialize: AIInit, AXInit, SoundSystem::InitSoundSystem,
 * the 3D managers; SoundHeapMgr::createSoundHeap; AudioFxMgr::initializeFx
 * with the Party Pack aux heap sizes; the static sound archive). Archives are
 * opened with RPSndAudioMgr::openArchiveRP (default storage: DVD); every
 * group is loaded with ArcPlayer::loadGroup. Sounds start through
 * RPSndAudioMgr::startSound; each game frame runs RPSndAudioMgr::calc.
 *
 * The clock: AX frames of 96 samples (3 ms) run back to back
 * (PortAXRunFrame); the game frame (60 Hz: ExpAudioMgr::calc, which updates
 * the SoundArchivePlayer, then the main thread idles so the other threads run)
 * comes before the first AX frame that starts at or after
 * floor(k * 32000 / 60) samples.
 *
 *   audio_harness --disc DIR --list ARCHIVE
 *       one line per sound: L id type fileId playerId volume subNo/bankId name
 *   audio_harness --disc DIR [--frames N] [--wav PATH] [--tap PATH] ARCHIVE SOUND[@F] ...
 *       starts each SOUND (label or id) at game frame F (default 0), renders N AX
 *       frames, prints the events and a summary; --wav writes the output as a
 *       RIFF WAV, --tap writes every voice of every frame (binary, see WriteTap);
 *       --src-none makes the DSP skip sample-rate conversion (a test switch:
 *       one decoded sample per output sample); --fx sets up the game's default
 *       effects first (RPSndAudioMgr::setDefaultFx).
 *   audio_harness --disc DIR [--frames N] [--wav PATH] [--tap PATH] --strm FILE
 *       plays a BRSTM file from the disc through the real StrmPlayer (no archive
 *       has a stream sound, so the player is driven directly, the way
 *       SoundArchivePlayer::PrepareStrmImpl / StrmSound::Prepare do: a
 *       StrmBufferPool of 0xA000 bytes a channel on the sound heap, a
 *       DvdFileStream, StrmPlayer::Setup / Prepare, Start once the header and
 *       the first blocks are loaded on the task thread). Its I line has file 0
 *       and the stream buffer's address (§24).
 *
 * Output lines:
 *   S id frame ok type name                   a sound started (ok 0/1)
 *   I id fileId waveDataPhys                  the file whose waves the sound plays (its
 *                                             RWSD, or its bank's RBNK) and the file's
 *                                             wave data (M0 physical address: it
 *                                             differs between the 64- and 32-bit builds)
 *   N axframe voice format addr loopFlag loopAddr endAddr ratio coefSelect srcSelect
 *     vol mixerCtrl vL vR                     a voice starting a new note; addresses
 *                                             as fileId+offset (see DspAddr)
 *   E axframe voice                           a voice no longer running
 *   C axframe count                           running voices, when it changes
 *   R frames samples rms peakL peakR hash     the rendered output
 */

#include "disc_walk.h"

#include <Pack/RPAudio.h>
#include <Pack/RPSystem.h>
#include <egg/audio.h>
#include <egg/core.h>
#include <nw4r/snd.h>
#include <nw4r/ut.h>

#include <revolution/AX.h>

#include <port/host.h>
#include <port/input.h>
#include <revolution/SC.h>
#include <revolution/WPAD.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nw4r;
using namespace nw4r::snd;

namespace {

/* The memory part of EGG's start-up (ConfigurationData::initMemory) on a
 * configuration with no video, display or scenes (as input_harness). */
class HarnessConfig : public EGG::ConfigurationData {
public:
    HarnessConfig() : EGG::ConfigurationData(0x600000) {}
    virtual EGG::Video* getVideo() { return NULL; }
    virtual EGG::Heap* getSystemHeap() { return mSystemHeap; }
    virtual EGG::Display* getDisplay() { return NULL; }
    virtual EGG::XfbManager* getXfbMgr() { return NULL; }
    virtual EGG::PerformanceView* getPerfView() { return NULL; }
    virtual EGG::SceneManager* getSceneMgr() { return NULL; }
    virtual EGG::IAudioMgr* getAudioMgr() { return sMgr; }

    RPSndAudioMgr* sMgr = NULL;
};

RPSndAudioMgr* sMgr = NULL;

/** Every voice of every frame, for the tap file and the event lines. */
struct VoiceState {
    bool running;
    u32 addr; // current address the DSP left
};
VoiceState sVoices[AX_VOICE_MAX];
bool sRunningThisFrame[AX_VOICE_MAX];
FILE* sTap = NULL;
int sCount = 0;

u32 Addr(u16 hi, u16 lo) {
    return static_cast<u32>(hi) << 16 | lo;
}

/*
 * Tap record (little-endian host order; the test reads it with struct):
 *   u32 magic 'VTAP', u32 frame, u32 voice, u32 inputCount,
 *   AXPB before (0x140 bytes), AXPB after (0x140 bytes) (list links and ITD
 *   buffer address zeroed),
 *   s16 input[inputCount], s16 src[96], s16 out[96]
 */
void WriteTap(const PortAXVoiceTrace* t) {
    u32 head[4] = {0x50415456u, static_cast<u32>(t->frame), t->index, t->inputCount};
    std::fwrite(head, sizeof(head), 1, sTap);
    // The PB list links and the ITD buffer address are host addresses
    // (__AXVPBInit stores the halves of the PB / ITD pointers): left out, so
    // that taps compare across runs and builds.
    AXPB pb[2];
    std::memcpy(&pb[0], t->pbBefore, sizeof(AXPB));
    std::memcpy(&pb[1], t->pbAfter, sizeof(AXPB));
    for (int i = 0; i < 2; i++) {
        pb[i].nextHi = pb[i].nextLo = pb[i].currHi = pb[i].currLo = 0;
        pb[i].itd.bufferHi = pb[i].itd.bufferLo = 0;
    }
    std::fwrite(pb, sizeof(AXPB), 2, sTap);
    std::fwrite(t->input, sizeof(s16), t->inputCount, sTap);
    std::fwrite(t->src, sizeof(s16), PORT_AX_FRAME_SAMPLES, sTap);
    std::fwrite(t->out, sizeof(s16), PORT_AX_FRAME_SAMPLES, sTap);
}

/** The wave files of the started sounds (I lines), for printing addresses. */
struct WaveFileBase {
    u32 fileId;
    u32 phys;
};
std::vector<WaveFileBase> sBases;

/**
 * A DSP address as "fileId+offset" (offset in the format's DSP units from the
 * start of that file's wave data; "Z+offset" for the zero buffer), so that the
 * event lines do not depend on where the heap put the data (which differs
 * between the 64- and 32-bit builds).
 */
std::string DspAddr(u32 addr, u16 format) {
    u32 byte = format == AX_SAMPLE_FORMAT_DSP_ADPCM ? addr >> 1
               : format == AX_SAMPLE_FORMAT_PCM_S16 ? addr * 2
                                                    : addr;
    std::vector<WaveFileBase> bases = sBases;
    WaveFileBase zero = {0xFFFFFFFFu,
                         OSCachedToPhysical(detail::AxManager::GetInstance().GetZeroBufferAddress())};
    bases.push_back(zero);
    const WaveFileBase* pBest = NULL;
    for (size_t i = 0; i < bases.size(); i++) {
        if (bases[i].phys <= byte && (pBest == NULL || bases[i].phys > pBest->phys)) {
            pBest = &bases[i];
        }
    }
    char buf[48];
    if (pBest == NULL) {
        std::snprintf(buf, sizeof(buf), "?0x%08x", addr);
    } else {
        u32 base = format == AX_SAMPLE_FORMAT_DSP_ADPCM ? pBest->phys * 2
                   : format == AX_SAMPLE_FORMAT_PCM_S16 ? pBest->phys / 2
                                                        : pBest->phys;
        if (pBest->fileId == 0xFFFFFFFFu) {
            std::snprintf(buf, sizeof(buf), "Z+0x%x", addr - base);
        } else {
            std::snprintf(buf, sizeof(buf), "%u+0x%x", pBest->fileId, addr - base);
        }
    }
    return buf;
}

void Tap(const PortAXVoiceTrace* t, void* user) {
    (void)user;
    const AXPB* b = t->pbBefore;
    u32 cur = Addr(b->addr.currentAddressHi, b->addr.currentAddressLo);
    VoiceState& v = sVoices[t->index];
    if (!v.running || v.addr != cur) {
        std::printf("N %llu %u %u %s %u %s %s 0x%08x %u %u 0x%04x 0x%08x 0x%04x 0x%04x\n",
                    t->frame, t->index, b->addr.format, DspAddr(cur, b->addr.format).c_str(),
                    b->addr.loopFlag,
                    DspAddr(Addr(b->addr.loopAddressHi, b->addr.loopAddressLo), b->addr.format)
                        .c_str(),
                    DspAddr(Addr(b->addr.endAddressHi, b->addr.endAddressLo), b->addr.format)
                        .c_str(),
                    Addr(b->src.ratioHi, b->src.ratioLo), b->coefSelect, b->srcSelect,
                    b->ve.currentVolume, b->mixerCtrl, b->mix.vL, b->mix.vR);
    }
    sRunningThisFrame[t->index] = true;
    v.addr = Addr(t->pbAfter->addr.currentAddressHi, t->pbAfter->addr.currentAddressLo);
    if (sTap != NULL) {
        WriteTap(t);
    }
}

void EndFrame(u64 frame) {
    int count = 0;
    for (int i = 0; i < AX_VOICE_MAX; i++) {
        if (sVoices[i].running && !sRunningThisFrame[i]) {
            std::printf("E %llu %d\n", static_cast<unsigned long long>(frame), i);
        }
        sVoices[i].running = sRunningThisFrame[i];
        sRunningThisFrame[i] = false;
        count += sVoices[i].running;
    }
    if (count != sCount) {
        std::printf("C %llu %d\n", static_cast<unsigned long long>(frame), count);
        sCount = count;
    }
}

const char* TypeName(SoundType type) {
    switch (type) {
    case SOUND_TYPE_SEQ:
        return "SEQ";
    case SOUND_TYPE_STRM:
        return "STRM";
    case SOUND_TYPE_WAVE:
        return "WAVE";
    default:
        return "INVALID";
    }
}

void Setup() {
    PortOSInit();
    PortSCLoadConfig(NULL); // a US console's defaults: stereo
    SCInit();
    static HarnessConfig config;
    EGG::BaseSystem::configure(&config);
    config.initMemory();
    WPADInit(); // KPADInit's, through EGG::CoreControllerMgr on the Wii

    // RPSysSystem::initialize
    EGG::Heap* pSystemHeap = config.getSystemHeap();
    RPSysProjectLocal::CreateInstance(pSystemHeap);
    config.sMgr = sMgr = RPSndAudioMgr::CreateInstance(pSystemHeap);
    RPSndSpeakerMgr::CreateInstance(pSystemHeap);

    // RPSysSystem::createEffectWork
    EGG::Heap* pHeap = EGG::BaseSystem::getRootHeapMem1();
    EGG::Heap* pCurrent = pHeap->becomeCurrentHeap();
    EGG::ExpAudioMgr::ExpAudioMgrArg arg;
    arg.pHeap = pHeap;
    arg.soundHeapSize = 0xB00000;
    sMgr->initialize(&arg);
    pCurrent->becomeCurrentHeap();

    // The end of RPSysSystem's start-up: the MEM2 root heap is current. The
    // harness's own containers (operator new is EGG::Heap::alloc) go there,
    // away from the MEM1 heaps the sound system uses.
    EGG::BaseSystem::getRootHeapMem2()->becomeCurrentHeap();
}

bool Open(const char* pArchive) {
    std::string path = std::string("/") + pArchive;
    if (sMgr->openArchiveRP(path.c_str(), NULL, NULL) == NULL) {
        std::fprintf(stderr, "audio_harness: cannot open %s\n", pArchive);
        return false;
    }
    SoundArchive& arc = sMgr->getSoundArchive();
    for (u32 id = 0; id < arc.GetGroupCount(); id++) {
        if (!sMgr->loadGroup(id, &sMgr->getSoundHeap(), 0)) {
            std::fprintf(stderr, "audio_harness: loadGroup %u failed\n", id);
            return false;
        }
    }
    return true;
}

void List() {
    SoundArchive& arc = sMgr->getSoundArchive();
    // ReadSoundInfo until it fails (there is no count getter).
    for (u32 id = 0;; id++) {
        SoundArchive::SoundInfo info;
        if (!arc.ReadSoundInfo(id, &info)) {
            break;
        }
        SoundType type = arc.GetSoundType(id);
        long sub = -1;
        if (type == SOUND_TYPE_WAVE) {
            SoundArchive::WaveSoundInfo w;
            if (arc.detail_ReadWaveSoundInfo(id, &w)) {
                sub = w.subNo;
            }
        } else if (type == SOUND_TYPE_SEQ) {
            SoundArchive::SeqSoundInfo s;
            if (arc.detail_ReadSeqSoundInfo(id, &s)) {
                sub = s.bankId;
            }
        }
        const char* pName = arc.GetSoundLabelString(id);
        std::printf("L %u %s %u %u %d %ld %s\n", id, TypeName(type), info.fileId, info.playerId,
                    info.volume, sub, pName != NULL ? pName : "-");
    }
}

/** The stream (--strm). */
struct Strm {
    const char* pPath;
    detail::StrmBufferPool pool;
    detail::StrmPlayer* pPlayer;
    ut::DvdFileStream* pStream;
    void* pBuffer;
    bool started;
};
Strm sStrm;

/** StrmSound::Prepare's lines (one voice per channel, offset 0 samples). */
bool PrepareStrm(const char* pPath) {
    std::string path = std::string("/") + pPath;
    s32 entry = DVDConvertPathToEntrynum(path.c_str());
    if (entry < 0) {
        std::fprintf(stderr, "audio_harness: no %s on the disc\n", pPath);
        return false;
    }
    const int channels = 2; // StrmPlayer allocates a pool block per channel
    const u32 size = channels * 0xA000; // SoundArchivePlayer::GetRequiredStrmBufferSize
    sStrm.pPath = pPath;
    sStrm.pBuffer = sMgr->getSoundHeap().Alloc(size);
    if (sStrm.pBuffer == NULL) {
        return false;
    }
    sStrm.pool.Setup(sStrm.pBuffer, size, channels);
    sStrm.pStream = new ut::DvdFileStream(entry);
    sStrm.pPlayer = new detail::StrmPlayer();
    if (!sStrm.pPlayer->Setup(&sStrm.pool)) {
        return false;
    }
    return sStrm.pPlayer->Prepare(sStrm.pStream, 1, detail::StrmPlayer::START_OFFSET_TYPE_SAMPLE,
                                  0);
}

/** Starts the stream once it is prepared (S and I lines). */
void UpdateStrm(u32 gameFrame) {
    if (sStrm.pPlayer == NULL || sStrm.started || !sStrm.pPlayer->IsPrepared()) {
        return;
    }
    bool ok = sStrm.pPlayer->Start();
    sStrm.started = true;
    std::printf("S strm %u %d STRM %s\n", gameFrame, ok ? 1 : 0, sStrm.pPath);
    u32 phys = OSCachedToPhysical(sStrm.pBuffer);
    std::printf("I strm 0 0x%08x\n", phys);
    WaveFileBase base = {0, phys};
    sBases.push_back(base);
}

struct Start {
    u32 id;
    u32 frame;
    SoundHandle* pHandle;
};

void WriteWav(const char* pPath, const std::vector<s16>& rOut) {
    FILE* f = std::fopen(pPath, "wb");
    if (f == NULL) {
        std::fprintf(stderr, "audio_harness: cannot write %s\n", pPath);
        std::exit(1);
    }
    u32 dataSize = static_cast<u32>(rOut.size() * sizeof(s16));
    u32 rate = 32000, byteRate = rate * 4;
    u32 riffSize = 36 + dataSize, fmtSize = 16;
    u16 pcm = 1, channels = 2, align = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riffSize, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtSize, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataSize, 4, 1, f);
    std::fwrite(rOut.data(), sizeof(s16), rOut.size(), f);
    std::fclose(f);
}

int Play(const std::vector<Start>& rStarts, u32 axFrames, const char* pWav) {
    SoundArchive& arc = sMgr->getSoundArchive();
    SoundArchivePlayer& player = sMgr->getPlayer();
    PortAXSetVoiceTap(Tap, NULL);

    std::vector<s16> out;
    out.reserve(static_cast<size_t>(axFrames) * PORT_AX_FRAME_SAMPLES * 2);
    s16 frameBuf[PORT_AX_FRAME_SAMPLES * 2];
    u32 gameFrame = 0;
    int errors = 0;

    for (u32 ax = 0; ax < axFrames; ax++) {
        u64 sample = static_cast<u64>(ax) * PORT_AX_FRAME_SAMPLES;
        while (static_cast<u64>(gameFrame) * 32000 / 60 <= sample) {
            for (size_t i = 0; i < rStarts.size(); i++) {
                if (rStarts[i].frame != gameFrame) {
                    continue;
                }
                bool ok = sMgr->startSound(rStarts[i].pHandle, rStarts[i].id);
                const char* pName = arc.GetSoundLabelString(rStarts[i].id);
                std::printf("S %u %u %d %s %s\n", rStarts[i].id, gameFrame, ok ? 1 : 0,
                            TypeName(arc.GetSoundType(rStarts[i].id)), pName != NULL ? pName : "-");
                SoundArchive::SoundInfo info;
                if (arc.ReadSoundInfo(rStarts[i].id, &info)) {
                    // The file whose waves the sound plays: its own (RWSD) or
                    // its bank's (RSEQ).
                    u32 fileId = info.fileId;
                    SoundArchive::SeqSoundInfo seq;
                    SoundArchive::BankInfo bank;
                    if (arc.GetSoundType(rStarts[i].id) == SOUND_TYPE_SEQ &&
                        arc.detail_ReadSeqSoundInfo(rStarts[i].id, &seq) &&
                        arc.detail_ReadBankInfo(seq.bankId, &bank)) {
                        fileId = bank.fileId;
                    }
                    const void* pWave = player.detail_GetFileWaveDataAddress(fileId);
                    u32 phys = pWave != NULL ? OSCachedToPhysical(pWave) : 0;
                    std::printf("I %u %u 0x%08x\n", rStarts[i].id, fileId, phys);
                    WaveFileBase base = {fileId, phys};
                    sBases.push_back(base);
                }
                errors += ok ? 0 : 1;
            }
            UpdateStrm(gameFrame);
            sMgr->calc();
            PortOSIdle();
            gameFrame++;
        }
        PortAXRunFrame(frameBuf);
        EndFrame(ax);
        out.insert(out.end(), frameBuf, frameBuf + PORT_AX_FRAME_SAMPLES * 2);
    }
    PortAXSetVoiceTap(NULL, NULL);

    double sum = 0;
    int peakL = 0, peakR = 0;
    u64 hash = 1469598103934665603ULL; // FNV-1a over the s16 samples (little-endian)
    for (size_t i = 0; i < out.size(); i++) {
        int s = out[i];
        sum += static_cast<double>(s) * s;
        int a = s < 0 ? -s : s;
        if (i & 1) {
            peakR = a > peakR ? a : peakR;
        } else {
            peakL = a > peakL ? a : peakL;
        }
        u16 u = static_cast<u16>(s);
        hash = (hash ^ (u & 0xFF)) * 1099511628211ULL;
        hash = (hash ^ (u >> 8)) * 1099511628211ULL;
    }
    double rms = out.empty() ? 0 : std::sqrt(sum / out.size());
    std::printf("R %u %u %.3f %d %d %016llx\n", axFrames,
                static_cast<u32>(out.size() / 2), rms, peakL, peakR,
                static_cast<unsigned long long>(hash));
    if (pWav != NULL) {
        WriteWav(pWav, out);
    }
    return errors;
}

} // namespace

int main(int argc, char** argv) {
    // First: with the EGG heaps linked, every operator new needs a heap.
    Setup();
    const char* pDisc = NULL;
    const char* pWav = NULL;
    const char* pTap = NULL;
    const char* pArchive = NULL;
    const char* pStrm = NULL;
    bool list = false;
    bool fx = false;
    u32 frames = 1000;
    std::vector<std::string> sounds;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--disc" && i + 1 < argc) {
            pDisc = argv[++i];
        } else if (a == "--wav" && i + 1 < argc) {
            pWav = argv[++i];
        } else if (a == "--tap" && i + 1 < argc) {
            pTap = argv[++i];
        } else if (a == "--fx") {
            fx = true;
        } else if (a == "--src-none") {
            PortAXSetSrcOverride(2);
        } else if (a == "--frames" && i + 1 < argc) {
            frames = static_cast<u32>(std::strtoul(argv[++i], NULL, 0));
        } else if (a == "--strm" && i + 1 < argc) {
            pStrm = argv[++i];
        } else if (a == "--list" && i + 1 < argc) {
            list = true;
            pArchive = argv[++i];
        } else if (pArchive == NULL) {
            pArchive = argv[i];
        } else {
            sounds.push_back(a);
        }
    }
    if (pDisc == NULL || (pArchive == NULL) == (pStrm == NULL)) {
        std::fprintf(stderr, "usage: audio_harness --disc DIR (--list ARCHIVE | [--frames N] "
                             "[--wav PATH] [--tap PATH] [--fx] [--src-none] (ARCHIVE SOUND[@F] ... "
                             "| --strm FILE))\n");
        return 2;
    }
    PortHostSetDiscRoot(pDisc);
    if (pStrm != NULL) {
        if (!PrepareStrm(pStrm)) {
            std::fprintf(stderr, "audio_harness: cannot prepare %s\n", pStrm);
            return 1;
        }
    } else if (!Open(pArchive)) {
        return 1;
    }
    if (list) {
        List();
        return 0;
    }
    if (fx) {
        sMgr->setDefaultFx();
    }

    SoundArchive& arc = sMgr->getSoundArchive();
    std::vector<Start> starts;
    for (size_t i = 0; i < sounds.size(); i++) {
        std::string name = sounds[i];
        u32 frame = 0;
        size_t at = name.rfind('@');
        if (at != std::string::npos) {
            frame = static_cast<u32>(std::strtoul(name.c_str() + at + 1, NULL, 0));
            name = name.substr(0, at);
        }
        char* pEnd = NULL;
        u32 id = static_cast<u32>(std::strtoul(name.c_str(), &pEnd, 0));
        if (pEnd == NULL || *pEnd != '\0' || name.empty()) {
            id = arc.ConvertLabelStringToSoundId(name.c_str());
        }
        SoundArchive::SoundInfo info;
        if (id == SoundArchive::INVALID_ID || !arc.ReadSoundInfo(id, &info)) {
            std::fprintf(stderr, "audio_harness: unknown sound %s\n", name.c_str());
            return 1;
        }
        Start s;
        s.id = id;
        s.frame = frame;
        s.pHandle = new SoundHandle();
        starts.push_back(s);
    }
    if (pTap != NULL) {
        sTap = std::fopen(pTap, "wb");
        if (sTap == NULL) {
            std::fprintf(stderr, "audio_harness: cannot write %s\n", pTap);
            return 1;
        }
    }
    int errors = Play(starts, frames, pWav);
    if (sTap != NULL) {
        std::fclose(sTap);
    }
    std::fflush(stdout);
    // The threads are parked on their queues; exit without tearing down.
    std::_Exit(errors == 0 ? 0 : 1);
}
