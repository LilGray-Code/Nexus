# Nexus Graphics Quality and Acceleration Roadmap

## Current implementation

Nexus currently receives an RGB framebuffer from Limine and draws directly into
that mapped surface using bounded CPU routines. The renderer supports clipped
pixel fills, alpha blending, circles, rounded rectangles, and a built-in 5x7
bitmap font. The desktop now has an optional fractional-size, area-coverage
text renderer for smoother large headings. The boot entries request a
1920x1080x32 framebuffer; Limine selects another available mode when that exact
mode is unavailable.

This is a higher-resolution software-rendered desktop, not a hardware
accelerated graphics stack. No Intel, AMD, NVIDIA, virtio-gpu, or other GPU
command submission driver is currently implemented. Do not report GPU
acceleration as enabled, and do not imply that 1080p is available on every
display adapter.

## Architecture required for real acceleration

1. **Device discovery.** Identify PCI display-class devices and retain exact
   vendor, device, revision, subsystem, BAR, and capability information.
   Enumeration and BAR sizing must be validated without enabling bus mastering.
2. **Mode setting and memory ownership.** Define a display-device interface for
   connector discovery, mode enumeration, framebuffer allocation/mapping,
   scanout, hot-plug events, and safe fallback to the bootloader framebuffer.
3. **DMA and command execution.** Create a reviewed DMA memory contract, GPU
   command buffers, submission queues, fences, timeout recovery, and device
   reset handling. Do not enable PCI bus mastering until DMA address
   translation, range validation, cache coherency, and ownership are correct.
4. **2D acceleration first.** Add accelerated fills, copies, alpha blends,
   glyph/image blits, and page flips. Keep the current CPU renderer as the
   reference/fallback and compare output in host tests.
5. **Composition and presentation.** Add a backbuffer, damage tracking,
   vsync-aware presentation, scaling, color-space handling, and a clear
   synchronization boundary between applications and scanout.
6. **3D APIs later.** Add a stable user-facing graphics API and sandboxed GPU
   submission only after process isolation and memory management are ready.
   OpenGL/Vulkan/DirectX compatibility is not implied by basic 2D acceleration.

## Driver strategy

Implement one GPU family at a time, based on exact hardware IDs and a documented
test machine. Prefer a small, reviewed 2D acceleration path or a standard
paravirtual device for development before attempting modern discrete GPUs.
Each new driver must specify its firmware/license policy, supported devices,
required BAR/IRQ/DMA capabilities, reset behavior, memory limits, and security
model. Never infer support for other adapters from a single model.

## Acceptance gates

- Host tests cover clipped rendering, channel formats, alpha arithmetic,
  fractional text coverage, overflow limits, and guard-page integrity.
- QEMU tests cover a software-only framebuffer fallback and each supported
  accelerated device model.
- Device tests cover mode changes, multi-monitor detection, reconnects,
  hot-unplug where supported, device reset, suspend/resume, and a lost-fence
  timeout without memory corruption.
- Image-quality comparisons verify text edges, scaled images, gradients, and
  compositing against a deterministic software reference.
- Publish a compatibility matrix with exact PCI IDs, firmware versions, test
  machines, and test dates.

Until those gates are satisfied, the supported graphics path remains the
firmware-provided framebuffer with CPU rendering and a requested high-resolution
mode where the firmware exposes it.
