#ifndef NEXUS_SYSCALL_H
#define NEXUS_SYSCALL_H

/*
 * Initial Nexus x86-64 syscall ABI.
 * int 0x80: RAX = number, RDI/RSI/RDX = arguments, RAX = result.
 * Failed calls return negative errno values.
 */
#define NEXUS_SYS_EXIT  1ULL
#define NEXUS_SYS_WRITE 2ULL
#define NEXUS_SYS_OPEN  3ULL
#define NEXUS_SYS_READ  4ULL
#define NEXUS_SYS_CLOSE 5ULL

#define NEXUS_ENOENT  2LL
#define NEXUS_EIO     5LL
#define NEXUS_EBADF   9LL
#define NEXUS_ENODEV 19LL
#define NEXUS_EFAULT 14LL
#define NEXUS_EINVAL 22LL
#define NEXUS_EMFILE 24LL
#define NEXUS_ENAMETOOLONG 36LL
#define NEXUS_ENOSYS 38LL

#endif
