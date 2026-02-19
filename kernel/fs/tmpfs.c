#define pr_fmt(fmt) "tmpfs: " fmt

#include <string.h>
#include <stddef.h>
#include <yak/heap.h>
#include <yak/queue.h>
#include <yak/vm/object.h>
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
#include <yak-abi/poll.h>

#include "tmpfs.h"

static void free_node(struct vnode *node);
static status_t alloc_node(struct vfs *vfs, enum vtype type,
			   struct tmpfs_node **out);

static struct vnode *tmpfs_getroot(struct vfs *vfs)
{
	struct tmpfs *fs = (struct tmpfs *)vfs;
	return &fs->root->vnode;
}

static status_t tmpfs_inactive(struct vnode *vn)
{
	// TODO:free node when nlinks=0
	return YAK_SUCCESS;
}

static status_t tmpfs_setattr(struct vnode *vn, unsigned int what,
			      struct vattr *attr)
{
	struct tmpfs_node *tvn = TO_TMP(vn);
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
	struct tmpfs_node *tvn = TO_TMP(vn);
	memcpy(attr, &tvn->vattr, sizeof(struct vattr));
	attr->block_count = DIV_ROUNDUP(vn->filesize, 512);
	return YAK_SUCCESS;
}

static status_t tmpfs_create(struct vnode *parent, enum vtype type, char *name,
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

	struct tmpfs_node *node;
	TRY(alloc_node(parent->vfs, type, &node));

	node->name_len = name_len;
	node->name = strndup(name, node->name_len);

	// All directories have '.' as well
	node->vattr.nlinks = (type == VDIR) ? 2 : 1;

	tmpfs_setattr(&node->vnode, SETATTR_ALL, initial_attr);

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

static status_t tmpfs_symlink(struct vnode *parent, char *name, char *path,
			      struct vattr *attr, struct vnode **out)
{
	char *path_copy = strdup(path);

	struct vnode *linkvn = NULL;
	status_t rv = tmpfs_create(parent, VLNK, name, attr, &linkvn);
	if (IS_ERR(rv)) {
		kfree(path_copy, 0);
		return rv;
	}

	struct tmpfs_node *tmpfs_node = TO_TMP(linkvn);
	tmpfs_node->link_path = path_copy;

	*out = linkvn;
	return YAK_SUCCESS;
}

static status_t tmpfs_link(struct vnode *node, struct vnode *dir,
			   const char *name)
{
	if (node->vfs != dir->vfs)
		return YAK_CROSS_DEVICE;

	if (dir->type != VDIR)
		return YAK_NODIR;

	if (node->type == VDIR)
		return YAK_ISDIR;

	struct tmpfs_node *tnode = TO_TMP(node);
	struct tmpfs_node *tdir = TO_TMP(dir);

	size_t namelen = strlen(name);

	if (ht_get(&tdir->children, name, namelen))
		return YAK_EXISTS;

	TRY(ht_set(&tdir->children, name, namelen, node, true));

	vnode_ref(node);
	tnode->vattr.nlinks++;

	return YAK_SUCCESS;
}

static status_t tmpfs_readlink(struct vnode *vn, char **path)
{
	if (vn->type != VLNK || path == NULL)
		return YAK_INVALID_ARGS;

	struct tmpfs_node *tmpfs_node = TO_TMP(vn);

	char *link_copy = kmalloc(strlen(tmpfs_node->link_path) + 1);
	if (!link_copy) {
		return YAK_OOM;
	}

	strcpy(link_copy, tmpfs_node->link_path);
	*path = link_copy;

	return YAK_SUCCESS;
}

static status_t tmpfs_getdirents(struct vnode *vn, struct mlibc_dirent *buf,
				 size_t bufsize, size_t *offset,
				 size_t *bytes_read)
{
	if (vn->type != VDIR) {
		return YAK_NODIR;
	}

	struct tmpfs_node *tvn = TO_TMP(vn);

	size_t curr_index = 0;
	size_t total_read = 0;

	struct ht_entry *elm;
	HASHTABLE_FOR_EACH(&tvn->children, elm)
	{
		if (*offset > curr_index) {
			curr_index++;
			continue;
		}

		struct tmpfs_node *child = elm->value;
		const char *name = elm->key;
		const size_t namelen = elm->key_len;
		size_t reclen =
			offsetof(struct mlibc_dirent, d_name) + namelen + 1;
		reclen = ALIGN_UP(reclen, sizeof(long));

		if (total_read + reclen > bufsize) {
			// Doesn't fit anymore!
			break;
		}

		struct mlibc_dirent *d =
			(struct mlibc_dirent *)((char *)buf + total_read);
		d->d_ino = child->vattr.inode;
		d->d_off = 0; // ?
		d->d_reclen = (unsigned short)reclen;
		d->d_type = vtype_to_dtype(child->vnode.type);
		memcpy(d->d_name, name, namelen);
		d->d_name[namelen] = '\0';

		total_read += reclen;
		curr_index++;
	}

	*bytes_read = total_read;
	*offset = curr_index;

	return YAK_SUCCESS;
}

static status_t tmpfs_lookup(struct vnode *vn, char *name, struct vnode **out)
{
	if (vn->type != VDIR) {
		return YAK_NODIR;
	}

	struct tmpfs_node *tvn = TO_TMP(vn);
	struct tmpfs_node *entry = ht_get(&tvn->children, name, strlen(name));
	if (!entry)
		return YAK_NOENT;

	*out = &entry->vnode;
	return YAK_SUCCESS;
}

static status_t tmpfs_mmap(struct vnode *vn, struct vm_map *map, size_t length,
			   voff_t offset, vm_prot_t prot,
			   vm_inheritance_t inheritance, vaddr_t hint,
			   int flags, vaddr_t *out)
{
	if (vn->type != VREG)
		return YAK_NOT_SUPPORTED;
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
		pr_warn("unsupported fallocate mode: %d\n", mode);
		return YAK_NOT_SUPPORTED;
	}
}

static status_t tmpfs_poll(struct vnode *vn, short mask, short *ret)
{
	(void)vn;
	if (mask & POLLOUT)
		*ret |= POLLOUT;
	if (mask & POLLIN)
		*ret |= POLLIN;
	return YAK_SUCCESS;
}

struct vn_ops tmpfs_vn_op = {
	.vn_lookup = tmpfs_lookup,
	.vn_create = tmpfs_create,
	.vn_symlink = tmpfs_symlink,
	.vn_link = tmpfs_link,
	.vn_inactive = tmpfs_inactive,
	.vn_getdirents = tmpfs_getdirents,
	.vn_readlink = tmpfs_readlink,
	.vn_mmap = tmpfs_mmap,
	.vn_fallocate = tmpfs_fallocate,
	.vn_getattr = tmpfs_getattr,
	.vn_setattr = tmpfs_setattr,
	.vn_poll = tmpfs_poll,

	.vn_read = vfs_vobj_read,
	.vn_write = vfs_vobj_write,
};

static status_t tmpfs_mount(struct vnode *vn);

static struct vfs_ops tmpfs_op = {
	.vfs_mount = tmpfs_mount,
	.vfs_getroot = tmpfs_getroot,
};

void tmpfs_init()
{
	vfs_inherit_vn_ops(&tmpfs_vn_op, &vfs_generic_ops);
	EXPECT(vfs_register("tmpfs", &tmpfs_op));
}

INIT_ENTAILS(tmpfs);
INIT_DEPS(tmpfs, vfs_stage);
INIT_NODE(tmpfs, tmpfs_init);

static status_t tmpfs_mount(struct vnode *vn)
{
	struct tmpfs *fs = kzalloc(sizeof(*fs));
	fs->seq_ino = 1;
	fs->vfs.ops = &tmpfs_op;

	// Try to create the root node
	TRY(alloc_node(&fs->vfs, VDIR, &fs->root));

	vnode_ref(vn);
	vn->mounted_vfs = &fs->vfs;
	fs->root->vnode.node_covered = vn;

	return YAK_SUCCESS;
}

status_t tmpfs_init_node(struct vfs *vfs, struct vnode *vn, enum vtype type)
{
	struct tmpfs *tmpfs = TO_TMPFS(vfs);
	struct tmpfs_node *node = TO_TMP(vn);

	memset(node, 0, sizeof(*node));

	vnode_init(vn, vfs, &tmpfs_vn_op, type);

	if (type == VDIR) {
		ht_init(&node->children, ht_hash_str, ht_eq_str);
		// create link to self
		TRY(ht_set(&node->children, ".", 1, vn, true));
	} else if (type == VREG) {
		vn->filesize = 0;
		vn->vobj = vm_aobj_create();
	}

	node->name = NULL;
	node->name_len = 0;

	node->vattr.inode =
		__atomic_fetch_add(&tmpfs->seq_ino, 1, __ATOMIC_RELAXED);

	// XXX: get major/minor from vfs instance
	node->vattr.major = 0;
	node->vattr.minor = 0;
	node->vattr.rmajor = 0;
	node->vattr.rminor = 0;
	node->vattr.block_size = PAGE_SIZE;

	return YAK_SUCCESS;
}

void tmpfs_deinit_node(struct vnode *vn)
{
	struct tmpfs_node *node = TO_TMP(vn);
	enum vtype vt = vn->type;
	if (vt == VDIR) {
		ht_destroy(&node->children);
	} else if (vt == VREG) {
		vm_object_deref(vn->vobj);
		vn->vobj = NULL;
	}
}

static void free_node(struct vnode *vn)
{
	tmpfs_deinit_node(vn);
	kfree(vn, sizeof(struct tmpfs_node));
}

static status_t alloc_node(struct vfs *vfs, enum vtype type,
			   struct tmpfs_node **out)
{
	struct tmpfs_node *node = kmalloc(sizeof(*node));
	if (!node) {
		*out = NULL;
		return YAK_OOM;
	}

	status_t rv = tmpfs_init_node(vfs, (void *)node, type);
	if (IS_ERR(rv)) {
		kfree(node, sizeof(*node));
		*out = NULL;
		return rv;
	}

	*out = node;
	return YAK_SUCCESS;
}
