/**
 * Unit test for port/be.h (C side): BE_GET/BE_SET through _Generic.
 */
#include <port/be.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    be_u32 magic;
    u8 type;
    be_u24 name;
    be_s32 offset;
    be_u16 count;
    be_s16 delta;
    be_f32 scale;
} Header;

_Static_assert(sizeof(Header) == 20, "layout");

int main(void) {
    static const unsigned char bytes[20] = {
        0x55, 0xAA, 0x38, 0x2D, 1,    0x00, 0x01, 0x02, 0xFF, 0xFF,
        0xFF, 0xF0, 0x12, 0x34, 0xFF, 0xFE, 0x3F, 0xC0, 0x00, 0x00};
    Header h;
    int failed = 0;
    memcpy(&h, bytes, sizeof(h));

    failed += BE_GET(h.magic) != 0x55AA382Du;
    failed += BE_GET(h.name) != 0x000102u;
    failed += BE_GET(h.offset) != -16;
    failed += BE_GET(h.count) != 0x1234;
    failed += BE_GET(h.delta) != -2;
    failed += BE_GET(h.scale) != 1.5f;

    BE_SET(h.count, 0xABCD);
    BE_SET(h.name, 0x123456);
    BE_SET(h.scale, -2.0f);
    {
        unsigned char out[20];
        memcpy(out, &h, sizeof(out));
        failed += out[12] != 0xAB || out[13] != 0xCD;
        failed += out[5] != 0x12 || out[6] != 0x34 || out[7] != 0x56;
        failed += out[16] != 0xC0;
    }

    printf("test_be (C): %s\n", failed ? "FAILED" : "OK");
    return failed ? 1 : 0;
}
