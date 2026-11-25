NEXUS OS - BUNDLED SYSTEM APPLICATIONS

This system pack is installed on the EFI System Partition and loaded at boot.
Its files are immutable system resources. User-created files are stored on the
separate Nexus Data volume when it is available.

Open the Files application to see the bundled native ELF programs. Select an
ELF file and choose Run ELF (or use Ctrl+Enter) to launch it.

Included examples:
  KINETIC.ELF  - framebuffer drawing, timer and keyboard/mouse input
  GAMEPAD.ELF  - generic USB HID gamepad status and axis display
  AUDIO.ELF    - bounded AC'97 PCM playback demonstration
  PIE.ELF      - static PIE and relative relocation example

These are Nexus-native SDK examples, not Windows programs, Steam titles,
or general Linux applications. Hardware-dependent examples may report that
their optional device is unavailable on a particular computer.

The system pack is read-only. Copy files to the Nexus Data volume before
editing them. The installer preserves the system pack on the EFI partition.
