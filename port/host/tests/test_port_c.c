/**
 * C side of the RES_PTR slot and the 16-bit wchar_t functions (host build).
 */

#include <port/arena.h>
#include <port/resptr.h>
#include <port/wide.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    be_u32 value;
    RES_PTR(void) pNext;
} CNode;

_Static_assert(sizeof(CNode) == 8, "C file struct with a RES_PTR stays 8 bytes");
_Static_assert(sizeof(wchar_t) == 2, "-fshort-wchar");

static int sFailures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                    \
            sFailures++;                                                       \
        }                                                                      \
    } while (0)

int main(void) {
    u8* file = (u8*)PortArenaAlloc(16, 32);
    CNode* n0 = (CNode*)file;
    CNode* n1 = (CNode*)(file + 8);
    wchar_t buf[8];

    memset(file, 0, 16);
    port_store_be32(file + 4, 8);

    CHECK(RES_PTR_RAW(n0->pNext) == 8);
    RES_PTR_SET(n0->pNext, file + RES_PTR_RAW(n0->pNext));
    CHECK(RES_PTR_GET(n0->pNext) == (void*)n1);
    CHECK(port_load_be32(file + 4) == PORT_PTR_TO_U32(n1));
    CHECK(PORT_U32_TO_PTR(CNode*, PORT_PTR_TO_U32(n1)) == n1);
    CHECK(RES_PTR_GET(n1->pNext) == NULL);
    PortArenaFree(file);

    CHECK(wcslen(L"abc") == 3);
    wcscpy(buf, L"Mii");
    CHECK(wcscmp(buf, L"Mii") == 0);

    if (sFailures != 0) {
        printf("test_port (C): %d failures\n", sFailures);
        return 1;
    }
    printf("test_port (C): OK\n");
    return 0;
}
