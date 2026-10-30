// Unit tests for POINTERS.md step 6: RES_LINK / RES_LIST (M2) on objects in
// the M0 arena, and the ef resource slots written through RES_PTR and
// PORT_STORE_PTR32. The disc-wide check is ef_harness + test_effects.py.
#include <nw4r/ef.h>

#include <port/arena.h>
#include <port/be.h>
#include <port/reslist.h>

#include <cstdio>
#include <cstring>

using namespace nw4r::ef;

static int sFailures = 0;

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            std::fprintf(stderr, "test_ef: %s:%d: %s\n", __FILE__, __LINE__, #c); \
            sFailures++;                                                       \
        }                                                                      \
    } while (0)

static u32 Be32(const void* p) {
    return port_load_be32(p);
}

int main() {
    // Three project headers inside one arena buffer, link at +0x4.
    u8* buf = static_cast<u8*>(PortArenaAlloc(0x60, 32));
    std::memset(buf, 0xCC, 0x60);
    EffectProject* a = reinterpret_cast<EffectProject*>(buf);
    EffectProject* b = reinterpret_cast<EffectProject*>(buf + 0x20);
    EffectProject* c = reinterpret_cast<EffectProject*>(buf + 0x40);
    u32 va = port_vaddr(a), vb = port_vaddr(b), vc = port_vaddr(c);

    RES_LIST list;
    RES_LIST_Init(&list, offsetof(EffectProject, projectlink));
    CHECK(RES_LIST_GetFirst(&list) == NULL && RES_LIST_GetLast(&list) == NULL);

    // Resource::Add: memset the link, then append.
    EffectProject* objs[3] = {a, b, c};
    for (EffectProject* p : objs) {
        std::memset(&p->projectlink, 0, sizeof(RES_LINK));
        RES_LIST_Append(&list, p);
    }
    CHECK(list.numObjects == 3);
    // The links hold big-endian emulated addresses, as on the Wii.
    CHECK(Be32(buf + 0x04) == 0 && Be32(buf + 0x08) == vb);
    CHECK(Be32(buf + 0x24) == va && Be32(buf + 0x28) == vc);
    CHECK(Be32(buf + 0x44) == vb && Be32(buf + 0x48) == 0);
    // Bytes outside the links are untouched.
    CHECK(buf[0x00] == 0xCC && buf[0x0C] == 0xCC && buf[0x2C] == 0xCC);
    CHECK(RES_LIST_GetFirst(&list) == a && RES_LIST_GetLast(&list) == c);
    CHECK(RES_LIST_GetNext(&list, a) == b && RES_LIST_GetPrev(&list, c) == b);
    CHECK(RES_LIST_GetPrev(&list, a) == NULL && RES_LIST_GetNext(&list, c) == NULL);

    // Remove the middle one: the neighbours' links in the other "files" change.
    RES_LIST_Remove(&list, b);
    CHECK(list.numObjects == 2);
    CHECK(Be32(buf + 0x08) == vc && Be32(buf + 0x44) == va);
    CHECK(Be32(buf + 0x24) == 0 && Be32(buf + 0x28) == 0);
    RES_LIST_Remove(&list, a);
    RES_LIST_Remove(&list, c);
    CHECK(RES_LIST_GetFirst(&list) == NULL && list.numObjects == 0);

    // Layout of the slots the relocation writes.
    static_assert(sizeof(RES_LINK) == 8, "RES_LINK is 8 bytes");
    static_assert(offsetof(EffectProject, projectnameLen) == 0xC, "EffectProject");
    static_assert(sizeof(TextureData) == 0x20, "TextureData");
    static_assert(sizeof(EmitterResource) == 8, "EmitterResource");

    // TextureData slots (breftRelocate) and a track pointer (LocateTracks).
    u8* tex = static_cast<u8*>(PortArenaAlloc(0x40, 32));
    std::memset(tex, 0, 0x40);
    TextureData* t = reinterpret_cast<TextureData*>(tex);
    t->dataSize = 0x10;
    t->texture = tex + sizeof(TextureData);
    t->tlut = tex + sizeof(TextureData) + t->dataSize;
    CHECK(Be32(tex + 0x8) == 0x10);
    CHECK(Be32(tex + 0x18) == port_vaddr(tex) + 0x20);
    CHECK(Be32(tex + 0x1C) == port_vaddr(tex) + 0x30);
    CHECK(t->texture == tex + 0x20 && t->tlut == tex + 0x30);

    be_u32 slot;
    PORT_STORE_PTR32(slot, tex + 4);
    CHECK(Be32(&slot) == port_vaddr(tex) + 4);
    CHECK(PORT_U32_TO_PTR(u8*, slot) == tex + 4);

    PortArenaFree(tex);
    PortArenaFree(buf);

    if (sFailures != 0) {
        return 1;
    }
    std::printf("test_ef: OK (pointer size %zu)\n", sizeof(void*));
    return 0;
}
