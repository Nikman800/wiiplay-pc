/**
 * Big-endian field types for on-disc structures (host build).
 *
 * Wii data files are big-endian and the game reads most of them in place: it
 * casts a pointer into the loaded file to a header struct and reads members.
 * On a little-endian host those member reads must byte-swap. Instead of
 * converting files offline, the struct members that map onto file bytes are
 * declared with the types below, which store the raw (big-endian) bytes and
 * convert on every read and write.
 *
 * Types (same size and alignment as the plain type, so struct layouts and
 * offsets do not change):
 *
 *   be_u16 be_s16 be_u32 be_s32 be_f32   16/32-bit integer and float fields
 *   be_u64 be_s64 be_f64                 64-bit fields (rare)
 *   be_u24                               3-byte field, alignment 1 (U8 nodes)
 *   BE_ENUM(E, Storage)                  enum E stored as integer `Storage`
 *
 * Accessors (work in C and C++; in the Wii build they are the identity):
 *
 *   BE_GET(field)         value of the field (host byte order)
 *   BE_SET(field, value)  store value into the field
 *
 * C++ code does not need the accessors: be<T> converts implicitly to T and
 * assigning a T stores big-endian, so decompiled C++ that reads
 * `pHeader->fileSize` compiles unchanged. C has no operator overloading, so C
 * code must use BE_GET/BE_SET; on the host the C types are one-member structs,
 * so a forgotten BE_GET is a compile error rather than a silent wrong value.
 *
 * How the matching build stays unaffected: this file lives in
 * port/host/include, which only the host build puts on its include path (and
 * before include/). The matching build finds include/port/be.h instead, which
 * defines the same names as macros that expand to the plain types
 * (be_u32 -> u32) and BE_GET(x) -> (x), so the code CodeWarrior compiles is
 * the same as before the wrapper was added.
 */

#ifndef PORT_BE_H
#define PORT_BE_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <types.h>

/* 1 when the be_* types are wrappers (host), 0 when they are plain (Wii). */
#define PORT_BE_WRAPPED 1

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PORT_HOST_BIG_ENDIAN 1
#else
#define PORT_HOST_BIG_ENDIAN 0
#endif

/******************************************************************************
 * Byte-swap primitives (C and C++)
 ******************************************************************************/

#if defined(_MSC_VER) && !defined(__clang__)
#include <stdlib.h>
#define PORT_BSWAP16(x) _byteswap_ushort(x)
#define PORT_BSWAP32(x) _byteswap_ulong(x)
#define PORT_BSWAP64(x) _byteswap_uint64(x)
#else
#define PORT_BSWAP16(x) __builtin_bswap16(x)
#define PORT_BSWAP32(x) __builtin_bswap32(x)
#define PORT_BSWAP64(x) __builtin_bswap64(x)
#endif

#if PORT_HOST_BIG_ENDIAN
#define PORT_BE16(x) ((u16)(x))
#define PORT_BE32(x) ((u32)(x))
#define PORT_BE64(x) ((u64)(x))
#else
#define PORT_BE16(x) ((u16)PORT_BSWAP16((u16)(x)))
#define PORT_BE32(x) ((u32)PORT_BSWAP32((u32)(x)))
#define PORT_BE64(x) ((u64)PORT_BSWAP64((u64)(x)))
#endif

#ifdef __cplusplus
#include <cstring>
#else
#include <string.h>
#endif

/* Load/store raw big-endian bytes at an arbitrary (possibly unaligned)
 * address. Useful for code that reads file bytes without a struct. */
static inline u16 port_load_be16(const void* p) {
    u16 v;
    memcpy(&v, p, 2);
    return PORT_BE16(v);
}
static inline u32 port_load_be32(const void* p) {
    u32 v;
    memcpy(&v, p, 4);
    return PORT_BE32(v);
}
static inline u64 port_load_be64(const void* p) {
    u64 v;
    memcpy(&v, p, 8);
    return PORT_BE64(v);
}
static inline void port_store_be16(void* p, u16 v) {
    v = PORT_BE16(v);
    memcpy(p, &v, 2);
}
static inline void port_store_be32(void* p, u32 v) {
    v = PORT_BE32(v);
    memcpy(p, &v, 4);
}
static inline void port_store_be64(void* p, u64 v) {
    v = PORT_BE64(v);
    memcpy(p, &v, 8);
}

#ifdef __cplusplus
/******************************************************************************
 * C++: be<T>
 ******************************************************************************/

// RVLFaceLib's headers include this inside extern "C".
extern "C++" {

#include <type_traits>

namespace port {

template <std::size_t N> struct be_uint;
template <> struct be_uint<1> { typedef u8 type; };
template <> struct be_uint<2> { typedef u16 type; };
template <> struct be_uint<4> { typedef u32 type; };
template <> struct be_uint<8> { typedef u64 type; };

inline u8 be_swap(u8 x) { return x; }
inline u16 be_swap(u16 x) { return PORT_BE16(x); }
inline u32 be_swap(u32 x) { return PORT_BE32(x); }
inline u64 be_swap(u64 x) { return PORT_BE64(x); }

/**
 * A T stored big-endian. `Storage` is the on-disc representation; it defaults
 * to T and only differs for enums whose file field is narrower or wider than
 * the enum (BE_ENUM). The member is the raw unsigned integer, aligned like
 * Storage, so sizeof/alignof match the plain field exactly.
 */
template <typename T, typename Storage = T> class be {
    typedef typename be_uint<sizeof(Storage)>::type raw_t;

public:
    typedef T value_type;

    // Trivial default constructor: be<T> stays a POD-like type usable in
    // unions and structs that are overlaid on file bytes.
    be() = default;
    // explicit: with an implicit constructor, `cond ? u16 : be_u16` would be
    // ambiguous (each operand converts to the other's type).
    explicit be(T v) { set(v); }

    T get() const {
        raw_t r = be_swap(raw_);
        Storage s;
        std::memcpy(&s, &r, sizeof(s));
        return static_cast<T>(s);
    }

    void set(T v) {
        Storage s = static_cast<Storage>(v);
        raw_t r;
        std::memcpy(&r, &s, sizeof(r));
        raw_ = be_swap(r);
    }

    operator T() const { return get(); }

    // static_cast<SomeEnum>(field) on an integer field, as the decompiled
    // code does for format fields (e.g. static_cast<GXTexFmt>(sheetFormat)).
    template <typename E, typename = typename std::enable_if<
                              std::is_enum<E>::value>::type>
    explicit operator E() const {
        return static_cast<E>(get());
    }

    be& operator=(T v) {
        set(v);
        return *this;
    }

    // Compound assignment, for game code that updates file fields in place.
    template <typename U> be& operator+=(U v) { set(get() + v); return *this; }
    template <typename U> be& operator-=(U v) { set(get() - v); return *this; }
    template <typename U> be& operator|=(U v) { set(get() | v); return *this; }
    template <typename U> be& operator&=(U v) { set(get() & v); return *this; }
    template <typename U> be& operator^=(U v) { set(get() ^ v); return *this; }
    template <typename U> be& operator*=(U v) { set(get() * v); return *this; }
    template <typename U> be& operator/=(U v) { set(get() / v); return *this; }
    be& operator++() { set(get() + 1); return *this; }
    be& operator--() { set(get() - 1); return *this; }
    T operator++(int) { T o = get(); set(o + 1); return o; }
    T operator--(int) { T o = get(); set(o - 1); return o; }

    // The bytes exactly as they are in the file.
    raw_t raw() const { return raw_; }

private:
    alignas(Storage) raw_t raw_;
};

/** 3-byte big-endian unsigned field (alignment 1). */
class be24 {
public:
    be24() = default;
    be24(u32 v) { set(v); }
    u32 get() const {
        return (u32)b_[0] << 16 | (u32)b_[1] << 8 | (u32)b_[2];
    }
    void set(u32 v) {
        b_[0] = (u8)(v >> 16);
        b_[1] = (u8)(v >> 8);
        b_[2] = (u8)v;
    }
    operator u32() const { return get(); }
    be24& operator=(u32 v) {
        set(v);
        return *this;
    }

private:
    u8 b_[3];
};

template <typename T, typename S> inline T be_get(const be<T, S>& f) {
    return f.get();
}
inline u32 be_get(const be24& f) { return f.get(); }

// BE_STORED(T, v): v as a file stores it, in a temporary that lives until the
// end of the full expression, for code that writes a value's bytes out
// (EGG::Stream::write_u32 and friends, HOST_BUILD.md §22).
template <typename T> inline be<T>& be_stored(be<T>&& v) {
    return v;
}

} // namespace port

typedef port::be<u16> be_u16;
typedef port::be<s16> be_s16;
typedef port::be<u32> be_u32;
typedef port::be<s32> be_s32;
typedef port::be<u64> be_u64;
typedef port::be<s64> be_s64;
typedef port::be<f32> be_f32;
typedef port::be<f64> be_f64;
typedef port::be24 be_u24;
#define BE_ENUM(E, Storage) port::be<E, Storage>

#define BE_GET(field) (port::be_get(field))
#define BE_SET(field, value) ((field) = (value))
#define BE_STORED(T, v) (port::be_stored(port::be<T>(v)))

#define PORT_BE_CHECK(T, P)                                                    \
    static_assert(sizeof(T) == sizeof(P) && alignof(T) == alignof(P) &&        \
                      std::is_trivially_copyable<T>::value &&                  \
                      std::is_standard_layout<T>::value,                       \
                  "be wrapper must be layout-identical to the plain type")
PORT_BE_CHECK(be_u16, u16);
PORT_BE_CHECK(be_s16, s16);
PORT_BE_CHECK(be_u32, u32);
PORT_BE_CHECK(be_s32, s32);
PORT_BE_CHECK(be_f32, f32);
PORT_BE_CHECK(be_u64, u64);
PORT_BE_CHECK(be_f64, f64);
static_assert(sizeof(be_u24) == 3 && alignof(be_u24) == 1, "be_u24 layout");
#undef PORT_BE_CHECK

} // extern "C++"

#else /* !__cplusplus */
/******************************************************************************
 * C: one-member structs plus BE_GET/BE_SET (C11 _Generic)
 ******************************************************************************/

typedef struct { u16 raw_; } be_u16;
typedef struct { u16 raw_; } be_s16;
typedef struct { u32 raw_; } be_u32;
typedef struct { u32 raw_; } be_s32;
typedef struct { u32 raw_; } be_f32;
typedef struct { u8 b_[3]; } be_u24;
/* C enums are int-sized; a C enum field is declared with its storage type. */
#define BE_ENUM(E, Storage) be_##Storage

static inline u16 port_be_get_u16(be_u16 f) { return PORT_BE16(f.raw_); }
static inline s16 port_be_get_s16(be_s16 f) { return (s16)PORT_BE16(f.raw_); }
static inline u32 port_be_get_u32(be_u32 f) { return PORT_BE32(f.raw_); }
static inline s32 port_be_get_s32(be_s32 f) { return (s32)PORT_BE32(f.raw_); }
static inline f32 port_be_get_f32(be_f32 f) {
    u32 r = PORT_BE32(f.raw_);
    f32 v;
    memcpy(&v, &r, 4);
    return v;
}
static inline u32 port_be_get_u24(be_u24 f) {
    return (u32)f.b_[0] << 16 | (u32)f.b_[1] << 8 | (u32)f.b_[2];
}

static inline void port_be_set_u16(be_u16* f, u16 v) { f->raw_ = PORT_BE16(v); }
static inline void port_be_set_s16(be_s16* f, s16 v) { f->raw_ = PORT_BE16((u16)v); }
static inline void port_be_set_u32(be_u32* f, u32 v) { f->raw_ = PORT_BE32(v); }
static inline void port_be_set_s32(be_s32* f, s32 v) { f->raw_ = PORT_BE32((u32)v); }
static inline void port_be_set_f32(be_f32* f, f32 v) {
    u32 r;
    memcpy(&r, &v, 4);
    f->raw_ = PORT_BE32(r);
}
static inline void port_be_set_u24(be_u24* f, u32 v) {
    f->b_[0] = (u8)(v >> 16);
    f->b_[1] = (u8)(v >> 8);
    f->b_[2] = (u8)v;
}

#define BE_GET(field)                                                          \
    _Generic((field),                                                          \
        be_u16: port_be_get_u16,                                               \
        be_s16: port_be_get_s16,                                               \
        be_u32: port_be_get_u32,                                               \
        be_s32: port_be_get_s32,                                               \
        be_f32: port_be_get_f32,                                               \
        be_u24: port_be_get_u24)(field)

#define BE_SET(field, value)                                                   \
    _Generic((field),                                                          \
        be_u16: port_be_set_u16,                                               \
        be_s16: port_be_set_s16,                                               \
        be_u32: port_be_set_u32,                                               \
        be_s32: port_be_set_s32,                                               \
        be_f32: port_be_set_f32,                                               \
        be_u24: port_be_set_u24)(&(field), (value))

_Static_assert(sizeof(be_u32) == 4 && _Alignof(be_u32) == 4, "be_u32 layout");
_Static_assert(sizeof(be_u16) == 2 && _Alignof(be_u16) == 2, "be_u16 layout");
_Static_assert(sizeof(be_f32) == 4 && _Alignof(be_f32) == 4, "be_f32 layout");
_Static_assert(sizeof(be_u24) == 3 && _Alignof(be_u24) == 1, "be_u24 layout");

#endif /* __cplusplus */

/*
 * BE_BYTE(i, size): the index, in host memory, of byte i (0 = most significant)
 * of a size-byte integer. For code that looks at an integer's bytes through a
 * union, as the Wii (big-endian) lays them out.
 */
#if PORT_HOST_BIG_ENDIAN
#define BE_BYTE(i, size) (i)
#else
#define BE_BYTE(i, size) ((size) - 1 - (i))
#endif

#endif /* PORT_BE_H */
