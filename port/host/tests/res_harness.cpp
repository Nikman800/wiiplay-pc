/**
 * Resource harness: runs the decompiled font and message loaders on the host.
 *
 * Walks the extracted disc with the real loader path (disc_walk.h) and, for
 * every file inside every archive:
 *
 *   RFNT  nw4r::ut::ResFont::SetResource (Rebuild relocates the file in place:
 *         every offset slot becomes an emulated Wii address, RES_PTR), then
 *         for each code point 0..0xFFFF:
 *           SetAlternateChar(ch)  -> FindGlyphIndex(ch) (index read back from
 *                                    FontInformation.alterCharIndex)
 *           GetCharWidths(ch), GetGlyph(&glyph, ch)
 *         and loads the same buffer a second time, which takes the RFNU
 *         (already relocated) branch, as lyt::TextBox does for fonts inside
 *         layout archives; both fonts must agree on every code point.
 *   MESG  PortSwapBmgText (UTF-16BE -> host order, tag payloads stay BE),
 *         then RPSysMessage (EGG::MsgRes): for each MID1 entry, the message
 *         id split, GetMsgEntry/GetAttribute/GetScale/GetCharSpace, GetMsg,
 *         a walk of the text with EGG::MsgRes::analyzeTag, and wcslen (the
 *         decomp's MSL_C version, 16-bit wchar_t).
 *
 * Output lines (tab-separated) are compared with an independent Python
 * decoding by port/host/tests/test_resources.py:
 *
 *   F path encoding fontType width height ascent descent baseline cellW cellH
 *     maxCharW texFmt linefeed alterIdx defLeft defGlyphW defCharW
 *     pGlyphOff pWidthOff pMapOff sheetImageOff
 *   C path cmapPayloadOffset...      (pMap chain, in chain order)
 *   W path cwdhPayloadOffset...      (pWidth chain, in chain order)
 *   G path ch index left glyphW charW texOff cellX cellY height texFmt texW texH
 *   A path notFoundCount left glyphW charW texOff cellX cellY
 *   M path i msgId group index entryIndex attribute scale charSpace textOffset
 *     codeUnits tags hash wcslen
 *
 * Offsets are relative to the start of the file buffer, recovered from the
 * relocated slots by subtracting the buffer's emulated address.
 */

#include <nw4r/ut.h>
#include <Pack/RPKernel/RPSysMessage.h>

#include <port/arena.h>
#include <port/host.h>

#include "disc_walk.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int sFonts = 0;
int sGlyphLines = 0;
int sBmgs = 0;
int sMessages = 0;

void Error(const std::string& path, const char* what) {
    std::fprintf(stderr, "ERROR: %s: %s\n", path.c_str(), what);
    disc_walk::Errors()++;
}

/******************************************************************************
 * BRFNT
 ******************************************************************************/

using nw4r::ut::BinaryBlockHeader;
using nw4r::ut::BinaryFileHeader;
using nw4r::ut::CharWidths;
using nw4r::ut::FontCodeMap;
using nw4r::ut::FontInformation;
using nw4r::ut::FontWidth;
using nw4r::ut::Glyph;
using nw4r::ut::ResFont;

FontInformation* FindFontInfo(u8* p) {
    BinaryFileHeader* pHeader = reinterpret_cast<BinaryFileHeader*>(p);
    BinaryBlockHeader* pBlock =
        reinterpret_cast<BinaryBlockHeader*>(p + pHeader->headerSize);
    for (int i = 0; i < pHeader->dataBlocks; i++) {
        if (pBlock->kind == FOURCC('F', 'I', 'N', 'F')) {
            return reinterpret_cast<FontInformation*>(pBlock + 1);
        }
        pBlock = reinterpret_cast<BinaryBlockHeader*>(
            reinterpret_cast<u8*>(pBlock) + pBlock->size);
    }
    return NULL;
}

long Off(const u8* base, const void* p) {
    return p == NULL ? 0 : static_cast<long>(static_cast<const u8*>(p) - base);
}

bool SameWidths(const CharWidths& a, const CharWidths& b) {
    return a.left == b.left && a.glyphWidth == b.glyphWidth &&
           a.charWidth == b.charWidth;
}

bool SameGlyph(const Glyph& a, const Glyph& b) {
    return a.pTexture == b.pTexture && SameWidths(a.widths, b.widths) &&
           a.height == b.height && a.texFormat == b.texFormat &&
           a.texWidth == b.texWidth && a.texHeight == b.texHeight &&
           a.cellX == b.cellX && a.cellY == b.cellY;
}

void TestFont(const std::string& path, u8* p, u32 size) {
    sFonts++;
    const char* cp = path.c_str();

    ResFont font;
    if (!font.SetResource(p)) {
        Error(path, "ResFont::SetResource failed");
        return;
    }
    if (std::memcmp(p, "RFNU", 4) != 0) {
        Error(path, "Rebuild did not mark the file RFNU");
    }

    FontInformation* pInfo = FindFontInfo(p);
    if (pInfo == NULL) {
        Error(path, "no FINF block");
        return;
    }

    // Slots hold emulated addresses now; they must point into this file.
    u32 vbase = port_vaddr(p);
    u32 vend = vbase + size;
    const u32 slots[] = {pInfo->pGlyph.raw(), pInfo->pWidth.raw(),
                         pInfo->pMap.raw(), pInfo->pGlyph->sheetImage.raw()};
    for (u32 v : slots) {
        if (v != 0 && (v < vbase || v >= vend)) {
            Error(path, "relocated slot points outside the file");
        }
    }

    CharWidths def = font.GetDefaultCharWidths();
    std::printf("F\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d"
                "\t%d\t%d\t%ld\t%ld\t%ld\t%ld\n",
                cp, static_cast<int>(font.GetEncoding()), pInfo->fontType,
                font.GetWidth(), font.GetHeight(), font.GetAscent(),
                font.GetDescent(), font.GetBaselinePos(), font.GetCellWidth(),
                font.GetCellHeight(), font.GetMaxCharWidth(),
                static_cast<int>(font.GetTextureFormat()), font.GetLineFeed(),
                static_cast<unsigned>(pInfo->alterCharIndex), def.left,
                def.glyphWidth, def.charWidth, Off(p, pInfo->pGlyph),
                Off(p, pInfo->pWidth), Off(p, pInfo->pMap),
                Off(p, pInfo->pGlyph->sheetImage));

    std::printf("C\t%s", cp);
    int guard = 0;
    for (const FontCodeMap* m = pInfo->pMap; m != NULL && guard < 4096;
         m = m->pNext, guard++) {
        std::printf("\t%ld", Off(p, m));
    }
    std::printf("\nW\t%s", cp);
    guard = 0;
    for (const FontWidth* w = pInfo->pWidth; w != NULL && guard < 4096;
         w = w->pNext, guard++) {
        std::printf("\t%ld", Off(p, w));
    }
    std::printf("\n");

    // Second load of the same (now RFNU) buffer, as lyt::TextBox does.
    ResFont again;
    if (!again.SetResource(p)) {
        Error(path, "second SetResource (RFNU branch) failed");
        return;
    }

    const u16 alt = pInfo->alterCharIndex;
    int notFound = 0;
    Glyph altGlyph;
    int mismatches = 0;

    for (u32 c = 0; c <= 0xFFFF; c++) {
        u16 ch = static_cast<u16>(c);

        // FindGlyphIndex(ch) through the public API: SetAlternateChar stores
        // the index it finds in FontInformation.alterCharIndex.
        bool found = font.SetAlternateChar(ch);
        u16 index = pInfo->alterCharIndex;
        pInfo->alterCharIndex = alt;

        CharWidths w = font.GetCharWidths(ch);
        Glyph g;
        font.GetGlyph(&g, ch);

        CharWidths w2 = again.GetCharWidths(ch);
        Glyph g2;
        again.GetGlyph(&g2, ch);
        if (!SameWidths(w, g.widths) || !SameWidths(w, w2) ||
            !SameGlyph(g, g2) || font.GetCharWidth(ch) != w.charWidth) {
            mismatches++;
        }

        if (found) {
            std::printf("G\t%s\t%u\t%u\t%d\t%d\t%d\t%ld\t%u\t%u\t%u\t%d\t%u\t%u"
                        "\n",
                        cp, c, index, w.left, w.glyphWidth, w.charWidth,
                        Off(p, g.pTexture), g.cellX, g.cellY, g.height,
                        static_cast<int>(g.texFormat), g.texWidth,
                        g.texHeight);
            sGlyphLines++;
        } else {
            // Not found: every such code point must give the alternate
            // character's glyph.
            if (notFound == 0) {
                altGlyph = g;
            } else if (!SameGlyph(g, altGlyph)) {
                mismatches++;
            }
            notFound++;
        }
    }

    if (notFound > 0) {
        std::printf("A\t%s\t%d\t%d\t%d\t%d\t%ld\t%u\t%u\n", cp, notFound,
                    altGlyph.widths.left, altGlyph.widths.glyphWidth,
                    altGlyph.widths.charWidth, Off(p, altGlyph.pTexture),
                    altGlyph.cellX, altGlyph.cellY);
    }
    if (mismatches != 0) {
        std::fprintf(stderr, "ERROR: %s: %d inconsistent code points\n", cp,
                     mismatches);
        disc_walk::Errors()++;
    }
}

/******************************************************************************
 * BMG
 ******************************************************************************/

/** RPSysMessage plus access to the MsgRes pieces it keeps protected. */
class MessageProbe : public RPSysMessage {
public:
    explicit MessageProbe(const void* pData)
        : RPSysMessage(pData, NULL), mpInfo(NULL), mpIds(NULL), mpText(NULL) {
        const MessageHeader* pHeader =
            static_cast<const MessageHeader*>(pData);
        const u8* pBlock = static_cast<const u8*>(pData) + sizeof(MessageHeader);
        for (u32 i = 0; i < pHeader->mNumBlocks; i++) {
            const DataBlock* pData = reinterpret_cast<const DataBlock*>(pBlock);
            if (pData->mKind == FOURCC('I', 'N', 'F', '1')) {
                mpInfo = reinterpret_cast<const MessageInfoDataBlock*>(pBlock);
            } else if (pData->mKind == FOURCC('M', 'I', 'D', '1')) {
                mpIds = reinterpret_cast<const MessageIDDataBlock*>(pBlock);
            } else if (pData->mKind == FOURCC('D', 'A', 'T', '1')) {
                mpText = reinterpret_cast<const MessageDataBlock*>(pBlock);
            }
            pBlock += pData->mDataSize;
        }
    }

    bool Valid() const {
        return mpInfo != NULL && mpIds != NULL && mpText != NULL;
    }
    u32 NumIds() const { return mpIds->mNumEntries; }
    u32 Id(u16 i) { return getMsgID(i); }

    /** Message id -> (group, index), as MsgRes::getMsgEntry splits it. */
    void Split(u32 id, u32* pGroup, u32* pIndex) const {
        u32 idBitNum = 32 - cMainMsgIdBitNumSet[mpIds->mFormSupplement];
        const MsgIDMask& rMask = cMsgIdMaskSet[mpIds->mFormSupplement];
        // A shift by 32 is undefined in C++; the PowerPC gives 0.
        *pGroup = idBitNum >= 32 ? 0 : (id & rMask.groupMask) >> idBitNum;
        *pIndex = id & rMask.indexMask;
    }

    long EntryIndex(const void* pEntry) const {
        return static_cast<long>(static_cast<const u8*>(pEntry) -
                                 reinterpret_cast<const u8*>(mpInfo->mContent)) /
               mpInfo->mEntrySize;
    }
    long TextOffset(const wchar_t* pText) const {
        return static_cast<long>(reinterpret_cast<const u8*>(pText) -
                                 reinterpret_cast<const u8*>(mpText->mContent));
    }

private:
    const MessageInfoDataBlock* mpInfo;
    const MessageIDDataBlock* mpIds;
    const MessageDataBlock* mpText;
};

void HashToken(u64* pHash, u32 token) {
    for (int i = 0; i < 4; i++) {
        *pHash ^= (token >> (8 * i)) & 0xFF;
        *pHash *= 0x100000001b3ULL;
    }
}

void TestBmg(const std::string& path, u8* p, u32 size) {
    (void)size;
    sBmgs++;

    if (!PortSwapBmgText(p)) {
        Error(path, "PortSwapBmgText failed");
        return;
    }

    MessageProbe msg(p);
    if (!msg.Valid()) {
        Error(path, "INF1, MID1 or DAT1 missing");
        return;
    }

    for (u32 i = 0; i < msg.NumIds(); i++) {
        u32 id = msg.Id(static_cast<u16>(i));
        u32 group, index;
        msg.Split(id, &group, &index);

        const RPSysMessage::Entry* pEntry = msg.GetMsgEntry(group, index);
        if (pEntry == NULL) {
            Error(path, "GetMsgEntry found no entry for a MID1 id");
            continue;
        }
        const wchar_t* pText = msg.GetMsg(group, index);

        // Walk the text the way the tag processors do: 0x1A starts a tag,
        // analyzeTag gives its length and id.
        u32 units = 0, tags = 0;
        u64 hash = 0xcbf29ce484222325ULL;
        const wchar_t* s = pText;
        while (*s != 0) {
            if (*s == static_cast<wchar_t>(0x1A)) { // MsgRes::cTagMark
                u8 tagLength;
                u32 tagId;
                void* pParam;
                EGG::MsgRes::analyzeTag(static_cast<u16>(*s), s + 1,
                                        &tagLength, &tagId, &pParam);
                HashToken(&hash, 0xFFFF0000u | tagLength);
                HashToken(&hash, tagId);
                tags++;
                s = reinterpret_cast<const wchar_t*>(
                    reinterpret_cast<const u8*>(s + 1) + tagLength);
                continue;
            }
            HashToken(&hash, static_cast<u16>(*s));
            units++;
            s++;
        }

        std::printf("M\t%s\t%u\t%u\t%u\t%u\t%ld\t%u\t%u\t%d\t%ld\t%u\t%u\t%016" PRIx64
                    "\t%zu\n",
                    path.c_str(), i, id, group, index, msg.EntryIndex(pEntry),
                    msg.GetAttribute(group, index),
                    static_cast<unsigned>(msg.GetScale(group, index)),
                    static_cast<s8>(msg.GetCharSpace(group, index)),
                    msg.TextOffset(pText), units, tags, hash, wcslen(pText));
        sMessages++;
    }
}

void Leaf(const std::string& path, u8* p, u32 size) {
    if (size >= 16 && std::memcmp(p, "RFNT", 4) == 0) {
        TestFont(path, p, size);
    } else if (size >= 32 && std::memcmp(p, "MESG", 4) == 0) {
        TestBmg(path, p, size);
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
        disc_walk::ListDisc(argv[1], "", &paths);
    }

    disc_walk::Visitor visitor = {Leaf, NULL};
    disc_walk::WalkDisc(visitor, paths);

    std::fprintf(stderr,
                 "res_harness: %d fonts (%d glyph lines), %d BMG files "
                 "(%d messages), %d errors (pointer size %zu, wchar_t %zu)\n",
                 sFonts, sGlyphLines, sBmgs, sMessages, disc_walk::Errors(),
                 sizeof(void*), sizeof(wchar_t));
    return disc_walk::Errors() == 0 ? 0 : 1;
}
