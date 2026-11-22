# Nexus OS

Nexus is an independent x86-64 operating-system project. Its intended runtime
is Nexus-owned: kernel, userland, system services, desktop, applications,
filesystem, and graphical installer. It is not intended to be a Linux, Debian,
or FreeBSD distribution.

## Current validated build

The original-kernel implementation is developed on the
[nexus-original-kernel-roadmap branch](https://github.com/LilGray-Code/Nexus/tree/nexus-original-kernel-roadmap).
It uses a freestanding x86-64 kernel and Limine for UEFI and legacy BIOS boot.
The Debian-based prototype is historical and is not part of this runtime.

The latest validated development ISO was produced by GitHub Actions run
[44971424953](https://github.com/LilGray-Code/Nexus/actions/runs/44971424953)
from commit `df95a903a591128feb19ed87ca3ceeb4ee86912d`. CI passed host-side
installer/storage tests, both kernel build configurations, the graphical
installer on a disposable NVMe disk, installed-disk UEFI and legacy BIOS boots,
persistent file creation/editing across reboot, and a boot check of the final
ISO. The artifact is named `NexusOS-OriginalKernel-BIOS-UEFI`.

This is an **installable, QEMU-validated prototype**, not a finished general-
purpose desktop OS or a physical-hardware-certified release. The installer
currently writes to NVMe targets only. The desktop is an early framebuffer
shell with built-in Files, Browser, Settings, Terminal, and Installer views.
Files supports small text-file creation/editing on the Nexus data partition.
It is not yet a full window manager or a suite of independent desktop apps.

## What remains before a complete desktop OS

- A complete installed userland/root filesystem, Nexus init, service manager,
  process lifecycle, IPC, permissions, and a supported application ABI.
- A real window manager/compositor and independently running desktop apps,
  including a capable file manager, terminal, settings, notifications, and
  power/session controls.
- SATA/AHCI and USB mass-storage drivers and tested installation targets.
- USB HID, modern touchpad, broad graphics/GPU, audio, Wi-Fi, Bluetooth, and
  ACPI power-management support.
- Secure networking for applications: maintained TLS, certificate handling,
  secure time, and a hardened browser/application runtime.
- System updates, rollback, recovery workflows, filesystem checking, and
  end-to-end crash/power-loss testing.
- Physical-hardware validation across multiple Intel/AMD systems, firmware
  implementations, and storage/controller vendors.
- A release process with reproducible builds, security review, signed update
  metadata, and documented installation/recovery instructions.

## Installer and safety

Nexus has its own graphical installer running in the Nexus live environment.
It does not invoke another distribution's installer or package manager. The
installer previews a target layout, requires the exact stable disk ID and a
separate typed `ERASE` confirmation, and uses guarded kernel block-device APIs.
Normal persistent filesystem writes are restricted to the validated Nexus data
partition on the positively identified boot disk; EFI, system, recovery, and
other sectors remain protected by that authorization path.

See [installer/README.md](installer/README.md) for implementation details and
remaining installer limitations, and
[docs/architecture/ORIGINAL-KERNEL-ROADMAP.md](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
for the broader kernel/userland/desktop roadmap.

A successful QEMU run proves only the tested virtual hardware configuration.
Do not treat this prototype as safe for installation on an important disk
without a verified backup; physical hardware installation and recovery have
not yet been certified.
