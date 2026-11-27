# Nexus developer tools and diagnostics

**Status:** bounded kernel-integrated diagnostics, serial log capture, and scheduler trace capture are implemented in the original-kernel branch. The terminal is still an early built-in shell, and the SDK/toolchain remains a preview rather than a complete POSIX development environment.

## 1. Desktop terminal and shell

Open the Terminal view with **F4**. Input is keyboard-driven and bounded to a 63-character command line. Output is a fixed-size desktop buffer; it is not a general VT/PTY terminal.

Built-in commands currently include:

| Command | Behavior |
|---|---|
| `help`, `about` | Show command summary and OS identity |
| `mem`, `uptime`, `diag` | Physical-memory and runtime summary |
| `cpu`, `pci`, `hardware` | CPU and PCI hardware inventory |
| `drivers` | Driver/device bind-state counts |
| `driverlist`, `drivererrors` | Registered driver versions and recent probe/dependency failures |
| `driverrescan` | Retry retained failed or unsupported device records |
| `driverload NAME`, `driverunload NAME` | Load or unload a registered in-tree descriptor, subject to dependency and active-binding checks |
| `net`, `netdiag` | Configured IPv4/gateway details or interface/link inventory |
| `dns HOST` | Bounded IPv4 DNS lookup |
| `http HOST` | Bounded HTTP/1.0 test on TCP port 80; this is not an HTTPS test |
| `storage` | Registered block-device/controller inventory and read-only/mounted flags |
| `logs`, `dmesg` | Recent captured serial/kernel output |
| `trace` | Recent scheduler trace state |
| `debug` | Explain the available debug channels |
| `sdk`, `build` | Show native SDK build-tool entry points |
| `ls`, `cat FILE` | Read the mounted VFS namespace |
| `touch NAME`, `write NAME TEXT` | Create/write a bounded RAMFS file |
| `save NAME` | Copy a RAM file into writable Nexus Data when mounted |
| `rm NAME` | Remove a file only through the VFS write policy |
| `ps`, `run FILE` | Inspect process records or run a supported native ELF in the foreground |
| `clear` | Clear the displayed terminal result |

This is a command dispatcher, not a full shell language. Pipes, redirection, globbing, shell variables, scripts, background jobs, PTYs, terminal escape sequences, and command history are not implemented. `run` is foreground and synchronous; the current process subsystem is not a general-purpose multi-user process environment. There is no POSIX shell or Linux command compatibility claim.

The terminal does not expose destructive disk/partition commands. The existing `rm` command follows the VFS's writable-volume policy; FAT32 remains read-only. Persistent file operations must be backed by a mounted writable Nexus Data volume. RAMFS content is volatile.

## 2. Kernel logs

The serial driver now mirrors output into a bounded **4 KiB rolling log buffer** while continuing to emit bytes through the COM1 serial port. The Terminal `logs` command displays the most recent captured bytes. The API is:

- `serial_log_read_recent(output, capacity)`
- `serial_log_dropped()`

Capture is non-blocking: a contended append is dropped instead of spinning while an interrupt or panic path may be active. The dropped-write counter is exposed so the UI/debugger can report that the visible log may be incomplete. This is a byte ring, not a structured journal: it does not yet assign severity, component IDs, per-record timestamps, persistent storage, or boot-to-boot retention. The serial port remains the primary early-boot diagnostic channel.

## 3. Hardware, driver, network, and storage diagnostics

The diagnostics commands query existing kernel inventories and do not reprogram hardware:

- **CPU:** CPUID vendor, family/model/stepping, core/logical counts, and an explicitly labelled hardware AVX2 capability bit. It does not report a live CPU frequency or temperature.
- **PCI:** enumerated function count and the number retained in the bounded inventory table. `hardware` combines CPU, PCI, and driver-binding summaries.
- **Drivers:** `drivers` shows PnP state counts; `driverlist` shows registered descriptors, versions, load state and bind/failure counters; `drivererrors` shows recent failed/blocked candidates. `driverrescan` retries retained records. `driverload NAME` requires a known descriptor and loaded dependencies. `driverunload NAME` refuses while a driver has active bindings or loaded dependents. These commands manage statically linked in-tree descriptors; they do not install an external binary driver package.
- **Network:** registered interface count, first interface name, medium, and link status. `net` separately reports configured IPv4 and gateway values. `dns HOST` checks IPv4 DNS resolution. The current tools do not yet provide ping, packet capture, Wi-Fi RF survey, throughput testing, or TLS certificate-chain analysis.
- **Storage:** controller/device counts, first device name, read-only capability and mounted state. It does not issue writes, benchmarks, SMART/NVMe health commands, or filesystem repair.

Settings remains the graphical configuration surface. The existing categories include Personalization, Display, Input, Accessibility, System, Devices, Network, Storage, Power, Security, Applications, and About. Persistent desktop preferences use the Nexus Data configuration path; without a writable Data volume, settings that require persistence may remain session-only or unavailable.

## 4. System tracing

A fixed-capacity **128-event trace ring** is available through `trace.h`. The first tracepoints are:

- kernel boot;
- cooperative scheduler context switches (old/new thread slot IDs).

Each retained record contains a monotonic sequence number, timer tick, event type, and two event arguments. Writes are non-blocking; contended events are dropped and counted. `trace` reports retained count and the most recent event.

This is foundational event tracing, not a full function tracer, CPU profiler, performance-counter sampler, packet tracer, or hardware bus analyzer. Additional tracepoints should be added only at well-defined safe subsystem boundaries. The API is kernel-internal and is not a stable userspace ABI.

## 5. Debugging workflow

Early-boot diagnostics are emitted over COM1 serial. For virtual-machine debugging, use QEMU's built-in GDB remote stub; this is emulator functionality, not a Nexus in-kernel debugger.

Example launch pattern (replace the disk/ISO paths with the artifact being tested):

```sh
qemu-system-x86_64 -machine q35 -m 2G \
  -S -s -serial stdio -cdrom NexusOS.iso
```

Then in a separate terminal:

```sh
gdb build/nexus-kernel.elf
(gdb) target remote :1234
(gdb) break kmain
(gdb) continue
```

Useful symbol and image inspection commands:

```sh
readelf -h -lW build/nexus-kernel.elf
objdump -dS build/nexus-kernel.elf > /tmp/nexus-kernel.disassembly
addr2line -e build/nexus-kernel.elf 0xffffffff80000000
```

Use the actual faulting instruction address with `addr2line`; the sample address above is only a syntax example. Preserve the serial log and exact QEMU machine/firmware arguments with every bug report. For physical hardware, use a serial adapter only when the board exposes a compatible UART; USB serial adapters are not equivalent to a kernel-supported USB debug transport.

## 6. Native application development

The current native SDK is declared in `kernel/include/nexus/app.h`. It is a Nexus-owned x86-64 ABI, not POSIX or Win32. It currently exposes process exit, console writes, root-file I/O, keyboard/mouse input polling, gamepad polling, display information, monotonic timer ticks/sleep, bounded framebuffer blits, optional AC'97 playback, and CPU capabilities.

Build samples:

```sh
make -C sdk
make -C sdk inspect
make -C sdk clean
```

The default toolchain prefix is `x86_64-elf-`. On a host that provides the GNU cross tools under the Linux target prefix, override it:

```sh
make -C sdk CROSS_COMPILE=x86_64-linux-gnu-
make -C sdk CROSS_COMPILE=x86_64-linux-gnu- inspect
```

The SDK Makefile produces Kinetic, gamepad, audio, Breakout, and static-PIE demonstrations under `sdk/build/`. `inspect` uses `file`, `readelf`, and `objdump` to show the ELF contract and disassembly. Native binaries must query the ABI version and must not use AVX/AVX2 unless the kernel ABI explicitly reports that the corresponding extended state is enabled.

The earlier `userland/` directory provides an allocation-free C runtime and `libnexus.a` sample using the smaller bootstrap ABI. It is still not a full libc/POSIX implementation. Both SDK tracks are documented separately:

- [Native application SDK](NATIVE-APP-SDK.md)
- [Native SDK and compatibility boundaries](NATIVE-APP-SDK.md#compatibility-boundary)

## 7. Build and test infrastructure

Kernel source is built with the freestanding x86-64 flags in `kernel/Makefile`. The GitHub Actions workflows build/boot-test the original kernel under QEMU UEFI and legacy BIOS, compile the native SDK, and run host-side regression tests.

The developer-tools workflow specifically runs host tests for:
- the 4 KiB serial log ring and overflow/wrap behavior;
- the 128-event trace ring and newest/oldest indexing;
- terminal diagnostics against mocked CPU/PCI/driver/network/storage/memory providers;
- strict compilation of the new kernel integration units;
- native SDK sample builds and ELF inspection.

These host tests validate bounds and formatting with controlled input; they are not a substitute for BIOS/UEFI boot tests or physical-device validation.

## 8. Remaining work

The following are not claimed as complete: a userspace terminal emulator and PTY, fully featured shell/parser, command history and script engine, job control/pipelines, user-facing structured log viewer, persistent crash dump store, structured severity/category logger, trace events for all device and memory paths, function-graph tracing, a kernel-resident interactive debugger, USB debug transport, full network diagnostics, SMART/NVMe health monitoring, filesystem check/repair utilities, and an IDE/editor/debugger toolchain running natively on Nexus.
