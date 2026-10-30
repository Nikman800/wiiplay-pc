#include <revolution/GX.h>

#include <port/resptr.h>
#if defined(PORT_HOST)
#include <port/gxsink.h>
#endif

void __GetImageTileCount(GXTexFmt fmt, u16 wd, u16 ht, u32* rowTiles,
                         u32* colTiles, u32* cmpTiles);

#define GX_PHY_ADDR(a) (PORT_PTR_TO_U32(a) & 0x3FFFFFFF)

void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    gxdt->cpDispSrc = 0;
    gxdt->cpDispSrc = GX_BITSET(gxdt->cpDispSrc, 22, 10, left);
    gxdt->cpDispSrc = GX_BITSET(gxdt->cpDispSrc, 12, 10, top);
    gxdt->cpDispSrc = GX_BITSET(gxdt->cpDispSrc, 0, 8, 0x49);
    gxdt->cpDispSize = 0;
    gxdt->cpDispSize = GX_BITSET(gxdt->cpDispSize, 22, 10, wd - 1);
    gxdt->cpDispSize = GX_BITSET(gxdt->cpDispSize, 12, 10, ht - 1);
    gxdt->cpDispSize = GX_BITSET(gxdt->cpDispSize, 0, 8, 0x4A);
}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    gxdt->cpTexSrc = 0;
    gxdt->cpTexSrc = GX_BITSET(gxdt->cpTexSrc, 22, 10, left);
    gxdt->cpTexSrc = GX_BITSET(gxdt->cpTexSrc, 12, 10, top);
    gxdt->cpTexSrc = GX_BITSET(gxdt->cpTexSrc, 0, 8, 0x49);
    gxdt->cpTexSize = 0;
    gxdt->cpTexSize = GX_BITSET(gxdt->cpTexSize, 22, 10, wd - 1);
    gxdt->cpTexSize = GX_BITSET(gxdt->cpTexSize, 12, 10, ht - 1);
    gxdt->cpTexSize = GX_BITSET(gxdt->cpTexSize, 0, 8, 0x4A);
}

void GXSetDispCopyDst(u16 wd, u16 ht) {
    u16 stride;

    stride = (u16)(wd * 2);
    gxdt->cpDispStride = 0;
    gxdt->cpDispStride = GX_BITSET(gxdt->cpDispStride, 22, 10, stride >> 5);
    gxdt->cpDispStride = GX_BITSET(gxdt->cpDispStride, 0, 8, 0x4D);
}

void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) {
    u32 rowTiles, colTiles, cmpTiles;
    u32 peTexFmt;
    u32 peTexFmtH;

    gxdt->cpTexZ = FALSE;
    peTexFmt = (u32)(fmt & 0xF);

    if (fmt == GX_TF_Z16) {
        peTexFmt = 0xB;
    }

    switch (fmt) {
    case GX_TF_I4:
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_IA8:
    case GX_CTF_YUVA8:
        gxdt->cpTex = GX_BITSET(gxdt->cpTex, 15, 2, 3);
        break;
    default:
        gxdt->cpTex = GX_BITSET(gxdt->cpTex, 15, 2, 2);
        break;
    }

    gxdt->cpTexZ = ((fmt & 0x10) == 0x10);
    peTexFmtH = (peTexFmt & 0x8) >> 3;
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 28, 1, peTexFmtH);
    peTexFmt = peTexFmt & 0x7;
    __GetImageTileCount(fmt, wd, ht, &rowTiles, &colTiles, &cmpTiles);

    gxdt->cpTexStride = 0;
    gxdt->cpTexStride = GX_BITSET(gxdt->cpTexStride, 22, 10, rowTiles * cmpTiles);
    gxdt->cpTexStride = GX_BITSET(gxdt->cpTexStride, 0, 8, 0x4D);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 22, 1, mipmap);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 25, 3, peTexFmt);
}

void GXSetDispCopyFrame2Field(u32 mode) {
    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 18, 2, mode);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 18, 2, 0);
}

void GXSetCopyClamp(GXCopyClamp clamp) {
    GXBool clmpB, clmpT;

    clmpT = ((clamp & GX_CLAMP_TOP) == GX_CLAMP_TOP);
    clmpB = ((clamp & GX_CLAMP_BOTTOM) == GX_CLAMP_BOTTOM);
    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 31, 1, clmpT);
    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 30, 1, clmpB);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 31, 1, clmpT);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 30, 1, clmpB);
}

static u32 __GXGetNumXfbLines(u32 efbHt, u32 iScale) {
    u32 count, realHt, iScaleD;

    count = (efbHt - 1) * 256;
    realHt = count / iScale + 1;
    iScaleD = iScale;

    if (iScaleD > 0x80 && iScaleD < 0x100) {
        while ((iScaleD & 0x01) == 0) {
            iScaleD >>= 1;
        }

        if ((efbHt % iScaleD) == 0) {
            ++realHt;
        }
    }

    if (realHt > 1024) {
        realHt = 1024;
    }

    return realHt;
}

u32 GXGetNumXfbLines(u16 efbHeight, f32 yScale) {
    u32 iScale;

    iScale = (u32)(256.0f / yScale) & 0x1FF;
    return (u16)__GXGetNumXfbLines((u32)efbHeight, iScale);
}

f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) {
    f32 fScale, yScale;
    u32 iScale, tgtHt, realHt;

    tgtHt = xfbHeight;
    yScale = (f32)xfbHeight / (f32)efbHeight;
    iScale = (u32)(256.0f / yScale) & 0x1FF;
    realHt = __GXGetNumXfbLines((u32)efbHeight, iScale);

    while (realHt > xfbHeight) {
        tgtHt--;
        yScale = (f32)tgtHt / (f32)efbHeight;
        iScale = (u32)(256.0f / yScale) & 0x1FF;
        realHt = __GXGetNumXfbLines((u32)efbHeight, iScale);
    }

    fScale = yScale;
    while (realHt < xfbHeight) {
        fScale = yScale;
        tgtHt++;
        yScale = (f32)tgtHt / (f32)efbHeight;
        iScale = (u32)(256.0f / yScale) & 0x1FF;
        realHt = __GXGetNumXfbLines((u32)efbHeight, iScale);
    }

    return fScale;
}

u32 GXSetDispCopyYScale(f32 vscale) {
    GXBool enable;
    u32 iScale;
    u32 ht;
    u32 reg;

    iScale = (u32)(256.0f / vscale) & 0x1FF;
    enable = (iScale != 0x100);

    reg = 0;
    reg = GX_BITSET(reg, 23, 9, iScale);
    reg = GX_BITSET(reg, 0, 8, 0x4E);
    GX_BP_LOAD_REG(reg);
    gxdt->lastWriteWasXF = FALSE;

    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 21, 1, enable);
    ht = GX_BITGET(gxdt->cpDispSize, 12, 10) + 1;
    return __GXGetNumXfbLines(ht, iScale);
}

void GXSetCopyClear(GXColor clear_clr, u32 clear_z) {
    u32 reg;

    reg = 0;
    reg = GX_BITSET(reg, 24, 8, clear_clr.r);
    reg = GX_BITSET(reg, 16, 8, clear_clr.a);
    reg = GX_BITSET(reg, 0, 8, 0x4F);
    GX_BP_LOAD_REG(reg);

    reg = 0;
    reg = GX_BITSET(reg, 24, 8, clear_clr.b);
    reg = GX_BITSET(reg, 16, 8, clear_clr.g);
    reg = GX_BITSET(reg, 0, 8, 0x50);
    GX_BP_LOAD_REG(reg);

    reg = 0;
    reg = GX_BITSET(reg, 8, 24, clear_z);
    reg = GX_BITSET(reg, 0, 8, 0x51);
    GX_BP_LOAD_REG(reg);

    gxdt->lastWriteWasXF = FALSE;
}

#define GX_SET_MSLOC(reg, xs0, ys0, xs1, ys1, xs2, ys2, rid)                    \
    do {                                                                       \
        (reg) = 0;                                                             \
        (reg) = GX_BITSET((reg), 28, 4, (xs0));                                \
        (reg) = GX_BITSET((reg), 24, 4, (ys0));                                \
        (reg) = GX_BITSET((reg), 20, 4, (xs1));                                \
        (reg) = GX_BITSET((reg), 16, 4, (ys1));                                \
        (reg) = GX_BITSET((reg), 12, 4, (xs2));                                \
        (reg) = GX_BITSET((reg), 8, 4, (ys2));                                 \
        (reg) = GX_BITSET((reg), 0, 8, (rid));                                 \
    } while (0)

void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf,
                     const u8 vfilter[GX_VFILTER_SZ]) {
    u32 msLoc[4];
    u32 coeff0, coeff1;

    if (aa) {
        GX_SET_MSLOC(msLoc[0], sample_pattern[0][0], sample_pattern[0][1],
                     sample_pattern[1][0], sample_pattern[1][1],
                     sample_pattern[2][0], sample_pattern[2][1], 1);
        GX_SET_MSLOC(msLoc[1], sample_pattern[3][0], sample_pattern[3][1],
                     sample_pattern[4][0], sample_pattern[4][1],
                     sample_pattern[5][0], sample_pattern[5][1], 2);
        GX_SET_MSLOC(msLoc[2], sample_pattern[6][0], sample_pattern[6][1],
                     sample_pattern[7][0], sample_pattern[7][1],
                     sample_pattern[8][0], sample_pattern[8][1], 3);
        GX_SET_MSLOC(msLoc[3], sample_pattern[9][0], sample_pattern[9][1],
                     sample_pattern[10][0], sample_pattern[10][1],
                     sample_pattern[11][0], sample_pattern[11][1], 4);
    } else {
        msLoc[0] = 0x01666666;
        msLoc[1] = 0x02666666;
        msLoc[2] = 0x03666666;
        msLoc[3] = 0x04666666;
    }

    GX_BP_LOAD_REG(msLoc[0]);
    GX_BP_LOAD_REG(msLoc[1]);
    GX_BP_LOAD_REG(msLoc[2]);
    GX_BP_LOAD_REG(msLoc[3]);

    coeff0 = 0;
    coeff0 = GX_BITSET(coeff0, 0, 8, 0x53);
    coeff1 = 0;
    coeff1 = GX_BITSET(coeff1, 0, 8, 0x54);

    if (vf) {
        coeff0 = GX_BITSET(coeff0, 26, 6, vfilter[0]);
        coeff0 = GX_BITSET(coeff0, 20, 6, vfilter[1]);
        coeff0 = GX_BITSET(coeff0, 14, 6, vfilter[2]);
        coeff0 = GX_BITSET(coeff0, 8, 6, vfilter[3]);
        coeff1 = GX_BITSET(coeff1, 26, 6, vfilter[4]);
        coeff1 = GX_BITSET(coeff1, 20, 6, vfilter[5]);
        coeff1 = GX_BITSET(coeff1, 14, 6, vfilter[6]);
    } else {
        coeff0 = GX_BITSET(coeff0, 26, 6, 0);
        coeff0 = GX_BITSET(coeff0, 20, 6, 0);
        coeff0 = GX_BITSET(coeff0, 14, 6, 21);
        coeff0 = GX_BITSET(coeff0, 8, 6, 22);
        coeff1 = GX_BITSET(coeff1, 26, 6, 21);
        coeff1 = GX_BITSET(coeff1, 20, 6, 0);
        coeff1 = GX_BITSET(coeff1, 14, 6, 0);
    }

    GX_BP_LOAD_REG(coeff0);
    GX_BP_LOAD_REG(coeff1);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetDispCopyGamma(u32 gamma) {
    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 23, 2, gamma);
}

void GXCopyDisp(void* dest, GXBool clear) {
    u32 reg;
    u32 tempPeCtrl;
    u32 phyAddr;
    GXBool changePeCtrl;

    if (clear) {
        reg = gxdt->zMode;
        reg = GX_BITSET(reg, 31, 1, 1);
        reg = GX_BITSET(reg, 28, 3, 7);
        GX_BP_LOAD_REG(reg);

        reg = gxdt->blendMode;
        reg = GX_BITSET(reg, 31, 1, 0);
        reg = GX_BITSET(reg, 30, 1, 0);
        GX_BP_LOAD_REG(reg);
    }

    changePeCtrl = FALSE;
    if ((clear || GX_BITGET(gxdt->zControl, 29, 3) == 3) &&
        GX_BITGET(gxdt->zControl, 25, 1) == 1) {
        changePeCtrl = TRUE;
        tempPeCtrl = gxdt->zControl;
        tempPeCtrl = GX_BITSET(tempPeCtrl, 25, 1, 0);
        GX_BP_LOAD_REG(tempPeCtrl);
    }

    GX_BP_LOAD_REG(gxdt->cpDispSrc);
    GX_BP_LOAD_REG(gxdt->cpDispSize);
    GX_BP_LOAD_REG(gxdt->cpDispStride);

    phyAddr = GX_PHY_ADDR(dest);
    reg = 0;
    reg = GX_BITSET(reg, 8, 24, phyAddr >> 5);
    reg = GX_BITSET(reg, 0, 8, 0x4B);
    GX_BP_LOAD_REG(reg);

    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 20, 1, clear);
    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 17, 1, 1);
    gxdt->cpDisp = GX_BITSET(gxdt->cpDisp, 0, 8, 0x52);
    GX_BP_LOAD_REG(gxdt->cpDisp);

    if (clear) {
        GX_BP_LOAD_REG(gxdt->zMode);
        GX_BP_LOAD_REG(gxdt->blendMode);
    }

    if (changePeCtrl) {
        GX_BP_LOAD_REG(gxdt->zControl);
    }

    gxdt->lastWriteWasXF = FALSE;

#if defined(PORT_HOST)
    // The frame is in the EFB: present it (the host has no XFB/VI scan-out)
    PortGXDisplayCopied(dest, clear);
#endif
}

void GXCopyTex(void* dest, GXBool clear) {
    u32 reg;
    u32 tempPeCtrl;
    u32 phyAddr;
    GXBool changePeCtrl;

    if (clear) {
        reg = gxdt->zMode;
        reg = GX_BITSET(reg, 31, 1, 1);
        reg = GX_BITSET(reg, 28, 3, 7);
        GX_BP_LOAD_REG(reg);

        reg = gxdt->blendMode;
        reg = GX_BITSET(reg, 31, 1, 0);
        reg = GX_BITSET(reg, 30, 1, 0);
        GX_BP_LOAD_REG(reg);
    }

    changePeCtrl = FALSE;
    tempPeCtrl = gxdt->zControl;

    if (gxdt->cpTexZ && GX_BITGET(tempPeCtrl, 29, 3) != 3) {
        changePeCtrl = TRUE;
        tempPeCtrl = GX_BITSET(tempPeCtrl, 29, 3, 3);
    }

    if (clear || GX_BITGET(tempPeCtrl, 29, 3) == 3) {
        if (GX_BITGET(tempPeCtrl, 25, 1) == 1) {
            changePeCtrl = TRUE;
            tempPeCtrl = GX_BITSET(tempPeCtrl, 25, 1, 0);
        }
    }

    if (changePeCtrl) {
        GX_BP_LOAD_REG(tempPeCtrl);
    }

    GX_BP_LOAD_REG(gxdt->cpTexSrc);
    GX_BP_LOAD_REG(gxdt->cpTexSize);
    GX_BP_LOAD_REG(gxdt->cpTexStride);

    phyAddr = GX_PHY_ADDR(dest);
    reg = 0;
    reg = GX_BITSET(reg, 8, 24, phyAddr >> 5);
    reg = GX_BITSET(reg, 0, 8, 0x4B);
    GX_BP_LOAD_REG(reg);

    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 20, 1, clear);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 17, 1, 0);
    gxdt->cpTex = GX_BITSET(gxdt->cpTex, 0, 8, 0x52);
    GX_BP_LOAD_REG(gxdt->cpTex);

    if (clear) {
        GX_BP_LOAD_REG(gxdt->zMode);
        GX_BP_LOAD_REG(gxdt->blendMode);
    }

    if (changePeCtrl) {
        GX_BP_LOAD_REG(gxdt->zControl);
    }

    gxdt->lastWriteWasXF = FALSE;
}

void GXClearBoundingBox(void) {
    u32 reg;

    reg = 0x550003FF;
    GX_BP_LOAD_REG(reg);
    reg = 0x560003FF;
    GX_BP_LOAD_REG(reg);
    gxdt->lastWriteWasXF = FALSE;
}
