#include <stddef.h>
#include <yak/syscall.h>
#include <yak-abi/errno.h>
#include <yak-abi/syscall.h>
#include <yak/init.h>
#include <yak/log.h>

#define SYSCALL_LIST                                                         \
	X(SYS_OPENAT, sys_openat, "dirfd=%d path=%p flags=%d mode=0%o")      \
	X(SYS_CLOSE, sys_close, "fd=%d")                                     \
	X(SYS_READ, sys_read, "fd=%d buf=%p count=%ld")                      \
	X(SYS_WRITE, sys_write, "fd=%d buf=%p count=%ld")                    \
	X(SYS_PREAD, sys_pread, "fd=%d buf=%p count=%ld off=%ld")            \
	X(SYS_PWRITE, sys_pwrite, "fd=%d buf=%p count=%ld off=%ld")          \
	X(SYS_DUP, sys_dup, "fd=%d")                                         \
	X(SYS_DUP2, sys_dup2, "oldfd=%d newfd=%d")                           \
	X(SYS_SEEK, sys_seek, "fd=%d off=%ld whence=%d")                     \
	X(SYS_FORK, sys_fork, "")                                            \
	X(SYS_EXECVE, sys_execve, "path=%p argv=%p envp=%p")                 \
	X(SYS_MUNMAP, sys_munmap, "addr=%p length=%ld")                      \
	X(SYS_MPROTECT, sys_mprotect, "addr=%p length=%ld prot=%lu")         \
	X(SYS_EXIT, sys_exit, "rc=%d")                                       \
	X(SYS_GETPID, sys_getpid, "")                                        \
	X(SYS_GETPPID, sys_getppid, "")                                      \
	X(SYS_GETPGID, sys_getpgid, "")                                      \
	X(SYS_GETSID, sys_getsid, "")                                        \
	X(SYS_SETPGID, sys_setpgid, "pid=%llu pgid=%llu")                    \
	X(SYS_SETSID, sys_setsid, "")                                        \
	X(SYS_ARCHCTL, sys_archctl, "op=%d data=%ld")                        \
	X(SYS_SLEEP, sys_sleep, "req=%p rem=%p")                             \
	X(SYS_FALLOCATE, sys_fallocate, "fd=%d mode=%d off=%ld size=%ld")    \
	X(SYS_FCNTL, sys_fcntl, "fd=%d op=%d arg=%ld")                       \
	X(SYS_IOCTL, sys_ioctl, "fd=%d op=%ld argp=%p")                      \
	X(SYS_FSTATAT, sys_fstatat, "dirfd=%d path=%s buf=%p flags=%d")      \
	X(SYS_CHDIR, sys_chdir, "path=%p")                                   \
	X(SYS_FCHDIR, sys_fchdir, "fd=%d")                                   \
	X(SYS_GETDENTS, sys_getdents, "fd=%d buf=%p max=%ld")                \
	X(SYS_FACCESSAT, sys_faccessat, "dirfd=%d path=%p mode=%d flags=%d") \
	X(SYS_WAITPID, sys_waitpid, "pid=%lld status=%p flags=%d")           \
	X(SYS_POLL, sys_poll, "fds=%p nfds=%ld tm=%p mask=%p")               \
	X(SYS_READLINKAT, sys_readlinkat,                                    \
	  "dirfd=%d path=%p buf=%p max_size=%ld")                            \
	X(SYS_SYMLINKAT, sys_symlinkat, "dirfd=%d target=%p link=%p")        \
	X(SYS_LINKAT, sys_linkat,                                            \
	  "olddirfd=%d oldpath=%p newdirfd=%d newpath=%p flags=%d")          \
	X(SYS_DEBUG_SLEEP, sys_debug_sleep, "duration=%ld ns")

#define SYSCALL_LIST_NOLOG        \
	SYSCALL_LIST              \
	X(SYS_MMAP, sys_mmap, "") \
	X(SYS_DEBUG_LOG, sys_debug_log, "")

#define X(num, fn, fmt) extern long fn();
SYSCALL_LIST_NOLOG
#undef X

#define X(num, fn, fmt) [num] = (void *)fn,
syscall_fn syscall_table[MAX_SYSCALLS] = { SYSCALL_LIST_NOLOG };
#undef X

struct syscall_result sys_noop()
{
	pr_warn("sys_noop called\n");
	return SYS_ERR(ENOSYS);
}

void syscall_log(uintptr_t num, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
		 uintptr_t arg4, uintptr_t arg5, uintptr_t arg6)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#pragma GCC diagnostic ignored "-Wformat"

#define X(n, fn, fmt)                                                     \
	case n:                                                           \
		pr_debug(#fn "(" fmt ")\n", arg1, arg2, arg3, arg4, arg5, \
			 arg6);                                           \
		break;

	switch (num) {
		SYSCALL_LIST
	}
#undef X

#pragma GCC diagnostic pop
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
