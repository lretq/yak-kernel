#pragma once

#include <yak/status.h>
#include <yak/process.h>
#include <stddef.h>

struct load_info {
	uintptr_t real_entry;
	uintptr_t prog_entry;
	uintptr_t base;
	uintptr_t phdr;
	size_t phnum;
	size_t phent;
};

status_t elf_load_path(char *path, struct vm_map *map,
		       struct load_info *loadinfo, uintptr_t base);

status_t elf_load(struct vnode *vn, struct vm_map *map,
		  struct load_info *loadinfo, uintptr_t base);
