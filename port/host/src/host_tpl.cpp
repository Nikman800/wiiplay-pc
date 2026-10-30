/**
 * TPLBind / TPLGet for the host (port/docs/POINTERS.md §3.1).
 *
 * The same algorithm as src/revolution/TPL/TPL.c: every offset slot of the
 * palette is replaced in place by "palette + offset", and the image and
 * palette data offsets are resolved once (guarded by the `unpacked` flags).
 * The slots are RES_PTR (port/host/include/revolution/TPL/TPL.h), so the
 * stored value is the big-endian emulated Wii address (M0): a bound palette
 * in host memory is the Wii image of that palette, and
 * lyt::TexMap::Set's "already bound" test (address >= 0x80000000) keeps its
 * meaning on every host.
 */
#include <types.h>

#include <revolution/OS.h>
#include <revolution/TPL.h>

#define TPL_VERSION 2142000

namespace {

template <typename T> T* ResolveTplOffset(TPLPalette* pal, const port::res_ptr<T>& slot) {
    return reinterpret_cast<T*>(reinterpret_cast<char*>(pal) + slot.raw());
}

} // namespace

extern "C" void TPLBind(TPLPalette* pal) {
    if (pal->versionNumber != TPL_VERSION) {
        OSPanic(__FILE__, __LINE__, "invalid version number for texture palette");
    }

    pal->descriptorArray = ResolveTplOffset(pal, pal->descriptorArray);

    for (u16 i = 0; i < pal->numDescriptors; i++) {
        TPLDescriptor& rDesc = pal->descriptorArray[i];

        if (rDesc.textureHeader != NULL) {
            rDesc.textureHeader = ResolveTplOffset(pal, rDesc.textureHeader);

            TPLHeader* pHeader = rDesc.textureHeader;
            if (!pHeader->unpacked) {
                pHeader->data = ResolveTplOffset(pal, pHeader->data);
                pHeader->unpacked = TRUE;
            }
        }

        if (rDesc.CLUTHeader != NULL) {
            rDesc.CLUTHeader = ResolveTplOffset(pal, rDesc.CLUTHeader);

            TPLClutHeader* pClut = rDesc.CLUTHeader;
            if (!pClut->unpacked) {
                pClut->data = ResolveTplOffset(pal, pClut->data);
                pClut->unpacked = TRUE;
            }
        }
    }
}

extern "C" TPLDescriptor* TPLGet(TPLPalette* pal, u32 id) {
    return &pal->descriptorArray[id % pal->numDescriptors];
}
