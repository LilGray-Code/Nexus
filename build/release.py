#!/usr/bin/env python3
"""Prepare and verify a distributable Nexus BIOS/UEFI ISO.

This is deliberately a release validator, not a source-tree/compile-only check.
It inspects the final ISO filesystem, extracts and compares its payloads, checks
ELF and NXSPACK1 structures, validates the Limine boot menu and boot catalog,
and records exact build hashes and sizes.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISO_NAME = "NexusOS-OriginalKernel-x86_64.iso"

REQUIRED_ISO_FILES = (
    "EFI/BOOT/BOOTX64.EFI",
    "boot/limine/limine.conf",
    "boot/limine/limine-bios.sys",
    "boot/limine/limine-bios-hdd.bin",
    "boot/limine/limine-bios-cd.bin",
    "boot/limine/limine-uefi-cd.bin",
    "boot/nexus-kernel.elf",
    "NEXUS/SYSTEM.NXP",
    "NEXUS/README.TXT",
    "NEXUS/APPS.TXT",
    "NEXUS/BUILD-INFO.TXT",
    "NEXUS/RELEASE-MANIFEST.TXT",
    "NEXUS/RECOVERY.TXT",
)

SOURCE_GROUPS = (
    ("KERNEL CORE AND MEMORY", (
        "kernel/src/main.c", "kernel/src/cpu.c", "kernel/src/memory.c",
        "kernel/src/scheduler.c", "kernel/src/smp.c", "kernel/src/paging.c",
        "kernel/src/interrupts.c", "kernel/src/timer.c",
    )),
    ("DESKTOP GRAPHICS DISPLAY INPUT AND SETTINGS", (
        "kernel/src/desktop.c", "kernel/src/graphics.c",
        "kernel/src/settings.c", "kernel/src/display.c",
        "kernel/src/input.c", "kernel/src/text_buffer.c",
    )),
    ("BROWSER AND NETWORK", (
        "kernel/src/browser_url.c", "kernel/src/browser_search.c",
        "kernel/src/browser_history.c", "kernel/src/html_text.c",
        "kernel/src/http.c", "kernel/src/net_https.c",
        "kernel/src/download.c", "kernel/src/net.c", "kernel/src/netdev.c",
        "kernel/src/e1000.c", "kernel/src/rtl8139.c",
    )),
    ("FIRMWARE AND PLATFORM DISCOVERY", (
        "kernel/src/acpi.c", "kernel/src/firmware.c",
        "kernel/src/pci.c", "kernel/src/pci_caps.c",
        "kernel/src/rtc.c", "kernel/src/fpu.c",
    )),
    ("STORAGE AND FILESYSTEMS", (
        "kernel/src/blockdev.c", "kernel/src/gpt.c",
        "kernel/src/nvme.c", "kernel/src/ahci.c",
        "kernel/src/storage.c", "kernel/src/fat32.c",
        "kernel/src/nxfs.c", "kernel/src/vfs.c",
    )),
    ("USB AUDIO AND GPU DRIVERS", (
        "kernel/src/xhci.c", "kernel/src/usb_hid.c",
        "kernel/src/usb_msc.c", "kernel/src/ac97.c",
        "kernel/src/ac97_format.c", "kernel/src/gpu.c",
        "kernel/src/virtio_pci.c", "kernel/src/virtio_gpu.c",
        "kernel/src/virtio_net.c", "kernel/src/virgl_protocol.c",
    )),
    ("INSTALLER AND RECOVERY", (
        "kernel/src/installer_runtime.c", "kernel/src/installer_storage.c",
        "installer/installer_journal.c", "installer/partition_plan.c",
        "installer/gpt_image.c", "installer/gpt_install.c",
        "installer/fat32_format.c", "installer/fat32_file.c",
        "installer/bios_install.c",
    )),
)

REQUIRED_PACK_ENTRIES = {
    "SYSTEM.MAN", "NEXUS-README.TXT", "NEXUS-APPS.TXT",
    "KINETIC.ELF", "GAMEPAD.ELF", "AUDIO.ELF", "PIE.ELF", "ARCADE.ELF",
}

REQUIRED_KERNEL_SYMBOLS = {
    "desktop_show",
    "desktop_handle_input",
    "desktop_set_installer_mode",
    "nexus_settings_defaults",
    "nexus_browser_search_parse",
    "nexus_browser_resolve_redirect",
    "nexus_installer_runtime_apply",
    "nexus_installer_runtime_recover",
    "nexus_firmware_init",
    "acpi_init",
    "nxfs_mount",
}


def run(args: list[str], *, capture: bool = False) -> str:
    try:
        completed = subprocess.run(
            args, cwd=ROOT, check=True, text=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
        )
    except subprocess.CalledProcessError as exc:
        output = exc.stdout if isinstance(exc.stdout, str) else ""
        raise RuntimeError(
            f"command failed ({exc.returncode}): {' '.join(args)}\n{output}"
        ) from exc
    return completed.stdout or ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_value(*args: str, fallback: str = "unknown") -> str:
    try:
        value = run(["git", *args], capture=True).strip()
        return value or fallback
    except Exception:
        return fallback


def source_manifest(kernel: Path, pack: Path) -> str:
    rows = [
        "NEXUS RELEASE CONTENT MANIFEST",
        "FORMAT=1",
        "ARCHITECTURE=x86_64",
        "KERNEL=boot/nexus-kernel.elf",
        "BOOT_MODE=Dedicated installer; installed target starts desktop",
        "FIRMWARE=Limine BIOS and UEFI boot paths; ACPI and SMBIOS discovery",
        "EXTERNAL_FIRMWARE_BLOBS=No proprietary firmware blobs are bundled",
        "RENDERING=CPU desktop renderer with firmware framebuffer and supported VirtIO scanout paths",
        "",
    ]
    for group, sources in SOURCE_GROUPS:
        rows.append(f"[{group}]")
        for relative in sources:
            path = ROOT / relative
            obj = ROOT / "build" / (Path(relative).stem + ".o")
            if not path.is_file():
                raise RuntimeError(f"release component source is missing: {relative}")
            if not obj.is_file() or obj.stat().st_size == 0:
                raise RuntimeError(f"release component was not built: {obj.relative_to(ROOT)}")
            rows.append(
                f"{relative} | source_sha256={sha256(path)} | "
                f"object_bytes={obj.stat().st_size}"
            )
        rows.append("")
    rows.extend([
        "[KERNEL IMAGE]",
        f"bytes={kernel.stat().st_size}",
        f"sha256={sha256(kernel)}",
        "",
        "[READ-ONLY SYSTEM APPLICATION PACK]",
        f"bytes={pack.stat().st_size}",
        f"sha256={sha256(pack)}",
        "entries=" + ",".join(sorted(parse_pack(pack).keys())),
        "",
        "[INTEGRATED APPLICATIONS]",
        "Settings, desktop shell, Files, terminal, and text-oriented HTTPS browser are kernel-integrated.",
        "The System pack contains static Nexus-native ELF example applications.",
        "",
        "[INSTALLER AND RECOVERY]",
        "The ISO boots the dedicated installer. The installed target boots the desktop.",
        "Installer writes use explicit target identity confirmation and typed ERASE confirmation.",
        "The installer journal and Recovery partition support transaction inspection and recovery.",
        "",
        "[SCOPE NOTES]",
        "Drivers and kernel services listed above are compiled into the freestanding kernel.",
        "This manifest is not a claim of universal hardware compatibility or physical-hardware certification.",
    ])
    return "\n".join(rows) + "\n"


def parse_pack(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    if len(data) < 32 or data[:8] != b"NXSPACK1":
        raise RuntimeError("system pack has invalid NXSPACK1 magic or is truncated")
    version, header_size, total_size, count, table_crc, payload_crc = struct.unpack_from(
        "<IIIIII", data, 8
    )
    if version != 1 or total_size != len(data):
        raise RuntimeError("system pack version or total-size field is invalid")
    if count < 1 or count > 32 or header_size != 32 + count * 48:
        raise RuntimeError("system pack file table size/count is invalid")
    table = data[32:header_size]
    payload = data[header_size:]
    if zlib.crc32(table) & 0xFFFFFFFF != table_crc:
        raise RuntimeError("system pack file-table checksum mismatch")
    if zlib.crc32(payload) & 0xFFFFFFFF != payload_crc:
        raise RuntimeError("system pack payload checksum mismatch")

    entries: dict[str, bytes] = {}
    for index in range(count):
        record = table[index * 48:(index + 1) * 48]
        raw_name = record[:32].split(b"\0", 1)[0]
        try:
            name = raw_name.decode("ascii")
        except UnicodeDecodeError as exc:
            raise RuntimeError("system pack contains a non-ASCII entry name") from exc
        offset, size, expected_crc, flags = struct.unpack_from("<IIII", record, 32)
        if not name or name in entries or flags != 0:
            raise RuntimeError(f"system pack entry is invalid or duplicated: {name!r}")
        if offset < header_size or size == 0 or offset + size > len(data):
            raise RuntimeError(f"system pack entry has invalid bounds: {name}")
        contents = data[offset:offset + size]
        if zlib.crc32(contents) & 0xFFFFFFFF != expected_crc:
            raise RuntimeError(f"system pack entry checksum mismatch: {name}")
        entries[name] = contents
    for name in ("KINETIC.ELF", "GAMEPAD.ELF", "AUDIO.ELF", "PIE.ELF", "ARCADE.ELF"):
        elf = entries.get(name)
        if elf is None or len(elf) < 64 or elf[:6] != b"\x7fELF\x02\x01":
            raise RuntimeError(f"system pack is missing a valid ELF64 application: {name}")
        if struct.unpack_from("<H", elf, 18)[0] != 62:
            raise RuntimeError(f"system pack application is not x86-64: {name}")
    missing = REQUIRED_PACK_ENTRIES - entries.keys()
    if missing:
        raise RuntimeError("system pack is missing entries: " + ", ".join(sorted(missing)))
    manifest = entries["SYSTEM.MAN"].decode("ascii", errors="strict")
    for name in sorted(REQUIRED_PACK_ENTRIES - {"SYSTEM.MAN"}):
        if f"FILE={name}\n" not in manifest:
            raise RuntimeError(f"SYSTEM.MAN does not name {name}")
    return entries


def prepare(args: argparse.Namespace) -> None:
    iso_root = args.iso_root.resolve()
    kernel = args.kernel.resolve()
    pack = args.system_pack.resolve()
    if not kernel.is_file() or not pack.is_file():
        raise RuntimeError("kernel ELF or Nexus system pack is missing")
    parse_pack(pack)
    nexus_dir = iso_root / "NEXUS"
    nexus_dir.mkdir(parents=True, exist_ok=True)
    for source, destination in (
        (ROOT / "system/README.TXT", nexus_dir / "README.TXT"),
        (ROOT / "system/APPS.TXT", nexus_dir / "APPS.TXT"),
        (pack, nexus_dir / "SYSTEM.NXP"),
    ):
        if not source.is_file():
            raise RuntimeError(f"required release input is missing: {source}")
        shutil.copyfile(source, destination)

    (nexus_dir / "RELEASE-MANIFEST.TXT").write_text(
        source_manifest(kernel, pack), encoding="utf-8"
    )
    recovery = """NEXUS INSTALLER AND RECOVERY NOTES

This ISO starts the dedicated Nexus installer. It does not boot the installed
desktop. The installed drive receives its own desktop boot configuration.

The installer uses a transaction journal and a dedicated Recovery partition.
If an installation is interrupted, boot this installer medium again, identify
the same target disk by its full model/capacity/stable identity, and use the
installer recovery path when it offers to inspect or resume the recorded
transaction. Never choose a disk based only on its transient device name.

The installer erases and repartitions the selected target. Back up important
data first. Do not attempt recovery against a different disk or bypass the
typed ERASE and stable-identity checks.
"""
    (nexus_dir / "RECOVERY.TXT").write_text(recovery, encoding="utf-8")

    commit = git_value("rev-parse", "HEAD")
    describe = git_value("describe", "--tags", "--always")
    branch = os.environ.get("GITHUB_REF_NAME") or git_value(
        "branch", "--show-current"
    )
    build_time = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    build_info = [
        "NEXUS ISO BUILD INFORMATION",
        "FORMAT=1",
        f"BUILD_UTC={build_time}",
        f"GIT_COMMIT={commit}",
        f"GIT_DESCRIBE={describe}",
        f"GIT_REF={branch}",
        "TARGET=x86_64 BIOS+UEFI install media",
        f"KERNEL_BYTES={kernel.stat().st_size}",
        f"KERNEL_SHA256={sha256(kernel)}",
        f"SYSTEM_PACK_BYTES={pack.stat().st_size}",
        f"SYSTEM_PACK_SHA256={sha256(pack)}",
    ]
    (nexus_dir / "BUILD-INFO.TXT").write_text(
        "\n".join(build_info) + "\n", encoding="utf-8"
    )
    print(f"Prepared release payload in {nexus_dir}")
    print(f"Release component groups: {len(SOURCE_GROUPS)}")


def verify(args: argparse.Namespace) -> None:
    iso = args.iso.resolve()
    kernel = args.kernel.resolve()
    pack = args.system_pack.resolve()
    config = (ROOT / "boot/limine.conf").read_text(encoding="utf-8")
    if not iso.is_file() or iso.stat().st_size == 0:
        raise RuntimeError(f"ISO is missing or empty: {iso}")
    raw = iso.read_bytes()[:17 * 2048]
    if len(raw) < 17 * 2048 or len(raw) % 2048 != 0:
        raise RuntimeError("ISO size is not a whole number of 2048-byte sectors")
    pvd = raw[16 * 2048:17 * 2048]
    if pvd[:7] != b"\x01CD001\x01":
        raise RuntimeError("ISO9660 primary volume descriptor is missing or invalid")

    if "default_entry: 1" not in config or "remember_last_entry: no" not in config:
        raise RuntimeError("Limine installer default/remember-last-entry policy is invalid")
    required_config = (
        "/Install Nexus OS",
        "path: boot():/boot/nexus-kernel.elf",
        "cmdline: nexus.mode=installer",
        "module_path: boot():/NEXUS/SYSTEM.NXP",
    )
    for line in required_config:
        if line not in config:
            raise RuntimeError(f"Limine installer configuration is missing: {line}")
    if "nexus.mode=desktop" in config:
        raise RuntimeError("installer ISO unexpectedly selects the installed desktop entry")

    boot_report = run(
        ["xorriso", "-indev", str(iso), "-report_el_torito", "plain"],
        capture=True,
    )
    for platform in ("BIOS", "UEFI"):
        if not re.search(
            rf"(?im)^.*El Torito boot img\s*:\s*\d+\s+{platform}\s+y(?:\s|$)",
            boot_report,
        ):
            raise RuntimeError(f"ISO El Torito catalog has no bootable {platform} entry\n{boot_report}")
    run(["readelf", "-h", str(kernel)])
    elf_header = run(["readelf", "-h", str(kernel)], capture=True)
    if "ELF64" not in elf_header or "Advanced Micro Devices X86-64" not in elf_header:
        raise RuntimeError("kernel image is not a 64-bit x86-64 ELF executable")
    symbol_table = run(["nm", "-g", "--defined-only", str(kernel)], capture=True)
    symbols = {line.split()[-1] for line in symbol_table.splitlines() if line.split()}
    missing_symbols = REQUIRED_KERNEL_SYMBOLS - symbols
    if missing_symbols:
        raise RuntimeError("kernel is missing linked release components: " +
                           ", ".join(sorted(missing_symbols)))

    expected_entries = parse_pack(pack)
    with tempfile.TemporaryDirectory(prefix="nexus-iso-verify-") as temp:
        extracted_root = Path(temp)
        extracted: dict[str, Path] = {}
        for relative in REQUIRED_ISO_FILES:
            target = extracted_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            run([
                "xorriso", "-osirrox", "on", "-indev", str(iso),
                "-extract", "/" + relative, str(target),
            ])
            if not target.is_file() or target.stat().st_size == 0:
                raise RuntimeError(f"ISO is missing required file: /{relative}")
            extracted[relative] = target
        iso_kernel = extracted["boot/nexus-kernel.elf"]
        iso_pack = extracted["NEXUS/SYSTEM.NXP"]
        if iso_kernel.read_bytes() != kernel.read_bytes():
            raise RuntimeError("kernel payload in ISO differs from the freshly-built kernel")
        if iso_pack.read_bytes() != pack.read_bytes():
            raise RuntimeError("system pack payload in ISO differs from the freshly-built pack")
        iso_config = extracted["boot/limine/limine.conf"].read_text(encoding="utf-8")
        if iso_config != config:
            raise RuntimeError("Limine configuration inside ISO differs from the validated source")
        for app_name, app_data in expected_entries.items():
            if app_name.endswith(".ELF") and not app_data.startswith(b"\x7fELF"):
                raise RuntimeError(f"invalid app in built system pack: {app_name}")
        for relative in (
            "NEXUS/README.TXT", "NEXUS/APPS.TXT",
            "NEXUS/BUILD-INFO.TXT", "NEXUS/RELEASE-MANIFEST.TXT",
            "NEXUS/RECOVERY.TXT",
        ):
            if extracted[relative].stat().st_size == 0:
                raise RuntimeError(f"ISO metadata file is empty: {relative}")
    print(f"ISO9660 filesystem: valid primary volume descriptor, {iso.stat().st_size} bytes")
    print("El Torito: bootable BIOS and UEFI entries")
    print(f"Required ISO files: {len(REQUIRED_ISO_FILES)} present and non-empty")
    print(f"Kernel: x86-64 ELF64; {len(REQUIRED_KERNEL_SYMBOLS)} linked component symbols verified")
    print(f"System pack: {len(expected_entries)} entries, CRCs and native ELF apps verified")
    print("Payload identity: ISO kernel and system pack match the just-built files")
    print("Installer Limine configuration: verified")


def report(args: argparse.Namespace) -> None:
    iso = args.iso.resolve()
    kernel = args.kernel.resolve()
    pack = args.system_pack.resolve()
    if not iso.is_file():
        raise RuntimeError(f"cannot report missing ISO: {iso}")
    release_dir = iso.parent
    shutil.copyfile(
        ROOT / "build/iso-root/NEXUS/RELEASE-MANIFEST.TXT",
        release_dir / "RELEASE-MANIFEST.TXT",
    )
    commit = git_value("rev-parse", "HEAD")
    describe = git_value("describe", "--tags", "--always")
    branch = os.environ.get("GITHUB_REF_NAME") or git_value(
        "branch", "--show-current"
    )
    build_info = [
        "NEXUS DISTRIBUTABLE ISO",
        "FORMAT=1",
        f"BUILD_UTC={datetime.now(timezone.utc).replace(microsecond=0).isoformat()}",
        f"GIT_COMMIT={commit}",
        f"GIT_DESCRIBE={describe}",
        f"GIT_REF={branch}",
        "ARCHITECTURE=x86_64",
        "FIRMWARE=Legacy BIOS and UEFI (OVMF/SeaBIOS QEMU checks are separate CI gates)",
        f"ISO_FILE={iso.name}",
        f"ISO_BYTES={iso.stat().st_size}",
        f"ISO_MIB={iso.stat().st_size / (1024 * 1024):.3f}",
        f"ISO_SHA256={sha256(iso)}",
        f"KERNEL_BYTES={kernel.stat().st_size}",
        f"KERNEL_SHA256={sha256(kernel)}",
        f"SYSTEM_PACK_BYTES={pack.stat().st_size}",
        f"SYSTEM_PACK_SHA256={sha256(pack)}",
        "CONTENT=kernel, Limine BIOS/UEFI boot payloads, installer, recovery notes, integrated desktop/settings/browser, read-only native application pack",
        "VALIDATION=release ISO structural and payload verification; QEMU UEFI/BIOS boot gates run in GitHub Actions",
        "PHYSICAL_HARDWARE=not certified by CI; separate bare-metal validation required",
    ]
    (release_dir / "BUILD-INFO.txt").write_text(
        "\n".join(build_info) + "\n", encoding="utf-8"
    )
    print("\n".join(build_info))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p_prepare = commands.add_parser("prepare")
    p_prepare.add_argument("--iso-root", type=Path, required=True)
    p_prepare.add_argument("--kernel", type=Path, required=True)
    p_prepare.add_argument("--system-pack", type=Path, required=True)
    p_prepare.set_defaults(func=prepare)

    p_verify = commands.add_parser("verify")
    p_verify.add_argument("--iso", type=Path, required=True)
    p_verify.add_argument("--kernel", type=Path, required=True)
    p_verify.add_argument("--system-pack", type=Path, required=True)
    p_verify.set_defaults(func=verify)

    p_report = commands.add_parser("report")
    p_report.add_argument("--iso", type=Path, required=True)
    p_report.add_argument("--kernel", type=Path, required=True)
    p_report.add_argument("--system-pack", type=Path, required=True)
    p_report.set_defaults(func=report)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
