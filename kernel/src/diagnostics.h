#ifndef NEXUS_DIAGNOSTICS_H
#define NEXUS_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>

/* Handles built-in, read-only diagnostic commands. Returns false for an
 * unknown command. Output is always NUL-terminated when capacity is nonzero.
 */
bool nexus_diagnostics_command(const char *command,
                               char *output, size_t capacity);

#endif
