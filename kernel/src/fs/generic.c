#define pr_fmt(fmt) "vfs_generic: " fmt

#include <string.h>
#include <yak/status.h>
#include <yak/log.h>
#include <yak/fs/vfs.h>
#include <yak/arch-mm.h>
#include <yak/vm/object.h>

status_t vfs_vobj_write(struct vnode *vn, voff_t offset, const void *buf,
			size_t length, size_t *writtenp)
{
	if (writtenp == NULL)
		return YAK_INVALID_ARGS;

	if (vn->type != VREG)
		return YAK_NOT_SUPPORTED;

	struct vm_object *vobj = vn->vobj;
	assert(vobj);

	if (length == 0)
		return YAK_SUCCESS;

	size_t written = 0;

	size_t start_off = offset;
	size_t end_off = offset + length;

	VOP_LOCK(vn);
	if (end_off > vn->filesize) {
		vn->filesize = end_off;
	}
	VOP_UNLOCK(vn);

	const char *src = buf;

	while (start_off < end_off) {
		voff_t pageoff = ALIGN_DOWN(start_off, PAGE_SIZE);
		size_t page_offset = start_off - pageoff;
		size_t chunk =
			MIN(PAGE_SIZE - page_offset, end_off - start_off);

		struct page *pg;
		// Lookup or allocate the page
		// XXX: should this wire pages?
		status_t res = vm_lookuppage(vobj, pageoff, 0, &pg);
		IF_ERR(res)
		{
			return res;
		}

		memcpy((char *)page_to_mapped_addr(pg) + page_offset, src,
		       chunk);

		// TODO: mark page as dirt?
		// later, pager shall write it back

		src += chunk;
		start_off += chunk;
		written += chunk;
	}

	*writtenp = written;
	return YAK_SUCCESS;
}

status_t vfs_vobj_read(struct vnode *vn, voff_t offset, void *buf,
		       size_t length, size_t *readp)
{
	if (readp == NULL)
		return YAK_INVALID_ARGS;

	if (vn->type != VREG)
		return YAK_NOT_SUPPORTED;

	struct vm_object *obj = vn->vobj;
	assert(obj);

	if (length == 0)
		return YAK_SUCCESS;

	if (offset >= vn->filesize) {
		*readp = 0;
		return YAK_EOF;
	}

	if (offset + length > vn->filesize)
		length = vn->filesize - offset;

	size_t read = 0;
	char *dst = buf;

	voff_t start_off = offset;
	voff_t end_off = offset + length;

	while (start_off < end_off) {
		voff_t pageoff = ALIGN_DOWN(start_off, PAGE_SIZE);
		size_t page_offset = start_off - pageoff;
		size_t chunk =
			MIN(PAGE_SIZE - page_offset, end_off - start_off);

		struct page *pg;
		// Lookup or retrieve the page
		status_t res = vm_lookuppage(obj, pageoff, 0, &pg);
		if (IS_ERR(res)) {
			return res;
		}

		memcpy(dst, (char *)page_to_mapped_addr(pg) + page_offset,
		       chunk);

		dst += chunk;
		start_off += chunk;
		read += chunk;
	}

	*readp = read;
	return YAK_SUCCESS;
}

status_t vfs_generic_ioctl(struct vnode *vn, unsigned long com, void *data,
			   int *ret)
{
	(void)vn;
	(void)com;
	(void)data;
	(void)ret;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_mmap(struct vnode *vn, struct vm_map *map, size_t length,
			  voff_t offset, vm_prot_t prot,
			  vm_inheritance_t inheritance, vaddr_t hint, int flags,
			  vaddr_t *out)
{
	(void)vn;
	(void)map;
	(void)length;
	(void)offset;
	(void)prot;
	(void)inheritance;
	(void)hint;
	(void)flags;
	(void)out;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_lock(struct vnode *vn)
{
	kmutex_acquire(&vn->lock, TIMEOUT_INFINITE);
	return YAK_SUCCESS;
}

status_t vfs_generic_unlock(struct vnode *vn)
{
	kmutex_release(&vn->lock);
	return YAK_SUCCESS;
}

status_t vfs_generic_open(struct vnode **vn)
{
	(void)vn;
	return YAK_SUCCESS;
}

status_t vfs_generic_lookup(struct vnode *vp, char *name, struct vnode **out)
{
	(void)vp;
	(void)name;
	*out = NULL;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_create(struct vnode *vp, enum vtype type, char *name,
			    struct vattr *attr, struct vnode **out)
{
	(void)vp;
	(void)type;
	(void)name;
	(void)attr;
	*out = NULL;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_symlink(struct vnode *parent, char *name, char *path,
			     struct vattr *attr, struct vnode **out)
{
	(void)parent;
	(void)name;
	(void)path;
	(void)attr;
	*out = NULL;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_inactive(struct vnode *vn)
{
	pr_warn("inactive vnode! (%p)\n", vn);
	return YAK_SUCCESS;
}

status_t vfs_generic_getdirents(struct vnode *vp, struct mlibc_dirent *buf,
				size_t bufsize, size_t *offset,
				size_t *bytes_read)
{
	(void)vp;
	(void)buf;
	(void)bufsize;
	(void)offset;
	*bytes_read = 0;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_readlink(struct vnode *vn, char **path)
{
	(void)vn;
	*path = NULL;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_read(struct vnode *vn, voff_t offset, void *buf,
			  size_t length, size_t *read_bytes)
{
	(void)vn;
	(void)offset;
	(void)buf;
	(void)length;
	*read_bytes = 0;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_write(struct vnode *vn, voff_t offset, const void *buf,
			   size_t length, size_t *written_bytes)
{
	(void)vn;
	(void)offset;
	(void)buf;
	(void)length;
	*written_bytes = 0;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_fallocate(struct vnode *vp, int mode, off_t offset,
			       off_t size)
{
	(void)vp;
	(void)mode;
	(void)offset;
	(void)size;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_getattr(struct vnode *vp, struct vattr *vattr)
{
	(void)vp;
	(void)vattr;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_setattr(struct vnode *vp, unsigned int what,
			     struct vattr *vattr)
{
	(void)vp;
	(void)what;
	(void)vattr;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_poll(struct vnode *vp, short events, short *revents)
{
	(void)vp;
	(void)events;
	(void)revents;
	return YAK_NOT_SUPPORTED;
}

status_t vfs_generic_link(struct vnode *vp, struct vnode *dir, const char *name)
{
	(void)vp;
	(void)dir;
	(void)name;
	return YAK_NOT_SUPPORTED;
}

const struct vn_ops vfs_generic_ops = {
	.vn_lookup = vfs_generic_lookup,
	.vn_create = vfs_generic_create,
	.vn_symlink = vfs_generic_symlink,
	.vn_link = vfs_generic_link,
	.vn_lock = vfs_generic_lock,
	.vn_unlock = vfs_generic_unlock,
	.vn_inactive = vfs_generic_inactive,
	.vn_getdirents = vfs_generic_getdirents,
	.vn_readlink = vfs_generic_readlink,
	.vn_read = vfs_generic_read,
	.vn_write = vfs_generic_write,
	.vn_open = vfs_generic_open,
	.vn_ioctl = vfs_generic_ioctl,
	.vn_mmap = vfs_generic_mmap,
	.vn_fallocate = vfs_generic_fallocate,
	.vn_getattr = vfs_generic_getattr,
	.vn_setattr = vfs_generic_setattr,
	.vn_poll = vfs_generic_poll,
};
