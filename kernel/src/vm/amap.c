#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <yak/vm/anon.h>
#include <yak/log.h>
#include <yak/refcount.h>
#include <yak/heap.h>
#include <yak/macro.h>
#include <yak/vm/object.h>
#include <yak/vm/pmm.h>
#include <yak/vm/page.h>
#include <yak/vm/amap.h>

struct vm_amap_l1 {
	struct vm_anon *entries[PAGE_SIZE / sizeof(void *)];
};

struct vm_amap_l2 {
	struct vm_amap_l1 *entries[PAGE_SIZE / sizeof(void *)];
};

struct vm_amap_l3 {
	struct vm_amap_l2 *entries[PAGE_SIZE / sizeof(void *)];
};

struct vm_amap *vm_amap_create(struct vm_object *obj)
{
	assert(obj != NULL);

	struct vm_amap *amap = kmalloc(sizeof(struct vm_amap));
	assert(amap != NULL);
	memset(amap, 0, sizeof(struct vm_amap));

	amap->refcnt = 1;
	amap->l3 = NULL;

	kmutex_init(&amap->lock, "amap");

	vm_object_ref(obj);
	amap->obj = obj;

	return amap;
}

static void amap_free_all(struct vm_amap *amap)
{
	for (size_t i = 0; i < elementsof(amap->l3->entries); i++) {
		struct vm_amap_l2 *l2e = amap->l3->entries[i];
		if (!l2e)
			continue;

		for (size_t j = 0; j < elementsof(l2e->entries); j++) {
			struct vm_amap_l1 *l1e = l2e->entries[j];
			if (!l1e)
				continue;
			for (size_t k = 0; k < elementsof(l1e->entries); k++) {
				struct vm_anon *anon = l1e->entries[k];
				if (!anon)
					continue;

				vm_anon_deref(anon);

				l1e->entries[k] = NULL;
			}

			kfree(l1e, sizeof(struct vm_amap_l1));
		}

		kfree(l2e, sizeof(struct vm_amap_l2));
	}

	kfree(amap->l3, sizeof(struct vm_amap_l3));
	amap->l3 = NULL;
}

static void amap_cleanup(struct vm_amap *amap)
{
	// at this point, every reference to the amap has been dropped
	// we now need to drop references to anons, pages, and the backing object.

	EXPECT(kmutex_acquire(&amap->lock, TIMEOUT_INFINITE));

	if (amap->l3) {
		amap_free_all(amap);
	}

	vm_object_deref(amap->obj);

	kfree(amap, sizeof(struct vm_amap));
}

GENERATE_REFMAINT(vm_amap, refcnt, amap_cleanup);

static struct vm_anon **locked_amap_lookup(struct vm_amap *amap, voff_t offset,
					   unsigned int flags)
{
	assert(amap);

	bool create = (flags & VM_AMAP_CREATE);

	size_t l3i, l2i, l1i;
	// mimic a 3level page table
	size_t pg = offset >> 12;
	l3i = (pg >> 18) & 511;
	l2i = (pg >> 9) & 511;
	l1i = pg & 511;

	if (!amap->l3) {
		if (!create)
			return NULL;

		amap->l3 = kmalloc(sizeof(struct vm_amap_l3));
		memset(amap->l3, 0, sizeof(struct vm_amap_l3));
	}

	struct vm_amap_l2 *l2_entry = amap->l3->entries[l3i];
	if (!l2_entry) {
		if (!create)
			return NULL;

		l2_entry = kmalloc(sizeof(struct vm_amap_l2));
		memset(l2_entry, 0, sizeof(struct vm_amap_l2));
		amap->l3->entries[l3i] = l2_entry;
	}

	struct vm_amap_l1 *l1_entry = l2_entry->entries[l2i];
	if (!l1_entry) {
		if (!create)
			return NULL;

		l1_entry = kmalloc(sizeof(struct vm_amap_l1));
		memset(l1_entry, 0, sizeof(struct vm_amap_l1));
		l2_entry->entries[l2i] = l1_entry;
	}

	struct vm_anon *anon = l1_entry->entries[l1i];
	if (anon && !(flags & VM_AMAP_DONT_LOCK_ANON))
		EXPECT(kmutex_acquire(&anon->anon_lock, TIMEOUT_INFINITE));

	return &l1_entry->entries[l1i];
}

struct vm_anon **vm_amap_lookup(struct vm_amap *amap, voff_t offset,
				unsigned int flags)
{
	if (flags & VM_AMAP_LOCKED)
		return locked_amap_lookup(amap, offset, flags);

	guard(mutex)(&amap->lock);
	return locked_amap_lookup(amap, offset, flags);
}

// XXX: should we create a new anon object for the amap?
// How should page-in be handled? They would share an offset in the object.
// Currently we don't get the new (copied) page from the object.
struct vm_amap *vm_amap_copy(struct vm_amap *amap)
{
	assert(amap);

	guard(mutex)(&amap->lock);

	struct vm_amap *new_amap = vm_amap_create(amap->obj);

	if (!amap->l3)
		return new_amap;

	new_amap->l3 = kzalloc(sizeof(struct vm_amap_l3));
	if (!new_amap->l3)
		panic("oom while copying amap\n");

	for (size_t l3i = 0; l3i < elementsof(amap->l3->entries); l3i++) {
		struct vm_amap_l2 *l2 = amap->l3->entries[l3i];
		if (!l2)
			continue;

		struct vm_amap_l2 *l2_copy = kzalloc(sizeof(struct vm_amap_l2));
		if (!l2_copy)
			panic("oom\n");

		new_amap->l3->entries[l3i] = l2_copy;

		for (size_t l2i = 0; l2i < elementsof(l2->entries); l2i++) {
			struct vm_amap_l1 *l1 = l2->entries[l2i];
			if (!l1)
				continue;

			struct vm_amap_l1 *l1_copy =
				kzalloc(sizeof(struct vm_amap_l1));
			if (!l1_copy)
				panic("oom\n");

			// oh the sorrows this line caused;
			// I spent days figuring out why the hell fork failed.
			// Turns out, I used l3i instead of l2i :[
			l2_copy->entries[l2i] = l1_copy;

			for (size_t l1i = 0; l1i < elementsof(l1->entries);
			     l1i++) {
				struct vm_anon *anon = l1->entries[l1i];
				if (!anon)
					continue;

				// share anon, bump the refcnt
				vm_anon_ref(anon);
				l1_copy->entries[l1i] = anon;
			}
		}
	}

	return new_amap;
}

struct vm_anon *vm_amap_fill_locked(struct vm_amap *amap, voff_t offset,
				    struct page *backing_page,
				    unsigned int flags)
{
	// create l3/l2/l1 if needed, amap already locked
	struct vm_anon **panon =
		vm_amap_lookup(amap, offset, VM_AMAP_CREATE | VM_AMAP_LOCKED);
	assert(*panon == NULL);

	struct page *dest_page = vm_pagealloc(NULL, 0);

	memcpy((void *)page_to_mapped_addr(dest_page),
	       (const void *)page_to_mapped_addr(backing_page), PAGE_SIZE);

	*panon = vm_anon_create(dest_page, 0);
	return *panon;
}
