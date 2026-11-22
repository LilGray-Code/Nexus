# Nexus OS

Nexus is an independent x86-64 desktop operating system. Its target runtime is
fully Nexus-owned: kernel, userland, system services, desktop, applications,
filesystem, and graphical installer. It is not a Linux, Debian, or FreeBSD
distribution.

## Active implementation

The original-kernel implementation is developed on the
[nexus-original-kernel-roadmap branch](https://github.com/LilGray-Code/Nexus/tree/nexus-original-kernel-roadmap).
It uses a freestanding x86-64 kernel and Limine for UEFI and legacy BIOS boot.
The Debian-based prototype is historical only and is not part of the target
runtime or installer.

## Current prototype

The active branch contains a freestanding x86-64 kernel, Limine BIOS/UEFI boot
media, a framebuffer desktop shell, PS/2 keyboard/mouse input, kernel terminal,
cooperative scheduler foundation, ring-3 ELF execution, read-only GPT/FAT32
storage and VFS, volatile RAM filesystem, Intel E1000 networking, DHCP, DNS,
and an early single-connection TCP/plain-HTTP client.

The desktop includes an **Install F5** graphical wizard with disk
selection, UEFI/dual-firmware planning, exact stable-ID confirmation, and a
separate typed `ERASE` confirmation. The kernel runtime creates GPT, formats
the EFI, NXFS system, and NXFS data partitions, deploys the system and boot
payloads, installs the optional BIOS boot stage, and journals each completed
phase. Interrupted installs resume from the last verified journal phase.

The CI release ISO is built with `NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES`
after the host tests, disposable-disk integration test, keyboard-driven QEMU
installer test, installed-disk UEFI/BIOS boot tests, and final ISO boot check
pass. The supported target write backend is currently NVMe only; AHCI/SATA and
USB mass-storage installation are not implemented.

This is an **installable prototype**, not a finished general-purpose desktop
OS. The installed system payload is still a minimal kernel, manifest, and
README rather than a complete userland. The data partition is formatted, but
normal persistent user-file writes and the file-management UI still need
completion. Physical NVMe hardware validation, broader GPU/audio/Wi-Fi/USB
drivers, power management, updates, and production recovery hardening remain
outstanding.

## First-party graphical installer

Nexus has its own graphical installer, running directly in the Nexus live
environment. It does not launch or depend on another distribution's installer
or package manager. The wizard invokes the guarded installer runtime; the
implementation, safety invariants, test gates, and remaining limitations are
documented in [installer/README.md](installer/README.md).

The GPT transaction and kernel storage adapters have host-side tests
against disposable in-memory block devices. Boot-medium provenance is resolved
from Limine's executable-file handoff by matching validated GPT partition GUID,
GPT disk GUID, or MBR disk ID; ambiguous or unknown identity fails closed.
The installer runtime, storage adapters, recovery path, both kernel
build variants, and QEMU install/boot matrix passed GitHub Actions run
[44969951849](https://github.com/LilGray-Code/Nexus/actions/runs/44969951849).
The resulting ISO is an installable NVMe-targeted prototype, not a claim of
physical-hardware certification or a complete general-purpose userland.

## Build and test

See the [original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions) for BIOS/UEFI
build and QEMU test status. A buildable ISO is not a finished OS: release
requires verified boots, actual installer and first-boot tests, recovery tests,
and a multi-vendor x86-64 hardware matrix.
