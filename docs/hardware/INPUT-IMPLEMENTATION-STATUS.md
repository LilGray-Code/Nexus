# Keyboard and Mouse Implementation Status

**Branch:** `nexus-original-kernel-roadmap`  
**Scope:** USB input transport, hubs, composite HID, Bluetooth, legacy host controllers, hot-plug/resume, installer/desktop testing.  
**Status date:** 2026-11-27

This is an implementation ledger, not a hardware compatibility claim. A parser unit test does not prove that a USB device works through the complete controller, installer, and desktop path. Physical hardware results must be recorded separately.

## Ordered implementation

| Priority | Work | Current implementation | Remaining gate |
|---|---|---|---|
| 1 | xHCI root-port discovery | Scans the full HCSPARAMS1 MaxPorts count (1–255), uses a 16-bit loop counter, and bounds-checks PORTSC register offsets. Host-side tests cover offsets and port status. | Physical testing across multiple xHCI vendors. |
| 1 | USB hub downstream ports | USB 2 hub descriptor parsing, port-status decoding, bounded 5-tier/4-bit route-string helpers, and tests exist. | **Not yet a live hub driver:** issue hub class requests, power/reset downstream ports, set parent hub/port and route fields in xHCI slot contexts, enumerate child devices, process hub change endpoints, and support disconnect/reconnect in hub trees. SuperSpeed hub support is also pending. |
| 2 | Composite USB HID | A bounded configuration-descriptor parser now collects up to eight HID interfaces and their interrupt-IN endpoints, with a composite keyboard/mouse receiver test. | xHCI still configures and services one HID interrupt endpoint for a physical USB device. Multiple HID interfaces/endpoints sharing one xHCI slot are not yet fully configured or dispatched. |
| 3 | HID report protocol | Generic keyboard/mouse report parsing now supports up to eight independent report IDs per interface. The xHCI path stores the layouts and selects the matching report ID before normalizing keyboard/mouse events. | Expand report collections, Consumer Control, output/LED reports, high-resolution scrolling, broader descriptor corpus, fuzzing, and hardware validation. Proprietary vendor protocols remain out of scope. |
| 4 | Bluetooth | Bounded HCI event and ACL frame parsers validate packet lengths and decode Classic/LE connection-complete and disconnection events. HCI/HID protocol constants are documented. | **Not a functioning Bluetooth stack yet:** USB/UART HCI transport, controller reset/feature discovery, command sequencing and flow control, L2CAP, pairing/bonding and secure key storage, ATT/GATT, HIDP, HOGP, notifications, and settings UI are not implemented. |
| 5 | Older USB host controllers | PCI class/subclass/programming-interface classification identifies UHCI, OHCI, EHCI, and xHCI. xHCI is the only controller driver currently advertised as available; legacy controllers are logged as detected but unsupported. | Implement and test actual UHCI/OHCI/EHCI controller drivers, DMA, interrupts, port state, and USB topology. |
| 6 | Hot-plug/recovery/resume | xHCI root ports are rescanned periodically to recover missed port events and retry failed direct-device enumeration. A resume check is exposed; it rescans only if the controller retained RUN/not-HALTED state. | Connect the resume hook to platform power management; safely reinitialize xHCI after controller context loss; process hub-child hot-plug; test repeated transfer stalls, disconnect races, and suspend/resume on physical hardware. |
| 7 | Installer and desktop validation | The QEMU input script verifies USB keyboard make/break and USB mouse movement/clicks. CI now invokes it for the live installer and the NVMe-installed desktop configuration. | CI execution must pass; add hub/composite/Bluetooth/legacy controller virtual-device coverage where supported; publish physical motherboard, front-panel, USB-C, receiver, and Bluetooth test results. |

## Important implementation details

- Root-port count is not the same as the number of input-device slots. Keyboard, mouse, and gamepad storage remains explicitly bounded.
- USB hub helper code is currently protocol/topology groundwork only. Do not advertise devices behind external or internal hubs as operational until live child enumeration and event handling exist.
- Composite HID interface discovery is distinct from configuring multiple interrupt endpoints under one USB device slot. The latter remains required.
- Multi-report-ID support is for bounded generic keyboard/mouse reports. Unsupported report layouts must fail closed and must not be misclassified as boot-protocol devices.
- HCI framing and link-event decoding do not establish Bluetooth HID support. A controller is not "connected" to the input stack until a complete authenticated HIDP/HOGP path delivers reports.
- UHCI/OHCI/EHCI PCI recognition is inventory only; it must not be described as a driver.
- The resume check intentionally refuses to touch rings or contexts after it detects a halted controller. A full controller restart must rebuild controller resources and enumerate devices safely.

## Test commands used by CI

The workflow compiles host-side tests for:
- xHCI root-port count, register offset and PORTSC helpers;
- USB 2 hub descriptor parsing, hub port status and route strings;
- composite HID interface discovery;
- multiple HID report IDs;
- HCI framing and Classic/LE link events;
- PCI host-controller classification;
- existing boot-protocol and generic HID keyboard/mouse decoders.

The QEMU end-to-end test checks the live installer and installed desktop separately. A successful CI result is still not a substitute for physical testing on different motherboard chipsets, rear and front-panel ports, USB-C ports, hubs, 2.4 GHz receivers, Bluetooth adapters, and legacy ports.

## No-universal-compatibility rule

Do not claim that every keyboard, mouse, receiver, hub, motherboard, Bluetooth radio, USB controller, or case-front connector works. Publish support only for tested controller/device combinations with evidence from the complete boot-to-desktop path.
