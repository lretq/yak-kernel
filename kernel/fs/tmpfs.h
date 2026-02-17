#pragma once

#include <yak/fs/vfs.h>
#include <yak/hashtable.h>
#include <yak/macro.h>

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

#define TO_TMP(vn) CHECKED_CAST(vn, struct vnode *, struct tmpfs_node *)
#define TO_TMPFS(vfs) CHECKED_CAST(vfs, struct vfs *, struct tmpfs *)
#define TO_VN(tmpn) &(tmpn)->vnode

void tmpfs_deinit_node(struct vnode *vn);
status_t tmpfs_init_node(struct vfs *vfs, struct vnode *vn, enum vtype type);
