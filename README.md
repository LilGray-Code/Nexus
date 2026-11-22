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

The desktop now includes an **Install F5** graphical wizard with target
selection, firmware-aware partition preview, exact disk-ID confirmation, and a
separate typed `ERASE` confirmation. A kernel-side runtime sequences GPT
creation, EFI/NXFS formatting, system and boot payload deployment, optional
BIOS boot-stage installation, and recovery-journal updates. Destructive calls
remain compile-time locked in the normal kernel build until the new runtime
tests, full kernel build, and QEMU install/reboot/recovery checks pass. The
current target write backend is NVMe; AHCI/SATA and USB mass-storage writes are
not implemented.

This is still an early prototype, not a complete desktop OS. Durable installable
filesystem hardening, a full installer wizard, system-image deployment,
target-disk BIOS stage-1 installation, first boot, recovery/update flows, modern
browser/TLS, broad USB/Wi-Fi/audio/GPU drivers, power management, and release
hardening remain incomplete.

## First-party graphical installer

Nexus will have its own graphical installer, running directly in the Nexus live
environment. It will not launch or depend on another distribution's installer
or package manager. The current UI is only a read-only preview; the actual
installer engine specification, GPT planner, metadata serializer, guarded
transaction, safety invariants, and implementation gates are in
[installer/README.md](installer/README.md).

The GPT transaction and kernel storage adapters have host-side tests
against disposable in-memory block devices. Boot-medium provenance is resolved
from Limine's executable-file handoff by matching validated GPT partition GUID,
GPT disk GUID, or MBR disk ID; ambiguous or unknown identity fails closed.
The new installer runtime has a separate host test suite and build workflow,
but those new checks must be observed passing before the compile-time write
lock can be removed. The installed NXFS payload is still a minimal kernel,
manifest, and README rather than a complete general-purpose userland.

## Build and test

See the [original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions) for BIOS/UEFI
build and QEMU test status. A buildable ISO is not a finished OS: release
requires verified boots, actual installer and first-boot tests, recovery tests,
and a multi-vendor x86-64 hardware matrix.
