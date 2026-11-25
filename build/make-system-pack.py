#!/usr/bin/env python3
"""Build the deterministic, checksummed Nexus NXSPACK1 boot-time system pack."""
from __future__ import annotations

import argparse
import re
import struct
import zlib
from pathlib import Path

MAGIC = b"NXSPACK1"
VERSION = 1
HEADER_SIZE = 32
RECORD_SIZE = 48
MAX_PACK_BYTES = 2 * 1024 * 1024
MAX_FILES = 32
NAME_RE = re.compile(r"^[A-Z0-9_.-]{1,31}$")


def parse_file(spec: str) -> tuple[str, Path]:
    if "=" not in spec:
        raise argparse.ArgumentTypeError("file must be NAME=PATH")
    name, raw_path = spec.split("=", 1)
    name = name.upper()
    path = Path(raw_path)
    if not NAME_RE.fullmatch(name) or name in {".", ".."}:
        raise argparse.ArgumentTypeError(f"invalid system-pack name: {name!r}")
    if name == "SYSTEM.MAN":
        raise argparse.ArgumentTypeError("SYSTEM.MAN is generated automatically")
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"input file does not exist: {path}")
    return name, path


def build_pack(files: list[tuple[str, Path]]) -> bytes:
    ordered = sorted(files, key=lambda item: item[0])
    names = [name for name, _ in ordered]
    if len(names) != len(set(names)):
        raise ValueError("duplicate system-pack filename")
    if len(ordered) + 1 > MAX_FILES:
        raise ValueError(f"system pack exceeds {MAX_FILES} files")

    manifest_lines = [
        "NEXUS SYSTEM PACK 1",
        "ARCH=x86_64",
        "ABI=1",
        f"FILE_COUNT={len(ordered) + 1}",
    ]
    manifest_lines.extend(f"FILE={name}" for name in names)
    manifest = ("\n".join(manifest_lines) + "\n").encode("ascii")
    content = [("SYSTEM.MAN", manifest)]
    for name, path in ordered:
        data = path.read_bytes()
        if not data:
            raise ValueError(f"empty system-pack input: {path}")
        if name.endswith(".ELF"):
            if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
                raise ValueError(f"{path} is not a little-endian ELF64 image")
            if struct.unpack_from("<H", data, 18)[0] != 62:
                raise ValueError(f"{path} is not an x86-64 ELF image")
        content.append((name, data))

    header_size = HEADER_SIZE + len(content) * RECORD_SIZE
    payload = bytearray()
    records = bytearray()
    for name, data in content:
        name_bytes = name.encode("ascii")
        if not NAME_RE.fullmatch(name):
            raise ValueError(f"invalid generated filename: {name}")
        padded_name = name_bytes + bytes(32 - len(name_bytes))
        offset = header_size + len(payload)
        crc = zlib.crc32(data) & 0xFFFFFFFF
        records.extend(padded_name)
        records.extend(struct.pack("<IIII", offset, len(data), crc, 0))
        payload.extend(data)

    total_size = header_size + len(payload)
    if total_size > MAX_PACK_BYTES:
        raise ValueError(
            f"system pack is {total_size} bytes; maximum is {MAX_PACK_BYTES}"
        )
    table_crc = zlib.crc32(records) & 0xFFFFFFFF
    payload_crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = MAGIC + struct.pack(
        "<IIIIII",
        VERSION,
        header_size,
        total_size,
        len(content),
        table_crc,
        payload_crc,
    )
    assert len(header) == HEADER_SIZE
    return bytes(header + records + payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--file", action="append", default=[], type=parse_file)
    args = parser.parse_args()
    if not args.file:
        parser.error("at least one --file is required")
    pack = build_pack(args.file)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(pack)
    print(f"Nexus system pack: {args.output} ({len(pack)} bytes)")


if __name__ == "__main__":
    main()
