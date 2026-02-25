#define pr_fmt(fmt) "devfs: " fmt

#include <string.h>
#include <yak/log.h>
#include <yak/heap.h>
#include <yak/init.h>
#include <yak/timer.h>
#include <yak/fs/devfs.h>

#include "tmpfs.h"

struct devfs_node {
	struct tmpfs_node tmp_node;
	struct device_ops *dev_ops;
	int major, minor;
};

struct devfs {
	struct tmpfs tmpfs;
};

extern struct vn_ops tmpfs_vn_op;

static struct devfs_node *create_node(struct vfs *vfs, enum vtype type,
				      int major, int minor);

static struct vnode *devfs_getroot(struct vfs *vfs)
{
	struct tmpfs *fs = (struct tmpfs *)vfs;
	return &fs->root->vnode;
}

static status_t devfs_inactive(struct vnode *vn)
{
	// TODO:free node when nlinks=0
	return YAK_SUCCESS;
}

static void free_node(struct vnode *vn)
{
	tmpfs_deinit_node(vn);
	kfree(vn, sizeof(struct devfs_node));
}

static struct vn_ops devfs_vn_op;

static status_t alloc_node(struct vfs *vfs, enum vtype type,
			   struct devfs_node **out)
{
	struct devfs_node *node = kzalloc(sizeof(*node));
	if (!node) {
		*out = NULL;
		return YAK_OOM;
	}

	status_t rv = tmpfs_init_node(vfs, (struct vnode *)node, type);
	if (IS_ERR(rv)) {
		kfree(node, sizeof(*node));
		*out = NULL;
		return rv;
	}

	((struct vnode *)node)->ops = &devfs_vn_op;

	*out = node;
	return YAK_SUCCESS;
}

static status_t devfs_create(struct vnode *parent, enum vtype type, char *name,
			     struct vattr *initial_attr, struct vnode **out)
{
	status_t rv;

	// Parent lifetime is managed by caller (see vfs_create)
	struct tmpfs_node *parent_node = TO_TMP(parent);

	struct tmpfs_node *n = NULL;

	size_t name_len = strlen(name);

	if ((n = ht_get(&parent_node->children, name, name_len)) != NULL) {
		pr_debug("exists already: %s (parent: %s, n: %s)\n", name,
			 parent_node->name, n->name);
		return YAK_EXISTS;
	}

	struct devfs_node *dnode;
	TRY(alloc_node(parent->vfs, type, &dnode));
	struct tmpfs_node *node = &dnode->tmp_node;

	node->name_len = name_len;
	node->name = strndup(name, node->name_len);

	// All directories have '.' as well
	node->vattr.nlinks = (type == VDIR) ? 2 : 1;

	VOP_SETATTR(&node->vnode, SETATTR_ALL, initial_attr);

	// Try to add link to parent first -> less to cleanup
	if (type == VDIR) {
		rv = ht_set(&node->children, "..", 2, parent_node, true);
		if (IS_ERR(rv)) {
			free_node(TO_VN(node));
			return rv;
		}
		parent_node->vattr.nlinks++;
	}

	// Finally add the named link in parent
	rv = ht_set(&parent_node->children, name, name_len, node, false);
	if (IS_ERR(rv)) {
		free_node(TO_VN(node));
		parent_node->vattr.nlinks--;
		return rv;
	}

	*out = &node->vnode;
	return YAK_SUCCESS;
}

#define CHECK_OP(op) (!node->dev_ops || !node->dev_ops->dev_read)

static status_t devfs_read(struct vnode *vn, voff_t offset, void *buf,
			   size_t length, size_t *read_bytes)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (CHECK_OP(dev_read)) {
		pr_warn("fallback to tmpfs\n");
		return tmpfs_vn_op.vn_read(vn, offset, buf, length, read_bytes);
	}
	return node->dev_ops->dev_read(node->minor, offset, buf, length,
				       read_bytes);
}

static status_t devfs_write(struct vnode *vn, voff_t offset, const void *buf,
			    size_t length, size_t *written_bytes)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (CHECK_OP(dev_write)) {
		pr_warn("fallback to tmpfs\n");
		return tmpfs_vn_op.vn_write(vn, offset, buf, length,
					    written_bytes);
	}
	return node->dev_ops->dev_write(node->minor, offset, buf, length,
					written_bytes);
}

static status_t devfs_open(struct vnode **vn)
{
	struct devfs_node *node = (struct devfs_node *)*vn;
	if (CHECK_OP(dev_open))
		return tmpfs_vn_op.vn_open(vn);
	return node->dev_ops->dev_open(node->minor, vn);
}

static status_t devfs_ioctl(struct vnode *vn, unsigned long com, void *data,
			    int *ret)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (CHECK_OP(dev_ioctl))
		return tmpfs_vn_op.vn_ioctl(vn, com, data, ret);
	return node->dev_ops->dev_ioctl(node->minor, com, data, ret);
}

static status_t devfs_poll(struct vnode *vn, short mask, short *ret)
{
	struct devfs_node *node = (struct devfs_node *)vn;
	if (CHECK_OP(dev_poll))
		return tmpfs_vn_op.vn_poll(vn, mask, ret);
	return node->dev_ops->dev_poll(node->minor, mask, ret);
}

static struct vn_ops devfs_vn_op = {
	.vn_create = devfs_create,
	.vn_inactive = devfs_inactive,
	.vn_read = devfs_read,
	.vn_write = devfs_write,
	.vn_open = devfs_open,
	.vn_ioctl = devfs_ioctl,
	.vn_poll = devfs_poll,
};

static status_t devfs_mount(struct vnode *vn);

static struct vfs_ops devfs_op = {
	.vfs_mount = devfs_mount,
	.vfs_getroot = devfs_getroot,
};

void devfs_init()
{
	vfs_inherit_vn_ops(&devfs_vn_op, &tmpfs_vn_op);
	EXPECT(vfs_register("devfs", &devfs_op));
}

INIT_ENTAILS(devfs);
INIT_DEPS(devfs, vfs_stage, tmpfs);
INIT_NODE(devfs, devfs_init);

static struct devfs *shared_devfs = NULL;

static status_t devfs_mount(struct vnode *vn)
{
	if (shared_devfs) {
		vnode_ref(vn);
		vn->mounted_vfs = (struct vfs *)&shared_devfs;
		return YAK_SUCCESS;
	}

	shared_devfs = kmalloc(sizeof(struct devfs));
	struct tmpfs *tmpfs = (struct tmpfs *)shared_devfs;
	tmpfs->seq_ino = 1;
	tmpfs->vfs.ops = &devfs_op;

	TRY(alloc_node(&tmpfs->vfs, VDIR, (struct devfs_node **)&tmpfs->root));

	vnode_ref(vn);
	vn->mounted_vfs = &tmpfs->vfs;
	tmpfs->root->vnode.node_covered = vn;

	return YAK_SUCCESS;
}

status_t devfs_register(char *name, int type, int major, int minor,
			struct device_ops *ops, struct vnode **out)
{
	assert(shared_devfs);

	struct vnode *vn = devfs_getroot((struct vfs *)shared_devfs);

	struct vattr attr;

	struct timespec now = time_now();
	attr.mtime = now;
	attr.atime = now;
	attr.btime = now;
	attr.uid = 0;
	attr.gid = 0;
	attr.mode = 0600;

	EXPECT(VOP_CREATE(vn, type, name, &attr, &vn));

	struct devfs_node *dnode = (struct devfs_node *)vn;
	struct tmpfs_node *tnode = (struct tmpfs_node *)dnode;
	dnode->major = major;

	dnode->minor = minor;
	tnode->vattr.major = major;

	tnode->vattr.minor = minor;

	dnode->dev_ops = ops;

	*out = (struct vnode *)dnode;
	return YAK_SUCCESS;
}

void devfs_fs_mount()
{
	EXPECT(vfs_mount("/dev", "devfs"));
}

INIT_ENTAILS(fs_devfs_mount);
INIT_DEPS(fs_devfs_mount, devfs, boot_finalized_stage);
INIT_NODE(fs_devfs_mount, devfs_fs_mount);
