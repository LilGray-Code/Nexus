# Nexus Native Application SDK (preview)

## What this SDK targets

The first Nexus-owned native application ABI targets x86-64 **static ELF64 ET_EXEC** programs. Applications are entered in ring 3 and use the Nexus `int 0x80` ABI from `kernel/include/nexus/app.h`. The ABI currently exposes:

- stable ABI version query and process exit;
- standard-output writes and bounded root-file open/read/write/close calls;
- keyboard and mouse event polling;
- display size, pixel format, and byte stride;
- monotonic timer ticks and interruptible sleep;
- bounded XRGB8888 framebuffer blits.

The kernel checks user pointers before copying data. Frame submission is capped at 16,384 source pixels per call. Input events and all SDK structures have fixed layout assertions in `kernel/tests/app_abi_layout_test.c`.

## Build the example

Install an x86-64 bare-metal GCC cross compiler, then run:

```sh
make -C sdk
```

This produces `sdk/build/kinetic_demo.elf`. The executable is a static, freestanding ELF image with no dynamic interpreter or shared-library dependencies. Put the file on a readable Nexus volume and use the desktop Files app's Run action to launch it. Press Escape to exit the demo.

Override the toolchain prefix when needed:

```sh
make -C sdk CROSS_COMPILE=x86_64-linux-gnu-
```

The example uses a 128×96 software-rendered frame, submits it through the native display syscall, reads live keyboard/mouse events, and sleeps one 100-Hz timer tick between frames. On the existing VirtIO-GPU path, frame updates are transferred and flushed through the VirtIO 2D display queue. Other displays use the active firmware framebuffer.

## Compatibility boundary

This is a first-party native ABI, not POSIX, Win32, SDL, Vulkan, OpenGL, or Direct3D. The current process runner is a foreground, synchronous launcher; it does not yet provide general-purpose multitasking, dynamic linking, shared libraries, a window manager API, audio, game controllers, or a 3D rendering API. VirtIO-GPU support here is **2D scanout and presentation only**; it must not be described as hardware-accelerated 3D rendering.

Windows PE applications and commercial Windows games do not run through this ABI. Native packages, publisher signatures, runtime libraries, and compatible device drivers remain required ecosystem work. Applications should query the ABI version and display capabilities instead of assuming a particular machine.
