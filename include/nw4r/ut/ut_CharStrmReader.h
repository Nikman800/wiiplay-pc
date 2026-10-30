#ifndef NW4R_UT_CHAR_STRM_READER_H
#define NW4R_UT_CHAR_STRM_READER_H
#include <nw4r/types_nw4r.h>

namespace nw4r {
namespace ut {

class CharStrmReader {
public:
    typedef u16 (CharStrmReader::*ReadFunc)();

public:
    explicit CharStrmReader(ReadFunc pFunc)
        : mCharStrm(NULL), mReadFunc(pFunc) {}

    ~CharStrmReader() {}

    u16 ReadNextCharUTF8();
    u16 ReadNextCharUTF16();
    u16 ReadNextCharCP1252();
    u16 ReadNextCharSJIS();

    u16 Next() {
        return (this->*mReadFunc)();
    }

    const void* GetCurrentPos() const {
        return mCharStrm;
    }

    void Set(const char* pStrm) {
        mCharStrm = pStrm;
    }
    void Set(const wchar_t* pStrm) {
        mCharStrm = pStrm;
    }

private:
    template <typename T> T GetChar(int offset) const {
        return static_cast<const T*>(mCharStrm)[offset];
    }

    template <typename T> void StepStrm(int offset) {
#if defined(PORT_HOST)
        // GCC rejects a cast as the left operand of +=.
        mCharStrm = static_cast<const T*>(mCharStrm) + offset;
#else
        static_cast<const T*>(mCharStrm) += offset;
#endif
    }

private:
    const void* mCharStrm; // at 0x0
    ReadFunc mReadFunc;    // at 0x4
};

} // namespace ut
} // namespace nw4r

#endif
