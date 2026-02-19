#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <yak/arch-mm.h>
#include <yak/types.h>
#include <yak/cpu.h>

struct pmap {
	struct cpumask mapped_on;
	paddr_t top_level;
};

void pmap_kernel_bootstrap(struct pmap *pmap);

void pmap_init(struct pmap *pmap);

void pmap_destroy(struct pmap *pmap);

void pmap_map(struct pmap *pmap, uintptr_t va, uintptr_t pa, size_t level,
	      vm_prot_t prot, vm_cache_t cache);

paddr_t pmap_unmap(struct pmap *pmap, uintptr_t va, size_t level);

void pmap_unmap_range(struct pmap *pmap, uintptr_t va, size_t length,
		      size_t level);

void pmap_unmap_range_and_free(struct pmap *pmap, uintptr_t va, size_t length,
			       size_t level);

void pmap_activate(struct pmap *pmap);

void pmap_large_map_range(struct pmap *pmap, uintptr_t base, size_t length,
			  uintptr_t virtual_base, vm_prot_t prot,
			  vm_cache_t cache);

void pmap_protect_range(struct pmap *pmap, vaddr_t va, size_t length,
			vm_prot_t prot, vm_cache_t cache, size_t level);

#ifdef __cplusplus
}
#endif
