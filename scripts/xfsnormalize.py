#!/usr/bin/env python3
import os
import struct
import sys

CHUNK_INODES = 64


def make_crc32c():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0x82F63B78 if c & 1 else c >> 1
        table.append(c)
    return table


CRC32C = make_crc32c()


def crc32c(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc = CRC32C[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


def normalize(img, epoch):
    def inode_crc(chunk, base):
        buf = bytearray(chunk[base:base + inodesize])
        buf[100:104] = b"\0\0\0\0"
        return crc32c(buf)

    def is_inode(chunk, base):
        if chunk[base:base + 2] != b"\x49\x4e" or chunk[base + 4] not in (2, 3):
            return False
        if chunk[base + 4] == 2:
            return chunk[base + 160:base + 176] == uuid
        return struct.pack("<I", inode_crc(chunk, base)) == chunk[base + 100:base + 104]

    with open(img, "r+b") as f:
        data = bytearray(f.read())
        if data[0:4] != b"XFSB":
            sys.exit(f"{img}: not an XFS filesystem")
        blocksize = struct.unpack(">I", data[4:8])[0]
        agblocks = struct.unpack(">I", data[84:88])[0]
        agcount = struct.unpack(">I", data[88:92])[0]
        inodesize = struct.unpack(">H", data[104:106])[0]
        uuid = bytes(data[32:48])
        agsize = agblocks * blocksize
        chunk = CHUNK_INODES * inodesize
        stamp = struct.pack(">II", epoch, 0)
        n = 0
        for agno in range(agcount):
            agoff = agno * agsize
            for cbase in range(agoff, min(agoff + agsize, len(data)), chunk):
                slots = min(CHUNK_INODES, (len(data) - cbase) // inodesize)
                for s in range(slots):
                    off = cbase + s * inodesize
                    if not is_inode(data, off):
                        continue
                    if data[off + 4] == 2 and data[off + 32:off + 56] == stamp * 3 and data[off + 144:off + 152] == stamp:
                        continue
                    data[off + 32:off + 40] = stamp
                    data[off + 40:off + 48] = stamp
                    data[off + 48:off + 56] = stamp
                    data[off + 92:off + 96] = b"\0\0\0\0"
                    data[off + 144:off + 152] = stamp
                    if data[off + 4] == 3:
                        struct.pack_into("<I", data, off + 100, inode_crc(data, off))
                    n += 1
        f.seek(0)
        f.write(data)
        f.truncate()
    return n


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: xfsnormalize.py IMAGE SOURCE_DATE_EPOCH")
    n = normalize(sys.argv[1], int(sys.argv[2]))
    print(f"xfsnormalize: {n} inodes updated in {os.path.basename(sys.argv[1])}")
