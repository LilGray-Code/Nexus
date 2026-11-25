#ifndef NEXUS_FPU_H
#define NEXUS_FPU_H

#include <stdbool.h>

/* Enable x87/SSE2 and preserve the kernel FPU state around one foreground app. */
bool nexus_fpu_init(void);
bool nexus_fpu_available(void);
bool nexus_fpu_user_begin(void);
bool nexus_fpu_user_end(void);

#endif
