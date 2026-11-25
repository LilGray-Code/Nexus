#ifndef NEXUS_FPU_H
#define NEXUS_FPU_H
#include <stdbool.h>
bool nexus_fpu_init(void);
bool nexus_fpu_available(void);
bool nexus_fpu_avx_available(void);
bool nexus_fpu_user_begin(void);
bool nexus_fpu_user_end(void);
#endif
