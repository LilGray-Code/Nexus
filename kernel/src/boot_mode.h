#ifndef NEXUS_BOOT_MODE_H
#define NEXUS_BOOT_MODE_H

#include <stdbool.h>

/* Return true only when Limine's command line contains the standalone
 * nexus.mode=installer token. Missing or unknown modes safely boot desktop.
 */
bool nexus_boot_mode_is_installer(const char *command_line);

#endif
