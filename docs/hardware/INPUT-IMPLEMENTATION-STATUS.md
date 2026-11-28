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
| 3 | HID report protocol | Generic keyboard/mouse report parsing supports up to eight report IDs per interface. Generic relative mouse X/Y deltas, vertical wheel, and Consumer AC Pan horizontal scroll retain signed 16-bit values; up to 16 standard button usages are retained. xHCI prefers valid HID Report protocol for boot-class mice, with Boot protocol fallback. | Expand report collections, Resolution Multiplier feature handling, output/LED reports, broader descriptor corpus, fuzzing, and hardware validation. Proprietary vendor protocols remain out of scope. |
| 4 | Bluetooth | HCI framing, command-credit-aware Reset/Read Local Version/Read BD_ADDR bring-up state machine, USB Bluetooth interrupt/bulk endpoint discovery, L2CAP framing, HIDP input parsing, HOGP report parsing, and ATT/GATT HID service/characteristic/Report Reference/CCCD discovery parsers and request builders have unit tests. | **Not a functioning Bluetooth input stack yet:** connect USB/UART endpoint transfers to the HCI host state machine, complete command timeouts/ACL reassembly and L2CAP signaling, add authenticated SMP/SSP pairing and secure bonding storage, subscribe to discovered GATT reports, and route decoded Bluetooth HID reports to the common input service. Settings UI and end-to-end radio testing remain. |
| 5 | Older USB host controllers | PCI class/subclass/programming-interface classification identifies UHCI, OHCI, EHCI, and xHCI. xHCI is the only controller driver currently advertised as available; legacy controllers are logged as detected but unsupported. | Implement and test actual UHCI/OHCI/EHCI controller drivers, DMA, interrupts, port state, and USB topology. |
| 6 | Hot-plug/recovery/resume | xHCI root ports and hub-child ports are monitored; child trees detach recursively. The xHCI poll watchdog checks RUN/HALTED/CNR after wake and invokes a full halt/cleanup/DMA-ring+DCBAA+scratchpad rebuild/restart/root-port rescan when controller state is lost. It retains DMA memory if halt cannot be proven. | Wire recovery into real ACPI S3/platform wake notifications; restore firmware ownership and reinitialize after D3/cold power transitions; rebuild and re-enumerate nested hubs; test actual suspend/resume and cold power transitions on physical hardware. |
| 7 | Installer and desktop validation | The QEMU input script verifies USB keyboard make/break, USB mouse movement/clicks, and keyboard/mouse hot-unplug/replug recovery. CI now invokes it for the live installer and the NVMe-installed desktop configuration. | CI execution must pass; add hub/composite/Bluetooth/legacy controller virtual-device coverage where supported; publish physical motherboard, front-panel, USB-C, receiver, and Bluetooth test results. |

## Important implementation details

- Root-port count is not the same as the number of input-device slots. Keyboard, mouse, and gamepad storage remains explicitly bounded.
- USB 2 hub child enumeration is integrated into xHCI using class GET_STATUS polling. SuperSpeed hub descriptors/port status and route handling exist, but full SuperSpeed companion-topology and endpoint-companion coverage still requires validation. Do not claim every hub/USB3 topology works.
- Composite HID now supports a primary endpoint plus one secondary keyboard/mouse endpoint on a single USB slot. It does not yet generalize to arbitrary numbers of endpoints or arbitrary composite collections.
- Multi-report-ID support is for bounded generic keyboard/mouse reports. Generic relative mouse X/Y movement, vertical wheel and Consumer AC Pan horizontal scroll preserve signed 16-bit values end to end. Generic mouse button collections retain up to 16 standard button usages. Unsupported report layouts must fail closed and must not be misclassified as boot-protocol devices.
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


### November 27, 2026 — extended mouse report path

- The common USB mouse event and desktop input queue now retain 16-bit vertical wheel values instead of narrowing parsed HID deltas to signed 8-bit values.
- Standard Consumer Control AC Pan (usage 0x0238) is decoded as an independent horizontal-wheel delta and delivered to the desktop, where it maps to left/right scrolling/navigation at the configured scroll speed.
- Generic HID button collections now retain up to 16 button usages (previously eight) in the common input event. This is a bounded extension, not a claim that arbitrary proprietary gaming-mouse controls work.
- Adjacent mouse reports coalesce vertical and horizontal wheel deltas with signed 16-bit saturation, preserving button-state edge ordering.
- Regression coverage exercises one numbered mouse report with 16 button bits, 16-bit X/Y motion, a 16-bit vertical wheel, and a 16-bit horizontal AC Pan field; the desktop input queue test checks that the widened fields survive delivery.
- Remaining limitations: no Resolution Multiplier Feature-report negotiation, HID++/vendor-specific DPI and button protocols, arbitrary report-collection layouts, or physical mouse validation is claimed.


### November 28, 2026 — UART HCI transport and expanded USB discovery

- Added a polling 16550-compatible HCI H4 UART transport with caller-selected I/O base, UART input clock and baud rate; it configures 8-N-1, FIFO mode and optional CTS-gated transmit.
- UART H4 receive is incremental: fragmented Event and ACL packets remain buffered across polls, declared lengths are bounded, malformed frames are dropped, and a completed packet is retained when the caller supplies a too-small destination.
- The UART transport is now compiled into the kernel and has a host register-model test covering initialization, command/ACL transmit, fragmented event receive, ACL receive, buffer retry and malformed-frame recovery. This is a real generic UART transport module, but it is not yet auto-discovered or wired to the Bluetooth service; USB Bluetooth HCI endpoint scheduling remains unimplemented.
- USB HID configuration discovery now retains up to 16 interrupt-IN HID interfaces (previously eight) and has a ten-interface composite-descriptor regression test. The xHCI runtime still configures only the primary and one companion interface; this parser change does not claim multi-interface runtime completion.
- Added hub interrupt-IN endpoint discovery from the hub interface's active configuration and a validated decoder for the hub/port change bitmap. Unit tests cover both. xHCI still polls GET_STATUS and has not yet scheduled the discovered hub interrupt endpoint, so interrupt-driven runtime notifications remain open.


### Bluetooth ACL/L2CAP receive reassembly

- Added a per-connection, bounded HCI ACL reassembler for L2CAP PDUs fragmented across multiple ACL transfers. It validates start/continuation packet-boundary sequences, handle consistency, broadcast mode, declared L2CAP length, and a 1 KiB maximum PDU size.
- Completed L2CAP PDUs remain available when the first consumer buffer is too small, allowing a safe retry without losing the packet.
- Host tests cover fragmented completion, orphan continuations, connection-handle mismatch, overflow, reserved packet-boundary values, and output-buffer retry.
- This is receive-path protocol infrastructure; Bluetooth scanning, connection initiation, pairing/bonding, ACL scheduling over USB, and end-to-end HID report delivery remain open.
