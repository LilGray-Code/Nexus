# Keyboard and Mouse Implementation Status

**Branch:** `nexus-original-kernel-roadmap`  
**Scope:** USB input transport, hubs, composite HID, Bluetooth, legacy host controllers, hot-plug/resume, installer/desktop testing.  
**Status date:** 2026-11-27

This is an implementation ledger, not a hardware compatibility claim. A parser unit test does not prove that a USB device works through the complete controller, installer, and desktop path. Physical hardware results must be recorded separately.

## Ordered implementation

| Priority | Work | Current implementation | Remaining gate |
|---|---|---|---|
| 1 | xHCI root-port discovery | Scans the full HCSPARAMS1 MaxPorts count (1–255), uses a 16-bit loop counter, and bounds-checks PORTSC register offsets. Host-side tests cover offsets and port status. | Physical testing across multiple xHCI vendors. |
| 1 | USB hub downstream ports | xHCI now recognizes USB 2 and SuperSpeed hub descriptors, sends hub class GET_DESCRIPTOR/GET_STATUS/SET/CLEAR_FEATURE requests, powers switched ports, marks hub slot contexts, polls downstream port state, resets connected child ports, builds xHCI route strings and TT metadata, enumerates child HID/hub devices, and recursively detaches child trees. QEMU hub topology coverage is added to CI. | Hub change handling currently polls class GET_STATUS one port per scheduler pass instead of using the hub interrupt-IN endpoint. SuperSpeed companion/USB2 companion hub behavior, larger physical hub matrix, transient reset retries, and real hardware validation remain. |
| 2 | Composite USB HID | A bounded configuration-descriptor parser collects up to eight HID interfaces/endpoints and detects composite receivers. | Runtime still selects one HID interface per physical device. Simultaneous keyboard and mouse endpoints sharing one xHCI slot remain unimplemented; the parser test must not be mistaken for runtime support. |
| 3 | HID report protocol | Generic keyboard/mouse report parsing now supports up to eight independent report IDs per interface. The xHCI path stores the layouts and selects the matching report ID before normalizing keyboard/mouse events. | Expand report collections, Consumer Control, output/LED reports, high-resolution scrolling, broader descriptor corpus, fuzzing, and hardware validation. Proprietary vendor protocols remain out of scope. |
| 4 | Bluetooth | HCI command packet builder and Command Complete/Status parsers, event/ACL framing, USB Bluetooth interrupt/bulk endpoint discovery, L2CAP framing, Classic HIDP input report parsing, BLE HOGP notification/read-response parsing, and unit tests are present. | **Not a functioning Bluetooth input stack yet:** USB/UART transport queue/transfer integration, command serialization/timeouts/flow control, controller feature discovery, L2CAP channel establishment, authenticated pairing/bonding, secure key storage, ATT/GATT service/report-map discovery, notification subscription, settings UI, and end-to-end Bluetooth HID events remain. |
| 5 | Older USB host controllers | PCI class/subclass/programming-interface classification identifies UHCI, OHCI, EHCI, and xHCI. xHCI is the only controller driver currently advertised as available; legacy controllers are logged as detected but unsupported. | Implement and test actual UHCI/OHCI/EHCI controller drivers, DMA, interrupts, port state, and USB topology. |
| 6 | Hot-plug/recovery/resume | xHCI root ports are rescanned on connection-state transitions; hub ports are polled and child trees are detached recursively. Resume now checks controller state and attempts a full halt/cleanup/ring+DCBAA+scratchpad rebuild/restart/port rescan after controller state loss. | Connect the resume API to the platform's actual ACPI sleep/wake path; re-probe MMIO/PCI state when power is removed; preserve/re-establish firmware ownership; rebuild hub trees; and test sleep/resume and power removal on physical systems. |
| 7 | Installer and desktop validation | The QEMU input script verifies USB keyboard make/break, USB mouse movement/clicks, and keyboard/mouse hot-unplug/replug recovery. CI now invokes it for the live installer and the NVMe-installed desktop configuration. | CI execution must pass; add hub/composite/Bluetooth/legacy controller virtual-device coverage where supported; publish physical motherboard, front-panel, USB-C, receiver, and Bluetooth test results. |

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

The QEMU end-to-end test checks direct-attached USB keyboard/mouse input, disconnect/reconnect, and live installer plus installed desktop paths. A hub topology variant is also configured to test a keyboard and mouse behind a USB hub. A successful CI result is still not a substitute for physical testing on different motherboard chipsets, rear and front-panel ports, USB-C ports, powered/unpowered hubs, 2.4 GHz receivers, Bluetooth adapters, and legacy ports.

## No-universal-compatibility rule

Do not claim that every keyboard, mouse, receiver, hub, motherboard, Bluetooth radio, USB controller, or case-front connector works. Publish support only for tested controller/device combinations with evidence from the complete boot-to-desktop path.


## November 27, 2026 implementation update

- USB 2 and SuperSpeed hub descriptor parsing and class request builders are now connected to xHCI hub initialization and port-status polling.
- xHCI slot contexts include child route-string fields and USB 2 high-speed transaction-translator information for low/full-speed children.
- Child-device connect/disconnect handling tracks occupied non-HID ports to avoid repeatedly resetting attached storage or audio devices.
- Hub status is polled using class GET_STATUS requests (one downstream port per hub per xHCI poll pass); interrupt-IN status change endpoint scheduling is a remaining optimization/robustness task.
- Controller state-loss recovery now retains allocations if halt cannot be confirmed, and otherwise rebuilds controller DMA resources before rescanning.
- Added Bluetooth USB endpoint discovery, HCI command construction/response parsing, L2CAP framing, and HIDP/HOGP report-PDU parsers. These are protocol components only and do not constitute Bluetooth pairing or a working wireless keyboard stack.
- CI currently reports passing installer-runtime build and QA matrix on the preceding input changes. The latest original-kernel suite has been failing on individual host-side tests during iteration; the latest revision is being rerun. No physical motherboard testing has been performed in this environment.
