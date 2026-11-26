# Nexus Desktop Environment — Implementation Status and Roadmap

## Implemented in this increment

The desktop remains a freestanding, framebuffer-driven kernel shell. The implementation favors fixed-size data structures, synchronous bounded drawing and a small memory footprint. It does not add a heavyweight UI framework or heap-backed widget tree.

- **Taskbar and launcher:** Retains the Nexus taskbar and searchable Start-style launcher. The taskbar now has four workspace selectors and Quick Settings, Notification Center and Clipboard History panel shortcuts. App tabs remain bounded to the available screen width.
- **Workspace model:** Four independent workspace snapshots retain the active app view and each app's placement state. Keyboard and taskbar switching activate the selected workspace. Workspace state is currently volatile and is not yet saved across reboot.
- **Window service:** A fixed-capacity window manager tracks IDs, titles, workspace ownership, visibility, minimized/maximized state, focus/z-order, bounded geometry and left/right/maximized snapping with restore bounds. The existing shell still paints one foreground app window at a time; it is not yet a full overlapping multi-window compositor.
- **Move and resize:** The existing pointer-driven drag, resize, maximize and edge-snap behavior remains active for the foreground application window.
- **Quick Settings:** Provides theme, high-contrast, wallpaper and volume controls plus a truthful wired/network DHCP status line. The service uses the existing persisted Nexus desktop preferences for supported settings.
- **Notification center:** Adds a bounded 12-entry notification ring, unread tracking, newest-first display, mark-read and clear behavior. Notifications are currently volatile and do not yet provide a general application notification permission/API.
- **Clipboard:** Adds a bounded 8-entry text history with a 255-byte item limit, duplicate suppression, clear and newest-first retrieval. Ctrl+C and Ctrl+V work for supported text fields in Files, the Browser and Terminal; Ctrl+Shift+V opens the history panel. This is a shell-level text clipboard, not a binary/image clipboard or a shared userspace clipboard daemon.
- **Search primitives:** Adds a bounded, case-insensitive search API over app, file and setting metadata, prioritizing title-prefix matches. The Files view retains its own search. A unified interactive global search surface and full-volume index are not yet connected.
- **Themes and accessibility:** Existing dark/light themes, 8 accent colors, 4 gradient wallpapers, high-contrast mode, UI scale, cursor size, mouse sensitivity, scroll speed and left-handed mouse support remain. Quick Settings exposes common controls.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Win + Ctrl + 1–4 | Switch to workspace 1–4 |
| Ctrl + Alt + Left / Right | Previous / next workspace |
| Ctrl + Shift + Q | Quick Settings |
| Ctrl + Shift + N | Notification Center |
| Ctrl + Shift + V | Clipboard History |
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
| Multi-monitor | Output discovery, per-display workspaces, scaling and window placement migration | Two physical/virtual displays with hotplug and reliable pointer movement |
| File-manager search | Recursive/indexed search, filters and metadata cache beyond current folder listing | Search across directories with bounded I/O and cancellation |
| Settings | More controls for network profiles, devices, power, accounts and per-app permissions | Persistence, validation and rollback tests for each supported control |

## Security boundary

Nexus currently does not have a secure multi-user identity and authorization system. The workspace selector is organization of a single interactive session, not a security boundary. It must not be described as account isolation. The current clipboard is volatile kernel-shell state and must not be used to handle passwords or other secrets until access controls and secure clearing are implemented.

## Release validation

Host regression tests exercise the fixed-capacity window/workspace manager, snapping and restore geometry, notification ring, clipboard ring, bounds validation and cross-category search ordering. CI also compiles the freestanding kernel. These checks do not replace an installed-ISO boot test, interactive hardware testing, accessibility evaluation or multi-display validation.
