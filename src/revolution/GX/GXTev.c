#include <revolution/GX.h>

static u32 TEVCOpTableST0[5] = {0xC008F8AF, 0xC008A89F, 0xC008AC8F,
                                0xC008FFF8, 0xC008FFFA};
static u32 TEVCOpTableST1[5] = {0xC008F80F, 0xC008089F, 0xC0080C8F,
                                0xC008FFF8, 0xC008FFF0};
static u32 TEVAOpTableST0[5] = {0xC108F2F0, 0xC108FFD0, 0xC108F2F0,
                                0xC108FFC0, 0xC108FFD0};
static u32 TEVAOpTableST1[5] = {0xC108F070, 0xC108FF80, 0xC108F070,
                                0xC108FFC0, 0xC108FF80};

void GXSetTevOp(GXTevStageID id, GXTevMode mode) {
    u32* ctmp;
    u32* atmp;
    u32 tevReg;

    if (id == GX_TEVSTAGE0) {
        ctmp = &TEVCOpTableST0[mode];
        atmp = &TEVAOpTableST0[mode];
    } else {
        ctmp = &TEVCOpTableST1[mode];
        atmp = &TEVAOpTableST1[mode];
    }

    tevReg = gxdt->tevc[id];
    tevReg = (*ctmp & ~0xFF000000) | (tevReg & 0xFF000000);
    GX_BP_LOAD_REG(tevReg);
    gxdt->tevc[id] = tevReg;

    tevReg = gxdt->teva[id];
    tevReg = (*atmp & ~0xFF00000F) | (tevReg & 0xFF00000F);
    GX_BP_LOAD_REG(tevReg);
    gxdt->teva[id] = tevReg;
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b,
                     GXTevColorArg c, GXTevColorArg d) {
    u32 tevReg = gxdt->tevc[stage];

    GX_BP_SET_TEVCOLORCOMBINER_A(tevReg, a);
    GX_BP_SET_TEVCOLORCOMBINER_B(tevReg, b);
    GX_BP_SET_TEVCOLORCOMBINER_C(tevReg, c);
    GX_BP_SET_TEVCOLORCOMBINER_D(tevReg, d);
    GX_BP_LOAD_REG(tevReg);
    gxdt->tevc[stage] = tevReg;
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b,
                     GXTevAlphaArg c, GXTevAlphaArg d) {
    u32 tevReg = gxdt->teva[stage];

    tevReg = GX_BITSET(tevReg, 16, 3, a);
    tevReg = GX_BITSET(tevReg, 19, 3, b);
    tevReg = GX_BITSET(tevReg, 22, 3, c);
    tevReg = GX_BITSET(tevReg, 25, 3, d);
    GX_BP_LOAD_REG(tevReg);
    gxdt->teva[stage] = tevReg;
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                     GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {
    u32 tevReg = gxdt->tevc[stage];

    GX_BP_SET_TEVCOLORCOMBINER_OP_OR_COMPARISON(tevReg, op & 1);
    if (op <= GX_TEV_SUB) {
        GX_BP_SET_TEVCOLORCOMBINER_SCALE_OR_COMPARE_MODE(tevReg, scale);
        GX_BP_SET_TEVCOLORCOMBINER_BIAS(tevReg, bias);
    } else {
        GX_BP_SET_TEVCOLORCOMBINER_SCALE_OR_COMPARE_MODE(tevReg, (op >> 1) & 3);
        GX_BP_SET_TEVCOLORCOMBINER_BIAS(tevReg, GX_MAX_TEVBIAS);
    }

    GX_BP_SET_TEVCOLORCOMBINER_CLAMP(tevReg, clamp);
    GX_BP_SET_TEVCOLORCOMBINER_DEST(tevReg, out_reg);
    GX_BP_LOAD_REG(tevReg);
    gxdt->tevc[stage] = tevReg;
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                     GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {
    u32 tevReg = gxdt->teva[stage];

    GX_BP_SET_TEVCOLORCOMBINER_OP_OR_COMPARISON(tevReg, op & 1);
    if (op <= GX_TEV_SUB) {
        GX_BP_SET_TEVCOLORCOMBINER_SCALE_OR_COMPARE_MODE(tevReg, scale);
        GX_BP_SET_TEVCOLORCOMBINER_BIAS(tevReg, bias);
    } else {
        GX_BP_SET_TEVCOLORCOMBINER_SCALE_OR_COMPARE_MODE(tevReg, (op >> 1) & 3);
        GX_BP_SET_TEVCOLORCOMBINER_BIAS(tevReg, GX_MAX_TEVBIAS);
    }

    GX_BP_SET_TEVCOLORCOMBINER_CLAMP(tevReg, clamp);
    GX_BP_SET_TEVCOLORCOMBINER_DEST(tevReg, out_reg);
    GX_BP_LOAD_REG(tevReg);
    gxdt->teva[stage] = tevReg;
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevColor(GXTevRegID id, GXColor color) {
    u32 rgba, regRA, regBG;

    rgba = BE_GET(*(be_u32*)&color);
    regRA = (u32)(GX_BP_REG_TEVREG0LO + id * 2) << 24;
    regRA = GX_BITSET(regRA, 24, 8, (rgba >> 24) & 0xFF);
    regRA = GX_BITSET(regRA, 12, 8, rgba & 0xFF);
    regBG = (u32)(GX_BP_REG_TEVREG0HI + id * 2) << 24;
    regBG = GX_BITSET(regBG, 24, 8, (rgba >> 8) & 0xFF);
    regBG = GX_BITSET(regBG, 12, 8, (rgba >> 16) & 0xFF);
    GX_BP_LOAD_REG(regRA);
    GX_BP_LOAD_REG(regBG);
    GX_BP_LOAD_REG(regBG);
    GX_BP_LOAD_REG(regBG);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) {
    u32 sRG, sBA, regRA, regBG;

#if defined(PORT_HOST)
    // The two words the Wii loads: (r << 16 | g) and (b << 16 | a)
    sRG = (u32)(u16)color.r << 16 | (u16)color.g;
    sBA = (u32)(u16)color.b << 16 | (u16)color.a;
#else
    sRG = *(u32*)&color;
    sBA = *((u32*)&color + 1);
#endif
    regRA = (u32)(GX_BP_REG_TEVREG0LO + id * 2) << 24;
    GX_BP_SET_TEVREGLO_RED(regRA, (sRG >> 16) & 0x7FF);
    GX_BP_SET_TEVREGLO_ALPHA(regRA, sBA & 0x7FF);
    regBG = (u32)(GX_BP_REG_TEVREG0HI + id * 2) << 24;
    GX_BP_SET_TEVREGHI_BLUE(regBG, (sBA >> 16) & 0x7FF);
    GX_BP_SET_TEVREGHI_GREEN(regBG, sRG & 0x7FF);
    GX_BP_LOAD_REG(regRA);
    GX_BP_LOAD_REG(regBG);
    GX_BP_LOAD_REG(regBG);
    GX_BP_LOAD_REG(regBG);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevKColor(GXTevKColorID id, GXColor color) {
    u32 rgba, regRA, regBG;

    rgba = BE_GET(*(be_u32*)&color);
    regRA = (u32)(GX_BP_REG_TEVREG0LO + id * 2) << 24;
    regRA = GX_BITSET(regRA, 24, 8, (rgba >> 24) & 0xFF);
    regRA = GX_BITSET(regRA, 12, 8, rgba & 0xFF);
    regRA = GX_BITSET(regRA, 8, 4, 8);
    regBG = (u32)(GX_BP_REG_TEVREG0HI + id * 2) << 24;
    regBG = GX_BITSET(regBG, 24, 8, (rgba >> 8) & 0xFF);
    regBG = GX_BITSET(regBG, 12, 8, (rgba >> 16) & 0xFF);
    regBG = GX_BITSET(regBG, 8, 4, 8);
    GX_BP_LOAD_REG(regRA);
    GX_BP_LOAD_REG(regBG);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {
    u32* reg = &gxdt->tevKsel[stage >> 1];

    if (stage & 1) {
        GX_BP_SET_TEVKSEL_KCSEL_ODD(*reg, sel);
    } else {
        GX_BP_SET_TEVKSEL_KCSEL_EVEN(*reg, sel);
    }

    GX_BP_LOAD_REG(*reg);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {
    u32* reg = &gxdt->tevKsel[stage >> 1];

    if (stage & 1) {
        GX_BP_SET_TEVKSEL_KASEL_ODD(*reg, sel);
    } else {
        GX_BP_SET_TEVKSEL_KASEL_EVEN(*reg, sel);
    }

    GX_BP_LOAD_REG(*reg);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel,
                      GXTevSwapSel tex_sel) {
    u32* reg = &gxdt->teva[stage];

    *reg = GX_BITSET(*reg, 30, 2, ras_sel);
    *reg = GX_BITSET(*reg, 28, 2, tex_sel);
    GX_BP_LOAD_REG(*reg);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red,
                           GXTevColorChan green, GXTevColorChan blue,
                           GXTevColorChan alpha) {
    u32* reg = &gxdt->tevKsel[table << 1];

    GX_BP_SET_TEVKSEL_SWAP_RB(*reg, red);
    GX_BP_SET_TEVKSEL_SWAP_GA(*reg, green);
    GX_BP_LOAD_REG(*reg);

    reg = &gxdt->tevKsel[(table << 1) + 1];
    GX_BP_SET_TEVKSEL_SWAP_RB(*reg, blue);
    GX_BP_SET_TEVKSEL_SWAP_GA(*reg, alpha);
    GX_BP_LOAD_REG(*reg);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op,
                       GXCompare comp1, u8 ref1) {
    u32 reg = 0xF3000000;

    GX_BP_SET_ALPHACOMPARE_REF0(reg, ref0);
    GX_BP_SET_ALPHACOMPARE_REF1(reg, ref1);
    GX_BP_SET_ALPHACOMPARE_COMP0(reg, comp0);
    GX_BP_SET_ALPHACOMPARE_COMP1(reg, comp1);
    GX_BP_SET_ALPHACOMPARE_LOGIC(reg, op);
    GX_BP_LOAD_REG(reg);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) {
    u32 zenv0 = 0;
    u32 zenv1 = 0;
    u32 type;

    zenv0 = GX_BITSET(zenv0, 8, 24, bias);
    GX_BP_SET_OPCODE(zenv0, GX_BP_REG_ZTEXTURE0);

    switch (fmt) {
    case GX_TF_Z8:
        type = 0;
        break;
    case GX_TF_Z16:
        type = 1;
        break;
    case GX_TF_Z24X8:
        type = 2;
        break;
    default:
        type = 2;
        break;
    }

    zenv1 = GX_BITSET(zenv1, 30, 2, type);
    zenv1 = GX_BITSET(zenv1, 28, 2, op);
    GX_BP_SET_OPCODE(zenv1, GX_BP_REG_ZTEXTURE1);
    GX_BP_LOAD_REG(zenv0);
    GX_BP_LOAD_REG(zenv1);
    gxdt->lastWriteWasXF = FALSE;
}

void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map,
                   GXChannelID color) {
    static u32 c2r[] = {0, 1, 0, 1, 0, 1, 7, 5, 6, 0};
    u32* ptref = &gxdt->tref[stage / 2];
    u32 tmap;
    u32 tcoord;

    gxdt->texmapId[stage] = map;
    tmap = (u32)(map & ~GX_TEX_DISABLE);
    tmap = (tmap >= GX_MAX_TEXMAP) ? GX_TEXMAP0 : tmap;

    if (coord >= GX_MAX_TEXCOORD) {
        tcoord = GX_TEXCOORD0;
        gxdt->tevTcEnab &= ~(1 << stage);
    } else {
        tcoord = coord;
        gxdt->tevTcEnab |= 1 << stage;
    }

    if (stage & 1) {
        GX_BP_SET_RAS1_TREF_TEXMAP_ODD(*ptref, tmap);
        GX_BP_SET_RAS1_TREF_TEXCOORD_ODD(*ptref, tcoord);
        GX_BP_SET_RAS1_TREF_COLORCHAN_ODD(
            *ptref, color == GX_COLOR_NULL ? 7 : c2r[color]);
        GX_BP_SET_RAS1_TREF_ENABLE_TEX_ODD(
            *ptref,
            map != GX_TEXMAP_NULL && (map & GX_TEX_DISABLE) == 0);
    } else {
        GX_BP_SET_RAS1_TREF_TEXMAP_EVEN(*ptref, tmap);
        GX_BP_SET_RAS1_TREF_TEXCOORD_EVEN(*ptref, tcoord);
        GX_BP_SET_RAS1_TREF_COLORCHAN_EVEN(
            *ptref, color == GX_COLOR_NULL ? 7 : c2r[color]);
        GX_BP_SET_RAS1_TREF_ENABLE_TEX_EVEN(
            *ptref,
            map != GX_TEXMAP_NULL && (map & GX_TEX_DISABLE) == 0);
    }

    GX_BP_LOAD_REG(*ptref);
    gxdt->lastWriteWasXF = FALSE;
    gxdt->gxDirtyFlags |= GX_DIRTY_SU_TEX;
}

void GXSetNumTevStages(u8 nStages) {
    GX_BP_SET_GENMODE_NUMTEVSTAGES(gxdt->genMode, nStages - 1);
    gxdt->gxDirtyFlags |= GX_DIRTY_GEN_MODE;
}
