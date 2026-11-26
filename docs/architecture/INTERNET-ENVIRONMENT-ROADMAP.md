# Nexus Internet Environment — Implementation Status and Porting Plan

## What the current system actually supports

The working wired path is a freestanding kernel networking stack. It is not a Linux network stack and not a general-purpose userspace socket service.

- **Network devices:** Intel e1000, Realtek RTL8139 and VirtIO-net drivers register Ethernet-II frame interfaces. The net-device layer selects a live link and restarts addressing when the active adapter changes. Hardware support is limited to these implementations; this is not a universal Ethernet driver.
- **IPv4:** Ethernet, ARP, IPv4 framing/checksums, DHCP-provided gateway routing, UDP and one stop-and-wait TCP connection are present. A bounded ICMPv4 echo request/reply primitive supports ping diagnostics with identifier/sequence/payload matching and a caller-supplied timeout. IPv4 fragmentation is rejected; ICMP error handling, path-MTU discovery, IPv6, a general routing table and multiple simultaneous TCP sockets are not implemented.
- **DHCPv4:** Dynamic addressing supports Discover/Offer/Request/Ack/NAK, retry, lease duration, T1 renew, T2 rebind and expiry. T1 renewal is unicast to the current lease server; T2 rebind is broadcast and can be accepted from another DHCP server. Invalid, expired or NAKed leases clear the stale IP, gateway, DNS, ARP cache, TCP session and resolver cache. Missing lease options default to one hour with T1 at 50% and T2 at 87%; infinite leases are retained without renewal. Static address configuration, user-selected DNS, DHCPv6 and IPv6 SLAAC are not implemented.
- **DNS:** Bounded, transaction-checked IPv4 A-record queries with CNAME resolution, positive TTL cache and bounded retry are implemented. There is no AAAA/IPv6, DNS-over-HTTPS, DNSSEC validation, negative cache or configurable per-interface resolver policy.
- **TLS:** HTTPS uses pinned Mbed TLS 4.1.2 with TLS 1.3, a built-in Mozilla-derived CA root bundle, certificate-chain verification and hostname validation. Unknown/bad certificates must fail closed. The HTTP layer supports bounded HTTP/1.1 framing and selected response handling. It is not HTTP/2 or HTTP/3.
- **Wi-Fi:** The kernel has an 802.11 beacon/security parser, WPA2-PSK/CCMP primitives and a Wi-Fi driver-registration/state interface. PCI discovery and Windows INF/PE inspection are inventory/assessment tools only. No supported physical Wi-Fi chipset driver is registered by the current boot path; scanning, associating and obtaining DHCP on arbitrary laptop Wi-Fi hardware are not presently usable.
- **Bluetooth:** No Bluetooth controller transport, HCI, L2CAP, RFCOMM, PAN, pairing or settings service is implemented.
- **Browser:** The current built-in browser performs HTTPS requests, search-provider fallback, bounded HTML text extraction, selectable links, history and streamed downloads. It has no modern DOM/CSS layout, JavaScript engine, WebAssembly, cookies, service workers, tabs, extension runtime, general web-app platform or broad form support. Calling it Chrome-equivalent would be inaccurate.
- **Shared use:** Browser requests and the app-management download service use the same kernel TCP/DNS/TLS mechanisms. The current TCP/TLS implementation has a single active connection and must not be treated as a concurrent socket API. Installed apps do not yet have a documented general-purpose networking ABI.

## DHCP lease lifecycle added in this increment

The DHCP client exposes public states: idle, discovering, requesting, bound, renewing and rebinding. The network information API reports the active interface/medium, link status, DHCP state, lease duration, remaining lease seconds and time until renewal. Network consumers treat an address as usable while the lease is bound, renewing or rebinding and before its expiry deadline.

Host regression tests exercise:
- DHCP ACK and option decoding for lease, T1 and T2.
- Default T1/T2 when options 58/59 are absent.
- Unicast T1 renewal with ciaddr, without SELECTING-only requested-IP/server-ID options.
- T2 broadcast rebinding with the current ciaddr.
- A different DHCP server answering the rebind.
- Infinite leases, DHCP NAK, lease expiry and route/DNS/ARP/TCP state cleanup.

The System settings panel now displays the actual active interface medium, a more accurate DHCP/renewal status and lease time remaining. It does not present an unregistered Wi-Fi adapter as connected.

## Maintained browser-engine evaluation

A real browser engine should be ported, not reimplemented in Nexus. The current OS does not yet offer the platform ABI that the candidate engines expect.

### Recommended target: WPE WebKit

WPE is WebKit's embedded Linux port. The current WPE 2.54 release makes WPEPlatform the default, stable integration API and provides platform integration for DRM/KMS, Wayland and headless environments. This is the most relevant long-term direction for a console/embedded-style Nexus browser because WPE is explicitly designed as a toolkit-independent embedded WebKit runtime.

It is not a drop-in binary for today's Nexus kernel. A WPE port requires a substantial runtime foundation: C/C++ shared library ABI, GLib/GObject and related dependencies, a supported POSIX/Linux platform layer, file and socket APIs, process/thread support, IPC, memory mapping, graphics buffer integration, input, fonts, image decoding and multimedia integration. Nexus should create a Linux/POSIX compatibility environment or a dedicated, maintained WPE platform port before claiming HTML5/CSS/JavaScript/WebAssembly compatibility.

### Other candidates

- **Servo** is a maintained Rust browser engine with an embedding/WebView direction. Its supported platform list currently names 64-bit Linux, macOS, Windows, OpenHarmony and Android, not a freestanding Nexus target. Its embedding documentation also describes itself as sparse and a work in progress. It remains an alternative for a future Rust-capable userspace, but cannot simply be linked into the current freestanding C kernel.
- **Ladybird** now has an alpha for Linux/macOS. Its documented platform ports use Linux/macOS abstractions; porting another OS requires implementations in its core/platform layer, process and socket APIs, and its IPC depends on Unix-domain sockets. Its alpha status and current OS dependencies make it a later candidate, not a ready-made Nexus component.

### Browser port gates

1. Establish a stable userspace ABI, dynamically linked ELF loader, shared-library loader, memory mapping, threads, process lifecycle and crash containment.
2. Implement a documented network socket API, asynchronous I/O/polling, DNS resolver service and shared TLS/certificate service. Do not let every app invent a private route to TCP or bypass certificate validation.
3. Implement authenticated system time, a trust-store updater with rollback protection and secure random/entropy health policy.
4. Implement process isolation, IPC, capability-based file/network permissions, sandbox/resource limits and a renderer crash boundary.
5. Integrate WPEPlatform behind a browser-process abstraction, then add standards tests for HTML, DOM, CSS, JS, WebAssembly, Fetch, cookies, storage, media, permissions, origins and real sites.
6. Use a maintained update channel for the engine and security fixes. Never bundle a one-off engine build without an update strategy.

## Remaining internet milestones

| Area | Next deliverable | Acceptance condition |
|---|---|---|
| IPv4 / DHCP | Add manual/static IPv4 and DNS configuration with safe validation and persistent profiles | Reboot persistence, gateway/DNS reachability and DHCP fallback |
| ICMP | Add ICMP error handling and diagnostics in the desktop network panel; current echo probe is API-only | Verified echo, unreachable and time-exceeded handling with explicit UI results |
| IPv6 | Add IPv6 Ethernet/IP parsing, ICMPv6/ND, SLAAC, DHCPv6 where needed, DNS AAAA and dual-stack route selection | IPv6-only and dual-stack network tests |
| TCP | Implement concurrent sockets, retransmission timers, congestion control, options, orderly shutdown, TIME_WAIT and robust connection cleanup | Parallel TLS + update + app transfers without shared-connection corruption |
| Network service | Stable userspace socket/resolver/TLS APIs and network-change notifications | Browser, Store and installed apps use one service and recover from link loss |
| Ethernet | Expand controller/PHY/PCI coverage using exact device-ID tests | Physical link, DHCP, DNS and HTTPS per supported adapter |
| Wi-Fi | Integrate and physically validate one chipset family with firmware, scan, WPA2/WPA3, connect, DHCP and reconnect | Exact adapter matrix with no credential logging |
| Wi-Fi UI | Network discovery, SSID/security/signal, password prompt, connect/disconnect, saved profile protection and troubleshooting | Tested end-to-end using a supported physical radio |
| Bluetooth | HCI transport, controller discovery, pairing, security manager, supported profiles such as PAN | Verified adapters and authenticated network-sharing tests |
| Browser | Port a maintained engine (WPE first candidate) after userspace/platform gates | WPT/BCD coverage, modern JavaScript/CSS/WebAssembly, real-site tests, security update path |
| Downloads/uploads | Shared transfer service with resume/range, atomic file writes, progress, user consent and upload permissions | Multi-hundred-MiB tests and interrupted-transfer recovery |
| Application networking | Documented socket, DNS and TLS APIs for native applications | Test app resolves, connects and validates TLS without browser-private hooks |

## Security and UX requirements

- Never downgrade HTTPS to plaintext after a certificate, DNS or TLS failure.
- Never persist a Wi-Fi passphrase in an ordinary settings file. Use a protected credential store backed by appropriate key management before offering "remember network".
- Display link, DHCP, DNS and HTTPS reachability as separate states. An adapter with link but no lease is not online; a lease with failed DNS is not internet ready.
- Provide explicit errors: no adapter, link down, DHCP timeout/NAK/expired, DNS timeout/NXDOMAIN, TCP timeout/reset, TLS trust/time/hostname error and HTTP status failure.
- Reconnect and update tasks must not block the entire UI indefinitely. The existing single-connection synchronous stack is a prototype, not an asynchronous networking API.
- Wi-Fi support is per chipset/firmware and must be backed by physical test results. PCI inventory or a matching Windows INF does not mean the device works.
- Browser engines and their security updates must be versioned and maintained. A custom HTML parser or search-results renderer is not a modern browser engine.
