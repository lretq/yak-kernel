#include <yak/process.h>
#include <yak/cpudata.h>
#include <yak/syscall.h>
#include <yak/log.h>
#include <yak-abi/fcntl.h>
#include <yak-abi/errno.h>

#include "common.h"

DEFINE_SYSCALL(SYS_FCNTL, fcntl, int fd, int op, size_t arg)
{
	struct kprocess *proc = curproc();

	status_t rv = YAK_INVALID_ARGS;

	switch (op) {
	case F_DUPFD: {
		int newfd;
		rv = fd_duplicate(proc, fd, arg,
				  FD_DUPE_NOCLOEXEC | FD_DUPE_MINIMUM, &newfd);
		RET_ERRNO_ON_ERR(rv);
		pr_debug("fd flags new: %d\n", proc->fds[newfd]->flags);
		return SYS_OK(newfd);
	}
	case F_DUPFD_CLOEXEC: {
		int newfd;
		rv = fd_duplicate(proc, fd, arg,
				  FD_DUPE_CLOEXEC | FD_DUPE_MINIMUM, &newfd);
		RET_ERRNO_ON_ERR(rv);
		return SYS_OK(newfd);
	}
	case F_GETFD: {
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			rv = YAK_BADF;
			break;
		}
		pr_warn("fd %d file refcnt %ld\n", fd, desc->file->refcnt);
		return SYS_OK(desc->flags);
	}
	case F_SETFD: {
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			rv = YAK_BADF;
			break;
		}

		// check if all the flags are supported
		if (0 != (arg & ~(FD_CLOEXEC))) {
			rv = YAK_INVALID_ARGS;
			break;
		}

		rv = YAK_SUCCESS;
		desc->flags = arg;
		break;
	}
	case F_GETFL: {
		struct file *file = getfile_ref(proc, fd);
		if (!file) {
			return SYS_ERR(EBADF);
		}
		guard_ref_adopt(file, file);

		unsigned int flags = internal_to_rwmode(file->flags);

		// XXX: check other flags (O_APPEND...)

		return SYS_OK(flags);
	}
	case F_SETFL: {
		struct file *file = getfile_ref(proc, fd);
		if (!file) {
			return SYS_ERR(EBADF);
		}
		guard_ref_adopt(file, file);

		// TODO: mask off args
		// only allow certain flags
		// and actually set some when they get meaning

		return SYS_OK(0);
	}
	default:
		pr_warn("unimplemented fcntl op: %d\n", op);
		return SYS_ERR(ENOTSUP);
	}

	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(0);
}
