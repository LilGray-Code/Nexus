#!/usr/bin/env python3
"""Create a disposable 64 MiB GPT + FAT32 disk image for Nexus CI only."""
import struct
import sys
import uuid
import zlib
from pathlib import Path

SECTOR = 512
TOTAL_SECTORS = 64 * 1024 * 1024 // SECTOR
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ENTRY_BYTES = ENTRY_COUNT * ENTRY_SIZE
ENTRY_SECTORS = ENTRY_BYTES // SECTOR
FIRST_USABLE = 2 + ENTRY_SECTORS
LAST_USABLE = TOTAL_SECTORS - ENTRY_SECTORS - 2
PARTITION_FIRST = 2048
PARTITION_LAST = 102399
NXFS_FIRST = 102400
NXFS_LAST = LAST_USABLE
NXFS_TYPE_GUID = uuid.UUID("4E585346-0001-4E58-9A11-4E5855534653")
README_PREFIX = b"Nexus OS read-only FAT32 filesystem test.\r\n"
README = README_PREFIX + b" " * (SECTOR - len(README_PREFIX)) + b"FAT32-CROSS-CLUSTER-READ-OK"

def make_header(current, backup, entries_lba, disk_guid, entries_crc):
    fields = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART", 0x00010000, 92, 0, 0,
        current, backup, FIRST_USABLE, LAST_USABLE,
        disk_guid.bytes_le, entries_lba, ENTRY_COUNT, ENTRY_SIZE, entries_crc
    )
    crc = zlib.crc32(fields) & 0xFFFFFFFF
    return fields[:16] + struct.pack("<I", crc) + fields[20:] + bytes(SECTOR - 92)


def make_hello_elf():
    """Build a tiny static x86-64 ELF64 program for the disposable CI volume."""
    message = b"NEXUS FILE EXEC OK"
    code = bytearray([
        0x48, 0xC7, 0xC0, 0x02, 0x00, 0x00, 0x00,  # write syscall
        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,  # fd 1
        0x48, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC2, len(message), 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,  # exit syscall
        0x48, 0xC7, 0xC7, 0x07, 0x00, 0x00, 0x00,  # status 7
        0xCD, 0x80,
        0xEB, 0xFE,
    ])
    message_address = 0x00400000 + len(code)
    struct.pack_into("<Q", code, 16, message_address)
    segment = bytes(code) + message
    segment_offset = 0x100
    file_size = segment_offset + len(segment)

    ident = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        ident, 2, 62, 1, 0x00400000, 64, 0, 0,
        64, 56, 1, 0, 0, 0
    )
    program_header = struct.pack(
        "<IIQQQQQQ",
        1, 5, segment_offset, 0x00400000, 0,
        len(segment), 4096, 0x100
    )
    image = bytearray(file_size)
    image[:64] = header
    image[64:120] = program_header
    image[segment_offset:] = segment
    return bytes(image)

def make_fat32(image):
    volume_sectors = PARTITION_LAST - PARTITION_FIRST + 1
    reserved = 32
    fats = 2
    sectors_per_fat = 1
    while True:
        clusters = volume_sectors - reserved - fats * sectors_per_fat
        needed = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if needed == sectors_per_fat:
            break
        sectors_per_fat = needed

    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"NEXUSOS "
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     SECTOR, 1, reserved, fats, 0, 0, 0xF8, 0, 63, 255,
                     PARTITION_FIRST, volume_sectors)
    struct.pack_into("<IHHIHH", boot, 36,
                     sectors_per_fat, 0, 0, 2, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x4E455855)
    boot[71:82] = b"NEXUS TEST "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"

    start = PARTITION_FIRST * SECTOR
    image[start:start + SECTOR] = boot

    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, max(0, clusters - 2))
    struct.pack_into("<I", fsinfo, 492, 4)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    image[start + SECTOR:start + 2 * SECTOR] = fsinfo

    fat_start = PARTITION_FIRST + reserved
    fat = bytearray(sectors_per_fat * SECTOR)
    struct.pack_into("<IIII", fat, 0,
                     0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF, 5)
    struct.pack_into("<I", fat, 4 * 4, 0x0FFFFFFF)
    struct.pack_into("<I", fat, 5 * 4, 0x0FFFFFFF)
    for copy in range(fats):
        offset = (fat_start + copy * sectors_per_fat) * SECTOR
        image[offset:offset + len(fat)] = fat

    data_start = PARTITION_FIRST + reserved + fats * sectors_per_fat
    root = bytearray(SECTOR)
    root_entry = bytearray(32)
    root_entry[0:11] = b"README  TXT"
    root_entry[11] = 0x20
    struct.pack_into("<H", root_entry, 20, 0)
    struct.pack_into("<H", root_entry, 26, 3)
    struct.pack_into("<I", root_entry, 28, len(README))
    root[0:32] = root_entry

    hello_elf = make_hello_elf()
    if len(hello_elf) > SECTOR:
        raise ValueError("HELLO.ELF test image must fit in one FAT32 cluster")
    hello_entry = bytearray(32)
    hello_entry[0:11] = b"HELLO   ELF"
    hello_entry[11] = 0x20
    struct.pack_into("<H", hello_entry, 20, 0)
    struct.pack_into("<H", hello_entry, 26, 4)
    struct.pack_into("<I", hello_entry, 28, len(hello_elf))
    root[32:64] = hello_entry
    root[64] = 0

    offset = data_start * SECTOR
    image[offset:offset + SECTOR] = root
    hello_sector = (data_start + 2) * SECTOR
    image[hello_sector:hello_sector + len(hello_elf)] = hello_elf

    file_sector = (data_start + 1) * SECTOR
    image[file_sector:file_sector + SECTOR] = README[:SECTOR]
    fragmented_file_sector = (data_start + 3) * SECTOR
    image[fragmented_file_sector:
          fragmented_file_sector + len(README) - SECTOR] = README[SECTOR:]

def make_nxfs(image):
    """Format a fresh dedicated NexusFS test partition."""
    block_size = SECTOR
    partition_blocks = NXFS_LAST - NXFS_FIRST + 1
    blocks_per_slot = (16 * 1024 + block_size - 1) // block_size
    data_start = 19
    if partition_blocks < data_start + 16 * 2 * blocks_per_slot:
        raise ValueError("NXFS test partition is too small")

    def make_superblock():
        block = bytearray(block_size)
        block[0:8] = b"NEXUSFS1"
        struct.pack_into("<IIIIQIII", block, 8,
                         1, block_size, 16, 16 * 1024,
                         partition_blocks, 3, data_start, blocks_per_slot)
        struct.pack_into("<I", block, 60, zlib.crc32(block[:60]) & 0xFFFFFFFF)
        return block

    start = NXFS_FIRST * SECTOR
    superblock = make_superblock()
    image[start:start + SECTOR] = superblock
    image[start + SECTOR:start + 2 * SECTOR] = superblock
    # Journal, entry table, and file banks remain zeroed on a fresh volume.


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: create-gpt-test-image.py IMAGE")
    path = Path(sys.argv[1])
    image = bytearray(TOTAL_SECTORS * SECTOR)

    # LBA 100 remains unused and unallocated; CI saves, writes, flushes,
    # verifies and restores it. The FAT32 partition starts at LBA 2048.
    image[:len(b"NEXUS-READ-ONLY-TEST")] = b"NEXUS-READ-ONLY-TEST"
    mbr_entry = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0xEE,
                            b"\xff\xff\xff", 1, TOTAL_SECTORS - 1)
    image[446:462] = mbr_entry
    image[510:512] = b"\x55\xaa"

    entries = bytearray(ENTRY_BYTES)
    type_guid = uuid.UUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7")
    unique_guid = uuid.UUID("7C1D9E5A-8E8A-4D13-9D9E-2B4D3E5A1001")
    name = "NEXUS TEST".encode("utf-16le")
    entry = bytearray(ENTRY_SIZE)
    entry[0:16] = type_guid.bytes_le
    entry[16:32] = unique_guid.bytes_le
    struct.pack_into("<QQQ", entry, 32, PARTITION_FIRST, PARTITION_LAST, 0)
    entry[56:56 + len(name)] = name
    entries[:ENTRY_SIZE] = entry

    nxfs_entry = bytearray(ENTRY_SIZE)
    nxfs_unique_guid = uuid.UUID("7C1D9E5A-8E8A-4D13-9D9E-2B4D3E5A1002")
    nxfs_name = "NEXUS DATA".encode("utf-16le")
    nxfs_entry[0:16] = NXFS_TYPE_GUID.bytes_le
    nxfs_entry[16:32] = nxfs_unique_guid.bytes_le
    struct.pack_into("<QQQ", nxfs_entry, 32, NXFS_FIRST, NXFS_LAST, 0)
    nxfs_entry[56:56 + len(nxfs_name)] = nxfs_name
    entries[ENTRY_SIZE:2 * ENTRY_SIZE] = nxfs_entry
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk_guid = uuid.UUID("5A0D8E11-4E58-49C8-9B2C-9C9B2B1A0001")

    primary_entries_lba = 2
    backup_entries_lba = TOTAL_SECTORS - 1 - ENTRY_SECTORS
    image[primary_entries_lba * SECTOR:
          primary_entries_lba * SECTOR + ENTRY_BYTES] = entries
    image[backup_entries_lba * SECTOR:
          backup_entries_lba * SECTOR + ENTRY_BYTES] = entries
    image[SECTOR:2 * SECTOR] = make_header(
        1, TOTAL_SECTORS - 1, primary_entries_lba, disk_guid, entries_crc)
    image[(TOTAL_SECTORS - 1) * SECTOR:] = make_header(
        TOTAL_SECTORS - 1, 1, backup_entries_lba, disk_guid, entries_crc)

    make_fat32(image)
    make_nxfs(image)
    path.write_bytes(image)

if __name__ == "__main__":
    main()
