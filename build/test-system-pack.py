#!/usr/bin/env python3
"""Host-side format and determinism tests for make-system-pack.py."""
from __future__ import annotations

import importlib.util
import struct
import tempfile
import zlib
from pathlib import Path

SCRIPT = Path(__file__).with_name("make-system-pack.py")
spec = importlib.util.spec_from_file_location("make_system_pack", SCRIPT)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    (root / "README.TXT").write_bytes(b"nexus test readme\n")
    (root / "APP.ELF").write_bytes(
        b"\x7fELF\x02\x01" + bytes(10) + struct.pack("<H", 2) +
        struct.pack("<H", 62) + bytes(46)
    )
    inputs = [
        ("README.TXT", root / "README.TXT"),
        ("APP.ELF", root / "APP.ELF"),
    ]
    first = module.build_pack(inputs)
    second = module.build_pack(list(reversed(inputs)))
    assert first == second
    assert first[:8] == b"NXSPACK1"
    version, header_size, total_size, count, table_crc, payload_crc = \
        struct.unpack_from("<IIIIII", first, 8)
    assert version == 1 and count == 3
    assert header_size == 32 + count * 48
    assert total_size == len(first)
    table = first[32:header_size]
    payload = first[header_size:]
    assert zlib.crc32(table) & 0xFFFFFFFF == table_crc
    assert zlib.crc32(payload) & 0xFFFFFFFF == payload_crc
    seen = set()
    for i in range(count):
        record = table[i * 48:(i + 1) * 48]
        name = record[:32].split(b"\0", 1)[0].decode("ascii")
        offset, size, crc, flags = struct.unpack_from("<IIII", record, 32)
        assert name not in seen
        seen.add(name)
        assert flags == 0 and offset >= header_size
        assert offset + size <= len(first)
        assert zlib.crc32(first[offset:offset + size]) & 0xFFFFFFFF == crc
    assert seen == {"SYSTEM.MAN", "README.TXT", "APP.ELF"}

print("NEXUS SYSTEM PACK BUILDER TESTS PASSED")
