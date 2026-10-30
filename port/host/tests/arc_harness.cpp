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

#include "disc_walk.h"

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

int sLeaves = 0;

u64 Fnv1a64(const u8* p, u32 n) {
    u64 h = 0xcbf29ce484222325ULL;
    for (u32 i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

void EmitLeaf(const std::string& path, u8* p, u32 size) {
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

void EmitArchive(const std::string& path, const ARCHandle& handle) {
    // Archive record: path and node count (root's next index).
    std::printf("#archive\t%s\t%u\n", path.c_str(),
                static_cast<unsigned>(handle.entryNum));
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
        disc_walk::ListDisc(argv[1], "", &paths);
    }

    disc_walk::Visitor visitor = {EmitLeaf, EmitArchive};
    disc_walk::WalkDisc(visitor, paths);

    std::fprintf(stderr,
                 "arc_harness: %zu disc files, %d archives, %d leaf files, "
                 "%d errors (pointer size %zu)\n",
                 paths.size(), disc_walk::Archives(), sLeaves,
                 disc_walk::Errors(), sizeof(void*));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
