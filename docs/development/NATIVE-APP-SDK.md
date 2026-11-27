# Nexus Native Application SDK (preview)

## What this SDK targets

The first Nexus-owned native application ABI targets x86-64 **static ELF64 ET_EXEC and constrained static PIE (ET_DYN)** programs. Static PIE is relocated by the kernel using symbol-free `R_X86_64_RELATIVE` entries only; it has no external shared-library dependencies, interpreter, TLS, or general dynamic linker. Applications are entered in ring 3 and use the Nexus `int 0x80` ABI from `kernel/include/nexus/app.h`. Nexus explicitly enables x87 and SSE2 for a foreground app, saves/restores the kernel's 512-byte FXSAVE state across that app, and clears XMM registers before entry. Kernel code is compiled to avoid FPU/SIMD use while an app is running. AVX/AVX2/FMA remain disabled because the OS does not yet manage full XSAVE extended state. The ABI currently exposes:

- stable ABI version query and process exit;
- standard-output writes and bounded root-file open/read/write/close calls;
- keyboard and mouse event polling through the original stable 12-byte input event;
- separate gamepad polling for up to four xHCI-attached generic HID controllers, with a 32-bit button mask, up to eight normalized axes, an eight-way hat, and a disconnect flag;
- display size, pixel format, and byte stride;
- monotonic timer ticks and interruptible sleep;
- bounded XRGB8888 framebuffer blits;
- optional AC'97 48 kHz, signed 16-bit stereo PCM playback in one bounded DMA chunk (maximum 1,024 frames per submission).

The kernel checks user pointers before copying data. Frame submission is capped at 16,384 source pixels per call. Input events and all SDK structures have fixed layout assertions in `kernel/tests/app_abi_layout_test.c`.

## Build the example

Install an x86-64 bare-metal GCC cross compiler, then run:

```sh
make -C sdk
make -C sdk inspect
```

This produces `sdk/build/kinetic_demo.elf`, `sdk/build/gamepad_demo.elf`, `sdk/build/audio_demo.elf`, and `sdk/build/pie_demo.elf`. The first three are static ET_EXEC images. The PIE demo is a static ET_DYN image that verifies the kernel's relative-relocation handling; none of these demos requires external shared libraries or a dynamic interpreter. Put a demo on a readable Nexus volume and use the desktop Files app's Run action to launch it. Press Escape to exit each demo. The gamepad demo displays normalized axis bars and button state, and clears its connected state when xHCI reports a controller disconnect. The audio demo requests one 1,024-frame AC'97 PCM chunk at a time and plays a 440 Hz stereo test tone when a supported controller is present.


The `inspect` target also reports ELF headers/program headers and a short disassembly using `file`, `readelf`, and `objdump`. It is a host-side build/inspection workflow; these tools are not yet shipped as native Nexus applications.

Override the toolchain prefix when needed:

```sh
make -C sdk CROSS_COMPILE=x86_64-linux-gnu-
```

The example uses a 128×96 software-rendered frame, submits it through the native display syscall, reads live keyboard/mouse events, and sleeps one 100-Hz timer tick between frames. On the existing VirtIO-GPU path, frame updates are transferred and flushed through the VirtIO 2D display queue. Other displays use the active firmware framebuffer.

## Compatibility boundary

This is a first-party native ABI, not POSIX, Win32, SDL, Vulkan, OpenGL, or Direct3D. Native app code can use x87/SSE2 floating-point and vector math, but must not use AVX/AVX2/FMA. The current process runner is a foreground, synchronous launcher; it does not yet provide general-purpose user-process multitasking, a shared-library dynamic linker, a window manager API, audio capture, multi-stream mixing, or a 3D rendering API. AC'97 playback is currently a single-buffer 48 kHz stereo path; Intel HD Audio, capture, low-latency streaming, and multi-application mixing remain future work. Generic USB HID gamepads are decoded through xHCI when their report descriptor fits the supported bounded single-report format. Boot-protocol keyboards/mice and common generic HID gamepads are supported, including disconnect events; composite controllers, USB hubs, Bluetooth pairing, vendor-specific HID extensions, and arbitrary HID report formats are not guaranteed. VirtIO-GPU support here is **2D scanout and presentation only**; it must not be described as hardware-accelerated 3D rendering.

Windows PE applications and commercial Windows games do not run through this ABI. Native packages, publisher signatures, runtime libraries, and compatible device drivers remain required ecosystem work. Applications should query the ABI version and display capabilities instead of assuming a particular machine.
