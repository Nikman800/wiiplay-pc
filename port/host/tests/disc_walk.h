/**
 * Disc walk shared by the host harnesses (arc_harness, res_harness).
 *
 * Loads every disc file the way RPSysResourceManager::LoadFromDVD and
 * ExpandCompressed do (EGG::DvdFile, EGG::DvdRipper, EGG::Decomp: real
 * decompiled code), mounts archives with EGG::Archive / ARC, and descends
 * into nested archives with the same rule as wiiarc._expand. Each leaf file
 * is handed to a callback while its archive is mounted, so the callback may
 * use (and modify, as the game does) the file in place.
 */

#ifndef PORT_HOST_TESTS_DISC_WALK_H
#define PORT_HOST_TESTS_DISC_WALK_H

#include <egg/core.h>
#include <revolution/ARC.h>

#include <port/host.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace disc_walk {

struct Visitor {
    /** A file that is neither compressed-then-U8 nor U8. */
    void (*leaf)(const std::string& path, u8* pData, u32 size);
    /** A mounted archive (may be NULL). */
    void (*archive)(const std::string& path, const ARCHandle& handle);
};

inline int& Errors() {
    static int sErrors = 0;
    return sErrors;
}
inline int& Archives() {
    static int sArchives = 0;
    return sArchives;
}

/**
 * RPSysResourceManager::LoadFromDVD (src/Pack/RPSystem/RPSysResourceManager.cpp),
 * reproduced with the same EGG calls. Returns the 32-byte-rounded buffer and
 * the true file size.
 */
inline u8* LoadFromDVD(const char* pPath, s32* pSize, u32* pTrueSize) {
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
inline u8* ExpandCompressed(u8* pData, s32 size, s32* pExpandSize) {
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

inline bool IsU8(const u8* p, u32 size) {
    return size >= 0x20 && p[0] == 0x55 && p[1] == 0xAA && p[2] == 0x38 &&
           p[3] == 0x2D;
}

inline void Expand(const Visitor& v, const std::string& path, u8* pData,
                   u32 size);

/** Walk one mounted archive with the ARC directory API. */
inline void WalkDir(const Visitor& v, EGG::Archive* pArchive,
                    ARCHandle* pHandle, const std::string& prefix,
                    const std::string& dir) {
    ARCDir arcDir;
    if (!ARCOpenDir(pHandle, dir.c_str(), &arcDir)) {
        std::fprintf(stderr, "ERROR: ARCOpenDir(%s) failed in %s\n",
                     dir.c_str(), prefix.c_str());
        Errors()++;
        return;
    }

    ARCDirEntry entry;
    while (ARCReadDir(&arcDir, &entry)) {
        std::string rel = (dir == "/" ? std::string("") : dir.substr(1) + "/") +
                          entry.name;

        if (entry.isDir) {
            WalkDir(v, pArchive, pHandle, prefix, "/" + rel);
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
            Errors()++;
            continue;
        }
        ARCClose(&fast);

        Expand(v, prefix + "/" + rel, pFile, info.getLength());
    }
    ARCCloseDir(&arcDir);
}

/** Same rule as wiiarc._expand: decompress, then descend if U8. */
inline void Expand(const Visitor& v, const std::string& path, u8* pData,
                   u32 size) {
    s32 expandSize = 0;
    u8* pExpand = NULL;

    if (EGG::Decomp::checkCompressed(pData) != EGG::Decomp::cCompress_None) {
        pExpand = ExpandCompressed(pData, static_cast<s32>(size), &expandSize);
        pData = pExpand;
        size = static_cast<u32>(expandSize);
    }

    if (IsU8(pData, size)) {
        Archives()++;
        EGG::Archive* pArchive = EGG::Archive::mount(pData, NULL, 4);
        ARCHandle handle;
        if (pArchive == NULL || !ARCInitHandle(pData, &handle)) {
            std::fprintf(stderr, "ERROR: mount failed: %s\n", path.c_str());
            Errors()++;
        } else {
            if (v.archive != NULL) {
                v.archive(path, handle);
            }
            WalkDir(v, pArchive, &handle, path, "/");
            delete pArchive;
        }
    } else {
        v.leaf(path, pData, size);
    }

    delete[] pExpand;
}

inline void ListDisc(const std::string& root, const std::string& rel,
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

/** Loads each disc path (relative to the disc root) and walks it. */
inline void WalkDisc(const Visitor& v, const std::vector<std::string>& paths) {
    for (const std::string& p : paths) {
        s32 bufSize = 0;
        u32 trueSize = 0;
        u8* pFile = LoadFromDVD(("/" + p).c_str(), &bufSize, &trueSize);
        if (pFile == NULL) {
            std::fprintf(stderr, "ERROR: cannot load %s\n", p.c_str());
            Errors()++;
            continue;
        }
        Expand(v, p, pFile, trueSize);
        delete[] pFile;
    }
}

} // namespace disc_walk

#endif
