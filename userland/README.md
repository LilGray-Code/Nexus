# Nexus native application SDK (early ABI)

This directory contains Nexus's early freestanding C SDK for native x86-64
applications. It targets the ABI-major-version-1 `int 0x80` syscall interface,
the private user address-space loader and the small `libnexus.a` runtime.
This is a Nexus-specific ABI, not Linux or POSIX.

## Build

From the repository root:

```sh
make -C userland
make -C userland inspect
make -C userland test
```

The build produces `build/userland/libnexus.a` and
`build/userland/HELLO.ELF`. The sample is compiled from C and a minimal
startup routine, then statically links only the runtime functions it uses. CI
inserts this exact compiler-built executable into the disposable FAT32
boot-test image.

## Syscall ABI

| Wrapper | Syscall | Behavior |
|---|---:|---|
| `nexus_exit(status)` | 1 | Exit the current native process |
| `nexus_write(fd, buffer, length)` | 2 | Write to descriptor 1 (serial-backed console) |
| `nexus_open(path)` | 3 | Open a simple read-only file name |
| `nexus_read(fd, buffer, length)` | 4 | Read a bounded chunk into the process |
| `nexus_close(fd)` | 5 | Close a descriptor |
| `nexus_file_write(path, buffer, length)` | 6 | Write through the current VFS policy |
| `nexus_abi_version()` | 7 | Query the kernel syscall ABI version |
| `nexus_get_permissions()` | 16 | Query the optional capabilities granted to this app |

Arguments use RDI/RSI/RDX, the syscall number and result use RAX, and errors
are returned as negative errno values. The ABI major version is currently 1;
native programs should query `nexus_abi_version()` before using other wrappers.
The permission query is additive in ABI v1 and reports optional capabilities
approved for the running package. The
kernel validates user-memory ranges before copying. File names are simple
volume-local names; the early interface does not support path separators or
directory descriptors.

## Reusable runtime

`libnexus.a` currently provides allocation-free memory/string helpers,
`nexus_write_all()` for retrying valid short writes,
`nexus_read_file()` for joining partial reads into a caller-owned buffer,
`nexus_write_file()` for the VFS-backed file-write syscall, `nexus_puts()`
for console output, and `nexus_printf()` for bounded formatted output, and a 64 KiB bootstrap heap with\n`nexus_malloc()`, `nexus_free()`, `nexus_calloc()`, and `nexus_realloc()`.

`nexus_read_file()` requires an explicit buffer capacity. It returns
`-NEXUS_EFBIG` if the file does not fit, closes the descriptor on success and
failure paths, and reports the byte count only after a successful close.
`nexus_write_all()` caps each syscall-sized chunk and returns `-NEXUS_EIO`
if a write reports zero progress.

The host test build defines `NEXUS_USERLAND_HOST_TEST` to inject a deterministic
syscall simulator. Production builds do not use this shim and invoke the
real kernel ABI. Tests cover partial reads/writes, EOF, capacity overflow,
missing files, close failures, memory/string operations, formatted output, invalid format rejection, and
file-write/readback.

## Sample application and limitations

The `HELLO.ELF` sample checks ABI version 1, reads and verifies
`README.TXT`, writes `HELLO.OUT` through the VFS, reopens it and verifies the
written bytes, prints the expected success line using `nexus_printf()` (including zero-padded numeric output), and exits with status 7. The
compiled ELF is stored in a bounded FAT32 chain (up to eight 512-byte clusters).
At runtime, `HELLO.OUT` is persistent on a writable NXFS data volume and
volatile when only the RAM overlay is available; read-only FAT32 is never
written.

This is an early runtime, not libc or POSIX. The loader accepts x86-64 static
ET_EXEC files and a constrained static PIE ET_DYN form with relative
relocations only. It rejects PT_INTERP, external DT_NEEDED libraries and ELF
TLS; there is no general dynamic linker or shared-library loader. The process
image input limit is 4 MiB and process names are limited to 15 characters.

Programs run synchronously in the foreground; the process table is bounded
lifecycle bookkeeping, not a general concurrent application scheduler. The
SDK exposes limited console, file, input, display/framebuffer, audio, gamepad
and CPU APIs, but no full windowing toolkit, libc/POSIX environment,
directory-descriptor API, process-spawn service, or application heap.

## Bounded formatted output

`nexus_printf()` accepts `%%`, `%c`, `%s`, `%d`/`%i`, `%u`, `%x`/`%X`, and
`%p`. Numeric conversions may use a width up to 32 and optional zero padding
(e.g. `%04u`). Output is assembled in a fixed 512-byte buffer and then sent
through `nexus_write_all()`, so valid short writes are retried. Invalid
specifiers/widths return `-NEXUS_EINVAL`; output overflow returns
`-NEXUS_EFBIG`. This is a small freestanding formatter, not full ISO C
`printf`/`stdio` compatibility.


## CPU capabilities

The native ABI provides `nexus_query_cpu_capabilities()` so an application
can select code paths based on features the running kernel has actually
enabled. AVX and AVX2 are reported only when the kernel has configured and
preserves XSAVE x87/SSE/YMM state; hardware CPUID support alone is not enough.
AVX-512 is not exposed until its opmask/ZMM state is implemented and saved.


The freestanding runtime selects AVX2 bulk copy/fill routines for requests of
256 bytes or larger when the kernel's CPU-capability syscall confirms AVX2
and active OS-managed YMM state. Older CPUs and failed capability queries use
the scalar fallback. Applications can query the same feature record directly.


## Permissions and package policy

A signed `.NPK` manifest can request a `PERMISSIONS=` capability mask. The
request is not automatic consent: a separate local `.NPA` record stores the
approved subset, missing approval means no optional capabilities, and package
updates revoke old approvals. The kernel checks file read/write, input,
display, audio and gamepad capabilities at syscall boundaries.
`nexus_get_permissions()` returns the granted mask.

The package backend can record and apply explicit approvals, but a complete
graphical permission prompt and a tamper-resistant protected settings store
are not yet available. Signature verification is a required callback; the
current host tests use a mock verifier. A production pinned Ed25519 keyring,
key rotation/revocation policy and end-to-end release signing pipeline remain
unfinished.

See [Application platform status](../docs/architecture/APPLICATION-PLATFORM.md)
for the exact compatibility boundary and roadmap.
