# Nexus driver compatibility database

**Scope:** the original-kernel x86-64 prototype on branch
`nexus-original-kernel-roadmap`. This is a source-backed compatibility
inventory, not a promise that every board using a listed chipset works.
PCI identifiers are matched by vendor/device and PCI class tuple; subsystem
IDs narrow a match when the driver descriptor provides them. A wildcard
(`*`) means the current driver path does not restrict that field.

## Status meanings

- **Functional path** — the kernel contains a driver path that performs real
  device I/O for the listed function. This does not mean all firmware,
  revisions, OEM boards, and physical systems have been validated.
- **Partial** — a constrained operation or subset of the protocol works;
  missing lifecycle, hotplug, power-management, or advanced features remain.
- **Inventory only** — Nexus can discover and identify the device, but has no
  native operational driver for that family.
- **Not supported** — there is no current native driver path. The device may
  still appear in PCI or firmware inventory.

## High-priority compatibility matrix

| Hardware family | Hardware ID / match | Current state | Implementation and limits |
|---|---|---|---|
| NVMe storage controllers | PCI class `01:08:02`, vendor/device wildcard | **Functional path (read-only default)** | The PCI manager dispatches the NVMe identify/probe path. A namespace is exposed through the block layer; normal boot discovery is read-only. The optional storage self-test build can exercise writes and must be treated as a test-only destructive configuration. |
| AHCI / SATA controllers | PCI class `01:06:01`, vendor/device wildcard | **Functional path (read-only default)** | AHCI controller initialization, port discovery, ATA identify and read-only block-device registration are present. ATAPI, RAID vendor modes, port multipliers and every SATA controller quirk are not implied by the generic class match. |
| Intel 8254x / 8257x Ethernet | PCI `8086:100E`, `8086:101E`, `8086:100F`, `8086:1026`, `8086:1010`, `8086:10D3` | **Functional path** | E1000 polling-mode rings and shared Ethernet send/receive path are implemented. Other Intel Ethernet generations are not covered by this ID list. |
| Realtek RTL8139 Ethernet | PCI `10EC:8139` | **Functional path** | RTL8139 polling-mode RX/TX and shared Ethernet device registration are implemented. This is not a claim of support for newer Realtek PCIe families. |
| VirtIO network | PCI `1AF4:1041` | **Functional path** | Modern VirtIO PCI feature negotiation and RX/TX virtqueues feed the common network stack. Legacy transitional IDs are not included by this exact match. |
| xHCI USB host controller | PCI class `0C:03:30` | **Partial** | Controller discovery and USB HID boot keyboard, mouse and gamepad handling exist. Generic bus enumeration, complete interface-driver binding and universal USB storage auto-attach/removal are not yet wired into the PnP manager. |
| USB HID devices | USB descriptors: HID class; boot keyboard/mouse and supported gamepad report layouts | **Partial** | Device descriptors and report formats are parsed behind xHCI. Compatibility is based on class/protocol/report-layout support, not a universal vendor/product whitelist. |
| USB mass storage | USB interface class `08`, subclass/protocol supported by BOT parser | **Partial** | A USB Mass Storage Bulk-Only Transport/SCSI implementation can initialize a supplied transport and register a block device. The current xHCI path does not yet automatically enumerate and bind every MSC interface. |
| Intel ICH-style AC'97 audio | PCI class `04:01` | **Partial** | AC'97 codec discovery and 48 kHz S16 stereo DMA playback exist. Multiple-controller arbitration, modern HDA, HDMI/DP audio, capture, mixing and broad codec validation remain incomplete. |
| VirtIO GPU / display | PCI `1AF4:1050` | **Partial** | The desktop can initialize the VirtIO 2D display path; optional Virgl 3D command submission depends on negotiated host capabilities. This is not a general Vulkan/OpenGL or modern discrete-GPU driver. |
| Intel integrated/discrete GPUs | PCI vendor `8086`, class `03:*` | **Inventory only** (except firmware display output) | Vendor/family/device inventory and firmware framebuffer output are available. No native Intel GPU command-submission driver is claimed. |
| AMD Radeon GPUs | PCI vendor `1002`, class `03:*` | **Inventory only** | The hardware is identified and reported; there is no native Radeon acceleration driver. |
| NVIDIA GPUs | PCI vendor `10DE`, class `03:*` | **Inventory only** | The hardware is identified and reported; there is no native NVIDIA acceleration driver. |
| VMware / QEMU display adapters | PCI vendors `15AD` / `1234`, class `03:*` | **Inventory only** except framebuffer / VirtIO paths above | Generic display identity is retained. VGA framebuffer availability is supplied by firmware/bootloader; this is not a native SVGA or QEMU VGA acceleration driver. |
| PCI Wi-Fi adapters | PCI class `02:80` | **Inventory only** | Candidate discovery and Windows INF hardware-ID assessment are available. No general native Wi-Fi chipset driver is currently bound by the PnP manager; radio firmware and chipset-specific MAC/PHY paths remain required. |
| Legacy PS/2 keyboard and mouse | Platform/legacy controller discovery | **Partial** | The legacy input path supports basic keyboard and pointer input where firmware exposes compatible controllers. Modern USB input follows the xHCI HID path above. |

The matching entries above reflect code paths in the kernel tree. A generic
class match (for example, AHCI or xHCI) is a driver-family selector, not proof
that every controller implementing that class is electrically or
firmware-compatible.

## Known implementation limits

The current driver manager is a bounded kernel registry, not a production
plug-and-play subsystem:

- Capacity is 32 registered drivers, 128 binding-history records and 256 PnP
  device records. Capacity failures are reported rather than allocating
  unbounded memory.
- PCI discovery inventories the stored PCI functions, including devices with
  no matching driver. The PCI layer itself retains at most 128 functions.
- The registry matches bus, vendor/device, subsystem vendor/device, class,
  subclass and programming interface. Subsystem ID zero in a match is a
  wildcard; nonzero values request an exact match.
- Probe priority allows a preferred driver to fail safely and a lower-priority
  match to try. Probe failures and dependency-blocked candidates remain in
  binding history with an explicit reason.
- Dependencies must refer to a driver already registered. This keeps
  registration order acyclic; runtime load checks require every dependency to
  be loaded.
- Unload is refused while a driver has active bindings or while a loaded
  driver depends on it. Updating requires a strictly newer version and no
  active bindings. The current update operation replaces an in-kernel
  descriptor; it is not a signed on-disk package updater.
- `install()` registers an in-tree descriptor and retries known devices.
  It does **not** load an arbitrary external binary. Drivers are statically
  linked kernel code with callback descriptors.
- Remove callbacks are invoked for active bindings when a bus owner reports a
  device removal. Removal is refused before changing any binding if an active
  driver has no teardown callback; the PnP record remains active rather than
  falsely reporting a safe detach. The generic callback path is present, but
  PCI surprise removal and all xHCI USB hot-unplug events are not yet connected
  to it. Existing drivers without a quiesce/remove callback cannot be removed
  through this API or dynamically unloaded.
- Manager calls currently rely on serialized kernel/PnP control flow. The
  manager has no SMP lock, RCU grace period or concurrent unload barrier.
- There is no general driver sandbox, signed kernel-module loader, firmware
  delivery service, Windows WDM/NDIS execution layer, Linux kernel-module ABI,
  or automatic rollback to an older on-disk package.

## Updating this database

When adding a device, record the exact IDs observed by PCI/USB enumeration,
the driver callback that matched it, the operations actually exercised, and
the test environment. Do not change an entry to **Functional path** merely
because an ID was discovered or a driver descriptor matched. Require at least
one tested end-to-end operation on the target path, and retain unresolved
revision, firmware and OEM restrictions in the notes.

The companion `driver-compatibility.csv` is intended for scripts and future
Settings / Driver Manager views. It carries the same scope and caveats.
