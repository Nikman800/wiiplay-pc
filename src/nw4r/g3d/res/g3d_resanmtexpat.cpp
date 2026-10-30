#include <nw4r/g3d.h>
#include <nw4r/math.h>
#include <nw4r/ut.h>

namespace nw4r {
namespace g3d {

#if defined(PORT_HOST)
// toResTexArray / toResPlttArray hold one 4-byte pointer slot per entry
// (port/docs/POINTERS.md g3d-4); ResTex/ResPltt are 8 bytes on a 64-bit host.
template <typename TRes, typename TData>
struct PortPat0Slot : port::res_ptr<TData> {
    PortPat0Slot& operator=(const TRes& rRes) {
        this->set(const_cast<TData*>(rRes.ptr()));
        return *this;
    }
};
typedef PortPat0Slot<ResTex, ResTexData> PAT0_TEX_SLOT;
typedef PortPat0Slot<ResPltt, ResPlttData> PAT0_PLTT_SLOT;
#else
typedef ResTex PAT0_TEX_SLOT;
typedef ResPltt PAT0_PLTT_SLOT;
#endif

ResFile ResAnmTexPat::GetParent() const {
    return ofs_to_obj<ResFile>(ref().toResFileData);
}

namespace {

const ResAnmTexPatFrmData* SearchFrame(const ResAnmTexPatAnmData* pAnmData,
                                       f32 frame) {
    const ResAnmTexPatFrmData& rFirst = pAnmData->frameValues[0];
    const ResAnmTexPatFrmData& rLast =
        pAnmData->frameValues[pAnmData->numFrameValues - 1];

    if (frame <= rFirst.frame) {
        return &rFirst;
    }

    if (rLast.frame <= frame) {
        return &rLast;
    }

    f32 frameOffset = frame - rFirst.frame;
    f32 numKeyFrame = math::U16ToF32(pAnmData->numFrameValues);

    f32 fEstimate = frameOffset * numKeyFrame * pAnmData->invKeyFrameRange;
    u16 iEstimate = math::F32ToU16(fEstimate);

    const ResAnmTexPatFrmData* pFrmData = &pAnmData->frameValues[iEstimate];

    if (frame < pFrmData->frame) {
        do {
            pFrmData--;
        } while (frame < pFrmData->frame);
    } else {
        do {
            pFrmData++;
        } while (pFrmData->frame <= frame);

        pFrmData--;
    }

    return pFrmData;
}

inline ResName GetResNameFromOffsetArray(be_s32* pStringArray, int idx) {
    s32 offset = pStringArray[idx];
    return NW4R_G3D_OFS_TO_RESNAME(pStringArray, offset);
}

} // namespace

void ResAnmTexPat::GetAnmResult(TexPatAnmResult* pResult, u32 idx,
                                f32 frame) const {
    (void)GetParent(); // unused

    const ResAnmTexPatMatData* pMatData = GetMatAnm(idx);
    const ResAnmTexPatMatData::AnmData* pAnmDataImpl = pMatData->anms;
    u32 flags = pMatData->flags;

    const ResAnmTexPatData& r = ref();
    const PAT0_TEX_SLOT* pResTexArray = ofs_to_ptr<PAT0_TEX_SLOT>(r.toResTexArray);
    const PAT0_PLTT_SLOT* pResPlttArray = ofs_to_ptr<PAT0_PLTT_SLOT>(r.toResPlttArray);

    pResult->bTexExist = 0;
    pResult->bPlttExist = 0;

    for (int i = 0; i < TexPatAnmResult::NUM_OF_ANMS;
         flags >>= ResAnmTexPatMatData::NUM_OF_FLAGS, i++) {

        if (!(flags & ResAnmTexPatMatData::FLAG_ANM_EXISTS)) {
            continue;
        }

        int texIndex, plttIndex;
        if (flags & ResAnmTexPatMatData::FLAG_ANM_CONST) {
            texIndex = pAnmDataImpl->constValue.texIndex;
            plttIndex = pAnmDataImpl->constValue.plttIndex;
        } else {
            const ResAnmTexPatAnmData* pAnmData =
                static_cast<const ResAnmTexPatAnmData*>(ut::AddOffsetToPtr(
                    pMatData, pAnmDataImpl->toResAnmTexPatAnmData));

            const ResAnmTexPatFrmData* pFrmData = SearchFrame(pAnmData, frame);
            texIndex = pFrmData->texIndex;
            plttIndex = pFrmData->plttIndex;
        }

        u32 targetBit = 1 << i;

        if (flags & ResAnmTexPatMatData::FLAG_ANM_TEX) {
            ResTex tex(pResTexArray[texIndex]);

            if (tex.IsValid()) {
                pResult->tex[i] = tex;
                pResult->bTexExist |= targetBit;
            }
        }

        if (flags & ResAnmTexPatMatData::FLAG_ANM_PLTT) {
            ResPltt pltt(pResPlttArray[plttIndex]);

            if (pltt.IsValid()) {
                pResult->pltt[i] = pltt;
                pResult->bPlttExist |= targetBit;
            }
        }

        pAnmDataImpl++;
    }
}

bool ResAnmTexPat::Bind(const ResFile file) {
    const ResAnmTexPatInfoData& rInfoData = ref().info;
    int numTexture = rInfoData.numTexture;
    int numPalette = rInfoData.numPalette;

    be_s32* pTexNameArray = ofs_to_ptr<be_s32>(ref().toTexNameArray);
    be_s32* pPlttNameArray = ofs_to_ptr<be_s32>(ref().toPlttNameArray);

    PAT0_TEX_SLOT* pResTexArray = ofs_to_ptr<PAT0_TEX_SLOT>(ref().toResTexArray);
    PAT0_PLTT_SLOT* pResPlttArray = ofs_to_ptr<PAT0_PLTT_SLOT>(ref().toResPlttArray);

    int numBound = 0;

    for (int i = 0; i < numTexture; i++) {
        ResTex tex(pResTexArray[i]);

        if (tex.IsValid()) {
            numBound++;
        } else {
            ResTex found =
                file.GetResTex(GetResNameFromOffsetArray(pTexNameArray, i));

            if (found.IsValid()) {
                pResTexArray[i] = found;
                numBound++;
            }
        }
    }

    for (int i = 0; i < numPalette; i++) {
        ResPltt pltt(pResPlttArray[i]);

        if (pltt.IsValid()) {
            numBound++;
        } else {
            ResPltt found =
                file.GetResPltt(GetResNameFromOffsetArray(pPlttNameArray, i));

            if (found.IsValid()) {
                pResPlttArray[i] = found;
                numBound++;
            }
        }
    }

    return numBound == numTexture + numPalette;
}

void ResAnmTexPat::Release() {
    const ResAnmTexPatInfoData& rInfoData = ref().info;
    int numTexture = rInfoData.numTexture;
    int numPalette = rInfoData.numPalette;

    PAT0_TEX_SLOT* pResTexArray = ofs_to_ptr<PAT0_TEX_SLOT>(ref().toResTexArray);
    PAT0_PLTT_SLOT* pResPlttArray = ofs_to_ptr<PAT0_PLTT_SLOT>(ref().toResPlttArray);

    for (int i = 0; i < numTexture; i++) {
        pResTexArray[i] = ResTex(NULL);
    }

    for (int i = 0; i < numPalette; i++) {
        pResPlttArray[i] = ResPltt(NULL);
    }
}

} // namespace g3d
} // namespace nw4r
