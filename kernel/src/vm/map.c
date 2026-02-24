#include <string.h>
#include <yak/vm/anon.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <yak/vm/map.h>
#include <yak/vm/object.h>
#include <yak/vm/aobj.h>
#include <yak/mutex.h>
#include <yak/rwlock.h>
#include <yak/cpudata.h>
#include <yak/sched.h>
#include <yak/macro.h>
#include <yak/vm.h>
#include <yak/vm/pmap.h>
#include <yak/vm/amap.h>
#include <yak/vm/vmem.h>
#include <yak/arch-mm.h>
#include <yak/status.h>
#include <yak/log.h>
#include <yak/types.h>
#include <yak/vmflags.h>
#include <yak/heap.h>
#include <yak/tree.h>

int vm_map_entry_cmp(const struct vm_map_entry *a, const struct vm_map_entry *b)
{
	if (a->base == b->base)
		return 0;
	return a->base > b->base ? 1 : -1;
}

RBT_PROTOTYPE(vm_map_rbtree, vm_map_entry, tree_entry, vm_map_entry_cmp);
RBT_GENERATE(vm_map_rbtree, vm_map_entry, tree_entry, vm_map_entry_cmp);

struct vm_map kernel_map;

struct vm_map *kmap()
{
	return &kernel_map;
}

static struct vm_map_entry *alloc_map_entry()
{
	// XXX: slab
	return kzalloc(sizeof(struct vm_map_entry));
}

static void free_map_entry(struct vm_map_entry *entry)
{
	kfree(entry, sizeof(struct vm_map_entry));
}

status_t vm_map_init(struct vm_map *map)
{
	rwlock_init(&map->map_lock, "map_lock");

	RBT_INIT(vm_map_rbtree, &map->map_tree);

	if (unlikely(map == &kernel_map)) {
		pmap_kernel_bootstrap(&map->pmap);
	} else {
		pmap_init(&map->pmap);
	}

	return YAK_SUCCESS;
}

void vm_map_destroy(struct vm_map *map)
{
	EXPECT(rwlock_acquire_exclusive(&map->map_lock, TIMEOUT_INFINITE));

	while (!RBT_EMPTY(vm_map_rbtree, &map->map_tree)) {
		struct vm_map_entry *entry =
			RBT_ROOT(vm_map_rbtree, &map->map_tree);
		RBT_REMOVE(vm_map_rbtree, &map->map_tree, entry);

		if (entry->type == VM_MAP_ENT_OBJ) {
			if (entry->amap)
				vm_amap_deref(entry->amap);
			vm_object_deref(entry->object);
		}

		free_map_entry(entry);
	}

	pmap_destroy(&map->pmap);
}

static void init_map_entry(struct vm_map_entry *entry, voff_t offset,
			   vaddr_t base, vaddr_t end, vm_prot_t prot,
			   vm_inheritance_t inheritance, vm_cache_t cache,
			   unsigned short entry_type)
{
	entry->base = base;
	entry->end = end;

	entry->type = entry_type;

	entry->offset = offset;

	entry->is_cow = (inheritance == VM_INHERIT_COPY);

	entry->amap = NULL;

	entry->max_protection = entry->protection = prot;

	entry->inheritance = inheritance;

	entry->cache = cache;
}

const char *entry_type(struct vm_map_entry *entry)
{
	switch (entry->type) {
	case VM_MAP_ENT_MMIO:
		return "mmio";
	case VM_MAP_ENT_OBJ:
		if (entry->object) {
			return "object";
		} else {
			return "anon";
		}
	}
	return "<unknown>";
}

#if CONFIG_DEBUG
void vm_map_dump(struct vm_map *map)
{
	struct vm_map_entry *entry;
	printk(0, "\t=== MAP DUMP ===\n");

	printk(0, "map: 0x%p\nentries:\n", map);
	RBT_FOREACH(entry, vm_map_rbtree, &map->map_tree)
	{
		printk(0, "(%p): 0x%lx - 0x%lx (%s)", entry, entry->base,
		       entry->end, entry_type(entry));
		if (entry->type == VM_MAP_ENT_OBJ) {
			printk(0, " inherit=%d is_cow=%d", entry->inheritance,
			       entry->is_cow);
		}
		printk(0, "\n");
	}
}
#endif

// lowest node where element->base >= base
static struct vm_map_entry *map_lower_bound(struct vm_map *map, vaddr_t addr)
{
	struct vm_map_entry *res = NULL;
	struct vm_map_entry *cur = RBT_ROOT(vm_map_rbtree, &map->map_tree);

	while (cur) {
		if (addr > cur->base) {
			// too little: have to go right
			cur = RBT_RIGHT(vm_map_rbtree, cur);
		} else {
			res = cur;
			// but there could be a closer one still
			cur = RBT_LEFT(vm_map_rbtree, cur);
		}
	}

	return res;
}

// carve out entry from a larger entry
static status_t carve_entry(struct vm_map *map, struct vm_map_entry *entry,
			    vaddr_t split_base, vaddr_t split_end)
{
	vaddr_t orig_base = entry->base;
	vaddr_t orig_end = entry->end;

	assert(split_base >= entry->base && split_base < entry->end);
	assert(split_end >= entry->base && split_end < entry->end);

	bool need_left = split_base > entry->base;
	bool need_right = split_end < entry->end;
	assert(need_left || need_right);

	RBT_REMOVE(vm_map_rbtree, &map->map_tree, entry);

	if (need_left) {
		struct vm_map_entry *left =
			kzalloc(sizeof(struct vm_map_entry));
		if (!left)
			return YAK_OOM;

		size_t left_size = split_base - orig_base;

		init_map_entry(left, entry->offset, orig_base, split_base,
			       entry->protection, entry->inheritance,
			       entry->cache, entry->type);

		left->max_protection = entry->max_protection;

		if (entry->type == VM_MAP_ENT_OBJ) {
			left->object = entry->object;
			left->amap = entry->amap;
			if (left->object)
				vm_object_ref(left->object);
			if (left->amap)
				vm_amap_ref(left->amap);
		}

		RBT_INSERT(vm_map_rbtree, &map->map_tree, left);

		entry->base = split_base;
		entry->offset += left_size;
	}

	if (need_right) {
		struct vm_map_entry *right =
			kzalloc(sizeof(struct vm_map_entry));

		if (!right)
			return YAK_OOM;

		size_t middle_size = split_end - split_base;

		init_map_entry(right, entry->offset + middle_size, split_end,
			       orig_end, entry->protection, entry->inheritance,
			       entry->cache, entry->type);

		right->max_protection = entry->max_protection;

		if (entry->type == VM_MAP_ENT_OBJ) {
			right->object = entry->object;
			right->amap = entry->amap;
			if (right->object)
				vm_object_ref(right->object);
			if (right->amap)
				vm_amap_ref(right->amap);
		}

		RBT_INSERT(vm_map_rbtree, &map->map_tree, right);

		entry->end = split_end;
	} else {
		assert(entry->end == split_end);
	}

	// Re-insert modified entry
	RBT_INSERT(vm_map_rbtree, &map->map_tree, entry);

	return YAK_SUCCESS;
}

// Enforces hierarchy:
// Write -> Exec -> Read
static inline bool vm_check_prot(vm_prot_t max_prot, vm_prot_t new_prot)
{
	if ((new_prot & VM_WRITE) && !(max_prot & VM_WRITE))
		return false;

	if ((new_prot & VM_EXECUTE) && !(max_prot & (VM_EXECUTE | VM_WRITE)))
		return false;

	if ((new_prot & VM_READ) &&
	    !(max_prot & (VM_READ | VM_EXECUTE | VM_WRITE)))
		return false;

	return true;
}

status_t vm_protect(struct vm_map *map, vaddr_t va, size_t length,
		    vm_prot_t prot, int flags)
{
	assert(map);
	if (!IS_ALIGNED_POW2(va, PAGE_SIZE) ||
	    !IS_ALIGNED_POW2(length, PAGE_SIZE))
		return YAK_INVALID_ARGS;

	guard(rwlock)(&map->map_lock, TIMEOUT_INFINITE,
		      (flags & VM_MAP_LOCK_HELD) ? RWLOCK_GUARD_SKIP :
						   RWLOCK_GUARD_EXCLUSIVE);

	if (RBT_EMPTY(vm_map_rbtree, &map->map_tree))
		return YAK_SUCCESS;

	vaddr_t search_base = va;
	vaddr_t search_end = va + length;

	struct vm_map_entry *current = map_lower_bound(map, search_base);
	if (!current)
		current = RBT_MAX(vm_map_rbtree, &map->map_tree);
	struct vm_map_entry *prev = RBT_PREV(vm_map_rbtree, current);

	// move to the first entry that might overlap [start, end)
	if (prev && prev->end > search_base)
		current = prev;

	while (current && current->base < search_end) {
		// we might need to modify/split later
		struct vm_map_entry *next = RBT_NEXT(vm_map_rbtree, current);

		vaddr_t entry_base = current->base;
		vaddr_t entry_end = current->end;

		if (entry_end <= search_base || entry_base >= search_end) {
			current = next;
			continue;
		}

		vaddr_t split_base = MAX(entry_base, search_base);
		vaddr_t split_end = MIN(entry_end, search_end);

		if (entry_base != split_base || entry_end != split_end) {
			// current is modified in-place
			status_t rv = carve_entry(map, current, split_base,
						  split_end);
			if (IS_ERR(rv))
				return rv;
		}

		// current is now fully inside [start, end)

		if (flags & VM_MAP_SETMAXPROT) {
			current->max_protection = prot;
		} else if (!vm_check_prot(current->max_protection, prot)) {
			return YAK_PERM_DENIED;
		}

		current->protection = prot;

		pmap_protect_range(&map->pmap, current->base,
				   current->end - current->base, prot,
				   current->cache, 0);

		current = next;
	}

	return YAK_SUCCESS;
}

status_t vm_unmap(struct vm_map *map, vaddr_t va, size_t length, int flags)
{
	assert(map);

	va = ALIGN_DOWN(va, PAGE_SIZE);
	length = ALIGN_UP(length, PAGE_SIZE);

	guard(rwlock)(&map->map_lock, TIMEOUT_INFINITE,
		      (flags & VM_MAP_LOCK_HELD) ? RWLOCK_GUARD_SKIP :
						   RWLOCK_GUARD_EXCLUSIVE);

	if (RBT_EMPTY(vm_map_rbtree, &map->map_tree))
		return YAK_SUCCESS;

	vaddr_t start = va;
	vaddr_t end = va + length;

	struct vm_map_entry *current = map_lower_bound(map, start);
	if (!current)
		current = RBT_MAX(vm_map_rbtree, &map->map_tree);

	struct vm_map_entry *prev = RBT_PREV(vm_map_rbtree, current);
	if (prev && prev->end > start)
		current = prev;

	while (current && current->base < end) {
		struct vm_map_entry *next = RBT_NEXT(vm_map_rbtree, current);

		vaddr_t entry_base = current->base;
		vaddr_t entry_end = current->end;

		if (entry_end <= start || entry_base >= end) {
			current = next;
			continue;
		}

		vaddr_t split_base = MAX(entry_base, start);
		vaddr_t split_end = MIN(entry_end, end);

		if (entry_base != split_base || entry_end != split_end) {
			status_t rv = carve_entry(map, current, split_base,
						  split_end);
			if (IS_ERR(rv))
				return rv;

			// current now exactly covers [split_base, split_end)
			current = map_lower_bound(map, split_base);
		}

		// unmap pages from the pmap
		pmap_unmap_range(&map->pmap, current->base,
				 current->end - current->base, 0);

		// deref object/amap if needed
		if (current->type == VM_MAP_ENT_OBJ) {
			assert(current->object);
			if (current->amap)
				vm_amap_deref(current->amap);
			vm_object_deref(current->object);
		}

		// remove from map and free
		RBT_REMOVE(vm_map_rbtree, &map->map_tree, current);
		free_map_entry(current);

		current = next;
	}

	return YAK_SUCCESS;
}

static status_t alloc_map_range_locked(struct vm_map *map, vaddr_t hint,
				       size_t length, vm_prot_t prot,
				       vm_inheritance_t inheritance,
				       vm_cache_t cache, voff_t offset,
				       unsigned short entry_type, int flags,
				       struct vm_map_entry **entry)
{
	vaddr_t min_addr, max_addr;

	if (map == kmap()) {
		min_addr = KERNEL_VA_BASE;
		max_addr = KERNEL_VA_END;
	} else {
		min_addr = USER_VA_BASE;
		max_addr = USER_VA_END;
	}

	if (length == 0) {
		return YAK_INVALID_ARGS;
	}

	if (length > max_addr - min_addr) {
		return YAK_NOSPACE;
	}

	struct vm_map_entry *next, *prev;
	next = prev = NULL;
	vaddr_t base = hint;

	// find first node >= hint
	next = map_lower_bound(map, base);

	if (flags & VM_MAP_FIXED) {
		// tried to allocate either null page or user va
		if (base < min_addr)
			return YAK_INVALID_ARGS;
		// overflow
		if (base + length < base)
			return YAK_INVALID_ARGS;
		// tried to allocate beyond allowed range
		// (e.g. > 32 bit or in kernel range)
		if (base + length > max_addr)
			return YAK_INVALID_ARGS;

		prev = next ? RBT_PREV(vm_map_rbtree, next) :
			      RBT_MAX(vm_map_rbtree, &map->map_tree);

		if ((prev && prev->end > base) ||
		    (next && next->base < base + length)) {
			if (!(flags & VM_MAP_OVERWRITE))
				return YAK_EXISTS;

			vm_unmap(map, base, length, VM_MAP_LOCK_HELD);
		}

		goto found;
	}

	if (base < min_addr)
		base = min_addr;

	if (base > max_addr)
		base = max_addr;

	if (RBT_EMPTY(vm_map_rbtree, &map->map_tree)) {
		// empty map: try to satisfy mapping exactly at hint
		if (base + length > max_addr) {
			base = min_addr;
			if (base + length > max_addr) {
				return YAK_NOSPACE;
			}
		}
		goto found;
	}

	next = map_lower_bound(map, base);
	prev = next ? RBT_PREV(vm_map_rbtree, next) : NULL;

	if (!prev) {
		assert(next);

		vaddr_t gap_start = min_addr;
		if (next->base >= gap_start + length) {
			base = gap_start;
			goto found;
		}

		prev = next;
		next = RBT_NEXT(vm_map_rbtree, next);
	}

	bool wrapped = false;
	for (;;) {
		vaddr_t gap_start = prev ? prev->end : min_addr;
		vaddr_t gap_end = next ? next->base : max_addr;

		// try to respect the hint if it lies inside the gap
		gap_start = MAX(gap_start, base);

		if (gap_end - gap_start >= length) {
			base = gap_start;
			goto found;
		}

		prev = next;
		next = RBT_NEXT(vm_map_rbtree, next);
		if (!next) {
			if (prev->end + length <= max_addr) {
				base = prev->end;
				goto found;
			}

			if (hint == 0 || wrapped) {
				return YAK_NOSPACE;
			}

			wrapped = true;
			base = min_addr;
			prev = NULL;
			next = RBT_MIN(vm_map_rbtree, &map->map_tree);
		}
	}

found:
	assert(base >= min_addr || base + length < max_addr);

	*entry = alloc_map_entry();
	if (!entry)
		return YAK_OOM;

	init_map_entry(*entry, offset, base, base + length, prot, inheritance,
		       cache, entry_type);
	RBT_INSERT(vm_map_rbtree, &map->map_tree, *entry);

	return YAK_SUCCESS;
}

status_t vm_map_reserve(struct vm_map *map, vaddr_t hint, size_t length,
			int flags, vaddr_t *out)
{
	guard(rwlock)(&map->map_lock, TIMEOUT_INFINITE, RWLOCK_GUARD_EXCLUSIVE);

	struct vm_map_entry *ent;
	status_t rv = alloc_map_range_locked(map, hint, length, 0, 0, 0, 0,
					     VM_MAP_ENT_RESERVED, flags, &ent);
	*out = IS_OK(rv) ? ent->base : 0;
	return rv;
}

status_t vm_map_mmio(struct vm_map *map, paddr_t device_addr, size_t length,
		     vm_prot_t prot, vm_cache_t cache, vaddr_t *out)
{
	guard(rwlock)(&map->map_lock, TIMEOUT_INFINITE, RWLOCK_GUARD_EXCLUSIVE);

	paddr_t rounded_addr = ALIGN_DOWN(device_addr, PAGE_SIZE);
	size_t offset = device_addr - rounded_addr;
	// aligned length
	length = ALIGN_UP(offset + length, PAGE_SIZE);

	struct vm_map_entry *entry;
	TRY(alloc_map_range_locked(map, 0, length, prot, VM_INHERIT_NONE, cache,
				   offset, VM_MAP_ENT_MMIO, 0, &entry));

	entry->mmio_addr = rounded_addr;

	vaddr_t addr = entry->base;
	*out = addr + offset;

	return YAK_SUCCESS;
}

status_t vm_map(struct vm_map *map, struct vm_object *obj, size_t length,
		voff_t offset, vm_prot_t prot, vm_inheritance_t inheritance,
		vm_cache_t cache, vaddr_t hint, int flags, vaddr_t *out)
{
	guard(rwlock)(&map->map_lock, TIMEOUT_INFINITE, RWLOCK_GUARD_EXCLUSIVE);

	struct vm_map_entry *entry;
	status_t rv = alloc_map_range_locked(map, hint, length, prot,
					     inheritance, cache, offset,
					     VM_MAP_ENT_OBJ, flags, &entry);
	IF_ERR(rv)
	{
		return rv;
	}

	vaddr_t addr = entry->base;

	if (obj == NULL) {
		obj = vm_aobj_create();
	} else {
		vm_object_ref(obj);
	}
	entry->object = obj;

	if (inheritance == VM_INHERIT_COPY) {
		entry->is_cow = true;
		entry->amap = vm_amap_create(obj);
	} else {
		entry->is_cow = false;
		entry->amap = NULL;
	}

	if (flags & VM_MAP_PREFILL) {
		for (voff_t off = 0; off < length; off += PAGE_SIZE) {
			EXPECT(vm_handle_fault(map, addr + off,
					       VM_FAULT_PREFILL));
		}
	}

	*out = addr;
	return YAK_SUCCESS;
}

struct vm_map_entry *vm_map_lookup_entry_locked(struct vm_map *map,
						uintptr_t address)
{
	struct vm_map_entry *entry = RBT_ROOT(vm_map_rbtree, &map->map_tree);
	while (entry) {
		if (address < entry->base) {
			entry = RBT_LEFT(vm_map_rbtree, entry);
		} else if (address >= entry->end) {
			entry = RBT_RIGHT(vm_map_rbtree, entry);
		} else {
			assert(address >= entry->base && address <= entry->end);
			return entry;
		}
	}
	return NULL;
}

void vm_map_activate(struct vm_map *map)
{
	assert(map);

	struct vm_map *old = PERCPU_FIELD_LOAD(current_map);

	if (old != map) {
		pmap_activate(&map->pmap);
	}

	PERCPU_FIELD_STORE(current_map, map);

	if (old != NULL) {
		bitset_atomic_clear(&old->pmap.mapped_on, cpuid());
	}

	bitset_atomic_set(&map->pmap.mapped_on, cpuid());
}

struct vm_map *vm_map_tmp_switch(struct vm_map *map)
{
	struct vm_map *orig = PERCPU_FIELD_LOAD(current_map);
	curthread()->vm_ctx = map;
	vm_map_activate(map);
	return orig;
}

void vm_map_tmp_disable(struct vm_map *map)
{
	assert(curthread()->vm_ctx);
	curthread()->vm_ctx = NULL;
	vm_map_activate(map);
}

status_t vm_map_fork(struct vm_map *from, struct vm_map *to)
{
	// to has to be initialized already

	guard(rwlock)(&from->map_lock, TIMEOUT_INFINITE, RWLOCK_GUARD_SHARED);
	guard(rwlock)(&to->map_lock, TIMEOUT_INFINITE, RWLOCK_GUARD_EXCLUSIVE);

	struct vm_map_entry *elm;
	VM_MAP_FOREACH(elm, &from->map_tree)
	{
		if (elm->inheritance == VM_INHERIT_NONE)
			continue;

		struct vm_map_entry *new_entry = alloc_map_entry();
		memset(new_entry, 0, sizeof(struct vm_map_entry));
		init_map_entry(new_entry, elm->offset, elm->base, elm->end,
			       elm->protection, elm->inheritance, elm->cache,
			       elm->type);
		// protection might allow less than max_protection
		new_entry->max_protection = elm->max_protection;

		switch (elm->type) {
		case VM_MAP_ENT_MMIO:
			new_entry->mmio_addr = elm->mmio_addr;
			break;
		case VM_MAP_ENT_OBJ:
			struct vm_object *obj = elm->object;
			assert(obj);
			vm_object_ref(obj);
			new_entry->object = obj;

			if (elm->inheritance == VM_INHERIT_COPY) {
				new_entry->amap = vm_amap_copy(elm->amap);
				if (elm->protection & VM_WRITE) {
					vm_prot_t cow_prot = elm->protection &
							     (~VM_WRITE);
					pmap_protect_range(&from->pmap,
							   elm->base,
							   elm->end - elm->base,
							   cow_prot, elm->cache,
							   0);
				}
			} else {
				assert(!elm->is_cow);
			}
			break;
		case VM_MAP_ENT_RESERVED:
			break;
		default:
			__builtin_unreachable();
		}

		RBT_INSERT(vm_map_rbtree, &to->map_tree, new_entry);
	}

	return YAK_SUCCESS;
}
