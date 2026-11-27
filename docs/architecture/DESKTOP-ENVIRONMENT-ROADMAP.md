# Nexus Desktop Environment — Implementation Status and Roadmap

## Implemented in this increment

The desktop remains a freestanding, framebuffer-driven kernel shell. The implementation favors fixed-size data structures, synchronous bounded drawing and a small memory footprint. It does not add a heavyweight UI framework or heap-backed widget tree.

- **Taskbar and launcher:** Retains the Nexus taskbar and searchable Start-style launcher. The taskbar has four workspace selectors and Quick Settings, Notification Center, Clipboard History and Task Manager panel shortcuts. Six built-in app shortcuts are available from the home workspace and Start menu. App tabs remain bounded to the available screen width.
- **Workspace model:** Four independent workspace snapshots retain the active app view and each app's placement state. Keyboard and taskbar switching activate the selected workspace. Workspace state is currently volatile and is not yet saved across reboot.
- **Window service:** A fixed-capacity window manager tracks IDs, titles, workspace ownership, visibility, minimized/maximized state, focus/z-order, bounded geometry and left/right/maximized snapping with restore bounds. The existing shell still paints one foreground app window at a time; it is not yet a full overlapping multi-window compositor.
- **Move and resize:** The existing pointer-driven drag, resize, maximize and edge-snap behavior remains active for the foreground application window.
- **Quick Settings:** Provides theme, high-contrast, wallpaper and volume controls plus a truthful wired/network DHCP status line. The service uses the existing persisted Nexus desktop preferences for supported settings.
- **Notification center:** Adds a bounded 12-entry notification ring, unread tracking, newest-first display, mark-read and clear behavior. Notifications are currently volatile and do not yet provide a general application notification permission/API.
- **Clipboard:** Adds a bounded 8-entry text history with a 255-byte item limit, duplicate suppression, clear and newest-first retrieval. Ctrl+C and Ctrl+V work for supported text fields in Files, the Browser and Terminal; Ctrl+Shift+V opens the history panel. This is a shell-level text clipboard, not a binary/image clipboard or a shared userspace clipboard daemon.
- **Global search:** Win+S opens a keyboard-driven search surface over app entries, common settings and the current Files directory listing. Results are case-insensitive, bounded and ranked with title-prefix matches first; selecting an app opens it, selecting a file opens Files with that name filtered, and selecting a setting opens Settings. The Files view retains its own search. The index is current-directory only, not a recursive or full-volume index.
- **Themes and accessibility:** Existing dark/light themes, 8 accent colors, 4 gradient wallpapers, high-contrast mode, UI scale, cursor size, mouse sensitivity, scroll speed and left-handed mouse support remain. Quick Settings exposes common controls.
- **Desktop shortcuts and widgets:** The home workspace exposes six built-in app shortcuts (Files, Browser, Settings, Terminal, Installer and App Center) and four live at-a-glance cards for free memory, uptime, storage availability and retained process count. User-created desktop links, arbitrary icon placement and persistent shortcut editing are not yet implemented.
- **Task Manager:** A system-tray panel displays the actual bounded process table (PID, name and state) and retained-process count. It is deliberately read-only because the process API has no safe process-termination operation. The existing App Center remains the application catalogue/manager for signed packages and web shortcuts.
- **Screen capture:** Ctrl+Shift+P reads the active framebuffer in bounded 128-pixel chunks and streams a P6 PPM image named `SCREEN.PPM` to the current writable NXFS directory. It refuses unsupported dimensions, non-writable storage, framebuffer read errors and stream commit failures; errors are surfaced as notifications. It does not allocate a full-frame image buffer.
- **Rendering and appearance:** The shell uses the existing antialiased built-in bitmap font, gradient wallpapers, alpha blending, rounded panels, drop-shadow fills and framebuffer damage/presentation. These effects are software-rasterized. VirtIO-GPU can present/flush modified scanout regions, but the current renderer still produces pixels on the CPU; there is no general GPU-accelerated compositor or hardware 3D desktop backend.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Win + Ctrl + 1–4 | Switch to workspace 1–4 |
| Ctrl + Alt + Left / Right | Previous / next workspace |
| Ctrl + Shift + Q | Quick Settings |
| Ctrl + Shift + N | Notification Center |
| Ctrl + Shift + V | Clipboard History |
| Ctrl + Shift + Esc | Read-only Task Manager |
| Ctrl + Shift + P | Capture the desktop to SCREEN.PPM on writable NXFS |
| Win + S | Search apps, files and settings |
| Ctrl + C / Ctrl + V | Copy / paste supported text |

## Performance design

- Window, workspace, notification and clipboard tables have compile-time capacities.
- No heap allocation is required for desktop service operations.
- Search input and results have explicit maximum sizes.
- Window geometry is clamped to the active display dimensions.
- Panel rendering is only invoked when a panel is open.
- The implementation continues to use the existing framebuffer renderer and its damage/presentation path.

## Remaining work — not claimed as implemented

| Subsystem | Missing implementation | Required acceptance evidence |
|---|---|---|
| True multi-window compositor | Multiple simultaneously visible, independently composited app surfaces; per-window input routing; occlusion/damage tracking | Several native apps visible at once, independently moved/resized, with no redraw corruption |
| Desktop icons and shortcuts | User-created file/app links, icon placement, drag/drop, persistence and safe launch policy | Create/move/delete shortcuts and launch them after reboot |
| File thumbnails | Image decoders, bounded thumbnail cache, async generation and invalidation | PNG/JPEG/WebP samples render thumbnails without blocking directory navigation |
| Global search UI | Unified launcher/search overlay wired to app registry, file index and settings | Results from apps, files and settings with ranking, keyboard navigation and open actions |
| Login and lock screen | Secure credential store, boot/session authentication, lock-state enforcement, trusted input path and timeout policy | Failed-login throttling, locked-console isolation, session lock/unlock tests |
| User accounts | User database, protected credential handling, session lifecycle, account switching and recovery | Multiple isolated accounts with verified login/logout/recovery |
| Permissions | VFS ownership/modes or ACLs, process identity, privilege separation and permission checks on every file/process boundary | Cross-user file/process access tests proving unauthorized operations fail |
| Clipboard service | General app-facing IPC API, ownership, pasteboard change notifications and sensitive-data policy | Two independent apps exchange text with permission and lifetime tests |
| Notifications | General user-space API, app identity/permissions, delivery queue and action dispatch | Sandboxed native app sends actionable notification and user interaction dispatches safely |
| Fonts and icons | Font file loader, glyph rasterizer/cache, icon asset format and scalable rendering | Unicode/font fallback tests and theme-consistent scalable icon assets |
| Animations | Frame scheduler, reduced-motion setting and bounded transitions that do not block input | Frame pacing and input latency benchmarks on software and VirtIO display paths |
| Screen recording | Capture scheduler, encoder/codec pipeline, muxer and bounded video file writer | Playable desktop recording with audio/video synchronization and dropped-frame reporting |
| Login and lock screen | Secure credential store, trusted input, authentication and protected session state | Locked-console isolation, login-failure throttling and reliable unlock tests |
| Multi-monitor desktop | Multiple active scanouts, display output manager, per-monitor workspaces/scaling and pointer/window migration | Two physical or virtual displays with hotplug and independent desktop layouts |
| Multi-monitor | Output discovery, per-display workspaces, scaling and window placement migration | Two physical/virtual displays with hotplug and reliable pointer movement |
| File-manager search | Recursive/indexed search, filters and metadata cache beyond current folder listing | Search across directories with bounded I/O and cancellation |
| Settings | More controls for network profiles, devices, power, accounts and per-app permissions | Persistence, validation and rollback tests for each supported control |

## Security boundary

Nexus currently does not have a secure multi-user identity and authorization system. The workspace selector is organization of a single interactive session, not a security boundary. It must not be described as account isolation. The current clipboard is volatile kernel-shell state and must not be used to handle passwords or other secrets until access controls and secure clearing are implemented.

## Explicit capability limits

The desktop still has one active display surface. There is no secure account database or trusted lock/login path, so virtual workspaces are organization inside one interactive session and are not user isolation. Screen recording is not implemented because there is no bounded frame-capture scheduler, video encoder or audio/video muxer. The current GPU path accelerates scanout/presentation only; scene rasterization remains CPU-based. The Task Manager is informational because the current process API does not expose safe process termination.

## Release validation

Host regression tests exercise the fixed-capacity window/workspace manager, snapping and restore geometry, notification ring, clipboard ring, bounds validation and cross-category search ordering. CI also compiles the freestanding kernel. These checks do not replace an installed-ISO boot test, interactive hardware testing, accessibility evaluation or multi-display validation.
