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

enum dirfd_result_type {
	DIRFD_BASE_DIR, // vnode is base directory vnode
	DIRFD_ROOT, // start from root
	DIRFD_VNODE, // vnode is final vnode
};

struct dirfd_result {
	struct vnode *vnode;
	enum dirfd_result_type kind;
};

// This function implements the dirfd handling for the sys_*at family of syscalls.
//
// On success, returns a referenced vnode in out.
// The vnode is either used as the base directory for path lookup,
// or is an already resolved FD when path is NULL.
//
// Behaviour depends on path and dirfd:
//
// 1) path is NULL or empty:
//    - If AT_EMPTY_PATH is not set, returns ENOENT
//    - Otherwise resolve dirfd as FD
//    - If found, return its vnode
//
// 2) path is absolute (starts with '/'):
//    - Ignores dirfd completely
//    - Later VFS lookup starts from root
//    - out is set to NULL
//
// 3) path is relative and dirfd is AT_FDCWD:
//    - Use the process' current working directory
//    - No file descriptors are looked up
//
// 4) path is relative and dirfd is a real fd:
//    - Resolve the FD
//    - Check if the FD refers to a directory
//    - Return the directory vnode
int dirfd_get(struct kprocess *proc, int dirfd, char *path, int flags,
	      struct dirfd_result *result)
{
	if (path == NULL || *path == '\0') {
		if (!(flags & AT_EMPTY_PATH))
			return ENOENT;

		struct file *file = getfile_ref(proc, dirfd);
		if (!file) {
			return EBADF;
		}

		guard_ref_adopt(file, file);

		vnode_ref(file->vnode);

		*result = (struct dirfd_result){
			.vnode = file->vnode,
			.kind = DIRFD_VNODE,
		};
		return 0;
	}

	if (*path == '/') {
		// ignore dirfd
		*result = (struct dirfd_result){
			.vnode = NULL,
			.kind = DIRFD_ROOT,
		};

		return 0;
	}

	if (dirfd == AT_FDCWD) {
		*result = (struct dirfd_result){
			.vnode = process_getcwd(proc),
			.kind = DIRFD_BASE_DIR,
		};

		return 0;
	}

	struct file *file = getfile_ref(proc, dirfd);
	guard_ref_adopt(file, file);

	if (file == NULL) {
		return EBADF;
	}

	if (file->vnode->type != VDIR) {
		return ENOTDIR;
	}

	vnode_ref(file->vnode);

	*result = (struct dirfd_result){
		.vnode = file->vnode,
		.kind = DIRFD_BASE_DIR,
	};

	return 0;
}

char *copy_user_path(const char *user_path, size_t *lengthp)
{
	size_t path_len;
	char *path = NULL;

	if (user_path == NULL)
		return NULL;

	path_len = strlen(user_path);

	if (path_len == 0)
		return NULL;

	path = kmalloc(path_len + 1);
	if (!path)
		return NULL;

	memcpy(path, user_path, path_len + 1);
	*lengthp = path_len;

	return path;
}

// TODO: implement real permission checks!
DEFINE_SYSCALL(SYS_FACCESSAT, faccessat, int dirfd, const char *user_path,
	       int mode, int flags)
{
	struct kprocess *proc = curproc();

	size_t path_len = 0;
	char *path = copy_user_path(user_path, &path_len);
	guard(autofree)(path, path_len + 1);

	struct dirfd_result dirfd_result;
	int err = dirfd_get(proc, dirfd, path, flags, &dirfd_result);
	if (err != 0) {
		return SYS_ERR(err);
	}

	struct vnode *vn = NULL;

	switch (dirfd_result.kind) {
	case DIRFD_ROOT:
	case DIRFD_BASE_DIR:
		RET_ERRNO_ON_ERR(vfs_lookup_path(
			path, dirfd_result.vnode,
			(flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
			&vn, NULL));

		VOP_UNLOCK(vn);
		vnode_deref(vn);
		return SYS_OK(0);

	case DIRFD_VNODE:
		vn = dirfd_result.vnode;
		if (vn == NULL) {
			return SYS_ERR(EACCES);
		}
		vnode_deref(vn);
		return SYS_OK(0);
	}
}

DEFINE_SYSCALL(SYS_FSTATAT, fstatat, int dirfd, const char *user_path,
	       struct stat *buf, int flags)
{
	struct kprocess *proc = curproc();
	size_t path_len = 0;
	char *path = copy_user_path(user_path, &path_len);
	guard(autofree)(path, path_len + 1);

	struct dirfd_result dirfd_result;
	int err = dirfd_get(proc, dirfd, path, flags, &dirfd_result);
	if (err != 0) {
		return SYS_ERR(err);
	}

	struct vnode *vn = NULL;

	switch (dirfd_result.kind) {
	case DIRFD_ROOT:
	case DIRFD_BASE_DIR:
		RET_ERRNO_ON_ERR(vfs_lookup_path(
			path, dirfd_result.vnode,
			(flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
			&vn, NULL));
		break;
	case DIRFD_VNODE:
		vn = dirfd_result.vnode;
		assert(vn);
		VOP_LOCK(vn);
		break;
	}

	// vn is ref'd and locked now

	size_t filesize = vn->filesize;
	struct vattr attr;
	mode_t file_mode;

	status_t rv = VOP_GETATTR(vn, &attr);
	if (IS_ERR(rv)) {
		VOP_UNLOCK(vn);
		vnode_deref(vn);
		return SYS_ERR(status_errno(rv));
	}

	file_mode = vtype_to_mode(vn->type);

	VOP_UNLOCK(vn);
	vnode_deref(vn);

	struct stat stat;
#define TODEV(major, minor) (((major & 0xFFF) << 8) + (minor & 0xFF))
	stat.st_dev = TODEV(attr.major, attr.minor);
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
	struct kprocess *proc = curproc();

	int file_flags = rwmode_to_internal(flags);
	if (file_flags == -1)
		return SYS_ERR(EINVAL);

	size_t path_len = 0;
	char *path = copy_user_path(user_path, &path_len);
	guard(autofree)(path, path_len + 1);

	struct dirfd_result dirfd_result;
	int err = dirfd_get(proc, dirfd, path, flags, &dirfd_result);
	if (err != 0) {
		return SYS_ERR(err);
	}

	status_t rv;
	struct vnode *vn = NULL;

	switch (dirfd_result.kind) {
	case DIRFD_ROOT:
	case DIRFD_BASE_DIR:
		rv = vfs_open(path, dirfd_result.vnode,
			      (flags & O_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
			      &vn);

		if (rv == YAK_NOENT) {
			if (flags & O_CREAT) {
				struct vattr attr;
				vattr_fill(proc, &attr, mode);
				rv = vfs_create(path, VREG, &attr, &vn);
			}
		}

		RET_ERRNO_ON_ERR(rv);

		if (dirfd_result.vnode)
			vnode_deref(dirfd_result.vnode);

		break;
	case DIRFD_VNODE:
		vn = dirfd_result.vnode;

		rv = VOP_OPEN(&vn);

		if (IS_ERR(rv)) {
			vnode_deref(vn);
			return SYS_ERR(status_errno(rv));
		}

		break;
	}

	if (flags & O_DIRECTORY && vn->type != VDIR) {
		vnode_deref(vn);
		return SYS_ERR(ENOTDIR);
	}

	guard(mutex)(&proc->fd_mutex);

	int fd;
	rv = fd_alloc_file(proc, &fd);
	if (IS_ERR(rv)) {
		vnode_deref(vn);
		return SYS_ERR(status_errno(rv));
	}

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
	struct kprocess *proc = curproc();

	pr_info("sys_close: pid %lld\n", proc->pid);

	guard(mutex)(&proc->fd_mutex);

	struct fd *desc = fd_safe_get(proc, fd);
	if (!desc) {
		return SYS_ERR(EBADF);
	}

	fd_close(proc, fd);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_DUP, dup, int fd)
{
	struct kprocess *proc = curproc();
	int alloc_fd;
	status_t rv = fd_duplicate(proc, fd, -1, FD_DUPE_NOCLOEXEC, &alloc_fd);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(alloc_fd);
}

DEFINE_SYSCALL(SYS_DUP2, dup2, int oldfd, int newfd)
{
	struct kprocess *proc = curproc();
	status_t rv =
		fd_duplicate(proc, oldfd, newfd, FD_DUPE_NOCLOEXEC, &newfd);
	RET_ERRNO_ON_ERR(rv);

	pr_debug("dup2 from %d to %d\n", oldfd, newfd);
	pr_debug("now file ptr to %p\n", proc->fds[newfd]->file);

	return SYS_OK(newfd);
}

DEFINE_SYSCALL(SYS_WRITE, write, int fd, const char *buf, size_t count)
{
	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_WRITE)) {
		return SYS_ERR(EBADF);
	}

	off_t offset = __atomic_load_n(&file->offset, __ATOMIC_SEQ_CST);
	size_t written = 0;
	RET_ERRNO_ON_ERR(VOP_WRITE(file->vnode, offset, buf, count, &written));
	__atomic_fetch_add(&file->offset, written, __ATOMIC_SEQ_CST);

	return SYS_OK(written);
}

DEFINE_SYSCALL(SYS_PWRITE, pwrite, int fd, const char *buf, size_t count,
	       off_t offset)
{
	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_WRITE)) {
		return SYS_ERR(EBADF);
	}

	size_t written = 0;
	RET_ERRNO_ON_ERR(VOP_WRITE(file->vnode, offset, buf, count, &written));

	return SYS_OK(written);
}

DEFINE_SYSCALL(SYS_READ, read, int fd, char *buf, size_t count)
{
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
	RET_ERRNO_ON_ERR(VOP_READ(file->vnode, offset, buf, count, &delta));
	__atomic_fetch_add(&file->offset, delta, __ATOMIC_SEQ_CST);

	return SYS_OK(delta);
}

DEFINE_SYSCALL(SYS_PREAD, pread, int fd, char *buf, size_t count, off_t offset)
{
	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}
	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_READ)) {
		return SYS_ERR(EBADF);
	}

	size_t delta = 0;
	RET_ERRNO_ON_ERR(VOP_READ(file->vnode, offset, buf, count, &delta));

	return SYS_OK(delta);
}

DEFINE_SYSCALL(SYS_SEEK, seek, int fd, off_t offset, int whence)
{
	struct kprocess *proc = curproc();
	guard(mutex)(&proc->fd_mutex);

	struct fd *desc = fd_safe_get(proc, fd);

	if (!desc) {
		return SYS_ERR(EBADF);
	}

	struct file *file = desc->file;

	enum vtype typ = file->vnode->type;
	if (typ != VREG) {
		return SYS_ERR(ESPIPE);
	}

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

	file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}

	guard_ref_adopt(file, file);

	if (!(file->flags & FILE_WRITE)) {
		return SYS_ERR(EBADF);
	}

	status_t rv = VOP_FALLOCATE(file->vnode, mode, offset, size);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_IOCTL, ioctl, int fd, unsigned long op, void *argp)
{
	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
	}
	guard_ref_adopt(file, file);

	if (file->vnode->type != VCHR && file->vnode->type != VBLK) {
		return SYS_ERR(ENOTTY);
	}

	int ret = 0;
	status_t rv = VOP_IOCTL(file->vnode, op, argp, &ret);
	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(ret);
}

DEFINE_SYSCALL(SYS_FCHDIR, fchdir, int fd)
{
	struct kprocess *proc = curproc();

	struct file *file = getfile_ref(proc, fd);
	if (!file) {
		return SYS_ERR(EBADF);
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
	struct kprocess *proc = curproc();

	struct vnode *vn;
	RET_ERRNO_ON_ERR(vfs_lookup_path(path, NULL, 0, &vn, NULL));

	if (vn->type != VDIR) {
		// lookup successful but wrong type
		VOP_UNLOCK(vn);
		vnode_deref(vn);
		return SYS_ERR(ENOTDIR);
	}

	process_setcwd(proc, vn);

	VOP_UNLOCK(vn);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_GETDENTS, getdents, int fd, void *buffer, size_t max_size)
{
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

	VOP_LOCK(vn);

	size_t bytes_read;
	RET_ERRNO_ON_ERR(
		VOP_GETDIRENTS(vn, buffer, max_size, &offset, &bytes_read));

	VOP_UNLOCK(vn);

	file->offset = offset;
	//pr_debug("getdents: read %ld; new offset: %ld\n", bytes_read, offset);

	return SYS_OK(bytes_read);
}

DEFINE_SYSCALL(SYS_READLINKAT, readlinkat, int dirfd, const char *user_path,
	       void *buffer, size_t max_size)
{
	struct kprocess *proc = curproc();

	size_t path_len;
	char *path = copy_user_path(user_path, &path_len);
	guard(autofree)(path, path_len + 1);

	struct dirfd_result res;
	int err = dirfd_get(proc, dirfd, path, AT_EMPTY_PATH, &res);
	if (err != 0) {
		return SYS_ERR(err);
	}

	struct vnode *vn = NULL;

	switch (res.kind) {
	case DIRFD_ROOT:
	case DIRFD_BASE_DIR:
		RET_ERRNO_ON_ERR(vfs_lookup_path(
			path, res.vnode, VFS_LOOKUP_NOFOLLOW, &vn, NULL));
		break;

	case DIRFD_VNODE:
		vn = res.vnode;
		break;
	}

	if (vn == NULL)
		return SYS_ERR(ENOENT);

	if (vn->type != VLNK) {
		vnode_deref(vn);
		return SYS_ERR(EINVAL);
	}

	char *link;
	RET_ERRNO_ON_ERR(VOP_READLINK(vn, &link));
	guard(autofree)(link, 0);

	size_t copy_len = MIN(strlen(link), max_size);
	memcpy(buffer, link, copy_len);

	VOP_UNLOCK(vn);
	vnode_deref(vn);

	return SYS_OK(copy_len);
}

DEFINE_SYSCALL(SYS_SYMLINKAT, symlinkat, int dirfd,
	       const char *user_target_path, const char *user_link_path)
{
	struct kprocess *proc = curproc();

	size_t target_path_len;
	char *target_path = copy_user_path(user_target_path, &target_path_len);
	guard(autofree)(target_path, target_path_len + 1);

	if (target_path == NULL) {
		return SYS_ERR(ENOENT);
	}

	size_t link_path_len;
	char *link_path = copy_user_path(user_link_path, &link_path_len);
	guard(autofree)(link_path, link_path_len + 1);

	if (link_path == NULL) {
		return SYS_ERR(ENOENT);
	}

	struct dirfd_result res;
	int err = dirfd_get(proc, dirfd, link_path, 0, &res);
	if (err != 0)
		return SYS_ERR(err);

	struct vnode *cwd;

	switch (res.kind) {
	case DIRFD_ROOT:
		cwd = NULL;
		break;

	case DIRFD_BASE_DIR:
		cwd = res.vnode;
		break;

	case DIRFD_VNODE:
		// not allowed! Also shouldn't be possible to happen
		vnode_deref(res.vnode);
		return SYS_ERR(ENOTDIR);
	}

	struct vattr attr;
	vattr_fill(proc, &attr, 0777);

	struct vnode *vn;
	RET_ERRNO_ON_ERR(vfs_symlink(link_path, target_path, &attr, cwd, &vn));
	vnode_deref(vn);

	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_LINKAT, linkat, int olddirfd, const char *user_old_path,
	       int newdirfd, const char *user_new_path, int flags)
{
	struct kprocess *proc = curproc();

	size_t old_len;
	char *old_path = copy_user_path(user_old_path, &old_len);
	guard(autofree)(old_path, old_len + 1);

	if (old_path == NULL)
		return SYS_ERR(ENOENT);

	size_t new_len;
	char *new_path = copy_user_path(user_new_path, &new_len);
	guard(autofree)(new_path, new_len + 1);

	if (new_path == NULL)
		return SYS_ERR(ENOENT);

	struct dirfd_result old_res;
	int err = dirfd_get(proc, olddirfd, old_path, 0, &old_res);
	if (err != 0)
		return SYS_ERR(err);

	struct vnode *old_cwd;

	switch (old_res.kind) {
	case DIRFD_ROOT:
		old_cwd = NULL;
		break;

	case DIRFD_BASE_DIR:
		old_cwd = old_res.vnode;
		break;

	case DIRFD_VNODE:
		vnode_deref(old_res.vnode);
		return SYS_ERR(ENOTDIR);
	}

	struct vnode *old_vn;
	RET_ERRNO_ON_ERR(vfs_lookup_path(
		old_path, old_cwd,
		(flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOFOLLOW : 0,
		&old_vn, NULL));

	// Disallow hardlink to a directory
	if (old_vn->type == VDIR) {
		VOP_UNLOCK(old_vn);
		vnode_deref(old_vn);
		return SYS_ERR(EPERM);
	}

	struct dirfd_result new_res;
	err = dirfd_get(proc, newdirfd, new_path, 0, &new_res);
	if (err != 0) {
		VOP_UNLOCK(old_vn);
		vnode_deref(old_vn);
		return SYS_ERR(err);
	}

	struct vnode *new_cwd;

	switch (new_res.kind) {
	case DIRFD_ROOT:
		new_cwd = NULL;
		break;

	case DIRFD_BASE_DIR:
		new_cwd = new_res.vnode;
		break;

	case DIRFD_VNODE:
		vnode_deref(new_res.vnode);
		VOP_UNLOCK(old_vn);
		vnode_deref(old_vn);
		return SYS_ERR(ENOTDIR);
	}

	status_t rv = vfs_link(old_vn, new_path, new_cwd);

	VOP_UNLOCK(old_vn);
	vnode_deref(old_vn);

	RET_ERRNO_ON_ERR(rv);

	return SYS_OK(0);
}
