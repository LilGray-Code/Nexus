#ifndef NEXUS_LOG_BUFFER_H
#define NEXUS_LOG_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#define NEXUS_LOG_BUFFER_CAPACITY 4096U

/* Non-blocking bounded log capture. Contended writes are dropped, never spun
 * on, so an interrupt or panic path cannot deadlock behind a log reader.
 */
void nexus_log_buffer_init(void);
void nexus_log_buffer_append(const char *text, size_t length);
size_t nexus_log_buffer_read_recent(char *output, size_t capacity);
uint64_t nexus_log_buffer_dropped(void);

#endif
