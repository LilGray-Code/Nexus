# Nexus Browser

## Product decision

Nexus will ship its own native browser, named **Nexus Browser**. It is a first-party application designed for the standalone Nexus operating system. It will not bundle Google Chrome, Chromium, a Linux browser binary, or delegate browsing to another operating system.

The target is a modern, general-purpose browser with the core capabilities people expect from Chrome: secure websites, tabs, history, bookmarks, downloads, forms, modern JavaScript applications, media, accessibility, private browsing, and safe multi-process site isolation. The implementation, UI, networking integration, runtime integration, and packaging are owned by the Nexus project.

“Fully custom” does not mean writing every standards engine from scratch without dependencies. Any third-party libraries or standards components must be deliberately selected, audited, licensed compatibly, built for Nexus, and included in the provenance and security records. The browser shell and OS integration remain Nexus-native. No Chrome or Chromium source/binary is silently relabeled as Nexus.

## Current implementation (not release-ready)

The desktop contains an early text-oriented HTTP client:
- one address field and a small fixed text display;
- DNS lookup and a single TCP connection;
- an HTTP/1.0 request to port 80;
- basic response-body text extraction.

It is not a modern web browser. It has no TLS/HTTPS, certificate validation, HTML/CSS layout engine, JavaScript engine, tabs, browser storage, downloads manager, extensions, media, sandbox, or site isolation. The current kernel networking path is early IPv4/DHCP/DNS/TCP and is not a secure browser transport.

The existing client must be treated as a prototype only. Do not advertise secure browsing or modern web compatibility until the relevant implementation and tests exist.

## Architecture to build

### 1. Native browser application and window model
- Nexus-native application lifecycle, independent from the kernel desktop event loop.
- Multi-window and multi-tab model with address/navigation controls, back/forward, reload/stop, tab creation/close, and keyboard navigation.
- Browser UI services for bookmarks, history, downloads, permissions, settings, and private sessions.
- Stable browser IPC and OS service interfaces; no blocking network or rendering work on the UI thread.

### 2. Web platform and rendering engine
- Standards-based HTML parser and DOM with robust malformed-input handling.
- CSS parser, cascade, selectors, layout, painting, scrolling, and responsive viewport behavior.
- JavaScript engine with modern language semantics, garbage collection, Web APIs, and event loop.
- Font shaping, Unicode, bidirectional text, image decoding, SVG, canvas, and accessibility tree.
- Incremental loading, resource scheduling, caching, and predictable memory limits.
- Standards-based conformance tests (HTML, CSS, ECMAScript, URL, Fetch, DOM) plus a documented compatibility matrix.

The project must make an explicit engine strategy decision: build and maintain a native Nexus engine, or port/adapt selected permissively licensed standards components. A text renderer or a home-grown subset must never be represented as equivalent to Chrome.

### 3. Secure networking
- URL parsing and canonicalization, HTTP/1.1 and HTTP/2, and HTTP/3 where transport support permits.
- TLS 1.2/1.3, modern cipher suites, secure random generation, hostname validation, certificate-chain validation, revocation/update policy, and trusted-root management.
- DNS, IPv4 and IPv6, TCP, UDP, congestion control, timeouts, redirects, cookies, content encodings, and connection pooling.
- Mixed-content blocking, secure-context rules, HSTS, origin isolation, and safe download handling.
- Never send credentials or private user data over plaintext HTTP. HTTPS failures must be visible and fail closed.

### 4. Browser security model
- Separate browser UI, network, and renderer processes with per-site isolation.
- Memory-safe IPC, origin/process boundaries, permission mediation, sandboxed renderers, and least-privilege system calls.
- Site permissions for camera, microphone, location, notifications, clipboard, and downloads, default-deny where appropriate.
- Safe browsing/download reputation strategy, phishing/malware warnings, content security policy, and secure cookie/storage rules.
- Signed updates, rollback protection, vulnerability disclosure, and a maintained security response process.

### 5. Web APIs and user features
- Tabs, history, bookmarks, find-in-page, downloads, password manager integration, autofill, print/print-to-PDF, and session restore.
- Cookies, local/session storage, IndexedDB, Cache API, service workers, and standards-based permissions.
- Audio/video playback, codecs with clear licensing, hardware acceleration where supported, and software fallback.
- Extensions only after a defined permission model, signing policy, and compatibility/security strategy.
- Accessibility: keyboard-only operation, screen-reader semantics, zoom, high contrast, and text scaling.

### 6. OS integration and distribution
- Native Nexus windowing, input, clipboard, fonts, audio, filesystem, network sockets, process, and memory APIs.
- Browser and engine payloads preinstalled on the release ISO and copied to the selected installation target.
- Offline first launch with all required runtime assets present.
- Reproducible build metadata, dependency/license inventory, hashes/signatures, update channel, and rollback-safe updates.

## Required platform prerequisites

The current kernel does not yet provide the general-purpose environment required by a modern browser. Before a production renderer can run safely, Nexus needs:
- persistent, preemptively scheduled user processes and threads;
- isolated address spaces, page-fault handling, memory mapping, shared libraries, and TLS;
- a stable syscall ABI, libc/runtime, synchronization primitives, timers, IPC, and file/directory semantics;
- nonblocking socket APIs, reliable TCP retransmission/windowing, IPv6, and a secure TLS implementation;
- a compositor/window service, graphics primitives, font/text shaping, audio, and complete input services.

These are OS engineering requirements, not features that can be supplied by a browser UI mockup.

## First implemented browser foundation

The kernel now includes a standalone, host-testable URL model/parser
(`kernel/src/browser_url.c`). It recognizes HTTP and HTTPS URL schemes,
validates host and numeric port syntax, applies default ports, preserves
path/query data, and strips fragments from the request target. It rejects
userinfo, bracketed IPv6 literals, unsupported schemes, whitespace, and
invalid ports rather than guessing.

This is only URL parsing and navigation-state groundwork. The desktop's
existing network path remains HTTP-only; HTTPS URLs must not be fetched until
TLS, certificate validation, and secure transport are implemented. The parser
does not provide HTML/CSS rendering, JavaScript, or browser security isolation.

## Implementation milestones and release gates

1. **Browser foundation:** define URL, navigation, document, resource, and UI state models; separate browser code from kernel desktop code; add host-side unit tests.
2. **Secure transport:** implement and test TLS, certificates, HTTPS, HTTP parsing, redirects, and safe failure behavior before any login-capable browsing is enabled.
3. **Rendering foundation:** select the engine strategy, implement HTML/CSS layout and paint pipeline, and pass a published baseline of standards tests.
4. **JavaScript and Web APIs:** integrate the runtime and event loop; test real script-driven sites and storage APIs.
5. **Isolation and features:** add process separation, sandbox, tabs, history, bookmarks, downloads, permissions, accessibility, and media.
6. **Nexus integration:** connect to native OS services, install all browser assets, and verify offline first launch and persistent profile behavior.
7. **Release validation:** test modern HTTPS sites, forms, authentication, downloads, tabs, crash recovery, malicious/malformed content, privacy controls, updates, and performance in QEMU and on supported physical PCs.

A release may claim only the browser features that have passed their corresponding tests. “Chrome-like” is the long-term product target, not a current compatibility claim. No final Nexus OS release may claim a complete browser until secure modern websites and ordinary browser workflows work on a clean standalone installation.
