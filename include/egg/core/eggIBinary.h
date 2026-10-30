#ifndef EGG_CORE_IBINARY_H
#define EGG_CORE_IBINARY_H
#include <egg/types_egg.h>

#include <egg/prim.h>

#include <port/be.h>

namespace EGG {

template <typename T> class IBinary {
public:
#pragma pack(push, 1)
    struct BinHeader {
        u8 magic[4]; // at 0x0 Wii Play: unsigned (RPHkyGame::Load)
        be_u32 size;   // at 0x4
        u8 version;    // at 0x8
        u8 unk9[0x10 - 0x9];
    };

    struct Bin {
        BinHeader header;
        typename T::BinData data;
    };
#pragma pack(pop)

    // Wii Play: declared before the virtuals; eggLightManager.o emits IBinary<LightObject>'s
    // virtuals right after GetBinary (CW instantiates members in declaration order)
    // Wii Play: not inline (RPHkyGame::Load calls an instance emitted in its own unit)
    void SetBinary(const void* bin);

    // Wii Play: not inline (eggLightManager.cpp calls an out-of-line instance)
    void GetBinary(void* bin) const;

    virtual void SetBinaryInner(const Bin& rBinary) = 0; // at 0x8
    virtual void GetBinaryInner(Bin* pBinary) const = 0; // at 0xC

    virtual const char* GetBinaryType() const = 0; // at 0x10
    virtual u32 GetBinarySize() const; // at 0x14

    // Wii Play: not pure (LightTexture inherits IBinary<LightTexture>::GetVersion)
    virtual u8 GetVersion() const; // at 0x18

    // Wii Play: asserts (RPGrpRenderer's engine setup)
    static void SetBinaryExtension(const char* pName) {
#line 46
        EGG_ASSERT(pName != NULL);
        spExtension = pName;
    }

private:
    static const char* spExtension;
};

template <typename T> void IBinary<T>::SetBinary(const void* bin) {
#line 86
    EGG_ASSERT(bin);
    Bin* pBinary = (Bin*)bin;

#line 89 // the assertion reports line 93, where the statement ends
    EGG_ASSERT_MSG(GetBinaryType()[0] == pBinary->header.magic[0]
            && GetBinaryType()[1] == pBinary->header.magic[1]
            && GetBinaryType()[2] == pBinary->header.magic[2]
            && GetBinaryType()[3] == pBinary->header.magic[3],
            "Not for this class.");

    SetBinaryInner(*pBinary);
}

template <typename T> void IBinary<T>::GetBinary(void* bin) const {
#line 105
    EGG_ASSERT(bin);
    Bin* pBinary = (Bin*)bin;

    pBinary->header.version = GetVersion();
    pBinary->header.size = GetBinarySize();

    for (int i = 0; i < 4; i++) {
        pBinary->header.magic[i] = GetBinaryType()[i];
    }

    GetBinaryInner(pBinary);
}

template <typename T> u32 IBinary<T>::GetBinarySize() const {
#if defined(PORT_HOST)
    return sizeof(typename T::Bin);
#else
    return sizeof(T::Bin);
#endif
}

// Wii Play: not pure; IBinary<LightTexture> specializes it
template <typename T> u8 IBinary<T>::GetVersion() const {
    return 0;
}

} // namespace EGG

#endif
