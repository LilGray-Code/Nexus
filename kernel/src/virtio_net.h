#ifndef NEXUS_VIRTIO_NET_H
#define NEXUS_VIRTIO_NET_H

#include <stdbool.h>

/* Register a modern VirtIO-net PCI adapter with the shared Ethernet stack. */
bool virtio_net_init(void);
bool virtio_net_ready(void);

#endif
