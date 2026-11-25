#ifndef NEXUS_RTL8139_H
#define NEXUS_RTL8139_H

#include <stdbool.h>

/* Register a supported Realtek RTL8139 PCI Ethernet controller. */
bool rtl8139_init(void);
bool rtl8139_ready(void);

#endif
