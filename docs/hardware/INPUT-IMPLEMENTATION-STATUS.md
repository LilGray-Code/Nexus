# Keyboard and Mouse Implementation Status

**Branch:** `nexus-original-kernel-roadmap`  
**Scope:** USB input transport, hubs, composite HID, Bluetooth, legacy host controllers, hot-plug/resume, installer/desktop testing.  
**Status date:** 2026-11-27

This is an implementation ledger, not a hardware compatibility claim. A parser unit test does not prove that a USB device works through the complete controller, installer, and desktop path. Physical hardware results must be recorded separately.

## Ordered implementation

| Priority | Work | Current implementation | Remaining gate |
|---|---|---|---|
| 1 | xHCI root-port discovery | Scans the full HCSPARAMS1 MaxPorts count (1–255), uses a 16-bit loop counter, and bounds-checks PORTSC register offsets. Host-side tests cover offsets and port status. | Physical testing across multiple xHCI vendors. |
| 1 | USB hub downstream ports | xHCI recognizes USB 2 and SuperSpeed hub descriptors, sends hub class GET_DESCRIPTOR/GET_STATUS/SET/CLEAR_FEATURE requests, powers switched ports, marks hub slot contexts, polls downstream port state, resets connected child ports, builds xHCI route strings and TT metadata, enumerates child HID/hub devices, and recursively detaches child trees. It tolerates shortened unused USB 2 hub bitmap tails when the fixed fields are present, and defers the power-good wait to timer-driven polling rather than blocking boot. QEMU hub topology coverage is in CI. | Hub change handling currently polls class GET_STATUS one port per scheduler pass instead of using the hub interrupt-IN endpoint. SuperSpeed companion/USB2 companion hub behavior, larger physical hub matrix, transient reset retries, and real hardware validation remain. |
| 2 | Composite USB HID | xHCI can configure a second keyboard-or-mouse HID interface on the same USB slot, with an independent interrupt ring/report buffer, endpoint-ID dispatch, and per-endpoint recovery. The configuration parser and companion-interface selector have host tests. | Current runtime supports one primary plus one secondary opposing keyboard/mouse interface. More than two HID interfaces, arbitrary multi-endpoint collections, and a physical composite-receiver test remain. |
| 3 | HID report protocol | Generic keyboard/mouse report parsing supports up to eight report IDs per interface. Generic relative mouse X/Y deltas retain signed 16-bit movement end to end. xHCI prefers valid HID Report protocol for boot-class mice to retain wheel and extra-button fields, with Boot protocol fallback. | Expand report collections, Consumer Control, output/LED reports, broader descriptor corpus, fuzzing, and hardware validation. Proprietary vendor protocols remain out of scope. |
| 4 | Bluetooth | HCI framing, command-credit-aware Reset/Read Local Version/Read BD_ADDR bring-up state machine, USB Bluetooth interrupt/bulk endpoint discovery, L2CAP framing, HIDP input parsing, HOGP report parsing, and ATT/GATT HID service/characteristic/Report Reference/CCCD discovery parsers and request builders have unit tests. | **Not a functioning Bluetooth input stack yet:** connect USB/UART endpoint transfers to the HCI host state machine, complete command timeouts/ACL reassembly and L2CAP signaling, add authenticated SMP/SSP pairing and secure bonding storage, subscribe to discovered GATT reports, and route decoded Bluetooth HID reports to the common input service. Settings UI and end-to-end radio testing remain. |
| 5 | Older USB host controllers | PCI class/subclass/programming-interface classification identifies UHCI, OHCI, EHCI, and xHCI. xHCI is the only controller driver currently advertised as available; legacy controllers are logged as detected but unsupported. | Implement and test actual UHCI/OHCI/EHCI controller drivers, DMA, interrupts, port state, and USB topology. |
| 6 | Hot-plug/recovery/resume | xHCI root ports and hub-child ports are monitored; child trees detach recursively. The xHCI poll watchdog checks RUN/HALTED/CNR after wake and invokes a full halt/cleanup/DMA-ring+DCBAA+scratchpad rebuild/restart/root-port rescan when controller state is lost. It retains DMA memory if halt cannot be proven. | Wire recovery into real ACPI S3/platform wake notifications; restore firmware ownership and reinitialize after D3/cold power transitions; rebuild and re-enumerate nested hubs; test actual suspend/resume and cold power transitions on physical hardware. |
| 7 | Installer and desktop validation | The QEMU input script verifies USB keyboard make/break, USB mouse movement/clicks, and keyboard/mouse hot-unplug/replug recovery. CI now invokes it for the live installer and the NVMe-installed desktop configuration. | CI execution must pass; add hub/composite/Bluetooth/legacy controller virtual-device coverage where supported; publish physical motherboard, front-panel, USB-C, receiver, and Bluetooth test results. |

## Important implementation details

- Root-port count is not the same as the number of input-device slots. Keyboard, mouse, and gamepad storage remains explicitly bounded.
- USB 2 hub child enumeration is integrated into xHCI using class GET_STATUS polling. SuperSpeed hub descriptors/port status and route handling exist, but full SuperSpeed companion-topology and endpoint-companion coverage still requires validation. Do not claim every hub/USB3 topology works.
- Composite HID now supports a primary endpoint plus one secondary keyboard/mouse endpoint on a single USB slot. It does not yet generalize to arbitrary numbers of endpoints or arbitrary composite collections.
- Multi-report-ID support is for bounded generic keyboard/mouse reports. Generic relative mouse X/Y movement preserves signed 16-bit values to the desktop; wheel remains an 8-bit signed step value. Unsupported report layouts must fail closed and must not be misclassified as boot-protocol devices.
- HCI framing and link-event decoding do not establish Bluetooth HID support. A controller is not "connected" to the input stack until a complete authenticated HIDP/HOGP path delivers reports.
- UHCI/OHCI/EHCI PCI recognition is inventory only; it must not be described as a driver.
- The resume watchdog refuses to free DMA buffers if controller halt cannot be confirmed. After confirmed halt it rebuilds controller resources; actual ACPI wake wiring and hardware power-loss tests remain.

## Test commands used by CI

The workflow compiles host-side tests for:
- xHCI root-port count, register offset and PORTSC helpers;
- USB 2 hub descriptor parsing, hub port status and route strings;
- composite HID interface discovery and companion keyboard/mouse selection;
- multiple HID report IDs;
- HCI framing, command bring-up, USB endpoint discovery, L2CAP/HIDP/HOGP, and BLE GATT discovery/request builders;
- PCI host-controller classification;
- existing boot-protocol and generic HID keyboard/mouse decoders.

The QEMU end-to-end test checks direct-attached USB keyboard/mouse input, disconnect/reconnect, and live installer plus installed desktop paths. A separate USB 2 hub topology test is configured to exercise child enumeration and hot-plug. Composite runtime currently has descriptor/companion-selection host tests; it has not yet been exercised with a real composite receiver or a faithful QEMU composite device. A successful CI result is still not a substitute for physical testing on different motherboard chipsets, rear and front-panel ports, USB-C ports, powered/unpowered hubs, 2.4 GHz receivers, Bluetooth adapters, and legacy ports.

## Physical validation

Use [INPUT-PHYSICAL-TEST-REPORT.md](INPUT-PHYSICAL-TEST-REPORT.md) to record each motherboard/controller/port/peripheral combination. No physical motherboard, front-panel header, USB-C, receiver, or Bluetooth radio testing has been performed in this session.

## No-universal-compatibility rule

Do not claim that every keyboard, mouse, receiver, hub, motherboard, Bluetooth radio, USB controller, or case-front connector works. Publish support only for tested controller/device combinations with evidence from the complete boot-to-desktop path.


## November 27, 2026 implementation update

- USB 2 and SuperSpeed hub descriptor parsing and class request builders are connected to xHCI hub initialization and port-status polling. The USB 2 parser validates the fixed descriptor fields it consumes without rejecting shortened unused bitmap tails; downstream scanning waits for the reported power-good interval using the timer-driven poll loop.
- xHCI slot contexts include child route-string fields and USB 2 high-speed transaction-translator information for low/full-speed children.
- Child-device connect/disconnect handling tracks occupied non-HID ports to avoid repeatedly resetting attached storage or audio devices.
- Hub status is polled using class GET_STATUS requests (one downstream port per hub per xHCI poll pass); interrupt-IN status change endpoint scheduling is a remaining optimization/robustness task.
- Controller state-loss recovery now retains allocations if halt cannot be confirmed, and otherwise rebuilds controller DMA resources before rescanning.
- Added Bluetooth USB endpoint discovery, HCI command construction/response parsing, L2CAP framing, and HIDP/HOGP report-PDU parsers. These are protocol components only and do not constitute Bluetooth pairing or a working wireless keyboard stack.
- Recent revisions have passed the Nexus installer-runtime build and QA matrix; the original-kernel BIOS/UEFI and distributable-ISO workflows are still being rerun after the latest hub/composite/Bluetooth changes. No physical motherboard or radio testing has been performed in this environment.


## Additional November 27, 2026 increments

- Composite HID runtime now activates one secondary keyboard/mouse interface on the same xHCI slot. Each interface has its own endpoint context/ring and report buffer; completion dispatch uses the slot plus endpoint ID. A failed secondary endpoint is released without tearing down the primary interface.
- The controller logs USB device VID/PID, root-port, route string, and route depth for attached input devices and hubs to support physical compatibility reports.
- The xHCI watchdog monitors controller RUN/HALTED/CNR on its poll interval and requests recovery when controller state is lost. Platform ACPI wake notification wiring remains a separate task.
- Bluetooth HCI host setup serializes Reset, Read Local Version, and Read BD_ADDR commands based on HCI command credits. Added GATT parsers for HID primary service discovery, characteristic discovery, Report Reference descriptors, CCCD lookup, and ATT read/write/notification-enable builders.
- The new CI runs are being used to validate these changes. Do not treat queued or in-progress workflows as passing. No physical motherboard testing has been done.


### Corrected hub speed and PCI resume details

- USB hub speed enum values now match xHCI Slot Context speed IDs (Full=1, Low=2, High=3, SuperSpeed=4, SuperSpeedPlus=5). This prevents low/full-speed downstream devices from being assigned the wrong xHCI context speed.
- xHCI recovery now re-enables PCI Memory Space and Bus Master bits, decodes BAR0 again, re-reads capability/operational offsets, and then verifies halt before freeing DMA pages and rebuilding controller structures. If PCI access or the halt check fails, DMA allocations are retained.
- This does not yet provide a platform ACPI S3 callback or full firmware ownership handoff for every D3/cold-power scenario; those remain release gates.


### Mouse report-protocol preference

- xHCI attempts HID Set_Protocol(Report) and parses the mouse's report descriptor for boot-class mice before choosing the legacy three-byte Boot format. This retains scroll-wheel and extra button fields on supported devices.
- If Report protocol selection or parsing fails, xHCI requests Boot protocol and retains the basic movement/button path. Composite boot mice use the same preference; generic non-boot HID interfaces are parsed without sending the boot-only Set_Protocol request.
- This does not guarantee every proprietary mouse extension or vendor-specific DPI control; those require device-specific descriptors and testing.


### Extended mouse event delivery

- Generic USB HID mouse decoding now records Consumer-page AC Pan as horizontal scrolling and preserves 16-bit vertical and horizontal wheel deltas in the common input event queue.
- The event queue coalesces and saturates both scroll axes without truncating high-resolution wheel values; PS/2 and boot-mouse producers remain compatible with zero horizontal-scroll defaults.
- The desktop maps horizontal wheel movement to horizontal arrow navigation and maps standard HID side buttons 4/5 to browser back/forward when the browser view is active.
- These are protocol and software-path improvements. They do not establish Bluetooth pairing, proprietary vendor extension support, UHCI/OHCI/EHCI operation, or universal physical-device compatibility.


### Bluetooth HID report-to-input integration

- Added per-peer HID Report Map configuration and a common input dispatcher for Classic HIDP input packets and BLE HOGP reports.
- HOGP notifications are matched against discovered GATT Report value handles and the associated Report Reference report ID/type before report decoding. Unknown characteristic handles and non-input Report types are rejected.
- Generic HID keyboard and mouse reports now have a tested handoff to the shared USB/PS/2 input queue; peer disconnect emits neutral key/button state.
- This is not a complete Bluetooth mouse driver. USB HCI interrupt/bulk transfers, ACL reassembly, L2CAP signaling, peer discovery/connection, authenticated SMP/SSP pairing and bonding, key storage, settings UI, and physical Bluetooth adapter tests remain outstanding.


### Bluetooth ACL fragmentation foundation

- Added bounded reassembly of HCI ACL first and continuation fragments into one complete L2CAP PDU, with connection-handle isolation, packet-boundary validation, declared-length enforcement, and a fixed 1 KiB maximum.
- Host tests cover normal split delivery, wrong-handle continuation, overlong fragments, and oversized L2CAP declarations.
- The transport driver must still schedule the USB HCI interrupt/bulk endpoints and feed fragments into this helper; L2CAP signaling/channel setup and authenticated pairing remain necessary for real wireless HID operation.


### Wide HID mouse report support

- Generic mouse descriptor parsing now accepts signed 32-bit X/Y and wheel fields in addition to the common 8- and 16-bit formats.
- Pointer deltas and wheel values saturate to the shared event ABI without wraparound; Y-axis inversion explicitly handles the signed 32-bit minimum.
- Added host coverage for 32-bit X/Y/wheel decoding and the INT32_MIN inversion boundary. This expands standard HID coverage, but still does not implement proprietary report formats or guarantee every physical device.


### Extended generic mouse button capacity

- Expanded generic USB HID mouse button state from 32 to 64 buttons across descriptor parsing, decoded reports, the shared event queue, and desktop button-edge tracking.
- Added descriptor-driven coverage proving Button 64 survives as the top bit of the 64-bit mask.
- Device-specific proprietary button pages and vendor command protocols remain outside generic HID support.


### Classic HID L2CAP channel negotiation

- Added bounded signaling-command parsing and Connection Request/Response codecs for the Classic HID Control PSM (0x0011) and HID Interrupt PSM (0x0013).
- Per-channel state validates transaction identifiers and local/remote dynamic CIDs, records accepted/pending/rejected outcomes, and emits a matched Disconnection Request before releasing state.
- Host tests cover a successful channel lifecycle and malformed/mismatched response rejection.
- The helper is not yet wired to a Bluetooth controller transport, command scheduler, or a live ACL connection; it does not by itself enable Classic Bluetooth mouse pairing or input.


### Composite receiver boundary

- The xHCI runtime currently activates a primary HID interface and at most one companion keyboard-or-mouse interface under the same USB slot. This covers the common two-interface wireless keyboard/mouse receiver layout, not arbitrary composite devices with three or more independently active HID interfaces.
- USB Bluetooth controller devices are not yet bound to xHCI interrupt/bulk transfer rings; endpoint descriptor parsing alone is not operational transport support.


### BLE discovery and connection command preparation

- Added serialized HCI host commands for LE Set Scan Parameters and LE Set Scan Enable, with checked interval/window values and an explicit start/stop state machine.
- Added a bounds-checked parser for legacy LE Advertising Report events, including multiple reports per event, address types, advertising data spans and signed RSSI.
- Added a validated LE Create Connection command builder for explicit peer address and connection interval/latency/supervision values.
- Host tests cover active scan enable/disable, single and multi-advertisement parsing, truncated report rejection, and connection parameter validation.
- These APIs still require a live HCI transport to submit commands and deliver events. Pairing/security, bond management, HID notification subscription scheduling and USB controller integration remain outstanding.
