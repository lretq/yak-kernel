#define pr_fmt(fmt) "elf: " fmt

#include <string.h>
#include <assert.h>
#include <yak/status.h>
#include <yak/process.h>
#include <yak/elf64.h>
#include <yak/vm/map.h>
#include <yak/fs/vfs.h>
#include <yak/vmflags.h>
#include <yak/heap.h>
#include <yak/log.h>
#include <yak/elf.h>

#define INTERP_BASE 0x7ffff7dd7000

#define PHDR_FLAG_READ (1 << 2) /* Readable segment */
#define PHDR_FLAG_WRITE (1 << 1) /* Writable segment */
#define PHDR_FLAG_EXECUTE (1 << 0) /* Executable segment */

status_t elf_load_path(char *path, struct vm_map *map,
		       struct load_info *loadinfo, uintptr_t base);

status_t elf_load(struct vnode *vn, struct vm_map *map,
		  struct load_info *loadinfo, uintptr_t base)
{
	if (vn->type != VREG)
		return YAK_INVALID_ARGS;

	Elf64_Ehdr ehdr;
	size_t read = -1;
	TRY(VOP_READ(vn, 0, &ehdr, sizeof(Elf64_Ehdr), &read));

	bool pie = (ehdr.e_type == ET_DYN);

	Elf64_Phdr *phdrs = kmalloc(ehdr.e_phnum * ehdr.e_phentsize);
	if (!phdrs) {
		return YAK_OOM;
	}
	guard(autofree)(phdrs, ehdr.e_phnum * ehdr.e_phentsize);

	for (size_t idx = 0; idx < ehdr.e_phnum; idx++) {
		size_t phoff = ehdr.e_phoff + idx * ehdr.e_phentsize;
		TRY(VOP_READ(vn, phoff, &phdrs[idx], ehdr.e_phentsize, &read));
	}

	vaddr_t min_va = UINTPTR_MAX;
	vaddr_t max_va = 0;

	for (size_t i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr *phdr = &phdrs[i];
		if (phdr->p_type != PT_LOAD)
			continue;

		vaddr_t seg_start = ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE);
		vaddr_t seg_end =
			ALIGN_UP(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);

		min_va = MIN(seg_start, min_va);
		max_va = MAX(seg_end, max_va);
	}

	if (base == 0 && pie)
		base = PAGE_SIZE;

	size_t reserved_size = max_va - min_va;

	TRY(vm_map_reserve(map, base, reserved_size,
			   pie ? VM_MAP_FIXED | VM_MAP_OVERWRITE : 0, &base));

	pr_extra_debug("reserve: %lx\n", base);

	// correct the load bias
	size_t load_bias = pie ? base - min_va : 0;

	pr_extra_debug("ehdr.e_phnum: %u\n", ehdr.e_phnum);
	for (size_t i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr *phdr = &phdrs[i];
		if (phdr->p_type != PT_LOAD)
			continue;

		if (!phdr->p_memsz)
			continue;

		assert(phdr->p_memsz >= phdr->p_filesz);

		size_t misalign = phdr->p_vaddr & (PAGE_SIZE - 1);

		// If the following condition is violated, we cannot use mmap() the segment;
		// however, GCC only generates ELF files that satisfy this.
		assert(misalign == (phdr->p_offset & (PAGE_SIZE - 1)));

		// Align addresses
		vaddr_t map_address = load_bias + phdr->p_vaddr - misalign;
		size_t backed_map_size =
			(phdr->p_filesz + misalign + PAGE_SIZE - 1) &
			~(PAGE_SIZE - 1);
		size_t total_map_size =
			(phdr->p_memsz + misalign + PAGE_SIZE - 1) &
			~(PAGE_SIZE - 1);

		vm_prot_t initial_prot = VM_RW;

		vm_prot_t prot = VM_USER;
		if (phdr->p_flags & PHDR_FLAG_READ)
			prot |= VM_READ;
		if (phdr->p_flags & PHDR_FLAG_WRITE)
			prot |= VM_WRITE;
		if (phdr->p_flags & PHDR_FLAG_EXECUTE)
			prot |= VM_EXECUTE;

		// we can avoid the vm_protect call if we don't have to write to the segment
		if (phdr->p_memsz == phdr->p_filesz)
			initial_prot = prot;

		vaddr_t out;

		TRY(VOP_MMAP(vn, map, backed_map_size,
			     phdr->p_offset - misalign, initial_prot,
			     VM_INHERIT_COPY, map_address,
			     VM_MAP_FIXED | VM_MAP_OVERWRITE, &out));

		if (total_map_size > backed_map_size) {
			TRY(vm_map(map, NULL, total_map_size - backed_map_size,
				   0, initial_prot, VM_INHERIT_COPY,
				   VM_CACHE_DEFAULT,
				   map_address + backed_map_size,
				   VM_MAP_FIXED | VM_MAP_OVERWRITE, &out));
		}

		// Clear the trailing area at the end of the backed mapping.
		// We do not clear the leading area; programs are not supposed to access it.
		memset((void *)(map_address + misalign + phdr->p_filesz), 0,
		       phdr->p_memsz - phdr->p_filesz);

		if (initial_prot != prot)
			TRY(vm_protect(map, map_address, total_map_size, prot,
				       VM_MAP_SETMAXPROT));
	}

	loadinfo->prog_entry = ehdr.e_entry + load_bias;
	loadinfo->real_entry = loadinfo->prog_entry;
	loadinfo->base = base;
	loadinfo->phnum = ehdr.e_phnum;
	loadinfo->phent = ehdr.e_phentsize;

	pr_extra_debug("load_bias : %lx\n", load_bias);

	for (size_t idx = 0; idx < ehdr.e_phnum; idx++) {
		Elf64_Phdr *phdr = &phdrs[idx];
		switch (phdr->p_type) {
		case PT_INTERP: {
			size_t interp_len = phdr->p_filesz;
			char *interp = kmalloc(interp_len);
			guard(autofree)(interp, interp_len);

			TRY(VOP_READ(vn, phdr->p_offset, interp, interp_len,
				     &read));

			pr_extra_debug("PT_INTERP: %s\n", interp);

			struct load_info interpinfo;
			TRY(elf_load_path(interp, map, &interpinfo,
					  INTERP_BASE));

			loadinfo->base = interpinfo.base;
			loadinfo->real_entry = interpinfo.prog_entry;

			break;
		}
		case PT_PHDR:
			loadinfo->phdr = phdr->p_vaddr + load_bias;
			break;
		/* no-op, rtld needs this */
		case PT_DYNAMIC:
			/* no-op */
		case PT_LOAD:
		case PT_NOTE:
		case PT_NULL:
			break;
		default:
			/* some gnu bs */
			pr_extra_debug("unhandled phdr type: %u\n",
				       phdr->p_type);
		}
	}

	return YAK_SUCCESS;
}

status_t elf_load_path(char *path, struct vm_map *map, struct load_info *info,
		       uintptr_t base)
{
	struct vnode *vn;
	TRY(vfs_open(path, NULL, 0, &vn));
	return elf_load(vn, map, info, base);
}
