/**
 * Unit tests for the M0 arena, RES_PTR slots and the 16-bit wchar_t
 * functions (port/arena.h, port/resptr.h, port/wide.h).
 */

#include <port/arena.h>
#include <port/resptr.h>
#include <port/wide.h>

#include <revolution/OS/OSAddress.h>

#include <cstdio>
#include <cstring>

namespace {

int sFailures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,       \
                         __LINE__, #cond);                                     \
            sFailures++;                                                       \
        }                                                                      \
    } while (0)

struct Node {
    be_u32 value;
    RES_PTR(Node) pNext;
};
static_assert(sizeof(Node) == 8, "file struct with a RES_PTR stays 8 bytes");

void TestArena() {
    PortArenaInit();
    u8* mem1 = static_cast<u8*>(PortArenaMEM1Lo());
    u8* mem2 = static_cast<u8*>(PortArenaMEM2Lo());
    CHECK(port_vaddr(mem1) == 0x80003100u);
    CHECK(port_vaddr(mem2) == 0x90000000u);
    CHECK(port_vaddr(static_cast<u8*>(PortArenaMEM2Hi()) - 1) == 0x93FFFFFFu);
    CHECK(port_vaddr(static_cast<u8*>(PortArenaMEM1Hi()) - 1) == 0x817FFFFFu);
    CHECK(port_vaddr(NULL) == 0);
    CHECK(port_hptr(0) == NULL);
    CHECK(port_hptr(0x90000000u) == mem2);
    CHECK(port_hptr(port_vaddr(mem1 + 123)) == mem1 + 123);

    // Physical/cached conversions (host OSAddress.h).
    CHECK(OSCachedToPhysical(mem2) == 0x10000000u);
    CHECK(OSPhysicalToCached(0x10000000u) == mem2);
    CHECK(OSPhysicalToUncached(0x10000000u) == mem2);

    // Allocation: alignment, inside the arena, reuse after free.
    size_t before = PortArenaUsed();
    void* a = PortArenaAlloc(100, 32);
    void* b = PortArenaAlloc(1000, -64);
    void* c = PortArenaAlloc(1, 4);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK((reinterpret_cast<uintptr_t>(a) & 31) == 0);
    CHECK((reinterpret_cast<uintptr_t>(b) & 63) == 0);
    CHECK(port_in_arena(a) && port_in_arena(b) && port_in_arena(c));
    CHECK((port_vaddr(a) & 0xF0000000u) == 0x90000000u); // MEM2 first
    std::memset(a, 0xAA, 100);
    std::memset(b, 0xBB, 1000);
    PortArenaFree(b);
    void* b2 = PortArenaAlloc(1000, 64);
    CHECK(b2 == b);
    PortArenaFree(a);
    PortArenaFree(b2);
    PortArenaFree(c);
    CHECK(PortArenaUsed() == before);

    // operator new comes from the arena too.
    int* pInt = new int(5);
    CHECK(port_in_arena(pInt));
    delete pInt;

    // A big block falls back to MEM1 when MEM2 cannot hold it.
    void* big = PortArenaAlloc(60u << 20, 32);
    void* more = PortArenaAlloc(16u << 20, 32);
    CHECK(big != NULL && more != NULL);
    CHECK((port_vaddr(big) & 0xF0000000u) == 0x90000000u);
    CHECK((port_vaddr(more) & 0xF0000000u) == 0x80000000u);
    PortArenaFree(big);
    PortArenaFree(more);
    CHECK(PortArenaUsed() == before);
}

void TestResPtr() {
    // A tiny "file": two nodes, links stored as file offsets (big-endian).
    u8* file = static_cast<u8*>(PortArenaAlloc(32, 32));
    std::memset(file, 0, 32);
    Node* n0 = reinterpret_cast<Node*>(file);
    Node* n1 = reinterpret_cast<Node*>(file + 8);
    port_store_be32(file + 0, 7);
    port_store_be32(file + 4, 8); // n0.pNext = offset 8
    port_store_be32(file + 8, 9);
    port_store_be32(file + 12, 0); // n1.pNext = NULL

    CHECK(RES_PTR_RAW(n0->pNext) == 8);
    CHECK(PORT_PTR_TO_U32(n0->pNext) == 8);
    CHECK(n0->pNext != 0);
    CHECK(n1->pNext == 0);
    CHECK(n1->pNext == NULL);

    // Relocate in place, as ResFont::Rebuild does.
    n0->pNext = reinterpret_cast<Node*>(file + RES_PTR_RAW(n0->pNext));
    CHECK(n0->pNext == n1);
    CHECK(n0->pNext->value == 9u);
    CHECK(RES_PTR_GET(n0->pNext) == n1);
    CHECK(RES_PTR_RAW(n0->pNext) == port_vaddr(n1));
    // The slot holds the big-endian Wii address: the file image is the Wii one.
    CHECK(port_load_be32(file + 4) == port_vaddr(file) + 8);

    // Walk the chain the way readers do.
    int count = 0;
    for (const Node* p = n0; p != NULL; p = p->pNext) {
        count++;
    }
    CHECK(count == 2);

    // Struct copies copy the raw slot.
    Node copy = *n0;
    CHECK(copy.pNext == n1);

    RES_PTR_SET(n1->pNext, n0);
    CHECK(n1->pNext->pNext == n1);
    n1->pNext = nullptr;
    CHECK(n1->pNext == NULL);

    // Cast macros.
    u32 v = PORT_PTR_TO_U32(n1);
    CHECK(v == port_vaddr(n1));
    CHECK(PORT_U32_TO_PTR(Node*, v) == n1);
    u32 slot32;
    PORT_STORE_PTR32(slot32, n1);
    CHECK(slot32 == v);

    PortArenaFree(file);
}

void TestWide() {
    static_assert(sizeof(wchar_t) == 2, "-fshort-wchar");
    static_assert(sizeof(L"ab") == 6, "UTF-16 literals");

    const wchar_t* s = L"Wii Play é";
    CHECK(wcslen(s) == 10);
    CHECK(s[9] == 0xE9);

    wchar_t buf[16];
    CHECK(wcscpy(buf, s) == buf);
    CHECK(wcscmp(buf, s) == 0);
    CHECK(wcscmp(L"abc", L"abd") < 0);
    CHECK(wcschr(buf, L'P') == buf + 4);
    CHECK(wcschr(buf, L'z') == NULL);

    wchar_t pad[8];
    std::memset(pad, 0xFF, sizeof(pad));
    wcsncpy(pad, L"ab", 5);
    CHECK(pad[0] == L'a' && pad[1] == L'b' && pad[2] == 0 && pad[4] == 0);
    CHECK(pad[5] == static_cast<wchar_t>(0xFFFF));

    wchar_t m[4];
    CHECK(wmemcpy(m, L"xyz", 4) == m);
    CHECK(wmemchr(m, L'z', 4) == m + 2);
    CHECK(wcslen(m) == 3);
}

} // namespace

int main() {
    TestArena();
    TestResPtr();
    TestWide();
    if (sFailures != 0) {
        std::printf("test_port: %d failures\n", sFailures);
        return 1;
    }
    std::printf("test_port: OK (pointer size %zu, wchar_t %zu)\n",
                sizeof(void*), sizeof(wchar_t));
    return 0;
}
