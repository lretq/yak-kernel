#include <stddef.h>
#include <yak/types.h>
#include <yak/syscall.h>
#include <yak/macro.h>
#include <yak/vmflags.h>
#include <yak/vm/map.h>
#include <yak/vm.h>
#include <yak/cpudata.h>
#include <yak/log.h>
#include <yak/fs/vfs.h>
#include <yak-abi/errno.h>
#include <yak-abi/vm-flags.h>

static vm_prot_t convert_prot_flags(unsigned long prot)
{
	// Always implies VM_USER
	vm_prot_t vm_prot = VM_USER;

	if (prot & PROT_READ)
		vm_prot |= VM_READ;
	if (prot & PROT_WRITE)
		vm_prot |= VM_WRITE;
	if (prot & PROT_EXEC)
		vm_prot |= VM_EXECUTE;
	return vm_prot;
}

DEFINE_SYSCALL(SYS_MUNMAP, munmap, void *addr, size_t length)
{
	struct kprocess *proc = curproc();

	if (!IS_ALIGNED_POW2((vaddr_t)addr, PAGE_SIZE)) {
		return SYS_ERR(EINVAL);
	}

	// this splits any existing mappings and should correctly
	// deref and free pages in use
	status_t rv = vm_unmap(proc->map, (vaddr_t)addr, length, 0);

	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_MPROTECT, mprotect, void *addr, size_t length,
	       unsigned long prot)
{
	struct kprocess *proc = curproc();
	vm_prot_t vm_prot = convert_prot_flags(prot);

	status_t rv = vm_protect(proc->map, (vaddr_t)addr, length, vm_prot, 0);

	RET_ERRNO_ON_ERR(rv);
	return SYS_OK(0);
}

DEFINE_SYSCALL(SYS_MMAP, mmap, void *hint, unsigned long len,
	       unsigned long prot, unsigned long flags, unsigned long fd,
	       unsigned long pgoff)
{
#if 0
	pr_debug(
		"mmap(hint=%p, len=%lu, prot=%lu, flags=%lu, fd=%lu, pgoff=%lu)\n",
		hint, len, prot, flags, fd, pgoff);

	if (flags & MAP_SHARED)
		printk(0, "MAP_SHARED ");
	if (flags & MAP_PRIVATE)
		printk(0, "MAP_PRIVATE ");
	if (flags & MAP_FIXED)
		printk(0, "MAP_FIXED ");
	if (flags & MAP_ANONYMOUS)
		printk(0, "MAP_ANONYMOUS ");
	printk(0, "\n");
#endif

	// file mappings not yet implemented
	// assert(flags & MAP_ANONYMOUS);

	struct kprocess *proc = curproc();

	vm_prot_t vm_prot = convert_prot_flags(prot);

	vm_inheritance_t inheritance = VM_INHERIT_SHARED;
	if (flags & MAP_PRIVATE) {
		// CoW mapping
		inheritance = VM_INHERIT_COPY;
	}

	int vmflags = 0;
	if (flags & MAP_FIXED) {
		vmflags |= VM_MAP_FIXED;
		vmflags |= VM_MAP_OVERWRITE;
	}

	status_t rv;
	vaddr_t out = 0;

	if (flags & MAP_ANONYMOUS) {
		rv = vm_map(proc->map, NULL, len, pgoff, vm_prot, inheritance,
			    VM_CACHE_DEFAULT, (vaddr_t)hint, vmflags, &out);
	} else {
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

		rv = VOP_MMAP(file->vnode, proc->map, len, pgoff, vm_prot,
			      inheritance, (vaddr_t)hint, vmflags, &out);
	}

	RET_ERRNO_ON_ERR(rv);

	return SYS_OK(out);
}
