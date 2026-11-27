# Nexus Audio Architecture

**Status:** Shared audio-core foundation and AC'97 playback adapter implemented. HD Audio, USB Audio, HDMI/DisplayPort audio, Bluetooth audio and physical microphone capture are not yet implemented.

## Current implementation

The current branch adds `kernel/src/audio.h` and `audio.c` as a backend-neutral audio core.

### Endpoint model

- A bounded registry holds up to eight playback or capture endpoints.
- Endpoint records distinguish AC'97, HD Audio, USB Audio, HDMI/DisplayPort and Bluetooth types, direction, label, sample rate, channel count, readiness and capabilities.
- The default playback and capture endpoints are selected independently.
- Runtime default-device switching, driver readiness changes, endpoint removal and replacement-default selection are supported.
- Only drivers that actually initialize should register a ready endpoint. A PCI multimedia-class device or GPU display connector is not automatically an audio endpoint.

The current boot integration initializes the AC'97 backend and registers an AC'97 playback endpoint only after the controller and codec are ready. The current code does not register fictitious HDA, USB, HDMI/DP or Bluetooth endpoints.

### Playback mixer

The software mixer currently provides:

- Up to eight bounded playback streams, each associated with an application/process identifier.
- Per-stream volume, mute and output route.
- Per-application volume changes applied to all that application's streams.
- A 2,048-frame ring per stream; producers get a bounded partial-write result and overrun accounting.
- Signed 16-bit mono/stereo input converted to stereo output.
- Linear interpolation sample-rate conversion from 8 kHz through 192 kHz to a 48 kHz output mix.
- Wide accumulation and signed 16-bit saturation to avoid integer wrap when several loud streams are mixed.
- A global master volume and mute setting.
- Explicit underrun, overrun, mix-call and backend-submit-error counters.

The initial AC'97 adapter submits 480 stereo frames at 48 kHz (10 ms) to the existing bus-master DMA output. The generic AC'97 driver can accept up to 1,024 frames per submission, but the audio service uses a smaller bounded block. The main idle loop polls the AC'97 DMA engine and submits mixed data when it is idle.

This is a first mixer, not a high-quality audiophile resampler or a production real-time audio server. The rings and manager currently assume serialized kernel-side access; they are not yet safe for unrestricted concurrent producers or user-thread preemption.

### Capture foundation

The core defines capture streams, a driver-to-core `nexus_audio_capture_publish()` submission path, and application reads from bounded capture rings. It validates capture format against the registered endpoint's negotiated sample rate/channel capabilities.

This is plumbing for future capture drivers, not proof of microphone support. No current HDA or USB Audio capture driver publishes hardware samples.

### Desktop audio panel

Settings now has a dedicated **Audio** page with:

- Output and input device lists populated from initialized endpoints.
- Default output and input selection.
- Master volume from 0–100% in 10% steps.
- Persistent master mute.
- Per-stream application volume adjustment and mute when streams are registered.
- Explicit empty states when no playback/capture endpoint or application stream exists.

Default endpoint selection is currently runtime-only; master volume and mute are persisted in the existing version-2 `NEXUS.CFG` record. The previous reserved byte is now a checked mute bit; records written before this change have zero in that byte and remain valid.

The early native application's audio syscall still has its legacy direct AC'97 path and is not yet converted into a persistent per-process mixer stream. Therefore the per-application panel controls apply to streams opened through the new core API, not every existing native application.

## Hardware driver roadmap

### AC'97

Existing legacy driver:
- PCI class/subclass detection for AC'97 multimedia devices.
- Codec-ready wait, mixer initialization, PCM bus-master DMA and master output volume.
- Bounded single-buffer stereo S16 playback at 48 kHz.
- DMA busy/error polling and recovery.

Remaining work includes a cyclic descriptor ring with multiple queued periods, completion interrupts rather than polling, robust codec-specific volume/routing, capture streams, and broader controller validation.

### Intel/AMD HD Audio

HD Audio must be a separate driver from AC'97. The Intel HD Audio specification describes a different link/codec architecture and explicitly does not provide AC'97 link compatibility.

A correct driver needs:
1. PCI class 04/subclass 03 discovery, MMIO BAR validation, bus mastering and power state management.
2. Controller reset, stream descriptor setup and DMA buffer descriptor lists.
3. CORB/RIRB command and response rings, codec address discovery and codec reset.
4. Codec function-group/widget discovery, connection-list parsing, pin defaults, converter capabilities and amplifier controls.
5. Playback and capture streams with supported PCM rates, channel maps, sample widths and format negotiation.
6. Unsolicited response handling for jack detection, device insertion/removal and power events.
7. DMA period completion, stream position tracking, xrun detection and safe stop/restart.

The Linux HD-audio driver notes are useful engineering references for controller/codec division, board quirks and interoperability testing; they are not a ready-made Nexus driver.

### HDMI and DisplayPort audio

HDMI/DP audio is tied to the graphics/display pipeline. It requires an initialized GPU display engine, a trained active link, codec/controller stream setup, ELD/sink capability parsing, supported channel/sample-rate negotiation and coordinated teardown before a modeset disables the port. Intel's i915 documentation describes the split between graphics-side codec sequences and audio-side controller sequences; audio enablement occurs after link training and is coordinated with modesetting.

No Nexus GPU currently exposes this audio stream path. A monitor being detected by EDID is not proof that its audio endpoint is usable.

### USB Audio

USB audio needs USB Audio Class descriptor parsing and a real isochronous transfer scheduler in the xHCI host controller. It must support playback and capture terminal/feature/clock entities, alternate interface settings, endpoint packet sizing, synchronization type, feedback endpoints and device clock behavior. UAC 1.0 and UAC 2.0 are sensible initial compatibility targets; device-specific descriptor quirks need a conformance corpus.

The existing xHCI support is currently oriented around USB HID input and storage paths. It does not yet schedule USB audio isochronous endpoints. The USB Audio 2.0 specification's synchronous, adaptive and asynchronous endpoint modes show why a USB audio driver cannot simply send one fixed-size packet every millisecond.

### Bluetooth audio

Bluetooth audio is a later driver/service milestone after Bluetooth controller transport, pairing, security and L2CAP are working. Audio profiles require separate A2DP media transport and HFP call/headset behavior, codec negotiation, device discovery, jitter buffering and resampling/clock-drift handling. The audio manager's endpoint API is ready to represent Bluetooth endpoints, but there is no Bluetooth stack or Bluetooth audio driver now.

## Runtime and service design

The long-term design should follow a graph-based user-space media service with:
- Device/source/sink nodes.
- Application playback/capture streams.
- Format and rate negotiation.
- Per-stream volume/mute and route policy.
- Resampling and clock-drift correction between endpoints.
- Bounded period queues and timestamped scheduling.
- Device hotplug and default-route policy.
- Session permissions for microphone capture and per-app control.
- A stable user/kernel driver interface that does not expose DMA addresses or raw device registers to applications.

PipeWire is a useful design reference for graph processing, format negotiation, out-of-process clients and low-latency graph scheduling. It is not a drop-in binary dependency for the current Nexus ABI.

## Validation strategy

The new host test suite covers endpoint registration and defaults, invalid descriptors, runtime device loss, mono-to-stereo mapping, multi-stream gain and mute, clipping, linear 24 kHz-to-48 kHz resampling, bounded ring capacity, underruns/overruns, capture ring publication/readback, AC'97 backend submission and backend error accounting. Settings tests cover master volume range and persistent mute state.

These host tests prove the core algorithms and state transitions with a fake AC'97 backend. They do not prove physical device compatibility, capture from a real microphone, output quality, low-latency behavior under load or hotplug on hardware.

Further acceptance gates:
1. QEMU AC'97 playback and long-duration underrun/error tests.
2. Physical AC'97 playback and codec volume validation.
3. Intel/AMD HDA playback/capture, jack sensing and codec matrix.
4. USB Audio 1.0/2.0 playback/capture, isochronous feedback and hotplug.
5. HDMI/DP sink ELD and audio lifecycle across modesets.
6. Bluetooth A2DP/HFP pairing, codec negotiation, latency, reconnect and sleep/wake.
7. Multi-application mixing, per-app volume, capture permissions and concurrent stream stress.
8. Loopback/frequency-response, underrun rate, scheduling latency, CPU cost and long-session stability measurements.
