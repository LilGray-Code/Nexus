# Nexus Professional Graphics and Display Acceleration Roadmap

**Status:** Initial display-discovery increment; full 2D/3D acceleration remains future work  
**Kernel target:** Nexus-owned x86-64 kernel and userland  
**Principle:** A scanout path is not a rendering engine. A detected PCI adapter is not an initialized GPU.

## 1. Current graphics state — do not overstate it

The current desktop draws with the Nexus CPU framebuffer rasterizer. It has clipping, alpha blending, rounded rectangles, damage tracking, text antialiasing and XRGB8888 blitting.

The current VirtIO-GPU path is a **2D host scanout transport**:
- Creates a VirtIO 2D resource and attaches guest DMA framebuffer memory.
- Sets scanout 0.
- Transfers modified rectangles to the host and flushes them.
- The desktop rasterizes pixels on the CPU before asking VirtIO to update the display.

This is a useful, functional display transport, but it is not GPU-accelerated rendering. It does not implement a 3D command context, shader compilation, OpenGL, Vulkan, video decoding or GPU compute. PCI discovery for Intel, AMD and NVIDIA is inventory only. Those devices must remain CPU-rendered until a driver initializes a real hardware engine and passes workload tests.

### Implemented in this increment

- A defensive EDID 1.x base-block parser validates the signature, version, base checksum and each declared extension checksum.
- The parser extracts base-block detailed timing descriptors, pixel clock, active/blanking totals, interlace status and a flagged preferred timing.
- The VirtIO-GPU driver negotiates the optional EDID feature, queries display scanout information and exposes per-scanout position and dimensions.
- When the device advertises EDID, the driver can fetch and validate a scanout's EDID before exposing its base-block modes.
- GPU runtime reporting separates firmware scanout, VirtIO scanout, 2D rendering, 3D rendering, OpenGL and Vulkan. VirtIO host scanout never sets the hardware-rendering flags.
- CI adds host tests for EDID parsing, VirtIO scanout response decoding and truthful GPU capability reporting.

The EDID parser checks extension-block checksums but does not yet decode CTA/DisplayID extension timing blocks. The desktop still renders one logical surface and sets only scanout 0. Querying multiple scanout descriptors is not the same as driving multiple monitors concurrently.

## 2. Graphics architecture target

Build an explicit pipeline with stable boundaries:

`Application -> OpenGL/Vulkan API -> user-mode loader/layers -> Mesa or other UMD -> Nexus graphics UAPI -> Nexus GPU kernel driver -> device`

Display composition and presentation are a separate pipeline:

`Application surfaces -> compositor -> display buffers -> KMS/display driver -> connector/monitor`

Required subsystems:

1. **Display/KMS layer** — connectors, encoders, CRTCs, planes, framebuffers, monitor modes, hotplug, atomic validation/commit, vblank and page flipping.
2. **GPU memory manager** — buffer objects, reference-counted lifetime, CPU/GPU mappings, device-local memory, system-memory mappings, eviction/migration, cache coherency and fences.
3. **Command submission** — per-process GPU contexts, command validation, bounded queue submission, reset/recovery, synchronization objects and scheduling/fairness.
4. **Driver model** — exact PCI/subsystem/revision match, firmware dependencies, DMA/IOMMU ownership, interrupts, power transitions, health state and removal.
5. **User-mode graphics runtime** — OpenGL ES/OpenGL and Vulkan loader/ICD paths, libraries, shader compiler, EGL-like surface integration and validation tools.
6. **Compositor** — hardware planes and cursor where supported, GPU composition, damage tracking, vsync, scaling, transparency and robust CPU fallback.
7. **Video acceleration** — separate decode/encode capabilities, profile reporting and test vectors; do not infer decode support from generic GPU support.

The Nexus kernel is not Linux. Do not copy a Linux DRM ioctl table and claim Linux DRM compatibility unless that exact ABI is implemented. Design a small, versioned Nexus graphics UAPI and provide explicit adaptation layers for each ported user-mode driver.

## 3. Kernel display model

Model the display resources explicitly:

- Adapter: PCI identity, driver, reset/health state and supported engines.
- Connector: connected state, connector type, EDID, mode list and color capabilities.
- Encoder/CRTC/plane: supported routing, pixel formats, scaling and hardware properties.
- Framebuffer: dimensions, pitch, format, planes, buffer objects and lifetime.
- Atomic state: validated connector/CRTC/plane configuration committed as one update.
- Presentation: page-flip completion, vblank sequence and monotonic timestamps.

The first implementation can support one active output and a simple primary plane; keep the data model extensible to multiple outputs. Never modeset an unknown device or trust unvalidated EDID timings. Enforce upper bounds on dimensions, refresh rates, pitch and allocation sizes before allocating display memory.

### Mode selection

- Read firmware-provided framebuffer dimensions as a conservative starting mode.
- Prefer validated EDID detailed timings when available.
- Parse established/standard timings and CTA extension blocks in a later milestone.
- Check the requested mode against display, connector and driver limits before modesetting.
- Preserve a recovery mode and return to firmware/known-good scanout if modesetting fails.
- Treat HDR metadata, 10-bit scanout, wide color gamut, VRR and adaptive-sync as independent optional features.

### Multi-monitor

- Track every scanout/connector, position, mode, scale and preferred mode independently.
- Support per-output framebuffers or a compositor surface with explicit output transforms.
- Implement hotplug notification and connector re-probe.
- Test mixed resolutions, refresh rates, rotation/scaling, hotplug and failure of one output without losing the others.

The current desktop must not report multi-monitor desktop support just because VirtIO returns more than one enabled scanout.

## 4. GPU memory and rendering interfaces

Create a buffer-object API before adding complex GPU drivers:

- Create/destroy a buffer with size, usage, format and alignment constraints.
- CPU map/unmap with explicit cache and coherency rules.
- GPU map/unmap into a device address space.
- Import/export only through reference-counted, permission-checked handles.
- Fence or timeline synchronization for CPU/GPU and producer/consumer ordering.
- Explicit ownership transitions for CPU writes, GPU access and scanout.
- Bounds checking, address-width constraints, IOMMU attachment and cleanup on device reset.
- Separate user virtual addresses, physical pages and GPU virtual addresses. Never expose raw physical addresses to untrusted applications.

Do not implement an unsafe "allocate VRAM and return its address" shortcut. Discrete GPUs and integrated GPUs have different placement and mapping needs. Design for system memory and dedicated device-local memory with driver-specific placement behind a common lifetime/synchronization contract.

Port GEM/TTM concepts only as appropriate. Linux DRM documentation describes GEM as shared buffer-object infrastructure and TTM as the broader placement/migration manager for UMA and dedicated VRAM. Those are useful designs to study, not Nexus APIs that can be linked unchanged.

## 5. First 3D target: VirtIO-GPU with virgl or Venus

The lowest-risk path to exercise a real GPU command interface is a virtual GPU under QEMU/KVM, before porting three distinct physical GPU families.

QEMU distinguishes:
- Default `virtio-gpu` 2D mode, where the guest still needs a software renderer for 3D.
- `virtio-gpu-gl`/virglrenderer, where guest OpenGL commands are translated through a Gallium/virgl command stream to the host renderer.
- The Venus protocol for Vulkan through virglrenderer, requiring host-visible/blob resources and compatible host support.

Nexus currently implements a subset of the 2D VirtIO protocol only. A real 3D implementation needs:
1. VirtIO feature negotiation for VIRGL, blob resources and relevant context features.
2. GET_CAPSET_INFO/GET_CAPSET parsing with strict length and version checks.
3. Context creation/destruction and context-specific resources.
4. 3D resource creation, attachment/import and host-visible memory support.
5. SUBMIT_3D command transport with fences and robust timeout/reset recovery.
6. A user-mode Gallium/virgl driver adapted to Nexus syscalls, loader and buffer-object ABI.
7. Vulkan Venus only after the 3D context, blob and synchronization contract is stable.
8. GPU-backed command test, OpenGL conformance tests and Vulkan CTS subset before advertising either API.

A virtual virglrenderer device is not the same as native acceleration on the host's physical card: it is a host-mediated virtualized renderer and depends on host GPU/runtime support. The guest must report that distinction.

## 6. Physical GPU driver strategy

### Intel

- Inventory exact device generations, subsystem IDs and firmware dependencies.
- Evaluate maintained Intel `i915`/Xe kernel components with their exact firmware, memory-management, power, interrupt, reset and user-mode ABI dependencies.
- Evaluate Mesa ANV (Vulkan) and Iris/Crocus (OpenGL) only after the corresponding kernel-facing interfaces and runtime prerequisites exist.
- Do not expose a single "Intel GPU supported" flag across all generations.

### AMD Radeon

- Select a narrow first generation and exact PCI/revision scope.
- Port the necessary amdgpu kernel-side pieces, including memory management, rings, power, interrupts, reset, firmware loading and synchronization.
- Integrate Mesa RadeonSI (OpenGL) and RADV (Vulkan) only against the required kernel/UAPI contract.
- Mesa documentation explicitly describes RADV as a user-mode Vulkan driver that relies on the amdgpu kernel-mode driver and DRM userspace API. It cannot simply be copied into a kernel that lacks those interfaces.

### NVIDIA

- Maintain a separate device-generation matrix and firmware/licensing review.
- Evaluate supported Nouveau/NVK paths and any required signed firmware or newer firmware processor/GSP support by exact generation.
- Keep proprietary Windows/Linux kernel modules outside the native Nexus driver path unless a dedicated compatibility architecture is explicitly engineered.
- Never claim universal NVIDIA support.

## 7. OpenGL and Vulkan runtime

### OpenGL
- Port a stable Mesa Gallium driver path only after the Nexus buffer, command submission, synchronization, process and shared-library requirements exist.
- Provide a Nexus platform integration layer for display surfaces, buffer sharing and presentation.
- Implement EGL-like context/surface support before a desktop browser or game is expected to use hardware OpenGL.
- Add conformance coverage and a software fallback such as a ported CPU rasterizer.

### Vulkan
- Implement a Vulkan loader with explicit ICD discovery, per-device dispatch and optional layer support.
- Keep application API, loader, validation layers and ICD drivers as separate components.
- A Vulkan driver is not provided by a PCI ID or by a loader alone. Each ICD must implement real device commands and pass its supported Vulkan conformance profile.
- Port Lavapipe/SwiftShader as a CPU fallback separately from physical GPU ICDs; report it as software Vulkan rather than GPU acceleration.

### Browser and video
- Only enable GPU compositing when a working driver, importable buffer path and synchronization model are available.
- Add browser accelerated compositing as a distinct capability from WebGL/WebGPU support.
- Add hardware video decode only when a codec/profile/bit-depth/format path is implemented and tested. Separate decode from encode, protected playback and HDR presentation.

## 8. Quality, performance and fallback

The compositor must support:
- Damage-region rendering and cached surfaces.
- Vsync-aware frame scheduling and tear-free page flips where available.
- Alpha blending, shadows and antialiasing.
- Font subpixel/coverage rendering with correct display color order.
- Multiple monitor scale factors and DPI-aware layout.
- GPU composition only when available and stable.

Fallback order:
1. Native GPU render + native display scanout.
2. Native GPU render + alternate supported scanout.
3. CPU raster + VirtIO/firmware display transport.
4. CPU raster + firmware framebuffer.
5. Serial/recovery console if no safe display surface exists.

When any backend times out or resets, stop submitting commands, quiesce the device, preserve or copy the last safe frame where possible, switch to the next fallback and record the failure. Do not leave stale GPU memory mappings or report an accelerated API after backend failure.

## 9. CI and required proof

### Host tests
- EDID header/checksum/extension validation and DTD decoding.
- VirtIO scanout response size, enabled bits, bounds and position decoding.
- GPU runtime capability reports never promote scanout to 2D/3D acceleration.
- GPU buffer lifetime, fence ordering and bounds when those modules are added.

### QEMU tests
- Firmware framebuffer fallback.
- VirtIO 2D scanout and rectangle transfer.
- Multiple reported scanouts, including disabled heads.
- EDID feature present/absent and malformed response handling.
- Virgl OpenGL and Venus Vulkan only with a host setup that demonstrably supports those backends.
- Injected queue timeout/reset and fallback continuity.

### Physical validation
- Intel, AMD and NVIDIA generations validated independently.
- EDID and real monitor hotplug.
- 1080p/1440p/4K timings, high refresh, mixed refresh, multi-monitor, scaling and HDR/VRR only where the complete display path supports them.
- Long-run 3D command stress, resource eviction and GPU reset/recovery.
- Benchmarks vs the CPU fallback on the same workloads.

Never use a host QEMU test as proof of physical GPU compatibility.

## 10. Completion gates

| Gate | Deliverable | Current status |
|---|---|---|
| G0 | Truthful GPU inventory and render-path reporting | Existing |
| G1 | EDID and VirtIO scanout enumeration | Initial implementation in this branch; CI pending |
| G2 | Connector/CRTC/plane mode-setting and hotplug | Not implemented |
| G3 | GPU buffer objects, address spaces, fences and reset path | Not implemented |
| G4 | VirtIO-GPU 3D virgl path and Mesa Gallium integration | Not implemented |
| G5 | OpenGL runtime and conformance on a named backend | Not implemented |
| G6 | Vulkan loader, ICD and conformance on a named backend | Not implemented |
| G7 | Intel/AMD/NVIDIA native driver generations | Not implemented |
| G8 | Browser compositor/video acceleration, HDR/VRR and multi-monitor validation | Not implemented |

**Immediate next engineering work:** complete EDID/scanout CI and QEMU boot validation; then add connector mode-setting and page-flip foundations. In parallel, prototype VirtIO-GPU capset and 3D queue support in an isolated branch. Do not start a physical Intel/AMD/NVIDIA port before the GPU memory, sync, command and reset contracts are designed.

## 11. Upstream references

- [OASIS VirtIO GPU specification, v1.4](https://docs.oasis-open.org/virtio/virtio/v1.4/virtio-v1.4.pdf)
- [QEMU VirtIO-GPU variants and acceleration modes](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html)
- [Linux DRM memory management: GEM and TTM](https://www.kernel.org/doc/html/latest/gpu/drm-mm.html)
- [Mesa RADV driver architecture and hardware dependencies](https://docs.mesa3d.org/drivers/radv.html)
- [Khronos Vulkan Loader architecture](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md)

These are architecture references. A component is only adopted after source review, API adaptation, licensing review and target-system tests.
