/**
 * Load-time byte order fix for BMG message text (host build).
 *
 * BMG DAT1 text is UTF-16BE. With -fshort-wchar the game reads it as
 * wchar_t, so on a little-endian host every code unit must be swapped once
 * after the file is loaded. Tags (0x001A, then a length byte, group byte,
 * 16-bit id and parameters) keep their payload bytes as in the file: the
 * game reads the length byte directly and, under PORT_HOST,
 * EGG::MsgRes::analyzeTag reads group and id with port_load_be32. Only the
 * 0x001A unit itself is swapped, so the text walkers still see the tag mark.
 * Docs: port/docs/HOST_BUILD.md, port/docs/POINTERS.md §6.
 */

#include <port/be.h>
#include <port/host.h>

#include <cstring>
#include <cstdlib>
#include <vector>

extern "C" int PortSwapBmgText(void* pBmg) {
    u8* p = static_cast<u8*>(pBmg);

    if (std::memcmp(p, "MESGbmg1", 8) != 0 || p[0x10] != 2) {
        return 0; // not a BMG, or not 16-bit text
    }
    // Swapped once: the last reserved header byte marks it (HOST_BUILD.md §29:
    // EGG::MsgRes swaps every BMG it is given, and buffers are shared)
    if (p[0x1F] == PORT_BMG_SWAPPED) {
        return 1;
    }

    u32 dataSize = port_load_be32(p + 0x8);
    u32 numBlocks = port_load_be32(p + 0xC);

    const u8* pInf = NULL;
    u8* pDat = NULL;
    u32 datSize = 0;

    u32 offset = 0x20;
    for (u32 i = 0; i < numBlocks && offset + 8 <= dataSize; i++) {
        u32 size = port_load_be32(p + offset + 4);
        if (std::memcmp(p + offset, "INF1", 4) == 0) {
            pInf = p + offset;
        } else if (std::memcmp(p + offset, "DAT1", 4) == 0) {
            pDat = p + offset + 8;
            datSize = size - 8;
        }
        if (size < 8) {
            return 0;
        }
        offset += size;
    }

    if (pInf == NULL || pDat == NULL) {
        return 0;
    }

    u16 numEntries = port_load_be16(pInf + 0x8);
    u16 entrySize = port_load_be16(pInf + 0xA);

    // One flag per byte of DAT1: messages may share text, swap each unit once.
    // (malloc: operator new may be the game's heap)
    u8* done = static_cast<u8*>(std::calloc(datSize + 2, 1));
    if (done == NULL) {
        return 0;
    }

    for (u16 i = 0; i < numEntries; i++) {
        u32 pos = port_load_be32(pInf + 0x10 + i * entrySize);

        while (pos + 2 <= datSize && !done[pos]) {
            u16 code = port_load_be16(pDat + pos);
            done[pos] = done[pos + 1] = 1;
            std::memcpy(pDat + pos, &code, 2);

            if (code == 0) {
                break;
            }

            if (code == 0x1A) {
                u32 length = pDat[pos + 2]; // bytes, including the 0x1A unit
                if (length < 2 || pos + length > datSize) {
                    std::free(done);
                    return 0;
                }
                for (u32 k = 2; k < length; k++) {
                    done[pos + k] = 1;
                }
                pos += length;
                continue;
            }

            pos += 2;
        }
    }

    std::free(done);
    p[0x1F] = PORT_BMG_SWAPPED;
    return 1;
}
