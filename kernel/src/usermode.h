#ifndef NEXUS_USERMODE_H
#define NEXUS_USERMODE_H

#include <stdbool.h>
#include <stdint.h>

/* Allocate an isolated low user mapping and run the ring-3 exit self-test. */
bool usermode_run_selftest(void);

extern volatile uint64_t usermode_exit_status;
void nexus_user_return(void);

#endif
