#include <yak/types.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <yak/panic.h>
#include <yak/log.h>
#include <yak/arch-mm.h>
#include <yak/arch-cpu.h>
#include <yak/macro.h>
#include <yak/ipi.h>
#include <yak/vm.h>
#include <yak/vm/pmap.h>
#include <yak/vm/pmm.h>
#include <yak/vm/page.h>
#include <yak/vm/map.h>

#define PTE_LOAD(p) (__atomic_load_n((p), __ATOMIC_SEQ_CST))
#define PTE_STORE(p, x) (__atomic_store_n((p), (x), __ATOMIC_SEQ_CST))

static void pmap_free_table_level(pte_t *table, size_t lvl)
{
	size_t limit = PMAP_LEVEL_ENTRIES[lvl];

	// Dont free kernel page tables!
	// XXX: is this universal or x86 specific?
	if (lvl == PMAP_LEVELS - 1) {
		limit /= 2;
	}

	for (size_t i = 0; i < limit; i++) {
		pte_t *ptep = &table[i];
		pte_t pte = PTE_LOAD(ptep);

		if (pte_is_zero(pte)) {
			continue;
		}

		if (lvl != 0 && !pte_is_large(pte, lvl)) {
			uintptr_t pa = pte_paddr(pte);
			pte_t *child = (pte_t *)p2v(pa);
			pmap_free_table_level(child, lvl - 1);
			pmm_free(pa);
		} else {
			PTE_STORE(ptep, 0);
		}
	}
}

static bool is_pmap_mapped(struct pmap *pmap)
{
	for (size_t i = 0; i < elementsof(pmap->mapped_on.bits); i++) {
		if (pmap->mapped_on.bits[i] != 0)
			return true;
	}

	return false;
}

void pmap_destroy(struct pmap *pmap)
{
	assert(pmap->top_level != 0);
	assert(pmap != &kmap()->pmap);
	if (is_pmap_mapped(pmap)) {
		panic("destroy pmap that is still mapped!\n");
	}

	pte_t *toplevel = (pte_t *)p2v(pmap->top_level);

	pmap_free_table_level(toplevel, PMAP_LEVELS - 1);

	pmm_free(pmap->top_level);

	pmap->top_level = 0;
}

static pte_t *pte_fetch(struct pmap *pmap, uintptr_t va, size_t atLevel,
			int alloc)
{
	pte_t *table = (pte_t *)p2v(pmap->top_level);

	size_t indexes[PMAP_MAX_LEVELS];

	for (size_t i = 0; i < PMAP_LEVELS; i++) {
		indexes[i] = (va >> PMAP_LEVEL_SHIFTS[i]) &
			     ((1ULL << PMAP_LEVEL_BITS[i]) - 1);
	}

	for (size_t lvl = PMAP_LEVELS - 1;; lvl--) {
		//assert(lvl <= PMAP_LEVELS - 1);
		pte_t *ptep = &table[indexes[lvl]];
		//assert((uint64_t)ptep >= 0xffff800000000000);
		pte_t pte = PTE_LOAD(ptep);

		if (atLevel == lvl) {
			return ptep;
		}

		if (pte_is_zero(pte)) {
			if (!alloc) {
				return NULL;
			}

			uintptr_t pa = pmm_alloc_zeroed();
			assert(pa != 0);

			pte = pte_make_dir(pa);
			PTE_STORE(ptep, pte);
		} else {
			assert(!pte_is_large(pte, lvl));
		}

		table = (uint64_t *)p2v(pte_paddr(pte));
	}

	return NULL;
}

static inline void pmap_invalidate(vaddr_t va)
{
	asm volatile("invlpg (%0)" ::"r"(va) : "memory");
}

static void pmap_invalidate_range(vaddr_t va, size_t length, size_t pgsz)
{
	for (size_t i = 0; i < length; i += pgsz) {
		pmap_invalidate(va + i);
	}
}

struct shootdown_context {
	vaddr_t va;
	size_t length;
#ifdef PMAP_HAS_LARGE_PAGE_SIZES
	size_t level;
#endif
};

static void shootdown_handler(void *ctx)
{
	struct shootdown_context *shootdown_ctx = ctx;
	vaddr_t va = shootdown_ctx->va;
	size_t len = shootdown_ctx->length;

#ifdef PMAP_HAS_LARGE_PAGE_SIZES
	size_t level = shootdown_ctx->level;
	size_t pgsz = level == 0 ? PAGE_SIZE : PMAP_LARGE_PAGE_SIZES[level - 1];
#else
	size_t pgsz = PAGE_SIZE;
#endif

	pmap_invalidate_range(va, len, pgsz);
}

size_t n_shootdowns = 0;

static void do_tlb_shootdown(struct pmap *pmap, vaddr_t va, size_t length,
			     size_t level)
{
	if (cpus_online() == 1)
		return;

	__atomic_fetch_add(&n_shootdowns, 1, __ATOMIC_RELAXED);

	struct shootdown_context ctx;
	ctx.va = va;
	ctx.length = length;
	ctx.level = level;

	struct cpumask *mask = &pmap->mapped_on;
	if (pmap == &kmap()->pmap) {
		mask = &cpumask_active;
	}
	ipi_mask_send(mask, shootdown_handler, &ctx, false, true);
}

void pmap_kernel_bootstrap(struct pmap *pmap)
{
	pmap->top_level = pmm_alloc_zeroed();
	uint64_t *top_dir = (uint64_t *)p2v(pmap->top_level);
	// preallocate the top half so we can share among user maps
	for (size_t i = PMAP_LEVEL_ENTRIES[PMAP_LEVELS] / 2;
	     i < PMAP_LEVEL_ENTRIES[PMAP_LEVELS]; i++) {
		if (pte_is_zero(top_dir[i])) {
			top_dir[i] = pte_make_dir(pmm_alloc_zeroed());
		}
	}
}

void pmap_init(struct pmap *pmap)
{
	pmap->top_level = pmm_alloc_zeroed();
	uint64_t *top_dir = (uint64_t *)p2v(pmap->top_level);
	uint64_t *kernel_top_dir = (uint64_t *)p2v(kmap()->pmap.top_level);

	// copy the top 256 entries from kernel to user map
	for (size_t i = PMAP_LEVEL_ENTRIES[PMAP_LEVELS] / 2;
	     i < PMAP_LEVEL_ENTRIES[PMAP_LEVELS]; i++) {
		top_dir[i] = kernel_top_dir[i];
		assert(top_dir[i] != 0);
	}
}

// It is much more efficient to first unmap_range something, and then to map!
void pmap_map(struct pmap *pmap, uintptr_t va, uintptr_t pa, size_t level,
	      vm_prot_t prot, vm_cache_t cache)
{
	assert(prot & VM_READ);

	pte_t *ppte = pte_fetch(pmap, va, level, 1);
	assert(ppte);

	pte_t pte = PTE_LOAD(ppte);

	PTE_STORE(ppte, pte_make(level, pa, prot, cache));

	if (!pte_is_zero(pte)) {
		pmap_invalidate(va);
		do_tlb_shootdown(pmap, va, PAGE_SIZE, 0);
	}
}

static paddr_t do_unmap(struct pmap *pmap, vaddr_t va, size_t level)
{
	pte_t *ppte = pte_fetch(pmap, va, level, 0);
	if (ppte) {
		pte_t pte = PTE_LOAD(ppte);
		PTE_STORE(ppte, 0);
		pmap_invalidate(va);
		return pte_paddr(pte);
	}
	return UINTPTR_MAX;
}

paddr_t pmap_unmap(struct pmap *pmap, uintptr_t va, size_t level)
{
	paddr_t pa = do_unmap(pmap, va, level);
	if (pa != UINTPTR_MAX)
		do_tlb_shootdown(pmap, va, PAGE_SIZE, level);
	return pa;
}

void pmap_protect_range(struct pmap *pmap, vaddr_t va, size_t length,
			vm_prot_t prot, vm_cache_t cache, size_t level)
{
#ifdef PMAP_HAS_LARGE_PAGE_SIZES
	size_t pgsz = level == 0 ? PAGE_SIZE : PMAP_LARGE_PAGE_SIZES[level - 1];
#else
	size_t pgsz = PAGE_SIZE;
#endif

	for (uintptr_t i = 0; i < length; i += pgsz) {
		pte_t *ppte = pte_fetch(pmap, va + i, level, 0);
		if (ppte) {
			pte_t pte = PTE_LOAD(ppte);
			if (pte_is_zero(pte))
				continue;

			PTE_STORE(ppte,
				  pte_make(level, pte_paddr(pte), prot, cache));

			pmap_invalidate(va + i);
		}
	}

	do_tlb_shootdown(pmap, va, length, level);
}

void pmap_unmap_range(struct pmap *pmap, uintptr_t va, size_t length,
		      size_t level)
{
#ifdef PMAP_HAS_LARGE_PAGE_SIZES
	size_t pgsz = level == 0 ? PAGE_SIZE : PMAP_LARGE_PAGE_SIZES[level - 1];
#else
	size_t pgsz = PAGE_SIZE;
#endif
	for (uintptr_t i = 0; i < length; i += pgsz) {
		do_unmap(pmap, va + i, level);
	}

	do_tlb_shootdown(pmap, va, length, level);
}

// 32 * 4k = 128kib
#define FREE_BATCH 32

void pmap_unmap_range_and_free(struct pmap *pmap, uintptr_t base, size_t length,
			       size_t level)
{
	assert(level == 0);
	size_t pgsz = PAGE_SIZE;

	paddr_t batch[FREE_BATCH];
	size_t batch_count = 0;

	for (voff_t offset = 0; offset < length; offset += pgsz) {
		vaddr_t vaddr = base + offset;
		paddr_t pa = do_unmap(pmap, vaddr, level);
		batch[batch_count++] = pa;

		if (batch_count == FREE_BATCH) {
			do_tlb_shootdown(pmap, vaddr - (FREE_BATCH - 1) * pgsz,
					 FREE_BATCH * pgsz, level);

			for (size_t i = 0; i < batch_count; i++) {
				pmm_free(batch[i]);
			}

			batch_count = 0;
		}
	}

	if (batch_count > 0) {
		vaddr_t vaddr = base + length - batch_count * pgsz;
		do_tlb_shootdown(pmap, vaddr, batch_count * pgsz, level);
		for (size_t i = 0; i < batch_count; i++) {
			pmm_free(batch[i]);
		}
	}
}

void pmap_large_map_range(struct pmap *pmap, uintptr_t base, size_t length,
			  uintptr_t virtual_base, vm_prot_t prot,
			  vm_cache_t cache)
{
	uintptr_t end = base + length;
	uintptr_t curr = base;

	// map until aligned
	while (curr < end) {
#ifdef PMAP_HAS_LARGE_PAGE_SIZES
		if (IS_ALIGNED_POW2(curr, PMAP_LARGE_PAGE_SIZES[0])) {
			break;
		}
#endif
		pmap_map(pmap, virtual_base + curr - base, curr, 0, prot,
			 cache);
		curr += PAGE_SIZE;
	}

#ifdef PMAP_HAS_LARGE_PAGE_SIZES
	for (size_t i = elementsof(PMAP_LARGE_PAGE_SIZES); i > 0; i--) {
		while (curr < end) {
			if (curr + PMAP_LARGE_PAGE_SIZES[i - 1] >= end ||
			    !IS_ALIGNED_POW2(curr,
					     PMAP_LARGE_PAGE_SIZES[i - 1])) {
				goto outer;
			}

			pmap_map(pmap, virtual_base - base + curr, curr, i,
				 prot, cache);

			curr += PMAP_LARGE_PAGE_SIZES[i - 1];
		}
outer:;
	}

	// map until end
	while (curr < end) {
		pmap_map(pmap, virtual_base - base + curr, curr, 0, prot,
			 cache);
		curr += PAGE_SIZE;
	}
#endif

	assert(curr == end);
}
