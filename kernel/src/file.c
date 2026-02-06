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

int fd_getnext(struct kprocess *proc)
{
	for (int i = 0; i < proc->fd_cap; i++) {
		if (proc->fds[i] == NULL)
			return i;
	}
	return -1;
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

status_t fd_alloc_at(struct kprocess *proc, int fd)
{
	status_t rv = fd_grow(proc, fd + 1);
	IF_ERR(rv) return rv;

	assert(proc->fds[fd] == NULL);

	proc->fds[fd] = kmalloc(sizeof(struct fd));
	struct fd *fdp = proc->fds[fd];
	assert(fdp);

	struct file *f = kmalloc(sizeof(struct file));
	fdp->file = f;

	file_init(f);

	return YAK_SUCCESS;
}

static status_t fd_alloc_nofile(struct kprocess *proc, int *fd)
{
	int alloc_fd;
	while ((alloc_fd = fd_getnext(proc)) == -1) {
		status_t res = fd_grow(proc, proc->fd_cap + FD_GROW_BY);
		if (IS_ERR(res))
			return res;
	}

	proc->fds[alloc_fd] = kzalloc(sizeof(struct fd));

	*fd = alloc_fd;

	//pr_debug("Alloc'd fd %d\n", *fd);

	return YAK_SUCCESS;
}

status_t fd_alloc(struct kprocess *proc, int *fd)
{
	status_t rv = fd_alloc_nofile(proc, fd);
	if (IS_ERR(rv))
		return rv;

	struct fd *fdp = proc->fds[*fd];
	assert(fdp);

	struct file *f = kzalloc(sizeof(struct file));
	assert(f);
	file_init(f);

	fdp->file = f;

	return YAK_SUCCESS;
}

struct fd *fd_safe_get(struct kprocess *proc, int fd)
{
	if (!proc->fds || fd >= proc->fd_cap) {
		return NULL;
	}

	return proc->fds[fd];
}

status_t fd_duplicate(struct kprocess *proc, int oldfd, int *newfd, int flags)
{
	guard(mutex)(&proc->fd_mutex);

	int alloc_fd = *newfd;

	struct fd *src_fd = fd_safe_get(proc, oldfd);
	if (src_fd == NULL) {
		return YAK_BADF;
	} else if (oldfd == alloc_fd) {
		return YAK_SUCCESS;
	}

	struct fd *dest_fd = NULL;

	status_t rv;

	if (alloc_fd == -1) {
		rv = fd_alloc(proc, &alloc_fd);
		if (IS_ERR(rv))
			return rv;
	} else {
		status_t rv = fd_grow(proc, alloc_fd + 1);
		IF_ERR(rv) return rv;

		struct fd **fdpp = &proc->fds[alloc_fd];
		if (*fdpp != NULL) {
			dest_fd = *fdpp;
			if (dest_fd->file != NULL) {
				file_deref(dest_fd->file);
				dest_fd->flags = 0;
				dest_fd->file = NULL;
			}
		} else {
			dest_fd = kzalloc(sizeof(struct fd));
			*fdpp = dest_fd;
		}
	}

	dest_fd = proc->fds[alloc_fd];
	assert(dest_fd != NULL);

	dest_fd->flags = src_fd->flags;
	if (flags & FD_DUPE_NOCLOEXEC)
		dest_fd->flags &= ~FD_CLOEXEC;
	else if (flags & FD_DUPE_CLOEXEC)
		dest_fd->flags |= FD_CLOEXEC;

	dest_fd->file = src_fd->file;
	file_ref(dest_fd->file);

	*newfd = alloc_fd;
	return YAK_SUCCESS;
}

void fd_close(struct kprocess *proc, int fd)
{
	// Upon entry fd mutex is locked

	struct fd *desc = proc->fds[fd];
	assert(desc);

	proc->fds[fd] = NULL;

	if (desc->file)
		file_deref(desc->file);
	kfree(desc, 0);
}
