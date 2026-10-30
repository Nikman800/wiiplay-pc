#ifndef NW4R_G3D_RES_RES_ANM_SCN_H
#define NW4R_G3D_RES_RES_ANM_SCN_H
#include <nw4r/types_nw4r.h>

#include <nw4r/g3d/res/g3d_resanm.h>
#include <nw4r/g3d/res/g3d_resanmamblight.h>
#include <nw4r/g3d/res/g3d_resanmcamera.h>
#include <nw4r/g3d/res/g3d_resanmfog.h>
#include <nw4r/g3d/res/g3d_resanmlight.h>
#include <nw4r/g3d/res/g3d_rescommon.h>
#include <nw4r/g3d/res/g3d_reslightset.h>

namespace nw4r {
namespace g3d {

struct ResAnmScnInfoData {
    be_u16 numFrame;                 // at 0x0
    be_u16 numSpecularLight;         // at 0x2
    BE_ENUM(AnmPolicy, u32) policy;             // at 0x4
    be_u16 numResLightSetData;       // at 0x8
    be_u16 numResAnmAmbLightData;    // at 0xA
    be_u16 numResAnmLightData;       // at 0xC
    be_u16 numResAnmFogData;         // at 0xE
    be_u16 numResAnmCameraData;      // at 0x10
    u8 PADDING_0x12[0x14 - 0x12]; // at 0x12
};

struct ResAnmScnData {
    ResBlockHeaderData header;     // at 0x0
    be_u32 revision;                  // at 0x8
    be_s32 toResFileData;             // at 0xC
    be_s32 toScnTopLevelDic;          // at 0x10
    be_s32 toResLightSetDataArray;    // at 0x14
    be_s32 toResAnmAmbLightDataArray; // at 0x18
    be_s32 toResAnmLightDataArray;    // at 0x1C
    be_s32 toResAnmFogDataArray;      // at 0x20
    be_s32 toResAnmCameraDataArray;   // at 0x24
    be_s32 name;                      // at 0x28
    be_s32 original_path;             // at 0x2C
    ResAnmScnInfoData info;        // at 0x30
};

class ResAnmScn : public ResCommon<ResAnmScnData> {
public:
    static const u32 SIGNATURE = FOURCC('S', 'C', 'N', '0');
    static const int REVISION = 4;

public:
    NW4R_G3D_RESOURCE_FUNC_DEF(ResAnmScn);

    bool Bind(const ResAnmScn scene);

    bool Bind() {
        return Bind(*this);
    }

    u32 GetRevision() const {
        return ref().revision;
    }

    bool CheckRevision() const {
        return GetRevision() == REVISION;
    }

    bool HasResAnmAmbLight() const;
    bool HasResAnmLight() const;

    ResLightSet GetResLightSet(int idx) const;
    ResLightSet GetResLightSet(u32 idx) const;
    u32 GetResLightSetNumEntries() const;

    ResAnmAmbLight GetResAnmAmbLight(const ResName name) const;
    ResAnmAmbLight GetResAnmAmbLight(int idx) const;
    ResAnmAmbLight GetResAnmAmbLight(u32 idx) const;

    ResAnmLight GetResAnmLight(const ResName name) const;
    ResAnmLight GetResAnmLight(int idx) const;
    ResAnmLight GetResAnmLight(u32 idx) const;

    ResLightSet GetResLightSetByRefNumber(u32 refNumber) const;
#if defined(VERSION_RSPE01_00)
    ResLightSet GetResLightSetByRefNumber(int refNumber) const;
#endif
    ResAnmAmbLight GetResAnmAmbLightByRefNumber(u32 refNumber) const;
    ResAnmLight GetResAnmLightByRefNumber(u32 refNumber) const;
    ResAnmFog GetResAnmFogByRefNumber(u32 refNumber) const;
    ResAnmCamera GetResAnmCameraByRefNumber(u32 refNumber) const;

    u16 GetResLightSetMaxRefNumber() const {
        return ref().info.numResLightSetData;
    }

    u16 GetResAnmAmbLightMaxRefNumber() const {
        return ref().info.numResAnmAmbLightData;
    }

    u16 GetResAnmLightMaxRefNumber() const {
        return ref().info.numResAnmLightData;
    }

    u16 GetResAnmFogMaxRefNumber() const {
        return ref().info.numResAnmFogData;
    }

    u16 GetResAnmCameraMaxRefNumber() const {
        return ref().info.numResAnmCameraData;
    }
};

} // namespace g3d
} // namespace nw4r

#endif
