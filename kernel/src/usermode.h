#ifndef NEXUS_USERMODE_H
#define NEXUS_USERMODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_USERMODE_OUTPUT_MAX 256U

/* Allocate an isolated low user mapping and run the ring-3 exit self-test. */
bool usermode_run_selftest(void);
bool usermode_execute(uint64_t root_physical, uint64_t entry_point,
                      uint64_t stack_pointer, uint64_t kernel_stack_top,
                      uint64_t *exit_status);
bool usermode_sys_write(uint64_t user_address, uint64_t length);
/* Copy the most recent native process's bounded write(1, ...) output. */
size_t usermode_console_output_copy(char *destination, size_t capacity,
                                    bool *truncated);
int64_t usermode_sys_open(uint64_t user_path);
int64_t usermode_sys_read(uint64_t descriptor, uint64_t user_buffer,
                          uint64_t length);
int64_t usermode_sys_close(uint64_t descriptor);
int64_t usermode_sys_file_write(uint64_t user_path, uint64_t user_buffer,
                                uint64_t length);
int64_t usermode_sys_input_poll(uint64_t user_event);
int64_t usermode_sys_display_info(uint64_t user_info);
int64_t usermode_sys_clock_ticks(void);
int64_t usermode_sys_framebuffer_blit(uint64_t user_request);
int64_t usermode_sys_sleep_ticks(uint64_t ticks);
int64_t usermode_sys_gamepad_poll(uint64_t user_event);
int64_t usermode_sys_audio_write(uint64_t user_samples,
                                 uint64_t sample_frames);
int64_t usermode_sys_cpu_info(uint64_t user_info);

extern volatile uint64_t usermode_exit_status;
extern volatile bool usermode_exit_seen;
extern volatile bool usermode_write_rejected;
void nexus_user_return(void);

#endif
