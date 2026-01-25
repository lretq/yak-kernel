#include <stddef.h>
#include <string.h>
#include <yak/status.h>
#include <yak/vm/anon.h>
#include <yak/types.h>
#include <yak/sched.h>
#include <yak/vm/map.h>
#include <yak/mutex.h>
#include <yak/rwlock.h>
#include <yak/heap.h>
#include <yak/vm/pmap.h>
#include <yak/vm/pmm.h>
#include <yak/vm/amap.h>
#include <yak/vm/object.h>
#include <yak/macro.h>
#include <yak/arch-mm.h>
#include <yak/log.h>

size_t n_pagefaults;

// TODO: fault_flags are also used in map.c:VM_PREFILL!!!
status_t vm_handle_fault(struct vm_map *map, vaddr_t address,
			 unsigned long fault_flags)
{
	assert(map);

	if ((fault_flags & VM_FAULT_PREFILL) == 0)
		__atomic_fetch_add(&n_pagefaults, 1, __ATOMIC_RELAXED);

	(void)fault_flags;

	address = ALIGN_DOWN(address, PAGE_SIZE);

	rwlock_acquire_shared(&map->map_lock, TIMEOUT_INFINITE);
	struct vm_map_entry *entry = vm_map_lookup_entry_locked(map, address);

	if (!entry || entry->type == VM_MAP_ENT_RESERVED) {
		rwlock_release_shared(&map->map_lock);
		if (address < PAGE_SIZE)
			return YAK_NULL_DEREF;

		return YAK_NOENT;
	}

	if (!(entry->protection & VM_WRITE)) {
		if (fault_flags & VM_FAULT_WRITE)
			return YAK_PERM_DENIED;
	}

	voff_t map_offset = address - entry->base;
	voff_t backing_offset = map_offset + entry->offset;

	if (entry->type == VM_MAP_ENT_MMIO) {
		pmap_map(&map->pmap, address, entry->mmio_addr + backing_offset,
			 0, entry->protection, entry->cache);

		rwlock_release_shared(&map->map_lock);
		return YAK_SUCCESS;
	} else if (entry->type == VM_MAP_ENT_OBJ) {
		rwlock_upgrade_to_exclusive(&map->map_lock);

		assert(entry->object != NULL);

		struct page *page = NULL;

		if (entry->is_cow) {
			struct vm_amap *amap = entry->amap;
			assert(amap);

			guard(mutex)(&amap->lock);

			// lookup, fail early (don't create layer chain)
			struct vm_anon *anon = NULL,
				       **panon = vm_amap_lookup(amap,
								backing_offset,
								VM_AMAP_LOCKED);
			// TODO: handle page-in?

			vm_prot_t prot = entry->protection;

			if (panon && *panon) {
				anon = *panon;
				page = anon->page;
				assert(page);
			} else {
				struct page *backing_page;
				EXPECT(vm_lookuppage(entry->object,
						     backing_offset, 0,
						     &backing_page));
				if (fault_flags & VM_FAULT_WRITE) {
					// anon will never take the cow route.
					// lookup & copy the backing file data
					anon = vm_amap_fill_locked(
						amap, backing_offset,
						backing_page, VM_AMAP_LOCKED);
					assert(anon);
					EXPECT(kmutex_acquire(&anon->anon_lock,
							      TIMEOUT_INFINITE));

					page = anon->page;
				} else {
					page = backing_page;

					prot &= ~VM_WRITE;

					pmap_map(&map->pmap, address,
						 page_to_addr(page), 0, prot,
						 entry->cache);

					goto exit;
				}
			}

			assert(anon);
			assert(page != NULL);

			// Handle Copy on Write (CoW)
			// => after fork(), for mmap(MAP_PRIVATE) mappings
			//
			// We essentially want to either duplicate the anon if
			// we write-fault, or, in case of a read-fault, map the
			// anons page read-only, regardless of the protection.
			// If an attempt to write is made, we shall meet again :)
			// Either, the anons refcount is now 1, or we copy.
			pr_extra_debug("fault %lx\n", address);
			if (anon->refcnt > 1) {
				pr_extra_debug("cow %lx\n", address);
				if (fault_flags & VM_FAULT_READ) {
					prot &= ~VM_WRITE;
					pr_extra_debug(
						"cow anon: read fault\n");
				} else if (fault_flags & VM_FAULT_WRITE) {
					// copy anon
					struct vm_anon *copied_anon =
						vm_anon_copy(anon);

					*panon = copied_anon;

					// safe: can't hit free condition
					vm_anon_deref(anon);
					kmutex_release(&anon->anon_lock);

					anon = copied_anon;
					// reacquire new anons lock
					kmutex_acquire(&anon->anon_lock,
						       TIMEOUT_INFINITE);

					page = anon->page;
				}
			}

			pmap_map(&map->pmap, address, page_to_addr(page), 0,
				 prot, entry->cache);

			kmutex_release(&anon->anon_lock);
		} else {
			// No cow & thus no amap associated
			EXPECT(vm_lookuppage(entry->object, backing_offset, 0,
					     &page));
			pmap_map(&map->pmap, address, page_to_addr(page), 0,
				 entry->protection, entry->cache);
		}

exit:
		rwlock_release_exclusive(&map->map_lock);
		return YAK_SUCCESS;
	}

	// corrupted entry type?
	__builtin_unreachable();
}
