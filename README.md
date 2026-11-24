# Nexus OS — Original Kernel

Nexus is an independent x86-64 operating-system project. The active **Original
Kernel** edition boots a Nexus-written freestanding kernel and its integrated
framebuffer desktop; it is not a Linux, Debian, or FreeBSD distribution.

> **Status: QEMU-validated installable engineering prototype.** The dual-mode
> BIOS/UEFI ISO and graphical installer have been exercised in virtual
> machines. Nexus has not yet been certified on physical PCs and is not a
> production-ready general-purpose desktop OS.

## What is implemented

- A freestanding x86-64 kernel, booted by Limine in UEFI or legacy BIOS mode.
- A framebuffer desktop with Home, Files, Browser, Settings, Terminal, and
  Installer views, plus keyboard and mouse input. The shell supports movable,
  resizable app windows, left/right edge snapping, top-edge maximize, minimize,
  maximize/restore/close controls, and a bottom-corner resize grip. Keyboard
  shortcuts include Alt+Tab / Alt+Shift+Tab to cycle app views, Alt+F4 to close,
  Win+Left/Right/Up/Down for window placement, Win+E for Files, Win+I for
  Settings, Win+D for Home, and Ctrl+Esc or the Windows key for Start. The Start
  launcher supports typed app search, Up/Down selection, and Enter to launch.
  The shell also supports live Dark/Light appearance, four accent palettes, and
  mouse-selectable Settings controls. Theme/accent preferences use a versioned,
  checksum-validated `NEXUS.CFG` in the root of the NXFS Data volume. Settings
  or Files performs the existing explicit storage discovery before loading
  preferences; no user disk scan was added to boot. When a writable NXFS Data
  volume is unavailable, preferences fall back to volatile RAM and do not
  survive reboot. The current shell still presents one foreground application
  view at a time. Taskbar switching and minimizing remember each app view's
  last window size, position, snap, and maximize state; closing discards that
  saved placement. This makes window navigation more consistent but is not
  yet a multi-window compositor with independent overlapping app instances.
- The framebuffer renderer supports clipped rectangles, alpha blending,
  rounded panels, and bounded row-rasterized circles used for subtle wallpaper
  lighting. This is CPU software rendering on a linear framebuffer, not GPU
  acceleration or a DirectX/OpenGL implementation.
- Root-scoped VFS configuration I/O reads/writes simple files at the NXFS Data
  volume root without changing the current directory. It prefers persistent
  NXFS writes, never writes FAT32, and uses the bounded RAM overlay only when
  persistent NXFS is unavailable or read-only. Host integration tests exercise
  root preferences while nested in NXFS/FAT32 directories and verify remount
  retention.
- PS/2 input and xHCI USB HID boot-keyboard / boot-mouse support.
- NVMe and AHCI/SATA block-device support. The AHCI driver identifies ATA disks
  and supports guarded installer writes when the device provides flush support.
- GPT, FAT32, NXFS, and a virtual filesystem layer.
- A Nexus graphical installer. The installer builds a GPT layout with an EFI
  System Partition, Nexus System volume, Recovery journal partition, and
  separate Nexus Data volume; BIOS installs also include a BIOS Boot
  partition.
- A Files view with bounded hierarchical directory navigation over NXFS,
  keyboard/mouse selection, create/open/edit/delete, and a 128-entry display
  cache (six rows visible at once). Entries are directory-first and can be
  filtered by case-insensitive filename substring using `Ctrl+F`; matching is
  limited to the current folder and the bounded 128-entry cache, not recursive
  full-volume search. Results retain their original VFS entry identity while
  sorting by name, file type, or size with the `S` key. Regular files support
  persistent rename (`R`), copy (`C`), and move (`M`). The selected file can
  also be launched with the Files toolbar's Run ELF action or `Ctrl+Enter`.
  This uses the native Nexus ELF64 loader, has a 128 KiB image limit and a
  15-character process-name limit, executes synchronously, and displays PID,
  exit status, and a bounded capture of the program's `write(1, ...)` output
  in Files while continuing to mirror output to serial. It is not a Windows
  `.exe` launcher. Copies stream through
  a bounded block buffer and verify the source CRC before journaled metadata
  makes the new file visible. Destination paths containing a slash are
  volume-root relative; a simple name uses the current directory. Names/path
  components are limited to 31 characters and the text editor is limited to
  4 KiB of ASCII. The desktop mounts the dedicated NXFS Data partition after
  the Recovery partition; it does not expose the NXFS System partition as the
  user's file store. Timestamps are not stored, so date sorting is unavailable.
  FAT32 volumes are read-only; RAM-only fallback saves are volatile.
- An early IPv4 stack with DHCP, DNS, TCP, and a plain HTTP text viewer.
  The network-device layer supports multiple registered adapters and tags
  wired Ethernet versus Wi-Fi media, but only the E1000 Ethernet driver is
  implemented; no Wi-Fi chipset driver or firmware is registered, so live radio
  scanning and association are not available. A bounded 802.11 beacon and
  probe-response parser plus generic Wi-Fi state/driver interface are included
  as a test-backed foundation only.
  WPA2-Personal PMK/PTK derivation, EAPOL-Key MIC verification, AES-128-CCM,
  RFC 3394 key unwrap, EAPOL-Key/GTK parsing, and a bounded supplicant M1-M4
  handshake engine have known-answer or integration tests. The engine
  requires a caller-supplied cryptographically random SNonce and callbacks
  for EAPOL transmission and hardware key installation. The CCMP layer builds
  802.11 AAD/nonces, protects data frames, and enforces per-TID replay windows.
  A bounded 802.11 beacon/probe-response parser and generic Wi-Fi driver/state
  interface can describe discovered networks and security advertisements,
  but no radio driver is registered and no live scans or associations occur.
  Retransmission timers and hardware key programming remain platform work.
  SHA-256, HMAC-SHA256, and HKDF-SHA256 primitives have known-answer tests as
  TLS building blocks; they do not constitute TLS.
- The HTTP viewer validates bounded HTTP/1.x response framing (including
  content-length and chunked bodies) and provides keyboard page scrolling. Its
  bounded HTML-to-text stage formats common block elements, list items, and
  entities while omitting comments and script/style content. It does not yet
  provide CSS layout, JavaScript, HTTPS/TLS, cookies, or a modern browser engine.

The desktop applications and current system services are integrated into the
kernel. The ELF64 loader has a bounded ring-3 execution test, but this does
**not** mean that Nexus currently has a full separate userland, a mature
init/service manager, or ordinary preemptive multitasking. Nexus does not yet
load Windows PE/COFF executables, implement Win32 APIs, or provide a Windows
application compatibility layer. Ordinary Windows `.exe` applications and
drivers therefore cannot be assumed to run. Broad software compatibility
requires a stable per-process ABI, userland libraries/services, GUI and graphics
APIs, drivers, and then a substantial, separately tested compatibility runtime.

## Installer safety

The installer requires the user to choose a target disk, confirm its stable
device identity, and separately type `ERASE` before destructive operations.
It revalidates the target and keeps a durable recovery journal. The Recovery
partition is separate from the System and Data volumes. An interrupted install
can be retried against the same target; the transaction tests verify resume
after simulated restarts without rewriting an already-committed GPT.

**Installing erases the selected disk.** Use a disposable disk or virtual
machine while evaluating Nexus. Back up important data and check the displayed
model, capacity, and stable identity before confirming.

## Validation evidence

The repository's GitHub Actions workflows build both the write-locked kernel
and the installer-enabled kernel and exercise:

- live UEFI boot with OVMF and live legacy-BIOS boot with SeaBIOS;
- installed UEFI and BIOS boot;
- graphical installation to disposable NVMe and AHCI/SATA targets;
- persistent NXFS user-file creation and fresh-boot retention on NVMe and
  AHCI/SATA targets;
- a Files-editor keyboard edit followed by save, reboot, and verification of
  the edited file on the NVMe Data volume;
- host-side installer fault injection across simulated power cycles, journal
  recovery, and NXFS unmount/remount/edit/remount persistence;
- dual-mode ISO construction and a boot check of the distributable ISO.

These are virtual-machine and host-test results. They do not demonstrate
physical power-loss behavior, compatibility with every controller or
firmware, or successful installation on arbitrary PCs. Check the
[GitHub Actions workflows](https://github.com/LilGray-Code/Nexus/actions) for
the latest run attached to the current branch.

## Important limitations

- No physical PC installation or hardware-compatibility matrix has been
  completed. Hardware testing remains required.
- Secure Boot is not supported.
- The browser is an early plain-HTTP text viewer; HTTPS/TLS and modern web
  rendering are not implemented.
- The Files app is still bounded: it has a 128-entry display cache,
  31-character path/name limits, and a 4 KiB ASCII editor. Directory rename,
  search, rich previews, timestamps, large-document editing, and broader
  filesystem capacity are not yet implemented.
- Desktop applications and much of the system runtime remain kernel-integrated.
  A separate userland, a full application/process model, robust preemptive
  multitasking, updates, and production recovery/upgrade tooling remain work
  in progress. There is no PE/COFF loader, Win32 subsystem, DirectX runtime, or
  general Windows application support yet; the existing ELF test is a Nexus
  native-process proof, not Windows compatibility.
- Broad GPU acceleration, audio, Wi-Fi, Bluetooth, power management, and
  support for arbitrary third-party hardware are not claimed.

## Build and test

See the [installer documentation](installer/README.md), the
[original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md),
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions).

A successful build or QEMU test is not a substitute for testing on the actual
PC and storage hardware where Nexus will be used.
