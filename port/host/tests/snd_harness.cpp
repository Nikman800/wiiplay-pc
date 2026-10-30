/**
 * Sound archive harness (POINTERS.md step 7, port/docs/HOST_BUILD.md §13).
 *
 * Opens every .brsar on the disc the way the game does and prints what the
 * real nw4r::snd code reads from it:
 *
 * - Disc-level archives (Sound/<Scene>/<name>.brsar): EGG::ArcPlayer::openArchive
 *   with SARC_STORAGE_DVD, the call RPSndAudioMgr::openArchiveRP makes for the
 *   default storage (RPSysProjectLocal: EStorage_DVDStream). That runs
 *   DvdSoundArchive::Open, LoadHeader, SoundArchivePlayer::GetRequiredMemSize /
 *   Setup and LoadLabelStringData.
 * - HomeButtonSe.brsar (inside Common/PartyStatic/common.carc): the buffer
 *   from the mounted archive goes to EGG::ArcPlayer::setupMemoryArchive, as
 *   RPSysHomeMenuMgr → RPSndHomeMenuArcMgr::setupArchive does
 *   (MemorySoundArchive::Setup, SoundArchivePlayer::Setup).
 *
 * Then, for each archive, through the real readers: every sound, bank, player,
 * group (with items), file (with positions) and the player info; names from the
 * SYMB patricia trees, cross-checked with GetSoundLabelString and the
 * ConvertLabelStringTo*Id lookups. Every group is loaded with
 * ArcPlayer::loadGroup into a SoundHeap (SoundArchiveLoader through the
 * archive's file streams), and every file is located with
 * SoundArchivePlayer::detail_GetFileAddress / detail_GetFileWaveDataAddress
 * and parsed: RSEQ (SeqFileReader), RBNK (BankFileReader::ReadInstInfo for
 * every program, key and velocity, ReadWaveParam for every wave), RWSD
 * (WsdFileReader::ReadWaveSoundInfo / ReadWaveSoundNoteInfo / ReadWaveParam),
 * and the head of each file's wave data (ut::IsValidBinaryFile says whether it
 * is an RWAR; in these v1.1 archives it never is: raw samples).
 *
 * tests/test_sound.py rebuilds every line from the file bytes with
 * port/assets/tools/fmt_sound.py and its own reading of the format.
 */

#include "disc_walk.h"

#include <egg/audio.h>
#include <nw4r/snd.h>
#include <nw4r/ut.h>

#include <port/arena.h>
#include <port/be.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace nw4r;
using namespace nw4r::snd;
using namespace nw4r::snd::detail;

namespace {

int sArchives = 0;
int sSounds = 0, sBanks = 0, sPlayers = 0, sGroups = 0, sFiles = 0;
int sLoadedGroups = 0, sSeq = 0, sBnk = 0, sWsd = 0, sWar = 0;
int sInstLines = 0, sWaveParams = 0, sNotes = 0, sWaveAreas = 0;

void Fail(const char* pArc, const char* pMsg, long a = 0, long b = 0) {
    std::fprintf(stderr, "ERROR: %s: %s (%ld, %ld)\n", pArc, pMsg, a, b);
    disc_walk::Errors()++;
}

const char* Str(const char* p) {
    return p != NULL ? p : "-";
}

std::string Fmt(f32 f) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", f);
    return buf;
}

/** Walks one SYMB patricia tree and collects (id -> name) from its leaves. */
void CollectTree(const SoundArchiveFileReader& rReader, const void* pStringBase,
                 u32 treeOffset, std::map<u32, std::string>* pOut) {
    if (treeOffset == 0) {
        return;
    }
    const SoundArchiveFile::StringTree* pTree =
        static_cast<const SoundArchiveFile::StringTree*>(
            ut::AddOffsetToPtr(pStringBase, treeOffset));
    for (u32 i = 0; i < pTree->nodeTable.count; i++) {
        const SoundArchiveFile::StringTreeNode& rNode = pTree->nodeTable.items[i];
        if (!(rNode.flags & 1)) {
            continue;
        }
        const char* pName = rReader.GetString(rNode.strIdx);
        (*pOut)[rNode.id] = pName != NULL ? pName : "";
    }
}

struct Names {
    std::map<u32, std::string> sound, player, group, bank;
};

/**
 * A SoundArchiveFileReader of the harness's own over the SYMB block (the
 * archive's reader keeps its tree pointers private): GetString for the leaves.
 */
void ReadNames(const u8* pFile, Names* pNames, SoundArchiveFileReader* pReader,
               const u8** ppSymb) {
    pReader->Init(pFile);
    const u8* pSymb = pFile + pReader->GetLabelStringChunkOffset();
    pReader->SetStringChunk(pSymb, pReader->GetLabelStringChunkSize());
    pReader->SetInfoChunk(pFile + pReader->GetInfoChunkOffset(),
                          pReader->GetInfoChunkSize());
    const SoundArchiveFile::SymbolBlock* pBlock =
        reinterpret_cast<const SoundArchiveFile::SymbolBlock*>(pSymb);
    const void* pBase = &pBlock->stringBlock;
    const SoundArchiveFile::StringChunk& rChunk = pBlock->stringBlock.stringChunk;
    CollectTree(*pReader, pBase, rChunk.soundTreeOffset, &pNames->sound);
    CollectTree(*pReader, pBase, rChunk.playerTreeOffset, &pNames->player);
    CollectTree(*pReader, pBase, rChunk.groupTreeOffset, &pNames->group);
    CollectTree(*pReader, pBase, rChunk.bankTreeOffset, &pNames->bank);
    *ppSymb = pSymb;
}

const char* NameOf(const std::map<u32, std::string>& rMap, u32 id) {
    std::map<u32, std::string>::const_iterator it = rMap.find(id);
    return it == rMap.end() ? "-" : it->second.c_str();
}

/** The bank's stringId through the INFO structs (no SoundArchive getter). */
u32 BankStringId(const u8* pFile, const SoundArchiveFileReader& rReader, u32 id) {
    const SoundArchiveFile::InfoBlock* pBlock =
        reinterpret_cast<const SoundArchiveFile::InfoBlock*>(
            pFile + rReader.GetInfoChunkOffset());
    const SoundArchiveFile::Info* pInfo = &pBlock->info;
    const SoundArchiveFile::BankTable* pTable =
        Util::GetDataRefAddress0(pInfo->bankTableRef, pInfo);
    const SoundArchiveFile::BankInfo* pBank =
        Util::GetDataRefAddress0(pTable->items[id], pInfo);
    return pBank->stringId;
}

void PrintWaveParam(const char* pArc, const char* pTag, u32 fileId, int a, int b,
                    const WaveData& rData, const void* pWaveAddr) {
    std::printf("%s %s %u %d %d fmt=%u loop=%u ch=%u rate=%u ls=%u le=%u", pTag, pArc,
                fileId, a, b, rData.sampleFormat, rData.loopFlag, rData.numChannels,
                rData.sampleRate, rData.loopStart, rData.loopEnd);
    for (int c = 0; c < rData.numChannels; c++) {
        const ChannelParam& rP = rData.channelParam[c];
        long off = pWaveAddr != NULL
                       ? static_cast<long>(static_cast<const u8*>(rP.dataAddr) -
                                           static_cast<const u8*>(pWaveAddr))
                       : -1;
        std::printf(" [%ld %u %u %u %u", off, rP.volumeFrontLeft, rP.volumeFrontRight,
                    rP.volumeRearLeft, rP.volumeRearRight);
        if (rData.sampleFormat == WaveFile::FORMAT_ADPCM) {
            // ReadWaveParam copies the file's (big-endian) ADPCM block into
            // the runtime ChannelParam in host order (HOST_BUILD.md §21).
            const AdpcmParam& rA = rP.adpcmInfo.param;
            std::printf(" c0=%d gain=%u ps=%u", static_cast<s16>(rA.coef[0]), rA.gain,
                        rA.pred_scale);
        }
        std::printf("]");
    }
    std::printf("\n");
    sWaveParams++;
}

void ParseBank(const char* pArc, u32 fileId, const void* pFile, const void* pWave) {
    BankFileReader reader(pFile);
    const BankFile::Header* pHeader = static_cast<const BankFile::Header*>(pFile);
    const BankFile::DataBlock* pData = static_cast<const BankFile::DataBlock*>(
        ut::AddOffsetToPtr(pHeader, pHeader->dataBlockOffset));
    const BankFile::WaveBlock* pWaveBlock = static_cast<const BankFile::WaveBlock*>(
        ut::AddOffsetToPtr(pHeader, pHeader->waveBlockOffset));
    u32 insts = pData->instTable.count;
    u32 waves = pWaveBlock->waveInfoTable.count;
    std::printf("K %s %u ver=%04x insts=%u waves=%u\n", pArc, fileId,
                (unsigned)pHeader->fileHeader.version, insts, waves);
    sBnk++;

    // Every program x key; velocities collapsed into runs with the same result.
    for (int prg = 0; prg < static_cast<int>(insts); prg++) {
        for (int key = 0; key < 128; key++) {
            std::string prev;
            int start = 0;
            for (int vel = 0; vel <= 128; vel++) {
                std::string cur;
                if (vel < 128) {
                    InstInfo info;
                    std::memset(&info, 0, sizeof(info));
                    if (reader.ReadInstInfo(&info, prg, key, vel)) {
                        char buf[160];
                        std::snprintf(buf, sizeof(buf), "w=%d adsr=%u,%u,%u,%u key=%u pan=%u vol=%u tune=%s",
                                      info.waveIndex, info.attack, info.decay, info.sustain,
                                      info.release, info.originalKey, info.pan, info.volume,
                                      Fmt(info.tune).c_str());
                        cur = buf;
                    } else {
                        cur = "none";
                    }
                }
                if (vel == 0) {
                    prev = cur;
                } else if (vel == 128 || cur != prev) {
                    if (prev != "none") {
                        std::printf("KI %s %u %d %d %d-%d %s\n", pArc, fileId, prg, key, start,
                                    vel - 1, prev.c_str());
                        sInstLines++;
                    }
                    prev = cur;
                    start = vel;
                }
            }
        }
    }

    for (u32 w = 0; w < waves; w++) {
        WaveData data;
        std::memset(&data, 0, sizeof(data));
        if (!reader.ReadWaveParam(&data, w, pWave)) {
            Fail(pArc, "BankFileReader::ReadWaveParam failed", fileId, w);
            continue;
        }
        PrintWaveParam(pArc, "KW", fileId, w, 0, data, pWave);
    }
    // One past the end must fail.
    WaveData data;
    if (reader.ReadWaveParam(&data, waves, pWave)) {
        Fail(pArc, "ReadWaveParam past the end succeeded", fileId, waves);
    }
}

void ParseWsd(const char* pArc, u32 fileId, const void* pFile, const void* pWave) {
    WsdFileReader reader(pFile);
    const WsdFile::Header* pHeader = static_cast<const WsdFile::Header*>(pFile);
    const WsdFile::DataBlock* pData = static_cast<const WsdFile::DataBlock*>(
        ut::AddOffsetToPtr(pHeader, pHeader->dataBlockOffset));
    const WsdFile::WaveBlock* pWaveBlock = static_cast<const WsdFile::WaveBlock*>(
        ut::AddOffsetToPtr(pHeader, pHeader->waveBlockOffset));
    u32 wsds = pData->wsdCount;
    u32 waves = pWaveBlock->waveCount;
    std::printf("W %s %u ver=%04x wsds=%u waves=%u\n", pArc, fileId,
                (unsigned)pHeader->fileHeader.version, wsds, waves);
    sWsd++;

    for (u32 i = 0; i < wsds; i++) {
        WaveSoundInfo info;
        reader.ReadWaveSoundInfo(&info, i);
        // Note count through the file structs (the reader has no getter).
        const WsdFile::Wsd* pWsd = Util::GetDataRefAddress0(pData->refWsd[i], &pData->wsdCount);
        const WsdFile::NoteTable* pNotes =
            Util::GetDataRefAddress0(pWsd->refNoteTable, &pData->wsdCount);
        std::printf("WS %s %u %u pitch=%s pan=%u span=%u fx=%u,%u,%u main=%u notes=%u\n", pArc,
                    fileId, i, Fmt(info.pitch).c_str(), info.pan, info.surroundPan, info.fxSendA,
                    info.fxSendB, info.fxSendC, info.mainSend, (unsigned)pNotes->count);
        for (u32 n = 0; n < pNotes->count; n++) {
            WaveSoundNoteInfo note;
            reader.ReadWaveSoundNoteInfo(&note, i, n);
            std::printf("WN %s %u %u %u w=%d adsr=%u,%u,%u,%u key=%u vol=%u pan=%u span=%u "
                        "pitch=%s\n",
                        pArc, fileId, i, n, note.waveIndex, note.attack, note.decay, note.sustain,
                        note.release, note.originalKey, note.volume, note.pan, note.surroundPan,
                        Fmt(note.pitch).c_str());
            sNotes++;
        }
    }
    for (u32 w = 0; w < waves; w++) {
        WaveData data;
        std::memset(&data, 0, sizeof(data));
        if (!reader.ReadWaveParam(w, &data, pWave)) {
            Fail(pArc, "WsdFileReader::ReadWaveParam failed", fileId, w);
            continue;
        }
        PrintWaveParam(pArc, "WW", fileId, w, 0, data, pWave);
    }
}

void ParseFile(const char* pArc, SoundArchive& rSA, SoundArchivePlayer& rPlayer, u32 fileId) {
    const void* pFile = rPlayer.detail_GetFileAddress(fileId);
    const void* pWave = rPlayer.detail_GetFileWaveDataAddress(fileId);
    if (pFile == NULL) {
        std::printf("X %s %u none\n", pArc, fileId);
        return;
    }
    const ut::BinaryFileHeader* pHeader = static_cast<const ut::BinaryFileHeader*>(pFile);
    u32 sig = pHeader->signature;
    char sigText[5] = {char(sig >> 24), char(sig >> 16), char(sig >> 8), char(sig), 0};
    std::printf("X %s %u %s size=%u wave=%s\n", pArc, fileId, sigText,
                (unsigned)pHeader->fileSize, pWave != NULL ? "yes" : "no");

    if (sig == SeqFileReader::SIGNATURE) {
        SeqFileReader reader(pFile);
        const SeqFile::Header* pSeq = static_cast<const SeqFile::Header*>(pFile);
        std::printf("Q %s %u ver=%04x base=%ld\n", pArc, fileId,
                    (unsigned)pSeq->fileHeader.version,
                    static_cast<long>(static_cast<const u8*>(reader.GetBaseAddress()) -
                                      static_cast<const u8*>(pFile)));
        sSeq++;
    } else if (sig == BankFileReader::SIGNATURE) {
        ParseBank(pArc, fileId, pFile, pWave);
    } else if (sig == WsdFileReader::SIGNATURE) {
        ParseWsd(pArc, fileId, pFile, pWave);
    } else {
        Fail(pArc, "unknown file signature", fileId, sig);
    }

    // The RWAR of a file with wave data (for the others the address is just
    // the group's wave area plus the item's offset).
    SoundArchive::FileInfo fi;
    if (pWave != NULL && rSA.detail_ReadFileInfo(fileId, &fi) && fi.waveDataFileSize != 0) {
        const ut::BinaryFileHeader* pW = static_cast<const ut::BinaryFileHeader*>(pWave);
        bool ok = ut::IsValidBinaryFile(pW, FOURCC('R', 'W', 'A', 'R'), NW4R_VERSION(1, 0), 1);
        // In these v1.1 archives the wave area holds the raw sample data the
        // WaveInfo offsets point into, not an RWAR container: say which.
        const u8* pB = static_cast<const u8*>(pWave);
        std::printf("R %s %u rwar=%d head=%02x%02x%02x%02x%02x%02x%02x%02x\n", pArc, fileId,
                    ok ? 1 : 0, pB[0], pB[1], pB[2], pB[3], pB[4], pB[5], pB[6], pB[7]);
        sWar += ok ? 1 : 0;
        sWaveAreas++;
    }
}

/** Everything after the archive is open: info, names, groups, files. */
void Report(const char* pArc, EGG::ArcPlayer& rArc, SoundHeap& rHeap, const u8* pFileImage) {
    SoundArchive& rSA = rArc.getSoundArchive();
    SoundArchivePlayer& rPlayer = rArc.getPlayer();
    if (!rPlayer.IsAvailable()) {
        Fail(pArc, "player not available");
        return;
    }
    sArchives++;

    SoundArchiveFileReader nameReader;
    const u8* pSymb = NULL;
    Names names;
    ReadNames(pFileImage, &names, &nameReader, &pSymb);

    SoundArchive::SoundArchivePlayerInfo pi;
    if (!rSA.ReadSoundArchivePlayerInfo(&pi)) {
        Fail(pArc, "ReadSoundArchivePlayerInfo");
    }
    std::printf("A %s players=%u groups=%u seq=%d,%d strm=%d,%d,%d wave=%d,%d\n", pArc,
                rSA.GetPlayerCount(), rSA.GetGroupCount(), pi.seqSoundCount, pi.seqTrackCount,
                pi.strmSoundCount, pi.strmTrackCount, pi.strmChannelCount, pi.waveSoundCount,
                pi.waveTrackCount);

    // Sounds: ReadSoundInfo until it fails (there is no count getter).
    for (u32 id = 0;; id++) {
        SoundArchive::SoundInfo si;
        if (!rSA.ReadSoundInfo(id, &si)) {
            break;
        }
        const char* pLabel = rSA.GetSoundLabelString(id);
        const char* pTree = NameOf(names.sound, id);
        if (std::strcmp(Str(pLabel), pTree) != 0) {
            Fail(pArc, "GetSoundLabelString differs from the SYMB tree", id);
        }
        if (pLabel != NULL && rSA.ConvertLabelStringToSoundId(pLabel) != id) {
            Fail(pArc, "ConvertLabelStringToSoundId", id);
        }
        SoundType type = rSA.GetSoundType(id);
        std::printf("S %s %u %s file=%u player=%u prio=%d vol=%d rf=%d pan=%d,%d type=%d user=%u",
                    pArc, id, Str(pLabel), si.fileId, si.playerId, si.playerPriority, si.volume,
                    si.remoteFilter, si.panMode, si.panCurve, type, rSA.GetSoundUserParam(id));
        if (type == SOUND_TYPE_SEQ) {
            SoundArchive::SeqSoundInfo q;
            if (!rSA.detail_ReadSeqSoundInfo(id, &q)) {
                Fail(pArc, "detail_ReadSeqSoundInfo", id);
            }
            std::printf(" seq=%u,%u,%u,%d,%d", q.dataOffset, q.bankId, q.allocTrack,
                        q.channelPriority, q.releasePriorityFixFlag ? 1 : 0);
        } else if (type == SOUND_TYPE_WAVE) {
            SoundArchive::WaveSoundInfo w;
            if (!rSA.detail_ReadWaveSoundInfo(id, &w)) {
                Fail(pArc, "detail_ReadWaveSoundInfo", id);
            }
            std::printf(" wsd=%d,%d,%d", w.subNo, w.channelPriority,
                        w.releasePriorityFixFlag ? 1 : 0);
        } else if (type == SOUND_TYPE_STRM) {
            SoundArchive::StrmSoundInfo s;
            std::printf(" strm=%d", rSA.detail_ReadStrmSoundInfo(id, &s) ? 1 : 0);
        }
        SoundArchive::Sound3DParam p3;
        if (rSA.detail_ReadSound3DParam(id, &p3)) {
            std::printf(" 3d=%u,%u,%u", p3.flags, p3.decayCurve, p3.decayRatio);
        }
        std::printf("\n");
        sSounds++;
    }

    for (u32 id = 0;; id++) {
        SoundArchive::BankInfo bi;
        if (!rSA.detail_ReadBankInfo(id, &bi)) {
            break;
        }
        const char* pName = nameReader.GetString(BankStringId(pFileImage, nameReader, id));
        if (std::strcmp(Str(pName), NameOf(names.bank, id)) != 0) {
            Fail(pArc, "bank name differs from the SYMB tree", id);
        }
        std::printf("B %s %u %s file=%u\n", pArc, id, Str(pName), bi.fileId);
        sBanks++;
    }

    for (u32 id = 0; id < rSA.GetPlayerCount(); id++) {
        SoundArchive::PlayerInfo info;
        if (!rSA.ReadPlayerInfo(id, &info)) {
            Fail(pArc, "ReadPlayerInfo", id);
            continue;
        }
        const char* pName = NameOf(names.player, id);
        if (rSA.ConvertLabelStringToPlayerId(pName) != id) {
            Fail(pArc, "ConvertLabelStringToPlayerId", id);
        }
        std::printf("P %s %u %s sounds=%d heap=%u\n", pArc, id, pName, info.playableSoundCount,
                    info.heapSize);
        sPlayers++;
    }

    // Groups: all of the INFO table (GetGroupCount() leaves out the last one).
    for (u32 id = 0;; id++) {
        SoundArchive::GroupInfo gi;
        if (!rSA.detail_ReadGroupInfo(id, &gi)) {
            break;
        }
        const char* pName = NameOf(names.group, id);
        if (std::strcmp(pName, "-") != 0 && rSA.ConvertLabelStringToGroupId(pName) != id) {
            Fail(pArc, "ConvertLabelStringToGroupId", id);
        }
        std::printf("G %s %u %s items=%u ext=%s off=%u size=%u woff=%u wsize=%u\n", pArc, id,
                    pName, gi.itemCount, Str(gi.extFilePath), gi.offset, gi.size,
                    gi.waveDataOffset, gi.waveDataSize);
        for (u32 i = 0; i < gi.itemCount; i++) {
            SoundArchive::GroupItemInfo it;
            if (!rSA.detail_ReadGroupItemInfo(id, i, &it)) {
                Fail(pArc, "detail_ReadGroupItemInfo", id, i);
                continue;
            }
            std::printf("GI %s %u %u file=%u off=%u size=%u woff=%u wsize=%u\n", pArc, id, i,
                        it.fileId, it.offset, it.size, it.waveDataOffset, it.waveDataSize);
        }
        sGroups++;
    }

    u32 fileCount = 0;
    for (u32 id = 0;; id++) {
        SoundArchive::FileInfo fi;
        if (!rSA.detail_ReadFileInfo(id, &fi)) {
            break;
        }
        std::printf("F %s %u size=%u wsize=%u ext=%s pos=%u", pArc, id, fi.fileSize,
                    fi.waveDataFileSize, Str(fi.extFilePath), fi.filePosCount);
        for (u32 p = 0; p < fi.filePosCount; p++) {
            SoundArchive::FilePos pos;
            if (rSA.detail_ReadFilePos(id, p, &pos)) {
                std::printf(" %u:%u", (unsigned)pos.groupId, (unsigned)pos.index);
            } else {
                std::printf(" -");
            }
        }
        std::printf("\n");
        fileCount++;
        sFiles++;
    }

    // Load every group the game can name (0 .. GetGroupCount() - 1).
    for (u32 id = 0; id < rSA.GetGroupCount(); id++) {
        SoundArchive::GroupInfo gi;
        rSA.detail_ReadGroupInfo(id, &gi);
        if (gi.size == 0 && gi.waveDataSize == 0) {
            // An empty group: SoundArchiveLoader would open a 0-length group
            // stream, which DvdFileStream / MemoryFileStream take to mean "the
            // whole archive", and read all of it. Not loaded.
            std::printf("L %s %u empty\n", pArc, id);
            continue;
        }
        int level = rHeap.SaveState();
        bool ok = rArc.loadGroup(id, &rHeap, 0);
        std::printf("L %s %u ok=%d\n", pArc, id, ok ? 1 : 0);
        if (!ok) {
            Fail(pArc, "loadGroup", id);
        }
        sLoadedGroups += ok ? 1 : 0;
        (void)level;
    }

    for (u32 id = 0; id < fileCount; id++) {
        ParseFile(pArc, rSA, rPlayer, id);
    }
}

/**
 * EGG::ArcPlayer in zeroed storage. ~ArcPlayer destroys its DvdSoundArchive,
 * whose Close calls DVDClose on mFileInfo even when that archive was never
 * opened (the HOME Menu archive is a MemorySoundArchive). The constructor
 * leaves mFileInfo uninitialised; the host DVDClose needs it zeroed.
 */
struct ArcPlayerBox {
    alignas(EGG::ArcPlayer) u8 mem[sizeof(EGG::ArcPlayer)];
    EGG::ArcPlayer* p;
    explicit ArcPlayerBox(SoundHeap* pHeap) {
        std::memset(mem, 0, sizeof(mem));
        p = new (mem) EGG::ArcPlayer(pHeap);
    }
    ~ArcPlayerBox() {
        p->~ArcPlayer();
    }
};

/** Heap memory for one archive: the M0 arena, as the game's MEM2 heap. */
struct HeapBlock {
    void* p;
    u32 size;
    explicit HeapBlock(u32 n) : size(n) {
        p = PortArenaAlloc(n, 32);
    }
    ~HeapBlock() {
        PortArenaFree(p);
    }
};

void OpenDvd(const std::string& rel) {
    std::string path = "/" + rel;
    // The whole file, for the harness's own name reader and the heap size.
    s32 bufSize = 0;
    u32 trueSize = 0;
    u8* pImage = disc_walk::LoadFromDVD(path.c_str(), &bufSize, &trueSize);
    if (pImage == NULL) {
        Fail(rel.c_str(), "cannot load");
        return;
    }

    HeapBlock block(trueSize + 0x400000);
    SoundHeap heap;
    if (!heap.Create(block.p, block.size)) {
        Fail(rel.c_str(), "SoundHeap::Create");
    }
    {
        ArcPlayerBox box(&heap);
        EGG::ArcPlayer& arc = *box.p;
        if (arc.openArchive(path.c_str(), &heap, EGG::ArcPlayer::SARC_STORAGE_DVD, NULL) ==
            NULL) {
            Fail(rel.c_str(), "openArchive (DVD) failed");
        } else {
            Report(rel.c_str(), arc, heap, pImage);
        }
        arc.closeArchive();
    }
    heap.Destroy();
    delete[] pImage;
}

void Leaf(const std::string& path, u8* pData, u32 size) {
    if (size >= 4 && std::memcmp(pData, "RSAR", 4) == 0 &&
        path.find(".carc/") != std::string::npos) {
        // Inside an archive: the game hands the mounted buffer to
        // setupMemoryArchive, so the archive is opened right here.
        HeapBlock block(size + 0x400000);
        SoundHeap heap;
        heap.Create(block.p, block.size);
        {
            ArcPlayerBox box(&heap);
            EGG::ArcPlayer& arc = *box.p;
            if (arc.setupMemoryArchive(pData, &heap) == NULL) {
                Fail(path.c_str(), "setupMemoryArchive failed");
            } else {
                Report(path.c_str(), arc, heap, pData);
            }
            arc.closeArchive();
        }
        heap.Destroy();
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir>\n", argv[0]);
        return 2;
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    std::vector<std::string> paths;
    disc_walk::ListDisc(argv[1], "", &paths);

    std::vector<std::string> archives;
    for (const std::string& p : paths) {
        if (p.size() > 6 && p.compare(p.size() - 6, 6, ".brsar") == 0) {
            OpenDvd(p);
        } else if (p.size() > 5 && p.compare(p.size() - 5, 5, ".carc") == 0) {
            archives.push_back(p);
        }
    }

    disc_walk::Visitor visitor = {Leaf, NULL};
    disc_walk::WalkDisc(visitor, archives);

    std::fprintf(stderr,
                 "snd_harness: %d archives, %d sounds, %d banks, %d players, %d groups (%d loaded), "
                 "%d files (RSEQ %d, RBNK %d, RWSD %d), %d wave areas (%d RWAR), %d instrument lines, %d wave "
                 "params, %d notes; %d errors (pointer size %zu)\n",
                 sArchives, sSounds, sBanks, sPlayers, sGroups, sLoadedGroups, sFiles, sSeq, sBnk,
                 sWsd, sWaveAreas, sWar, sInstLines, sWaveParams, sNotes, disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
