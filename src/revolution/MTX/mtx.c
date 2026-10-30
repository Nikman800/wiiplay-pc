#include <revolution/MTX.h>

#define qr0 0

f32 Unit01[2] = {0.0f, 1.0f};

const f32 mtxOne = 1.0f;
const f32 mtxZero = 0.0f;

#if defined(PORT_HOST)
// Host form: the asm transliterated (port/host/include/port/ps_forms.h).
void PSMTXIdentity(Mtx mtx) {
    int i;
    for (i = 0; i < 12; i++) {
        (&mtx[0][0])[i] = (i % 5 == 0) ? mtxOne : mtxZero;
    }
}
#else
asm void PSMTXIdentity(register Mtx mtx) {
    nofralloc

    lfs f0, mtxZero(r2)
    lfs f1, mtxOne(r2)
    psq_st f0, 0x8(mtx), 0, qr0
    ps_merge10 f2, f1, f0
    ps_merge01 f1, f0, f1
    psq_st f0, 0x18(mtx), 0, qr0
    psq_st f0, 0x20(mtx), 0, qr0
    psq_st f1, 0x10(mtx), 0, qr0
    psq_st f2, 0x0(mtx), 0, qr0
    psq_st f2, 0x28(mtx), 0, qr0
    blr
}
#endif

#if defined(PORT_HOST)
// Host form: the asm transliterated (port/host/include/port/ps_forms.h).
void PSMTXCopy(const Mtx src, Mtx dst) {
    f32 t[12];
    __builtin_memcpy(t, src, sizeof t);
    __builtin_memcpy(dst, t, sizeof t);
}
#else
asm void PSMTXCopy(register const Mtx src, register Mtx dst) {
    nofralloc

    psq_l f0, 0x0(src), 0, qr0
    psq_st f0, 0x0(dst), 0, qr0
    psq_l f1, 0x8(src), 0, qr0
    psq_st f1, 0x8(dst), 0, qr0
    psq_l f2, 0x10(src), 0, qr0
    psq_st f2, 0x10(dst), 0, qr0
    psq_l f3, 0x18(src), 0, qr0
    psq_st f3, 0x18(dst), 0, qr0
    psq_l f4, 0x20(src), 0, qr0
    psq_st f4, 0x20(dst), 0, qr0
    psq_l f5, 0x28(src), 0, qr0
    psq_st f5, 0x28(dst), 0, qr0
    blr
}
#endif

#if defined(PORT_HOST)
// Host form: the asm transliterated (port/host/include/port/ps_forms.h).
void PSMTXConcat(const Mtx srcA, const Mtx srcB, Mtx dst) {
    port_mtx_concat(&srcA[0][0], &srcB[0][0], &dst[0][0], 0);
}
#else
asm void PSMTXConcat(register const Mtx srcA, register const Mtx srcB,
                     register Mtx dst) {
    nofralloc

    stwu r1, -0x40(r1)
    psq_l f0, 0x0(srcA), 0, qr0
    stfd f14, 0x8(r1)
    psq_l f6, 0x0(srcB), 0, qr0
    lis r6, Unit01@ha
    psq_l f7, 0x8(srcB), 0, qr0
    stfd f15, 0x10(r1)
    addi r6, r6, Unit01@l
    stfd f31, 0x28(r1)
    psq_l f8, 0x10(srcB), 0, qr0
    ps_muls0 f12, f6, f0
    psq_l f2, 0x10(srcA), 0, qr0
    ps_muls0 f13, f7, f0
    psq_l f31, 0x0(r6), 0, qr0
    ps_muls0 f14, f6, f2
    psq_l f9, 0x18(srcB), 0, qr0
    ps_muls0 f15, f7, f2
    psq_l f1, 0x8(srcA), 0, qr0
    ps_madds1 f12, f8, f0, f12
    psq_l f3, 0x18(srcA), 0, qr0
    ps_madds1 f14, f8, f2, f14
    psq_l f10, 0x20(srcB), 0, qr0
    ps_madds1 f13, f9, f0, f13
    psq_l f11, 0x28(srcB), 0, qr0
    ps_madds1 f15, f9, f2, f15
    psq_l f4, 0x20(srcA), 0, qr0
    psq_l f5, 0x28(srcA), 0, qr0
    ps_madds0 f12, f10, f1, f12
    ps_madds0 f13, f11, f1, f13
    ps_madds0 f14, f10, f3, f14
    ps_madds0 f15, f11, f3, f15
    psq_st f12, 0x0(dst), 0, qr0
    ps_muls0 f2, f6, f4
    ps_madds1 f13, f31, f1, f13
    ps_muls0 f0, f7, f4
    psq_st f14, 0x10(dst), 0, qr0
    ps_madds1 f15, f31, f3, f15
    psq_st f13, 0x8(dst), 0, qr0
    ps_madds1 f2, f8, f4, f2
    ps_madds1 f0, f9, f4, f0
    ps_madds0 f2, f10, f5, f2
    lfd f14, 0x8(r1)
    psq_st f15, 0x18(dst), 0, qr0
    ps_madds0 f0, f11, f5, f0
    psq_st f2, 0x20(dst), 0, qr0
    ps_madds1 f0, f31, f5, f0
    lfd f15, 0x10(r1)
    psq_st f0, 0x28(dst), 0, qr0
    lfd f31, 0x28(r1)
    addi r1, r1, 0x40
    blr
}
#endif

#if defined(PORT_HOST)
// Host form: the asm transliterated (port/host/include/port/ps_forms.h).
void PSMTXConcatArray(const Mtx srcA, const Mtx srcB, Mtx dst, u32 count) {
    u32 i;
    for (i = 0; i < count; i++) {
        port_mtx_concat(&srcA[0][0], &srcB[0][0] + 12 * i, &dst[0][0] + 12 * i, 1);
    }
}
#else
asm void PSMTXConcatArray(register const Mtx srcA, register const Mtx srcB,
                           register Mtx dst, register u32 count) {
    nofralloc

    stwu r1, -0x60(r1)
    stfd f31, 0x50(r1)
    psq_st f31, 0x58(r1), 0, qr0
    stfd f30, 0x40(r1)
    psq_st f30, 0x48(r1), 0, qr0
    stfd f29, 0x30(r1)
    psq_st f29, 0x38(r1), 0, qr0
    stfd f28, 0x20(r1)
    psq_st f28, 0x28(r1), 0, qr0
    stfd f27, 0x10(r1)
    psq_st f27, 0x18(r1), 0, qr0
    subi r0, count, 0x1
    psq_l f0, 0x0(srcA), 0, qr0
    psq_l f1, 0x8(srcA), 0, qr0
    la r6, Unit01(r13)
    psq_l f2, 0x10(srcA), 0, qr0
    psq_l f3, 0x18(srcA), 0, qr0
    psq_l f4, 0x20(srcA), 0, qr0
    psq_l f5, 0x28(srcA), 0, qr0
    mtctr r0
    psq_l f6, 0x0(srcB), 0, qr0
    psq_l f7, 0x8(srcB), 0, qr0
    ps_muls0 f12, f6, f0
    psq_l f8, 0x10(srcB), 0, qr0
    ps_muls0 f31, f6, f2
    psq_l f10, 0x20(srcB), 0, qr0
    ps_muls0 f29, f6, f4
    psq_l f9, 0x18(srcB), 0, qr0
    ps_madds1 f12, f8, f0, f12
    psq_l f27, 0x0(r6), 0, qr0
    ps_madds1 f31, f8, f2, f31
    psq_l f11, 0x28(srcB), 0, qr0
    ps_madds1 f29, f8, f4, f29
    ps_muls0 f13, f7, f0
    ps_muls0 f30, f7, f2
    ps_muls0 f28, f7, f4
    ps_madds0 f12, f10, f1, f12
    ps_madds0 f31, f10, f3, f31
    ps_madds0 f29, f10, f5, f29
    ps_madds1 f13, f9, f0, f13
    psq_st f12, 0x0(dst), 0, qr0
    ps_madds1 f30, f9, f2, f30
    ps_madds1 f28, f9, f4, f28
    psq_st f31, 0x10(dst), 0, qr0

loop:
    ps_madds0 f13, f11, f1, f13
    psq_l f6, 0x30(srcB), 0, qr0
    ps_madds0 f30, f11, f3, f30
    psq_st f29, 0x20(dst), 0, qr0
    ps_madds0 f28, f11, f5, f28
    psq_l f8, 0x40(srcB), 0, qr0
    ps_madd f13, f27, f1, f13
    psq_l f10, 0x50(srcB), 0, qr0
    ps_muls0 f12, f6, f0
    psq_l f7, 0x38(srcB), 0, qr0
    ps_muls0 f31, f6, f2
    psq_l f9, 0x48(srcB), 0, qr0
    psq_st f13, 0x8(dst), 0, qr0
    ps_madd f30, f27, f3, f30
    ps_madds1 f12, f8, f0, f12
    psq_l f11, 0x58(srcB), 0, qr0
    ps_muls0 f29, f6, f4
    addi srcB, srcB, 0x30
    psq_st f30, 0x18(dst), 0, qr0
    ps_madd f28, f27, f5, f28
    ps_madds1 f31, f8, f2, f31
    ps_madds1 f29, f8, f4, f29
    psq_st f28, 0x28(dst), 0, qr0
    ps_madds0 f12, f10, f1, f12
    ps_muls0 f13, f7, f0
    ps_muls0 f30, f7, f2
    ps_muls0 f28, f7, f4
    psq_st f12, 0x30(dst), 0, qr0
    ps_madds0 f31, f10, f3, f31
    ps_madds0 f29, f10, f5, f29
    ps_madds1 f13, f9, f0, f13
    psq_st f31, 0x40(dst), 0, qr0
    ps_madds1 f30, f9, f2, f30
    ps_madds1 f28, f9, f4, f28
    addi dst, dst, 0x30
    bdnz loop
    ps_madds0 f13, f11, f1, f13
    psq_st f29, 0x20(dst), 0, qr0
    ps_madds0 f30, f11, f3, f30
    ps_madds0 f28, f11, f5, f28
    ps_madd f13, f27, f1, f13
    ps_madd f30, f27, f3, f30
    ps_madd f28, f27, f5, f28
    psq_st f13, 0x8(dst), 0, qr0
    psq_st f30, 0x18(dst), 0, qr0
    psq_st f28, 0x28(dst), 0, qr0
    psq_l f31, 0x58(r1), 0, qr0
    lfd f31, 0x50(r1)
    psq_l f30, 0x48(r1), 0, qr0
    lfd f30, 0x40(r1)
    psq_l f29, 0x38(r1), 0, qr0
    lfd f29, 0x30(r1)
    psq_l f28, 0x28(r1), 0, qr0
    lfd f28, 0x20(r1)
    psq_l f27, 0x18(r1), 0, qr0
    lfd f27, 0x10(r1)
    addi r1, r1, 0x60
    blr
}
#endif

