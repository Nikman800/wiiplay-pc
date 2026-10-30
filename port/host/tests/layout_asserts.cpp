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
#include <nw4r/ef.h>
#include <nw4r/g3d.h>
#include <nw4r/lyt.h>
#include <nw4r/ut.h>
#include <revolution/TPL.h>
#include <nw4r/snd.h>
#include <Pack/RPParty/RPBomScene/RPBomQuestionParam.h>
#include <Pack/RPParty/RPPartyCommon/RPKcl.h>
#include <Pack/RPParty/RPPartyCommon/RPResFile_2.h>
#include <Pack/RPParty/RPPartyCommon/RPTnkMapData.h>
#include <Pack/RPParty/RPPnpScene/RPPnpLevelTable.h>
#include <Pack/RPParty/RPWlyScene/RPWlyLevelTable.h>
#include <egg/gfxe/eggDrawPathBloom.h>
#include <egg/gfxe/eggLightManager.h>
#include <egg/gfxe/eggLightTexture.h>

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

// nw4r::g3d resources (include/nw4r/g3d/res; POINTERS.md §4 and step 5).
namespace g3d = nw4r::g3d;
PORT_LAYOUT_SIZE(g3d::ResDicNodeData, 0x10);
PORT_LAYOUT_OFFSET(g3d::ResDicData, data, 0x8);
PORT_LAYOUT_OFFSET(g3d::ResFileData, dict, 0x10);
PORT_LAYOUT_SIZE(g3d::ResNameData, 0x8);
PORT_LAYOUT_SIZE(g3d::ResTagDLData, 0xC);
PORT_LAYOUT_SIZE(g3d::ResMdlData, 0x80);
PORT_LAYOUT_OFFSET(g3d::ResMdlData, name, 0x3C);
PORT_LAYOUT_OFFSET(g3d::ResMdlData, info, 0x40);
PORT_LAYOUT_OFFSET(g3d::ResMdlInfoData, toMtxIDToNodeID, 0x24);
PORT_LAYOUT_OFFSET(g3d::ResNodeData, toParentNode, 0x5C);
PORT_LAYOUT_SIZE(g3d::ResNodeData, 0xD0);
PORT_LAYOUT_SIZE(g3d::ResVtxPosData, 0x38);
PORT_LAYOUT_OFFSET(g3d::ResVtxPosData, stride, 0x1D);
PORT_LAYOUT_OFFSET(g3d::ResVtxTexCoordData, numTexCoord, 0x1E);
PORT_LAYOUT_SIZE(g3d::ResPrePrimDL, 0xE0);
PORT_LAYOUT_OFFSET(g3d::ResShpData, tagPrePrimDL, 0x18);
PORT_LAYOUT_OFFSET(g3d::ResShpData, idVtxPosition, 0x48);
PORT_LAYOUT_OFFSET(g3d::ResShpData, idVtxTexCoord, 0x50);
PORT_LAYOUT_OFFSET(g3d::ResShpData, msu, 0x64);
PORT_LAYOUT_SIZE(g3d::ResTexPlttInfoData, 0x34);
PORT_LAYOUT_OFFSET(g3d::ResTexPlttInfoData, pTexData, 0x8);
PORT_LAYOUT_OFFSET(g3d::ResTexPlttInfoData, pPlttData, 0xC);
PORT_LAYOUT_OFFSET(g3d::ResTexPlttInfoData, lod_bias, 0x28);
PORT_LAYOUT_SIZE(g3d::ResTexObjData, 0x104);
PORT_LAYOUT_SIZE(g3d::ResTlutObjData, 0x64);
PORT_LAYOUT_OFFSET(g3d::ResMatData, texObjData, 0x3C);
PORT_LAYOUT_OFFSET(g3d::ResMatData, tlutObjData, 0x140);
PORT_LAYOUT_OFFSET(g3d::ResMatData, texSrtData, 0x1A4);
PORT_LAYOUT_OFFSET(g3d::ResMatData, chan, 0x3EC);
PORT_LAYOUT_OFFSET(g3d::ResTevData, dl, 0x20);
PORT_LAYOUT_SIZE(g3d::ResTexData, 0x38);
PORT_LAYOUT_OFFSET(g3d::ResTexData, mipmap_level, 0x24);
PORT_LAYOUT_SIZE(g3d::ResPlttData, 0x28);
PORT_LAYOUT_OFFSET(g3d::ResPlttData, numEntries, 0x1C);
PORT_LAYOUT_OFFSET(g3d::ResAnmChrData, info, 0x1C);
PORT_LAYOUT_OFFSET(g3d::ResAnmChrInfoData, scalingRule, 0x8);
PORT_LAYOUT_OFFSET(g3d::ResAnmClrData, info, 0x1C);
PORT_LAYOUT_OFFSET(g3d::ResAnmTexSrtData, info, 0x1C);
PORT_LAYOUT_OFFSET(g3d::ResAnmTexSrtInfoData, policy, 0x8);
PORT_LAYOUT_OFFSET(g3d::ResAnmTexPatData, info, 0x2C);
PORT_LAYOUT_OFFSET(g3d::ResAnmTexPatInfoData, policy, 0x8);
PORT_LAYOUT_SIZE(g3d::ResAnmTexPatFrmData, 0x8);
PORT_LAYOUT_OFFSET(g3d::ResAnmVisData, info, 0x1C);
PORT_LAYOUT_OFFSET(g3d::ResAnmShpData, info, 0x20);
PORT_LAYOUT_OFFSET(g3d::ResAnmScnData, info, 0x30);
PORT_LAYOUT_OFFSET(g3d::ResLightSetData, lightId, 0x3C);
PORT_LAYOUT_SIZE(g3d::ResKeyFrameData, 0xC);
PORT_LAYOUT_SIZE(g3d::ResAnmChrFrm48Data, 0x6);
PORT_LAYOUT_SIZE(GXTexObj, 0x20);
PORT_LAYOUT_SIZE(GXTlutObj, 0xC);

// nw4r::ef (POINTERS.md step 6). FileHeader, BlockHeader and NameTable are
// local to ef_resource.cpp; ef_harness covers them.
PORT_LAYOUT_SIZE(RES_LINK, 0x8);
PORT_LAYOUT_OFFSET(nw4r::ef::EffectProject, projectlink, 0x4);
PORT_LAYOUT_OFFSET(nw4r::ef::EffectProject, projectnameLen, 0xC);
PORT_LAYOUT_OFFSET(nw4r::ef::EffectProject, projectname, 0x10);
PORT_LAYOUT_SIZE(nw4r::ef::EffectProject, 0x14);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureProject, projectlink, 0x4);
PORT_LAYOUT_SIZE(nw4r::ef::TextureProject, 0x14);
PORT_LAYOUT_SIZE(nw4r::ef::EmitterResource, 0x8); // locates the EmitterDesc
// Field parameter blocks (ef_res_animcurve.h; read by ParticleManager::Calc).
PORT_LAYOUT_OFFSET(nw4r::ef::FieldGravityParam, space, 0x10);
PORT_LAYOUT_SIZE(nw4r::ef::FieldGravityParam, 0x14);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldRandomParam, interval, 0x8);
PORT_LAYOUT_SIZE(nw4r::ef::FieldRandomParam, 0xC);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldSpinParam, space, 0x10);
PORT_LAYOUT_SIZE(nw4r::ef::FieldSpinParam, 0x14);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldMagnetParam, space, 0x10);
PORT_LAYOUT_SIZE(nw4r::ef::FieldMagnetParam, 0x14);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldNewtonParam, pos, 0x8);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldNewtonParam, space, 0x14);
PORT_LAYOUT_SIZE(nw4r::ef::FieldNewtonParam, 0x18);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldVortexParam, rot, 0xC);
PORT_LAYOUT_OFFSET(nw4r::ef::FieldVortexParam, space, 0x18);
PORT_LAYOUT_SIZE(nw4r::ef::FieldVortexParam, 0x1C);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureData, width, 0x4);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureData, dataSize, 0x8);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureData, tlutEntries, 0xE);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureData, tlutSize, 0x10);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureData, texture, 0x18);
PORT_LAYOUT_OFFSET(nw4r::ef::TextureData, tlut, 0x1C);
PORT_LAYOUT_SIZE(nw4r::ef::TextureData, 0x20); // locates the image
PORT_LAYOUT_OFFSET(nw4r::ef::Lighting, mRadius, 0xA); // #pragma pack(1)
PORT_LAYOUT_OFFSET(nw4r::ef::Lighting, mPosition, 0xE);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDrawSetting, mAlphaFlickCycle, 0x66);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDrawSetting, mLighting, 0x6A);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDrawSetting, mIndTexOffsetMtx, 0x84);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDrawSetting, ptcltype, 0xA0);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDrawSetting, zOffset, 0xA8);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, emitEmit, 0x10);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, commonParam, 0x1C);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, emitEmitDiv, 0x34);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, velDiffusionSpecDir, 0x50);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, randomSeed, 0x88);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, drawSetting, 0x94);
PORT_LAYOUT_OFFSET(nw4r::ef::ParticleParameterDesc, textureRotate, 0x44);
PORT_LAYOUT_OFFSET(nw4r::ef::ParticleParameterDesc, mTexture, 0x68);
PORT_LAYOUT_OFFSET(nw4r::ef::ParticleParameterDesc, textureWrap, 0x74);
PORT_LAYOUT_OFFSET(nw4r::ef::ParticleParameterDesc, rotateOffset, 0x7C);
PORT_LAYOUT_OFFSET(nw4r::ef::ParticleParameterDesc, textureNames, 0x88);
PORT_LAYOUT_SIZE(nw4r::ef::AnimCurveHeader, 0x20);
PORT_LAYOUT_OFFSET(nw4r::ef::AnimCurveHeader, randomSeed, 0x6);
PORT_LAYOUT_OFFSET(nw4r::ef::AnimCurveHeader, keyTable, 0xC);
PORT_LAYOUT_OFFSET(nw4r::ef::AnimCurveHeader, infoTable, 0x1C);
PORT_LAYOUT_OFFSET(nw4r::ef::AnimCurveNameTable, datas, 0x4);
PORT_LAYOUT_SIZE(nw4r::ef::AnimCurveNameTable::Name, 0x4); // work slots, stride 4

// nw4r::snd file structs (POINTERS.md step 7, HOST_BUILD.md §13).
namespace snd_layout {
using namespace nw4r::snd;
using namespace nw4r::snd::detail;
typedef Util::DataRef<void> Ref;
PORT_LAYOUT_SIZE(Ref, 0x8);
PORT_LAYOUT_OFFSET(Ref, value, 0x4);
PORT_LAYOUT_SIZE(Util::Table<Ref>, 0xC);
PORT_LAYOUT_SIZE(SoundArchive::FilePos, 0x8);
PORT_LAYOUT_SIZE(SoundArchiveFile::StringTreeNode, 0x14);
PORT_LAYOUT_OFFSET(SoundArchiveFile::StringTreeNode, id, 0x10);
PORT_LAYOUT_SIZE(SoundArchiveFile::StringChunk, 0x14);
PORT_LAYOUT_OFFSET(SoundArchiveFile::SeqSoundInfo, channelPriority, 0xC);
PORT_LAYOUT_OFFSET(SoundArchiveFile::WaveSoundInfo, channelPriority, 0x8);
PORT_LAYOUT_OFFSET(SoundArchiveFile::SoundCommonInfo, param3dRef, 0xC);
PORT_LAYOUT_OFFSET(SoundArchiveFile::SoundCommonInfo, soundInfoRef, 0x18);
PORT_LAYOUT_OFFSET(SoundArchiveFile::SoundCommonInfo, userParam, 0x20);
PORT_LAYOUT_OFFSET(SoundArchiveFile::SoundCommonInfo, panCurve, 0x29);
PORT_LAYOUT_OFFSET(SoundArchiveFile::PlayerInfo, heapSize, 0x8);
PORT_LAYOUT_OFFSET(SoundArchiveFile::FileInfo, filePosTableRef, 0x14);
PORT_LAYOUT_SIZE(SoundArchiveFile::GroupItemInfo, 0x14);
PORT_LAYOUT_OFFSET(SoundArchiveFile::GroupInfo, itemTableRef, 0x20);
PORT_LAYOUT_SIZE(SoundArchiveFile::SoundArchivePlayerInfo, 0xE);
PORT_LAYOUT_OFFSET(SoundArchiveFile::Info, soundArchivePlayerInfoRef, 0x28);
PORT_LAYOUT_SIZE(SoundArchiveFile::Header, 0x28);
PORT_LAYOUT_SIZE(SeqFile::Header, 0x20);
PORT_LAYOUT_OFFSET(SeqFile::DataBlock, baseOffset, 0x8);
PORT_LAYOUT_OFFSET(BankFile::InstParam, originalKey, 0xC);
PORT_LAYOUT_OFFSET(BankFile::InstParam, tune, 0x10);
PORT_LAYOUT_OFFSET(BankFile::IndexTable, ref, 0x4);
PORT_LAYOUT_OFFSET(BankFile::DataBlock, instTable, 0x8);
PORT_LAYOUT_SIZE(WaveFile::WaveInfo, 0x1C);
PORT_LAYOUT_OFFSET(WaveFile::WaveInfo, dataOffset, 0x14);
PORT_LAYOUT_SIZE(WaveFile::WaveChannelInfo, 0x1C);
PORT_LAYOUT_SIZE(WsdFile::WsdInfo, 0x20);
PORT_LAYOUT_OFFSET(WsdFile::NoteInfo, hold, 0x8);
PORT_LAYOUT_OFFSET(WsdFile::NoteInfo, pitch, 0x10);
PORT_LAYOUT_SIZE(WsdFile::NoteInfo, 0x30);
PORT_LAYOUT_OFFSET(WsdFile::DataBlock, refWsd, 0xC);
PORT_LAYOUT_OFFSET(WsdFile::WaveBlock, offsetTable, 0xC);
PORT_LAYOUT_SIZE(AdpcmInfo, 0x30);
} // namespace snd_layout

// Step 8 (HOST_BUILD.md §14): KCL, PMP, EGG binary settings, game tables.
namespace step8_layout {
PORT_LAYOUT_SIZE(BE_MATH(VEC3), 0xC);
PORT_LAYOUT_SIZE(BE_MATH(_MTX34), 0x30);
PORT_LAYOUT_SIZE(RPKclPrism, 0x10);
PORT_LAYOUT_OFFSET(RPKclPrism, attribute, 0xE);
PORT_LAYOUT_SIZE(RPKclHeader, 0x3C);
PORT_LAYOUT_OFFSET(RPKclHeader, areaMin, 0x14);
PORT_LAYOUT_OFFSET(RPKclHeader, sphereRadius, 0x38);
PORT_LAYOUT_OFFSET(RPResData, numEntries, 0x10);
PORT_LAYOUT_OFFSET(RPResData, pEntries, 0x40);
PORT_LAYOUT_OFFSET(RPResData, pPoints, 0x48);
PORT_LAYOUT_SIZE(RPResEntry, 0x58);
PORT_LAYOUT_OFFSET(RPResEntry, pPath, 0x44);
PORT_LAYOUT_SIZE(RPPathRes, 0x20);
PORT_LAYOUT_OFFSET(RPPathRes, pPoints, 0x18);
PORT_LAYOUT_SIZE(RPPathPoint, 0x14);
PORT_LAYOUT_SIZE(EGG::IBinary<EGG::LightObject>::BinHeader, 0x10);
PORT_LAYOUT_SIZE(EGG::IBinary<EGG::LightObject>::Bin, 0x50);
PORT_LAYOUT_OFFSET(EGG::LightObject::BinData, pos, 0x8);
PORT_LAYOUT_OFFSET(EGG::LightObject::BinData, distAttnArg1, 0x34);
PORT_LAYOUT_OFFSET(EGG::LightManager::BinData, lights, 0x18);
PORT_LAYOUT_OFFSET(EGG::LightTexture::BinData, lightMask, 0x28);
PORT_LAYOUT_OFFSET(EGG::LightTexture::BinData, lights, 0x2C);
PORT_LAYOUT_SIZE(EGG::DrawPathBloom::BinData, 0x94);
PORT_LAYOUT_OFFSET(EGG::DrawPathBloom::BinData, mGatherScale1, 0x90);
PORT_LAYOUT_SIZE(RPTnkMapData::Map, 0x14);
PORT_LAYOUT_SIZE(RPBomQuestionParamFile, 19228);
PORT_LAYOUT_OFFSET(RPBomQuestionParamFile, mPatterns, 0x1004);
PORT_LAYOUT_OFFSET(RPBomQuestionParamFile, mComboPoints, 0x4AF4);
PORT_LAYOUT_SIZE(RPWlyLevelData, 0x40);
PORT_LAYOUT_SIZE(RPPnpLevelTable::RallyEntry, 0xC);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResNodeData, translate, 0x38);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResNodeData, modelMtx, 0x70);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResNodeData, invModelMtx, 0xA0);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResMdlInfoData, volume_max, 0x34);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResVtxPosData, max, 0x2C);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResVtxTexCoordData, max, 0x28);
PORT_LAYOUT_OFFSET(nw4r::g3d::TexMtxEffect, effectMtx, 0x4);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterDesc, translate, 0x78);
PORT_LAYOUT_OFFSET(nw4r::ef::Lighting, mPosition, 0xE);
PORT_LAYOUT_OFFSET(nw4r::ef::ParticleParameterDesc, textureTranslate, 0x50);
} // namespace step8_layout

// Animation data read by the evaluators (HOST_BUILD.md §17): the SCN0 enums
// are 4-byte BE_ENUMs, and a child curve key embeds EmitterInheritSetting.
namespace anim_layout {
PORT_LAYOUT_SIZE(nw4r::g3d::ResAnmCameraData, 0x5C);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResAnmCameraData, projType, 0x14);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResAnmCameraData, orthoHeight, 0x58);
PORT_LAYOUT_SIZE(nw4r::g3d::ResAnmLightData, 0x5C);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResAnmLightData, distFunc, 0x40);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResAnmLightData, spotFunc, 0x4C);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResAnmLightData, shininess, 0x58);
PORT_LAYOUT_SIZE(nw4r::g3d::ResAnmFogData, 0x28);
PORT_LAYOUT_OFFSET(nw4r::g3d::ResAnmFogData, type, 0x18);
PORT_LAYOUT_SIZE(nw4r::ef::EmitterInheritSetting, 0x8);
PORT_LAYOUT_OFFSET(nw4r::ef::EmitterInheritSetting, type, 0x6);
PORT_LAYOUT_SIZE(nw4r::ef::AnimCurveHeader, 0x20);
} // namespace anim_layout

} // namespace

int main() {
    std::printf("test_layout: OK (static_asserts, pointer size %zu)\n",
                sizeof(void*));
    return 0;
}
