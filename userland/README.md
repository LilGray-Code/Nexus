# Nexus native application SDK (early ABI)

This directory contains the first freestanding C SDK for native Nexus
applications. It targets the original x86-64 kernel's current static ELF64
loader and the initial `int 0x80` syscall ABI.

## Build

From the repository root:

```sh
make -C userland
make -C userland inspect
```

The default target produces `build/userland/HELLO.ELF`. The example is
compiled from C and a minimal startup routine; it is not hand-assembled by the
disk-image generator. CI inserts this exact compiler-built executable into
the disposable FAT32 boot-test image.

## Syscalls currently wrapped

| Wrapper | Syscall | Behavior |
|---|---:|---|
| `nexus_exit(status)` | 1 | Exit the current native process |
| `nexus_write(fd, buffer, length)` | 2 | Write to descriptor 1 (serial-backed console) |
| `nexus_open(path)` | 3 | Open a simple read-only file name |
| `nexus_read(fd, buffer, length)` | 4 | Read a bounded chunk into the process |
| `nexus_close(fd)` | 5 | Close a descriptor |
| `nexus_file_write(path, buffer, length)` | 6 | Write through the current VFS policy |

Arguments use RDI/RSI/RDX, the syscall number and result use RAX, and errors
are returned as negative errno values. The kernel validates user-memory ranges
before copying. File names are simple volume-local names; the early interface
does not support path separators or directory descriptors.

## Current limitations

This is an SDK starter, not a libc or POSIX implementation. The process
loader accepts static x86-64 ET_EXEC ELF files only: no PIE, dynamic linker,
shared libraries, TLS, or Windows PE/COFF. The current process image limit is
128 KiB, process names are limited to 15 characters, and the initial descriptor
table and user console are bootstrap facilities. Programs run synchronously
and the shell resumes only after process exit. The test image's HELLO.ELF is
kept below one 512-byte FAT32 cluster so the compact CI image generator can
install it without introducing a multi-cluster directory-entry test dependency.

The sample deliberately exercises open/read/close/write and returns status 7.
The test image stores the 571-byte compiled ELF in a bounded two-cluster FAT32
chain. The QEMU boot workflow checks its output and exit status from the
actual Nexus kernel, making this a verified native ABI example rather than a
host-only compile demo.
