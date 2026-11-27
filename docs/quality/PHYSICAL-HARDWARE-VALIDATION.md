# Nexus physical-hardware validation record

This form is a test artifact, not a declaration of support. Create one copy per
machine and test session. Do not mark an operating-system feature complete
because the kernel compiles, a virtual machine boots, or a device appears in
inventory.

## Run identity

- Date/time (UTC):
- Tester:
- Nexus branch and commit:
- ISO filename and SHA-256:
- Test report ID:
- Test environment: bare metal / passthrough VM / other
- Result: NOT RUN / PASS / FAIL / BLOCKED
- Full serial log and test artifacts:

## Platform inventory

- Manufacturer / motherboard or system model:
- BIOS/UEFI vendor and version:
- Secure Boot state:
- CPU model, family/model/stepping, microcode:
- Installed RAM and configuration:
- GPU(s), PCI vendor/device/subsystem IDs, revision:
- Display model(s), EDID, connection and refresh rates:
- Audio controller/codec PCI IDs and output device:
- Ethernet controller PCI IDs and link partner:
- Wi-Fi controller PCI IDs and firmware version:
- USB host controllers and representative HID/storage devices:
- Storage controller(s), drive model(s), firmware and transport:
- ACPI/SMBIOS versions and notable firmware warnings:

## Required test record

For each item, write PASS, FAIL, BLOCKED or NOT RUN, include reproducible
steps, expected versus actual result, and attach logs/screenshots where useful.

| Area | Test | Result | Evidence / failure details |
|---|---|---|---|
| Boot | Cold boot from installer ISO in UEFI mode | NOT RUN | |
| Boot | Cold boot from installer ISO in legacy BIOS mode, when supported | NOT RUN | |
| Boot | Reboot installed system from internal storage | NOT RUN | |
| CPU | Correct vendor, topology and online CPU count | NOT RUN | |
| Memory | Allocator stress and sustained memory pressure; validate accounting | NOT RUN | |
| Graphics | Desktop rendering, resize, clipping and repeated redraw | NOT RUN | |
| GPU | Confirm actual 2D/3D engine operations; distinguish firmware framebuffer fallback | NOT RUN | |
| Display | EDID, native mode, alternate resolution/refresh, rotation and recovery | NOT RUN | |
| Display | Multi-monitor attach/detach and primary-display recovery, if applicable | NOT RUN | |
| Audio | Play a known test signal and verify physical output/loopback | NOT RUN | |
| Audio | Stress playback; check underruns, hangs and recovery | NOT RUN | |
| Ethernet | DHCP, DNS, HTTPS and sustained transfer | NOT RUN | |
| Wi-Fi | Scan, associate, WPA2 handshake, data transfer and reconnect | NOT RUN | |
| USB | Keyboard/mouse/gamepad, hubs and repeated detach/reattach | NOT RUN | |
| USB storage | Read, authorized write, flush, remove and reattach | NOT RUN | |
| Storage | Identify, read, error handling and filesystem remount | NOT RUN | |
| Storage | Safe write/flush and reboot persistence on disposable test media | NOT RUN | |
| Installer | Select the intended target, install, reboot and verify data | NOT RUN | |
| Installer recovery | Interrupt at documented safe checkpoints on disposable media; verify journal recovery | NOT RUN | |
| Security | Verify package rejection on tampering and unauthorized installation | NOT RUN | |
| Performance | Record cold boot, app start, memory pressure, input/audio latency and network throughput | NOT RUN | |
| Thermal/power | Sustained load, temperature/fan reporting and safe shutdown/restart | NOT RUN | |

## Failure and recovery record

| Injected failure | Preconditions / exact action | Expected safe behavior | Actual result / recovery evidence |
|---|---|---|---|
| Corrupted filesystem or package metadata | Use disposable image only | Reject corrupt data without publishing partial state | |
| Interrupted installer write | Use disposable target only | Journal replay or safe rollback; no false success | |
| Device initialization timeout | Record exact device and boot log | Report failure, continue safely when optional | |
| Display mode failure | Use recoverable display mode | Restore prior mode or report recovery failure without corrupting state | |
| USB/storage disconnect | Use disposable USB device | Stop unsafe I/O; report device loss; do not reuse stale DMA buffers | |
| Network loss during transfer | Interrupt link deliberately | Timeout/abort without exposing partial trusted package | |
| Repeated reboot after failure | Use the same disposable setup | Stable reboot, no stale state or filesystem damage | |

## Evidence and disposition

- Serial log filename/hash:
- QEMU/VM logs (reference only; do not substitute for physical results):
- Screenshots/video:
- PCI/USB IDs observed:
- Issues filed:
- Known limitations:
- Retest date and result:
- Reviewer:
- Final disposition: UNSUPPORTED / PARTIAL / PHYSICALLY VALIDATED FOR THIS CONFIGURATION

A PASS applies only to the exact hardware, firmware, Nexus commit and test
conditions recorded above. It must not be generalized to every device sharing
a marketing name or chipset family.
