# Nexus Physical Keyboard/Mouse Hardware Test Report

This form is for real hardware validation. Do not complete it using QEMU or a VM. One report should describe one motherboard/controller + connection path + peripheral combination.

## System under test

- Date/time (UTC):
- Tester:
- Motherboard manufacturer/model/revision:
- Firmware vendor/version/date:
- Firmware settings relevant to USB/xHCI/legacy support:
- CPU/platform:
- Nexus commit SHA:
- ISO SHA-256:
- Boot mode (UEFI/legacy BIOS):
- Installer or installed desktop:
- Kernel serial log attachment:

## USB controller and topology

- PCI segment:bus:device.function:
- PCI vendor/device ID:
- PCI class/subclass/programming interface:
- Controller driver (xHCI/EHCI/OHCI/UHCI):
- BAR0 base:
- Root port:
- Root port negotiated speed:
- Hub manufacturer/model/VID/PID (if any):
- Hub port number:
- Hub negotiated speed (if any):
- xHCI route string:
- Route depth:
- Front-panel or rear I/O:
- Motherboard USB header used (if front-panel):
- USB connector type (USB-A/USB-C):
- External hub powered/unpowered:

## Keyboard or mouse

- Peripheral type (keyboard/mouse/combo receiver):
- Manufacturer/model:
- VID/PID:
- Wired USB / 2.4 GHz receiver / Bluetooth / PS/2:
- Report protocol (boot/report/unknown):
- Interface number:
- Endpoint address:
- Report descriptor SHA-256 or attachment:
- Keyboard layout / mouse buttons and wheel features:

## Test stages

Mark each as PASS, FAIL, or NOT TESTED. Include log timestamps and observed behavior.

| Stage | Result | Evidence / notes |
|---|---|---|
| Firmware setup/boot menu input | NOT TESTED | |
| Nexus boot menu | NOT TESTED | |
| First installer screen | NOT TESTED | |
| Installer navigation and text entry | NOT TESTED | |
| Installer-to-desktop reboot/transition | NOT TESTED | |
| Login screen | NOT TESTED | |
| Desktop pointer and keyboard | NOT TESTED | |
| Multiple simultaneous input devices | NOT TESTED | |
| Unplug / reconnect | NOT TESTED | |
| USB hub child-port reconnect | NOT TESTED | |
| Suspend / resume | NOT TESTED | |
| Cold power cycle | NOT TESTED | |

## Input behavior checks

- Keyboard make/break, modifiers, rollover:
- Layout and function keys:
- Mouse X/Y motion:
- Left/right/middle buttons:
- Wheel and horizontal wheel:
- Extra buttons:
- Multi-report-ID handling:
- Composite keyboard+mouse receiver, both endpoints simultaneously:
- Hot-unplug releases all held keys/buttons:
- No kernel lockup, panic, DMA fault, or stuck input:

## Bluetooth-only fields

- Radio manufacturer/model/VID/PID:
- Integrated or USB adapter:
- HCI transport (USB/UART):
- Bluetooth controller version/manufacturer:
- Pairing method and security level:
- Bond persistence after reboot:
- HID service discovered:
- Keyboard/mouse reports received:
- Disconnect/reconnect:
- Pairing and key material must not be recorded in this report.

## Result

- Overall result: NOT TESTED
- Known limitations:
- Reproduction steps for any failure:
- Serial log:
- Relevant dmesg/PCI inventory:
- Attachments (photos of cabling/ports, report descriptors, controller logs):
- Tester sign-off:

## Required coverage before advertising support

1. Test rear motherboard USB 2 and USB 3 ports.
2. Test front-panel USB 2 and USB 3 ports through the correct motherboard headers.
3. Test USB-C data ports.
4. Test at least one external powered hub and one unpowered hub.
5. Test a 2.4 GHz combo receiver with both keyboard and mouse endpoints active at once.
6. Test Bluetooth on the integrated radio and one USB adapter where available.
7. Test across at least two xHCI vendors before describing a controller family as supported.
8. Test EHCI/OHCI/UHCI only after the matching host controller driver exists.
9. Repeat installer and installed-desktop tests, then repeat after suspend/resume and a cold power cycle.

A report with any required stage marked NOT TESTED is not evidence for a full support claim. Never infer that a case front port works merely because a rear motherboard port works; confirm header wiring and test the actual front-panel socket.
