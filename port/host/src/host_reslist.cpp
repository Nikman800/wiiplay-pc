// RES_LIST_* (port/host/include/port/reslist.h): ut::List_* from
// src/nw4r/ut/ut_list.cpp, with the link stored as two RES_PTR slots so a
// link inside a loaded file holds the big-endian emulated Wii addresses the
// Wii would store there. Same algorithm, same order of writes.

#include <port/reslist.h>

namespace port {

static res_link* GetLink(const res_list* pList, const void* pObject) {
    return reinterpret_cast<res_link*>(
        const_cast<u8*>(static_cast<const u8*>(pObject)) + pList->offset);
}

void res_list_init(res_list* pList, u16 offset) {
    pList->headObject = NULL;
    pList->tailObject = NULL;
    pList->numObjects = 0;
    pList->offset = offset;
}

static void SetFirstObject(res_list* pList, void* pObject) {
    res_link* pLink = GetLink(pList, pObject);
    pLink->nextObject = NULL;
    pLink->prevObject = NULL;
    pList->headObject = pObject;
    pList->tailObject = pObject;
    pList->numObjects++;
}

void res_list_append(res_list* pList, void* pObject) {
    if (pList->headObject == NULL) {
        SetFirstObject(pList, pObject);
        return;
    }
    res_link* pLink = GetLink(pList, pObject);
    pLink->prevObject = pList->tailObject;
    pLink->nextObject = NULL;
    GetLink(pList, pList->tailObject)->nextObject = pObject;
    pList->tailObject = pObject;
    pList->numObjects++;
}

void res_list_prepend(res_list* pList, void* pObject) {
    if (pList->headObject == NULL) {
        SetFirstObject(pList, pObject);
        return;
    }
    res_link* pLink = GetLink(pList, pObject);
    pLink->prevObject = NULL;
    pLink->nextObject = pList->headObject;
    GetLink(pList, pList->headObject)->prevObject = pObject;
    pList->headObject = pObject;
    pList->numObjects++;
}

void res_list_remove(res_list* pList, void* pObject) {
    res_link* pLink = GetLink(pList, pObject);
    void* pPrev = pLink->prevObject;
    void* pNext = pLink->nextObject;

    if (pPrev == NULL) {
        pList->headObject = pNext;
    } else {
        GetLink(pList, pPrev)->nextObject = pNext;
    }

    if (pNext == NULL) {
        pList->tailObject = pPrev;
    } else {
        GetLink(pList, pNext)->prevObject = pPrev;
    }

    pLink->prevObject = NULL;
    pLink->nextObject = NULL;
    pList->numObjects--;
}

void* res_list_get_next(const res_list* pList, const void* pObject) {
    if (pObject == NULL) {
        return pList->headObject;
    }
    return GetLink(pList, pObject)->nextObject;
}

void* res_list_get_prev(const res_list* pList, const void* pObject) {
    if (pObject == NULL) {
        return pList->tailObject;
    }
    return GetLink(pList, pObject)->prevObject;
}

} // namespace port
