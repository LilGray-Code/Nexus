# Nexus network adapter support

## What works in the current kernel

Nexus has a network-device interface registry and an IPv4 network stack. A driver registers frame send/receive and MAC-address callbacks; the network stack uses the selected device for Ethernet frames, ARP, DHCP, DNS, UDP, and its early TCP/HTTP client.

The currently implemented physical Ethernet driver is Intel E1000-family PCI Ethernet. Its explicit PCI IDs are:

- Intel 82540EM (8086:100E)
- Intel 82540EP (8086:101E)
- Intel 82545EM (8086:100F)
- Intel 82545GM (8086:1026)
- Intel 82546EB (8086:1010)
- Intel 82574L (8086:10D3)

This is not a claim that every Intel Ethernet controller is compatible. Other PCI/PCIe Ethernet chips need their own validated driver or a separately implemented compatible driver.

## Selecting a network interface

The kernel can register up to eight network interfaces. Settings exposes the currently registered adapter and the **5** key cycles to the next registered interface. Switching resets the current TCP session and restarts DHCP on the selected adapter. If initialization fails, the previous default adapter is restored.

A device appears in this selector only after its hardware driver has initialized and registered successfully. This selector does not install a missing hardware driver.

## Wi-Fi status

The 802.11 beacon/probe-response parser and Wi-Fi connection-state API are foundations only. The current tree does **not** contain a chipset-specific Wi-Fi radio driver, USB Wi-Fi driver, WPA2/WPA3 authentication/key management, or a complete 802.11-to-Ethernet data path. Therefore Wi-Fi cards and USB Wi-Fi adapters are not yet usable on a real Nexus installation.

## USB driver limitation

The current xHCI implementation handles directly attached USB HID boot keyboards and mice. It does not yet provide the general USB device stack needed by USB network adapters: hub enumeration, generic interface/endpoint discovery, robust bulk transfers, hotplug lifecycle, and device power management. A USB Wi-Fi adapter cannot be made to work merely by copying a driver file to the device.

## Driver installation policy and compatibility

A driver must be built for the Nexus kernel ABI and the adapter's exact PCI or USB vendor/product and interface identifiers. Windows and Linux driver binaries are not executable by Nexus. Firmware blobs, when required by a chipset, are separate from the driver and must have redistribution terms compatible with Nexus.

The safe initial model is a curated, signed Nexus driver package containing a manifest, hardware match IDs, kernel-ABI version, driver binary, optional firmware, license notices, and cryptographic hashes/signature. The package manager must reject mismatched ABI versions, unsupported hardware, invalid signatures, and firmware/license violations. Until a verified native driver-package loader and signing/update path exist, drivers must be built into a tested Nexus kernel release; do not install arbitrary kernel code from an untrusted USB stick.

## Required milestones for usable Wi-Fi adapters

1. Generalize xHCI device enumeration and add tested bulk IN/OUT transfers, hub support, hotplug, and endpoint teardown.
2. Implement one USB Wi-Fi chipset driver end-to-end, including firmware loading and licensing review; separately add a PCIe Wi-Fi driver only after the PCI resource, DMA, and interrupt paths are ready.
3. Connect radio scan results to the 802.11 parser, implement authentication/association and WPA2/WPA3 key handling, and bridge protected Wi-Fi data frames into the network-device interface.
4. Add disconnect/reconnect, roaming, power management, and error recovery.
5. Validate on physical adapters and clean installs before listing each chipset as supported.
6. Only then ship a signed installable driver package workflow and publish a tested compatibility list.

## Network security and browser caveat

The early network stack is not a modern secure browser network stack. HTTPS/TLS, certificate validation, IPv6, and modern browser transport support remain separate work. Ethernet link support does not imply Chrome compatibility.
