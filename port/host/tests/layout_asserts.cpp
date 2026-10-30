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
#include <nw4r/lyt.h>
#include <nw4r/ut.h>
#include <revolution/TPL.h>

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

// TPL (port/host/include/revolution/TPL/TPL.h; POINTERS.md §4)
PORT_LAYOUT_SIZE(TPLPalette, 0xC);
PORT_LAYOUT_OFFSET(TPLPalette, numDescriptors, 0x4);
PORT_LAYOUT_OFFSET(TPLPalette, descriptorArray, 0x8);
PORT_LAYOUT_SIZE(TPLDescriptor, 0x8);
PORT_LAYOUT_OFFSET(TPLDescriptor, CLUTHeader, 0x4);
PORT_LAYOUT_SIZE(TPLHeader, 0x24);
PORT_LAYOUT_OFFSET(TPLHeader, width, 0x2);
PORT_LAYOUT_OFFSET(TPLHeader, format, 0x4);
PORT_LAYOUT_OFFSET(TPLHeader, data, 0x8);
PORT_LAYOUT_OFFSET(TPLHeader, wrapS, 0xC);
PORT_LAYOUT_OFFSET(TPLHeader, magFilter, 0x18);
PORT_LAYOUT_OFFSET(TPLHeader, LODBias, 0x1C);
PORT_LAYOUT_OFFSET(TPLHeader, edgeLODEnable, 0x20);
PORT_LAYOUT_OFFSET(TPLHeader, unpacked, 0x23);
PORT_LAYOUT_SIZE(TPLClutHeader, 0xC);
PORT_LAYOUT_OFFSET(TPLClutHeader, unpacked, 0x2); // include/revolution/TPL/TPL.h comments 0x1
PORT_LAYOUT_OFFSET(TPLClutHeader, format, 0x4);
PORT_LAYOUT_OFFSET(TPLClutHeader, data, 0x8);

// nw4r::lyt resource blocks (lyt_resources.h, lyt_pane.h, lyt_picture.h,
// lyt_textBox.h, lyt_window.h, lyt_group.h, lyt_layout.h, lyt_material.h,
// lyt_animation.h). AnimationTarget, StepKey and HermiteKey are local to
// lyt_animation.cpp and are covered by the layout harness instead.
namespace lres = nw4r::lyt::res;
PORT_LAYOUT_SIZE(lres::BinaryFileHeader, 0x10);
PORT_LAYOUT_OFFSET(lres::BinaryFileHeader, fileSize, 0x8);
PORT_LAYOUT_OFFSET(lres::BinaryFileHeader, dataBlocks, 0xE);
PORT_LAYOUT_SIZE(lres::DataBlockHeader, 0x8);
PORT_LAYOUT_SIZE(lres::Texture, 0x8);
PORT_LAYOUT_SIZE(lres::Font, 0x8);
PORT_LAYOUT_SIZE(lres::TextureList, 0xC);
PORT_LAYOUT_SIZE(lres::FontList, 0xC);
PORT_LAYOUT_SIZE(lres::MaterialList, 0xC);
PORT_LAYOUT_SIZE(lres::TexMap, 0x4);
PORT_LAYOUT_SIZE(lres::VEC2, 0x8);
PORT_LAYOUT_SIZE(lres::VEC3, 0xC);
PORT_LAYOUT_SIZE(lres::TexSRT, 0x14);
PORT_LAYOUT_SIZE(lres::TexCoord, 0x20);
PORT_LAYOUT_SIZE(lres::Layout, 0x14);
PORT_LAYOUT_OFFSET(lres::Layout, layoutSize, 0xC);
PORT_LAYOUT_SIZE(lres::Material, 0x40);
PORT_LAYOUT_OFFSET(lres::Material, tevCols, 0x14);
PORT_LAYOUT_OFFSET(lres::Material, tevKCols, 0x2C);
PORT_LAYOUT_OFFSET(lres::Material, resNum, 0x3C);
PORT_LAYOUT_SIZE(lres::Pane, 0x4C);
PORT_LAYOUT_OFFSET(lres::Pane, translate, 0x24);
PORT_LAYOUT_OFFSET(lres::Pane, rotate, 0x30);
PORT_LAYOUT_OFFSET(lres::Pane, scale, 0x3C);
PORT_LAYOUT_OFFSET(lres::Pane, size, 0x44);
PORT_LAYOUT_SIZE(lres::Picture, 0x60);
PORT_LAYOUT_OFFSET(lres::Picture, vtxCols, 0x4C);
PORT_LAYOUT_OFFSET(lres::Picture, materialIdx, 0x5C);
PORT_LAYOUT_SIZE(lres::TextBox, 0x74);
PORT_LAYOUT_OFFSET(lres::TextBox, textStrOffset, 0x58);
PORT_LAYOUT_OFFSET(lres::TextBox, textCols, 0x5C);
PORT_LAYOUT_OFFSET(lres::TextBox, fontSize, 0x64);
PORT_LAYOUT_OFFSET(lres::TextBox, lineSpace, 0x70);
PORT_LAYOUT_SIZE(lres::WindowContent, 0x14);
PORT_LAYOUT_SIZE(lres::WindowFrame, 0x4);
PORT_LAYOUT_SIZE(lres::Window, 0x68);
PORT_LAYOUT_OFFSET(lres::Window, inflation, 0x4C);
PORT_LAYOUT_OFFSET(lres::Window, contentOffset, 0x60);
PORT_LAYOUT_OFFSET(lres::Window, frameOffsetTableOffset, 0x64);
PORT_LAYOUT_SIZE(lres::Group, 0x1C);
PORT_LAYOUT_OFFSET(lres::Group, paneNum, 0x18);
PORT_LAYOUT_SIZE(lres::AnimationBlock, 0x14);
PORT_LAYOUT_OFFSET(lres::AnimationBlock, fileNum, 0xC);
PORT_LAYOUT_OFFSET(lres::AnimationBlock, animContOffsetsOffset, 0x10);
PORT_LAYOUT_SIZE(lres::AnimationInfo, 0x8);
PORT_LAYOUT_SIZE(lres::AnimationContent, 0x18);
PORT_LAYOUT_SIZE(nw4r::ut::Color, 0x4);

} // namespace

int main() {
    std::printf("test_layout: OK (static_asserts, pointer size %zu)\n",
                sizeof(void*));
    return 0;
}
