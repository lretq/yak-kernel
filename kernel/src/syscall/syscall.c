#include <stddef.h>
#include <yak/syscall.h>
#include <yak-abi/errno.h>
#include <yak-abi/syscall.h>
#include <yak/init.h>
#include <yak/log.h>

#define SYSCALL_LIST                        \
	X(SYS_OPENAT, sys_openat)           \
	X(SYS_CLOSE, sys_close)             \
	X(SYS_READ, sys_read)               \
	X(SYS_WRITE, sys_write)             \
	X(SYS_DUP, sys_dup)                 \
	X(SYS_DUP2, sys_dup2)               \
	X(SYS_SEEK, sys_seek)               \
	X(SYS_FORK, sys_fork)               \
	X(SYS_EXECVE, sys_execve)           \
	X(SYS_MMAP, sys_mmap)               \
	X(SYS_MUNMAP, sys_munmap)           \
	X(SYS_MPROTECT, sys_mprotect)       \
	X(SYS_EXIT, sys_exit)               \
	X(SYS_GETPID, sys_getpid)           \
	X(SYS_GETPPID, sys_getppid)         \
	X(SYS_GETPGID, sys_getpgid)         \
	X(SYS_GETSID, sys_getsid)           \
	X(SYS_SETPGID, sys_setpgid)         \
	X(SYS_SETSID, sys_setsid)           \
	X(SYS_ARCHCTL, sys_archctl)         \
	X(SYS_SLEEP, sys_sleep)             \
	X(SYS_FALLOCATE, sys_fallocate)     \
	X(SYS_FCNTL, sys_fcntl)             \
	X(SYS_IOCTL, sys_ioctl)             \
	X(SYS_FSTATAT, sys_fstatat)         \
	X(SYS_CHDIR, sys_chdir)             \
	X(SYS_FCHDIR, sys_fchdir)           \
	X(SYS_GETDENTS, sys_getdents)       \
	X(SYS_FACCESSAT, sys_faccessat)     \
	X(SYS_WAITPID, sys_waitpid)         \
	X(SYS_DEBUG_SLEEP, sys_debug_sleep) \
	X(SYS_DEBUG_LOG, sys_debug_log)

#define X(num, fn) extern long fn();
SYSCALL_LIST
#undef X

#define X(num, fn) [num] = (void *)fn,
syscall_fn syscall_table[MAX_SYSCALLS] = { SYSCALL_LIST };
#undef X

struct syscall_result sys_noop()
{
	pr_warn("sys_noop called\n");
	return SYS_ERR(ENOSYS);
}

void syscall_init()
{
	for (size_t i = 0; i < MAX_SYSCALLS; i++) {
		if (syscall_table[i] == NULL)
			syscall_table[i] = (void *)sys_noop;
	}
}

INIT_ENTAILS(syscall);
INIT_DEPS(syscall);
INIT_NODE(syscall, syscall_init);
