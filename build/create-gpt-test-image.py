#!/usr/bin/env python3
"""Create a disposable 64 MiB GPT disk image for Nexus CI only."""
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
PARTITION_LAST = 4095

def make_header(current, backup, entries_lba, disk_guid, entries_crc):
    fields = struct.pack(
        "<8sIIIIQQQQ16sQIII",
        b"EFI PART", 0x00010000, 92, 0, 0,
        current, backup, FIRST_USABLE, LAST_USABLE,
        disk_guid.bytes_le, entries_lba, ENTRY_COUNT, ENTRY_SIZE, entries_crc
    )
    crc = zlib.crc32(fields) & 0xFFFFFFFF
    return fields[:16] + struct.pack("<I", crc) + fields[20:] + bytes(SECTOR - 92)

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: create-gpt-test-image.py IMAGE")
    path = Path(sys.argv[1])
    image = bytearray(TOTAL_SECTORS * SECTOR)

    # Keep the known LBA0 test signature in the protective MBR boot-code area.
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
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk_guid = uuid.UUID("5A0D8E11-4E58-49C8-9B2C-9C9B2B1A0001")

    primary_entries_lba = 2
    backup_entries_lba = TOTAL_SECTORS - 1 - ENTRY_SECTORS
    image[primary_entries_lba * SECTOR:
          primary_entries_lba * SECTOR + ENTRY_BYTES] = entries
    image[backup_entries_lba * SECTOR:
          backup_entries_lba * SECTOR + ENTRY_BYTES] = entries

    primary = make_header(1, TOTAL_SECTORS - 1, primary_entries_lba,
                          disk_guid, entries_crc)
    backup = make_header(TOTAL_SECTORS - 1, 1, backup_entries_lba,
                         disk_guid, entries_crc)
    image[SECTOR:2 * SECTOR] = primary
    image[(TOTAL_SECTORS - 1) * SECTOR:] = backup
    path.write_bytes(image)

if __name__ == "__main__":
    main()
