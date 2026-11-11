#!/usr/bin/env python3
"""Minimal GameCube disc with a real RARC (forest_1st.arc) and a BFN1 font.

The image is enough for pc_disc_init, JKRAramArchive::open, and JUTResFont.
It is not a playable GAFE01 dump.
"""
import struct
import sys


def be32(n):
    return struct.pack(">I", n)


def be16(n):
    return struct.pack(">H", n)


def bfn1():
    """Big-endian BFN1: INF1, WID1, MAP1, GLY1. Same shape the PC font parser expects after bswap."""
    chunks = []
    chunks.append(be32(0x494E4631) + be32(0x14) + be16(0) + be16(12) + be16(2) + be16(8) + be16(0) + be16(0x20))
    chunks.append(be32(0x57494431) + be32(0x10) + be16(0x20) + be16(0x7E) + b"\x08\x00\x00\x00")
    chunks.append(be32(0x4D415031) + be32(0x14) + be16(0) + be16(0x20) + be16(0x20) + be16(1) + be16(0) + be16(0))
    chunks.append(be32(0x474C5931) + be32(0x20) + be16(0x20) + be16(0x20) + be16(1) + be16(1) + be32(0) + be16(0) + be16(1) + be16(1) + be16(1) + be16(1) + be16(0))
    body = b"".join(chunks)
    size = 0x20 + len(body)
    header = be32(0x52464E54) + be32(0) + be32(size) + be32(4) + b"\x00" * 0x10
    return header + body


def rarc(payload, payload_name):
    strings = b"ROOT\0root\0" + payload_name.encode("ascii") + b"\0"
    name_root = 0
    name_dir = 5
    name_file = 10
    info = 0x20
    node_off = 0x20
    file_off = 0x30
    str_off = 0x58
    # file_data_offset is an absolute file offset. open() reads that many bytes from offset 0x20.
    data_off = 0xC0
    header = b"".join([
        b"RARC",
        be32(data_off + len(payload)),
        be32(0x20),
        be32(data_off),
        be32(len(payload)),
        be32(0), be32(0), be32(0),
    ])
    info_b = b"".join([
        be32(1),
        be32(node_off),
        be32(2),
        be32(file_off),
        be32(len(strings)),
        be32(str_off),
        be16(1),
        b"\x01\x00\x00\x00\x00\x00",
    ])
    node = b"".join([
        be32(name_root),
        be32(name_dir),
        be16(0),
        be16(2),
        be32(0),
    ])
    def file_ent(fid, name_off, flags, data_off, size):
        return be16(fid) + be16(0) + be32((flags << 24) | name_off) + be32(data_off) + be32(size) + be32(0)
    files = file_ent(0xFFFF, name_dir, 0x02, 0, 0x10) + file_ent(0, name_file, 0x00, 0, len(payload))
    blob = info_b + node + files
    # string table sits at str_off within the info image
    if len(blob) != str_off:
        raise SystemExit("info layout drifted: %d != %d" % (len(blob), str_off))
    blob += strings
    blob += b"\x00" * (data_off - 0x20 - len(blob))
    return header + blob + payload


def fst_entry(is_dir, name_off, a, b):
    return bytes([1 if is_dir else 0]) + struct.pack(">I", name_off)[1:] + be32(a) + be32(b)


def disc(arc, font):
    fst_off = 0x440
    names = b"\0forest_1st.arc\0font.bfn\0"
    entries = b"".join([
        fst_entry(True, 0, 0, 3),
        fst_entry(False, 1, 0x1000, len(arc)),
        fst_entry(False, 1 + len("forest_1st.arc") + 1, 0x1800, len(font)),
    ])
    image = bytearray(0x1800 + len(font))
    image[0:4] = b"GAFE"
    image[4:6] = b"01"
    image[0x1C:0x20] = be32(0xC2339F3D)
    image[0x420:0x424] = be32(0x100)  # empty DOL header
    image[0x424:0x428] = be32(fst_off)
    image[fst_off:fst_off + len(entries)] = entries
    image[fst_off + len(entries):fst_off + len(entries) + len(names)] = names
    image[0x1000:0x1000 + len(arc)] = arc
    image[0x1800:0x1800 + len(font)] = font
    return bytes(image)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "stub.gcm"
    font = bfn1()
    arc = rarc(font, "font.bfn")
    data = disc(arc, font)
    with open(out, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes), arc %d, font %d" % (out, len(data), len(arc), len(font)))


if __name__ == "__main__":
    main()
