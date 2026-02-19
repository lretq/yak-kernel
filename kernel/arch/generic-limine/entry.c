#include "yak/init.h"
#include <string.h>
#include <uacpi/uacpi.h>
#include <stddef.h>
#include <limine.h>
#include <yak/types.h>
#include <yak/macro.h>
#include <yak/kernel-file.h>
#include <yak/vm/pmm.h>
#include <yak/vm/map.h>
#include <yak/log.h>
#include <yak/initrd.h>
#include <yak/heap.h>

#include "request.h"

LIMINE_REQ
static volatile LIMINE_BASE_REVISION(3);

[[gnu::used, gnu::section(".limine_requests_start")]] //
static volatile LIMINE_REQUESTS_START_MARKER;

[[gnu::used, gnu::section(".limine_requests_end")]] //
static volatile LIMINE_REQUESTS_END_MARKER;

LIMINE_REQ static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0,
	.response = NULL,
};

LIMINE_REQ static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 3,
	.response = NULL,
};

LIMINE_REQ static volatile struct limine_executable_address_request
	address_request = {
		.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
		.revision = 0,
		.response = NULL,
	};

LIMINE_REQ static volatile struct limine_paging_mode_request
	paging_mode_request = {
		.id = LIMINE_PAGING_MODE_REQUEST,
		.revision = 1,
		.response = NULL,
#ifdef x86_64
		.mode = LIMINE_PAGING_MODE_X86_64_5LVL,
		.min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
		.max_mode = LIMINE_PAGING_MODE_X86_64_5LVL,
#elif defined(riscv64)
		.mode = LIMINE_PAGING_MODE_RISCV_SV57,
		.min_mode = LIMINE_PAGING_MODE_RISCV_SV39,
		.max_mode = LIMINE_PAGING_MODE_RISCV_SV57,
#endif
	};

LIMINE_REQ static volatile struct limine_module_request module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0,
	.response = NULL,
};

LIMINE_REQ static volatile struct limine_rsdp_request rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0,
	.response = NULL
};

paddr_t plat_get_rsdp()
{
	return rsdp_request.response == NULL ? 0 :
					       rsdp_request.response->address;
}

size_t HHDM_BASE;
size_t PMAP_LEVELS;

void plat_mem_init()
{
	struct limine_memmap_response *res = memmap_request.response;

	// setup PFNDB, HHDM and kernel mappings
	HHDM_BASE = hhdm_request.response->offset;

	switch (paging_mode_request.response->mode) {
#if defined(x86_64)
	case LIMINE_PAGING_MODE_X86_64_5LVL:
		PMAP_LEVELS = 5;
		break;
	case LIMINE_PAGING_MODE_X86_64_4LVL:
		PMAP_LEVELS = 4;
		break;
#elif defined(riscv64)
	case LIMINE_PAGING_MODE_RISCV_SV57:
		PMAP_LEVELS = 5;
		break;
	case LIMINE_PAGING_MODE_RISCV_SV48:
		PMAP_LEVELS = 4;
		break;
	case LIMINE_PAGING_MODE_RISCV_SV39:
		PMAP_LEVELS = 3;
		break;
#endif
	}

	// normal zone
	pmm_zone_init(ZONE_HIGH, "ZONE_HIGH", 1, UINT32_MAX, UINT64_MAX);
	// 32bit zone
	pmm_zone_init(ZONE_LOW, "ZONE_LOW", 1, 1048576, UINT32_MAX);
	// used for startup trampoline
	pmm_zone_init(ZONE_1MB, "ZONE_1MB", 0, 0x0, 1048576);

	for (size_t i = 0; i < res->entry_count; i++) {
		struct limine_memmap_entry *ent = res->entries[i];
		if (ent->type != LIMINE_MEMMAP_USABLE)
			continue;

		pmm_add_region(ent->base, ent->base + ent->length);
	}

	// bootstrap kernel pmap + setup kmap
	vm_map_init(kmap());
	struct pmap *kpmap = &kmap()->pmap;

	for (size_t i = 0; i < res->entry_count; i++) {
		struct limine_memmap_entry *ent = res->entries[i];
		if (ent->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE &&
		    ent->type != LIMINE_MEMMAP_FRAMEBUFFER &&
		    ent->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES &&
		    ent->type != LIMINE_MEMMAP_USABLE)
			continue;

		vm_cache_t cache = VM_CACHE_DEFAULT;
		if (ent->type == LIMINE_MEMMAP_FRAMEBUFFER)
			cache = VM_WC;

		pmap_large_map_range(kpmap, ent->base, ent->length,
				     HHDM_BASE + ent->base, VM_RW, cache);
	}

	uintptr_t kernel_pbase = address_request.response->physical_base;
	uintptr_t kernel_vbase = address_request.response->virtual_base;

#define MAP_SECTION(SECTION, VMFLAGS)                                         \
	uintptr_t SECTION##_start =                                           \
		ALIGN_DOWN((uintptr_t)__kernel_##SECTION##_start, PAGE_SIZE); \
	uintptr_t SECTION##_end =                                             \
		ALIGN_UP((uintptr_t)__kernel_##SECTION##_end, PAGE_SIZE);     \
	pmap_large_map_range(kpmap,                                           \
			     SECTION##_start - kernel_vbase + kernel_pbase,   \
			     (SECTION##_end - SECTION##_start),               \
			     SECTION##_start, (VMFLAGS), VM_CACHE_DEFAULT);

	MAP_SECTION(limine, VM_READ);
	MAP_SECTION(text, VM_RX);
	MAP_SECTION(rodata, VM_READ);
	MAP_SECTION(data, VM_RW);

#undef MAP_SECTION

	vm_map_activate(kmap());
}

INIT_ENTAILS(pmm_node, bsp_ready);
INIT_DEPS(pmm_node);
INIT_NODE(pmm_node, plat_mem_init);

extern void c_expert_early_start();

static void load_modules()
{
	struct limine_module_response *res = module_request.response;
	for (size_t i = 0; i < res->module_count; i++) {
		struct limine_file *mod = res->modules[i];
		initrd_unpack_tar("/", mod->address, mod->size);
	}
}

INIT_ENTAILS(boot_modules);
INIT_DEPS(boot_modules, rootfs);
INIT_NODE(boot_modules, load_modules);

static void reclaim_memory()
{
	struct pmm_stat stat;
	pmm_get_stat(&stat);
	size_t total_before = stat.total_pages;

	struct limine_module_response *module_res = module_request.response;
	for (size_t i = 0; i < module_res->module_count; i++) {
		struct limine_file *mod = module_res->modules[i];
		paddr_t pa = v2p((vaddr_t)mod->address);
		pmm_add_region(pa, ALIGN_UP(pa + mod->size, PAGE_SIZE));
	}

	struct limine_memmap_response *res = memmap_request.response;
	size_t entry_count = res->entry_count;
	struct limine_memmap_entry *map_copy =
		kcalloc(res->entry_count, sizeof(struct limine_memmap_entry));

	for (size_t i = 0; i < entry_count; i++) {
		struct limine_memmap_entry *ent = res->entries[i];
		memcpy(&map_copy[i], ent, sizeof(struct limine_memmap_entry));
	}

	for (size_t i = 0; i < entry_count; i++) {
		struct limine_memmap_entry *ent = &map_copy[i];
		if (ent->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
			continue;

		pmm_add_region(ent->base, ent->base + ent->length);
	}

	kfree(map_copy, entry_count * sizeof(struct limine_memmap_entry));

	pmm_get_stat(&stat);
	pr_debug("reclaimed %ld MiB\n",
		 (stat.total_pages - total_before) * 4096 / 1024 / 1024);
}

void boot_finalize_fn()
{
	reclaim_memory();
}

INIT_STAGE(boot_finalized);
INIT_ENTAILS(boot_finalize, boot_finalized);
INIT_DEPS(boot_finalize, boot_modules);
INIT_NODE(boot_finalize, boot_finalize_fn);
