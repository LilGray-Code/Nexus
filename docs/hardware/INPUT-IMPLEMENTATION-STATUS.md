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
| 4 | Bluetooth | USB xHCI HCI transport; passive LE scanning and selected-peer connection initiation; controller event-mask setup; HOGP ATT discovery/report delivery; Secure Connections P-256 public-key and DHKey HCI operations; f4/f5/f6 transcript verification; derived LTK + LE Start Encryption/Encryption Change gate; explicit same-exchange legacy fallback only when peer declines SC. | Secure Connections uses NoInputNoOutput (Just Works), so it provides no MITM authentication and no bond persistence. MITM/OOB/passkey/numeric-comparison requests are rejected; RPA resolution, bonding/key storage, runtime Classic Inquiry/HIDP integration is in progress; persistent Secure Simple Pairing bonds, MITM/passkey/OOB, and physical radio validation remain incomplete. UART Bluetooth remains platform-configured. HOGP descriptor/class support is bounded. |
| 5 | Older USB host controllers | PCI identification for UHCI/OHCI/EHCI/xHCI; xHCI remains the only advertised input driver. EHCI now performs BIOS ownership handoff, halt/reset, DMA32 async schedule startup, polled root-port connection monitoring and bounded high-speed EP0 SETUP/DATA/STATUS qTD transfers with IAA-safe unlink. | EHCI root-port reset/address/configuration enumeration, hub/companion TT routing, periodic interrupt scheduling, and HID keyboard/mouse delivery are still absent; UHCI/OHCI remain PCI recognition only. EHCI must not be treated as a working mouse/keyboard driver yet. |
| 6 | Hot-plug/recovery/resume | xHCI root ports and hub-child ports are monitored; child trees detach recursively. The xHCI poll watchdog checks RUN/HALTED/CNR after wake and invokes a full halt/cleanup/DMA-ring+DCBAA+scratchpad rebuild/restart/root-port rescan when controller state is lost. It retains DMA memory if halt cannot be proven. | Wire recovery into real ACPI S3/platform wake notifications; restore firmware ownership and reinitialize after D3/cold power transitions; rebuild and re-enumerate nested hubs; test actual suspend/resume and cold power transitions on physical hardware. |
| 7 | Installer and desktop validation | The QEMU input script verifies USB keyboard make/break, USB mouse movement/clicks, and keyboard/mouse hot-unplug/replug recovery. CI now invokes it for the live installer and the NVMe-installed desktop configuration. | CI execution must pass; add hub/composite/Bluetooth/legacy controller virtual-device coverage where supported; publish physical motherboard, front-panel, USB-C, receiver, and Bluetooth test results. |

## Important implementation details

- Root-port count is not the same as the number of input-device slots. Keyboard, mouse, and gamepad storage remains explicitly bounded.
- USB hub interrupt-IN endpoint scheduling and bitmap dispatch are connected to xHCI; one initial GET_STATUS sweep handles devices already attached, and endpoint setup/transfer failures fall back to GET_STATUS polling. Full SuperSpeed companion-topology/endpoint-companion coverage and physical hub validation remain. Do not claim every USB3 topology works.
- Composite HID supports a primary endpoint, one companion and bounded additional boot/generic keyboard/mouse endpoints on the same USB slot, up to the 16-interface descriptor-discovery limit. Each active endpoint has independent report state, DMA buffer, transfer ring and completion dispatch. Unsupported sibling gamepad/vendor HID interfaces are skipped; this is not arbitrary composite-class support.
- Multi-report-ID support is for bounded generic keyboard/mouse reports. Generic relative X/Y movement, vertical wheel and Consumer AC Pan horizontal scroll preserve signed 16-bit values end to end. Generic mouse button collections retain a 32-bit bitmap for up to 32 standard button usages. Unsupported report layouts must fail closed and must not be misclassified as boot-protocol devices.
- HCI framing and link-event decoding do not establish Bluetooth HID support. A controller is not "connected" to the input stack until a complete authenticated HIDP/HOGP path delivers reports.
- EHCI work now includes a host-tested, DMA32-bounded qTD and QH descriptor construction layer, capability/legacy-register validation and root-port status decoding. This is schedule/transfer infrastructure only: there is no EHCI MMIO controller lifecycle, PCI BIOS-ownership handoff, async/periodic schedule activation, control/bulk/interrupt submission, enumeration, or EHCI input path. Keep EHCI marked unavailable until those pieces are implemented and exercised.
- EHCI has a controller-core bring-up path (PCI legacy ownership, halt/reset, DMA32 asynchronous reclamation head, RUN/ASE verification and polled root-port connection detection). It now also supports bounded high-speed endpoint-zero control transfers using chained SETUP/DATA/STATUS qTDs and an IAA-verified asynchronous unlink. Bulk/periodic interrupt scheduling, root-port reset/address/configuration enumeration, companion routing and HID event delivery remain unfinished, so EHCI is not yet advertised as an operational input driver.
- UHCI and OHCI remain PCI recognition only.
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
- USB Bluetooth HCI transport is now wired into xHCI: class interfaces are configured with interrupt event-IN and bulk ACL-IN/OUT endpoints, controller initialization commands are sent over the class control pipe, and HCI events/ACL packets reach the HCI and L2CAP state. Pairing/bonding, device inquiry/LE scan, connection initiation, GATT report notifications and HIDP/HOGP delivery into the desktop input service remain incomplete.
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


### USB Bluetooth class-interface descriptor hardening

- The Bluetooth HCI USB interface parser now validates the interface's declared endpoint count against the descriptors actually present and only accepts the default alternate setting for the supported HCI transport.
- Endpoint zero and reserved endpoint-address/attribute bits are rejected. The interrupt-IN event pipe must have a nonzero interval, a supported packet size and no unsupported high-bandwidth multiplier. Bulk pipes must have valid USB 2 packet sizes and no high-bandwidth multiplier.
- Duplicate endpoint addresses, missing event/bulk-in/bulk-out pipes, malformed descriptor lengths (including malformed trailing descriptors after a valid interface), and truncated configurations fail closed. Tests cover these negative cases and verify that unrelated interfaces can precede the Bluetooth class interface.
- The subsequent xHCI integration now configures the interrupt event-IN pipe and bulk ACL-IN/OUT pipes, starts HCI Reset/Version/BD_ADDR over the USB class control pipe, dispatches events and ACL packets into the HCI/L2CAP state, and exposes L2CAP send/read APIs. This initial runtime path has not yet been validated against a physical adapter; it does not implement scanning, pairing, bonding or HID report subscriptions.


### xHCI endpoint context and deferred completion foundation — November 28, 2026

- Extracted non-control endpoint-context construction into a tested helper. It maps USB endpoint addresses to xHCI DCI/endpoint types for Bulk IN/OUT and Interrupt IN/OUT, validates reserved address bits, interval, packet/burst size and transfer-ring alignment, and never shrinks the Slot Context's existing context-entry count.
- The existing interrupt endpoint path (HID and hub) now uses the shared builder. Added a generic normal-TRB scheduler with explicit short-packet reporting for IN transfers and no ISP request for OUT transfers.
- Added a bounded 63-entry deferred xHCI event FIFO. Synchronous command/EP0 transfer waiters now retain unrelated port and transfer events instead of dropping them; the ordinary xHCI poll loop drains retained events before newer hardware events.
- Host tests cover endpoint DCI/type/context construction, IN/OUT direction, context-entry preservation, malformed endpoint addresses and ring addresses, plus FIFO ordering/wrap/full/empty behavior.
- These were the endpoint/deferred-event foundations subsequently used by the USB Bluetooth HCI transport integration below. Authenticated pairing/scanning and HID report delivery remain incomplete.


### USB Bluetooth xHCI HCI transport integration — November 28, 2026

- xHCI now recognizes a validated Bluetooth Wireless Controller class interface during USB configuration enumeration and configures its interrupt-IN event endpoint, bulk-IN ACL endpoint, and bulk-OUT ACL endpoint under the device's existing xHCI slot.
- The initial HCI Reset, Read Local Version and Read BD_ADDR commands are submitted through the USB Bluetooth class control pipe. The control-OUT data stage now copies the command bytes into DMA-visible memory; control-IN transfers alone copy received bytes back to the caller.
- Interrupt event-IN and bulk ACL-IN transfers are independently scheduled and rearmed. HCI events are passed through the raw USB event parser, connection/disconnection events update bounded per-adapter link state, and ACL packets are parsed from the USB bulk frame and reassembled into per-link L2CAP PDUs.
- Added xhci_bluetooth_read_l2cap and xhci_bluetooth_send_l2cap as an upper-layer handoff. The receive API preserves a PDU if the caller supplies an undersized buffer; the send API validates the L2CAP length and allows one in-flight ACL bulk-OUT transfer per adapter.
- Adapter endpoint rings/buffers are released on a confirmed slot disable or after the controller is halted. When slot disable fails, the implementation retains DMA state rather than freeing pages that hardware may still access. The standard xHCI deferred-event FIFO prevents synchronous endpoint-zero commands from discarding unrelated Bluetooth, HID, or hub completion events.
- Host tests now cover USB HCI event framing, raw USB ACL parsing/building, packet-boundary bits, endpoint validation, and xHCI bulk/interrupt endpoint contexts. These tests do not substitute for a physical USB Bluetooth adapter test.
- Still not implemented: Inquiry/LE scan, outgoing Bluetooth connection initiation, SMP/SSP authentication, secure bonding/key storage, L2CAP signalling/channel negotiation, HIDP/HOGP report subscriptions, and delivery of Bluetooth HID reports to the common keyboard/mouse event queue. Bluetooth is not yet a fully working wireless HID stack.


### Bluetooth LE discovery and connection initiation — November 28, 2026

- The HCI layer now parses bounded legacy LE Advertising Report events (up to 25 reports per event), copies peer address/type, AD data and RSSI into owned records, and validates the full event payload before exposing any report.
- Added parameter builders for passive LE scanning, duplicate filtering, and LE Create Connection. Scan interval/window, address types, connection interval/latency and supervision timeout are validated before building commands.
- The xHCI Bluetooth runtime automatically configures and enables passive LE scanning after controller Reset/Version/BD_ADDR initialization. Advertising reports are placed in a bounded 15-entry adapter queue and are exposed through xhci_bluetooth_read_advertisement.
- An upper layer can initiate a connection to a selected address/type through xhci_bluetooth_connect_le. The adapter disables scanning, sends LE Create Connection through the USB HCI control pipe, processes Command Status and LE Connection Complete, then resumes scanning after success/failure.
- Generic HCI command submission now remains usable after initial controller bring-up and tracks the matching Command Complete/Command Status opcode, status and command credits.
- This does not implement authenticated SMP pairing, bonding/key persistence, L2CAP signalling, HID service discovery/subscriptions or keyboard/mouse input report delivery. Only legacy advertisements are parsed; LE Extended Advertising Reports are not yet supported. No physical Bluetooth radio has been tested.


### BLE HID GATT discovery support — November 28, 2026

- Extended HID characteristic inventory to retain declaration/value handles, properties and 16-bit UUIDs for all characteristics in the bounded service range, not only Report characteristics.
- Added ATT Read Blob and Find Information request builders and validated continuation-handle extraction for Read By Group Type and Read By Type responses.
- These are reusable ATT/GATT discovery primitives, not yet an end-to-end HOGP client. The runtime still needs to negotiate ATT MTU, paginate discovery across responses, discover per-report descriptors, read and parse the HID Report Map, enable the input-report CCCDs, and deliver report notifications into the shared input subsystem.


### BLE HOGP ATT client state machine — November 28, 2026

- Added a bounded, asynchronous ATT client sequence: MTU exchange; paginated primary HID service and characteristic discovery; Report Map read plus Read Blob paging; per-Report-characteristic Find Information; Report Reference reads; and CCCD writes for input notifications/indications.
- The state machine matches each response to the outstanding ATT opcode, handles the expected Attribute Not Found pagination terminators and Invalid Offset end-of-blob response, clamps the negotiated MTU, and bounds the Report Map to 512 bytes and report characteristics to the existing eight-entry service limit.
- Added host tests for the complete discovery/CCCD subscription sequence, Report Map blob paging, response ordering and malformed/unmatched ATT replies.
- This is still a protocol client primitive. The xHCI runtime has not yet started it on connected LE links or mapped resulting HOGP report notifications into the desktop input queue.


### HOGP-to-desktop input integration — November 28, 2026

- Each successful LE connection now starts its own HOGP ATT client. xHCI serializes MTU exchange, HID service/characteristic discovery, Report Map reads, per-report descriptor lookup, Report Reference reads and CCCD subscription over that connection's ATT fixed L2CAP channel.
- ATT Handle Value Notifications and Indications are matched to enabled input Report characteristics. Indications queue the required ATT Handle Value Confirmation.
- The HOGP client parses the downloaded HID Report Map with the existing generic HID descriptor parser and decodes report-ID-associated input payloads. Supported keyboard reports are passed through per-connection keyboard state; mouse reports preserve decoded buttons, motion and wheel values.
- Decoded Bluetooth keyboard/mouse activity is sent to the same input_usb_keyboard_report_state and input_usb_mouse_event producers used by wired HID, so it reaches the shared desktop input queue. On link disconnect, held keyboard keys and mouse buttons are released.
- Host tests now verify the ATT subscription sequence and sample keyboard/mouse notification decoding against Report Maps. These are protocol/parser tests; no physical LE keyboard/mouse pairing or over-the-air test has been performed.
- Known limitations remain: the only implemented pairing fallback is non-bonding legacy Just Works, with no MITM authentication or persistent keys. LE Secure Connections and passkey/OOB/Numeric Comparison are not implemented. The current generic parser accepts only one HID class (keyboard or mouse) per Report Map with a bounded maximum of eight report IDs. Vendor-specific/consumer-control usages and unsupported report maps are not exposed as working devices.


### BLE legacy Security Manager pairing foundation — November 28, 2026

- Exposed a reusable AES-128 block-encryption primitive and verified it with the FIPS-197 AES-128 known-answer vector.
- Added BLE legacy Security Manager c1 and s1 functions and tested them against the Bluetooth Core 6.3 examples.
- Added a bounded initiator state machine for non-bonding legacy Just Works pairing: Pairing Request/Response, Pairing Confirm/Random exchange, constant-time peer-confirm verification, STK derivation, and an explicit transition requiring HCI link encryption.
- Pairing features deliberately request no MITM, no OOB, no bonding/key distribution, and require a 16-byte key. The state machine rejects peers that require MITM, request OOB/bonding distribution, or negotiate a smaller encryption key.
- This pairing mode provides no authentication and no protection against eavesdropping or MITM during the pairing exchange. It is a compatibility fallback, not secure authenticated pairing. LE Secure Connections (P-256, f4/f5/f6/g2), passkey/numeric comparison, bonding/key persistence, RPA resolution and physical radio validation remain unimplemented.


### Runtime BLE legacy Just Works encryption fallback — November 28, 2026

- xHCI now accepts SMP fixed-channel (CID 0x0006) traffic for connected LE links. It starts a non-bonding legacy Pairing Request only after a peripheral sends SMP Security Request or an ATT response indicates insufficient authentication/authorization/encryption.
- The local pairing features request NoInputNoOutput, no OOB, no MITM, no bonding and no key distribution. Peer requests for MITM, OOB, bonding/key distribution, invalid key parameters or a key size other than 16 bytes are refused with Pairing Failed rather than silently downgraded.
- The initiator validates the peer's Pairing Confirm using c1, derives the STK with s1, submits HCI LE Start Encryption (zero RAND/EDIV, session STK), and waits for a successful HCI Encryption Change event. Only then is HOGP discovery restarted and input reports enabled.
- If random generation, pairing, HCI command submission or link encryption fails, HOGP input is disabled and held keyboard/mouse state is released. No STK is retained after encryption confirmation, and no bond is persisted; reconnections repeat pairing if the peer requires security.
- This is strictly an unauthenticated compatibility fallback. It does not provide MITM protection, LE Secure Connections, passkey/Numeric Comparison/OOB pairing, persistent LTK/IRK bonding, or resolvable private address handling. Securely authenticated BLE HID is still unsupported. This runtime path has host protocol/crypto tests but no physical over-the-air validation.


### SMP deadline and fail-closed handling — November 28, 2026

- Added a 15-second deadline for legacy SMP feature/confirm/random exchange and a separate 5-second deadline while waiting for the HCI link-encryption procedure to complete.
- On timeout, malformed or unsupported SMP traffic, rejected HCI encryption commands, or unsuccessful Encryption Change events, the HOGP session is disabled and any held keyboard/mouse state from that link is released. Pairing failure is reported where the transport remains available.
- Added host tests for reserved AuthReq bits, unsupported/bonding requests, unknown SMP commands and clearing all temporary TK/STK/random/confirm material after a failed encryption result.


### AES-CMAC foundation for LE Secure Connections — November 28, 2026

- Added an RFC 4493 AES-128-CMAC primitive on top of the FIPS-197 AES block implementation. It supports empty, partial-block and complete-block messages with a bounded 1 KiB input.
- Added all four RFC 4493 known-answer vectors (empty, 16-byte, 40-byte and 64-byte messages) plus invalid pointer and length checks.
- This AES-CMAC primitive is used by the LE Secure Connections transcript described below. Its standalone vectors validate the primitive; the complete runtime path still requires controller interoperability and physical radio testing.


### LE Secure Connections crypto and HCI controller primitives — November 28, 2026

- Added the HCI command builders for global HCI Event Mask, LE Event Mask, LE Read Local P-256 Public Key and LE Generate DHKey (v1). The runtime LE mask enables subevents 0, 1, 7, 8, 9 and 10 (including Enhanced Connection Complete); the global mask enables command completion/status, disconnect, encryption change and LE Meta events.
- Added strict parsers for the LE Read Local P-256 Public Key Complete event (0x3E/0x08) and LE Generate DHKey Complete event (0x3E/0x09). The DHKey parser rejects all-0xFF output, which the HCI specification defines as the invalid-remote-public-key result.
- Added Core 6.3 f4, f5, and f6 on the RFC 4493 AES-CMAC primitive. The implementation includes the 128-bit CMAC salt, btle key ID, 256-bit key-length field, central/peripheral nonce ordering and seven-octet address values.
- Added HCI command/event tests and Core 6.3 sample-vector tests for f4/f5/f6. The P-256/DHKey commands, event masks and encrypted HOGP gating are now connected to the xHCI runtime in the later runtime integration section; physical controller/radio validation remains outstanding.


### BLE Secure Connections SMP transcript state machine — November 28, 2026

- Added a bounded Secure Connections initiator transcript for NoInputNoOutput Just Works without bonding: Pairing Request/Response, Central-first Public Key PDU, peer-key handoff for controller DHKey generation, f4 confirm/random validation, f5 MacKey/LTK derivation, and bilateral f6 DHKey Check validation.
- The state machine emits explicit actions when it needs HCI local P-256 key generation, HCI DHKey generation, or HCI Start Encryption. It only returns an LTK after the peer DHKey Check validates; it zeroes DHKey/MacKey/LTK/nonces/public-key material after confirmed encryption or a failed exchange.
- A peer that explicitly responds without the SC AuthReq bit is identified for the legacy pairing path, as required when the peer does not support Secure Connections. MITM, OOB, bonding/key distribution and invalid key-size requirements are rejected.
- Host tests cover a complete deterministic SC transcript, local public-key transmission, the generated-DHKey handoff, both confirm/check values, LTK gating, legacy peer fallback and MITM/invalid-key refusal.
- The state machine is now connected to xHCI's controller P-256/DHKey procedures and serialized adapter-wide crypto operation state. Runtime integration details and current security limitations are recorded in the later runtime section.


### Runtime LE Secure Connections integration — November 28, 2026

- xHCI configures the global HCI event mask for connection/disconnection, Encryption Change, Command Complete/Status and LE Meta. The LE event mask enables LE connection completion, legacy advertising reports, LE Read Local P-256 Public Key Complete and LE Generate DHKey Complete.
- On an LE SMP Security Request or an ATT security-permission error, the adapter requests the controller's local P-256 public key, sends the Secure Connections Pairing Request and Central Public Key, and submits the peer public key to the controller's LE Generate DHKey command.
- The resulting DHKey and a fresh 128-bit nonce from the kernel RNG feed the tested SMP Secure Connections transcript. The driver validates f4 peer confirms and f6 DHKey checks, derives the f5 LTK/MacKey and starts LE link encryption using RAND=0/EDIV=0. HOGP ATT discovery is resumed only after a successful Encryption Change event.
- P-256 and DHKey controller procedures are serialized across links because their LE Meta completion events do not identify a connection handle. Controller command rejection, invalid public-key/DHKey results, RNG failure, SMP failure and pairing/encryption timeout all fail closed and release held keyboard/mouse state.
- If the peer's Pairing Response explicitly omits Secure Connections, the driver continues the already exchanged Pairing Request/Response with the legacy c1/s1 Just Works confirm path; it does not start a second Pairing Request. HCI/crypto errors never trigger legacy fallback.
- This pairing policy is NoInputNoOutput, no MITM, no bonding, no OOB, and no key distribution. MITM-required peers are rejected because passkey/OOB/Numeric Comparison UI does not exist. No persistent LTK/IRK store or RPA resolver is present. No physical Bluetooth adapter has been tested.


### EHCI endpoint-zero transfer engine — November 28, 2026

- Added a serialized synchronous high-speed endpoint-zero transfer API over the active asynchronous schedule. Each transfer uses one below-4-GiB DMA page containing a QH, up to four qTDs, the 8-byte SETUP packet and a bounded data buffer (maximum 3072 bytes).
- SETUP/DATA/STATUS descriptor construction reuses the tested chain builder, selects IN/OUT DATA PID from bmRequestType, applies packet-toggle progression and routes short IN data to the STATUS qTD.
- The QH is inserted after the reclamation head, and the driver polls the STATUS and DATA qTD tokens. It reports actual DATA bytes from the DATA qTD residual count (not the zero-length STATUS qTD).
- The temporary QH is unlinked through the EHCI Async Advance Doorbell and is freed only after the matching IAA status is observed. If IAA does not arrive, the DMA page is quarantined and further control transfers are refused.
- This primitive is high-speed only and is not yet called by device enumeration. Root-port reset/speed handoff, SET_ADDRESS/configuration discovery, full/low-speed transaction translators, periodic interrupt schedule, hub child enumeration and HID report delivery remain outstanding. No EHCI mouse/keyboard has yet been exercised in QEMU or on physical hardware.


### Bluetooth OS discovery controls — November 28, 2026

- Exposed a bounded xHCI adapter status snapshot with adapter readiness, active scan count, connected LE links, encrypted links, HOGP-ready links and aggregate dropped-packet telemetry.
- Added an OS scan-resume control that requests passive scanning only when a USB HCI adapter is initialized and is not in the middle of a connection attempt.
- Added bounded parsing of complete/short local-name AD structures, with printable output, truncation handling and malformed-field rejection.
- The desktop Network Settings view now displays Bluetooth adapter/scan/link/HOGP state, a rolling list of discovered LE devices with advertised name/address/RSSI, and a Connect action for each visible device.
- The UI also discloses that this build only offers NoInputNoOutput Just Works security, has no MITM/passkey/OOB flow or saved bonds, and does not yet support classic HIDP input.
- This is a discovery/device-selection surface, not a persistent device manager, saved-bond list or pairing-approval workflow.


### Classic HID protocol test/build integration — November 28, 2026

- The Classic HCI inquiry parser and Inquiry/Create Connection builders are now included in the host test matrix.
- Added the standalone Classic HID session implementation to the kernel object list and CI. The test exercises separate Control/Interrupt PSM connections, both directions of L2CAP configuration, HIDP Set Protocol boot-mode handshake, keyboard and mouse input decoding, malformed signaling and retry after failed transmit.
- This commit provides the protocol component and build validation; runtime HCI Inquiry UI/connection orchestration and xHCI report dispatch are the next integration step.
