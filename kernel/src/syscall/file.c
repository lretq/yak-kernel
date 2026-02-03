#include <stddef.h>
#include <string.h>
#include <yak/file.h>
#include <yak/process.h>
#include <yak/mutex.h>
#include <yak/types.h>
#include <yak/heap.h>
#include <yak/refcount.h>
#include <yak/cpudata.h>
#include <yak/syscall.h>
#include <yak/log.h>
#include <yak/fs/vfs.h>
#include <yak/status.h>
#include <yak-abi/stat.h>
#include <yak-abi/errno.h>
#include <yak-abi/seek-whence.h>
#include <yak-abi/fcntl.h>
#include <yak-abi/mode_t.h>

#include "common.h"

static unsigned int convert_accmode(unsigned int flags)
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

static void vattr_fill(struct kprocess *proc, struct vattr *attr, mode_t mode)
{
	// XXX: correct time
	struct timespec now = time_now();
	attr->mtime = now;
	attr->atime = now;
	attr->uid = proc->euid;
	attr->gid = proc->egid;
	attr->mode = mode;
}

// On sucess: Returns a referenced VNODE
int dirfd_get(struct kprocess *proc, int dirfd, char *path, int flags,
	      struct vnode **out)
{
	if (path == NULL || *path == '\0') {
		if (!(flags & AT_EMPTY_PATH))
			return ENOENT;

		struct file *f = getfile_ref(proc, dirfd);
		if (!f) {
			return EBADF;
		}

		guard_ref_adopt(f, file);

		vnode_ref(f->vnode);

		*out = f->vnode;
		return 0;
	}

	if (*path == '/') {
		// ignore dirfd
		// vfs lookup will get the root
		*out = NULL;
		return 0;
	}

	if (dirfd == AT_FDCWD) {
		// unfalliable
		*out = process_getcwd(proc);
		return 0;
	}

	struct file *f = getfile_ref(proc, dirfd);

	if (!f) {
		return EBADF;
	}

	if (f->vnode->type != VDIR) {
		return ENOTDIR;
	}

	guard_ref_adopt(f, file);

	vnode_ref(f->vnode);

	*out = f->vnode;
	return 0;
}

// TODO: implement real permission checks!
DEFINE_SYSCALL(SYS_FACCESSAT, faccessat, int dirfd, const char *user_path,
	       int mode, int flags)
{
	pr_debug("faccessat(%d, %s, %d, %d)\n", dirfd, user_path, mode, flags);

	struct kprocess *proc = curproc();

	size_t path_len = 0;
	char *path = NULL;
	if (user_path != NULL) {
		path_len = strlen(user_path);
		// we never need to check for "" now
		if (path_len != 0) {
			path = kmalloc(path_len + 1);
			memcpy(path, user_path, path_len + 1);
		}
	}

	struct vnode *from_node = NULL;
	int dirfd_res = dirfd_get(proc, dirfd, path, flags, &from_node);
	if (dirfd_res != 0) {
		return SYS_ERR(dirfd_res);
	}

	guard_ref_adopt(from_node, vnode);

	if (path == NULL) {
		if (from_node != NULL)
			return SYS_OK(0);
	} else {
		struct vnode *vn;
		RET_ERRNO_ON_ERR(vfs_lookup_path(
			path, from_node,
			(flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
			&vn, NULL));

		vnode_deref(vn);
		kmutex_release(&vn->lock);

		return SYS_OK(0);
	}

	return SYS_ERR(EACCES);
}

DEFINE_SYSCALL(SYS_FSTATAT, fstatat, int dirfd, const char *user_path,
	       struct stat *buf, int flags)
{
	pr_debug("fstatat(%d, %s, %p, %d)\n", dirfd, user_path, buf, flags);

	struct kprocess *proc = curproc();
	size_t path_len = 0;
	char *path = NULL;
	if (user_path != NULL) {
		path_len = strlen(user_path);
		// we never need to check for "" now
		if (path_len != 0) {
			path = kmalloc(path_len + 1);
			memcpy(path, user_path, path_len + 1);
		}
	}

	struct vnode *from_node = NULL;
	int dirfd_res = dirfd_get(proc, dirfd, path, flags, &from_node);
	if (dirfd_res != 0) {
		return SYS_ERR(dirfd_res);
	}

	guard_ref_adopt(from_node, vnode);

	size_t filesize = 0;
	struct vattr attr;
	mode_t file_mode;

	if (path == NULL) {
		pr_debug("fstatat: lookup not needed\n");
		VOP_GETATTR(from_node, &attr);
		filesize = from_node->filesize;
		file_mode = vtype_to_mode(from_node->type);
	} else {
		pr_debug("fstatat: %s, from_node: %p (is AT_FDCWD: %d)\n", path,
			 from_node, dirfd == AT_FDCWD);
		struct vnode *vn;
		RET_ERRNO_ON_ERR(vfs_lookup_path(
			path, from_node,
			(flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
			&vn, NULL));

		VOP_GETATTR(vn, &attr);
		filesize = vn->filesize;
		file_mode = vtype_to_mode(vn->type);

		vnode_deref(vn);
		kmutex_release(&vn->lock);
	}

	struct stat stat;
	stat.st_dev = 0;
	stat.st_ino = attr.inode;
	stat.st_mode = file_mode | attr.mode;
	stat.st_nlink = attr.nlinks;
	stat.st_uid = attr.uid;
	stat.st_gid = attr.gid;
	stat.st_rdev = 0;
	stat.st_size = filesize;
	stat.st_atim = attr.atime;
	stat.st_mtim = attr.mtime;
	stat.st_ctim = attr.ctime;
	stat.st_btim = attr.btime;
	stat.st_blksize = attr.block_size;
	stat.st_blocks = attr.block_count;

	memcpy(buf, &stat, sizeof(struct stat));

	return SYS_OK(0);
}

// Userspace shall implement open() using openat()!
DEFINE_SYSCALL(SYS_OPENAT, openat, int dirfd, const char *user_path, int flags,
	       mode_t mode)
{
	pr_debug("sys_openat(%d, %s, %d, %d)\n", dirfd, user_path, flags, mode);
	struct kprocess *proc = curproc();

	int file_flags = convert_accmode(flags);
	if (file_flags == -1)
		return SYS_ERR(EINVAL);

	size_t path_len = 0;
	char *path = NULL;
	if (user_path != NULL) {
		path_len = strlen(user_path);
		path = kmalloc(path_len + 1);
		memcpy(path, user_path, path_len + 1);
	}

	struct vnode *from_node = NULL;
	int dirfd_res = dirfd_get(proc, dirfd, path, flags, &from_node);
	if (dirfd_res != 0) {
		return SYS_ERR(dirfd_res);
	}

	struct vnode *vn;
	status_t res = vfs_open(path, from_node,
				(flags & O_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
				&vn);
	if (res == YAK_NOENT) {
		if (flags & O_CREAT) {
			struct vattr attr;
			vattr_fill(proc, &attr, mode);
			RET_ERRNO_ON_ERR(vfs_create(path, VREG, &attr, &vn));
		} else {
			// file does not exist, we also dont want to create it
			return SYS_ERR(ENOENT);
		}
	}

	RET_ERRNO_ON_ERR(res);

	if (flags & O_DIRECTORY && vn->type != VDIR) {
		vnode_deref(vn);
		return SYS_ERR(ENOTDIR);
	}

	guard(mutex)(&proc->fd_mutex);

	int fd;
	RET_ERRNO_ON_ERR(fd_alloc(proc, &fd));

	struct fd *desc = proc->fds[fd];
	desc->flags = 0;
	if (flags & O_CLOEXEC)
		desc->flags |= FD_CLOEXEC;

	struct file *file = desc->file;
	file->vnode = vn;
	file->offset = 0;
	file->flags = file_flags;

	return SYS_OK(fd);
}

DEFINE_SYSCALL(SYS_CLOSE, close, int fd)
{
	pr_debug("sys_close(%d)\n", fd);
	struct kprocess *proc = curproc();

	kmutex_acquire(&proc->fd_mutex, TIMEOUT_INFINITE);
	struct fd *desc = fd_safe_get(proc, fd);
	if (!desc) {
		kmutex_release(&proc->fd_mutex);
		return SYS_ERR(EBADF);
	}
	proc->fds[fd] = NULL;
	kmutex_release(&proc->fd_mutex);

	struct file *file = desc->file;

	kfree(desc, sizeof(struct fd));

	file_deref(file);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_DUP, dup, int fd)
{
	pr_debug("sys_dup(%d)\n", fd);
	struct kprocess *proc = curproc();
	int newfd = -1;
	status_t rv = fd_duplicate(proc, fd, &newfd, 0);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(newfd);
}

DEFINE_SYSCALL(SYS_DUP2, dup2, int oldfd, int newfd)
{
	pr_debug("sys_dup2(%d %d)\n", oldfd, newfd);
	struct kprocess *proc = curproc();
	status_t rv = fd_duplicate(proc, oldfd, &newfd, 0);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(newfd);
}

DEFINE_SYSCALL(SYS_WRITE, write, int fd, const char *buf, size_t count)
{
	pr_debug("sys_write: %d %p %ld\n", fd, buf, count);

	struct kprocess *proc = curproc();
	struct file *file;

	{
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			return SYS_ERR(EBADF);
		}
		file = desc->file;
		file_ref(file);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_WRITE)) {
		return SYS_ERR(EBADF);
	}

	off_t offset = __atomic_load_n(&file->offset, __ATOMIC_SEQ_CST);
	size_t written = 0;
	status_t res = vfs_write(file->vnode, offset, buf, count, &written);
	RET_ERRNO_ON_ERR(res);
	__atomic_fetch_add(&file->offset, written, __ATOMIC_SEQ_CST);

	return SYS_OK(written);
}

DEFINE_SYSCALL(SYS_READ, read, int fd, char *buf, size_t count)
{
	pr_debug("sys_read: %d %p %ld\n", fd, buf, count);

	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_READ)) {
		return SYS_ERR(EBADF);
	}

	off_t offset = __atomic_load_n(&file->offset, __ATOMIC_SEQ_CST);
	size_t delta = 0;
	status_t res = vfs_read(file->vnode, offset, buf, count, &delta);
	RET_ERRNO_ON_ERR(res);
	__atomic_fetch_add(&file->offset, delta, __ATOMIC_SEQ_CST);

	return SYS_OK(delta);
}

DEFINE_SYSCALL(SYS_SEEK, seek, int fd, off_t offset, int whence)
{
	struct kprocess *proc = curproc();
	guard(mutex)(&proc->fd_mutex);

	struct fd *desc = proc->fds[fd];
	if (!desc) {
		return SYS_ERR(EBADF);
	}

	struct file *file = desc->file;

	switch (whence) {
	case SEEK_SET:
		file->offset = offset;
		break;
	case SEEK_CUR:
		if (file->offset + offset < 0)
			return SYS_ERR(EINVAL);
		file->offset += offset;
		break;
	case SEEK_END:
		VOP_LOCK(file->vnode);
		file->offset = file->vnode->filesize + offset;
		VOP_UNLOCK(file->vnode);
		break;
	default:
		pr_warn("sys_seek(): unknown whence %d\n", whence);
		return SYS_ERR(EINVAL);
	}

	return SYS_OK(file->offset);
}

/* Implements fallocate like linux */
DEFINE_SYSCALL(SYS_FALLOCATE, fallocate, int fd, int mode, off_t offset,
	       off_t size)
{
	struct kprocess *proc = curproc();
	struct file *file;

	{
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			return SYS_ERR(EBADF);
		}
		file = desc->file;
		file_ref(file);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_WRITE)) {
		return SYS_ERR(EBADF);
	}

	status_t rv = VOP_FALLOCATE(file->vnode, mode, offset, size);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_FCNTL, fcntl, int fd, int op, size_t arg)
{
	pr_debug("sys_fcntl(%d, %d, %ld)\n", fd, op, arg);

	struct kprocess *proc = curproc();

	status_t rv = YAK_INVALID_ARGS;

	switch (op) {
	case F_DUPFD:
		int newfd = -1;
		rv = fd_duplicate(proc, fd, &newfd, FD_DUPE_NOCLOEXEC);
		break;
	case F_GETFD: {
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			rv = YAK_BADF;
			break;
		}
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
		struct file *file;

		{
			guard(mutex)(&proc->fd_mutex);
			struct fd *desc = fd_safe_get(proc, fd);
			if (!desc) {
				return SYS_ERR(EBADF);
			}
			file = desc->file;
			file_ref(file);
		}

		guard_ref_adopt(file, file);

		return SYS_OK(file->flags);
	}
	case F_SETFL: {
		struct file *file;

		{
			guard(mutex)(&proc->fd_mutex);
			struct fd *desc = fd_safe_get(proc, fd);
			if (!desc) {
				return SYS_ERR(EBADF);
			}
			file = desc->file;
			file_ref(file);
		}

		guard_ref_adopt(file, file);

		// TODO: mask off args
		// only allow certain flags
		// and actually set some when they get meaning

		return SYS_OK(0);
	}
	default:
		pr_warn("unimplemented fcntl op: %d\n", op);
		break;
	}

	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_IOCTL, ioctl, int fd, unsigned long op, void *argp)
{
	pr_debug("sys_ioctl(%d, %lu, %p)\n", fd, op, argp);

	struct kprocess *proc = curproc();
	struct file *file;

	{
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			return SYS_ERR(EBADF);
		}
		file = desc->file;
		file_ref(file);
	}

	guard_ref_adopt(file, file);

	if (file->vnode->type != VCHR && file->vnode->type != VBLK) {
		return SYS_ERR(ENOTTY);
	}

	int ret = 0;
	status_t rv = vfs_ioctl(file->vnode, op, argp, &ret);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(ret);
}

DEFINE_SYSCALL(SYS_FCHDIR, fchdir, int fd)
{
	struct kprocess *proc = curproc();
	struct file *file;

	{
		guard(mutex)(&proc->fd_mutex);
		struct fd *desc = fd_safe_get(proc, fd);
		if (!desc) {
			return SYS_ERR(EBADF);
		}
		file = desc->file;
		file_ref(file);
	}

	guard_ref_adopt(file, file);

	if (file->vnode->type != VDIR) {
		return SYS_ERR(ENOTDIR);
	}

	vnode_ref(file->vnode);
	process_setcwd(proc, file->vnode);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_CHDIR, chdir, const char *path)
{
	pr_debug("chdir(%s)\n", path);

	struct kprocess *proc = curproc();

	struct vnode *vn;
	RET_ERRNO_ON_ERR(vfs_lookup_path(path, NULL, 0, &vn, NULL));

	process_setcwd(proc, vn);

	VOP_UNLOCK(vn);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_GETDENTS, getdents, int fd, void *buffer, size_t max_size)
{
	pr_debug("getdents(%d, %p, %ld)\n", fd, buffer, max_size);
	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_READ)) {
		return SYS_ERR(EBADF);
	}

	if (file->vnode->type != VDIR) {
		return SYS_ERR(ENOTDIR);
	}

	struct vnode *vn = file->vnode;

	size_t offset = file->offset;

	size_t bytes_read;
	RET_ERRNO_ON_ERR(
		VOP_GETDENTS(vn, buffer, max_size, &offset, &bytes_read));

	file->offset = offset;
	pr_debug("getdents: read %ld; new offset: %ld\n", bytes_read, offset);

	return SYS_OK(bytes_read);
}
