# Nexus OS Networking, HTTPS, Browser, and Wi-Fi Engineering Specification

**Status:** Active implementation. Nexus has a freestanding TCP/HTTP stack, a pinned Mbed TLS 4.1.2 TLS 1.3 adapter, certificate and hostname verification, a bounded crypto allocator, and an HTTPS-first browser with web-search form submission and selectable text results. The release build now embeds a checksum-verified Mozilla-derived CA root bundle. Physical Wi-Fi drivers and a modern HTML/CSS/JavaScript browser engine remain incomplete.

## Verified current boundary

The browser's secure search providers use HTTPS and parse bounded, server-rendered HTML result links. Bing is tried first and DuckDuckGo HTML is attempted once when the first provider returns an unusable response. The release build downloads the Mozilla-derived CA PEM from curl's CA Extract service, verifies the published SHA-256 digest, and embeds the immutable file in the TLS-enabled kernel. A valid NEXUS.CRT file on the writable Nexus Data volume may override the built-in store. The trust source, licensing, and policy limits are documented in docs/security/CA-TRUST-STORE.md.

The IPv4 DNS client validates the active transaction's question and answer framing, follows a bounded CNAME chain, rejects malformed compression pointers, retries unanswered queries with bounded exponential backoff, and keeps a small positive A-record cache. Cached entries honor the shortest TTL in the CNAME/A chain, cap storage at five minutes, and are cleared when the selected link or adapter changes. This is not a full recursive resolver, negative cache, or IPv6 resolver.

The TCP receive path accepts a bounded number of out-of-order segments and reassembles them into the application FIFO once missing bytes arrive. The advertised receive window reflects occupied FIFO and reassembly space; reading buffered bytes sends a window-update ACK so peers can continue large transfers. This remains a small kernel TCP implementation, not a complete general-purpose TCP stack with all congestion-control and option support.

The browser's HTTPS reader does not treat a short idle interval as end-of-response. It waits for the HTTP decoder to confirm a complete Content-Length or chunked body, or for authenticated TLS close-notify on close-delimited responses, subject to a bounded overall deadline. The QEMU live-desktop acceptance test now boots Nexus, submits a real HTTPS search, and verifies that nonzero search results are rendered. This verifies the online-search path in the emulator; it does not establish universal hardware/network compatibility or Chrome-equivalent HTML/CSS/JavaScript support.

The current browser remains text-oriented: it does not implement CSS layout, JavaScript, cookies, tabs, or a modern DOM engine. The Wi-Fi state manager and 802.11 parser do not make arbitrary wireless chipsets usable; no general physical Wi-Fi driver matrix is yet established.

Do not describe Nexus as Windows/Chrome-equivalent or claim all Wi-Fi hardware works. HTTPS support must still pass the release build, emulator, and physical hardware acceptance tests.

## Layered architecture

1. **Device/DMA:** PCI and USB identity, BAR validation, bounded DMA, interrupt routing, cache coherency, reset and timeout handling. Do not enable bus mastering until DMA ownership and isolation are defined.
2. **Link:** Ethernet first; wireless drivers behind a stable network-device interface. Driver callbacks must report state safely and define packet-buffer ownership.
3. **Network:** Ethernet framing, ARP, IPv4, IPv6, ICMP, routing and MTU policy.
4. **Transport:** UDP and TCP with retransmission, congestion control, sequence validation, socket lifetime and bounded resource accounting.
5. **Name/time:** DNS resolver with cache and TTL; monotonic clock and trustworthy wall-clock bootstrap. Certificate checks fail closed when time is unknown or invalid.
6. **Security:** maintained TLS 1.3 implementation, certificate-chain validation, hostname verification, trusted-root lifecycle, secure randomness and explicit errors.
7. **Userland:** network configuration, connection manager, resolver, certificate updates, and documented socket/TLS APIs.
8. **Browser:** use the userland networking APIs; no private bypass around certificate validation.

## HTTPS and TLS requirements

- Integrate a maintained TLS implementation only after license, update, memory-allocation, entropy, and porting review. Use a narrow adapter; do not write cryptographic primitives from scratch.
- TLS 1.3 is the baseline. TLS 1.2 may be enabled only with an approved cipher-suite policy. Disable SSL, TLS 1.0/1.1, obsolete algorithms, weak key exchange and insecure renegotiation.
- Use a cryptographically secure random source with a reviewed entropy health policy. Never substitute timestamps, counters, or ordinary rand() for cryptographic entropy.
- Validate certificate chains to an installed trust store, validity periods, requested hostname/SAN, key usage and algorithm constraints.
- Unknown time, missing trust roots, expired certificates, hostname mismatch, invalid signatures and malformed chains must produce explicit errors. Never silently retry as plaintext HTTP.
- Trust roots need authenticated updates and rollback protection. Keep credentials and session secrets out of logs and crash output.
- Test memory bounds, parser fuzz cases, truncated records, oversized certificates, timeouts, resets and invalid certificates.

### HTTPS acceptance tests

- Valid TLS 1.3 handshake and authenticated HTTPS GET.
- TLS 1.2 only when approved by policy.
- Correct hostname and trusted chain succeed; untrusted root, expired certificate, hostname mismatch, invalid signature and malformed chain fail closed.
- Invalid system time blocks or clearly reports validation failure.
- No plaintext retry after TLS failure.
- Fragmented records, partial reads/writes, EOF, timeout, reset, large responses and repeated connect/close cycles.

## Windows driver package import (initial compatibility layer)

Nexus includes a bounded Windows INF model-section reader for PCI and USB hardware IDs. It can compare an installed adapter's vendor/device IDs, PCI subsystem IDs and revision against an INF package, then report the matching model description and install section. The reader treats INF text as untrusted metadata: it does not execute INF directives, copy arbitrary files, load PE images, or grant kernel privileges. Long lines and oversized output fields are rejected.

The next user-facing step is a driver-package assistant that accepts a vendor-supplied driver archive, inventories its INF and firmware files, verifies hashes/signatures where applicable, and displays exact adapter matches before installation. Windows catalog signatures and package provenance must be verified; an INF match alone is not proof that a driver is safe or compatible.

Actual Windows Wi-Fi driver execution requires a separate, isolated NDIS compatibility subsystem: PE/COFF loading, Windows kernel API/IRQL/timer/work-item/DMA abstractions, NDIS miniport lifecycle and packet APIs, synchronization semantics, firmware loading, power management, and a stable Nexus network-device adapter. It must not run arbitrary Windows kernel code in the Nexus kernel. Start with a restricted, isolated driver host and a small, explicitly supported NDIS API surface; unsupported imports must fail closed. Do not advertise generic Windows .sys support until physical adapters pass the full test matrix.

## Wi-Fi architecture and compatibility

Wi-Fi is a hardware enablement program, not one generic driver. Maintain a public compatibility matrix keyed by PCI/USB vendor and device IDs, revision, firmware filename/version, bands, security modes and physical test status.

Implementation sequence:

1. Complete PCI/USB enumeration and a safe device/firmware inventory.
2. Stabilize the network-device interface and Ethernet path; validate DMA, interrupts, packet ownership, link state and recovery.
3. Add a connection manager and supplicant boundary with explicit scan, authenticate, associate, DHCP, disconnect and reconnect states.
4. Integrate one chipset family at a time after driver-license and firmware redistribution review.
5. Add WPA2-Personal with AES-CCMP and WPA3-Personal with SAE. Do not describe WEP or TKIP as secure. Enterprise 802.1X/EAP needs a separate review.
6. Expand coverage using exact hardware IDs and physical test reports.

The API must expose scan results, BSSID/SSID, channel/band, signal metrics, authentication result, firmware errors and disconnect reason. Credentials must never appear in logs. Report association separately from address acquisition and internet reachability. Support 2.4/5/6 GHz only where adapter, access point and regulatory domain permit it.

### Wi-Fi acceptance tests

For each adapter, record exact model/revision, firmware source/hash, driver version, test machine and date. Test enumeration, firmware failure, scan, hidden SSID policy, authentication, association, DHCP, WPA2/WPA3 where supported, IPv4/IPv6, DNS, HTTPS, roaming, sleep/resume, radio toggling, access-point restart, signal loss, repeated reconnects and credential confidentiality.

Do not claim all Wi-Fi supported. Publish tested adapters and known limitations. Respect regulatory domain and transmit-power rules.

## Browser compatibility strategy

HTTPS alone does not make modern websites work. A Chrome-class browser requires a maintained HTML parser/DOM, CSS layout and painting, JavaScript engine, image/font/media decoders, storage/cookies, Fetch APIs, origin/security model, accessibility, sandboxing and continual web-platform updates.

Evaluate a maintained browser engine against the Nexus ABI, process model, memory manager, graphics stack, IPC, sandbox and update mechanism. Do not attempt to implement a complete web platform from scratch. Pin the engine version and provide security updates. If a full engine cannot run natively yet, call it a future milestone rather than claiming compatibility.

Minimum browser tests include redirects, status handling, content encodings, large downloads, HTML5 parsing, DOM mutation, CSS layout, fonts, SVG, canvas, image formats, modern JavaScript, cookies, SameSite, secure storage, origin isolation, permissions, tabs, history, bookmarks, downloads, renderer/network isolation, and standards plus real-site compatibility suites.

## Delivery sequence and release gates

**N0 — Network foundations:** safe DMA/network-device contract, Ethernet driver, packet buffers, ARP, IPv4/IPv6, UDP/TCP and host tests.

**N1 — Userland networking:** socket API, DNS, DHCP, routing, clock bootstrap, diagnostics and network settings.

**N2 — Verified HTTPS:** secure entropy, TLS adapter, trust store, certificate validation, authenticated requests and negative tests. Gate: HTTPS tests pass in CI and on a physical Ethernet device.

**N3 — Wi-Fi:** connection manager, supplicant, first chipset driver and firmware, WPA2/WPA3 tests. Gate: exact adapter passes repeated physical tests and is listed in the matrix.

**N4 — Browser platform:** select and port a maintained engine; implement sandbox and update pipeline. Gate: standards tests and documented website compatibility pass.

**N5 — Broad hardware coverage:** add drivers based on demand and exact PCI/USB IDs. Gate each adapter independently; never infer support for untested devices.

A bootable ISO, successful QEMU boot, raw HTTP request or visible browser window does not prove these gates have passed.
