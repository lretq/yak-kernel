#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <yak/vm/page.h>
#include <yak/types.h>
#include <yak/macro.h>

enum {
	ZONE_1MB = 0,
	ZONE_LOW,
	ZONE_HIGH,
};

#define pmm_bytes_to_order(b) (next_ilog2((b)) - PAGE_SHIFT)

void pmm_init();

void pmm_zone_init(int zone_id, const char *name, int may_alloc, paddr_t base,
		   paddr_t end);

void pmm_add_region(paddr_t base, paddr_t end);

struct page *pmm_lookup_page(paddr_t addr);

struct page *pmm_dma_alloc_order(unsigned int order);
#define pmm_dma_free_order(pa, order) pmm_free_order(pa, order)

struct page *pmm_alloc_order(unsigned int order);

void pmm_free_pages_order(struct page *page, unsigned int order);

void pmm_free_order(paddr_t addr, unsigned int order);

void pmm_dump();

struct pmm_stat {
	size_t total_pages;
	size_t usable_pages;
	size_t free_pages;
};

void pmm_get_stat(struct pmm_stat *buf);

static inline paddr_t pmm_alloc()
{
	struct page *page = pmm_alloc_order(0);
	if (!page)
		return 0;
	return page_to_addr(page);
}

static inline paddr_t pmm_alloc_zeroed()
{
	struct page *page = pmm_alloc_order(0);
	if (!page)
		return 0;
	page_zero(page, 0);
	return page_to_addr(page);
}

static inline void pmm_free(paddr_t pa)
{
	struct page *page = pmm_lookup_page(pa);
	page_deref(page);
}

#ifdef __cplusplus
}
#endif
