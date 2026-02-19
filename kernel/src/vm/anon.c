#define pr_fmt(fmt) "vm: " fmt

#include <assert.h>
#include <string.h>
#include <yak/log.h>
#include <yak/vm/anon.h>
#include <yak/vm/page.h>
#include <yak/vm/pmm.h>
#include <yak/heap.h>

static void vm_anon_free(struct vm_anon *anon);
GENERATE_REFMAINT(vm_anon, refcnt, vm_anon_free);

static void vm_anon_free(struct vm_anon *anon)
{
	if (anon->page) {
		page_deref(anon->page);
		anon->page = NULL;
	}

	kfree(anon, sizeof(struct vm_anon));
}

struct vm_anon *vm_anon_create(struct page *page, voff_t offset)
{
	struct vm_anon *anon = kmalloc(sizeof(struct vm_anon));
	memset(anon, 0, sizeof(struct vm_anon));

	kmutex_init(&anon->anon_lock, "anon");
	anon->page = page;
	anon->offset = offset;
	anon->refcnt = 1;

	return anon;
}

// called with anon lock held
struct vm_anon *vm_anon_copy(struct vm_anon *anon)
{
	assert(anon);
	struct page *src_page = anon->page;
	struct page *dest_page =
		vm_pagealloc(src_page->vmobj, src_page->offset);

	pr_extra_debug("anon_copy: from %lx to %lx\n", src_page->pfn,
		       dest_page->pfn);
	pr_extra_debug("anon_copy: refcounts: from=%ld to=%ld\n",
		       src_page->shares, dest_page->shares);

	memcpy((void *)page_to_mapped_addr(dest_page),
	       (const void *)page_to_mapped_addr(src_page), PAGE_SIZE);

	return vm_anon_create(dest_page, anon->offset);
}
