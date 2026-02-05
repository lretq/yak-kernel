#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <yak/vm/pmap.h>
#include <yak/rwlock.h>
#include <yak/status.h>
#include <yak/tree.h>
#include <yak/vmflags.h>
#include <yak/types.h>
#include <yak/mutex.h>

struct vm_object;
struct page;

enum {
	VM_MAP_ENT_MMIO = 1,
	VM_MAP_ENT_OBJ,
	VM_MAP_ENT_RESERVED, /* basically like PROT_NONE */
};

struct vm_map_entry {
	vaddr_t base; /*! start address */
	vaddr_t end; /*! end address exclusive */

	unsigned short type; /*! mapping type;
				 valid are mmio, obj and reserved */

	union {
		paddr_t mmio_addr; /*! backing physical device memory */
		struct vm_object *object; /*! backing object */
	};
	voff_t offset; /*! offset into backing store */

	bool is_cow;

	struct vm_amap *amap; /*! reference to the amap */

	vm_prot_t protection; /*! current protection */
	vm_prot_t max_protection; /*! maximum protection */

	vm_inheritance_t inheritance; /*! inheritance mode */

	vm_cache_t cache; /*! cache mode */

	RBT_ENTRY(struct vm_map_entry) tree_entry;
};

typedef RBT_HEAD(vm_map_rbtree, struct vm_map_entry) vm_map_tree_t;

#define VM_MAP_FOREACH(e, head) RBT_FOREACH(e, vm_map_rbtree, (head))

struct vm_map {
	struct rwlock map_lock;

	vm_map_tree_t map_tree;

	struct pmap pmap;
};

/// @brief Retrieve the global kernel VM map
struct vm_map *kmap();

/*!
 * @brief Initialize a VM map
 * 
 * @param map Target VM map
 * 
 * @retval YAK_SUCCESS on success
 */
status_t vm_map_init(struct vm_map *map);

void vm_map_destroy(struct vm_map *map);

/*!
 * @brief Allocate virtual address from the map arena
 *
 * @param map Target VM map
 *
 * @param length Space to reserve in bytes
 *
 * @param[out] out Receives the allocated virtual address
 *
 * @retval YAK_SUCCESS on success
 */
status_t vm_map_alloc(struct vm_map *map, size_t length, vaddr_t *out);

/*!
 * @brief Setup a MMIO mapping
 *
 * Essentially, a lazy version of @pmap_map_range, that handles mapping
 * physical addresses which may not be page-aligned
 *
 * @param map Target VM map
 * @param device_addr Physical address (may be unaligned)
 * @param length Length of the mapping in bytes 
 * @param prot Memory protection flags
 * @param cache Cache behaviour (Arch-defined; default is always VM_CACHE_DEFAULT)
 * @param[out] out On success, receives the vaddr of the mapped region
 */
status_t vm_map_mmio(struct vm_map *map, paddr_t device_addr, size_t length,
		     vm_prot_t prot, vm_cache_t cache, vaddr_t *out);

/**
 * @brief Remove a MMIO mapping, also removes physical memory mapping
 *
 * @param map Target VM map
 * @param va mapped address, offset does NOT need to be removed
 */
status_t vm_unmap_mmio(struct vm_map *map, vaddr_t va);

/*!
 * @brief Map a VM object or a zero-fill anon space
 *
 * @param map Target VM map
 *
 * @param obj Nullable pointer to a VM object (Purely anonymous mapping if NULL)
 *
 * @param length Length of the mapping in bytes
 *
 * @param offset Offset into the object in bytes
 *
 * @param map_exact If set, try to use address in out or fail
 *
 * @param initial_prot Memory protection flags
 *
 * @param inheritance Mapping inheritance on map clone
 *
 * @param[in,out] out May contain a hint for address allocation; 
 *                If map_exact is set, must contain a valid address. 
 *                On return, receives the address of the mapped region
 *
 * @retval YAK_SUCCESS on success
 */
status_t vm_map(struct vm_map *map, struct vm_object *obj, size_t length,
		voff_t offset, vm_prot_t prot, vm_inheritance_t inheritance,
		vm_cache_t cache, vaddr_t hint, int flags, vaddr_t *out);

/*!
 * @brief Remove any (page-aligned) VM mapping
 *
 * @param map Target VM map
 * @param va Mapping address
 * @param length Length to unmap
 */
status_t vm_unmap(struct vm_map *map, uintptr_t va, size_t length, int flags);

status_t vm_protect(struct vm_map *map, vaddr_t va, size_t length,
		    vm_prot_t prot, int flags);

status_t vm_map_reserve(struct vm_map *map, vaddr_t hint, size_t length,
			int flags, vaddr_t *out);

void vm_map_activate(struct vm_map *map);

status_t vm_map_fork(struct vm_map *from, struct vm_map *to);

// i.e. for use during ELF loading from kernel thread -> user process
// -> per thread vm context override
struct vm_map *vm_map_tmp_switch(struct vm_map *map);
void vm_map_tmp_disable(struct vm_map *map);

struct vm_map_entry *vm_map_lookup_entry_locked(struct vm_map *map,
						vaddr_t address);

#ifdef CONFIG_DEBUG
void vm_map_dump(struct vm_map *map);
#endif

#ifdef __cplusplus
}
#endif
