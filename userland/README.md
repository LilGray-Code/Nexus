# Nexus native application SDK (early ABI)

This directory contains the first freestanding C SDK for native Nexus
applications. It targets the original x86-64 kernel's static ELF64 loader and
the initial `int 0x80` syscall ABI.

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

Arguments use RDI/RSI/RDX, the syscall number and result use RAX, and errors
are returned as negative errno values. The ABI version is currently 1; native
programs should query `nexus_abi_version()` before using other wrappers. The
kernel validates user-memory ranges before copying. File names are simple
volume-local names; the early interface does not support path separators or
directory descriptors.

## Reusable runtime

`libnexus.a` currently provides allocation-free memory/string helpers,
`nexus_write_all()` for retrying valid short writes,
`nexus_read_file()` for joining partial reads into a caller-owned buffer,
`nexus_write_file()` for the VFS-backed file-write syscall, `nexus_puts()`
for console output, and `nexus_printf()` for bounded formatted output.

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

This is an early runtime, not libc or POSIX. The process loader accepts static
x86-64 ET_EXEC ELF files only: no PIE, dynamic linker, shared libraries, TLS,
or Windows PE/COFF. The current process image limit is 128 KiB, process names
are limited to 15 characters, and the initial descriptor table and user
console are bootstrap facilities. Programs run synchronously and the shell
resumes only after process exit. There is no heap allocator, full libc/POSIX formatted stdio,
directory descriptor API, process-spawn API, GUI SDK, or background scheduler.

## Bounded formatted output

`nexus_printf()` accepts `%%`, `%c`, `%s`, `%d`/`%i`, `%u`, `%x`/`%X`, and
`%p`. Numeric conversions may use a width up to 32 and optional zero padding
(e.g. `%04u`). Output is assembled in a fixed 512-byte buffer and then sent
through `nexus_write_all()`, so valid short writes are retried. Invalid
specifiers/widths return `-NEXUS_EINVAL`; output overflow returns
`-NEXUS_EFBIG`. This is a small freestanding formatter, not full ISO C
`printf`/`stdio` compatibility.
