#ifndef NEXUS_USERLAND_RUNTIME_H
#define NEXUS_USERLAND_RUNTIME_H

#include <stddef.h>
#include <stdint.h>


#include "nexus/syscall.h"

/* Allocation-free helpers provided by libnexus.a. These are not a drop-in
 * libc/POSIX implementation. File I/O uses the active Nexus VFS policy.
 */
size_t nexus_strlen(const char *text);
size_t nexus_strnlen(const char *text, size_t maximum);
int nexus_strcmp(const char *left, const char *right);
int nexus_strncmp(const char *left, const char *right, size_t maximum);
void *nexus_memcpy(void *destination, const void *source, size_t length);
void *nexus_memset(void *destination, int value, size_t length);
int nexus_memcmp(const void *left, const void *right, size_t length);

/* Complete-write loop; returns bytes written or a negative Nexus errno. */
int64_t nexus_write_all(int descriptor, const void *buffer, size_t length);
int64_t nexus_puts(const char *text);

/* Bounded printf subset: %%, %c, %s, %d/%i, %u, %x/%X, %p;
 * optional zero padding and numeric widths up to 32. Output is capped at 511 bytes.
 */
int64_t nexus_printf(const char *format, ...);

/* Read a whole file into caller-owned storage. On capacity overflow, returns
 * -NEXUS_EFBIG, closes the descriptor, and sets *bytes_read to zero.
 */
int64_t nexus_read_file(const char *path, void *buffer, size_t capacity,
                        size_t *bytes_read);
int64_t nexus_write_file(const char *path, const void *buffer, size_t length);

#endif
