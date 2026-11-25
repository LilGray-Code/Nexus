# Nexus Installer

**Status: QEMU-validated installable engineering prototype.** The Nexus-owned
graphical installer runs in the live Nexus desktop. The dual-mode BIOS/UEFI
build has passed the repository's disposable-disk QEMU install and boot
workflows. Physical-PC installation and recovery remain unverified.

## Product boundary

This is a first-party installer for the Nexus freestanding x86-64 kernel. It
does not launch Debian Installer, Calamares, Subiquity, Anaconda, Linux
utilities, or another distribution's package manager.

The installed runtime currently consists of the Nexus kernel and its integrated
desktop/system features, plus a separate NXFS user Data volume. The System
partition receives `KERNEL.ELF`, `SYSTEM.MAN`, and `README.TXT`. The
desktop, terminal, file browser, settings, and early HTTPS-capable text
browser are currently kernel-integrated; this is not yet a separate full
userland or a production desktop application environment.

## Installer-only ISO and installed startup

The distributable ISO is an installer medium, not a live desktop image. Its
Limine menu has a single **Install Nexus OS** entry and starts the dedicated,
full-screen installer session by default. The ISO does not offer or display
the regular Nexus desktop, taskbar, workspace, or desktop shortcuts while
installation is in progress. Last-entry memory is disabled so firmware cannot
silently select a previously used desktop entry.

The installer requires a target disk selection, exact stable-device-ID
confirmation, and a separate typed `ERASE` confirmation before any
destructive write. It excludes the installer boot medium and rejects
ambiguous or mounted targets. After a successful install, the target disk
receives its own Limine configuration with a single **Nexus OS - Installed
Desktop** entry and the `nexus.mode=desktop` boot argument.

When installation completes, shut down or restart, remove the installer USB,
and boot from the newly installed drive. That drive starts the normal Nexus
desktop; the installer environment remains on the separate USB ISO and is not
part of the installed system's boot menu.

## Current installer flow

The installer-only ISO starts directly in the graphical installer. It discovers
eligible block devices and provides a target selection and layout preview. A
destructive install is blocked until the user confirms the selected device's
stable identity and separately types `ERASE`. The kernel revalidates the
device immediately before each destructive stage. The live boot medium,
mounted devices, and devices with ambiguous identity are rejected.

**The selected target disk is erased and repartitioned.** Only proceed after
checking the exact model, capacity, and stable device identity, and after
backing up all important data.

## Disk layout

The installer creates a GPT partition table. UEFI installs include:

1. **EFI System Partition** — FAT32, containing the Limine UEFI loader,
   configuration, and required BIOS boot support files.
2. **Nexus System** — NXFS, containing the installed kernel and system
   manifest/readme.
3. **Nexus Recovery** — a separate reserved partition for redundant,
   checksummed installer transaction-journal records.
4. **Nexus Data** — NXFS, used by the installed Files view for persistent
   user files.

Legacy-BIOS and dual-firmware layouts also contain a BIOS Boot partition for
the Limine BIOS stage. The same installed kernel and Data volume are used by
both firmware boot paths. Secure Boot is not supported.

## Storage and deployment

The installer uses the kernel's block-device interfaces. The current virtual
test targets include NVMe and AHCI/SATA. The AHCI path identifies ATA devices
and registers guarded writes when flush support is available. These storage
paths are exercised using disposable QEMU disks; this does not certify every
physical NVMe/SATA controller or firmware implementation.

The installer formats the System and Data NXFS volumes separately, copies the
kernel and boot payloads, and records each completed transaction phase in the
Recovery partition. The ordinary kernel build remains write-locked. Destructive
write support is enabled only for the installer-enabled release build after
the test workflow builds the candidate.

The installed Files view mounts the NXFS Data partition following the
Recovery partition. It does not present the NXFS System volume as the user's
writable file store. FAT32 volumes remain read-only. If no writable Data volume
is mounted, RAM-only file saves are volatile and do not survive a reboot.

## Keyboard, mouse, and display compatibility

The input stack supports legacy PS/2 keyboards/mice and directly attached
wired USB HID boot-protocol keyboards and mice through xHCI. Some 2.4 GHz
wireless keyboard/mouse devices also work through their USB receiver when it
presents a standard boot-HID keyboard or mouse interface. Connect the USB
device or receiver before booting the installer. Receiver-specific features,
USB hubs, non-boot HID reports, and native Bluetooth discovery and pairing are
not implemented, so Bluetooth devices and many proprietary wireless receivers
are not guaranteed to work.

The ISO and installed Limine configuration request a 1920x1080, 32-bit
framebuffer. Limine automatically selects another available mode when that
resolution is unavailable. The software renderer includes fractional
coverage for smoother text and 4x sub-pixel coverage at rounded panel/window
corners, reducing stair-step edges. Rendering is still CPU software
composited into the framebuffer. No Intel, AMD, or NVIDIA GPU driver or
hardware-accelerated composition backend is present yet; OpenGL, Vulkan, and
DirectX are not supported.

## Recovery and test coverage

Host-side tests use disposable mock block devices and inject failures at
filesystem formatting, system-payload deployment, and boot-payload deployment.
They simulate multiple restarts while preserving the disk's GPT and Recovery
journal, then verify that a retry resumes without rewriting the committed GPT.
NXFS tests unmount and remount the Data volume, verify a file, replace it with
edited contents, remount again, and verify the new generation.

The QEMU workflow additionally exercises:

- UEFI and legacy-BIOS live boot;
- installed UEFI and BIOS boot from disposable target disks;
- graphical installation to disposable NVMe and AHCI/SATA disks;
- creation and persistence of user files across a fresh boot on both target
  types;
- a GUI text-editor insertion and save on NVMe, followed by reboot and
  verification that the larger edited file is still present;
- construction and boot-check of the install-enabled dual-mode ISO.

The simulated power cycles and QEMU reboots are not physical power-cut tests.
No physical-PC installation, controller matrix, or real-world recovery
certification has been completed.

## Current user-file manager

The installed Files view currently exposes a flat root directory with up to
64 indexed entries and eight visible entries at a time. Keyboard arrow keys
and mouse selection navigate the view. Users can create, open, edit, save, and
delete supported files. The built-in editor is limited to 4 KiB of ASCII text,
and current filenames use an 8.3-style format. This is a bounded basic file
manager, not a complete hierarchical desktop file manager.

## Known limits

- No physical hardware compatibility matrix or physical recovery tests.
- No Secure Boot support.
- No full separate userland or mature init/service manager; desktop apps and
  core services remain integrated into the kernel.
- No production-grade updates, rollback workflow, or filesystem repair
  environment beyond the implemented installer transaction recovery path.
- No claim of arbitrary GPU, audio, Wi-Fi, Bluetooth, or motherboard support.
- The browser is a bounded text-oriented client, not a full HTML5/CSS/JavaScript
  engine. Its online path uses verified TLS 1.3, a built-in CA trust bundle,
  complete HTTP response framing, redirects, and server-rendered Bing search
  results with a DuckDuckGo HTML fallback. The QEMU live-desktop acceptance
  test now performs a real HTTPS search and verifies that result links are
  parsed. Provider anti-bot challenges and modern page features can still
  prevent some sites from working.
- DNS currently supports IPv4 A queries, verified question matching, bounded
  CNAME-chain resolution, and a small positive cache with a maximum five-minute
  TTL. This is not a complete recursive resolver or full IPv6 DNS implementation.

Treat the ISO as an engineering prototype. Prefer QEMU or another disposable
test machine until physical testing has been completed.
