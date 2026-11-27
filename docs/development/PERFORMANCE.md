# Nexus performance optimization and measurement

## Current engineering status

Nexus is an x86-64 original-kernel prototype, not yet a production OS with mature power management, multi-process scheduling, a hardware-accelerated compositor, page-cache, NVMe queue scaling, or low-latency audio mixing. Optimization work must follow the real implementation boundary. An advertised device capability or a driver probe is never counted as working acceleration.

## Completed measured optimization: kernel log ring

The 4 KiB serial log ring previously performed ring-index modulo arithmetic for every copied byte, both when appending and when reading. It also cleared every ring byte during initialization even though an empty logical length makes the old contents unreachable.

The optimized implementation:

- Tracks the next write index and number of retained bytes separately.
- Copies at most two contiguous spans when a write or read crosses the ring boundary.
- Uses x86-64 `rep movsb` for the bounded contiguous copies.
- Resets only the ring metadata during initialization; it does not clear 4 KiB of data.
- Retains the non-blocking lock behavior. On contention, the append is dropped and the dropped-write counter is incremented; it never spins in an interrupt/panic-sensitive path.
- Preserves the same external API and bounded recent-log behavior.

Correctness tests cover empty reads, appended ordering, full-ring overwrite, a write that crosses the physical ring boundary, small output buffers, and reinitialization without exposing stale bytes. The log buffer also compiles with the kernel's freestanding x86-64 flags (including `-mno-sse -mgeneral-regs-only`).

### Host microbenchmark results

The benchmark compares the exact previous per-byte-indexing algorithm with the optimized production API in one process, using identical payloads, buffer sizes, compiler options, and loop counts. Values below are the median of seven runs on the same host.

Host: AMD EPYC 9V74 (80-Core), x86-64, Linux 6.18.57, GCC 14.2.0. Build flags: `-std=c11 -O2 -Wall -Wextra -Werror -pedantic`.

| Operation | Previous implementation | Optimized implementation | Median speedup |
|---|---:|---:|---:|
| Append one 43-byte log payload | 15.9 ns/op | 8.5 ns/op | 1.91× |
| Read up to 111 payload bytes into a 112-byte output buffer | 48.7 ns/op | 8.3 ns/op | 5.91× |
| Read full 4 KiB ring | 1,655.8 ns/op | 58.0 ns/op | 29.52× |

The measured ranges across those seven runs were:

| Operation | Previous range | Optimized range |
|---|---:|---:|
| Append | 15.4–20.2 ns/op | 8.0–10.0 ns/op |
| Read up to 111 bytes (112-byte buffer) | 47.7–56.4 ns/op | 8.0–9.1 ns/op |
| Read 4 KiB | 1,593.3–3,159.6 ns/op | 51.5–85.9 ns/op |

These are host microbenchmark results for the log ring only. They are not boot-time, whole-kernel, physical-PC, storage-throughput, or application-startup measurements. Absolute timings vary with CPU, power state, virtualization, compiler, and system load. The dedicated GitHub Actions workflow also runs the benchmark and prints its measurements; CI timing is informational and does not fail on a noisy speedup threshold.

## Optimization policy by subsystem

| Area | Current constraint | Safe optimization / measurement plan |
|---|---|---|
| Boot | Serial diagnostics and broad PCI enumeration contribute to initialization work; no validated end-to-end boot-time baseline yet | Add phase timestamps and compare cold UEFI/BIOS boots; reduce work only after device-discovery coverage is proven |
| Memory | Bootstrap page allocator is a bounded region allocator with a shared ticket lock; it is not a general heap or virtual-memory manager | Measure allocation latency/contention and fragmentation; preserve accounting, DMA limits, IRQ safety, and ownership checks |
| Multicore | SMP bring-up exists, but user processes are not generally scheduled across CPUs | Measure per-core kernel worker utilization and contention; do not report multicore application scaling until user scheduling and CPU affinity exist |
| Graphics/GPU | Software rasterization with framebuffer/VirtIO presentation; VirGL command submissions are not proof of accelerated desktop rendering | Benchmark raster and present paths separately; verify rendered output and device-backed execution before enabling acceleration claims |
| Filesystem | NXFS/FAT32 and VFS support exist, but there is no general page cache/write-back cache | Add explicit cache-coherency and crash tests before measuring hit rate/throughput; never cache stale metadata across writes |
| Input | IRQ/USB HID event queues are bounded rings with overflow accounting | Measure interrupt-to-desktop-consumption latency and queue drops under load before changing synchronization |
| Application startup | Current app runner is foreground/synchronous; static ELF and constrained PIE support are not a full dynamic runtime | Measure parse, file-read, relocation, and first-frame phases independently; cache only immutable verified data |
| Audio | Optional AC'97 output is a bounded single-buffer path | Measure DMA completion and underruns before adding a multi-buffer ring; keep format/range validation |
| Networking | DNS cache exists; TCP uses a bounded synchronous stop-and-wait sender, not a tuned congestion-controlled stack | Benchmark DNS cache hit/miss and TCP throughput/latency separately; do not weaken retry/timeout or packet validation |
| Storage | NVMe/AHCI paths exist, with explicit write authorization and limited request/DMA buffers | Measure sequential/random I/O on disposable images and supported hardware; retain flush, bounds, and boot/Data partition gates |
| Background work | The service manager and polling network path are small; there is no general low-power service scheduler | Add per-service wakeup/CPU-use counters before introducing periodic work or background daemons |

## Required benchmark protocol

For each future optimization, preserve a reproducible before/after report containing:

1. Exact commit IDs, source configuration, compiler/toolchain versions, and build flags.
2. Hardware model, firmware mode, RAM, storage controller/device, display adapter, and VM settings where applicable.
3. At least 7 repeated runs for microbenchmarks and 20 cold boots for boot measurements; report median and p95, not the best run.
4. The exact workload, payload sizes, cache-warm/cold state, iteration count, and error/overflow counters.
5. Correctness checks and relevant regression tests run on both builds.
6. A rollback commit or switch to disable the optimization if fault rates, dropped events, corruption, or tail latency regress.

Do not compare a QEMU software renderer against a physical GPU and call the result a hardware acceleration improvement. Do not mix cache-warm and cache-cold storage runs. Do not infer low-latency input/audio from aggregate CPU utilization.

## Stability and security guardrails

- Keep kernel/user pointer validation, memory ownership, bounds checks, and executable W^X policy intact.
- Keep filesystem writes behind the current Nexus Data-volume policy; no benchmark may write to a user's system or recovery partition.
- Keep cryptographic package and update verification independent of performance shortcuts.
- Never drop keyboard/button transitions silently to achieve a better latency number; report queue overflow and test recovery.
- Do not enable optional CPU vector features unless the kernel saves/restores all required extended state on every supported context switch.
- Do not label VirGL, a reported PCI GPU, or an EDID HDR capability as accelerated rendering/HDR output without verified end-to-end execution.
- Retain conservative defaults until measurements on multiple Intel and AMD physical machines and both BIOS/UEFI paths show the optimization is safe.

## Not yet measured end-to-end

No before/after whole-OS result is currently claimed for boot time, application startup, multicore utilization, GPU frame time, filesystem throughput/cache hit rate, interrupt-to-input latency, audio underruns/round-trip latency, network throughput, or NVMe/AHCI throughput. Those require repeatable QEMU and physical-hardware measurement jobs plus workload-specific correctness criteria.
