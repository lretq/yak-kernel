#include <assert.h>
#include <yak/heap.h>
#include <yak/log.h>
#include <yak/status.h>
#include <yak/file.h>
#include <yak/process.h>
#include <yak/fs/vfs.h>
#include <yak-abi/fcntl.h>

#define FD_LIMIT 65535
#define FD_GROW_BY 4

static void file_cleanup(struct file *file);
GENERATE_REFMAINT(file, refcnt, file_cleanup);

static void file_cleanup(struct file *file)
{
	vnode_deref(file->vnode);
	kfree(file, sizeof(struct file));
}

status_t fd_grow(struct kprocess *proc, int new_cap)
{
	int old_cap = proc->fd_cap;
	if (new_cap < old_cap)
		return YAK_SUCCESS;

	struct fd **old_fds = proc->fds;

	if (new_cap > FD_LIMIT) {
		return YAK_MFILE;
	}

	struct fd **table = kcalloc(new_cap, sizeof(struct fd *));
	if (!table)
		return YAK_OOM;

	int i;
	for (i = 0; i < old_cap; i++)
		table[i] = old_fds[i];
	for (; i < new_cap; i++)
		table[i] = NULL;

	proc->fds = table;
	proc->fd_cap = new_cap;

	kfree(old_fds, old_cap * sizeof(struct fd *));

	return YAK_SUCCESS;
}

void file_init(struct file *file)
{
	kmutex_init(&file->lock, "file");
	file->offset = 0;
	file->refcnt = 1;

	file->vnode = NULL;
	file->flags = 0;
}

struct fd *fd_safe_get(struct kprocess *proc, int fd)
{
	if (!proc->fds || fd >= proc->fd_cap) {
		return NULL;
	}

	return proc->fds[fd];
}

status_t fd_allocate_next(struct kprocess *proc, int *outfd)
{
	for (int i = 0; i < proc->fd_cap; i++) {
		if (!proc->fds[i]) {
			*outfd = i;
			proc->fds[i] = kzalloc(sizeof(struct fd));
			if (!proc->fds[i])
				return YAK_OOM;
			return YAK_SUCCESS;
		}
	}

	status_t rv = fd_grow(proc, proc->fd_cap + FD_GROW_BY);
	if (IS_ERR(rv))
		return rv;

	*outfd = proc->fd_cap - FD_GROW_BY;
	proc->fds[*outfd] = kzalloc(sizeof(struct fd));
	return YAK_SUCCESS;
}

status_t fd_allocate_min(struct kprocess *proc, int min_fd, int *outfd)
{
	int start = min_fd;

	while (true) {
		for (int i = start; i < proc->fd_cap; i++) {
			if (!proc->fds[i]) {
				*outfd = i;
				proc->fds[i] = kzalloc(sizeof(struct fd));
				return YAK_SUCCESS;
			}
		}

		status_t rv = fd_grow(proc, proc->fd_cap + FD_GROW_BY);
		if (IS_ERR(rv))
			return rv;

		start = proc->fd_cap - FD_GROW_BY;
	}
}

status_t fd_allocate_at(struct kprocess *proc, int fd, int *outfd)
{
	if (fd >= proc->fd_cap) {
		status_t rv = fd_grow(proc, fd + FD_GROW_BY);
		if (IS_ERR(rv))
			return rv;
	}

	struct fd *old = proc->fds[fd];

	if (old) {
		fd_close(proc, fd);
	}

	assert(proc->fds[fd] == NULL);
	proc->fds[fd] = kzalloc(sizeof(struct fd));
	if (!proc->fds[fd])
		return YAK_OOM;

	*outfd = fd;
	return YAK_SUCCESS;
}

status_t fd_assign(struct kprocess *proc, int fd, struct fd *src_fd, int flags)
{
	struct fd *dest_fd = proc->fds[fd];
	assert(dest_fd);

	dest_fd->file = src_fd->file;
	file_ref(dest_fd->file);

	dest_fd->flags = src_fd->flags;

	if (flags & FD_DUPE_NOCLOEXEC)
		dest_fd->flags &= ~FD_CLOEXEC;
	else if (flags & FD_DUPE_CLOEXEC)
		dest_fd->flags |= FD_CLOEXEC;

	return YAK_SUCCESS;
}

status_t fd_duplicate(struct kprocess *proc, int oldfd, int newfd, int flags,
		      int *outfd)
{
	guard(mutex)(&proc->fd_mutex);

	struct fd *src_fd = fd_safe_get(proc, oldfd);
	if (!src_fd) {
		return YAK_BADF;
	}

	// Early exit for normal dup2
	if (!(flags & FD_DUPE_MINIMUM) && oldfd == newfd) {
		return YAK_SUCCESS;
	}

	int alloc_fd;
	status_t rv;

	if (newfd == -1) {
		rv = fd_allocate_next(proc, &alloc_fd);
	} else if (flags & FD_DUPE_MINIMUM) {
		rv = fd_allocate_min(proc, newfd, &alloc_fd);
	} else {
		rv = fd_allocate_at(proc, newfd, &alloc_fd);
	}

	if (IS_ERR(rv))
		return rv;

	rv = fd_assign(proc, alloc_fd, src_fd, flags);
	if (IS_ERR(rv))
		return rv;

	*outfd = alloc_fd;
	return YAK_SUCCESS;
}

status_t fd_alloc_file(struct kprocess *proc, int *outfd)
{
	status_t rv;

	int fd;
	rv = fd_allocate_next(proc, &fd);
	if (IS_ERR(rv))
		return rv;

	struct fd *desc = proc->fds[fd];
	desc->file = kzalloc(sizeof(struct file));
	if (!desc->file) {
		fd_close(proc, fd);
		return YAK_OOM;
	}

	file_init(desc->file);

	*outfd = fd;
	return YAK_SUCCESS;
}

void fd_close(struct kprocess *proc, int fd)
{
	// Upon entry fd mutex is locked

	struct fd *desc = proc->fds[fd];
	assert(desc);

	if (desc->file)
		file_deref(desc->file);

	kfree(desc, 0);

	proc->fds[fd] = NULL;
}

void fd_clone(struct kprocess *old_proc, struct kprocess *new_proc)
{
	// Upon entry old proc fd mutex is locked

	new_proc->fd_cap = old_proc->fd_cap;
	new_proc->fds = kcalloc(old_proc->fd_cap, sizeof(struct fd *));
	if (!new_proc->fds)
		panic("oom in fd clone");

	for (int fd = 0; fd < old_proc->fd_cap; fd++) {
		struct fd *old_desc = old_proc->fds[fd];
		if (old_desc == NULL)
			continue;

		struct fd *desc = kzalloc(sizeof(struct fd));
		if (!desc)
			panic("oom in fd clone");

		desc->flags = old_desc->flags;
		desc->file = old_desc->file;

		file_ref(desc->file);

		new_proc->fds[fd] = desc;
	}
}
