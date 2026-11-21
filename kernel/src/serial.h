#ifndef NEXUS_SERIAL_H
#define NEXUS_SERIAL_H

#include <stdint.h>
#include <stddef.h>

void serial_init(void);
void serial_write(const char *text);
void serial_write_n(const char *text, size_t length);
void serial_write_hex(uint64_t value);

#endif
