/**
 * List links inside loaded files: Wii (matching) build.
 *
 * nw4r::ef::Resource keeps the loaded BREFF and BREFT projects in two
 * ut::Lists whose links live inside the files (EffectProject::projectlink,
 * TextureProject::projectlink). The PC port declares those links RES_LINK and
 * the lists RES_LIST; on the host the link is two 4-byte RES_PTR slots
 * (port/host/include/port/reslist.h, see port/docs/POINTERS.md M2).
 *
 * Here every macro expands to the type or function it replaces, so
 * CodeWarrior compiles the same code as before.
 */

#ifndef PORT_RESLIST_H
#define PORT_RESLIST_H

#if defined(PORT_HOST)
#error "host builds must find port/host/include/port/reslist.h first"
#endif

/* 1 when RES_LINK is the host's 8-byte slot pair, 0 when it is ut::Link. */
#define PORT_RES_LINK_WRAPPED 0

#define RES_LINK nw4r::ut::Link
#define RES_LIST nw4r::ut::List

#define RES_LIST_Init nw4r::ut::List_Init
#define RES_LIST_Append nw4r::ut::List_Append
#define RES_LIST_Remove nw4r::ut::List_Remove
#define RES_LIST_GetFirst nw4r::ut::List_GetFirst
#define RES_LIST_GetLast nw4r::ut::List_GetLast
#define RES_LIST_GetNext nw4r::ut::List_GetNext
#define RES_LIST_GetPrev nw4r::ut::List_GetPrev

#endif
