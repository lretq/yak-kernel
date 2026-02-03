#include "yak/wait.h"
#include <stddef.h>
#include <yak/cpudata.h>
#include <yak/file.h>
#include <yak/heap.h>
#include <yak/log.h>
#include <yak/timespec.h>
#include <yak/syscall.h>
#include <yak-abi/errno.h>
#include <yak-abi/poll.h>

#include "common.h"

struct pollfd {
	int fd; /* file descriptor */
	short events; /* requested events */
	short revents; /* returned events */
};

// implements ppoll
DEFINE_SYSCALL(SYS_POLL, poll, struct pollfd *fds, size_t nfds)
{
	pr_debug("sys_poll()\n");

	struct kprocess *proc = curproc();

	struct file **files = kcalloc(nfds, sizeof(struct file *));
	if (!files) {
		return SYS_ERR(ENOMEM);
	}

	guard(autofree)(files, sizeof(struct file *) * nfds);

	void **events = kcalloc(nfds, sizeof(void *));
	if (!events) {
		return SYS_ERR(ENOMEM);
	}
	guard(autofree)(events, sizeof(void *) * nfds);

	struct wait_block *wait_blocks = NULL;

	int ready_count = 0;

	int event_count = 0;

	for (size_t i = 0; i < nfds; i++) {
		struct pollfd *entry = &fds[i];

		entry->revents = 0;

		// skip this entry
		if (entry->fd < 0) {
			continue;
		}

		struct file *file = getfile_ref(proc, entry->fd);
		files[i] = file;

		if (!file) {
			entry->revents = POLLNVAL;
			ready_count++;
			continue;
		}

		events[event_count++] = &file->vnode->poll_event;

		VOP_POLL(file->vnode, entry->events, &entry->revents);

		if (entry->revents != 0)
			ready_count++;
	}

	if (ready_count) {
		goto Cleanup;
	}

	wait_blocks = kcalloc(event_count, sizeof(struct wait_block));
	if (!wait_blocks) {
		return SYS_ERR(ENOMEM);
	}

	while (ready_count == 0) {
		sched_wait_many(wait_blocks, events, event_count,
				WAIT_MODE_BLOCK, WAIT_TYPE_ANY,
				TIMEOUT_INFINITE);

		for (size_t i = 0; i < nfds; i++) {
			struct pollfd *entry = &fds[i];
			struct file *file = files[i];

			if (!file)
				continue;

			VOP_POLL(file->vnode, entry->events, &entry->revents);

			if (entry->revents != 0)
				ready_count++;
		}
	}

	kfree(wait_blocks, event_count * sizeof(struct wait_block));

Cleanup:

	for (size_t i = 0; i < nfds; i++) {
		if (files[i] != NULL)
			file_deref(files[i]);
	}

	return SYS_OK(ready_count);
}
