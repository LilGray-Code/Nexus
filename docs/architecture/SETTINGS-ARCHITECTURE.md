# Nexus Settings Architecture

**Current increment:** 16-category navigation, category search, persistence validation, import/export/default restore, service-backed network controls and category-specific capability/status views.

Settings categories provide a central place to inspect and configure the services that exist today. The UI must not imply a subsystem exists merely because it has a category. Controls are only active when there is an actual service/driver path; otherwise the page reports that capability as unavailable or not implemented.

## 1. Categories

The category metadata lives in `kernel/src/settings.c` and is indexed by a stable enum in `settings.h`. Search is case-insensitive and matches both the visible category name and its search-term index.

1. **Personalization** — theme, accent color and wallpaper. Export, import and restore-default actions are also shown here.
2. **Display** — live desktop text scaling and pointer size. Current screen dimensions and rendering path are reported. Resolution/refresh/mode changes are not offered until a safe display modeset service is available.
3. **Sound** — master volume/mute, active playback/capture endpoint selection, per-stream mixer levels and mute. The UI uses the audio core APIs.
4. **Network & Internet** — Ethernet link/IP status from the network stack; Wi-Fi status, scan, selected scan result, disconnect and connect via registered Wi-Fi driver APIs. Wi-Fi credentials are session-only, masked, excluded from settings export and cleared after successful connect or explicit cancel.
5. **Bluetooth & Devices** — real xHCI keyboard, mouse and gamepad readiness. Bluetooth pairing, BLE, A2DP and Bluetooth HID remain unavailable until a Bluetooth transport and profile services exist.
6. **Input & Accessories** — pointer sensitivity, scroll speed, handedness and input/drop diagnostics; USB input readiness is read from xHCI.
7. **Accessibility** — high contrast, UI scale presets and cursor size. These apply to the active desktop drawing/input paths.
8. **Applications** — live process list/state and app-store catalogue count. Package install/removal is still provided by the signed package manager. Startup tasks, per-app permission controls and default-app associations are not implemented.
9. **Storage** — the storage monitor described in `STORAGE-ARCHITECTURE.md`, including block-device and partition information. Destructive installation/format actions continue through the guarded installer.
10. **Privacy & Security** — read-only view of signed-package verification and process execution/isolation foundations. User ACLs, per-app permission editing, firewall policy and a general privacy control broker are not implemented.
11. **Accounts & Users** — explicit status that multi-user account database, login/password management and session switching are not implemented.
12. **Power & Battery** — ACPI platform-table presence and explicit battery/suspend/hibernate capability status. No power toggle appears without an ACPI battery/power-management driver and validated service.
13. **Gaming** — runtime capability mask, game-library inventory and current render path, drawn from the gaming/game-library/GPU services. Unsupported compatibility runtimes are identified rather than offered as switches.
14. **Updates & Recovery** — installer recovery-journal availability and explicit status that a transactional in-place OS updater and system rollback manager are not yet implemented.
15. **System Information** — runtime CPU identity, PCI device count, GPU identity/render path, memory, uptime, firmware type, SMBIOS and ACPI availability.
16. **Developer Settings** — read-only service-manager state and serial-diagnostics status. No debug toggle is exposed unless a runtime control API exists.

## 2. Search and navigation

The Settings window provides a searchable 16-category navigation. Search filters category tabs using the category label and an authored set of searchable terms. The keyboard search supports typing, backspace, Escape to clear, and Enter to open the first result. Ctrl+F focuses search. Mouse selection uses the filtered visible navigation.

The category enum is independent of display order. Out-of-range/invalid category values never match, including an empty query.

## 3. Persistent settings format

The existing compact NXS2 settings record remains versioned and checksummed. Its current fields are:
- theme, accent and wallpaper;
- desktop text/UI scale and pointer size;
- mouse sensitivity, scroll speed and handedness;
- high-contrast mode;
- master audio volume and mute.

It does not contain network credentials, device-specific secrets, arbitrary system state, account passwords or service diagnostics. The format remains backward-compatible with existing NXS2/NXP1 preference reads.

On setting changes, Nexus validates the in-memory structure before serialization, applies live desktop/audio settings and writes `NEXUS.CFG` through the root VFS path. When NXFS Data is writable, the setting is reported as persistent. If only the RAM overlay is available, the UI reports that it is volatile; it does not claim it will survive restart.

## 4. Import, export and defaults

- **Export** writes the validated NXS2 record to root file `NEXUS.EXP`.
- **Import** requires exactly one complete supported NXS2 record, validates checksum and field ranges before replacing current settings, applies it, then persists the imported settings to `NEXUS.CFG` when writable.
- **Restore defaults** creates the validated current default structure, applies the relevant live UI/audio APIs and attempts to persist it.
- All paths display a useful status result when validation, file access, persistence or backend availability fails.
- Import is atomic at the in-memory structure boundary: a partial or invalid record is rejected without replacing the current in-memory settings.
- Neither export nor import includes Wi-Fi passwords or other secrets.

## 5. Safe failure and honest capability reporting

Settings must not write arbitrary registers or invent a working state. Service-backed actions use the associated subsystem API, and read-only pages call real discovery/status APIs. A driver-not-ready state is reported as unavailable, not silently enabled. Destructive disk operations continue to use the separately guarded installer path.

Current limitations:
- Display mode/refresh/HDR/rotation control is not integrated with a universal modeset driver.
- Bluetooth pairing/profiles, battery telemetry and suspend/hibernate are not implemented.
- Multi-user accounts/login, general permissions/ACL editing, firewall policy and per-app startup/default-app management are not implemented.
- A complete OS update/rollback manager is not implemented.
- Settings persistence currently covers the NXS2 preferences listed above, not the full runtime status of every category.
- Physical hardware verification for every vendor/device combination remains required.

## 6. Validation

The Settings CI workflow compiles and runs the category/search and NXS2 validation/persistence tests with strict warnings, then compiles the complete freestanding kernel. The live Wi-Fi UI depends on an active registered Wi-Fi driver and should be tested on actual radio hardware; absent radios are expected to report unavailable.
