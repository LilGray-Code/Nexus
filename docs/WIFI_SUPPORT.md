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

The Wi-Fi driver API now has an optional all-or-none Ethernet-frame bridge:
a native radio driver can provide MAC-address, frame-send, and frame-receive
callbacks. Nexus registers that radio with the same network-device registry
as wired Ethernet under **wlan0**, so the existing network selector and
IPv4/DHCP/DNS stack can use it. Drivers that omit any of those callbacks are
not exposed as IP network interfaces. The driver still owns scanning,
association, WPA2/WPA3, encryption, and conversion between 802.11 radio traffic
and Ethernet frames. This bridge makes integration consistent; it does not
provide a radio driver by itself.

## Adapter detection and current chipset identification

Nexus now classifies PCI wireless-controller functions separately from ordinary
network-controller functions. It also flags PCI network-controller subclass
0x80 as a *possible* WLAN device because this class is vendor-specific and is
not proof that the adapter is Wi-Fi. Startup diagnostics print the PCI bus/
device/function address and exact vendor:device ID for WLAN candidates.

A conservative exact-ID lookup currently names these common families:

| Bus | Vendor:Device | Identified family |
|---|---|---|
| PCI | 8086:2723 | Intel Wi-Fi 6 AX200 |
| PCI | 8086:2725 | Intel Wi-Fi 6E AX210 |
| PCI | 8086:272B | Intel Wi-Fi 7 BE200 |
| PCI | 14C3:7602 | MediaTek MT7602E |
| PCI | 14C3:7603 | MediaTek MT7603E |
| PCI | 14C3:7610 | MediaTek MT7610E |
| PCI | 14C3:7611 | MediaTek MT7611E |
| PCI | 14C3:7612 | MediaTek MT7612E |
| PCI | 14C3:7662 | MediaTek MT7662E |
| PCI | 14C3:0608 | MediaTek MT7921K / RZ608 |
| PCI | 14C3:0616 / 7922 | MediaTek MT7922 |
| PCI | 14C3:7961 | MediaTek MT7921 |
| PCI | 14C3:7920 | MediaTek MT7920 |
| PCI | 14C3:7902 | MediaTek MT7902 |
| PCI | 14C3:7925 | MediaTek MT7925 |
| PCI | 10EC:B821 / C821 | Realtek RTL8821CE |
| PCI | 10EC:B822 | Realtek RTL8822BE |
| PCI | 10EC:C822 / C82F | Realtek RTL8822CE |
| PCI | 10EC:B851 | Realtek RTL8851BE |
| PCI | 10EC:B852 / B85B | Realtek RTL8852BE |
| PCI | 10EC:C852 | Realtek RTL8852CE |
| PCI | 10EC:D723 | Realtek RTL8723DE |
| PCI | 168C:003C | Qualcomm Atheros QCA988x |
| PCI | 168C:003E | Qualcomm Atheros QCA6174 |
| PCI | 168C:0040 | Qualcomm Atheros QCA9980 / QCA9990 |
| PCI | 168C:0041 | Qualcomm Atheros QCA6164 |
| PCI | 168C:0042 | Qualcomm Atheros QCA9377 |
| USB | 0E8D:7612 / 7663 / 7961 / 7925 | MediaTek USB adapter families |
| USB | 148F:7610 | MediaTek MT7610U |

This is a recognition list, not a driver compatibility list. IDs not in this
small table are still enumerated and shown numerically; they are not silently
discarded. Retail adapter names are not enough to identify a chipset because
the same retail model can ship with different silicon revisions.

The xHCI path now keeps scanning root ports after it finds HID input devices
and records up to 32 attached USB device descriptors. It reports VID:PID,
revision, device/interface class, and recognizes common CDC-ECM, CDC-NCM,
MBIM, and RNDIS network-interface classes. Vendor-specific interfaces are
reported separately so a USB Wi-Fi chipset can be matched against its exact
VID:PID later. The inventory is not a USB network driver and does not yet
handle devices behind hubs.

**Nothing in this detection work connects a Wi-Fi radio.** No chipset-specific
radio driver is registered, and there is still no authentication/association,
WPA2/WPA3 key exchange, encrypted 802.11 data path, or DHCP-over-Wi-Fi. The
status output explicitly says when a chipset has been recognized but its
native Nexus driver is not installed.

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
