# Nexus Hardware Compatibility and Driver Platform Roadmap

**Status:** Implementation roadmap; not a claim of universal hardware support  
**Architecture target:** Nexus-owned x86-64 kernel and userland, Limine BIOS/UEFI boot  
**Test rule:** Discovery, driver binding, successful I/O, stress validation, and physical-device validation are separate maturity levels.

## 1. Goal and support terminology

The goal is broad, predictable PC and peripheral compatibility across Intel and AMD platforms and multiple PC vendors. The project must not define compatibility around a single laptop or a list of vendor names.

A hardware device is not "supported" merely because PCI/USB enumeration found it. Every driver and device entry must carry an evidence-backed support level.

| Level | Meaning | Evidence required |
|---|---|---|
| H0 — Enumerated | A bus or firmware table reports a device | Stable identity and resource metadata captured without unsafe register access |
| H1 — Matched | A built-in or installed driver advertises a matching hardware ID | Exact match rule and candidate driver/version recorded |
| H2 — Probed | Driver initialized the device and registered its class interface | Probe result, resource ownership and initialization log |
| H3 — Operational | Real device operations complete | Read/write, packet I/O, HID report, display, audio or class-specific transaction tests |
| H4 — Stress-tested | Repeated I/O, reset, timeout, suspend/resume or error recovery passes | Automated tests with bounded failure handling and no leaks/corruption |
| H5 — Physically validated | The exact device/revision works on a real PC | Hardware ID, firmware/BIOS version, test log and known limitations recorded |

Graphics needs additional dimensions: firmware framebuffer; display scanout; 2D acceleration; 3D command submission; shader/API conformance; video decode/encode; multi-monitor and mode-setting. A device must not receive a generic "GPU accelerated" label when only framebuffer scanout or CPU rasterization is used.

Power and device hotplug also need their own evidence. A driver that works at boot but fails after a sleep/resume cycle is not fully supported.

## 2. Current source baseline

The active original-kernel branch contains meaningful discovery and driver foundations. The following is the conservative status that should appear in engineering reports.

| Area | Verified source foundation | Status boundary / remaining work |
|---|---|---|
| CPU | CPUID vendor/brand/family/model/stepping, feature and XSAVE-aware capability discovery; FPU state support; AP startup and SMP test foundations | Broad Intel/AMD power-state, thermal, microcode policy, topology edge cases and real-hardware coverage remain |
| Firmware / motherboard | Limine BIOS/UEFI handoff, RSDP/SMBIOS inventory, ACPI MADT and MCFG parsing, PCI/PCIe ECAM discovery | Full AML interpretation, FADT/FACS power flow, embedded controller, battery, thermal zones and broad board validation remain |
| PCI / PCIe | Segment-aware device discovery, BAR/capability parsing, bus resource foundations | MSI/MSI-X policy, hotplug, IOMMU isolation and complete resource lifecycle remain |
| Intel / AMD / NVIDIA GPUs | PCI display-controller identity, subsystem IDs and revision are retained | These identities do not establish a native display or 3D driver |
| Virtual graphics | VirtIO-GPU transport and host scanout with CPU-rendered backing surface | Not equivalent to physical GPU acceleration or a production Vulkan/OpenGL stack |
| NVMe | PCI discovery and Identify Controller foundation are present; QEMU CI tests controller metadata | Do not claim general namespace data I/O until namespace enumeration, read/write, flush, timeout and recovery tests pass |
| SATA / AHCI | AHCI controller initialization, ATA IDENTIFY, DMA read path, guarded write/flush path and disposable-image testing exist | Production write policy remains gated; broaden device/controller compatibility, error recovery and physical validation |
| Ethernet | e1000, RTL8139 and VirtIO network drivers register through a common frame interface | Coverage is limited to specific controller families; add device IDs, link/error handling and physical tests |
| Wi-Fi | PCI wireless candidate inventory, radio/security state foundations, WPA2 components and firmware/driver matching groundwork | Candidate detection is not a working radio. Supported chipset drivers, signed firmware, scanning, association, regulatory behavior and packet I/O need proof |
| USB | xHCI foundation, USB HID keyboard/mouse/gamepad paths and USB mass-storage work/tests | Expand controller quirks, hub/device lifecycle, hotplug, power management and device-class coverage |
| Bluetooth | No complete controller/HCI plus common profile stack has been demonstrated by the current release gates | Implement HCI transport and profiles, then test actual adapters and peripherals |
| Audio | AC'97 controller/audio path and bounded PCM DMA foundation | Intel HD Audio, USB Audio Class, modern codecs, microphone routing, jack detection and Bluetooth audio remain |
| Printers | No complete print spooler and broad USB/network printer stack is verified | Add standard USB printer class and/or IPP client/spooler before vendor-specific drivers |
| Cameras / microphones | USB and audio primitives exist | UVC camera streaming, USB Audio Class capture, format negotiation and application permissions remain |
| External displays | Bootloader framebuffer and virtual display path | EDID, physical mode setting, DisplayPort/HDMI link training, hotplug, refresh-rate control and multi-monitor support remain |
| Battery / sleep / hibernate | ACPI discovery foundations | ACPI AML interpreter, EC/battery drivers, S-states, S0ix, device suspend/resume and hibernation are not complete |

This table must be kept current as tests change. The words "supported", "working", and "accelerated" must always include the tested hardware family and maturity level.

## 3. Unified device and driver architecture

Nexus currently has a small statically registered driver manager, but device families are not yet all routed through a single lifecycle. The driver manager has a fixed registry/binding capacity and driver probe callbacks currently return a boolean. The hardware-support increment adds a backward-compatible diagnostic probe report so callers can distinguish:

- Invalid device/probe request.
- No registered driver matches the hardware identity.
- A driver successfully initialized and bound.
- One or more candidate drivers matched, but every probe failed.
- Driver registry or binding capacity was exhausted.

The storage path uses this report to distinguish an unavailable driver from a detected-but-failing driver. This is an initial diagnostic improvement, not yet a dynamic driver package manager.

### 3.1 Stable device identity

Create a bus-neutral device descriptor with bus-specific identity extensions.

PCI/PCIe identity must include:

- Segment/domain, bus, device, and function.
- Vendor ID, device ID, subsystem vendor/device IDs, revision.
- Class, subclass and programming interface.
- Header type, BAR resource ranges and capabilities.
- Interrupt mode/capabilities, DMA width and IOMMU group when known.
- Parent bridge and hotplug slot where available.

USB identity must include:

- Controller, root hub, hub path, port and device address.
- Vendor/product/revision IDs.
- Device, configuration, interface and endpoint descriptors.
- Interface class/subclass/protocol and serial string only when safely available.

Firmware/platform identity must include ACPI HID/CID identifiers, compatible IDs, parent namespace path, firmware resources and power methods. CPU devices and board-level platform functions should use explicit typed descriptors rather than pretending to be PCI devices.

Do not use a product name or marketing model as the matching key. OEM subsystem IDs and device revisions matter for firmware and quirks.

### 3.2 Device lifecycle

Use a common state machine with explicit ownership:

`DISCOVERED -> MATCHED -> PROBING -> ACTIVE`

Failure and removal paths must be explicit:

`PROBING -> FAILED`, `ACTIVE -> QUIESCING -> REMOVED`, and `MATCHED -> UNBOUND`.

A device must not be exposed to applications as ready until the driver has completed initialization and registered the relevant class interface.

The bus layer owns enumeration and resource discovery. The device manager owns identity, parent/child topology and lifecycle. The driver owns only resources assigned to it. The class subsystem owns the stable user-facing interface, such as block, network, HID, audio, camera or display.

### 3.3 Driver resource contract

Before adding many new hardware drivers, define reusable kernel interfaces for:

- PCI config access and safe BAR assignment.
- MMIO mapping with cache attributes and bounds checks.
- DMA allocation, mapping, coherency, address-width constraints and teardown.
- IOMMU group/domain attachment and device isolation where supported.
- MSI/MSI-X and legacy interrupt routing.
- Deferred work, timers, locks and wait queues.
- Firmware blob request/validation and secure lifetime.
- Device reset, power transitions, suspend/resume and removal.
- Structured logs, health state, last error and counters.

Bus mastering must not be enabled before DMA buffers and mappings are valid. Unknown devices must remain inert. MMIO must not be touched using an unvalidated BAR. An unresponsive device must time out and become failed/offline without halting the OS.

### 3.4 Driver match rules

Match priority should be:

1. Exact bus + vendor/device + subsystem vendor/device + revision range.
2. Exact vendor/device with class/function validation.
3. Standard class/subclass/interface match where the protocol is genuinely vendor-independent.
4. Explicit compatibility fallback with a reduced feature list.

Wildcard vendor/device matches must never cause a driver to touch arbitrary hardware. A match is permission to attempt a safe probe, not permission to assume identical registers.

## 4. Driver-management application and package lifecycle

The user-facing Device Manager should show every discovered device, including unsupported devices.

Required information:

- Device name and category.
- Exact hardware IDs and parent bus path.
- Current support level H0–H5.
- Bound driver name, version and origin (built-in, Nexus-signed package or compatibility layer).
- Firmware dependencies and version.
- Device health: active, degraded, probe failed, timed out, disabled or removed.
- Last failure reason and useful troubleshooting steps.
- Whether the driver is native Nexus code, a port of upstream code, or a compatibility component.

### 4.1 Package installation and updates

Initially, most kernel drivers are statically built into the kernel. A downloadable driver-package feature must wait until the kernel has a stable module ABI, signature verification, dependency loading, safe unload rules and rollback.

When those foundations exist, implement a signed driver repository with:

- Signed manifest and package hash.
- Supported Nexus kernel ABI range.
- Exact supported hardware-ID list and tested revisions.
- Architecture, dependencies and required services.
- Firmware blobs with a separate per-file redistribution/license review.
- Driver source/origin/license and security contact.
- Install, update, disable, uninstall and rollback operations.
- Reboot requirement when a kernel module cannot be safely replaced live.
- A known-good driver fallback and recovery boot mode.

Never automatically install an untrusted kernel-mode binary solely because its manifest names a device ID. Firmware packages are not executable drivers and need their own validation and license handling.

## 5. Use of upstream drivers and compatibility layers

### 5.1 Preferred path: native ports of maintained upstream implementations

Use vendor-maintained specifications and high-quality open-source drivers as the primary source of hardware behavior. Port the relevant device-specific logic behind the Nexus bus, DMA, interrupt, power, firmware and class interfaces.

A Linux driver is not a drop-in Nexus driver. Much of a Linux driver depends on Linux's driver core, PCI and DMA APIs, memory allocation, locking, work queues, device resources, power management and networking/graphics subsystems. Porting requires an auditable adaptation layer and conformance testing. The upstream license must be reviewed before code is copied or adapted.

Prioritize standard protocols and well-supported devices before vendor-specific features. VirtIO, AHCI, NVMe, USB HID, USB mass storage, USB Audio Class, UVC, standard Ethernet and ACPI provide useful compatibility targets.

### 5.2 Linux compatibility shim: only a scoped fallback

A Linux-driver compatibility layer may eventually reduce porting work for a selected family, but it is a substantial OS subsystem, not a small header-compatibility trick.

If evaluated, scope it to one driver class and implement only the kernel APIs required by that reviewed driver set. Do not claim general Linux kernel module compatibility. Keep the shim versioned, measured, testable and isolated from the stable Nexus native driver API.

### 5.3 Windows driver compatibility

Do not load arbitrary Windows `.sys` files as native Nexus drivers. A PE/COFF parser or a Windows INF reader does not provide the Windows driver environment.

Windows kernel drivers integrate with Windows device objects and device stacks, Plug and Play, power management, I/O request handling, DMA, interrupts and Windows kernel services. WDM/WDF/NDIS drivers expect those interfaces. A driver binary can be structurally valid yet unusable because the operating-system contract is absent.

Possible future experiments, each with an explicit hardware list:

- User-mode compatibility for applications that communicate through standard device interfaces.
- A narrowly scoped NDIS miniport compatibility layer for selected networking hardware, only after a sufficient Windows kernel-driver ABI and PnP/power/DMA model exists.
- A WDF/WDM compatibility environment for selected device classes, treated as a major independent subsystem.
- Native reimplementation or upstream port, preferred when practical.

None of these approaches supplies a universal Windows driver layer. Kernel-mode Windows driver compatibility must remain marked experimental until actual devices and stress/recovery tests pass. See the Microsoft references below for the operating-system-specific WDM/WDF contracts.

## 6. Hardware-family implementation plans

### 6.1 Intel and AMD processors

- Preserve a conservative x86-64 baseline and detect optional CPU features before use.
- Validate CPUID topology on hybrid and multi-socket systems; do not assume one leaf or one logical-CPU layout works for every generation.
- Keep XSAVE/XCR0 state consistent with scheduler context switching; expose AVX/AVX2/AVX-512 only when the OS saves and restores the required state.
- Implement local APIC timer, IOAPIC routing, MSI/MSI-X, TSC calibration, invariant clock selection and CPU hotplug policy.
- Add ACPI CPPC, Intel HWP and AMD P-state/CPPC controls only through validated firmware and per-vendor capability paths.
- Implement thermal reporting and safe fan/thermal response before exposing manual tuning controls.
- Define a signed microcode-update policy and recovery path before attempting runtime microcode packages. Never write undocumented MSRs based solely on a vendor string.

### 6.2 Motherboards, chipsets and buses

- Finish PCIe bridges, multifunction devices, capability/resource parsing, bus-number windows and hotplug lifecycle.
- Add explicit MSI/MSI-X setup/teardown and interrupt affinity.
- Add IOMMU detection and DMA isolation before granting third-party module DMA access.
- Use ACPI namespace resources to discover platform devices and motherboard controllers.
- Test UEFI and legacy BIOS boot on several vendor platforms and firmware generations.
- Unknown chipset functions remain inventory-only and must not be probed by speculative register writes.

### 6.3 NVIDIA, AMD Radeon and Intel graphics

The roadmap has two separate graphics tracks.

**Bring-up and baseline display**
- Keep firmware framebuffer as a reliable fallback.
- Complete VirtIO-GPU 2D/display transport and validation.
- Add EDID parsing, display hotplug, mode selection, multiple outputs and reliable software rendering.
- Clearly label the renderer path and do not claim hardware acceleration from a PCI vendor ID.

**Physical GPU acceleration**
- Establish stable GPU object, memory, command submission, fence, interrupt and reset interfaces.
- Port maintained upstream driver components for well-defined Intel and AMD GPU generations first, with exact PCI IDs and required firmware documented.
- Evaluate open-source NVIDIA paths for specific supported generations; distinguish driver coverage and feature maturity by generation.
- Port or adapt a user-mode graphics stack such as Mesa only after the kernel graphics ABI and memory model are stable. Mesa's driver catalogue is a useful upstream capability inventory, but its platform integration and kernel-facing interfaces must be reviewed for Nexus rather than assumed to work unchanged.
- Implement Vulkan/OpenGL conformance and real application tests before publishing API support.
- Treat video decode, display mode-setting, HDR, VRR, multi-monitor and GPU compute as separate capabilities.

### 6.4 NVMe, SATA and removable storage

**NVMe**
- Complete controller reset and initialization.
- Enumerate namespaces and identify logical/physical sector geometry.
- Implement admin and I/O queue lifecycle, queue-full behavior, timeouts, completion handling, reset and controller failure recovery.
- Add bounded read, write, flush and discard behavior with namespace bounds checks.
- Test 512-byte and 4Kn media, multiple namespaces, hotplug and error injection.
- Keep destructive installation writes separately authorized.

**SATA/AHCI**
- Expand from current ATA IDENTIFY and bounded DMA transfers to multi-port stress tests, reset, timeout recovery, ATAPI and controller quirks.
- Verify flush/FUA semantics and write caching behavior before allowing durable filesystem operations.
- Maintain read-only default exposure until the installer/session policy explicitly authorizes writes.

**USB mass storage**
- Complete BOT and evaluate UAS separately.
- Test hubs, multiple LUNs, removable media, device removal mid-I/O, reset recovery and malformed descriptors.

No installer test may target a real disk. Use disposable virtual disks and dedicated spare media.

### 6.5 Ethernet, Wi-Fi and Bluetooth

**Ethernet**
- Extend the existing e1000/RTL8139/VirtIO foundations with a documented PCI ID and revision matrix.
- Add queue/DMA ownership, interrupt moderation, link changes, MTU handling, checksum offload negotiation and reset recovery.
- Add further widely used families only after a driver has H2/H3 evidence in QEMU and physical tests.

**Wi-Fi**
- Select chipsets by exact PCI/USB ID and firmware availability, not by a vague "Intel Wi-Fi" or "Realtek Wi-Fi" category.
- Implement one supported station chipset end to end: firmware request, reset, scan, association, authentication, key installation, packet transmit/receive, disconnect and reconnect.
- Add regulatory-domain handling, rfkill, power-save and suspend/resume.
- Keep the 802.11 MAC/crypto/supplicant interface distinct from the Ethernet frame interface.
- Expand WPA2/WPA3 support only with reviewed cryptography, fresh randomness, protocol tests and real AP interoperability.
- Maintain an explicit supported chipset/firmware table.

**Bluetooth**
- Implement USB/PCI HCI transport, controller reset, device discovery, pairing, security, reconnect and power handling.
- Add profiles by priority: HID, audio, then serial/other profiles.
- Keep Bluetooth audio distinct from wired audio devices while exposing a consistent audio-session API.
- Validate real controller and peripheral combinations.

### 6.6 USB input, audio, cameras and printers

- Expand xHCI lifecycle and add reliable hub enumeration, hotplug and port reset.
- Support standard HID boot and report protocols for keyboards, mice, touchpads and controllers. Handle composite devices and malformed reports safely.
- Add USB Audio Class playback/capture, sample formats, endpoints and feedback.
- Add UVC camera enumeration, stream negotiation, supported pixel formats and capture buffers.
- Add USB printer class and IPP/network printing with a spooler and per-printer capability query.
- Treat proprietary gaming peripherals, programmable buttons, RGB control and vendor tools as optional device-specific extensions.
- Provide stable user-mode handles and permission controls for cameras, microphones and input devices.

### 6.7 ACPI, battery, sleep, hibernation and power management

- Keep current MADT/MCFG discovery; add validated FADT, FACS, DSDT and SSDT handling.
- Integrate and audit ACPICA or another maintained ACPI AML interpreter rather than building an incomplete AML interpreter in the kernel.
- Implement ACPI namespace enumeration, device resources, power methods and event handling.
- Add battery and AC adapter interfaces, embedded-controller support where required, thermal zones, lid switch and brightness controls.
- Implement safe device suspend/resume ordering and CPU/platform state transitions.
- Add S3 and modern S0ix only where firmware and hardware support are proven; distinguish platform capability from actual working sleep.
- Implement hibernation only after stable storage, memory-image integrity, resume boot validation and rollback protections exist.

### 6.8 External displays and other peripherals

- Implement EDID parsing and validation, connector topology and mode database.
- Add monitor hotplug and display configuration persistence.
- Implement HDMI/DisplayPort link training and mode setting with the target GPU driver, not as a generic PCI feature.
- Add scaling, refresh selection and multi-monitor tests.
- Provide standard class-based support before vendor-specific printer, webcam, capture device, docking station, card reader and headset features.

## 7. CI and physical compatibility matrix

The existing original-kernel workflow already exercises a useful set of emulated devices: UEFI/Q35, legacy BIOS/PC, e1000 and VirtIO networking, VirtIO GPU, AC'97 audio, xHCI keyboard/mouse, NVMe and AHCI-backed disposable images, and multiple CPUs. Retain those profiles, but ensure each step proves the claimed operation rather than merely checking a log string.

Expand CI profiles incrementally:

| Profile | Devices | Required result |
|---|---|---|
| UEFI Q35 baseline | PCIe, APIC, ACPI, framebuffer | Boot, topology and resource inventory |
| Legacy BIOS PC | IDE/legacy interrupt path | Boot and safe controller discovery |
| NVMe | Multiple namespaces, small and large sectors | Identify, read, write, flush and read-back on disposable image |
| AHCI | Multiple SATA ports | IDENTIFY, bounded read/write/flush, reset and error handling |
| USB | xHCI, hubs, HID, mass storage, audio/camera where emulated | Enumerate, operate, disconnect and recover |
| Ethernet | e1000 and VirtIO | TX/RX, DHCP/DNS/network tests when the stack supports them |
| Graphics | VirtIO-GPU and software fallback | Scanout and explicit render-path reporting |
| CPU/SMP | Intel/AMD-style feature profiles and different core counts | Feature gating, AP startup, timer and interrupt behavior |
| Firmware | ACPI and UEFI variations | Table validation, power capability reporting and safe fallback |

Physical testing must rotate across multiple manufacturers, CPU vendors and generations. Record:

- PC model as context, not as the compatibility key.
- CPU vendor/family/model/stepping and topology.
- BIOS/UEFI vendor/version and boot mode.
- PCI segment/BDF, vendor/device/subsystem IDs and revision.
- USB VID/PID/revision/interface descriptor for USB peripherals.
- Firmware and driver package versions.
- Exact H0–H5 level and test logs per subsystem.
- Known limitations and recovery result.

Every driver suite must include negative tests for malformed descriptors, invalid BARs, DMA boundary violation, missing firmware, probe failure, unplug during I/O, controller timeout, reset failure, resource exhaustion and unload/rebind. Never let a device error crash the entire desktop.

## 8. Priority order and exit criteria

### H0 — Hardware inventory and diagnostic correctness
- Complete stable PCI, USB and ACPI device identity.
- Report no matching driver separately from failed candidate probes.
- Report support level and failure reason.
- Add CI for the common driver manager.

**Exit:** Every enumerated device can be listed with exact identity and honest driver status.

### H1 — Bus, DMA, interrupt and driver lifecycle
- Complete resource ownership and teardown.
- Add DMA/IOMMU foundations and MSI/MSI-X.
- Add safe probe, remove, reset, hotplug and structured failure handling.

**Exit:** Driver failure/removal does not corrupt other devices or wedge the kernel.

### H2 — Storage and USB
- Complete NVMe namespace block I/O.
- Validate SATA/AHCI durable data operations.
- Finish USB hubs, HID and mass-storage lifecycle.

**Exit:** Disposable-device tests demonstrate read/write/flush/readback, removal and recovery without filesystem corruption.

### H3 — Network and wireless
- Extend Ethernet ID coverage and error handling.
- Bring up one Wi-Fi chipset end to end.
- Add Bluetooth HCI and HID foundation.

**Exit:** Real-device wireless scan/connect/reconnect and Bluetooth pairing are tested on named hardware.

### H4 — Audio, camera, printer and platform power
- Add HDA and USB Audio Class.
- Add UVC and printer support.
- Add ACPI AML, battery, suspend/resume and power policy.

**Exit:** Named devices work through user-visible applications and survive suspend/resume where supported.

### H5 — Accelerated graphics and broad vendor coverage
- Complete physical GPU driver and command path.
- Port a compatible user-mode graphics stack.
- Add API conformance, monitor modes and game/application tests.

**Exit:** Publish per-generation GPU support and measured acceleration. Do not promise universal NVIDIA/AMD/Intel compatibility.

### H6 — Signed driver and firmware package management
- Complete module ABI, signature verification, repository, dependency handling, firmware licensing and rollback.
- Integrate Device Manager install/update/uninstall.

**Exit:** A driver update can be verified, installed, activated, rolled back and recovered from a failed boot without risking user data.

## 9. Release rules

- A hardware ID is not a driver.
- A successful probe is not successful device I/O.
- Successful QEMU I/O is not physical compatibility.
- A working framebuffer is not GPU acceleration.
- A PCI Wi-Fi candidate is not a working wireless adapter.
- A valid PE/COFF binary is not a compatible Windows driver.
- A driver package must identify its exact kernel ABI, hardware IDs, origin, license, firmware and tests.
- Every public compatibility claim must refer to a specific tested hardware family and maturity level.
- Never auto-enable bus mastering, write to storage, flash firmware or change platform power state without a validated resource and authorization path.

## 10. Upstream references to evaluate

- [Microsoft — Introduction to WDM](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-wdm)
- [Microsoft — Differences Between WDM and KMDF](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/differences-between-wdm-and-kmdf)
- [Microsoft — Choosing a driver model](https://learn.microsoft.com/en-us/windows-hardware/drivers/gettingstarted/choosing-a-driver-model)
- [Mesa — Platforms and Drivers](https://docs.mesa3d.org/systems.html)
- [Intel — ACPICA source and releases](https://www.intel.com/content/www/us/en/developer/topic-technology/open/acpica/download.html)

These references are candidates for technical study and integration planning, not a statement that their kernel-facing components can be used unchanged in Nexus. Review exact versions, APIs, licenses, security advisories and platform assumptions before adopting any source.

## 11. Changes in this hardware-support increment

The driver-manager API now has an explicit probe outcome/report:

- `INVALID`
- `NO_MATCH`
- `ACTIVE`
- `ALL_FAILED`
- `CAPACITY_EXHAUSTED`

The existing boolean APIs remain as compatibility wrappers. PCI probe reporting is exposed without requiring each caller to construct its own generic bus descriptor. The storage controller path now reports "no compatible driver registered" separately from "matching driver probes failed". Host tests cover fallback success, no match, invalid device and all-candidates-failed outcomes. CI is wired to run that driver-manager test.

This does **not** yet implement dynamic driver installation, a downloadable driver catalog, dynamic kernel modules, broad hardware support, a new GPU driver, a Wi-Fi chipset driver, Bluetooth, HDA, webcams, printers or ACPI sleep. Those remain explicit engineering milestones above.
