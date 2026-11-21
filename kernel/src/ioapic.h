#ifndef NEXUS_IOAPIC_H
#define NEXUS_IOAPIC_H

#include <stdbool.h>

/* Read-only controller identification/version probe. No routing is changed. */
bool ioapic_probe(void);

#endif
