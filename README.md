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
- A framebuffer desktop with Home, Files, Browser, Settings, Terminal,
  System Monitor, Control Center, and a native Calculator, plus keyboard
  and mouse input. The modernized shell uses a centered icon dock, a compact
  two-column Start launcher, shared dark app chrome, Aurora-style accents,
  and a Control Center with live memory, uptime, storage and adapter status
  plus working theme, wallpaper, contrast, motion and taskbar quick settings.
- The Calculator evaluates bounded integer expressions with precedence,
  parentheses, unary signs, division and modulo; it reports syntax,
  divide-by-zero, nesting and overflow errors. Its keypad accepts mouse input
  as well as keyboard entry.
- A native multi-window desktop shell with a 12-window manager, generation-
  safe handles, z-order, focus, hit-testing, window movement/resizing,
  minimize/maximize/restore, title-bar controls, taskbar app switching, and
  overlapping built-in app windows. Each built-in app currently has one live
  window instance; this is not yet a general third-party application runtime.
- Four real virtual desktops. The top-bar desktop tabs switch workspace
  visibility and focus without closing windows. Task View is available from
  the top-bar button or `Ctrl+Alt+Tab`; use arrow keys and Enter to select a
  desktop, or click a desktop card. `Ctrl+Alt+PageUp/PageDown` switches to
  the previous/next desktop. `Ctrl+Shift+PageUp/PageDown` moves the focused
  window to the neighboring desktop. Alt+Tab cycles windows on the current
  desktop, and Shift+Alt+Tab cycles in reverse.
- Window snapping includes left/right halves, four corner layouts, drag-to-
  edge snapping, double-click title-bar maximize, and F11 maximize/restore.
  Use `Ctrl+Alt+Left/Right` for halves, `Ctrl+Alt+Up/Down` for maximize/
  restore, and `Ctrl+Alt+1..4` for corner layouts.
- The installed desktop uses a dark Aurora palette, centered icon dock,
  rounded app surfaces, a searchable Start launcher, a Control Center with
  live memory/uptime/storage/adapter status, quick personalization settings,
  five color themes, and five wallpaper styles. It displays an animated
  Nexus startup splash and does not expose the live installer as an app or
  launcher item.
- PS/2 input, xHCI USB HID boot-keyboard / boot-mouse support, and
  report-protocol USB gamepad discovery with bounded HID report parsing and
  interrupt-IN input decoding for supported controller layouts. Unsupported
  or malformed interface/endpoint layouts are rejected; broad controller
  compatibility is not claimed.
- NVMe and AHCI/SATA block-device support. The AHCI driver identifies ATA disks
  and supports guarded installer writes when the device provides flush support.
- GPT, FAT32, NXFS, and a virtual filesystem layer.
- Separate live-installer and installed-OS kernel images. The ISO boots
  `nexus-installer.elf` and carries `nexus-os.elf` as a separate deployment
  payload. The installed image is compiled with installer mode disabled and
  without the destructive installer-write build flag. Booting the USB opens
  the full-screen installer; booting the installed disk opens the normal
  desktop. The installer builds a compact GPT layout with an EFI System
  Partition, Nexus System volume, and separate Nexus Data volume. Its small
  alternating install journal lives in reserved EFI sectors, so no separate
  Recovery partition is created. Legacy BIOS installs add a BIOS Boot
  partition. After successful
  deployment, the live installer offers **Restart into Nexus OS**. Remove
  the installation USB, then press Enter or click Restart. The installer
  resets the computer and firmware loads the separately built installed-system
  kernel into the normal desktop instead of continuing the live installation
  session.
- A Files view with a bounded listing (up to 128 indexed entries, six
  visible at once), keyboard/mouse selection, folder-first alphabetical
  sorting, visible directory labels, nested read-only FAT32 navigation,
  create/open/edit/delete/copy/rename for writable root-level files, and a
  scrollable 1 MiB ASCII text editor. Press F5 to refresh, Delete to request
  deletion (confirm with Y or cancel with N/Esc), C to copy, and R or F2 to
  rename the selected file;
  the move transaction copies, reads back, compares, and only then removes
  the source. Existing destination names are rejected, and FAT32 remains
  read-only. User-created NXFS files
  are persistent while RAMFS fallback files are volatile. The desktop mounts
  the dedicated NXFS Data partition after the System partition; it does not
  expose the NXFS System partition as the user's file store.
- A Terminal with live system overview (`info`), storage/network/process
  inspection, current-directory reporting (`pwd`), nested FAT32 navigation
  (`cd DIR`, `cd ..`), directory listings with types and file sizes (`ls`),
  file reads (`cat FILE`), safe file copying (`cp SOURCE DEST`), and
  verified file moves/renames (`mv SOURCE DEST`) into Nexus root storage.
  Copy and move operations are bounded to 1 MiB and refuse existing
  destinations; moves read back and compare the copied bytes before deleting
  the source. They use transactional NXFS writes when that volume is writable;
  otherwise only files fitting the 4 KiB RAMFS limit can be copied or moved,
  and those copies are volatile.
- The Browser accepts plain search queries as queries (rather than mistaking
  them for hostnames), builds an encoded DuckDuckGo Lite search URL, and
  provides a dedicated search landing page. Its HTTP response parser validates
  status lines, bounded headers, content types and redirect locations. Live
  search and modern websites still require a verified TLS implementation;
  search requests are not silently downgraded to plaintext. Direct plain-HTTP
  pages can be fetched and shown as bounded text.
- The Browser's HTML text extraction retains block separation for headings
  and paragraphs, line breaks, and list bullets, while suppressing comments,
  script/style contents, and decoding common HTML entities. This remains a
  text-only page viewer, not a modern browser engine.
- An early Intel E1000 wired-Ethernet stack with DHCP, DNS, TCP, and a plain
  HTTP text viewer. This is limited network access, not a general-purpose
  internet/browser implementation.

The desktop applications and current system services are integrated into the
kernel. The presence of an ELF loader and ring-3 execution tests does **not**
mean that Nexus currently has a full separate userland, a mature init/service
manager, or ordinary preemptive multitasking.

## Installer safety

The installer asks the user to choose a target disk and review its model,
capacity, stable ID and proposed layout. A clear **Confirm & Install** button
(or Enter) authorizes the destructive operation; no confirmation phrase needs
to be typed. Nexus captures the selected identity automatically and
revalidates it before destructive writes. A two-copy, checksummed transaction journal lives
in reserved EFI sectors. There is no separate Recovery partition or required
recovery setup. If power is interrupted, a later install attempt automatically
resumes from a valid journal. If an old journal is missing or damaged, the user
can start a clean install with the same explicit erase confirmation.

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
- host-side installer fault injection across simulated power cycles, automatic
  journal resume, and NXFS unmount/remount/edit/remount persistence;
- dual-mode ISO construction and a boot check of the distributable ISO;
- separate installer/OS ELF validation and installed UEFI/BIOS boot checks
  that verify the installed runtime reports installer mode disabled.

These are virtual-machine and host-test results. They do not demonstrate
physical power-loss behavior, compatibility with every controller or
firmware, or successful installation on arbitrary PCs. Check the
[GitHub Actions workflows](https://github.com/LilGray-Code/Nexus/actions) for
the latest run attached to the current branch.

## Important limitations

- No physical PC installation or hardware-compatibility matrix has been
  completed. Hardware testing remains required.
- Secure Boot is not supported.
- The built-in Browser now has a native search landing page, query-to-search
  URL handling, bounded HTTP response parsing and redirect detection. Search
  providers require HTTPS, so live public search results remain unavailable
  until TLS and certificate validation are implemented. Its current direct
  page viewer still supports only plain HTTP text. Modern web rendering,
  Wi-Fi, and broad
  network-adapter support are not implemented. Google Chrome cannot run on
  the current freestanding kernel: Nexus still lacks the Linux/POSIX runtime,
  dynamic userspace, graphics and system interfaces Chrome depends on.
- The E1000/DHCP stack is exercised with emulated networking in QEMU; actual
  public-internet access on physical hardware has not yet been validated.
- Game downloads and third-party launchers are not supported yet. Nexus has no
  secure HTTPS/TLS transport, streaming download manager, or general-purpose
  application runtime. Steam and Epic Games launchers cannot currently be
  installed or run; see the [Nexus Gaming roadmap](docs/NEXUS_GAMING.md).
- File management remains bounded: Nexus's writable NXFS namespace is
  root-level, FAT32 subdirectories are read-only, and writable folder
  creation plus volume-to-volume management remain incomplete. The Files
  app supports root-level create/open/edit/delete/copy/rename operations;
  nested FAT32 folders can be browsed but not modified. The Terminal does
  not yet provide a full shell or a general file-management command set.
- Desktop applications and much of the system runtime remain kernel-integrated.
  A separate userland, a full application/process model, robust preemptive
  multitasking, updates, and production recovery/upgrade tooling remain work
  in progress.
- Broad GPU acceleration, audio, Wi-Fi, Bluetooth, power management, and
  support for arbitrary third-party hardware are not claimed.

## Build and test

See the [installer documentation](installer/README.md), the
[original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md),
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions).

A successful build or QEMU test is not a substitute for testing on the actual
PC and storage hardware where Nexus will be used.
