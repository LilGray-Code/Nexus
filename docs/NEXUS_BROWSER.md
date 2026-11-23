# Nexus Browser

## Product decision

Nexus will ship its own native browser, named **Nexus Browser**. It is a first-party application designed for the standalone Nexus operating system. It will not bundle Google Chrome, Chromium, a Linux browser binary, or delegate browsing to another operating system.

The target is a modern, general-purpose browser with the core capabilities people expect from Chrome: secure websites, tabs, history, bookmarks, downloads, forms, modern JavaScript applications, media, accessibility, private browsing, and safe multi-process site isolation. The implementation, UI, networking integration, runtime integration, and packaging are owned by the Nexus project.

“Fully custom” does not mean writing every standards engine from scratch without dependencies. Any third-party libraries or standards components must be deliberately selected, audited, licensed compatibly, built for Nexus, and included in the provenance and security records. The browser shell and OS integration remain Nexus-native. No Chrome or Chromium source/binary is silently relabeled as Nexus.

## Current implementation (not release-ready)

The desktop contains an early text-oriented HTTP client:
- an address field and a small fixed text display;
- DNS lookup and one active TCP connection;
- HTTP/1.0 GET requests and bounded HTTP response-header parsing;
- an 8 KiB bounded HTTP response buffer and a 64-line text view with Page Up
  and Page Down scrolling;
- basic HTML-to-text extraction and up to five distinct absolute HTTP(S)
  anchor targets, opened with Alt+1 through Alt+5;
- a plain-HTTP download action (Ctrl+S) that asks for confirmation,
  requires a writable persistent Nexus Data volume at its root, requires a
  Content-Length, rejects chunked transfer and files larger than 1 MiB,
  refuses overwrites, and reads the stored file back to verify the disk write.

The search UI constructs a DuckDuckGo Lite HTTPS URL, but the browser correctly
refuses to fetch it because no TLS client or certificate validator exists.
Therefore live public search results and ordinary HTTPS sources are not
available. Relative links, automatic redirects, CSS layout, JavaScript,
cookies, tabs, browser profile storage, resumable downloads, and download
management are also missing. HTTP downloads are unencrypted and must never be
used for credentials or other private data.

This is an incremental prototype, not a modern web browser. Do not advertise
secure browsing, general-purpose downloads, or Chrome-level compatibility
until their corresponding implementations and tests exist.

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

### 6. Downloads and file handling

Nexus Browser is intended to download ordinary web files (documents, images,
archives, installers, and other user-selected content) into the user's chosen
Nexus storage location. This is a required browser feature, not an optional
extension.

The download manager must provide:
- a visible list of active, paused, completed, and failed transfers;
- progress, byte counts, transfer rate, remaining-time estimate, and cancel/retry;
- safe handling of redirects, interrupted connections, partial files, and
  insufficient disk space;
- sanitized filenames and path confinement to the selected download directory;
- temporary partial files followed by an atomic final rename after a complete
  transfer, with integrity checks where the server provides a digest;
- explicit warnings for executable or potentially dangerous file types;
- no automatic execution or installation of downloaded content;
- HTTPS certificate validation before secure downloads are allowed;
- persistent download history and recovery after browser or OS restart.

A download must never silently overwrite an existing file. The user chooses
the destination or uses a clearly displayed default Downloads folder. Browser
downloads must use the same VFS/storage APIs as the file manager so completed
files appear in Nexus Files.

**Current status:** a narrow HTTP-only download prototype now exists.
Ctrl+S re-requests the currently loaded URL after an unencrypted-transfer
warning, accepts only a bounded Content-Length response, writes to the
persistent Nexus Data root, refuses an existing filename, and verifies the
saved bytes by reading them back. It rejects HTTPS, redirects, chunked transfer,
missing length, and files over 1 MiB. It has no progress manager, pause/resume,
recovery journal, destination picker, atomic partial-file rename, checksum
validation from the origin, or post-reboot download history. It is not yet the
general-purpose secure download feature specified above; do not advertise it
as one until transfer, persistence, security, and recovery tests pass.

### 7. OS integration and distribution
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

The kernel includes host-testable URL, HTTP response, search URL, and HTML
text/link parsing modules. The URL parser validates HTTP and HTTPS schemes,
host and port syntax, applies default ports, preserves path/query data, and
strips fragments from the request target. The HTML path can collect up to five
absolute HTTP(S) anchor URLs; the desktop opens them with Alt+1 through Alt+5.
The HTTP response parser extracts Content-Length, Content-Type,
Content-Disposition filename, redirects, and chunked-transfer metadata.
Host tests cover those parsers and malformed/conflicting content lengths.

The desktop transport remains HTTP-only. Secure search and HTTPS pages/downloads
must remain blocked until Nexus has a TLS client, secure random generation,
trusted roots, certificate-chain and hostname validation, and reliable secure
transport. This prototype does not provide a standards-compliant HTML/CSS
renderer, JavaScript, or browser security isolation.

## Implementation milestones and release gates

1. **Browser foundation:** define URL, navigation, document, resource, and UI state models; separate browser code from kernel desktop code; add host-side unit tests.
2. **Secure transport:** implement and test TLS, certificates, HTTPS, HTTP parsing, redirects, and safe failure behavior before any login-capable browsing is enabled.
3. **Rendering foundation:** select the engine strategy, implement HTML/CSS layout and paint pipeline, and pass a published baseline of standards tests.
4. **JavaScript and Web APIs:** integrate the runtime and event loop; test real script-driven sites and storage APIs.
5. **Isolation and features:** add process separation, sandbox, tabs, history, bookmarks, downloads, permissions, accessibility, and media.
6. **Downloads:** implement secure transfer streaming, destination selection, safe filename/path validation, atomic file completion, persistent download history, and recovery tests.
7. **Nexus integration:** connect to native OS services, install all browser assets, and verify offline first launch and persistent profile behavior.
8. **Release validation:** test modern HTTPS sites, forms, authentication, downloads, tabs, crash recovery, malicious/malformed content, privacy controls, updates, and performance in QEMU and on supported physical PCs.

A release may claim only the browser features that have passed their corresponding tests. “Chrome-like” is the long-term product target, not a current compatibility claim. No final Nexus OS release may claim a complete browser until secure modern websites and ordinary browser workflows work on a clean standalone installation.


## Engine strategy decision: Chromium compatibility target

The Chrome-level compatibility target requires a production browser engine, not incremental extensions to the current text extractor. The selected engineering direction is to **port and maintain the open-source Chromium engine stack (Blink + V8 and required Chromium platform components) for Nexus**, built from source for the Nexus ABI. This is an architecture target, not an assertion that the port currently builds or runs.

Why this direction:
- Blink and V8 are the engine family behind Chrome's web compatibility; using the upstream standards implementation is substantially more credible than writing a new HTML/CSS/JavaScript engine from scratch.
- Chromium is an open-source project, but its full dependency graph, build system, codecs, fonts, sandbox, and platform integration must be audited and tracked. Chrome-branded proprietary services and Google account integrations are not implied.
- No prebuilt Linux/Windows Chromium binary may be relabeled as a Nexus application. The engine and platform glue must be built for Nexus and tested against Nexus system interfaces.
- Upstream security updates, CVE response, reproducible builds, dependency provenance, and a supported update cadence are release blockers, not optional maintenance tasks.

### Porting prerequisites and gates

1. Stabilize the Nexus userland ABI: processes/threads, virtual memory and shared memory, file descriptors/handles, synchronization, timers, signals or equivalent event delivery, dynamic loading, and a maintained C/C++ runtime.
2. Provide nonblocking sockets and mature TCP/IP, DNS, IPv6, TLS, certificate trust, and network-change notifications.
3. Provide compositor surfaces, GPU/software rendering interfaces, font discovery and shaping, image codecs, audio output, clipboard, keyboard/input, and accessibility services.
4. Port Chromium's build/toolchain and required third-party dependencies. Replace platform-specific Linux/Windows/ChromeOS services with explicit Nexus platform adapters; do not silently emulate unsupported APIs.
5. Bring up a single-process software-rendered development build first, then introduce sandboxed renderer/network/GPU processes only after process isolation and IPC are security-tested.
6. Run Chromium's relevant unit/browser tests plus Web Platform Tests, CSS/HTML/ECMAScript conformance, security regression tests, and a representative real-site matrix. Publish exact pass rates and known incompatibilities.
7. Only then enable profile persistence, tabs, downloads, extensions, media, hardware acceleration, and user-facing release claims in stages.

**Acceptance bar:** a clean Nexus installation must load modern HTTPS sites with script, CSS, forms, authentication, media, and responsive layouts; survive malformed/untrusted content without compromising the OS; and pass the published compatibility and security suite. A browser shell that merely resembles Chrome does not meet this bar.
