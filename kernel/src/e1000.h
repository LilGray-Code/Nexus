#ifndef NEXUS_E1000_H
#define NEXUS_E1000_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool e1000_init(void);
bool e1000_ready(void);
bool e1000_get_mac(uint8_t address[6]);
bool e1000_send_frame(const void *frame, size_t length);
bool e1000_receive_frame(void *buffer, size_t capacity, size_t *length);
bool e1000_send_arp_probe(void);

#endif
