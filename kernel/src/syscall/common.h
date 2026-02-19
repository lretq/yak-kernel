#pragma once

#include <yak/process.h>
#include <yak-abi/fcntl.h>

static inline struct file *getfile_ref(struct kprocess *proc, int fd)
{
	struct file *file;

	guard(mutex)(&proc->fd_mutex);

	struct fd *desc = fd_safe_get(proc, fd);
	if (!desc) {
		return NULL;
	}

	file = desc->file;
	file_ref(file);

	return file;
}

static inline unsigned int rwmode_to_internal(unsigned int flags)
{
	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		return FILE_READ;
	case O_WRONLY:
		return FILE_WRITE;
	case O_RDWR:
		return FILE_READ | FILE_WRITE;
	default:
		return -1;
	}
}

static inline unsigned int internal_to_rwmode(unsigned int flags)
{
	switch (flags & (FILE_READ | FILE_WRITE)) {
	case FILE_READ:
		return O_RDONLY;
	case FILE_WRITE:
		return O_WRONLY;
	case FILE_READ | FILE_WRITE:
		return O_RDWR;
	default:
		panic("invalid");
	}
}
