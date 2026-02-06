#define pr_fmt(fmt) "devfs: " fmt

#include <assert.h>
#include <yak/heap.h>
#include <yak/fs/devfs.h>
#include <yak/queue.h>
#include <yak/hashtable.h>
#include <yak/fs/vfs.h>
#include <yak/macro.h>
#include <yak/status.h>
#include <yak/init.h>
#include <yak/vm/map.h>
#include <yak/vm/page.h>
#include <yak/vm/pmm.h>
#include <yak/vm/aobj.h>
#include <yak/types.h>
#include <yak/log.h>
#include <string.h>
#include <stddef.h>

struct devfs_node {
	struct vnode vnode;
	char *name;
	size_t name_len;

	size_t inode;
	size_t major, minor;

	struct device_ops *dev_ops;

	struct hashtable children;
};

struct devfs {
	struct vfs vfs;

	struct devfs_node *root;
	size_t seq_ino;
};

static struct devfs_node *create_node(struct vfs *vfs, enum vtype type,
				      int major, int minor);

static struct vnode *devfs_getroot(struct vfs *vfs)
{
	struct devfs *fs = (struct devfs *)vfs;
	if (!fs->root) {
		fs->root = create_node(vfs, VDIR, 0, 0);
	}
	return &fs->root->vnode;
}

static status_t devfs_inactive(struct vnode *vn)
{
	// TODO:free node
	return YAK_SUCCESS;
}

static status_t devfs_create(struct vnode *parent, enum vtype type, char *name,
			     struct vattr *attr, struct vnode **out)
{
	if (type != VCHR && type != VBLK && type != VDIR)
		return YAK_NOT_SUPPORTED;

	struct devfs_node *parent_node = (struct devfs_node *)parent;

	struct devfs_node *n;

	vnode_ref(parent);

	size_t name_len = strlen(name);

	if ((n = ht_get(&parent_node->children, name, name_len)) != NULL) {
		pr_debug("exists already: %s (parent: %s, n: %s)\n", name,
			 parent_node->name, n->name);
		vnode_deref(parent);
		return YAK_EXISTS;
	}

	struct devfs_node *node = create_node(parent->vfs, type, 0, 0);
	if (!node) {
		vnode_deref(parent);
		return YAK_OOM;
	}

	node->name_len = name_len;
	node->name = strndup(name, node->name_len);

	status_t ret;
	IF_ERR((ret = ht_set(&parent_node->children, name, name_len, node, 0)))
	{
		vnode_deref(parent);
		return ret;
	};

	*out = &node->vnode;
	return YAK_SUCCESS;
}

static status_t devfs_symlink(struct vnode *parent, char *name, char *path,
			      struct vattr *attr, struct vnode **out)
{
	return YAK_NOT_SUPPORTED;
}

static status_t devfs_getdents(struct vnode *vn, struct dirent *buf,
			       size_t bufsize, size_t *offset,
			       size_t *bytes_read)
{
	if (vn->type != VDIR) {
		return YAK_NODIR;
	}

	struct devfs_node *tvn = (struct devfs_node *)vn;

	char *outp = (char *)buf;
	size_t remaining = bufsize;

	size_t curr = 0;

	struct ht_entry *elm;
	HASHTABLE_FOR_EACH(&tvn->children, elm)
	{
		if (*offset > curr++) {
			continue;
		}

		struct devfs_node *child = elm->value;
		const char *name = elm->key;
		size_t namelen = elm->key_len + 1;
		size_t reclen = offsetof(struct dirent, d_name) + namelen;
		reclen = ALIGN_UP(reclen, sizeof(long));

		if (reclen > remaining) {
			break;
		}

		struct dirent *d = (struct dirent *)outp;
		d->d_ino = child->inode;
		d->d_off = 0; // ?
		d->d_reclen = (unsigned short)reclen;
		d->d_type = child->vnode.type; // DT_* = V*
		memcpy(d->d_name, name, namelen);

		outp += reclen;
		remaining -= reclen;
		*bytes_read += reclen;
	}

	return YAK_SUCCESS;
}

static status_t devfs_lookup(struct vnode *vn, char *name, struct vnode **out)
{
	if (vn->type != VDIR) {
		return YAK_NODIR;
	}

	struct devfs_node *elm = ht_get(&((struct devfs_node *)vn)->children,
					name, strlen(name));
	if (!elm)
		return YAK_NOENT;

	*out = &elm->vnode;
	return YAK_SUCCESS;
}

static status_t devfs_lock(struct vnode *vn)
{
	kmutex_acquire(&vn->lock, TIMEOUT_INFINITE);
	return YAK_SUCCESS;
}

static status_t devfs_unlock(struct vnode *vn)
{
	kmutex_release(&vn->lock);
	return YAK_SUCCESS;
}

static status_t devfs_read(struct vnode *vn, voff_t offset, void *buf,
			   size_t length, size_t *read_bytes)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (!node->dev_ops->dev_read)
		return YAK_NOT_SUPPORTED;
	return node->dev_ops->dev_read(node->minor, offset, buf, length,
				       read_bytes);
}

static status_t devfs_write(struct vnode *vn, voff_t offset, const void *buf,
			    size_t length, size_t *written_bytes)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (!node->dev_ops->dev_write)
		return YAK_NOT_SUPPORTED;
	return node->dev_ops->dev_write(node->minor, offset, buf, length,
					written_bytes);
}

static status_t devfs_open(struct vnode **vn)
{
	struct devfs_node *node = (struct devfs_node *)*vn;
	if (!node->dev_ops->dev_open)
		return YAK_SUCCESS;
	return node->dev_ops->dev_open(node->minor, vn);
}

static status_t devfs_ioctl(struct vnode *vn, unsigned long com, void *data,
			    int *ret)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (!node->dev_ops->dev_ioctl)
		return YAK_SUCCESS;
	return node->dev_ops->dev_ioctl(node->minor, com, data, ret);
}

static status_t devfs_poll(struct vnode *vn, short events, short *revents)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (!node->dev_ops->dev_poll) {
		*revents = events;
		return YAK_SUCCESS;
	}

	return node->dev_ops->dev_poll(node->minor, events, revents);
}

static status_t devfs_getattr(struct vnode *vn, struct vattr *buf)
{
	return YAK_SUCCESS;
}

static struct vn_ops devfs_vn_op = {
	.vn_lookup = devfs_lookup,
	.vn_create = devfs_create,
	.vn_lock = devfs_lock,
	.vn_unlock = devfs_unlock,
	.vn_inactive = devfs_inactive,
	.vn_getdents = devfs_getdents,
	.vn_symlink = devfs_symlink,
	.vn_readlink = NULL,
	.vn_read = devfs_read,
	.vn_write = devfs_write,
	.vn_open = devfs_open,
	.vn_ioctl = devfs_ioctl,
	.vn_poll = devfs_poll,
	.vn_getattr = devfs_getattr,
};

static status_t devfs_mount(struct vnode *vn);

static struct vfs_ops devfs_op = {
	.vfs_mount = devfs_mount,
	.vfs_getroot = devfs_getroot,
};

void devfs_init()
{
	EXPECT(vfs_register("devfs", &devfs_op));
}

INIT_ENTAILS(devfs);
INIT_DEPS(devfs, vfs_stage);
INIT_NODE(devfs, devfs_init);

static struct devfs *shared_devfs = NULL;

static status_t devfs_mount(struct vnode *vn)
{
	if (shared_devfs) {
		vnode_ref(vn);
		vn->mounted_vfs = &shared_devfs->vfs;
		return YAK_SUCCESS;
	}

	shared_devfs = kmalloc(sizeof(struct devfs));
	shared_devfs->root = NULL;
	shared_devfs->seq_ino = 1;
	shared_devfs->vfs.ops = &devfs_op;

	vnode_ref(vn);

	vn->mounted_vfs = &shared_devfs->vfs;
	VFS_GETROOT(&shared_devfs->vfs);
	shared_devfs->root->vnode.node_covered = vn;

	return YAK_SUCCESS;
}

static struct devfs_node *create_node(struct vfs *vfs, enum vtype type,
				      int major, int minor)
{
	struct devfs_node *node = kmalloc(sizeof(struct devfs_node));
	if (!node)
		return NULL;

	memset(node, 0, sizeof(struct devfs_node));

	if (type == VDIR) {
		ht_init(&node->children, ht_hash_str, ht_eq_str);
	} else {
		node->vnode.filesize = 0;

		node->major = major;
		node->minor = minor;
	}

	node->name = NULL;
	node->name_len = 0;
	node->inode = __atomic_fetch_add(&((struct devfs *)vfs)->seq_ino, 1,
					 __ATOMIC_RELAXED);

	vnode_init(&node->vnode, vfs, &devfs_vn_op, type);

	return node;
}

status_t devfs_register(char *name, int type, int major, int minor,
			struct device_ops *ops, struct vnode **out)
{
	assert(shared_devfs);
	struct vnode *vn = devfs_getroot(&shared_devfs->vfs);
	struct vattr attr;
	EXPECT(VOP_CREATE(vn, type, name, &attr, &vn));
	struct devfs_node *dnode = (struct devfs_node *)vn;
	dnode->major = major;
	dnode->minor = minor;
	dnode->dev_ops = ops;
	*out = &dnode->vnode;
	return YAK_SUCCESS;
}

void devfs_fs_mount()
{
	EXPECT(vfs_mount("/dev", "devfs"));
}

INIT_ENTAILS(fs_devfs_mount);
INIT_DEPS(fs_devfs_mount, boot_finalized_stage);
INIT_NODE(fs_devfs_mount, devfs_fs_mount);
