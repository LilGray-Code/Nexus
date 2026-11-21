#ifndef NEXUS_USERMODE_H
#define NEXUS_USERMODE_H

#include <stdbool.h>
#include <stdint.h>

/* Allocate an isolated low user mapping and run the ring-3 exit self-test. */
bool usermode_run_selftest(void);
bool usermode_execute(uint64_t root_physical, uint64_t entry_point,
                      uint64_t stack_pointer, uint64_t kernel_stack_top,
                      uint64_t *exit_status);
bool usermode_sys_write(uint64_t user_address, uint64_t length);
int64_t usermode_sys_open(uint64_t user_path);
int64_t usermode_sys_read(uint64_t descriptor, uint64_t user_buffer,
                          uint64_t length);
int64_t usermode_sys_close(uint64_t descriptor);

extern volatile uint64_t usermode_exit_status;
extern volatile bool usermode_exit_seen;
extern volatile bool usermode_write_rejected;
void nexus_user_return(void);

#endif
