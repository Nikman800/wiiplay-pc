"""Read the player's own extracted Wii Play disc: Yaz0 decompression, U8 archives, and a walker that
yields every file, including files inside (nested) archives.

    from wiiarc import walk
    for path, data in walk("/path/to/extracted/files"):
        ...

Paths inside archives look like "Common/RPWlyScene/common.carc/arc/Wly.brres". Nothing here writes
disc data anywhere; callers must not commit it either.
"""
import os
import struct

U8_MAGIC = b"\x55\xaa\x38\x2d"


def yaz0(data):
    """Decompress a Yaz0 stream (header 'Yaz0', u32 decompressed size, 8 reserved bytes)."""
    if data[:4] != b"Yaz0":
        raise ValueError("not Yaz0")
    size = struct.unpack(">I", data[4:8])[0]
    out = bytearray()
    src = 16
    while len(out) < size:
        code = data[src]
        src += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> bit):
                out.append(data[src])
                src += 1
            else:
                b1, b2 = data[src], data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                count = b1 >> 4
                if count == 0:
                    count = data[src] + 0x12
                    src += 1
                else:
                    count += 2
                for _ in range(count):
                    out.append(out[-dist])
    return bytes(out)


def u8(data):
    """List a U8 archive: returns [(path, bytes)] for its files (directories are walked)."""
    if data[:4] != U8_MAGIC:
        raise ValueError("not U8")
    root, _header_size, _data_offset = struct.unpack(">III", data[4:16])
    total = struct.unpack(">I", data[root + 8:root + 12])[0]
    strings = root + total * 12

    def name(off):
        end = data.index(b"\0", strings + off)
        return data[strings + off:end].decode("ascii", "replace")

    files = []
    ends = [(total, "")]
    for i in range(1, total):
        while ends and i >= ends[-1][0]:
            ends.pop()
        node = root + i * 12
        is_dir = data[node]
        name_off = int.from_bytes(data[node + 1:node + 4], "big")
        a, b = struct.unpack(">II", data[node + 4:node + 12])
        prefix = ends[-1][1] if ends else ""
        path = (prefix + "/" + name(name_off)).lstrip("/")
        if is_dir:
            ends.append((b, path))
        else:
            files.append((path, data[a:a + b]))
    return files


def unpack(data):
    """Decompress if Yaz0; return the bytes."""
    return yaz0(data) if data[:4] == b"Yaz0" else data


def walk(root, nested=True):
    """Yield (path, bytes) for every file under root, descending into Yaz0/U8 archives."""
    for dirpath, _dirs, names in os.walk(root):
        for n in sorted(names):
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, root)
            with open(full, "rb") as f:
                data = f.read()
            yield from _expand(rel, data, nested)


def _expand(path, data, nested):
    data = unpack(data)
    if nested and data[:4] == U8_MAGIC:
        for inner, blob in u8(data):
            yield from _expand(path + "/" + inner, blob, nested)
    else:
        yield path, data


if __name__ == "__main__":
    import collections
    import sys

    counts = collections.Counter()
    for p, d in walk(sys.argv[1]):
        counts[d[:4]] += 1
    for magic, n in counts.most_common():
        print(magic, n)
