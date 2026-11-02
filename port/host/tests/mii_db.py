#!/usr/bin/env python3
"""A Mii database (RFL_DB.dat) built from scratch, for tests (port/docs/HOST_BUILD.md §40).

The file is what the Wii Menu's Mii Channel keeps at /shared2/menu/FaceLib/RFL_DB.dat:
RFLiDatabase (0x1F1E0 bytes: "RNOD", 100 RFLiCharData records, the isolation flag, the
hidden database header "RNHD" with its 10000-entry list, the CRC), then the 10000 hidden
RFLiHiddenCharData records (0x40 bytes each) and the tail RFLiFormatAsync writes: 779968
bytes in all. Everything is big-endian with bit-fields from the most significant bit.

The Miis here are made from parameters written below (no data from a console or a disc).

    python3 mii_db.py OUT [NAME ...]    one Mii per name (default: one, "Tester")
"""

import struct
import sys

DB_SIZE = 0x1F1E0          # sizeof(RFLiDatabase)
FILE_SIZE = 779968         # what RFLiFormatAsync writes
CHAR_SIZE = 0x4A           # sizeof(RFLiCharData)
CHAR_MAX = 100
HIDDEN_OFS = 0x1D00
HIDDEN_MAX = 10000

# The 16-bit words of RFLiCharData after the name / height / build / create ID, each field
# (name, bits) from the most significant bit; padding fields are 0.
WORDS = [
    (0x20, [("faceType", 3), ("faceColor", 3), ("faceTex", 4), (None, 3), ("localonly", 1), ("type", 2)]),
    (0x22, [("hairType", 7), ("hairColor", 3), ("hairFlip", 1), (None, 5)]),
    (0x24, [("eyebrowType", 5), ("eyebrowRotate", 5), (None, 6)]),
    (0x26, [("eyebrowColor", 3), ("eyebrowScale", 4), ("eyebrowY", 5), ("eyebrowX", 4)]),
    (0x28, [("eyeType", 6), ("eyeRotate", 5), ("eyeY", 5)]),
    (0x2A, [("eyeColor", 3), ("eyeScale", 4), ("eyeX", 4), (None, 5)]),
    (0x2C, [("noseType", 4), ("noseScale", 4), ("noseY", 5), (None, 3)]),
    (0x2E, [("mouthType", 5), ("mouthColor", 2), ("mouthScale", 4), ("mouthY", 5)]),
    (0x30, [("glassType", 4), ("glassColor", 3), ("glassScale", 4), ("glassY", 5)]),
    (0x32, [("mustacheType", 2), ("beardType", 2), ("beardColor", 3), ("beardScale", 4), ("beardY", 5)]),
    (0x34, [("moleType", 1), ("moleScale", 4), ("moleY", 5), ("moleX", 5), (None, 1)]),
]
HEAD = [(None, 1), ("sex", 1), ("birthMonth", 4), ("birthDay", 5), ("favoriteColor", 4), ("favorite", 1)]

# A plain face in the middle of every range (RFLiCheckValidInfo's limits in RFLi_Config.h).
FACE = {
    "faceType": 1, "faceColor": 1, "faceTex": 0, "localonly": 0, "type": 0,
    "hairType": 30, "hairColor": 2, "hairFlip": 0,
    "eyebrowType": 3, "eyebrowRotate": 6, "eyebrowColor": 2, "eyebrowScale": 4, "eyebrowY": 10, "eyebrowX": 2,
    "eyeType": 4, "eyeRotate": 4, "eyeY": 12, "eyeColor": 2, "eyeScale": 4, "eyeX": 2,
    "noseType": 1, "noseScale": 4, "noseY": 9,
    "mouthType": 6, "mouthColor": 0, "mouthScale": 4, "mouthY": 13,
    "glassType": 0, "glassColor": 0, "glassScale": 4, "glassY": 10,
    "mustacheType": 0, "beardType": 0, "beardColor": 0, "beardScale": 4, "beardY": 10,
    "moleType": 0, "moleScale": 4, "moleY": 20, "moleX": 2,
    "sex": 0, "birthMonth": 0, "birthDay": 0, "favoriteColor": 4, "favorite": 0,
}


def pack_word(fields, values):
    w, bit = 0, 16
    for name, n in fields:
        bit -= n
        v = values.get(name, 0) if name else 0
        assert 0 <= v < (1 << n), (name, v)
        w |= v << bit
    assert bit == 0
    return w


def char_data(name, create_id, creator="", height=64, build=64, **face):
    """One RFLiCharData record (0x4A bytes)."""
    values = dict(FACE, **face)
    b = bytearray(CHAR_SIZE)
    struct.pack_into(">H", b, 0, pack_word(HEAD, values))
    b[0x02:0x16] = name.encode("utf-16-be")[:20].ljust(20, b"\0")
    b[0x16], b[0x17] = height, build
    b[0x18:0x20] = create_id
    for ofs, fields in WORDS:
        struct.pack_into(">H", b, ofs, pack_word(fields, values))
    b[0x36:0x4A] = creator.encode("utf-16-be")[:20].ljust(20, b"\0")
    return bytes(b)


def create_id(n):
    """A create ID as the Mii Channel makes them: the top bit set (not a special Mii), the
    temporary bit (0x20000000) clear, a counter, then four console-specific bytes."""
    return struct.pack(">II", 0x80000000 | (0x0A000000 + n), 0x0C0FFEE0 + n)


def crc16(data):
    """RFLiCalculateCRC (CRC-16/CCITT, no augmentation)."""
    crc = 0
    for byte in data:
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
            if byte & 0x80:
                crc ^= 1
            byte = (byte << 1) & 0xFF
    return crc


def database(records):
    """The whole file; records: list of (slot, RFLiCharData bytes)."""
    db = bytearray(DB_SIZE)
    db[0:4] = b"RNOD"
    for slot, rec in records:
        db[4 + slot * CHAR_SIZE:4 + (slot + 1) * CHAR_SIZE] = rec
    db[0x1CEC] = 0x80  # isolation, as RFLiClearDBBuffer leaves it
    db[HIDDEN_OFS:HIDDEN_OFS + 4] = b"RNHD"
    struct.pack_into(">hh", db, HIDDEN_OFS + 4, -1, -1)  # head, tail: an empty list
    for i in range(HIDDEN_MAX):  # RFLiClearTableData: next = prev = -1 (15 bits)
        struct.pack_into(">HH", db, HIDDEN_OFS + 8 + i * 12 + 8, 0x7FFF, 0x7FFF)
    struct.pack_into(">H", db, DB_SIZE - 2, crc16(db))  # the CRC over the whole is then 0
    assert crc16(db) == 0
    return bytes(db) + bytes(FILE_SIZE - DB_SIZE)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    names = sys.argv[2:] or ["Tester"]
    recs = [(i, char_data(n, create_id(i), creator="Test")) for i, n in enumerate(names)]
    with open(sys.argv[1], "wb") as f:
        f.write(database(recs))


if __name__ == "__main__":
    main()
