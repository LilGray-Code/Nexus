# Nexus Wi-Fi support

## Current, verified scope

The current kernel has a generic Wi-Fi state/driver interface and a host-testable
802.11 beacon/probe-response parser. The parser recognizes open, WPA2-Personal
(PSK), and WPA3-Personal (SAE with required PMF) advertisements. It now
classifies common 2.4 GHz and 5 GHz channel plans and reports center frequency.

This is **not working wireless connectivity**. No production Wi-Fi chipset
driver is currently registered. The kernel does not yet perform radio scans,
association, WPA2 four-way handshakes, SAE, CCMP data protection, roaming, or
live DHCP over Wi-Fi. Do not advertise a chipset as supported until a driver
has passed hardware/emulator tests and can connect, exchange traffic, and
recover from disconnects.

## Broad-compatibility target

Nexus should support multiple independently maintained driver families rather
than depending on one laptop vendor. Candidate families, in priority order:

1. Intel PCIe adapters (older AC and newer AX generations), subject to chipset
   generation, firmware availability, and a complete native driver port.
2. MediaTek PCIe and USB adapters, selected by exact USB/PCI IDs and firmware
   provenance.
3. Realtek PCIe and USB adapters, selected by exact chipset revision; vendor
   names alone are not sufficient because similarly named products use
   incompatible silicon.
4. Qualcomm/Atheros PCIe adapters where firmware and licensing permit.
5. USB Wi-Fi adapters using a generic USB lifecycle, endpoint, interrupt and
   bulk-transfer framework.

A device is not supported merely because PCI/USB enumeration sees it or a
driver can read its identity. The driver must initialize the radio, load
verified firmware when required, scan 2.4/5 GHz (and 6 GHz where supported),
associate using supported security, exchange encrypted data, obtain an IP
configuration, and survive suspend/resume or disconnect/reconnect tests where
those power features are claimed.

## Required platform work

- Generic PCIe and USB Wi-Fi driver binding by exact device/revision IDs.
- A reusable firmware loader with signature/hash verification, version policy,
  and clear license/provenance records.
- DMA-safe buffer ownership, interrupt/MSI handling, reset and timeout paths.
- A complete 802.11 management/data path, authentication, association,
  WPA2-Personal, WPA3-Personal/SAE, PMF, and CCMP.
- Regulatory-domain/channel restrictions and safe transmit-power policy.
- Scan result de-duplication, hidden-network handling, roaming, disconnect
  recovery, and power management.
- Network-stack integration for DHCP, DNS, IPv4 and IPv6.
- Per-chipset test records for cold boot, scan, connect, encrypted traffic,
  loss/recovery, and sustained transfers.

## Release policy

The 2.4/5 GHz parser metadata is a scan-format foundation only. It does not
increase the number of radios that work. A compatibility matrix must list
exact chipset IDs, driver/firmware versions, supported bands, security modes,
and test results. Unknown devices must fail safely and remain clearly marked
unsupported rather than being shown as connected.
