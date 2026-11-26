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
- CPU discovery identifies Intel, AMD, Hygon, Centaur/VIA, Zhaoxin, and
  Transmeta CPUID vendors, records family/model/stepping and topology, and
  records APIC ID plus Intel performance/efficiency/other core class for each
  online logical CPU. AMD/Hygon systems with CPUID topology extensions also
  report Extended APIC ID, CoreId, NodeId, nodes-per-processor encoding, and
  threads per core from CPUID 0x8000001E; package logical-processor count is
  derived from the advertised core count and SMT width when both are valid.
  The AMD topology decoder has synthetic AMD/Hygon regression cases so it can
  be checked on CI hosts that are not themselves AMD systems.
  It reports Intel hybrid bootstrap-core type and nominal base/maximum/bus
  frequency values when CPUID exposes them. CPUID.6 thermal and HWP/Turbo
  capabilities are recorded as advertised support only; Nexus does not yet
  read live temperatures, change CPU P-states, or implement vendor-specific
  thermal/fan/power policy.
- PCI graphics inventory distinguishes explicitly catalogued Intel Arc
  Alchemist and Battlemage, Xe-LPG, Xe2, and Xe3 identifiers, and retains
  AMD Radeon and NVIDIA display controllers as vendor-classified devices.
  It also decodes assigned PCI BAR addresses and attributes without probing
  or modifying them. This is discovery/resource inventory only: Intel, AMD,
  and NVIDIA command engines are not initialized, no native 3D driver is
  shipped for those devices, and their 2D/3D acceleration flags remain false.
- VirtIO GPU continues to provide a real 2D resource/scanout/transfer command
  path, but the desktop pixels are still rasterized by the CPU and uploaded
  through TRANSFER_TO_HOST_2D; this is host scanout, not accelerated desktop
  rendering. When a VirtIO GPU advertises VIRGL and returns a supported Virgl
  capset, the driver now negotiates optional VIRGL/CONTEXT_INIT features,
  creates a Virgl context, and exposes a bounded 64 KiB command-stream submit
  path. Every submit requests a fence and waits for the matching completion
  response. The driver also supports a bounded set of 64 host-side 3D
  resources using RESOURCE_CREATE_3D, context attach/detach, and RESOURCE_UNREF,
  with tracked handles and cleanup retries. For a tracked 2D render-target
  resource it can encode and submit a real Virgl surface-create, framebuffer
  bind, color-clear, unbind, and surface-destroy command stream; it increments
  its fenced-clear counter only after the matching completion fence returns.
  The command stream encoder has host-side layout/bounds regression coverage.
  This is a first device-rendering workload, not an accelerated desktop: Nexus
  does not read pixels back to verify the clear result, and a Virgl host may
  execute through either a physical GPU or a software renderer. Consequently
  the kernel still does not label this as verified hardware acceleration.
  The driver also queries the
  VirtIO-GPU display-info table (up to 16 virtual scanouts), prefers an enabled
  output matching the firmware surface size, and selects an enabled fallback
  output when the exact mode is unavailable. The desktop still binds one
  scanout and does not yet provide independent per-monitor framebuffers,
  hotplug mode management, HDR, VRR, or hardware composition. These resources
  are not yet exposed as guest-mapped VRAM and are not used as render targets
  by the desktop. This proves only context/queue/resource protocol operations,
  not successful 3D
  drawing: the kernel still does not compile shaders, execute a draw workload,
  or present GPU-rendered frames. The 2D/3D acceleration flags therefore remain
  false. There is no Vulkan ICD, OpenGL/Mesa Gallium frontend,
  shader compiler, userspace graphics ABI, asynchronous GPU scheduler, or
  general GPU memory manager yet; Venus/Vulkan capsets are not exposed as a
  working Vulkan implementation.
- A framebuffer desktop with Home, Files, Browser, Settings, Terminal, and
  Installer views, plus keyboard and mouse input. The shell supports movable,
  resizable app windows, left/right edge snapping, top-edge maximize, minimize,
  maximize/restore/close controls, and a bottom-corner resize grip. Keyboard
  shortcuts include Alt+Tab / Alt+Shift+Tab to cycle app views, Alt+F4 to close,
  Alt+Left/Right for browser history, Win+Left/Right/Up/Down for window
  placement, Win+E for Files, Win+I for
  Settings, Win+D for Home, and Ctrl+Esc or the Windows key for Start. The Start
  launcher supports typed app search, Up/Down selection, and Enter to launch.
  The shell includes a twelve-category Settings application with a five-tab
  viewport and previous/next navigation: Personalization (dark/light theme,
  eight accent palettes, four wallpaper presets), Display (active framebuffer,
  EDID monitor name and detailed timings, firmware mode inventory, 80–120%
  text sizing, pointer size, and truthful HDR/modeset capability status), Input
  (mouse speed, scroll speed, handedness), Accessibility (high contrast,
  larger text and pointer), System (AC'97 master volume when supported, network
  status, memory and uptime), Devices (detected hardware and driver state),
  Network (Ethernet/Wi-Fi status and scan when a radio driver is registered),
  Storage (block-device inventory), Power (platform power capability status),
  Security (runtime trust and protection status), Applications (native app and
  catalog status), and About (CPU, framebuffer, memory, uptime and renderer).
  Display discovery validates bootloader-provided EDID checksums, reads monitor
  names and detailed timings, derives advertised refresh rates, inventories
  firmware-listed modes, and recognizes CTA HDR static-metadata advertisements.
  This is capability discovery only: no general GPU mode-setting driver or HDR
  output/color pipeline is shipped. Resolution and refresh-rate changes remain
  disabled until a driver registers safe test/apply/restore callbacks.
  Theme, wallpaper, text sizing, mouse controls, accessibility, and volume
  preferences use a versioned, checksum-validated `NEXUS.CFG` record in the
  root of the NXFS Data volume. Settings or Files performs the existing explicit
  storage discovery before loading preferences; no user disk scan was added to
  boot. When a writable NXFS Data volume is unavailable, preferences fall back
  to volatile RAM and do not survive reboot. Resolution changes are not exposed
  because a general display-mode-setting driver is not yet implemented. Audio
  volume reaches supported AC'97 codecs; otherwise the preference is saved for
  a future compatible audio device. The current shell still presents one
  foreground application
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
- PS/2 input and xHCI USB HID boot-keyboard / boot-mouse support, plus generic USB HID gamepad polling for up to four controllers with bounded report parsing and hotplug/disconnect events. Composite controllers, USB hubs, arbitrary HID formats, and native Bluetooth pairing are not yet supported.
- A normalized physical-memory allocator that coalesces firmware memory
  ranges, allocates ordinary, DMA-limited, and contiguous pages, tracks
  ownership/accounting, and rejects invalid or duplicate frees. Allocator
  operations and queries are serialized by an IRQ-safe FIFO ticket lock so
  BSP and AP contexts can use the shared allocator without handing out the
  same page concurrently. A host pthread stress test and a boot-time SMP
  allocate/free self-test have been added. This is still a bootstrap
  bump-pointer allocator with a bounded reclaimed-page list, not yet a buddy
  allocator, bitmap allocator, or per-zone/per-CPU cache design.
- NVMe and AHCI/SATA block-device support. The AHCI driver identifies ATA disks
  and supports guarded installer writes when the device provides flush support.
- GPT, FAT32, NXFS, and a virtual filesystem layer.
- A separate ISO boot-menu installer entry that launches a full-screen install-only session. The installed disk boots directly into the Nexus desktop after restart. The installer builds a GPT layout with an EFI
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
  This uses the native Nexus ELF64 loader for static ET_EXEC or constrained static PIE, has a 1 MiB image limit and a
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
- An early IPv4 stack with DHCP, DNS, TCP, and a bounded HTTP/HTTPS text
  browser. The HTTPS client uses Mbed TLS 4.1.2 with TLS 1.3, certificate-chain
  verification, Nexus-backed cryptographic randomness, RTC time, and a fixed
  1 MiB TLS heap. HTTPS is the default for scheme-less addresses; plain HTTP
  requires an explicit `http://` URL. The browser requires a PEM CA bundle
  named `NEXUS.CRT` in the root of the persistent NXFS Data volume. Missing,
  invalid, or unavailable trust anchors cause HTTPS to fail closed; there is
  no certificate-verification bypass or silent downgrade.
  The network-device layer supports multiple registered adapters and tags
  wired Ethernet versus Wi-Fi media, but only the E1000 Ethernet driver is
  implemented; no Wi-Fi chipset driver or firmware is registered, so live radio
  scanning and association are not available. A bounded 802.11 beacon and
  probe-response parser plus generic Wi-Fi state/driver interface are included
  as a test-backed foundation only. The state manager preserves synchronous
  scan-result and connection-completion callbacks from a driver.
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
- The browser validates bounded HTTP/1.x response framing, including
  Content-Length and chunked bodies, parses bounded Location and Content-Type
  headers, and follows at most five redirects. It supports absolute,
  scheme-relative, root-relative, query-only, and directory-relative redirect
  targets, and refuses HTTPS-to-HTTP downgrade redirects. Responses are
  bounded to 16 KiB and plain text, JSON, XML, and CSS are displayed as text;
  HTML/XHTML pass through a bounded HTML-to-text stage that formats common
  block elements, list items, and entities while omitting comments and
  script/style content. The browser provides keyboard page scrolling and a
  12-entry session history with branch truncation and oldest-entry eviction
  (Alt+Left / Alt+Right). This is not yet a general-purpose modern browser:
  it has no CSS layout, JavaScript engine, DOM, cookies, tabbed browsing,
  image/video/audio rendering, downloads, compression support, or full
  browser compatibility layer.

The dual-mode ISO now carries a deterministic, checksummed `NEXUS/SYSTEM.NXP`
boot pack. It contains a system manifest, quick-start/index text, and the four
first-party SDK examples (`KINETIC.ELF`, `GAMEPAD.ELF`, `AUDIO.ELF`, and
`PIE.ELF`). The installer copies the pack to the EFI System Partition and
adds it as a Limine boot module, so the installed desktop can use the same
built-in apps after reboot. The boot pack uses `NEXUS-`-prefixed documentation filenames so bundled
resources cannot shadow common user-volume names such as the FAT32 test
`README.TXT`.

The kernel validates the pack header, file table,
non-overlapping file ranges, table/payload CRCs, and per-file CRCs before
publishing it. The read-only VFS overlay exposes its files to the Files view;
the existing Run ELF action can launch the included native demos. These are
small Nexus ABI demonstrations, not a general-purpose app catalog or Windows/
Linux compatibility runtime. User changes still belong on the writable Nexus
Data volume, not in the immutable boot pack.

The first-party native application SDK is in `kernel/include/nexus/app.h`,
with freestanding samples and build targets under `sdk/`. Its additive v1
syscall ABI exposes keyboard/mouse event polling, separate gamepad polling,
display information, monotonic timer ticks, interruptible timer sleep,
root-file I/O, bounded XRGB8888 framebuffer blits, and optional AC'97 48 kHz
S16 stereo PCM playback in bounded chunks. Blits are copied through a bounded kernel staging
buffer and presented as partial updates through the existing VirtIO 2D
scanout when available. The foreground app gets an isolated x87/SSE2 state
context for floating-point/vector math; AVX remains disabled until XSAVE
context management exists. This enables simple native 2D apps and game
prototypes to draw without bypassing user-memory checks. Common USB HID gamepads can be polled through xHCI, and a first single-buffer AC'97 playback path is available; neither is a full SDL/audio stack. The renderer remains CPU-side rasterization, not a 3D API or GPU renderer. The current process launcher is foreground and
synchronous, and the ABI does not yet provide shared-library dynamic linking,
multiple concurrent user processes, audio capture/mixing, or a full window manager. The ELF loader accepts ET_EXEC and a constrained static-PIE ET_DYN subset with only symbol-free R_X86_64_RELATIVE relocations; it is not a general-purpose dynamic linker. See
[`docs/development/NATIVE-APP-SDK.md`](docs/development/NATIVE-APP-SDK.md)
for the SDK and compatibility limits.

The software distribution layer now has a preview native `.NUP` package
format, a signed catalog parser, and an NXFS-backed installer that writes
versioned ELF payloads before atomically replacing each app's signed receipt.
Catalog and package signatures use an Ed25519 verifier callback keyed by
pinned key ID; the verifier is mandatory and missing verification fails
closed. **A production Ed25519 provider and release public key are not yet
shipped**, and the host tests use only a non-cryptographic mock verifier. The
Store is not yet connected to this package pipeline. Steam, Discord, Spotify,
GitHub, VS Code, itch.io, Microsoft 365, Gmail, Google Drive, YouTube, Zoom,
Slack, WhatsApp Web, and Notion are listed as official HTTPS website shortcuts,
not native desktop installations. Five bundled Nexus ELF demos can be launched
directly from the Store, but they are immutable examples rather than
user-installable NUP packages. Verified HTTPS binary attachments can now
stream directly to Nexus Data in bounded-memory chunks up to 256 MiB per file,
including Content-Length and HTTP/1.1 chunked responses. New NXFS volumes
choose the largest supported per-file size tier that fits the Data partition;
older 4 MiB and 1 MiB volumes remain compatible at their recorded limits.
Ordinary browser page rendering remains a bounded 4 MiB in-memory path, and
native NUP package payloads remain capped at 4 MiB until package installation
is connected to the streaming downloader. The browser is not yet a full
JavaScript/CSS/DOM engine and cannot run Chrome web applications. See
[`docs/development/SOFTWARE-ECOSYSTEM.md`](docs/development/SOFTWARE-ECOSYSTEM.md)
for the format and trust requirements. The [Steam/game compatibility roadmap](docs/development/STEAM-COMPATIBILITY-ROADMAP.md) lists the Linux ABI, process model, audio, 3D, filesystem, and runtime work that still blocks Steam titles.

The boot architecture now includes a bus-neutral driver registry with
priority-based matching, probe fall-through, and per-device bind records.
AHCI and NVMe initialization are dispatched through registered PCI drivers;
other existing controllers are being migrated incrementally. A dependency-aware
service supervisor now orders PCI discovery, graphics, network devices and
stack, storage, timer, input, audio, desktop, and process-runtime startup. These
callbacks still execute inside the kernel: this is a lifecycle/control layer,
not yet isolated ring-3 service processes or dynamically loadable driver modules.

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
- The browser now has a TLS 1.3 HTTPS transport and bounded redirect
  handling, but it remains a text-only browser. The user must place a trusted
  PEM CA bundle named `NEXUS.CRT` in the root of the persistent NXFS Data
  volume. CSS layout, JavaScript, cookies, tabs, image/media decoding,
  downloads, compression, and a full modern browser engine remain unimplemented.
- The Files app is still bounded: it has a 128-entry display cache,
  current-folder-only filename search, 31-character path/name limits, and a
  4 KiB ASCII editor. Directory rename, recursive search, rich previews,
  timestamps, large-document editing, and broader filesystem capacity remain
  future work.
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


## Native application SDK

Nexus now includes a first-party freestanding x86-64 native application SDK
under `userland/`. Build and inspect the sample with:

```sh
make -C userland inspect
```

The SDK wraps the Nexus `int 0x80` ABI for process exit, console write,
read-only file open/read/close, bounded file writes, and runtime ABI version
discovery. ABI version 1 is reported by syscall 7; applications should check
`nexus_abi_version()` before using version-specific services. The SDK build
also creates a reusable static `libnexus.a` with bounded memory/string and file
I/O helpers, including `nexus_printf()` for bounded `%s`, `%c`, signed/unsigned
integer, hexadecimal, pointer, and percent formatting with short-write retry. The bundled C
application opens `README.TXT`, reads and verifies its contents, closes the
descriptor, writes `HELLO.OUT` through the VFS, reopens it and verifies the
written bytes, then uses `nexus_printf()` to emit a zero-padded numeric field in its success message and exits with status 7. The
BIOS/UEFI QEMU workflow builds the application and inserts the compiled ELF
into its disposable FAT32 image; the kernel then loads it in ring 3 and checks
read/write syscalls, console output, and exit status. User-file writes go
through NXFS when explicitly writable, otherwise through the volatile RAM
overlay; FAT32 is never written.

The runtime is intentionally small, not a general-purpose C library or POSIX
implementation. It has no heap allocator, full libc/POSIX stdio semantics, dynamic linking,
shared libraries, signals, sockets, native GUI SDK, background user-process
scheduling, or Windows PE/COFF compatibility. The test image stores this static ELF in a bounded
two-cluster FAT32 chain.


The native SDK host workflow runs `make -C userland test` against an injected
syscall simulator. It covers partial file reads and console writes, EOF,
capacity overflow, missing files, close failures, string/memory operations,
and VFS file-write/readback. The production application still uses the real
ring-3 `int 0x80` entry path.

The native runtime also includes a bounded `nexus_printf()` formatter. It
supports `%%`, `%c`, `%s`, `%d`/`%i`, `%u`, `%x`/`%X`, and `%p`, with optional
numeric width/zero padding (maximum width 32) and a 511-byte output bound.
Host tests verify formatting, argument conversions, invalid specifiers, and
short-write completion. This is intentionally a small SDK formatter, not a
complete ISO C `printf` implementation.
