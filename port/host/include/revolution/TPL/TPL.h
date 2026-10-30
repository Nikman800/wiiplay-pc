/**
 * TPL texture palettes, host build (port/docs/POINTERS.md §3.1).
 *
 * Shadows include/revolution/TPL/TPL.h. The structs are the Wii file layout:
 * scalar members are big-endian (be_*), GX enums are stored as u32
 * (BE_ENUM), and the five members that TPLBind turns from file offsets into
 * pointers are RES_PTR slots (4 bytes, big-endian emulated Wii addresses).
 * TPLBind and TPLGet are in port/host/src/host_tpl.cpp. Texture and palette
 * data stay GX-native big-endian bytes.
 */
#ifndef RVL_SDK_TPL_H
#define RVL_SDK_TPL_H
#include <types.h>

#ifdef __cplusplus
/* revolution/TPL.h includes this header inside extern "C"; the wrapper
 * templates need C++ linkage. */
extern "C++" {
#endif
#include <port/be.h>
#include <port/resptr.h>
#ifdef __cplusplus
}
#endif

#include <revolution/GX.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct TPLHeader {
    be_u16 height;                            // at 0x0
    be_u16 width;                             // at 0x2
    be_u32 format;                            // at 0x4
    RES_PTR(char) data;                       // at 0x8
    BE_ENUM(GXTexWrapMode, u32) wrapS;        // at 0xC
    BE_ENUM(GXTexWrapMode, u32) wrapT;        // at 0x10
    BE_ENUM(GXTexFilter, u32) minFilter;      // at 0x14
    BE_ENUM(GXTexFilter, u32) magFilter;      // at 0x18
    be_f32 LODBias;                           // at 0x1C
    u8 edgeLODEnable;                         // at 0x20
    u8 minLOD;                                // at 0x21
    u8 maxLOD;                                // at 0x22
    u8 unpacked;                              // at 0x23
} TPLHeader;

typedef struct TPLClutHeader {
    be_u16 numEntries;                 // at 0x0
    u8 unpacked;                       // at 0x2 (the Wii header says 0x1)
    u8 pad8;                           // at 0x3
    BE_ENUM(GXTlutFmt, u32) format;    // at 0x4
    RES_PTR(char) data;                // at 0x8
} TPLClutHeader;

typedef struct TPLDescriptor {
    RES_PTR(TPLHeader) textureHeader;  // at 0x0
    RES_PTR(TPLClutHeader) CLUTHeader; // at 0x4
} TPLDescriptor;

typedef struct TPLPalette {
    be_u32 versionNumber;                  // at 0x0
    be_u32 numDescriptors;                 // at 0x4
    RES_PTR(TPLDescriptor) descriptorArray; // at 0x8
} TPLPalette;

void TPLBind(TPLPalette* pal);
TPLDescriptor* TPLGet(TPLPalette* pal, u32 id);

#ifdef __cplusplus
}
#endif
#endif
