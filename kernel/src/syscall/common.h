#pragma once

#include <yak/process.h>

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
