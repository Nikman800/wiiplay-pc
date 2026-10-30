/**
 * Pointer slots inside loaded files (host build; port/docs/POINTERS.md M1).
 *
 * RES_PTR(T) declares a struct member that the file stores as a 32-bit
 * offset and that the game's loader overwrites in place with a pointer
 * (nw4r::ut::ResFont::Rebuild, TPL, ef, g3d). On the host it is
 * port::res_ptr<T>: 4 bytes, alignment 4, holding a big-endian u32 that is
 * either the unrelocated file value or, after relocation, an emulated Wii
 * address (M0, port/arena.h). Reading it decodes the address to a host
 * pointer; assigning a T* encodes it. Struct layouts and offsets therefore
 * stay the Wii ones on every host, and a relocated file in host memory is the
 * Wii image of that file byte for byte.
 *
 *   RES_PTR(T)              member type (Wii: T*)
 *   RES_PTR_GET(slot)       decoded pointer (Wii: slot)
 *   RES_PTR_SET(slot, p)    store a pointer (Wii: slot = p)
 *   RES_PTR_RAW(slot)       the u32 in the slot, host order (Wii: (u32)slot)
 *   PORT_PTR_TO_U32(p)      pointer -> 32-bit address; for a res_ptr slot the
 *                           raw u32 (Wii: reinterpret_cast<u32>(p))
 *   PORT_U32_TO_PTR(T, v)   32-bit address -> pointer of type T
 *   PORT_STORE_PTR32(s, p)  store p's 32-bit address in a u32 slot
 *
 * The Wii header, include/port/resptr.h, defines the same names as macros
 * for the plain types and casts.
 */

#ifndef PORT_RESPTR_H
#define PORT_RESPTR_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <port/arena.h>
#include <port/be.h>

#define PORT_RES_PTR_WRAPPED 1

#ifdef __cplusplus
#include <cstddef>
#include <type_traits>

namespace port {

template <typename T> class res_ptr {
public:
    typedef T element_type;

    // Trivial default constructor and copy: struct copies of file data copy
    // the 4 raw bytes.
    res_ptr() = default;

    T* get() const { return static_cast<T*>(port_hptr(v_.get())); }
    void set(T* p) { v_ = port_vaddr(p); }

    operator T*() const { return get(); }
    T* operator->() const { return get(); }

    res_ptr& operator=(T* p) {
        set(p);
        return *this;
    }
    res_ptr& operator=(std::nullptr_t) {
        v_ = 0;
        return *this;
    }

    /** The u32 in the slot (host byte order): the file offset before
     * relocation, the emulated address after. */
    u32 raw() const { return v_.get(); }
    void set_raw(u32 v) { v_ = v; }

    // `slot != 0`, `slot == NULL`: compare the raw value, so a test on an
    // unrelocated offset works like the Wii pointer test.
    template <typename I,
              typename = typename std::enable_if<std::is_integral<I>::value>::type>
    bool operator==(I v) const {
        return raw() == static_cast<u32>(v);
    }
    template <typename I,
              typename = typename std::enable_if<std::is_integral<I>::value>::type>
    bool operator!=(I v) const {
        return raw() != static_cast<u32>(v);
    }
    bool operator==(std::nullptr_t) const { return raw() == 0; }
    bool operator!=(std::nullptr_t) const { return raw() != 0; }

private:
    be<u32> v_;
};

inline u32 ptr_to_u32(const void* p) {
    return port_vaddr(p);
}
template <typename T> inline u32 ptr_to_u32(const res_ptr<T>& slot) {
    return slot.raw();
}

} // namespace port

#define RES_PTR(T) port::res_ptr<T>
#define RES_PTR_GET(slot) ((slot).get())
#define RES_PTR_SET(slot, p) ((slot) = (p))
#define RES_PTR_RAW(slot) ((slot).raw())
#define PORT_PTR_TO_U32(p) (port::ptr_to_u32(p))
#define PORT_U32_TO_PTR(T, v) (reinterpret_cast<T>(port_hptr(v)))

static_assert(sizeof(port::res_ptr<char>) == 4 &&
                  alignof(port::res_ptr<char>) == 4 &&
                  std::is_trivially_copyable<port::res_ptr<char> >::value &&
                  std::is_standard_layout<port::res_ptr<char> >::value,
              "res_ptr must be a 4-byte slot");

#else /* !__cplusplus */

typedef struct {
    be_u32 v;
} res_ptr_c;

static inline void* port_res_ptr_get(res_ptr_c s) {
    return port_hptr(BE_GET(s.v));
}
static inline void port_res_ptr_set(res_ptr_c* s, const void* p) {
    BE_SET(s->v, port_vaddr(p));
}

#define RES_PTR(T) res_ptr_c
#define RES_PTR_GET(slot) port_res_ptr_get(slot)
#define RES_PTR_SET(slot, p) port_res_ptr_set(&(slot), (p))
#define RES_PTR_RAW(slot) (BE_GET((slot).v))
#define PORT_PTR_TO_U32(p) (port_vaddr(p))
#define PORT_U32_TO_PTR(T, v) ((T)port_hptr(v))

_Static_assert(sizeof(res_ptr_c) == 4 && _Alignof(res_ptr_c) == 4,
               "res_ptr_c must be a 4-byte slot");

#endif /* __cplusplus */

#define PORT_STORE_PTR32(u32slot, p) ((u32slot) = PORT_PTR_TO_U32(p))

#endif
