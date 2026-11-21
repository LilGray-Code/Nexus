#ifndef NEXUS_STORAGE_H
#define NEXUS_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

/* Classifies mass-storage controllers from the read-only PCI inventory. */
bool storage_probe(void);
size_t storage_controller_count(void);

#endif
