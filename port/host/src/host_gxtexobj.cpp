/**
 * Host GXInitTexObj / GXInitTexObjCI / GXInitTexObjLOD / GXInitTlutObj / GXGetTexBufferSize
 * (port/docs/HOST_BUILD.md §11, POINTERS.md g3d-2), and the GXGetTexObj* readers that
 * g3d's PAT0 application uses (§20).
 *
 * Contract: a GXTexObj (0x20) and a GXTlutObj (0xC) keep the Wii size and
 * layout (GXTexObjImpl / GXTlutObjImpl in include/revolution/GX/GXInternal.h)
 * and hold every word big-endian, exactly as the Wii CPU would store it.
 * Image and palette addresses are emulated Wii physical addresses
 * (OSCachedToPhysical of the M0 address), so a material bound by
 * g3d::ResTexPlttInfo is byte-identical to the Wii's in-memory copy. The
 * Phase 7 GX layer decodes these objects (Dolphin-style).
 *
 * The bit arithmetic is that of src/revolution/GX/GXTexture.c, computed in host
 * order on a local copy and stored big-endian. No drawing happens here.
 *
 * All weak: the GX harnesses link the decomp's GXTexture.c itself, whose
 * GXTexObjImpl/GXTlutObjImpl are big-endian too (docs/HOST_BUILD.md §26), so
 * both produce the same words.
 */
#include <types.h>

#include <revolution/GX.h>
#include <revolution/OS/OSAddress.h>

#include <port/be.h>

#include <cstring>

namespace {

// GXTexObjImpl with big-endian members (same offsets).
struct HostTexObj {
    be_u32 mode0;    // at 0x0
    be_u32 mode1;    // at 0x4
    be_u32 image0;   // at 0x8
    be_u32 image3;   // at 0xC
    be_u32 userData; // at 0x10 (void* on the Wii; always 0 after init)
    be_u32 fmt;      // at 0x14
    be_u32 tlutName; // at 0x18
    be_u16 loadCnt;  // at 0x1C
    u8 loadFmt;      // at 0x1E
    u8 flags;        // at 0x1F
};
static_assert(sizeof(HostTexObj) == sizeof(GXTexObj), "GXTexObj is 0x20");
static_assert(offsetof(HostTexObj, loadCnt) == 0x1C, "GXTexObjImpl layout");

// GXTlutObjImpl with big-endian members.
struct HostTlutObj {
    be_u32 tlut;       // at 0x0
    be_u32 loadTlut0;  // at 0x4
    be_u16 numEntries; // at 0x8
    u8 PADDING_0xA[2]; // at 0xA
};
static_assert(sizeof(HostTlutObj) == sizeof(GXTlutObj), "GXTlutObj is 0xC");

const u8 GX2HWFiltConv[8] = {0, 4, 1, 5, 2, 6, 0, 0};

// GX_BITSET (rlwimi): insert the low `size` bits of `value` at bits
// pos..pos+size-1, counting from the most significant bit.
inline u32 BitSet(u32 field, u32 pos, u32 size, u32 value) {
    u32 shift = 32 - pos - size;
    u32 mask = (size >= 32 ? 0xFFFFFFFF : ((1u << size) - 1)) << shift;
    return (field & ~mask) | ((value << shift) & mask);
}

// GX_BITGET: the `size` bits at pos..pos+size-1, counting from the most significant bit.
inline u32 BitGet(u32 field, u32 pos, u32 size) {
    return field >> (32 - pos - size) & ((1u << size) - 1);
}

// GXTexture.c's HW2GXFiltConv.
const u8 HW2GXFiltConv[8] = {0, 2, 4, 0, 1, 3, 5, 0};

inline bool IsCIFmt(GXTexFmt fmt) {
    return fmt == GX_TF_C4 || fmt == GX_TF_C8 || fmt == GX_TF_C14X2;
}

inline u32 CountLeadingZeros(u32 x) {
    return x == 0 ? 32 : __builtin_clz(x);
}

// PowerPC float -> integer conversion followed by truncation to u8 (fctiwz,
// then a byte store), for values that may be negative (LOD bias).
inline u8 F32ToU8Wii(f32 f) {
    return static_cast<u8>(static_cast<s32>(f));
}

} // namespace

extern "C" {

__attribute__((weak)) void GXInitTexObj(GXTexObj* obj, void* image, u16 w, u16 h, GXTexFmt fmt,
                  GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, GXBool mipmap) {
    u32 mode0 = 0, mode1 = 0, image0 = 0, image3 = 0;
    u8 flags = 0, loadFmt;
    u32 rowT, colT;

    mode0 = BitSet(mode0, 30, 2, wrap_s);
    mode0 = BitSet(mode0, 28, 2, wrap_t);
    mode0 = BitSet(mode0, 27, 1, 1);

    if (mipmap) {
        u32 maxLOD;
        flags |= 1;
        mode0 = BitSet(mode0, 24, 3, IsCIFmt(fmt) ? 5 : 6);
        maxLOD = 31 - CountLeadingZeros(w > h ? w : h);
        mode1 = BitSet(mode1, 16, 8, F32ToU8Wii(16.0f * maxLOD));
    } else {
        mode0 = BitSet(mode0, 24, 3, 4);
    }

    image0 = BitSet(image0, 22, 10, w - 1);
    image0 = BitSet(image0, 12, 10, h - 1);
    image0 = BitSet(image0, 8, 4, fmt & 0xF);
    image3 = BitSet(image3, 8, 24, OSCachedToPhysical(image) >> 5);

    switch (fmt & 0xF) {
    case GX_TF_I4:
    case GX_TF_C4:
        loadFmt = 1, rowT = 3, colT = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
        loadFmt = 2, rowT = 3, colT = 2;
        break;
    case GX_TF_IA8:
    case GX_TF_C14X2:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
        loadFmt = 2, rowT = 2, colT = 2;
        break;
    case GX_TF_RGBA8:
        loadFmt = 3, rowT = 2, colT = 2;
        break;
    case GX_TF_CMPR:
        loadFmt = 0, rowT = 3, colT = 3;
        break;
    default:
        loadFmt = 2, rowT = 2, colT = 2;
        break;
    }

    u32 rowC = (w + (1 << rowT) - 1) >> rowT;
    u32 colC = (h + (1 << colT) - 1) >> colT;
    flags |= 2;

    HostTexObj* t = reinterpret_cast<HostTexObj*>(obj);
    std::memset(t, 0, sizeof(*t));
    t->mode0 = mode0;
    t->mode1 = mode1;
    t->image0 = image0;
    t->image3 = image3;
    t->fmt = static_cast<u32>(fmt);
    t->loadCnt = static_cast<u16>((rowC * colC) & 0x7FFF);
    t->loadFmt = loadFmt;
    t->flags = flags;
}

__attribute__((weak)) void GXInitTexObjCI(GXTexObj* obj, void* image, u16 w, u16 h, GXTexFmt fmt,
                    GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, GXBool mipmap,
                    u32 tlut) {
    GXInitTexObj(obj, image, w, h, fmt, wrap_s, wrap_t, mipmap);
    HostTexObj* t = reinterpret_cast<HostTexObj*>(obj);
    t->flags &= ~2;
    t->tlutName = tlut;
}

__attribute__((weak)) void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt,
                     f32 min_lod, f32 max_lod, f32 lod_bias, GXBool bias_clamp,
                     GXBool do_edge_lod, GXAnisotropy max_aniso) {
    HostTexObj* t = reinterpret_cast<HostTexObj*>(obj);
    u32 mode0 = t->mode0, mode1 = t->mode1;

    if (lod_bias < -4.0f) {
        lod_bias = -4.0f;
    } else if (lod_bias >= 4.0f) {
        lod_bias = 3.99f;
    }

    mode0 = BitSet(mode0, 15, 8, F32ToU8Wii(32.0f * lod_bias));
    mode0 = BitSet(mode0, 27, 1, mag_filt == GX_LINEAR ? 1 : 0);
    mode0 = BitSet(mode0, 24, 3, GX2HWFiltConv[min_filt]);
    mode0 = BitSet(mode0, 23, 1, do_edge_lod ? 0 : 1);
    mode0 = BitSet(mode0, 14, 1, 0);
    mode0 = BitSet(mode0, 13, 1, 0);
    mode0 = BitSet(mode0, 11, 2, max_aniso);
    mode0 = BitSet(mode0, 10, 1, bias_clamp);

    if (min_lod < 0.0f) {
        min_lod = 0.0f;
    } else if (min_lod > 10.0f) {
        min_lod = 10.0f;
    }
    if (max_lod < 0.0f) {
        max_lod = 0.0f;
    } else if (max_lod > 10.0f) {
        max_lod = 10.0f;
    }

    mode1 = BitSet(mode1, 24, 8, F32ToU8Wii(16.0f * min_lod));
    mode1 = BitSet(mode1, 16, 8, F32ToU8Wii(16.0f * max_lod));

    t->mode0 = mode0;
    t->mode1 = mode1;
}

// The readers below are those of src/revolution/GX/GXTexture.c, on the big-endian words.
__attribute__((weak)) u16 GXGetTexObjWidth(const GXTexObj* obj) {
    return BitGet(reinterpret_cast<const HostTexObj*>(obj)->image0, 22, 10) + 1;
}

__attribute__((weak)) u16 GXGetTexObjHeight(const GXTexObj* obj) {
    return BitGet(reinterpret_cast<const HostTexObj*>(obj)->image0, 12, 10) + 1;
}

__attribute__((weak)) GXTexWrapMode GXGetTexObjWrapS(GXTexObj* obj) {
    return static_cast<GXTexWrapMode>(BitGet(reinterpret_cast<HostTexObj*>(obj)->mode0, 30, 2));
}

__attribute__((weak)) GXTexWrapMode GXGetTexObjWrapT(GXTexObj* obj) {
    return static_cast<GXTexWrapMode>(BitGet(reinterpret_cast<HostTexObj*>(obj)->mode0, 28, 2));
}

__attribute__((weak)) void GXGetTexObjLODAll(const GXTexObj* obj, GXTexFilter* min_filt, GXTexFilter* mag_filt,
                       f32* min_lod, f32* max_lod, f32* lod_bias, GXBool* bias_clamp,
                       GXBool* do_edge_lod, GXAnisotropy* max_aniso) {
    const HostTexObj* t = reinterpret_cast<const HostTexObj*>(obj);
    u32 mode0 = t->mode0, mode1 = t->mode1;

    *min_filt = static_cast<GXTexFilter>(HW2GXFiltConv[BitGet(mode0, 24, 3)]);
    *mag_filt = static_cast<GXTexFilter>(BitGet(mode0, 27, 1));
    *min_lod = BitGet(mode1, 24, 8) / 16.0f;
    *max_lod = BitGet(mode1, 16, 8) / 16.0f;
    s16 tmp = static_cast<s16>(BitGet(mode0, 15, 8));
    *lod_bias = static_cast<s8>(tmp) / 32.0f;
    *bias_clamp = BitGet(mode0, 10, 1);
    *do_edge_lod = !BitGet(mode0, 23, 1);
    *max_aniso = static_cast<GXAnisotropy>(BitGet(mode0, 11, 2));
}

__attribute__((weak)) void GXInitTexObjTlut(GXTexObj* obj, u32 tlut) {
    reinterpret_cast<HostTexObj*>(obj)->tlutName = tlut;
}

__attribute__((weak)) u32 GXGetTexObjTlut(GXTexObj* obj) {
    return reinterpret_cast<HostTexObj*>(obj)->tlutName;
}

__attribute__((weak)) void GXInitTlutObj(GXTlutObj* obj, void* lut, GXTlutFmt fmt, u16 n_entries) {
    HostTlutObj* t = reinterpret_cast<HostTlutObj*>(obj);
    u32 tlut = 0, loadTlut0 = 0;

    // GXTexture.c starts from the object's previous loadTlut0 bits; both
    // fields it sets cover the whole word, so the result is the same.
    tlut = BitSet(tlut, 20, 2, fmt);
    loadTlut0 = BitSet(loadTlut0, 8, 24, OSCachedToPhysical(lut) >> 5);
    loadTlut0 = BitSet(loadTlut0, 0, 8, GX_BP_REG_TMEMTLUTSRC);

    t->tlut = tlut;
    t->loadTlut0 = loadTlut0;
    t->numEntries = n_entries;
}

// GXGetTexBufferSize and its tile-shift helper, as in src/revolution/GX/GXTexture.c
// (pure arithmetic; the EGG CpuTexture/CapTexture code sizes its buffers with it).
static void HostGetTexTileShift(GXTexFmt fmt, u32* rowTileS, u32* colTileS) {
    switch (fmt) {
    case GX_TF_C4:
    case GX_TF_I4:
    case GX_TF_CMPR:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        *rowTileS = 3;
        *colTileS = 3;
        break;
    case GX_TF_C8:
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_CTF_A8:
    case GX_TF_Z8:
    case GX_CTF_RA4:
    case GX_CTF_R8:
    case GX_CTF_G8:
    case GX_CTF_B8:
    case GX_CTF_Z8M:
    case GX_CTF_Z8L:
        *rowTileS = 3;
        *colTileS = 2;
        break;
    case GX_TF_C14X2:
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_RGBA8:
    case GX_TF_Z16:
    case GX_TF_Z24X8:
    case GX_CTF_RA8:
    case GX_CTF_RG8:
    case GX_CTF_GB8:
    case GX_CTF_Z16L:
        *rowTileS = 2;
        *colTileS = 2;
        break;
    default:
        *rowTileS = *colTileS = 0;
        break;
    }
}

__attribute__((weak)) u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap, u8 max_lod) {
    u32 tileShiftX, tileShiftY, tileBytes;
    u32 bufferSize, nx, ny, level;

    HostGetTexTileShift((GXTexFmt)format, &tileShiftX, &tileShiftY);

    if (format == GX_TF_RGBA8 || format == GX_TF_Z24X8) {
        tileBytes = 64;
    } else {
        tileBytes = 32;
    }

    if (mipmap == GX_TRUE) {
        bufferSize = 0;
        for (level = 0; level < max_lod; level++) {
            nx = (u32)((width + (1 << tileShiftX) - 1) >> tileShiftX);
            ny = (u32)((height + (1 << tileShiftY) - 1) >> tileShiftY);
            bufferSize += tileBytes * (nx * ny);
            if (width == 1 && height == 1) {
                break;
            }
            width = (u16)((width > 1) ? (width >> 1) : 1);
            height = (u16)((height > 1) ? (height >> 1) : 1);
        }
    } else {
        nx = (u32)((width + (1 << tileShiftX) - 1) >> tileShiftX);
        ny = (u32)((height + (1 << tileShiftY) - 1) >> tileShiftY);
        bufferSize = tileBytes * (nx * ny);
    }

    return bufferSize;
}

} // extern "C"
