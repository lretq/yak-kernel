#ifndef _YAK_SYSCALL_H
#define _YAK_SYSCALL_H

#include <yak/arch-syscall.h>

#define SYSCALL_SELECT(_0, _1, _2, _3, _4, _5, _6, NAME, ...) NAME

#define syscall(...)                                                   \
	SYSCALL_SELECT(__VA_ARGS__, _SYSCALL6_CAST, _SYSCALL5_CAST,    \
		       _SYSCALL4_CAST, _SYSCALL3_CAST, _SYSCALL2_CAST, \
		       _SYSCALL1_CAST, _SYSCALL0_CAST)(__VA_ARGS__)

#define _SYSCALL0_CAST(num) __syscall0((int)(num))

#define _SYSCALL1_CAST(num, arg1) __syscall1((int)(num), (uint64_t)(arg1))

#define _SYSCALL2_CAST(num, arg1, arg2) \
	__syscall2((int)(num), (uint64_t)(arg1), (uint64_t)(arg2))

#define _SYSCALL3_CAST(num, arg1, arg2, arg3)                      \
	__syscall3((int)(num), (uint64_t)(arg1), (uint64_t)(arg2), \
		   (uint64_t)(arg3))

#define _SYSCALL4_CAST(num, arg1, arg2, arg3, arg4)                \
	__syscall4((int)(num), (uint64_t)(arg1), (uint64_t)(arg2), \
		   (uint64_t)(arg3), (uint64_t)(arg4))

#define _SYSCALL5_CAST(num, arg1, arg2, arg3, arg4, arg5)          \
	__syscall5((int)(num), (uint64_t)(arg1), (uint64_t)(arg2), \
		   (uint64_t)(arg3), (uint64_t)(arg4), (uint64_t)(arg5))

#define _SYSCALL6_CAST(num, arg1, arg2, arg3, arg4, arg5, arg6)          \
	__syscall6((int)(num), (uint64_t)(arg1), (uint64_t)(arg2),       \
		   (uint64_t)(arg3), (uint64_t)(arg4), (uint64_t)(arg5), \
		   (uint64_t)(arg6))

#define syscall_rv(...) (syscall(__VA_ARGS__).retval)
#define syscall_err(...) (syscall(__VA_ARGS__).err)

enum {
	SYS_OPENAT = 1,
	SYS_CLOSE,
	SYS_READ,
	SYS_WRITE,
	SYS_DUP,
	SYS_DUP2,
	SYS_SEEK,
	SYS_FORK,
	SYS_EXECVE,
	SYS_MMAP,
	SYS_MUNMAP,
	SYS_MPROTECT,
	SYS_EXIT,
	SYS_GETPID,
	SYS_GETPPID,
	SYS_GETPGID,
	SYS_GETSID,
	SYS_SETPGID,
	SYS_SETSID,
	SYS_ARCHCTL,
	SYS_SLEEP,
	SYS_FALLOCATE,
	SYS_FCNTL,
	SYS_IOCTL,
	SYS_FSTATAT,
	SYS_CHDIR,
	SYS_FCHDIR,
	SYS_GETDENTS,
	SYS_DEBUG_SLEEP,
	SYS_DEBUG_LOG,
};

#endif
