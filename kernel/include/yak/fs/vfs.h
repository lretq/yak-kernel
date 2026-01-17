#pragma once

#include <stddef.h>
#include <yak/types.h>
#include <yak/timespec.h>
#include <yak/status.h>
#include <yak/mutex.h>
#include <yak/refcount.h>
#include <yak/vmflags.h>
#include <yak-abi/blkcnt_t.h>
#include <yak-abi/blksize_t.h>
#include <yak-abi/gid_t.h>
#include <yak-abi/ino_t.h>
#include <yak-abi/mode_t.h>
#include <yak-abi/nlink_t.h>
#include <yak-abi/uid_t.h>

struct vm_map;

struct vnode;

struct vfs {
	struct vfs_ops *ops;
};

struct vfs_ops {
	status_t (*vfs_mount)(struct vnode *vroot);
	status_t (*vfs_unmount)(struct vfs *vfsp);
	struct vnode *(*vfs_getroot)(struct vfs *vfsp);
};

#define VFS_GETROOT(vfs) (vfs)->ops->vfs_getroot(vfs)

enum vtype {
	VREG = 1,
	VBLK,
	VDIR,
	VCHR,
	VFIFO,
	VLNK,
	VSOCK,
};

#define DT_REG 1
#define DT_DIR 3

#define SETATTR_MODE (1 << 0)
#define SETATTR_UID (1 << 1)
#define SETATTR_GID (1 << 2)
#define SETATTR_ATIME (1 << 3)
#define SETATTR_MTIME (1 << 4)

#define SETATTR_ALL \
	SETATTR_MODE | SETATTR_UID | SETATTR_GID | SETATTR_ATIME | SETATTR_MTIME

struct vattr {
	// protection: only contains access modes
	mode_t mode;

	// inode number
	ino_t inode;

	// dev
	int major;
	int minor;

	// rdev ??
	// XXX: figure this out
	int rmajor;
	int rminor;

	// number of hard links
	nlink_t nlinks;

	// owning user ID
	uid_t uid;
	// owning group ID
	gid_t gid;

	// time of last access
	struct timespec atime;
	// time of last modification
	struct timespec mtime;
	// time of last attribute update
	struct timespec ctime;
	// time of creation
	struct timespec btime;

	// optimal block size
	blksize_t block_size;

	// number of 512B blocks allocated
	blkcnt_t block_count;
};

struct vnode {
	struct vn_ops *ops;
	enum vtype type;

	refcount_t refcnt;
	struct kmutex lock;

	struct vfs *vfs;
	struct vfs *mounted_vfs;
	struct vnode *node_covered;

	size_t filesize;

	struct vm_object *vobj;

	int flags;
};

struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

struct vn_ops {
	status_t (*vn_lookup)(struct vnode *vp, char *name, struct vnode **out);

	status_t (*vn_create)(struct vnode *vp, enum vtype type, char *name,
			      struct vattr *attr, struct vnode **out);

	status_t (*vn_symlink)(struct vnode *parent, char *name, char *path,
			       struct vattr *attr, struct vnode **out);

	status_t (*vn_lock)(struct vnode *vp);
	status_t (*vn_unlock)(struct vnode *vp);

	status_t (*vn_inactive)(struct vnode *vp);

	status_t (*vn_getdents)(struct vnode *vp, struct dirent *buf,
				size_t bufsize, size_t *bytes_read);

	status_t (*vn_readlink)(struct vnode *vn, char **path);

	status_t (*vn_read)(struct vnode *vn, voff_t offset, void *buf,
			    size_t length, size_t *read_bytes);

	status_t (*vn_write)(struct vnode *vp, voff_t offset, const void *buf,
			     size_t length, size_t *written_bytes);

	status_t (*vn_open)(struct vnode **vp);

	status_t (*vn_ioctl)(struct vnode *vp, unsigned long com, void *data,
			     int *ret);

	status_t (*vn_mmap)(struct vnode *vp, struct vm_map *map, size_t length,
			    voff_t offset, vm_prot_t prot,
			    vm_inheritance_t inheritance, vaddr_t hint,
			    int flags, vaddr_t *out);

	status_t (*vn_fallocate)(struct vnode *vp, int mode, off_t offset,
				 off_t size);

	status_t (*vn_getattr)(struct vnode *vp, struct vattr *vattr);

	status_t (*vn_setattr)(struct vnode *vp, unsigned int what,
			       struct vattr *vattr);
};

#define VOP_INIT(vn, vfs_, ops_, type_)    \
	(vn)->ops = ops_;                  \
	(vn)->type = type_;                \
	(vn)->refcnt = 1;                  \
	kmutex_init(&(vn)->lock, "vnode"); \
	(vn)->vfs = vfs_;                  \
	(vn)->mounted_vfs = NULL;          \
	(vn)->node_covered = NULL;         \
	(vn)->filesize = 0;                \
	(vn)->vobj = NULL;                 \
	(vn)->flags = 0;

#define VOP_LOOKUP(vp, name, out) vp->ops->vn_lookup(vp, name, out)

#define VOP_CREATE(vp, type, name, attr, out) \
	vp->ops->vn_create(vp, type, name, attr, out)

#define VOP_GETDENTS(vp, buf, bufsize, bytes_read) \
	vp->ops->vn_getdents(vp, buf, bufsize, bytes_read)

#define VOP_WRITE(vp, offset, buf, count) \
	vp->ops->vn_write(vp, offset, buf, count)

#define VOP_READ(vp, offset, buf, count) \
	vp->ops->vn_read(vp, offset, buf, count)

#define VOP_LOCK(vp) (vp)->ops->vn_lock(vp)
#define VOP_UNLOCK(vp) (vp)->ops->vn_unlock(vp)

#define VOP_OPEN(vp) (*(vp))->ops->vn_open(vp)

#define VOP_SYMLINK(vp, name, dest, attr, out) \
	vp->ops->vn_symlink(vp, name, dest, attr, out)

#define VOP_READLINK(vp, out) vp->ops->vn_readlink(vp, out)

#define VOP_IOCTL(vp, com, data, ret) vp->ops->vn_ioctl(vp, com, data, ret)

#define VOP_MMAP(vp, m, l, o, p, i, h, f, out) \
	vp->ops->vn_mmap(vp, m, l, o, p, i, h, f, out)

#define VOP_FALLOCATE(vp, mode, offset, size) \
	vp->ops->vn_fallocate(vp, mode, offset, size)

#define VOP_GETATTR(vp, buf) (vp)->ops->vn_getattr(vp, buf)
#define VOP_SETATTR(vp, what, attr) (vp)->ops->vn_setattr((vp), what, attr)

GENERATE_REFMAINT_INLINE(vnode, refcnt, p->ops->vn_inactive)

void vfs_init();

status_t vfs_register(const char *name, struct vfs_ops *ops);

status_t vfs_mount(const char *path, char *fsname);

status_t vfs_getdents(struct vnode *vn, struct dirent *buf, size_t bufsize,
		      size_t *bytes_read);

status_t vfs_write(struct vnode *vn, size_t offset, const void *buf,
		   size_t count, size_t *writtenp);

status_t vfs_read(struct vnode *vn, size_t offset, void *buf, size_t count,
		  size_t *readp);

status_t vfs_create(char *path, enum vtype type, struct vattr *initial_attr,
		    struct vnode **out);

status_t vfs_open(char *path, struct vnode *cwd, int lookup_flags,
		  struct vnode **out);

status_t vfs_symlink(char *link_path, char *dest_path,
		     struct vattr *initial_attr, struct vnode **out);

status_t vfs_ioctl(struct vnode *vn, unsigned long com, void *data, int *ret);

status_t vfs_mmap(struct vnode *vn, struct vm_map *map, size_t length,
		  voff_t offset, vm_prot_t prot, vm_inheritance_t inheritance,
		  vaddr_t hint, int flags, vaddr_t *out);

#define VFS_LOOKUP_PARENT (1 << 0)
#define VFS_LOOKUP_NOFOLLOW (1 << 1)

status_t vfs_lookup_path(const char *path, struct vnode *cwd, int flags,
			 struct vnode **out, char **last_comp);

struct vnode *vfs_getroot();
