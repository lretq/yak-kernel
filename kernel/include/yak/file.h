#pragma once

#include <yak/refcount.h>
#include <yak/mutex.h>

#define FILE_READ 0x1
#define FILE_WRITE 0x2

struct vnode;
struct kprocess;

struct file {
	struct vnode *vnode;
	struct kmutex lock;
	unsigned long refcnt;
	off_t offset;
	unsigned int flags;
};

struct fd {
	struct file *file;
	unsigned int flags;
};

void file_init(struct file *file);
DECLARE_REFMAINT(file);

void fd_close(struct kprocess *proc, int fd);
status_t fd_alloc_file(struct kprocess *proc, int *outfd);
status_t fd_grow(struct kprocess *proc, int new_cap);
int fd_getnext(struct kprocess *proc);

#define FD_DUPE_NOCLOEXEC 0x1
#define FD_DUPE_CLOEXEC 0x2
#define FD_DUPE_MINIMUM 0x4
status_t fd_duplicate(struct kprocess *proc, int oldfd, int newfd, int flags,
		      int *outfd);

struct fd *fd_safe_get(struct kprocess *proc, int fd);

void fd_clone(struct kprocess *old_proc, struct kprocess *new_proc);
