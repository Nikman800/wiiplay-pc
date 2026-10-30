/**
 * Loader harness: runs the decompiled disc-loading code on the host.
 *
 * For every file on the extracted disc it does what the game's resource
 * manager does (RPSysResourceManager::LoadFromDVD + ExpandCompressed):
 *
 *   EGG::DvdFile::open -> EGG::DvdRipper::loadToMainRAM   (real decomp code)
 *   EGG::Decomp::checkCompressed / getExpandSize / decode (real decomp code)
 *   EGG::Archive::mount -> Archive::getFile               (real decomp code)
 *   ARCInitHandle / ARCOpenDir / ARCReadDir / ARCFastOpen (real decomp code)
 *
 * and descends into nested archives the same way. Every leaf file is printed
 * as one tab-separated line:
 *
 *   path  size  fnv1a64  nw4r-header
 *
 * and every archive it mounts (top-level or nested) as
 *
 *   #archive  path  node-count
 *
 * where nw4r-header is "-" or, for files that start with an nw4r
 * BinaryFileHeader (byte-order mark FE FF), the header fields read through the
 * big-endian wrapper (nw4r::ut::BinaryFileHeader) and the result of the real
 * nw4r::ut::IsValidBinaryFile. port/host/tests/test_archives.py compares the
 * output with port/assets/tools/wiiarc.py.
 *
 * Usage: arc_harness <extracted-disc-files-dir> [path ...]
 *   With no paths, every file under the directory is processed.
 */

#include <egg/core.h>
#include <nw4r/ut.h>
#include <revolution/ARC.h>

#include <port/host.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

int sErrors = 0;
int sArchives = 0;
int sLeaves = 0;

u64 Fnv1a64(const u8* p, u32 n) {
    u64 h = 0xcbf29ce484222325ULL;
    for (u32 i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/**
 * RPSysResourceManager::LoadFromDVD (src/Pack/RPSystem/RPSysResourceManager.cpp),
 * reproduced with the same EGG calls. Returns the 32-byte-rounded buffer and
 * the true file size.
 */
u8* LoadFromDVD(const char* pPath, s32* pSize, u32* pTrueSize) {
    EGG::DvdFile f;

    if (!f.open(pPath, NULL)) {
        return NULL;
    }

    *pTrueSize = f.getFileSize();
    s32 fileSize = ROUND_UP(f.getFileSize(), 32);
    *pSize = fileSize;

    u8* pBuffer = new (static_cast<EGG::Heap*>(NULL), -32) u8[fileSize];
    if (pBuffer == NULL) {
        return NULL;
    }

    return EGG::DvdRipper::loadToMainRAM(&f, pBuffer, NULL,
                                         EGG::DvdRipper::ALLOC_DIR_HEAD);
}

/**
 * RPSysResourceManager::ExpandCompressed, same EGG calls.
 */
u8* ExpandCompressed(u8* pData, s32 size, s32* pExpandSize) {
    u8* pExpandData = NULL;

    if (EGG::Decomp::checkCompressed(pData) != EGG::Decomp::cCompress_None) {
        *pExpandSize = EGG::Decomp::getExpandSize(pData);
        pExpandData = new (static_cast<EGG::Heap*>(NULL), 32) u8[*pExpandSize];
        EGG::Decomp::decode(pData, pExpandData);
    } else {
        *pExpandSize = size;
        pExpandData = new (static_cast<EGG::Heap*>(NULL), 32) u8[*pExpandSize];
        std::memcpy(pExpandData, pData, *pExpandSize);
    }

    return pExpandData;
}

bool IsU8(const u8* p, u32 size) {
    return size >= 0x20 && p[0] == 0x55 && p[1] == 0xAA && p[2] == 0x38 &&
           p[3] == 0x2D;
}

void EmitLeaf(const std::string& path, const u8* p, u32 size) {
    std::string hdr = "-";

    if (size >= sizeof(nw4r::ut::BinaryFileHeader) && p[4] == 0xFE &&
        p[5] == 0xFF) {
        const nw4r::ut::BinaryFileHeader* pHeader =
            reinterpret_cast<const nw4r::ut::BinaryFileHeader*>(p);

        // Plain member reads: the be_* members convert implicitly.
        u32 sig = pHeader->signature;
        u16 version = pHeader->version;
        u32 fileSize = pHeader->fileSize;
        u16 headerSize = pHeader->headerSize;
        u16 blocks = pHeader->dataBlocks;
        bool valid = nw4r::ut::IsValidBinaryFile(pHeader, sig, version, 1);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "%c%c%c%c v%04x size=%u hdr=%u blocks=%u valid=%d",
                      (char)(sig >> 24), (char)(sig >> 16), (char)(sig >> 8),
                      (char)sig, version, fileSize, headerSize, blocks,
                      valid ? 1 : 0);
        hdr = buf;
    }

    std::printf("%s\t%u\t%016" PRIx64 "\t%s\n", path.c_str(), size,
                Fnv1a64(p, size), hdr.c_str());
    sLeaves++;
}

void Expand(const std::string& path, u8* pData, u32 size);

/** Walk one mounted archive with the ARC directory API. */
void WalkDir(EGG::Archive* pArchive, ARCHandle* pHandle,
             const std::string& prefix, const std::string& dir) {
    ARCDir arcDir;
    if (!ARCOpenDir(pHandle, dir.c_str(), &arcDir)) {
        std::fprintf(stderr, "ERROR: ARCOpenDir(%s) failed in %s\n",
                     dir.c_str(), prefix.c_str());
        sErrors++;
        return;
    }

    ARCDirEntry entry;
    while (ARCReadDir(&arcDir, &entry)) {
        std::string rel = (dir == "/" ? std::string("") : dir.substr(1) + "/") +
                          entry.name;

        if (entry.isDir) {
            WalkDir(pArchive, pHandle, prefix, "/" + rel);
            continue;
        }

        // Game-style access by path through EGG::Archive...
        EGG::Archive::FileInfo info;
        u8* pFile =
            static_cast<u8*>(pArchive->getFile(("/" + rel).c_str(), &info));

        // ...cross-checked against ARC's entry-number API.
        ARCFileInfo fast;
        if (pFile == NULL ||
            !ARCFastOpen(pHandle, entry.entryNum, &fast) ||
            ARCGetStartOffset(&fast) != info.getStartOffset() ||
            ARCGetLength(&fast) != info.getLength() ||
            ARCConvertPathToEntrynum(pHandle, ("/" + rel).c_str()) !=
                static_cast<s32>(entry.entryNum)) {
            std::fprintf(stderr, "ERROR: lookup mismatch for %s/%s\n",
                         prefix.c_str(), rel.c_str());
            sErrors++;
            continue;
        }
        ARCClose(&fast);

        Expand(prefix + "/" + rel, pFile, info.getLength());
    }
    ARCCloseDir(&arcDir);
}

/** Same rule as wiiarc._expand: decompress, then descend if U8. */
void Expand(const std::string& path, u8* pData, u32 size) {
    s32 expandSize = 0;
    u8* pExpand = NULL;

    if (EGG::Decomp::checkCompressed(pData) != EGG::Decomp::cCompress_None) {
        pExpand = ExpandCompressed(pData, static_cast<s32>(size), &expandSize);
        pData = pExpand;
        size = static_cast<u32>(expandSize);
    }

    if (IsU8(pData, size)) {
        sArchives++;
        EGG::Archive* pArchive = EGG::Archive::mount(pData, NULL, 4);
        ARCHandle handle;
        if (pArchive == NULL || !ARCInitHandle(pData, &handle)) {
            std::fprintf(stderr, "ERROR: mount failed: %s\n", path.c_str());
            sErrors++;
        } else {
            // Archive record: path and node count (root's next index).
            std::printf("#archive\t%s\t%u\n", path.c_str(),
                        static_cast<unsigned>(handle.entryNum));
            WalkDir(pArchive, &handle, path, "/");
            delete pArchive;
        }
    } else {
        EmitLeaf(path, pData, size);
    }

    delete[] pExpand;
}

void ListDisc(const std::string& root, const std::string& rel,
              std::vector<std::string>* pOut) {
    std::string full = rel.empty() ? root : root + "/" + rel;
    DIR* d = opendir(full.c_str());
    if (d == NULL) {
        return;
    }
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        if (std::strcmp(e->d_name, ".") != 0 &&
            std::strcmp(e->d_name, "..") != 0) {
            names.push_back(e->d_name);
        }
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const std::string& n : names) {
        std::string r = rel.empty() ? n : rel + "/" + n;
        struct stat st;
        if (stat((root + "/" + r).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            ListDisc(root, r, pOut);
        } else {
            pOut->push_back(r);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> [path ...]\n",
                     argv[0]);
        return 2;
    }

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    std::vector<std::string> paths;
    if (argc > 2) {
        paths.assign(argv + 2, argv + argc);
    } else {
        ListDisc(argv[1], "", &paths);
    }

    for (const std::string& p : paths) {
        s32 bufSize = 0;
        u32 trueSize = 0;
        u8* pFile = LoadFromDVD(("/" + p).c_str(), &bufSize, &trueSize);
        if (pFile == NULL) {
            std::fprintf(stderr, "ERROR: cannot load %s\n", p.c_str());
            sErrors++;
            continue;
        }
        Expand(p, pFile, trueSize);
        delete[] pFile;
    }

    std::fprintf(stderr,
                 "arc_harness: %zu disc files, %d archives, %d leaf files, "
                 "%d errors (pointer size %zu)\n",
                 paths.size(), sArchives, sLeaves, sErrors, sizeof(void*));
    return sErrors == 0 ? 0 : 1;
}
