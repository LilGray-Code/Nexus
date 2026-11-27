# Nexus Input Hardware Compatibility and Implementation Plan

**Scope:** keyboard and pointing-device support in the live environment, installer, login screen, and installed desktop.
**Status:** architecture and acceptance plan; this document does not claim that unimplemented controller or Bluetooth drivers are operational.

## Product requirement

A keyboard and mouse must remain usable from the first interactive boot screen through installation, first boot, login, and the desktop. The installer and desktop must consume the same kernel input-device service and event ABI. A device that works in the live environment must not disappear merely because the installer or desktop starts.

Support is capability-based, not based on whether a device is marketed as wired or wireless. The transport and controller determine the driver path.

## Connection types and expected paths

| Physical connection | Required path | Notes |
|---|---|---|
| Rear motherboard USB-A / USB-C | PCI USB host controller -> USB hub/topology -> HID interface | USB 2, USB 3.x and USB-C data ports |
| Case front-panel USB-A / USB-C | Same USB host-controller path through motherboard header wiring | Case ports are not a separate input protocol; firmware, header wiring, and controller routing matter |
| External powered/unpowered USB hub | USB hub enumeration -> downstream ports -> HID | Multi-tier hubs, hot-plug, reset/recovery, and power budgeting |
| 2.4 GHz keyboard/mouse receiver | USB HID boot or report protocol | Treat receiver as a USB device; many combo receivers expose multiple HID interfaces |
| Bluetooth keyboard/mouse | Bluetooth controller HCI transport -> host stack -> HID over GATT (BLE/HOGP) and HID over Classic (HIDP) | USB Bluetooth dongles and motherboard-integrated radios; pairing is user-authorized |
| PS/2 keyboard/mouse | i8042 controller -> PS/2 driver | Legacy motherboard ports and internal laptop keyboard/trackpad paths where exposed |
| PCI/PCIe USB expansion card | PCI enumeration -> matching host-controller driver -> USB topology -> HID | Controller support depends on PCI ID, programming interface, DMA and interrupt implementation |
| Internal motherboard USB headers | Same USB host-controller path as external ports | Includes case front-panel headers and internal USB devices |
| Integrated laptop pointing devices | Enumerated bus/controller -> device-specific driver -> common input ABI | I2C-HID, SMBus/HID-over-I2C and vendor devices require their own discovery/transport drivers |

## Driver architecture

### 1. Bus and controller layer

- Enumerate all PCI USB host controllers and match class/subclass/programming-interface IDs.
- Keep xHCI as the primary modern USB controller target.
- Add EHCI and UHCI/OHCI controller support for older systems where practical; do not assume every motherboard routes all ports through xHCI.
- Implement controller ownership handoff, reset, DMA mapping, interrupt delivery, port power, link-state handling, and bounded timeout/recovery.
- Discover root ports and recursively enumerate external and internal hubs, including multi-tier hubs and hot-plug events.
- Do not hard-code a single controller, root port, port speed, or device address.
- Support USB 2 high/full/low speed and USB 3 SuperSpeed link negotiation where the controller and port permit it.

### 2. USB device and HID layer

- Parse device, configuration, interface, endpoint, and HID report descriptors with strict bounds checks.
- Enumerate every interface of composite devices independently. A single receiver may expose keyboard, mouse, consumer-control, and vendor interfaces.
- Support boot protocol as a compatibility fallback, not as the only accepted protocol.
- Support HID report protocol with multiple report IDs and multiple input collections in one interface.
- Handle interrupt-IN polling/transfer completion, NAK, stalls, short packets, disconnects, endpoint halt recovery, and re-enumeration.
- Provide keyboard rollover handling, modifier state, key release on disconnect, mouse buttons, relative motion, wheel and high-resolution wheel where described.
- Keep per-device state; never share one keyboard's previous report state with another device.
- Apply descriptor, report-size, endpoint, and queue limits to prevent malformed devices from exhausting kernel memory or CPU time.

### 3. Bluetooth layer

Bluetooth must be a real transport and protocol stack, not a label applied to 2.4 GHz USB receivers.

- Add HCI core with command/event sequencing, controller reset, version/features discovery, buffer/flow-control handling, connection lifecycle, and disconnect recovery.
- Add USB HCI transport for common USB Bluetooth adapters; add UART HCI for supported motherboard-integrated combo radios after serial transport support exists.
- Add Bluetooth Classic HIDP and Bluetooth Low Energy HID over GATT (HOGP), including GATT service/characteristic discovery, report map parsing, notifications, protocol mode, and suspend/resume reconnection.
- Implement pairing and bonding with explicit user approval, secure key storage, privacy-preserving device names, and removal/revocation controls.
- Do not silently pair, accept unauthenticated input, or expose pairing secrets in logs.
- Make Bluetooth pairing available only after the live UI has a secure, usable pairing workflow; retain USB/PS/2 fallback during installation.

### 4. Common input service

- Publish normalized keyboard, pointer, wheel, device-added, device-removed, and capability events.
- Maintain independent per-device state and stable runtime device IDs.
- Use bounded queues with overflow counters and a defined recovery policy; prioritize key/button release events so a dropped movement report cannot leave a key or button logically stuck.
- Handle hot-plug, reconnect, sleep/resume, controller reset, and device removal.
- Expose device metadata and capabilities to the settings UI without granting applications raw controller access.
- Keep installer and desktop clients on the same ABI and event service.

## Firmware and motherboard compatibility

- Respect firmware USB legacy support and OS ownership handoff; do not disable firmware/SMI behavior unless the controller handoff state is understood and validated.
- Preserve a working firmware keyboard path until the OS controller has completed initialization and accepted input.
- Detect i8042 presence rather than assuming a PS/2 controller exists on every modern board.
- Probe PCI IDs and ACPI-described devices; avoid assumptions based on motherboard brand or port color.
- A case front USB port works only when correctly connected to a motherboard header and enabled by firmware. Nexus should diagnose an absent/unrouted port rather than claim it is a software keyboard failure.
- Provide serial and boot-log diagnostics when no input device is detected.

## Installer and desktop behavior

- Initialize the shared input service before rendering the first interactive installer page.
- Do not switch input backends when changing from live shell to installer, login manager, or desktop.
- Show a clear keyboard/mouse-not-detected warning with port/controller troubleshooting guidance, while preserving any functioning input path.
- Support multiple keyboards and mice simultaneously.
- Keep basic wired USB and PS/2 operation independent of Bluetooth pairing services.
- Provide settings for pointer speed, acceleration, natural scrolling, primary button, repeat delay/rate, keyboard layout, accessibility keys, and Bluetooth pairing when implemented.
- Do not display a Bluetooth device as connected until HCI link and HID service readiness are confirmed.

## Acceptance tests

### Automated/QEMU

1. Boot with USB keyboard and mouse attached; verify input before installer interaction.
2. Attach devices behind one- and multi-tier USB 2/3 hubs; verify enumeration and events.
3. Test a composite 2.4 GHz receiver exposing keyboard and mouse interfaces simultaneously.
4. Test boot protocol and report protocol HID devices, multiple report IDs, and multiple devices of the same class.
5. Inject malformed descriptors, oversized reports, transfer stalls, NAKs, disconnects, and reconnects; kernel must remain stable.
6. Verify independent key/button release state for each device and queue-overflow recovery.
7. Verify installer-to-desktop transition does not lose input or leave stuck keys/buttons.
8. Test PS/2 keyboard and mouse paths independently of USB.
9. Test xHCI ownership handoff and controller reset paths under both UEFI and legacy BIOS QEMU configurations.

### Physical hardware matrix

Test at minimum:
- rear motherboard USB 2 and USB 3 ports;
- case front-panel USB 2 and USB 3 ports connected to motherboard headers;
- USB-C data port where available;
- direct wired keyboard/mouse;
- 2.4 GHz receiver with keyboard and mouse;
- external USB hub, including a powered hub;
- Bluetooth USB adapter and integrated Bluetooth radio;
- PS/2 keyboard/mouse where available;
- at least two xHCI controller vendors and one older non-xHCI controller family.

Record motherboard model, firmware version/settings, PCI vendor/device IDs, controller driver, port topology, device VID/PID, negotiated speed, test stage, and pass/fail evidence.

## Release gates

A feature is not "supported" merely because its descriptor parses or a QEMU test passes. A transport is marked supported only after:
- controller enumeration and lifecycle tests pass;
- input events reach the common service;
- installer and desktop both consume those events;
- disconnect/reconnect and sleep/resume tests pass;
- physical hardware evidence exists for the advertised controller/device class.

Until then, retain the known-good wired/PS/2 fallback and describe the feature as experimental or not yet implemented. Never claim universal compatibility with every keyboard, mouse, motherboard, Bluetooth chipset, or proprietary receiver.


## Implementation progress

### Phase 1 — xHCI root-port discovery (first increment)

- The xHCI driver now reads the controller-advertised eight-bit root-port count rather than truncating discovery to 32 ports.
- Root-port scanning uses a 16-bit loop counter, preventing wraparound when the controller advertises 255 ports.
- Root-port register offsets are computed through a bounds-checked helper; status-change handling uses the same helper.
- The number of tracked HID devices remains independently bounded at 32 per input class.
- Host-side tests cover root-port counts, valid/invalid port numbers, register offsets through port 255, connection state, speed extraction, and PORTSC change bits.

**Not yet complete:** external USB hub child-port enumeration is still unsupported by the controller driver. Hub descriptor parsing, hub class requests, child-device route strings, and downstream hot-plug are required before USB hubs can be advertised as supported. Phases 2–7 have not been implemented by this increment.
