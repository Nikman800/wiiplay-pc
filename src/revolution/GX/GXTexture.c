#include <revolution/GX.h>
#include <string.h>

#include <port/resptr.h>
#if defined(PORT_HOST)
#include <revolution/OS/OSAddress.h>
#endif

#define GX_TEXOBJ(obj) ((GXTexObjImpl*)(obj))
#define GX_TLUTOBJ(obj) ((GXTlutObjImpl*)(obj))
#define GX_TEXREGION(obj) ((GXTexRegionImpl*)(obj))
#define GX_TLUTREGION(obj) ((GXTlutRegionImpl*)(obj))

#define IS_CI_TEXFMT(fmt) ((fmt) == GX_TF_C4 || (fmt) == GX_TF_C8 || (fmt) == GX_TF_C14X2)

#define GX_PHY_ADDR(a) (PORT_PTR_TO_U32(a) & 0x3FFFFFFF)

u8 GXTexMode0Ids[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
u8 GXTexMode1Ids[8] = {0x84, 0x85, 0x86, 0x87, 0xA4, 0xA5, 0xA6, 0xA7};
u8 GXTexImage0Ids[8] = {0x88, 0x89, 0x8A, 0x8B, 0xA8, 0xA9, 0xAA, 0xAB};
u8 GXTexImage1Ids[8] = {0x8C, 0x8D, 0x8E, 0x8F, 0xAC, 0xAD, 0xAE, 0xAF};
u8 GXTexImage2Ids[8] = {0x90, 0x91, 0x92, 0x93, 0xB0, 0xB1, 0xB2, 0xB3};
u8 GXTexImage3Ids[8] = {0x94, 0x95, 0x96, 0x97, 0xB4, 0xB5, 0xB6, 0xB7};
u8 GXTexTlutIds[8] = {0x98, 0x99, 0x9A, 0x9B, 0xB8, 0xB9, 0xBA, 0xBB};

// GXTexFilter -> hardware min filter
u8 GX2HWFiltConv[8] = {0, 4, 1, 5, 2, 6, 0, 0};
// hardware min filter -> GXTexFilter
u8 HW2GXFiltConv[8] = {0, 2, 4, 0, 1, 3, 5, 0};

static void __GXGetTexTileShift(GXTexFmt fmt, u32* rowTileS, u32* colTileS) {
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

u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap,
                       u8 max_lod) {
    u32 tileShiftX, tileShiftY, tileBytes;
    u32 bufferSize, nx, ny, level;

    __GXGetTexTileShift((GXTexFmt)format, &tileShiftX, &tileShiftY);

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

void __GetImageTileCount(GXTexFmt fmt, u16 wd, u16 ht, u32* rowTiles,
                         u32* colTiles, u32* cmpTiles) {
    u32 texRowShift, texColShift;

    __GXGetTexTileShift(fmt, &texRowShift, &texColShift);

    if (wd == 0) {
        wd = 1;
    }
    if (ht == 0) {
        ht = 1;
    }

    *rowTiles = (u32)((wd + (1 << texRowShift) - 1) >> texRowShift);
    *colTiles = (u32)((ht + (1 << texColShift) - 1) >> texColShift);
    *cmpTiles = (fmt == GX_TF_RGBA8 || fmt == GX_TF_Z24X8) ? 2 : 1;
}

void GXInitTexObj(GXTexObj* obj, void* image, u16 w, u16 h, GXTexFmt fmt,
                  GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, GXBool mipmap) {
    u32 imageBase;
    u32 maxLOD;
    u16 rowT, colT;
    u32 rowC, colC;
    GXTexObjImpl* t = GX_TEXOBJ(obj);

    memset(t, 0, sizeof(GXTexObjImpl));

    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 30, 2, wrap_s));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 28, 2, wrap_t));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 27, 1, 1));

    if (mipmap) {
        u8 lmax;

        t->flags |= 1;
        if (IS_CI_TEXFMT(fmt)) {
            BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 24, 3, 5));
        } else {
            BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 24, 3, 6));
        }

        if (w > h) {
            maxLOD = 31 - __cntlzw(w);
        } else {
            maxLOD = 31 - __cntlzw(h);
        }

        lmax = (u8)(16.0f * maxLOD);
        BE_SET(t->mode1, GX_BITSET(BE_GET(t->mode1), 16, 8, lmax));
    } else {
        BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 24, 3, 4));
    }

    BE_SET(t->fmt, fmt);
    BE_SET(t->image0, GX_BITSET(BE_GET(t->image0), 22, 10, w - 1));
    BE_SET(t->image0, GX_BITSET(BE_GET(t->image0), 12, 10, h - 1));
    BE_SET(t->image0, GX_BITSET(BE_GET(t->image0), 8, 4, fmt & 0xF));

    imageBase = GX_PHY_ADDR(image) >> 5;
    BE_SET(t->image3, GX_BITSET(BE_GET(t->image3), 8, 24, imageBase));

    switch (fmt & 0xF) {
    case GX_TF_I4:
    case GX_TF_C4:
        t->loadFmt = 1;
        rowT = 3;
        colT = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
        t->loadFmt = 2;
        rowT = 3;
        colT = 2;
        break;
    case GX_TF_IA8:
    case GX_TF_C14X2:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
        t->loadFmt = 2;
        rowT = 2;
        colT = 2;
        break;
    case GX_TF_RGBA8:
        t->loadFmt = 3;
        rowT = 2;
        colT = 2;
        break;
    case GX_TF_CMPR:
        t->loadFmt = 0;
        rowT = 3;
        colT = 3;
        break;
    default:
        t->loadFmt = 2;
        rowT = 2;
        colT = 2;
        break;
    }

    rowC = (w + (1 << rowT) - 1) >> rowT;
    colC = (h + (1 << colT) - 1) >> colT;
    BE_SET(t->loadCnt, (rowC * colC) & 0x7FFF);
    t->flags |= 2;
}

void GXInitTexObjCI(GXTexObj* obj, void* image, u16 w, u16 h, GXTexFmt fmt,
                    GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, GXBool mipmap,
                    u32 tlut) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);

    GXInitTexObj(obj, image, w, h, fmt, wrap_s, wrap_t, mipmap);

    t->flags &= ~2;
    BE_SET(t->tlutName, tlut);
}

void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt,
                     f32 min_lod, f32 max_lod, f32 lod_bias, GXBool bias_clamp,
                     GXBool do_edge_lod, GXAnisotropy max_aniso) {
    u8 lbias;
    u8 lmin, lmax;
    GXTexObjImpl* t = GX_TEXOBJ(obj);

    if (lod_bias < -4.0f) {
        lod_bias = -4.0f;
    } else if (lod_bias >= 4.0f) {
        lod_bias = 3.99f;
    }

    lbias = (u8)(32.0f * lod_bias);
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 15, 8, lbias));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 27, 1, mag_filt == GX_LINEAR ? 1 : 0));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 24, 3, GX2HWFiltConv[min_filt]));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 23, 1, do_edge_lod ? 0 : 1));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 14, 1, 0));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 13, 1, 0));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 11, 2, max_aniso));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 10, 1, bias_clamp));

    if (min_lod < 0.0f) {
        min_lod = 0.0f;
    } else if (min_lod > 10.0f) {
        min_lod = 10.0f;
    }
    lmin = (u8)(16.0f * min_lod);

    if (max_lod < 0.0f) {
        max_lod = 0.0f;
    } else if (max_lod > 10.0f) {
        max_lod = 10.0f;
    }
    lmax = (u8)(16.0f * max_lod);

    BE_SET(t->mode1, GX_BITSET(BE_GET(t->mode1), 24, 8, lmin));
    BE_SET(t->mode1, GX_BITSET(BE_GET(t->mode1), 16, 8, lmax));
}

void GXInitTexObjWrapMode(GXTexObj* obj, GXTexWrapMode wrap_s,
                          GXTexWrapMode wrap_t) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);

    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 30, 2, wrap_s));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 28, 2, wrap_t));
}

void GXInitTexObjTlut(GXTexObj* obj, u32 tlut) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);
    BE_SET(t->tlutName, tlut);
}

void GXInitTexObjFilter(GXTexObj* obj, GXTexFilter min_filt,
                        GXTexFilter mag_filt) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);

    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 27, 1, mag_filt == GX_LINEAR ? 1 : 0));
    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 24, 3, GX2HWFiltConv[min_filt]));
}

void GXGetTexObjAll(const GXTexObj* obj, void** image_ptr, u16* width,
                    u16* height, GXTexFmt* format, GXTexWrapMode* wrap_s,
                    GXTexWrapMode* wrap_t, GXBool* mipmap) {
    const GXTexObjImpl* t = (const GXTexObjImpl*)obj;

#if defined(PORT_HOST)
    // The physical address as a host pointer (OSPhysicalToCached)
    *image_ptr = OSPhysicalToCached(GX_BITGET(BE_GET(t->image3), 8, 24) << 5);
#else
    *image_ptr = (void*)(GX_BITGET(t->image3, 8, 24) << 5);
#endif
    *width = GX_BITGET(BE_GET(t->image0), 22, 10) + 1;
    *height = GX_BITGET(BE_GET(t->image0), 12, 10) + 1;
    *format = BE_GET(t->fmt);
    *wrap_s = (GXTexWrapMode)GX_BITGET(BE_GET(t->mode0), 30, 2);
    *wrap_t = (GXTexWrapMode)GX_BITGET(BE_GET(t->mode0), 28, 2);
    *mipmap = (t->flags & 1) == 1;
}

u16 GXGetTexObjWidth(const GXTexObj* obj) {
    const GXTexObjImpl* t = GX_TEXOBJ(obj);
    return GX_BITGET(BE_GET(t->image0), 22, 10) + 1;
}

u16 GXGetTexObjHeight(const GXTexObj* obj) {
    const GXTexObjImpl* t = GX_TEXOBJ(obj);
    return GX_BITGET(BE_GET(t->image0), 12, 10) + 1;
}

GXTexFmt GXGetTexObjFmt(const GXTexObj* obj) {
    const GXTexObjImpl* t = GX_TEXOBJ(obj);
    return BE_GET(t->fmt);
}

GXTexWrapMode GXGetTexObjWrapS(GXTexObj* obj) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);
    return (GXTexWrapMode)GX_BITGET(BE_GET(t->mode0), 30, 2);
}

GXTexWrapMode GXGetTexObjWrapT(GXTexObj* obj) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);
    return (GXTexWrapMode)GX_BITGET(BE_GET(t->mode0), 28, 2);
}

GXBool GXGetTexObjMipMap(const GXTexObj* obj) {
    const GXTexObjImpl* t = GX_TEXOBJ(obj);
    return (t->flags & 1) == 1;
}

void GXGetTexObjLODAll(const GXTexObj* obj, GXTexFilter* min_filt,
                       GXTexFilter* mag_filt, f32* min_lod, f32* max_lod,
                       f32* lod_bias, GXBool* bias_clamp, GXBool* do_edge_lod,
                       GXAnisotropy* max_aniso) {
    s16 tmp;
    const GXTexObjImpl* t = (const GXTexObjImpl*)obj;

    *min_filt = (GXTexFilter)HW2GXFiltConv[GX_BITGET(BE_GET(t->mode0), 24, 3)];
    *mag_filt = (GXTexFilter)GX_BITGET(BE_GET(t->mode0), 27, 1);
    *min_lod = GX_BITGET(BE_GET(t->mode1), 24, 8) / 16.0f;
    *max_lod = GX_BITGET(BE_GET(t->mode1), 16, 8) / 16.0f;
    tmp = (s16)GX_BITGET(BE_GET(t->mode0), 15, 8);
    *lod_bias = (s8)tmp / 32.0f;
    *bias_clamp = GX_BITGET(BE_GET(t->mode0), 10, 1);
    *do_edge_lod = !GX_BITGET(BE_GET(t->mode0), 23, 1);
    *max_aniso = (GXAnisotropy)GX_BITGET(BE_GET(t->mode0), 11, 2);
}

u32 GXGetTexObjTlut(GXTexObj* obj) {
    GXTexObjImpl* t = GX_TEXOBJ(obj);
    return BE_GET(t->tlutName);
}

void GXLoadTexObjPreLoaded(const GXTexObj* obj, const GXTexRegion* region,
                           GXTexMapID id) {
    GXTlutRegionImpl* tlr;
    GXTexObjImpl* t = GX_TEXOBJ(obj);
    GXTexRegionImpl* r = GX_TEXREGION(region);

    BE_SET(t->mode0, GX_BITSET(BE_GET(t->mode0), 0, 8, GXTexMode0Ids[id]));
    BE_SET(t->mode1, GX_BITSET(BE_GET(t->mode1), 0, 8, GXTexMode1Ids[id]));
    BE_SET(t->image0, GX_BITSET(BE_GET(t->image0), 0, 8, GXTexImage0Ids[id]));
    r->image1 = GX_BITSET(r->image1, 0, 8, GXTexImage1Ids[id]);
    r->image2 = GX_BITSET(r->image2, 0, 8, GXTexImage2Ids[id]);
    BE_SET(t->image3, GX_BITSET(BE_GET(t->image3), 0, 8, GXTexImage3Ids[id]));

    GX_BP_LOAD_REG(BE_GET(t->mode0));
    GX_BP_LOAD_REG(BE_GET(t->mode1));
    GX_BP_LOAD_REG(BE_GET(t->image0));
    GX_BP_LOAD_REG(r->image1);
    GX_BP_LOAD_REG(r->image2);
    GX_BP_LOAD_REG(BE_GET(t->image3));

    if (!(t->flags & 2)) {
        tlr = GX_TLUTREGION(gxdt->tlutRegionCallback(BE_GET(t->tlutName)));
        BE_SET(tlr->tlutObj.tlut,
               GX_BITSET(BE_GET(tlr->tlutObj.tlut), 0, 8, GXTexTlutIds[id]));
        GX_BP_LOAD_REG(BE_GET(tlr->tlutObj.tlut));
    }

    gxdt->tImage0[id] = BE_GET(t->image0);
    gxdt->tMode0[id] = BE_GET(t->mode0);
    gxdt->gxDirtyFlags |= GX_DIRTY_SU_TEX;
    gxdt->lastWriteWasXF = FALSE;
}

void GXLoadTexObj(const GXTexObj* obj, GXTexMapID id) {
    GXTexRegion* r = gxdt->texRegionCallback(obj, id);
    GXLoadTexObjPreLoaded(obj, r, id);
}

void GXInitTlutObj(GXTlutObj* obj, void* lut, GXTlutFmt fmt, u16 n_entries) {
    GXTlutObjImpl* t = GX_TLUTOBJ(obj);

    BE_SET(t->tlut, 0);
    BE_SET(t->tlut, GX_BITSET(BE_GET(t->tlut), 20, 2, fmt));
    BE_SET(t->loadTlut0, GX_BITSET(BE_GET(t->loadTlut0), 8, 24, GX_PHY_ADDR(lut) >> 5));
    BE_SET(t->loadTlut0, GX_BITSET(BE_GET(t->loadTlut0), 0, 8, GX_BP_REG_TMEMTLUTSRC));
    BE_SET(t->numEntries, n_entries);
}

void GXLoadTlut(const GXTlutObj* obj, u32 tlut) {
    GXTlutRegionImpl* r;
    u32 tlut_offset;
    GXTlutObjImpl* t = GX_TLUTOBJ(obj);

    r = GX_TLUTREGION(gxdt->tlutRegionCallback(tlut));

    __GXFlushTextureState();
    GX_BP_LOAD_REG(BE_GET(t->loadTlut0));
    GX_BP_LOAD_REG(r->loadTlut1);
    __GXFlushTextureState();

    tlut_offset = GX_BITGET(r->loadTlut1, 22, 10);
    BE_SET(t->tlut, GX_BITSET(BE_GET(t->tlut), 22, 10, tlut_offset));
    r->tlutObj = *t;
}

void GXInitTexCacheRegion(GXTexRegion* region, GXBool is_32b_mipmap,
                          u32 tmem_even, GXTexCacheSize size_even,
                          u32 tmem_odd, GXTexCacheSize size_odd) {
    u32 widthExp2;
    GXTexRegionImpl* t = GX_TEXREGION(region);

    switch (size_even) {
    case GX_TEXCACHE_32K:
        widthExp2 = 3;
        break;
    case GX_TEXCACHE_128K:
        widthExp2 = 4;
        break;
    case GX_TEXCACHE_512K:
        widthExp2 = 5;
        break;
    default:
        break;
    }

    t->image1 = 0;
    t->image1 = GX_BITSET(t->image1, 17, 15, tmem_even >> 5);
    t->image1 = GX_BITSET(t->image1, 14, 3, widthExp2);
    t->image1 = GX_BITSET(t->image1, 11, 3, widthExp2);
    t->image1 = GX_BITSET(t->image1, 10, 1, 0);

    switch (size_odd) {
    case GX_TEXCACHE_32K:
        widthExp2 = 3;
        break;
    case GX_TEXCACHE_128K:
        widthExp2 = 4;
        break;
    case GX_TEXCACHE_512K:
        widthExp2 = 5;
        break;
    case GX_TEXCACHE_NONE:
        widthExp2 = 0;
        break;
    default:
        break;
    }

    t->image2 = 0;
    t->image2 = GX_BITSET(t->image2, 17, 15, tmem_odd >> 5);
    t->image2 = GX_BITSET(t->image2, 14, 3, widthExp2);
    t->image2 = GX_BITSET(t->image2, 11, 3, widthExp2);

    t->is32bMipmap = is_32b_mipmap;
    t->isCached = TRUE;
}

void GXInitTlutRegion(GXTlutRegion* region, u32 tmem_addr,
                      GXTlutSize tlut_size) {
    GXTlutRegionImpl* t = GX_TLUTREGION(region);

    t->loadTlut1 = 0;
    tmem_addr -= 0x80000;
    t->loadTlut1 = GX_BITSET(t->loadTlut1, 22, 10, tmem_addr >> 9);
    t->loadTlut1 = GX_BITSET(t->loadTlut1, 11, 11, tlut_size);
    t->loadTlut1 = GX_BITSET(t->loadTlut1, 0, 8, GX_BP_REG_TMEMTLUTDST);
}

void GXInvalidateTexAll(void) {
    __GXFlushTextureState();
    GX_BP_LOAD_REG(0x66001000);
    GX_BP_LOAD_REG(0x66001100);
    __GXFlushTextureState();
}

GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback callback) {
    GXTexRegionCallback old = gxdt->texRegionCallback;
    gxdt->texRegionCallback = callback;
    return old;
}

GXTlutRegionCallback GXSetTlutRegionCallback(GXTlutRegionCallback callback) {
    GXTlutRegionCallback old = gxdt->tlutRegionCallback;
    gxdt->tlutRegionCallback = callback;
    return old;
}

void GXSetTexCoordScaleManually(GXTexCoordID coord, GXBool enable, u16 ss,
                                u16 ts) {
    gxdt->tcsManEnab = gxdt->tcsManEnab & ~(1 << coord) | (enable << coord);

    if (enable) {
        gxdt->txcRegs[coord] = GX_BITSET(gxdt->txcRegs[coord], 16, 16, (u16)(ss - 1));
        gxdt->suTs1[coord] = GX_BITSET(gxdt->suTs1[coord], 16, 16, (u16)(ts - 1));

        GX_BP_LOAD_REG(gxdt->txcRegs[coord]);
        GX_BP_LOAD_REG(gxdt->suTs1[coord]);
        gxdt->lastWriteWasXF = FALSE;
    }
}

void GXSetTexCoordCylWrap(GXTexCoordID coord, GXBool s_enable,
                          GXBool t_enable) {
    gxdt->txcRegs[coord] = GX_BITSET(gxdt->txcRegs[coord], 15, 1, s_enable);
    gxdt->suTs1[coord] = GX_BITSET(gxdt->suTs1[coord], 15, 1, t_enable);

    if (gxdt->tcsManEnab & (1 << coord)) {
        GX_BP_LOAD_REG(gxdt->txcRegs[coord]);
        GX_BP_LOAD_REG(gxdt->suTs1[coord]);
        gxdt->lastWriteWasXF = FALSE;
    }
}

void __SetSURegs(u32 tmap, u32 tcoord) DECOMP_DONT_INLINE {
    u32 w, h;
    GXBool s_bias, t_bias;

    w = gxdt->tImage0[tmap] & 0x3FF;
    h = (gxdt->tImage0[tmap] & 0xFFC00) >> 10;
    gxdt->txcRegs[tcoord] = GX_BITSET(gxdt->txcRegs[tcoord], 16, 16, w);
    gxdt->suTs1[tcoord] = GX_BITSET(gxdt->suTs1[tcoord], 16, 16, h);

    s_bias = (gxdt->tMode0[tmap] & 0x3) == GX_REPEAT;
    t_bias = ((gxdt->tMode0[tmap] & 0xC) >> 2) == GX_REPEAT;
    gxdt->txcRegs[tcoord] = GX_BITSET(gxdt->txcRegs[tcoord], 15, 1, s_bias);
    gxdt->suTs1[tcoord] = GX_BITSET(gxdt->suTs1[tcoord], 15, 1, t_bias);

    GX_BP_LOAD_REG(gxdt->txcRegs[tcoord]);
    GX_BP_LOAD_REG(gxdt->suTs1[tcoord]);
    gxdt->lastWriteWasXF = FALSE;
}

void __GXSetSUTexRegs(void) {
    u32 nStages, nIndStages, i, map, tmap, coord;
    u32* ptref;

    if (gxdt->tcsManEnab == 0xFF) {
        return;
    }

    nStages = ((gxdt->genMode & 0x3C00) >> 10) + 1;
    nIndStages = (gxdt->genMode & 0x70000) >> 16;

    for (i = 0; i < nIndStages; i++) {
        switch (i + GX_INDTEXSTAGE0) {
        case GX_INDTEXSTAGE0:
            tmap = gxdt->ras1_iref & 0x7;
            coord = (gxdt->ras1_iref & 0x38) >> 3;
            break;
        case GX_INDTEXSTAGE1:
            tmap = (gxdt->ras1_iref & 0x1C0) >> 6;
            coord = (gxdt->ras1_iref & 0xE00) >> 9;
            break;
        case GX_INDTEXSTAGE2:
            tmap = (gxdt->ras1_iref & 0x7000) >> 12;
            coord = (gxdt->ras1_iref & 0x38000) >> 15;
            break;
        case GX_INDTEXSTAGE3:
            tmap = (gxdt->ras1_iref & 0x1C0000) >> 18;
            coord = (gxdt->ras1_iref & 0xE00000) >> 21;
            break;
        }

        if (!(gxdt->tcsManEnab & (1 << coord))) {
            __SetSURegs(tmap, coord);
        }
    }

    for (i = 0; i < nStages; i++) {
        ptref = &gxdt->tref[i / 2];
        map = gxdt->texmapId[i];
        tmap = map & ~0x100;

        if (i & 1) {
            coord = (*ptref & 0x38000) >> 15;
        } else {
            coord = (*ptref & 0x38) >> 3;
        }

        if (tmap != GX_TEXMAP_NULL && !(gxdt->tcsManEnab & (1 << coord)) &&
            (gxdt->tevTcEnab & (1 << i))) {
            __SetSURegs(tmap, coord);
        }
    }
}

#define TX_SETIMAGE1(tmem, w, h, type, rid)                                    \
    ((u32)(tmem) | (u32)(w) << 15 | (u32)(h) << 18 | (u32)(type) << 21 |       \
     (u32)(rid) << 24)
#define TX_SETIMAGE2(tmem, w, h, rid)                                          \
    ((u32)(tmem) | (u32)(w) << 15 | (u32)(h) << 18 | (u32)(rid) << 24)

void __GXSetTmemConfig(u32 config) {
    switch (config) {
    case 2:
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x00000 >> 5, 3, 3, 0, 0x8C));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x80000 >> 5, 3, 3, 0x90));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x10000 >> 5, 3, 3, 0, 0x8D));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x90000 >> 5, 3, 3, 0x91));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x20000 >> 5, 3, 3, 0, 0x8E));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xA0000 >> 5, 3, 3, 0x92));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x30000 >> 5, 3, 3, 0, 0x8F));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xB0000 >> 5, 3, 3, 0x93));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x40000 >> 5, 3, 3, 0, 0xAC));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x88000 >> 5, 3, 3, 0xB0));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x50000 >> 5, 3, 3, 0, 0xAD));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x98000 >> 5, 3, 3, 0xB1));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x60000 >> 5, 3, 3, 0, 0xAE));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xA8000 >> 5, 3, 3, 0xB2));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x70000 >> 5, 3, 3, 0, 0xAF));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xB8000 >> 5, 3, 3, 0xB3));
        break;
    case 1:
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x00000 >> 5, 3, 3, 0, 0x8C));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x80000 >> 5, 3, 3, 0x90));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x10000 >> 5, 3, 3, 0, 0x8D));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x90000 >> 5, 3, 3, 0x91));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x20000 >> 5, 3, 3, 0, 0x8E));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xA0000 >> 5, 3, 3, 0x92));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x30000 >> 5, 3, 3, 0, 0x8F));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xB0000 >> 5, 3, 3, 0x93));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x40000 >> 5, 3, 3, 0, 0xAC));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xC0000 >> 5, 3, 3, 0xB0));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x50000 >> 5, 3, 3, 0, 0xAD));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xD0000 >> 5, 3, 3, 0xB1));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x60000 >> 5, 3, 3, 0, 0xAE));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xE0000 >> 5, 3, 3, 0xB2));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x70000 >> 5, 3, 3, 0, 0xAF));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xF0000 >> 5, 3, 3, 0xB3));
        break;
    case 0:
    default:
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x00000 >> 5, 3, 3, 0, 0x8C));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x80000 >> 5, 3, 3, 0x90));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x08000 >> 5, 3, 3, 0, 0x8D));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x88000 >> 5, 3, 3, 0x91));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x10000 >> 5, 3, 3, 0, 0x8E));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x90000 >> 5, 3, 3, 0x92));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x18000 >> 5, 3, 3, 0, 0x8F));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0x98000 >> 5, 3, 3, 0x93));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x20000 >> 5, 3, 3, 0, 0xAC));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xA0000 >> 5, 3, 3, 0xB0));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x28000 >> 5, 3, 3, 0, 0xAD));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xA8000 >> 5, 3, 3, 0xB1));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x30000 >> 5, 3, 3, 0, 0xAE));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xB0000 >> 5, 3, 3, 0xB2));
        GX_BP_LOAD_REG(TX_SETIMAGE1(0x38000 >> 5, 3, 3, 0, 0xAF));
        GX_BP_LOAD_REG(TX_SETIMAGE2(0xB8000 >> 5, 3, 3, 0xB3));
        break;
    }
}
