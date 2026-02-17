#define pr_fmt(fmt) "pmm: " fmt

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <yak/queue.h>
#include <yak/log.h>
#include <yak/spinlock.h>
#include <yak/panic.h>
#include <yak/macro.h>
#include <yak/vm/pmm.h>
#include <yak/arch-mm.h>
#include <yak/vm/page.h>
#include <yak/vm.h>

#define BLOCK_SIZE(order) (1ULL << (PAGE_SHIFT + order))

struct region {
	paddr_t base, end;
	SLIST_ENTRY(region) list_entry;
	struct page pages[];
};

static SLIST_HEAD(region_list,
		  region) region_list = SLIST_HEAD_INITIALIZER(region_list);

static struct region *lookup_region(paddr_t addr)
{
	struct region *region;
	SLIST_FOREACH(region, &region_list, list_entry)
	{
		if (addr >= region->base && addr <= region->end)
			return region;
	}

	return NULL;
}

struct page *pmm_lookup_page(paddr_t addr)
{
	struct region *region = lookup_region(addr);
	if (!region)
		return NULL;

	const paddr_t base_pfn = (region->base >> PAGE_SHIFT);

	return &region->pages[(addr >> PAGE_SHIFT) - base_pfn];
}

typedef TAILQ_HEAD(page_list, page) page_list_t;

struct zone {
	int zone_id;
	const char *zone_name;

	unsigned int max_zone_order;

	paddr_t base, end;
	struct spinlock zone_lock;
	int may_alloc;

	SLIST_ENTRY(zone) list_entry;

	page_list_t orders[BUDDY_ORDERS];

	size_t npages[BUDDY_ORDERS];
};

static SLIST_HEAD(zone_list, zone) zone_list;

static size_t total_pagecnt = 0;
static size_t usable_pagecnt = 0;
static size_t free_pagecnt = 0;

#define MAX_ZONES 8
static struct zone static_zones[MAX_ZONES];
static size_t static_zones_pos = 0;

void pmm_zone_init(int zone_id, const char *name, int may_alloc, paddr_t base,
		   paddr_t end)
{
	size_t pos = __atomic_fetch_add(&static_zones_pos, 1, __ATOMIC_SEQ_CST);

	if (pos >= MAX_ZONES)
		panic("tried to initialize too many zones");

	struct zone *zone = &static_zones[pos];

	zone->zone_id = zone_id;
	zone->zone_name = name;

	zone->max_zone_order = 0;

	zone->base = base;
	zone->end = end;

	spinlock_init(&zone->zone_lock);

	zone->may_alloc = may_alloc;

	for (int i = 0; i < BUDDY_ORDERS; i++) {
		TAILQ_INIT(&zone->orders[i]);
		zone->npages[i] = 0;
	}

	if (SLIST_EMPTY(&zone_list)) {
		SLIST_INSERT_HEAD(&zone_list, zone, list_entry);
		return;
	}

	// insert zones sorted by address
	struct zone *ent, *min_zone = NULL;
	SLIST_FOREACH(ent, &zone_list, list_entry)
	{
		if (ent->base > zone->base)
			break;
		min_zone = ent;
	}

	if (min_zone == NULL) {
		SLIST_INSERT_HEAD(&zone_list, zone, list_entry);
		return;
	}

	pr_info("min_zone->base=0x%lx zone->base=0x%lx\n", min_zone->base,
		zone->base);

	SLIST_INSERT_AFTER(min_zone, zone, list_entry);
}

static struct zone *lookup_zone(paddr_t addr)
{
	struct zone *ent;
	SLIST_FOREACH(ent, &zone_list, list_entry)
	{
		if (addr >= ent->base && addr < ent->end)
			return ent;
	}
	return NULL;
}

static struct zone *lookup_zone_by_id(int zone_id)
{
	struct zone *ent;
	SLIST_FOREACH(ent, &zone_list, list_entry)
	{
		if (ent->zone_id == zone_id)
			return ent;
	}
	return NULL;
}

void page_zero(struct page *page, unsigned int order)
{
	memset((void *)page_to_mapped_addr(page), 0,
	       (1ULL << (order + PAGE_SHIFT)));
}

#if CONFIG_DEBUG
#define VALIDATE
#if 0
#define HARDCORE_VALIDATE
#endif
#endif

#ifdef VALIDATE
static void zone_validate(struct zone *zone)
{
	[[maybe_unused]]
	struct page *elm;

	for (unsigned int order = 0; order < BUDDY_ORDERS; order++) {
		if (zone->npages[order] > 0)
			assert(!TAILQ_EMPTY(&zone->orders[order]));
		else
			assert(TAILQ_EMPTY(&zone->orders[order]));

#ifdef HARDCORE_VALIDATE
		size_t n = 0;
		TAILQ_FOREACH(elm, &zone->orders[order], tailq_entry)
		{
			n++;
			assert(elm->shares == 0);
			assert(elm->order == order);
		}
		assert(n == zone->npages[order]);
#endif
	}
}
#else
#define zone_validate(...)
#endif

void pmm_add_region(paddr_t base, paddr_t end)
{
	assert((base % PAGE_SIZE) == 0);
	assert((end % PAGE_SIZE) == 0);

	assert(((base > UINT32_MAX) && (end > UINT32_MAX)) ||
	       ((base < UINT32_MAX) && (end < UINT32_MAX)));

	struct zone *zone = lookup_zone(base);
	zone_validate(zone);

	size_t pagecnt_total = (end - base) >> PAGE_SHIFT;
	size_t pagecnt_used =
		(ALIGN_UP(sizeof(struct region) +
				  sizeof(struct page) * pagecnt_total,
			  PAGE_SIZE)) >>
		PAGE_SHIFT;

	struct region *desc = (struct region *)p2v(base);

	desc->base = base;
	desc->end = end;

	if (SLIST_EMPTY(&region_list)) {
		SLIST_INSERT_HEAD(&region_list, desc, list_entry);
	} else {
		struct region *ent, *min_region = NULL;
		SLIST_FOREACH(ent, &region_list, list_entry)
		{
			if (ent->base > desc->base)
				break;
			min_region = ent;
		}

		if (min_region == NULL) {
			SLIST_INSERT_HEAD(&region_list, desc, list_entry);
		} else {
			SLIST_INSERT_AFTER(min_region, desc, list_entry);
		}
	}

	total_pagecnt += pagecnt_total;
	usable_pagecnt += pagecnt_total - pagecnt_used;

	free_pagecnt += pagecnt_total - pagecnt_used;

	memset(desc->pages, 0, sizeof(struct page) * pagecnt_total);

	const paddr_t base_pfn = base >> PAGE_SHIFT;
	for (size_t i = 0; i < pagecnt_used; i++) {
		struct page *page = &desc->pages[i];
		page->pfn = base_pfn + i;
		page->shares = 1;
		page->order = -1;
		page->max_order = -1;
	}

	if (pagecnt_total - pagecnt_used <= 0)
		return;

	for (size_t i = base + pagecnt_used * PAGE_SIZE; i < end;) {
		unsigned int max_order = 0;
		while ((max_order < BUDDY_ORDERS - 1) &&
		       (i + (2ULL << (PAGE_SHIFT + max_order)) < end) &&
		       IS_ALIGNED_POW2(i, (2ULL << (PAGE_SHIFT + max_order)))) {
			max_order++;
		}

		zone->max_zone_order = MAX(zone->max_zone_order, max_order);

		const size_t blksize = BLOCK_SIZE(max_order);

		for (size_t j = i; j < i + blksize; j += PAGE_SIZE) {
			const paddr_t pfn = (j >> PAGE_SHIFT);
			struct page *page = &desc->pages[pfn - base_pfn];
			page->pfn = pfn;
			page->shares = 0;
			page->order = max_order;
			page->max_order = max_order;
		}

		const paddr_t pfn = (i >> PAGE_SHIFT);
		struct page *page = &desc->pages[pfn - base_pfn];

		TAILQ_INSERT_TAIL(&zone->orders[max_order], page, tailq_entry);
		zone->npages[max_order] += 1;

		i += blksize;
	}

	zone_validate(zone);

	pr_debug("added 0x%lx-0x%lx\n", base, end);
}

static struct page *zone_alloc(struct zone *zone, unsigned int desired_order)
{
	assert(desired_order < BUDDY_ORDERS);

	if (unlikely(desired_order > zone->max_zone_order))
		return NULL;

	ipl_t ipl = spinlock_lock(&zone->zone_lock);
	zone_validate(zone);

	if (zone->npages[desired_order] > 0) {
		struct page *page = TAILQ_FIRST(&zone->orders[desired_order]);
		// -> npages >= 1
		assert(page);
		TAILQ_REMOVE(&zone->orders[desired_order], page, tailq_entry);
		zone->npages[desired_order] -= 1;

		// checks if page is actually free
		assert(page->shares == 0);

		page->shares = 1;

		zone_validate(zone);

		spinlock_unlock(&zone->zone_lock, ipl);

		__atomic_fetch_sub(&free_pagecnt, (1 << desired_order),
				   __ATOMIC_RELAXED);

		return page;
	}

	unsigned int order = desired_order;
	while (++order < BUDDY_ORDERS && TAILQ_EMPTY(&zone->orders[order])) {
	}
	if (unlikely(order >= BUDDY_ORDERS)) {
		spinlock_unlock(&zone->zone_lock, ipl);
		return NULL;
	}

	struct page *page = TAILQ_FIRST(&zone->orders[order]), *buddy;
	TAILQ_REMOVE(&zone->orders[order], page, tailq_entry);
	zone->npages[order] -= 1;
	buddy = page + (1 << order) / 2;

	while (order != desired_order) {
		order -= 1;

		TAILQ_INSERT_TAIL(&zone->orders[order], buddy, tailq_entry);
		zone->npages[order] += 1;
		buddy->order = order;

		buddy = page + (1 << order) / 2;
	}

	page->order = desired_order;
	assert(page->shares == 0);
	page->shares = 1;

	zone_validate(zone);

	spinlock_unlock(&zone->zone_lock, ipl);
	__atomic_fetch_sub(&free_pagecnt, (1 << desired_order),
			   __ATOMIC_RELAXED);

	return page;
}

static void zone_free(struct zone *zone, struct page *page, unsigned int order)
{
	assert(page);
	assert(order < BUDDY_ORDERS);

	ipl_t ipl = spinlock_lock(&zone->zone_lock);
	zone_validate(zone);

	assert(page->shares == 0);

	unsigned int initial_order = order;
	while (order < page->max_order) {
		const size_t block_size = BLOCK_SIZE(order);

		paddr_t page_addr = page_to_addr(page);

		paddr_t buddy_addr = page_addr ^ block_size;

		struct page *buddy_page = pmm_lookup_page(buddy_addr);

		assert(buddy_page); /* our buddy MUST exist,
                           else max_order is corrupt */
		assert(buddy_page->max_order == page->max_order);

		if (buddy_page->shares > 0 ||
		    buddy_page->order != page->order) {
			break;
		}

		assert(zone->npages[order] > 0);
		assert(!TAILQ_EMPTY(&zone->orders[order]));

#ifdef VALIDATE

		/* buddy page has to be on list */
		struct page *elm;

		TAILQ_FOREACH(elm, &zone->orders[order], tailq_entry)
		{
			if (elm == buddy_page)
				goto valid;
		}

		panic("buddy page not on list");

valid:
#endif

		/* now both buddy and we are free, off-list */
		zone->npages[order] -= 1;
		TAILQ_REMOVE(&zone->orders[order], buddy_page, tailq_entry);
		buddy_page->order += 1;
		page->order += 1;

		paddr_t lower_addr = page_addr;
		if (buddy_addr < page_addr) {
			lower_addr = buddy_addr;
		}

		page = pmm_lookup_page(lower_addr);

		order += 1;
	}

	zone->npages[order] += 1;
	TAILQ_INSERT_HEAD(&zone->orders[order], page, tailq_entry);
	assert(page->order == order);

	zone_validate(zone);

	spinlock_unlock(&zone->zone_lock, ipl);
	__atomic_fetch_add(&free_pagecnt, (1 << initial_order),
			   __ATOMIC_RELAXED);
}

struct page *pmm_alloc_order(unsigned int order)
{
	struct page *page;
	struct zone *zone;
	SLIST_FOREACH(zone, &zone_list, list_entry)
	{
		if (unlikely(!zone->may_alloc))
			continue;
		page = zone_alloc(zone, order);
		if (likely(page != NULL))
			return page;
	}
	return NULL;
}

struct page *pmm_zone_alloc_order(int zone_id, unsigned int order)
{
	struct zone *zone = lookup_zone_by_id(zone_id);
	if (!zone)
		return NULL;
	return zone_alloc(zone, order);
}

void pmm_free_order(paddr_t addr, unsigned int order)
{
	struct page *page = pmm_lookup_page(addr);
	zone_free(lookup_zone(addr), page, order);
}

void pmm_free_pages_order(struct page *page, unsigned int order)
{
	zone_free(lookup_zone(page_to_addr(page)), page, order);
}

void pmm_dump()
{
	printk(0, "\n=== PMM DUMP ===\n");

	struct zone *zone;
	SLIST_FOREACH(zone, &zone_list, list_entry)
	{
		int empty = 1;
		for (int i = 0; i < BUDDY_ORDERS; i++) {
			if (zone->npages[i] > 0) {
				empty = 0;
				break;
			}
		}
		if (empty)
			continue;

		printk(0, "\n%s: (max o. %u)\n", zone->zone_name,
		       zone->max_zone_order);

		for (int i = 0; i < BUDDY_ORDERS; i++) {
			if ((zone)->npages[i] > 0)
				printk(0, " * order %d pagecount: %ld\n", i,
				       (zone)->npages[i]);
		}
	}

	printk(0, "\nusable memory: %zuMiB/%zuMiB\n", usable_pagecnt >> 8,
	       total_pagecnt >> 8);

	printk(0, "\n");
}

void pmm_get_stat(struct pmm_stat *buf)
{
	assert(buf);
	buf->total_pages = total_pagecnt;
	buf->usable_pages = usable_pagecnt;
	buf->free_pages = __atomic_load_n(&free_pagecnt, __ATOMIC_RELAXED);
}
