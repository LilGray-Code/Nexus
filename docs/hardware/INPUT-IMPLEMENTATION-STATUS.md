# Keyboard and Mouse Implementation Status

**Branch:** `nexus-original-kernel-roadmap`  
**Scope:** USB input transport, hubs, composite HID, Bluetooth, legacy host controllers, hot-plug/resume, installer/desktop testing.  
**Status date:** 2026-11-28

This is an implementation ledger, not a hardware compatibility claim. A parser unit test does not prove that a USB device works through the complete controller, installer, and desktop path. Physical hardware results must be recorded separately.

## Ordered implementation

| Priority | Work | Current implementation | Remaining gate |
|---|---|---|---|
| 1 | xHCI root-port discovery | Scans the full HCSPARAMS1 MaxPorts count (1–255), uses a 16-bit loop counter, and bounds-checks PORTSC register offsets. Host-side tests cover offsets and port status. | Physical testing across multiple xHCI vendors. |
| 1 | USB hub downstream ports | xHCI recognizes USB 2 and SuperSpeed hub descriptors, sends hub class requests, powers switched ports, configures hub slots/route strings/TT metadata, and recursively enumerates/detaches children. It discovers and schedules the hub interrupt-IN endpoint, decodes port-change bitmaps, queries only changed ports, and rearms notifications. One initial status sweep discovers already-attached children; unsupported or failing interrupt endpoints fall back to bounded GET_STATUS polling. | Validate interrupt change delivery with QEMU hub topologies and real USB 2/SuperSpeed hubs; SuperSpeed companion topology, transient reset retries and physical multi-tier testing remain. |
| 2 | Composite USB HID | xHCI configures the primary keyboard/mouse interface, a companion interface, and additional boot/generic keyboard/mouse interfaces on the same USB slot. Each additional interface has its own interrupt ring, DMA report buffer, keyboard state, descriptor layout, endpoint-ID dispatch, and per-endpoint recovery. Configuration discovery is bounded to 16 interfaces per USB configuration. | Runtime is bounded to 16 discovered HID interfaces and one interrupt-IN endpoint per interface; unsupported HID classes/vendor reports are skipped. Physical receiver validation and stress tests with many concurrent interfaces remain. |
| 3 | HID report protocol | Generic keyboard/mouse report parsing supports up to eight report IDs per interface. Generic relative mouse X/Y deltas, vertical wheel, and Consumer AC Pan horizontal scroll retain signed 16-bit values; up to 32 standard button usages are retained. xHCI prefers valid HID Report protocol for boot-class mice, with Boot protocol fallback. | Expand report collections, Resolution Multiplier feature handling, output/LED reports, broader descriptor corpus, fuzzing, and hardware validation. Proprietary vendor protocols remain out of scope. |
| 4 | Bluetooth | HCI framing and command-credit bring-up; 16550 UART H4 packet I/O; UART host state machine for Reset/Read Local Version/Read BD_ADDR; classic/LE connection event tracking; ACL transmit; per-connection fragmented ACL-to-L2CAP reassembly; bounded completed-PDU receive queue; USB Bluetooth interrupt/bulk endpoint discovery and HOGP/ATT/GATT/HIDP parsers. | UART must be selected/configured by platform policy and is not auto-probed or started by the desktop service. USB HCI endpoint transfers are not connected to the HCI host; no Inquiry/LE scan, connection initiation, authenticated SMP/SSP pairing, secure bonding storage, GATT report subscriptions, or HID report delivery into the input service is complete. No physical radio tests. |
| 5 | Older USB host controllers | PCI class/subclass/programming-interface classification identifies UHCI, OHCI, EHCI, and xHCI. xHCI is the only controller driver currently advertised as available; legacy controllers are logged as detected but unsupported. | Implement and test actual UHCI/OHCI/EHCI controller drivers, DMA, interrupts, port state, and USB topology. |
| 6 | Hot-plug/recovery/resume | xHCI root ports and hub-child ports are monitored; child trees detach recursively. The xHCI poll watchdog checks RUN/HALTED/CNR after wake and invokes a full halt/cleanup/DMA-ring+DCBAA+scratchpad rebuild/restart/root-port rescan when controller state is lost. It retains DMA memory if halt cannot be proven. | Wire recovery into real ACPI S3/platform wake notifications; restore firmware ownership and reinitialize after D3/cold power transitions; rebuild and re-enumerate nested hubs; test actual suspend/resume and cold power transitions on physical hardware. |
| 7 | Installer and desktop validation | The QEMU input script verifies USB keyboard make/break, USB mouse movement/clicks, and keyboard/mouse hot-unplug/replug recovery. CI now invokes it for the live installer and the NVMe-installed desktop configuration. | CI execution must pass; add hub/composite/Bluetooth/legacy controller virtual-device coverage where supported; publish physical motherboard, front-panel, USB-C, receiver, and Bluetooth test results. |

## Important implementation details

- Root-port count is not the same as the number of input-device slots. Keyboard, mouse, and gamepad storage remains explicitly bounded.
- USB hub interrupt-IN endpoint scheduling and bitmap dispatch are connected to xHCI; one initial GET_STATUS sweep handles devices already attached, and endpoint setup/transfer failures fall back to GET_STATUS polling. Full SuperSpeed companion-topology/endpoint-companion coverage and physical hub validation remain. Do not claim every USB3 topology works.
- Composite HID supports a primary endpoint, one companion and bounded additional boot/generic keyboard/mouse endpoints on the same USB slot, up to the 16-interface descriptor-discovery limit. Each active endpoint has independent report state, DMA buffer, transfer ring and completion dispatch. Unsupported sibling gamepad/vendor HID interfaces are skipped; this is not arbitrary composite-class support.
- Multi-report-ID support is for bounded generic keyboard/mouse reports. Generic relative X/Y movement, vertical wheel and Consumer AC Pan horizontal scroll preserve signed 16-bit values end to end. Generic mouse button collections retain a 32-bit bitmap for up to 32 standard button usages. Unsupported report layouts must fail closed and must not be misclassified as boot-protocol devices.
- HCI framing and link-event decoding do not establish Bluetooth HID support. A controller is not "connected" to the input stack until a complete authenticated HIDP/HOGP path delivers reports.
- EHCI work now includes a host-tested, DMA32-bounded qTD and QH descriptor construction layer, capability/legacy-register validation and root-port status decoding. This is schedule/transfer infrastructure only: there is no EHCI MMIO controller lifecycle, PCI BIOS-ownership handoff, async/periodic schedule activation, control/bulk/interrupt submission, enumeration, or EHCI input path. Keep EHCI marked unavailable until those pieces are implemented and exercised.
- UHCI and OHCI remain PCI recognition only. None of UHCI/OHCI/EHCI is currently advertised as an operational input driver.
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


### xHCI hub interrupt-IN runtime integration

- xHCI now discovers the hub-class interrupt-IN endpoint from the active configuration after reading the hub descriptor, verifies that its packet can hold the full port-change bitmap, configures its endpoint context and schedules an interrupt read.
- Transfer events for hub slots are dispatched separately from HID events. A valid change bitmap causes GET_STATUS requests only for the ports whose change bits are set, followed by the appropriate port-change acknowledgements and child detach/re-enumeration.
- Interrupt-capable hubs receive an initial GET_STATUS sweep so devices already attached at boot are found. After each downstream port has been successfully sampled, routine polling stops and change notifications drive later updates.
- Hubs without a usable interrupt endpoint, or whose interrupt transfer/re-arm fails, return to the existing GET_STATUS polling path. DMA ring allocation is released on setup/scheduling failure.
- QEMU and physical SuperSpeed multi-hub topology coverage is still needed; endpoint scheduling and notification handling need the fresh CI and hardware matrix before being described as complete.


### Composite HID runtime expansion

- xHCI now retains a bounded array of extra keyboard/mouse HID interfaces beyond the existing primary and companion endpoints. The configuration parser allows up to 16 discovered HID interfaces, yielding up to 16 active keyboard/mouse input interfaces on one USB slot when their reports are supported.
- Each additional interface has a separate interrupt endpoint ring and DMA report buffer. Keyboard report history is per interface, so key releases from one physical interface do not overwrite another interface's state. Endpoint transfer completions are dispatched by slot and endpoint ID.
- Boot keyboards/mice use their boot protocol when needed; report protocol is preferred for mice and generic report protocol is used for non-boot HID keyboards/mice. Unsupported gamepad, vendor-specific and non-keyboard/mouse sibling interfaces are skipped without disabling the primary interface.
- Endpoint context-entry depth is preserved as endpoints are configured, preventing a later lower-numbered interface endpoint from reducing the xHCI Slot Context's maximum endpoint ID.
- Automated compile/boot and descriptor-discovery results are required before release. A physical receiver exposing three or more simultaneous input interfaces has not been tested in this environment.


### UART HCI host service integration

- Added a UART HCI host service above the 16550 H4 transport. It runs the serialized Reset, Read Local Version and Read BD_ADDR bring-up sequence, records the controller's local version/manufacturer/address, tracks classic and LE connection/disconnection events, transmits ACL fragments, and delivers bounded completed L2CAP PDUs from a per-connection reassembler through a small receive queue.
- The service refuses to retry an H4 command after a partial UART transmit failure because doing so could desynchronize the serial packet stream; it marks the host failed and requires transport reinitialization.
- The host service and a mocked-register end-to-end test are included in the kernel object list and CI. Platform-selected UART base/clock/baud configuration, automatic controller discovery, USB HCI transport scheduling, Inquiry/LE scanning, connection initiation, authenticated pairing/bonding, HIDP/HOGP channel setup, and delivery into the common input queue remain release blockers.


### Extended standard HID mouse buttons

- Generic mouse report layouts, the common mouse event, and desktop input queue now preserve a 32-bit standard button bitmap (32 independent Button-page usages). The upper 16 buttons are decoded and delivered without narrowing.
- The numbered advanced mouse regression descriptor now exercises buttons 1, 3, and 32 together with 16-bit X/Y, vertical wheel, and horizontal AC Pan. Input queue coverage verifies that button 32 survives the report-to-desktop event path.
- This remains standard HID usage support; proprietary gaming-mouse extensions and feature-report programming are not claimed.


### November 28, 2026 integration status

- Added a generic polling 16550 HCI H4 UART transport and a UART HCI host service that drives Reset/Read Local Version/Read BD_ADDR, tracks completed classic/LE links, transmits ACL frames, reassembles incoming ACL/L2CAP PDUs per connection, and exposes a bounded PDU receive queue. The platform must explicitly supply UART base/clock/baud; automatic UART discovery and service startup are not implemented.
- USB Bluetooth controller endpoint discovery remains parser-only: xHCI does not yet claim/configure a USB Bluetooth class interface or schedule its interrupt/bulk HCI streams. Pairing/bonding, device inquiry/LE scan, connection initiation, GATT report notifications and HIDP/HOGP delivery into the desktop input service remain incomplete.
- xHCI hub interrupt-IN notification handling is connected to the transfer-event path, with an initial scan for attached children and polling fallback.
- xHCI composite runtime supports bounded additional standard keyboard/mouse interfaces under one USB slot, up to 16 discovered HID interfaces, each with independent endpoint/report state. Unsupported sibling HID classes and proprietary reports are skipped.
- Mouse report parsing and the shared input path preserve a 32-button standard HID bitmap, 16-bit X/Y movement and vertical/horizontal scroll. Feature-report based Resolution Multiplier support and proprietary gaming extensions remain open.
- The latest 2026-11-28 installer-runtime, QA and platform-foundation workflows have passed on preceding code revisions. The current original-kernel/ISO workflows are rerunning after the latest UART host, composite endpoint and 32-button changes; only completed workflow results should be treated as passed.
- Physical validation is still outstanding: no motherboard, USB-C port, 2.4 GHz receiver or Bluetooth radio was attached to this build environment.


### EHCI transfer-schedule foundation — November 28, 2026

- Added EHCI capability-register validation for CAPLENGTH, interface version, HCSPARAMS port count/power capability and HCCPARAMS flags, including bounds checking for the EHCI extended-capability pointer.
- Added root-port PORTSC decoding for connect/enable/suspend/over-current/reset/power, line state and write-one-to-clear change bits.
- Added ABI-sized/aligned EHCI QH and qTD definitions plus bounded builders. qTD buffer construction validates DMA32 page bases, first-page offsets, the five-page coverage limit, 15-bit transfer byte count, descriptor alignment, PID, data toggle, IOC and terminated/link pointers. Zero-byte status-stage qTDs need no data page list.
- Added a control-transfer chain builder that links SETUP, optional fragmented DATA qTDs, and STATUS, derives DATA PID from bmRequestType, advances DATA toggles by packet count, and routes short IN packets directly to STATUS. Host tests cover this sequence, including a five-page first qTD and toggle transition.
- Added host tests for malformed capability and legacy pointer values, root-port status mapping, multi-page qTD buffer pointers, invalid alignment/oversize descriptors, zero-byte status qTDs, and QH endpoint fields.
- This is the first EHCI schedule/transfer primitive, not an operational EHCI host-controller driver. PCI memory mapping, BIOS/OS legacy ownership, controller halt/reset/run, schedule DMA allocation/activation, asynchronous/periodic execution, transfer completion, root-port reset, USB enumeration, and HID delivery remain open. OHCI and UHCI drivers also remain unimplemented.
