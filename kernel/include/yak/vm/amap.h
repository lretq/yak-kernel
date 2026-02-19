#pragma once

#include <yak/mutex.h>
#include <yak/refcount.h>
#include <yak/types.h>
#include <yak/arch-mm.h>
#include <yak/vm/object.h>

// if amap_lookup should create intermediary layers
#define VM_AMAP_CREATE 0x1
// if we hold the amap lock already
#define VM_AMAP_LOCKED 0x2
#define VM_AMAP_DONT_LOCK_ANON 0x4

struct vm_amap_l3;

struct vm_amap {
	/* map entries that reference this amap */
	refcount_t refcnt;

	/* protects the actual map */
	struct kmutex lock;

	struct vm_object *obj;
	struct vm_amap_l3 *l3;
};

struct vm_amap *vm_amap_create(struct vm_object *obj);

DECLARE_REFMAINT(vm_amap);

struct vm_anon **vm_amap_lookup(struct vm_amap *amap, voff_t offset,
				unsigned int flags);

struct vm_amap *vm_amap_copy(struct vm_amap *amap);

struct vm_anon *vm_amap_fill_locked(struct vm_amap *amap, voff_t offset,
					   struct page *backing_page,
					   unsigned int flags);
