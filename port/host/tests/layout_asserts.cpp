/**
 * Compile-time layout checks for structs that mirror file data
 * (port/docs/POINTERS.md §2 "Detecting misses at compile time").
 *
 * Every struct that is overlaid on a loaded file must have the Wii size and
 * member offsets on every host: a member left as T* (8 bytes on 64-bit) or a
 * byte array with the wrong alignment would otherwise only show up as
 * corrupt data at run time. The expected values are the `// at 0xNN`
 * comments in the decomp headers. Add a block here for each struct moved to
 * be_* / RES_PTR. The program itself only prints OK; the checks run when it
 * compiles (built by `make`, run by `make test`).
 */

#include <Pack/RPKernel/RPSysMessage.h>
#include <egg/util/eggMsgRes.h>
#include <nw4r/ut.h>

#include <cstddef>
#include <cstdio>

#define PORT_LAYOUT_SIZE(T, n)                                                 \
    static_assert(sizeof(T) == (n), "sizeof(" #T ") != " #n)
#define PORT_LAYOUT_OFFSET(T, f, o)                                            \
    static_assert(offsetof(T, f) == (o), "offsetof(" #T ", " #f ") != " #o)

namespace {

using namespace nw4r::ut;

// nw4r::ut::BinaryFileHeader / BinaryBlockHeader (ut_binaryFileFormat.h)
PORT_LAYOUT_SIZE(BinaryFileHeader, 0x10);
PORT_LAYOUT_OFFSET(BinaryFileHeader, byteOrder, 0x4);
PORT_LAYOUT_OFFSET(BinaryFileHeader, version, 0x6);
PORT_LAYOUT_OFFSET(BinaryFileHeader, fileSize, 0x8);
PORT_LAYOUT_OFFSET(BinaryFileHeader, headerSize, 0xC);
PORT_LAYOUT_OFFSET(BinaryFileHeader, dataBlocks, 0xE);
PORT_LAYOUT_SIZE(BinaryBlockHeader, 0x8);
PORT_LAYOUT_OFFSET(BinaryBlockHeader, size, 0x4);

// BRFNT blocks (ut_ResFontBase.h, ut_Font.h)
PORT_LAYOUT_SIZE(CharWidths, 0x3);
PORT_LAYOUT_SIZE(FontTextureGlyph, 0x18);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetSize, 0x4);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetNum, 0x8);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetFormat, 0xA);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetRow, 0xC);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetLine, 0xE);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetWidth, 0x10);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetHeight, 0x12);
PORT_LAYOUT_OFFSET(FontTextureGlyph, sheetImage, 0x14);
PORT_LAYOUT_SIZE(FontWidth, 0x8);
PORT_LAYOUT_OFFSET(FontWidth, indexEnd, 0x2);
PORT_LAYOUT_OFFSET(FontWidth, pNext, 0x4);
PORT_LAYOUT_OFFSET(FontWidth, widthTable, 0x8);
PORT_LAYOUT_SIZE(FontCodeMap, 0xC);
PORT_LAYOUT_OFFSET(FontCodeMap, ccodeEnd, 0x2);
PORT_LAYOUT_OFFSET(FontCodeMap, mappingMethod, 0x4);
PORT_LAYOUT_OFFSET(FontCodeMap, reserved, 0x6);
PORT_LAYOUT_OFFSET(FontCodeMap, pNext, 0x8);
PORT_LAYOUT_OFFSET(FontCodeMap, mapInfo, 0xC);
PORT_LAYOUT_SIZE(FontInformation, 0x18);
PORT_LAYOUT_OFFSET(FontInformation, linefeed, 0x1);
PORT_LAYOUT_OFFSET(FontInformation, alterCharIndex, 0x2);
PORT_LAYOUT_OFFSET(FontInformation, defaultWidth, 0x4);
PORT_LAYOUT_OFFSET(FontInformation, encoding, 0x7);
PORT_LAYOUT_OFFSET(FontInformation, pGlyph, 0x8);
PORT_LAYOUT_OFFSET(FontInformation, pWidth, 0xC);
PORT_LAYOUT_OFFSET(FontInformation, pMap, 0x10);
PORT_LAYOUT_OFFSET(FontInformation, height, 0x14);
PORT_LAYOUT_OFFSET(FontInformation, width, 0x15);
PORT_LAYOUT_OFFSET(FontInformation, ascent, 0x16);

// BMG blocks (eggMsgRes.h, RPSysMessage.h). The nested types are protected,
// so the checks live in a derived class.
struct MsgResLayout : EGG::MsgRes {
    PORT_LAYOUT_SIZE(DataBlockHeader, 0x10);
    PORT_LAYOUT_OFFSET(DataBlockHeader, mType, 0x4);
    PORT_LAYOUT_OFFSET(DataBlockHeader, mDataSize, 0x8);
    PORT_LAYOUT_OFFSET(DataBlockHeader, mNumBlocks, 0xC);
    PORT_LAYOUT_SIZE(MessageHeader, 0x20);
    PORT_LAYOUT_OFFSET(MessageHeader, mEncoding, 0x10);
    PORT_LAYOUT_SIZE(DataBlock, 0x8);
    PORT_LAYOUT_OFFSET(DataBlock, mDataSize, 0x4);
    PORT_LAYOUT_SIZE(MessageInfoDataBlock::Entry, 0x8);
    PORT_LAYOUT_OFFSET(MessageInfoDataBlock::Entry, mAttribute, 0x4);
    PORT_LAYOUT_OFFSET(MessageInfoDataBlock, mNumEntries, 0x8);
    PORT_LAYOUT_OFFSET(MessageInfoDataBlock, mEntrySize, 0xA);
    PORT_LAYOUT_OFFSET(MessageInfoDataBlock, mGroupID, 0xC);
    PORT_LAYOUT_OFFSET(MessageInfoDataBlock, mContent, 0x10);
    PORT_LAYOUT_OFFSET(MessageDataBlock, mContent, 0x8);
    static_assert(sizeof(MessageDataBlock().mContent[0]) == 2,
                  "DAT1 text must be 16-bit wchar_t");
    PORT_LAYOUT_OFFSET(MessageIDDataBlock, mNumEntries, 0x8);
    PORT_LAYOUT_OFFSET(MessageIDDataBlock, mForm, 0xA);
    PORT_LAYOUT_OFFSET(MessageIDDataBlock, mFormSupplement, 0xB);
    PORT_LAYOUT_OFFSET(MessageIDDataBlock, mContent, 0x10);
};

// RPSysMessage::Entry extends the INF1 entry to 12 bytes.
PORT_LAYOUT_SIZE(RPSysMessage::Entry, 0xC);
PORT_LAYOUT_OFFSET(RPSysMessage::Entry, scale, 0x8);
PORT_LAYOUT_OFFSET(RPSysMessage::Entry, charSpace, 0xA);

} // namespace

int main() {
    std::printf("test_layout: OK (static_asserts, pointer size %zu)\n",
                sizeof(void*));
    return 0;
}
