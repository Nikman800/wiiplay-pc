/**
 * Unit test for port/be.h (C++ side): layout, reads, writes, enums, u24.
 */
#include <port/be.h>

#include <cstdio>
#include <cstring>

namespace {

int sFailed = 0;
#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
            sFailed++;                                                         \
        }                                                                      \
    } while (0)

enum Kind { KIND_A = 1, KIND_B = 0x0203 };

// A struct overlaid on file bytes; layout must match the Wii's.
struct FileHeader {
    be_u32 magic;               // 0x0
    u8 flag;                    // 0x4
    be_u16 count;               // 0x6 (2-byte aligned, as on the Wii)
    be_f32 scale;               // 0x8
    BE_ENUM(Kind, u16) kind16;  // 0xC
    BE_ENUM(Kind, u32) kind32;  // 0x10
    be_s16 delta;               // 0x14
    be_s32 offset;              // 0x18
};
static_assert(sizeof(FileHeader) == 0x1C, "layout");
static_assert(offsetof(FileHeader, count) == 0x6, "layout");
static_assert(offsetof(FileHeader, scale) == 0x8, "layout");
static_assert(offsetof(FileHeader, kind32) == 0x10, "layout");
static_assert(offsetof(FileHeader, offset) == 0x18, "layout");

} // namespace

int main() {
    alignas(4) const u8 bytes[0x1C] = {
        'R', 'A', 'R', 'C',       // magic
        0x80, 0x00,               // flag, pad
        0x12, 0x34,               // count
        0x3F, 0xC0, 0x00, 0x00,   // 1.5f
        0x02, 0x03, 0x00, 0x00,   // kind16, pad
        0x00, 0x00, 0x00, 0x01,   // kind32
        0xFF, 0xFE, 0x00, 0x00,   // -2, pad
        0xFF, 0xFF, 0xFF, 0xF0,   // -16
    };
    FileHeader h;
    std::memcpy(&h, bytes, sizeof(h));

    CHECK(h.magic == 0x52415243u);
    CHECK(h.flag == 0x80);
    CHECK(h.count == 0x1234);
    CHECK(h.scale == 1.5f);
    CHECK(h.kind16 == KIND_B);
    CHECK(h.kind32 == KIND_A);
    CHECK(h.delta == -2);
    CHECK(h.offset == -16);
    CHECK(BE_GET(h.count) == 0x1234);

    // Writes store big-endian bytes.
    h.count = 0xABCD;
    h.scale = -2.0f;
    h.kind16 = KIND_A;
    h.offset += 0x20; // -16 + 32 = 16
    h.magic |= 0x20;  // 'RARC' -> 'RARc'
    BE_SET(h.delta, (s16)7);
    u8 out[0x1C];
    std::memcpy(out, &h, sizeof(out));
    CHECK(out[6] == 0xAB && out[7] == 0xCD);
    CHECK(out[8] == 0xC0 && out[9] == 0x00);
    CHECK(out[12] == 0x00 && out[13] == 0x01);
    CHECK(out[24] == 0 && out[25] == 0 && out[26] == 0 && out[27] == 0x10);
    CHECK(out[3] == 'c');
    CHECK(out[20] == 0 && out[21] == 7);

    // u24 (U8 archive node name offsets).
    struct Node {
        u8 type;
        be_u24 name;
        be_u32 a;
    };
    static_assert(sizeof(Node) == 8, "node layout");
    const u8 nb[8] = {1, 0x01, 0x02, 0x03, 0, 0, 0, 9};
    Node n;
    std::memcpy(&n, nb, 8);
    CHECK(n.type == 1 && n.name == 0x010203u && n.a == 9u);

    // Raw access and unaligned helpers.
    CHECK(port_load_be32(bytes + 1) == 0x41524380u);
    u8 tmp[3] = {};
    port_store_be16(tmp + 1, 0xBEEF);
    CHECK(tmp[1] == 0xBE && tmp[2] == 0xEF);

    std::printf("test_be (C++): %s\n", sFailed ? "FAILED" : "OK");
    return sFailed ? 1 : 0;
}
