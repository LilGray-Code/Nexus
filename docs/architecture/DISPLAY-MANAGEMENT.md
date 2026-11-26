# Nexus display management

## Current implementation

The display subsystem is split between the display model in `kernel/src/display.c`,
the software framebuffer transforms in `kernel/src/graphics.c`, and the
VirtIO-GPU scanout driver in `kernel/src/virtio_gpu.c`. The desktop owns
hardware discovery and registers the active output backends.

### Discovery and monitor identity

- The boot framebuffer contributes the initial output and firmware mode list.
- VirtIO-GPU `GET_DISPLAY_INFO` discovers up to 16 scanouts.
- When the device negotiated EDID support, `GET_EDID` supplies each output's
  EDID. Nexus checks the base block header and checksum and checks every
  declared extension block checksum before marking the extension set complete.
- The parser collects established, standard, detailed and CTA detailed timings,
  the monitor name, CTA HDR static-metadata flags, manufacturer code, product
  code, serial number, and advertised physical dimensions.
- DPI is derived from the EDID physical dimensions and the active pixel size.
  Unknown or invalid physical dimensions remain unknown rather than being
  guessed.

EDID describes supported timings and monitor capabilities. It is not evidence
that a particular timing is currently active or that the GPU can program it.

### Mode-change transaction and recovery

The mode API requires a backend to provide a preflight test, set operation,
restore operation, and post-set verification. A change follows this order:

1. Snapshot the last known-good mode.
2. Run the backend's preflight test. A failed test returns without changing
   scanout state.
3. Program the candidate mode.
4. Read back and verify the resulting mode.
5. Publish the new mode to the display model only after verification.
6. If programming or verification fails, restore and verify the previous mode.
7. If restoration cannot be verified, disable further modesets and move the
   desktop back to the boot firmware framebuffer where that fallback remains
   usable.

The host display test covers test rejection, a failed set, failed verification,
successful rollback, failed rollback, and firmware fallback.

### Multiple outputs

The display model tracks each discovered output independently, including its
EDID data, position, dimensions, connected/enabled state, and primary flag.
The current VirtIO backend can select a primary scanout and request mirrored
or extended layouts when the host-provided scanout rectangles make that safe.
An extended layout is not synthesized by placing outputs outside the
preallocated shared framebuffer. The current modeset backend deliberately
disables resolution changes when more than one scanout is active.

### Software display controls

Rotation, logical scaling, brightness dimming, and night-light color
temperature are software framebuffer transforms. DPI is exposed as monitor
metadata; the UI scale preference is separate from native scanout dimensions.
These effects do not program a panel's physical backlight or color hardware.

## Hardware capability boundaries

The present kernel branch does **not** yet contain native Intel, AMD, or NVIDIA
display-mode drivers, an ACPI/I2C panel backlight driver, or a complete HDR
output pipeline. The VirtIO-GPU 2D protocol used here exposes scanout rectangles,
not a physical connector's pixel clock or refresh-rate programming. As a result:

| Capability | Current status |
|---|---|
| EDID discovery, name and vendor/model/serial identity | Implemented for the firmware framebuffer and VirtIO EDID-capable outputs |
| EDID timing enumeration | Implemented; advertised timings are not automatically usable by every driver |
| Resolution/scanout resize | Verified VirtIO rectangle changes on one active scanout, bounded by the existing backing resource |
| Physical refresh-rate switching | Not implemented; EDID rates are informational |
| Rotation and logical scaling | Software framebuffer transforms |
| DPI reporting | Calculated from EDID size when available |
| Multiple outputs, primary selection, extended/mirror layout | VirtIO scanout support, subject to host-provided layout and device capabilities |
| Brightness and night light | Software-only effects; panel backlight control is not implemented |
| HDR metadata discovery | Implemented for supported CTA metadata blocks |
| HDR signal output | Not implemented; monitor metadata alone does not constitute an HDR pipeline |
| Automatic rollback and firmware fallback | Implemented and host-tested for the registered mode backend |

Do not present EDID rates as selectable physical refresh rates, software
brightness as panel-backlight control, or HDR monitor metadata as working HDR
output. A native GPU/backlight driver and hardware conformance tests are
required before those controls can safely be enabled.
