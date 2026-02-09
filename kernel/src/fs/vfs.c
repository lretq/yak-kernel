#define pr_fmt(fmt) "vfs: " fmt

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <yak/process.h>
#include <yak/cpudata.h>
#include <yak/macro.h>
#include <yak/types.h>
#include <yak/init.h>
#include <yak/hashtable.h>
#include <yak/heap.h>
#include <yak/log.h>
#include <yak/queue.h>
#include <yak/status.h>
#include <yak/fs/vfs.h>
#include <yak/vm.h>
#include <yak/vm/map.h>
#include <yak/vm/object.h>

#define MAX_FS_NAME 16

static struct hashtable filesystems;

static struct vnode *root_node;

status_t root_lock([[maybe_unused]] struct vnode *vn)
{
	return YAK_SUCCESS;
}

status_t root_unlock([[maybe_unused]] struct vnode *vn)
{
	return YAK_SUCCESS;
}

status_t root_inactive([[maybe_unused]] struct vnode *vn)
{
	pr_warn("root_node is inactive??\n");
	return YAK_SUCCESS;
}

static struct vn_ops root_ops = {
	.vn_lock = root_lock,
	.vn_unlock = root_unlock,
	.vn_inactive = root_inactive,
};

void vfs_init()
{
	ht_init(&filesystems, ht_hash_str, ht_eq_str);

	root_node = kmalloc(sizeof(struct vnode));
	vnode_init(root_node, NULL, &root_ops, VDIR);
}

INIT_STAGE(vfs);
INIT_ENTAILS(vfs, vfs);
INIT_DEPS(vfs);
INIT_NODE(vfs, vfs_init);

status_t vfs_register(const char *name, struct vfs_ops *ops)
{
	size_t namelen = strlen(name);
	assert(namelen < MAX_FS_NAME);
	return ht_set(&filesystems, name, namelen, ops, 0);
}

static struct vfs_ops *lookup_fs(const char *name)
{
	return ht_get(&filesystems, name, strlen(name));
}

status_t vfs_mount(const char *path, char *fsname)
{
	struct vfs_ops *ops = lookup_fs(fsname);
	if (!ops)
		return YAK_UNKNOWN_FS;

	struct vnode *vn;
	status_t res = vfs_lookup_path(path, NULL, 0, &vn, NULL);
	IF_ERR(res)
	{
		goto exit;
	}

	res = ops->vfs_mount(vn);
	IF_ERR(res)
	{
		goto exit;
	}

	pr_info("mounted %s on %s\n", fsname, path);

exit:
	if (vn) {
		VOP_UNLOCK(vn);
		vnode_deref(vn);
	}

	return res;
}

status_t vfs_create(char *path, enum vtype type, struct vattr *attr,
		    struct vnode **out)
{
	struct vnode *parent, *vn;
	char *last_comp = NULL;
	status_t res = vfs_lookup_path(path, NULL, VFS_LOOKUP_PARENT, &parent,
				       &last_comp);

	IF_ERR(res)
	{
		assert(last_comp == NULL);
		return res;
	}

	size_t comp_size = strlen(last_comp) + 1;

	assert(parent);
	res = VOP_CREATE(parent, type, last_comp, attr, &vn);

	VOP_UNLOCK(parent);
	vnode_deref(parent);

	if (out && IS_OK(res)) {
		*out = vn;
	}

	kfree(last_comp, comp_size);

	return res;
}

status_t vfs_link(struct vnode *old_vn, char *new_path, struct vnode *new_cwd)
{
	char *last_comp;
	struct vnode *parent;
	TRY(vfs_lookup_path(new_path, new_cwd, VFS_LOOKUP_PARENT, &parent,
			    &last_comp));

	size_t comp_size = strlen(last_comp) + 1;
	guard(autofree)(last_comp, comp_size);

	status_t rv = VOP_LINK(old_vn, parent, last_comp);

	VOP_UNLOCK(parent);
	vnode_deref(parent);

	return rv;
}

status_t vfs_symlink(char *link_path, char *dest_path, struct vattr *attr,
		     struct vnode *cwd, struct vnode **out)
{
	char *last_comp;
	struct vnode *parent, *vn;
	TRY(vfs_lookup_path(link_path, cwd, VFS_LOOKUP_PARENT, &parent,
			    &last_comp));

	size_t comp_size = strlen(last_comp) + 1;

	guard(autofree)(last_comp, comp_size);

	assert(parent);
	status_t rv = VOP_SYMLINK(parent, last_comp, dest_path, attr, &vn);

	VOP_UNLOCK(parent);
	vnode_deref(parent);

	if (IS_OK(rv)) {
		if (out)
			*out = vn;
		else
			vnode_deref(vn);
	}

	return rv;
}

static struct vnode *resolve(struct vnode *vn)
{
	assert(vn != NULL);
	if (vn->mounted_vfs) {
		return resolve(VFS_GETROOT(vn->mounted_vfs));
	}

	// TODO: handle links

	return vn;
}

static struct vnode *resolve_upward(struct vnode *vn)
{
	while (vn->node_covered != NULL)
		vn = vn->node_covered;

	return vn;
}

static size_t split_path(char *path)
{
	size_t count = 0;
	int in_comp = 0;

	char *dst = path;
	char *src = path;
	while (*src) {
		if (*src == '/') {
			while (*src == '/')
				src++;

			if (in_comp) {
				*dst++ = '\0';
				in_comp = 0;
			}
		} else {
			if (!in_comp) {
				count++;
				in_comp = 1;
			}

			*dst++ = *src++;
		}
	}

	*dst = '\0';
	return count;
}

status_t vfs_open(char *path, struct vnode *cwd, int lookup_flags,
		  struct vnode **out)
{
	struct vnode *vn;
	TRY(vfs_lookup_path(path, cwd, lookup_flags, &vn, NULL));

	status_t res = VOP_OPEN(&vn);
	if (IS_ERR(res)) {
		VOP_UNLOCK(vn);
		vnode_deref(vn);
		return res;
	}

	VOP_UNLOCK(vn);

	// keep refcnt
	*out = vn;

	return YAK_SUCCESS;
}

struct vnode *vfs_getroot()
{
	struct vnode *vn = resolve(root_node);
	assert(vn);
	vnode_ref(vn);
	return vn;
}

// cwd is expected to be referenced if non-null
// vnodes are returned referenced+locked
// => caller must call VOP_UNLOCK and vnode_deref
status_t vfs_lookup_path(const char *path_, struct vnode *cwd, int flags,
			 struct vnode **out, char **last_comp)
{
	if (!path_ || *path_ == '\0')
		return YAK_INVALID_ARGS;

	size_t pathlen = strlen(path_);

	int want_dir = (path_[pathlen - 1] == '/');

	// don't resolve cwd, because we don't want
	// to access a newly mounted filesystem
	// XXX: wtf do you mean access a newly mounted filesystem?
	struct vnode *current = cwd;
	if (path_[0] == '/') {
		current = vfs_getroot();

		if (cwd != NULL) {
			vnode_deref(cwd);
		}
	} else if (cwd == NULL) {
		current = process_getcwd(curproc());
	} /* else cwd was passed in referenced */

	assert(current);

	struct vnode *next = NULL;

	// we can't stack allocate, symlinks can recurse
	char *path = kmalloc(pathlen + 1);
	guard(autofree)(path, pathlen + 1);
	memcpy(path, path_, pathlen + 1);

	// turns '/' to '\0' in-place
	size_t n_comps = split_path(path);

	*out = NULL;
	if (last_comp)
		*last_comp = NULL;

	// current is returned with a reference
	VOP_LOCK(current);

	if (pathlen == 1) {
		if (path_[0] == '/') {
			if (last_comp)
				*last_comp = strndup("/", 2);
			*out = current;
			assert(current->type == VDIR);
			return YAK_SUCCESS;
		} else if (path_[0] == '.') {
			// WTF
			if (current->type != VDIR) {
				VOP_UNLOCK(current);
				vnode_deref(current);
				*out = NULL;
				return YAK_NODIR;
			}

			if (last_comp)
				*last_comp = strndup(".", 2);
			*out = current;
			return YAK_SUCCESS;
		}
	}

	char *comp = path;

	for (size_t i = 0; i < n_comps; i++) {
		if (current->type != VDIR) {
			VOP_UNLOCK(current);
			vnode_deref(current);
			return YAK_NODIR;
		}

		bool is_last = (i + 1 == n_comps);

		if (is_last && (flags & VFS_LOOKUP_PARENT)) {
			*out = current;
			if (last_comp)
				*last_comp = strndup(comp, strlen(comp) + 1);
			return YAK_SUCCESS;
		}

		if (strcmp(comp, "..") == 0) {
			struct vnode *root = resolve(root_node);
			assert(root);
			// if at VFS root, skip '..'
			if (root == current) {
				next = current;
				goto advance;
			}

			if (current->node_covered) {
				struct vnode *vn = resolve_upward(current);
				if (vn != current) {
					pr_warn("found mountpoint?\n");
					assert(!"todo");
				}
			}
		}

		status_t res = VOP_LOOKUP(current, comp, &next);
		if (IS_ERR(res)) {
			VOP_UNLOCK(current);
			vnode_deref(current);
			return res;
		}

		// might happen when comp = dot
		if (current != next) {
			vnode_ref(next);
			VOP_LOCK(next);

			VOP_UNLOCK(current);
		}

		// follow mountpoints
		struct vnode *resolved;
		resolved = resolve(next);
		if (next != resolved) {
			vnode_ref(resolved);
			VOP_UNLOCK(next);
			vnode_deref(next);

			next = resolved;
			VOP_LOCK(next);
		}

		// resolve symlinks
		if (next->type == VLNK) {
			if (is_last && (flags & VFS_LOOKUP_NOFOLLOW)) {
				// intermediary symlinks may be followed
				goto skip_follow;
			}

			//pr_debug("lookup link: %s\n", comp);

			char *dest;
			status_t symresolve_retval = VOP_READLINK(next, &dest);
			VOP_UNLOCK(next);
			if (IS_ERR(symresolve_retval)) {
				vnode_deref(next);
				return symresolve_retval;
			}

			struct vnode *destvn, *resolve_cwd;

			if (*dest == '/') {
				resolve_cwd = resolve(root_node);
			} else {
				resolve_cwd = current;
			}

			vnode_ref(resolve_cwd);

			symresolve_retval = vfs_lookup_path(dest, resolve_cwd,
							    0, &destvn, NULL);

			kfree(dest, 0);

			vnode_deref(next);

			if (IS_ERR(symresolve_retval)) {
				return symresolve_retval;
			}

			next = destvn;
		}

skip_follow:

		if (current != next) {
			vnode_deref(current);
		}

advance:
		current = next;

		if (is_last) {
			if (want_dir && next->type != VDIR) {
				VOP_UNLOCK(next);
				vnode_deref(next);
				return YAK_NODIR;
			}

			*out = next;

			if (last_comp)
				*last_comp = strndup(comp, strlen(comp) + 1);

			return YAK_SUCCESS;
		}

		comp += strlen(comp) + 1;
	}

	VOP_UNLOCK(current);
	vnode_deref(current);
	return YAK_NOENT;
}

void vnode_init(struct vnode *vn, struct vfs *vfs, struct vn_ops *ops,
		enum vtype type)
{
	vn->ops = ops;
	vn->type = type;
	vn->refcnt = 1;
	vn->vfs = vfs;
	vn->mounted_vfs = NULL;
	vn->node_covered = NULL;
	vn->filesize = 0;
	vn->vobj = NULL;
	vn->flags = 0;
	kmutex_init(&vn->lock, "vnode");
	event_init(&vn->poll_event, false, 0);
}

#define INHERIT(field)                                   \
	do {                                             \
		if ((dest)->field == NULL)               \
			(dest)->field = (source)->field; \
	} while (0)

void vfs_inherit_vn_ops(struct vn_ops *dest, const struct vn_ops *source)
{
#define X(op) INHERIT(op);
	VN_OP_XLIST(X)
#undef X
}

#undef INHERIT
