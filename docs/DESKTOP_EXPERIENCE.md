# Nexus desktop experience

## Visual system

The kernel desktop now uses one shared visual language across the built-in
applications: a deep graphite window surface, a theme-aware accent rail,
soft rounded cards, a compact Nexus-branded title bar, and a consistent native
application icon. The five persisted palettes are Midnight, Ocean, Ember,
Violet and Forest. Five wallpaper styles remain available: Aurora, Gradient,
Solid, Grid and Orbit.

The built-in 5x7 font now includes distinct lowercase letters and common
punctuation rather than converting lowercase input to uppercase. Larger
headings use the framebuffer's antialiased text routine. This remains a
fixed-size bitmap font; font shaping, full Unicode, font discovery and
resolution-independent UI scaling are not implemented.

## Applications

- **Home and launcher:** the desktop keeps the Nexus orb, app icon dock,
  searchable Start menu, workspace switcher and live system snapshot.
- **Settings:** palette swatches preview the five accent families. Seven
  existing controls remain available by clicking their rows or using keys
  1-7: color theme, wallpaper, taskbar status, window sizing preference,
  network interface selection, high contrast and reduced motion. Preferences
  are saved to writable NXFS when available; otherwise they are session-only.
- **Control Center:** six quick-setting tiles expose theme, wallpaper,
  high-contrast, reduced-motion, taskbar-status and the full Settings screen.
  Keys 1-5 activate the first five actions; Enter opens Settings. Clicking
  the quick-setting tiles performs the same actions.
- **Files:** the summary shows item count and a truthful storage capability
  label (persistent NXFS, read-only boot volume or RAM/session storage).
  Entries carry explicit directory/file markers. Existing file create, open,
  edit, copy, rename, delete, sort, search and navigation operations retain
  their keyboard/mouse paths and safeguards. The list remains bounded.
- **System Monitor:** stat cards show total/free memory, uptime and block
  devices; a memory utilization meter is computed from kernel memory totals.
  Additional cards show data-volume status, active network interface/IP
  configuration status, PCI WLAN candidate count, USB inventory count and
  the explicit state that no native Wi-Fi radio driver is installed.
- **Terminal:** the command panel presents the current command result and
  input prompt in a dedicated shell surface. The HELP, MEM and NET quick
  tiles are clickable and execute the corresponding existing commands. The
  terminal remains a small built-in command interface, not a POSIX shell.
- **Calculator:** the expression/result panel and action-key styling have
  clearer hierarchy. The existing bounded integer expression engine and
  mouse/keyboard keypad behavior are unchanged.
- **Nexus Browser:** the start screen has a Nexus-branded orb, wordmark,
  custom start panel and native address toolbar. Loaded text pages are marked
  PLAIN HTTP so users are not led to believe the connection is encrypted.
  Its rendering and networking limits are documented separately in
  `NEXUS_BROWSER.md`.
- **Installer:** the graphical selection step emphasizes the destination disk
  and firmware mode; the confirmation panel highlights that installation is
  destructive and keeps the existing explicit Confirm & Install / Cancel
  actions. The selected drive's identity, mounted/boot-media state and write
  eligibility continue to be checked by the installer code.

## Current engineering limits

The built-in applications still run inside the kernel desktop loop and each
application currently has one live window instance. This is not a mature
third-party application platform. The text renderer is not a web browser
layout engine; the kernel does not yet supply the full text shaping, audio,
GPU compositor, accessibility tree, clipboard service or display scaling
expected from a production graphical OS. Visual enhancements do not change
those platform limits.

Do not describe the system as production-ready until the remaining platform,
hardware support, security, installer release gates and real-PC validation
have been completed.
