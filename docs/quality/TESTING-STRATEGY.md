# Nexus testing and quality assurance

**Policy: no feature is complete because it compiles.** Completion requires
repeatable behavioral tests for the claimed operation, negative/failure
conditions, recovery evidence, and validation at the level of hardware the
feature claims to support.

## QA structure

The repository has many focused host-test workflows. This guide and the
machine-readable [subsystem test matrix](test-matrix.csv) join them into one
auditable coverage map. The matrix validator checks that every required
subsystem is represented, that its test/evidence files and owning workflows
exist, and that negative and recovery scenarios are recorded.

The top-level CI audit is
[.github/workflows/nexus-quality-assurance.yml](../../.github/workflows/nexus-quality-assurance.yml).
It validates the matrix and runs a strict-warning/sanitizer smoke group on every
branch push and pull request. Existing focused workflows continue to own the
full subsystem test suites; the QEMU BIOS/UEFI and distributable ISO workflows
own integration and boot testing.

## Evidence levels

| Evidence | What it proves | What it does not prove |
|---|---|---|
| **Build-only** | Sources compile/link for the selected flags and target | Runtime correctness, device operation, recovery or compatibility |
| **Host-tested** | Unit/protocol/state behavior passes deterministic tests or mocks | Real hardware, DMA correctness, timing, firmware compatibility |
| **QEMU-tested** | The recorded kernel/ISO boots or operates with the specific QEMU machine, firmware and virtual devices | Physical device support or equivalent behavior in another hypervisor |
| **Physical-tested** | A dated report reproduces the test on the exact recorded hardware and firmware | Universal support for every board/revision sharing the same marketing name |

The labels host-automated, partial-automated, qemu-automated, and
manual-required describe current test coverage. They deliberately do not use
a generic complete status.

## Current automated suites

The workflows are split by subsystem so failures identify the relevant gate:

| Subsystem | Main automated evidence | Important failure/recovery cases |
|---|---|---|
| Kernel core and services | Driver manager, service manager, bounded logs and trace rings | Dependency cycles, unsupported hardware, failed probes, ring wraparound and service ordering |
| Memory | Concurrent allocator stress and allocator invariant self-test | Double free rejection, allocation failure accounting, free-list validation and reuse |
| CPU | CPUID vendor/topology and hybrid-core decoding | Malformed AMD/Hygon topology and capability fallback |
| Filesystems | FAT32, NXFS, VFS and boot-pack tests | Corrupt checksums, cyclic chains, truncated metadata, invalid bounds and failed navigation |
| Graphics/GPU | Framebuffer, blit, adapter inventory, VirtIO queue and Virgl protocol tests | Invalid surfaces, clipping, malformed command streams and safe fallback |
| Display | EDID, mode enumeration, topology and mode rollback | Failed set/verify/restore and invalid topology |
| Audio | AC'97 DMA buffer/descriptor format | Invalid alignment, zero/oversized transfer and DMA address bounds |
| Network/Wi-Fi | netdev/TCP/HTTP/TLS, 802.11, WPA2 and hardware-ID parser tests | Malformed packets/IEs, retransmission, failed key derivation, invalid IDs and certificate rejection |
| USB | HID keyboard/mouse/gamepad and MSC BOT/SCSI tests | Invalid descriptors, queue overflow, short transfers, bad CSW and failed BOT reset recovery |
| Storage/installer | Block-device identity/write authorization, GPT and installer journals | Duplicate identity, out-of-range I/O, corrupted metadata, interruption and transactional recovery |
| Applications | Native ABI/ELF and Windows PE/INF/NDIS package assessment | Malformed executables, unsupported imports and ABI drift |
| Security | Package, digest, AES-CCM, WPA2 and TLS tests | Tamper detection, malformed lengths, invalid signatures/certificates and unsafe policy |
| Performance | Log-ring benchmark and allocator contention stress | Correctness/invariant checks across repeated samples; no machine-dependent timing threshold |

This summary does not claim every driver, filesystem operation, GPU engine or
application format is fully supported. See the compatibility and architecture
documents for feature limitations.

## Failure injection and recovery requirements

Every subsystem suite should exercise, where applicable:

1. **Invalid input:** malformed IDs, headers, lengths, descriptors, offsets,
   checksums, bounds and unsupported feature combinations.
2. **Resource exhaustion:** full tables, no free pages, allocation failure,
   queue overflow, buffer limits and exhausted handles.
3. **Partial operations:** short reads/writes/transfers, timeouts, device
   disappearance, failed DMA/command completion and interrupted installs.
4. **Corruption:** corrupted filesystem chains, package payloads, signatures,
   firmware tables, EDID records and network protocol fields.
5. **Recovery:** rollback to prior state, retry, reinitialization, journal replay,
   safe fallback, or an explicit fail-closed state. A test that only checks
   that an error is returned is insufficient when the subsystem is expected
   to recover.

For persistent writes and installation tests, use memory-backed mocks or
disposable virtual disks. Never inject power loss or destructive I/O into a
user's real disk.

## VM and boot validation

The current automated integration targets include:

- **QEMU q35 + OVMF:** UEFI boot, installer, installed target, xHCI input,
  storage, networking, persistent files, and desktop HTTPS flows. The ISO
  release workflow injects USB keyboard press/release and USB mouse movement/
  click events through QMP in both installer and installed-desktop sessions,
  and verifies the resulting HID reports and shared input-dispatch events.
  Host tests cover generic HID keyboard/mouse reports, report IDs, PS/2
  status-routed polling fallback, USB 2.0/SuperSpeed/SuperSpeedPlus link IDs,
  EP0 packet sizes, xHCI extended-capability traversal, BIOS/OS ownership
  semaphores and legacy SMI masking.
- **QEMU pc + SeaBIOS:** legacy BIOS boot and installed-target checks.
- **QEMU virtual devices:** NVMe, SATA/AHCI, USB/xHCI, RTL8139, E1000,
  VirtIO network and VirtIO GPU where enabled by the workflow.

The run configuration and serial assertions are in
[nexus-original-kernel.yml](../../.github/workflows/nexus-original-kernel.yml)
and [nexus-distributable-iso.yml](../../.github/workflows/nexus-distributable-iso.yml).

VirtualBox and VMware are not currently automated or certified by this branch.
A future hypervisor is a separate test target; passing QEMU does not imply
compatibility with it.

## Physical hardware validation

No automated CI runner can prove that physical HDMI/DP output, audio codecs,
Wi-Fi RF, USB hotplug, ACPI power states, DMA behavior or storage durability
works on every real machine. A bare-metal report must identify the exact
motherboard/system, BIOS/UEFI build, CPU, RAM, PCI/USB hardware IDs, firmware,
display/audio/network devices, storage devices and Nexus commit.

Use the [physical hardware validation report](PHYSICAL-HARDWARE-VALIDATION.md)
for a reproducible run. The record must include failures and recovery results,
not only successful boot output. A feature remains partial or unvalidated until
the report supports the claimed device family and operations.

## Performance measurement

Microbenchmarks must record compiler, flags, host CPU, OS, sample count,
median/range and a correctness checksum. CI performance tests should detect
functional regressions without imposing flaky absolute timing thresholds on
shared runners. End-to-end boot, app launch, memory pressure, input/audio
latency, storage throughput and network throughput baselines remain separate
work items; a log-ring microbenchmark is not an OS-wide performance result.

## Completion gate

A feature may be labelled **validated for a specific configuration** only when:

- its automated tests pass for the supported configuration;
- negative and recovery tests cover relevant failure boundaries;
- the relevant QEMU integration checks pass, where applicable;
- physical validation is recorded when the claim concerns real hardware;
- logs, commit, test environment and known limitations are retained; and
- unsupported devices and untested paths are labelled honestly.

A failing test blocks the associated claim until the failure is fixed or the
scope is narrowed explicitly. Skipping a test is not a pass.
