#define pr_fmt(fmt) "tmpfs: " fmt

#include <assert.h>
#include <yak/heap.h>
#include <yak/queue.h>
#include <yak/timer.h>
#include <yak/hashtable.h>
#include <yak/fs/vfs.h>
#include <yak/macro.h>
#include <yak/status.h>
#include <yak/vm/map.h>
#include <yak/vm/page.h>
#include <yak/vm/pmm.h>
#include <yak/vm/aobj.h>
#include <yak/types.h>
#include <yak/init.h>
#include <yak/log.h>
#include <string.h>
#include <stddef.h>

struct tmpfs_node {
	struct vnode vnode;
	struct vattr vattr;

	char *name;
	size_t name_len;

	char *link_path;

	struct hashtable children;
};

struct tmpfs {
	struct vfs vfs;

	struct tmpfs_node *root;
	size_t seq_ino;
};

static struct tmpfs_node *create_node(struct vfs *vfs, enum vtype type);

static struct vnode *tmpfs_getroot(struct vfs *vfs)
{
	struct tmpfs *fs = (struct tmpfs *)vfs;
	return &fs->root->vnode;
}

static status_t tmpfs_inactive(struct vnode *vn)
{
	// TODO:free node
	return YAK_SUCCESS;
}

static status_t tmpfs_setattr(struct vnode *vn, unsigned int what,
			      struct vattr *attr)
{
	struct tmpfs_node *tvn = (struct tmpfs_node *)vn;
	struct vattr *t_atr = &tvn->vattr;
	if (what & SETATTR_ATIME)
		t_atr->atime = attr->atime;
	if (what & SETATTR_MTIME)
		t_atr->mtime = attr->mtime;
	if (what & SETATTR_GID)
		t_atr->gid = attr->gid;
	if (what & SETATTR_UID)
		t_atr->uid = attr->uid;
	if (what & SETATTR_MODE)
		t_atr->mode = attr->mode;

	// if anything was modified, update ctime too
	if (what & SETATTR_ALL)
		t_atr->ctime = time_now();

	return YAK_SUCCESS;
}

static status_t tmpfs_getattr(struct vnode *vn, struct vattr *attr)
{
	struct tmpfs_node *tvn = (struct tmpfs_node *)vn;
	memcpy(attr, &tvn->vattr, sizeof(struct vattr));

	attr->block_count = ALIGN_UP(vn->filesize, PAGE_SIZE) / 512;

	return YAK_SUCCESS;
}

static status_t tmpfs_create(struct vnode *parent, enum vtype type, char *name,
			     struct vattr *initial_attr, struct vnode **out)
{
	struct tmpfs_node *parent_node = (struct tmpfs_node *)parent;

	struct tmpfs_node *n;

	size_t name_len = strlen(name);

	if ((n = ht_get(&parent_node->children, name, name_len)) != NULL) {
		pr_debug("exists already: %s (parent: %s, n: %s)\n", name,
			 parent_node->name, n->name);
		return YAK_EXISTS;
	}

	struct tmpfs_node *node = create_node(parent->vfs, type);
	if (type == VREG) {
		assert(node->vnode.vobj != NULL);
	}
	if (!node) {
		return YAK_OOM;
	}

	node->name_len = name_len;
	node->name = strndup(name, node->name_len);

	node->vattr.nlinks = (type == VDIR) ? 2 : 1;
	tmpfs_setattr(&node->vnode, SETATTR_ALL, initial_attr);

	status_t ret;
	IF_ERR((ret = ht_set(&parent_node->children, name, name_len, node, 0)))
	{
		return ret;
	};

	if (type == VDIR) {
		// XXX: but muh errors :(
		EXPECT(ht_set(&node->children, "..", 2, parent_node, 1));
		parent_node->vattr.nlinks++;
	}

	vnode_ref(parent);

	*out = &node->vnode;
	return YAK_SUCCESS;
}

static status_t tmpfs_symlink(struct vnode *parent, char *name, char *path,
			      struct vattr *attr, struct vnode **out)
{
	char *path_copy = strdup(path);

	struct vnode *linkvn;
	status_t rv = tmpfs_create(parent, VLNK, name, attr, &linkvn);
	IF_ERR(rv)
	{
		kfree(path_copy, 0);
		return rv;
	}

	struct tmpfs_node *tmpfs_node = (struct tmpfs_node *)linkvn;
	tmpfs_node->link_path = path_copy;

	*out = linkvn;

	return YAK_SUCCESS;
}

static status_t tmpfs_readlink(struct vnode *vn, char **path)
{
	if (vn->type != VLNK || path == NULL)
		return YAK_INVALID_ARGS;

	*path = strdup(((struct tmpfs_node *)vn)->link_path);

	return YAK_SUCCESS;
}

static status_t tmpfs_getdents(struct vnode *vn, struct dirent *buf,
			       size_t bufsize, size_t *offset,
			       size_t *bytes_read)
{
	if (vn->type != VDIR) {
		return YAK_NODIR;
	}

	struct tmpfs_node *tvn = (struct tmpfs_node *)vn;

	char *outp = (char *)buf;
	size_t remaining = bufsize;
	size_t curr = 0;
	size_t read = 0;

	struct ht_entry *elm;
	HASHTABLE_FOR_EACH(&tvn->children, elm)
	{
		if (*offset > curr++) {
			continue;
		}

		struct tmpfs_node *child = elm->value;
		const char *name = elm->key;
		size_t namelen = elm->key_len + 1;
		size_t reclen = offsetof(struct dirent, d_name) + namelen;
		reclen = ALIGN_UP(reclen, sizeof(long));

		if (reclen > remaining) {
			break;
		}

		struct dirent *d = (struct dirent *)outp;
		d->d_ino = child->vattr.inode;
		d->d_off = 0; // ?
		d->d_reclen = (unsigned short)reclen;
		d->d_type = vtype_to_dtype(child->vnode.type);
		memcpy(d->d_name, name, elm->key_len);
		d->d_name[elm->key_len] = '\0';

		outp += reclen;
		remaining -= reclen;
		read += reclen;
	}

	*bytes_read = read;
	*offset = curr;

	return YAK_SUCCESS;
}

static status_t tmpfs_lookup(struct vnode *vn, char *name, struct vnode **out)
{
	if (vn->type != VDIR) {
		return YAK_NODIR;
	}

	struct tmpfs_node *elm = ht_get(&((struct tmpfs_node *)vn)->children,
					name, strlen(name));
	if (!elm)
		return YAK_NOENT;

	*out = &elm->vnode;
	return YAK_SUCCESS;
}

static status_t tmpfs_lock(struct vnode *vn)
{
	kmutex_acquire(&vn->lock, TIMEOUT_INFINITE);
	return YAK_SUCCESS;
}

static status_t tmpfs_unlock(struct vnode *vn)
{
	kmutex_release(&vn->lock);
	return YAK_SUCCESS;
}

static status_t tmpfs_open(struct vnode **vn)
{
	return YAK_SUCCESS;
}

static status_t tmpfs_mmap(struct vnode *vn, struct vm_map *map, size_t length,
			   voff_t offset, vm_prot_t prot,
			   vm_inheritance_t inheritance, vaddr_t hint,
			   int flags, vaddr_t *out)
{
	struct tmpfs_node *tvn = (struct tmpfs_node *)vn;
	assert(tvn->vnode.type == VREG);
	return vm_map(map, vn->vobj, length, offset, prot, inheritance,
		      VM_CACHE_DEFAULT, hint, flags, out);
}

static status_t tmpfs_fallocate(struct vnode *vn, int mode, off_t offset,
				off_t size)
{
	switch (mode) {
	case 0:
		if ((size_t)(offset + size) > vn->filesize)
			vn->filesize = offset + size;
		return YAK_SUCCESS;
	default:
		return YAK_NOT_SUPPORTED;
	}
}

static struct vn_ops tmpfs_vn_op = {
	.vn_lookup = tmpfs_lookup,
	.vn_create = tmpfs_create,
	.vn_lock = tmpfs_lock,
	.vn_unlock = tmpfs_unlock,
	.vn_inactive = tmpfs_inactive,
	.vn_getdents = tmpfs_getdents,
	.vn_symlink = tmpfs_symlink,
	.vn_readlink = tmpfs_readlink,
	.vn_read = NULL,
	.vn_write = NULL,
	.vn_open = tmpfs_open,
	.vn_mmap = tmpfs_mmap,
	.vn_fallocate = tmpfs_fallocate,
	.vn_setattr = tmpfs_setattr,
	.vn_getattr = tmpfs_getattr,
};

static status_t tmpfs_mount(struct vnode *vn);

static struct vfs_ops tmpfs_op = {
	.vfs_mount = tmpfs_mount,
	.vfs_getroot = tmpfs_getroot,
};

void tmpfs_init()
{
	EXPECT(vfs_register("tmpfs", &tmpfs_op));
}

INIT_ENTAILS(tmpfs);
INIT_DEPS(tmpfs, vfs_stage);
INIT_NODE(tmpfs, tmpfs_init);

static status_t tmpfs_mount(struct vnode *vn)
{
	struct tmpfs *fs = kzalloc(sizeof(struct tmpfs));
	fs->seq_ino = 1;

	vnode_ref(vn);

	fs->vfs.ops = &tmpfs_op;

	vn->mounted_vfs = &fs->vfs;

	fs->root = create_node(&fs->vfs, VDIR);

	fs->root->vnode.node_covered = vn;

	return YAK_SUCCESS;
}

static struct tmpfs_node *create_node(struct vfs *vfs, enum vtype type)
{
	struct tmpfs_node *node = kmalloc(sizeof(struct tmpfs_node));
	if (!node)
		return NULL;

	memset(node, 0, sizeof(struct tmpfs_node));

	vnode_init(&node->vnode, vfs, &tmpfs_vn_op, type);

	if (type == VDIR) {
		ht_init(&node->children, ht_hash_str, ht_eq_str);

		// create self-pointer
		EXPECT(ht_set(&node->children, ".", 1, node, 1));
	} else {
		node->vnode.filesize = 0;
		node->vnode.vobj = vm_aobj_create();
	}

	node->name = NULL;
	node->name_len = 0;

	node->vattr.inode = __atomic_fetch_add(&((struct tmpfs *)vfs)->seq_ino,
					       1, __ATOMIC_RELAXED);
	// XXX: get major/minor from vfs instance
	node->vattr.major = 0;
	node->vattr.minor = 0;
	node->vattr.rmajor = 0;
	node->vattr.rminor = 0;
	node->vattr.block_size = PAGE_SIZE;

	return node;
}
