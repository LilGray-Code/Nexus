# Nexus graphical installer

## Screen flow

1. **Select drive.** The installer presents the currently selected block
   device, model, stable ID, capacity and logical sector size. Use Up/Down to
   select another drive. F1 selects UEFI, F2 selects the legacy BIOS layout,
   and F3 selects the dual-firmware layout. The proposed GPT partitions are
   displayed before any destructive action.
2. **Review erase operation.** The installer displays the selected model and
   stable ID again, highlights the destructive operation, and presents
   **Confirm & Install** and **Cancel**. Nothing is intentionally written
   before this explicit confirmation.
3. **Install.** When the destructive-write build gate is enabled, the runtime
   revalidates the captured disk identity and safety conditions before it
   deploys partitions, filesystems and boot payloads. Progress messages are
   emitted to the screen and serial diagnostics.
4. **Complete or recover.** The success screen offers a restart into the
   installed system. On failure, the displayed reason should be retained and
   reviewed before retrying. The persistent install journal is used by the
   installer recovery path.

## Safety requirements

Installation erases the selected target disk. The installer must reject the
boot medium, mounted volumes, ambiguous/non-unique identities and devices
without a supported guarded write path. Do not choose a disk unless its
model, stable ID and capacity match the intended target.

The installed OS image is built separately from the live installer. A release
must keep its installer-write build gate disabled in the installed-system
kernel and must pass the disposable NVMe/SATA installation, persistent-data,
UEFI/BIOS boot and recovery tests before being called installable.

**A graphical confirmation screen does not itself prove that destructive
writes are enabled or safe.** The build configuration and end-to-end test
results determine whether a particular ISO can install to a physical disk.
