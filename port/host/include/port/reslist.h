/**
 * List links inside loaded files: host build (port/docs/POINTERS.md M2).
 *
 * ut::Link is two pointers: 8 bytes on the Wii, 16 on a 64-bit host. The
 * nw4r::ef project headers hold one inside the file (+0x4), so on the host
 * the link is RES_LINK: two RES_PTR slots (8 bytes, big-endian emulated Wii
 * addresses, exactly the bytes the Wii stores). RES_LIST is the list head; it
 * is a runtime object (ef::Resource), so it keeps native pointers. The
 * RES_LIST_* functions are ut::List_* (src/nw4r/ut/ut_list.cpp) over these
 * types (src/host_reslist.cpp).
 */

#ifndef PORT_RESLIST_H
#define PORT_RESLIST_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <port/resptr.h>

#define PORT_RES_LINK_WRAPPED 1

namespace port {

struct res_link {
    RES_PTR(void) prevObject; // at 0x0
    RES_PTR(void) nextObject; // at 0x4
};

struct res_list {
    void* headObject;
    void* tailObject;
    u16 numObjects;
    u16 offset; // offset of the res_link inside each object
};

void res_list_init(res_list* pList, u16 offset);
void res_list_append(res_list* pList, void* pObject);
void res_list_prepend(res_list* pList, void* pObject);
void res_list_remove(res_list* pList, void* pObject);
void* res_list_get_next(const res_list* pList, const void* pObject);
void* res_list_get_prev(const res_list* pList, const void* pObject);

inline void* res_list_get_first(const res_list* pList) {
    return res_list_get_next(pList, NULL);
}
inline void* res_list_get_last(const res_list* pList) {
    return res_list_get_prev(pList, NULL);
}

static_assert(sizeof(res_link) == 8 && alignof(res_link) == 4,
              "res_link must keep the Wii ut::Link layout");

} // namespace port

#define RES_LINK port::res_link
#define RES_LIST port::res_list

#define RES_LIST_Init port::res_list_init
#define RES_LIST_Append port::res_list_append
#define RES_LIST_Remove port::res_list_remove
#define RES_LIST_GetFirst port::res_list_get_first
#define RES_LIST_GetLast port::res_list_get_last
#define RES_LIST_GetNext port::res_list_get_next
#define RES_LIST_GetPrev port::res_list_get_prev

#endif
