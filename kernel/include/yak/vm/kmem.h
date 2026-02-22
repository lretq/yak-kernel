#pragma once

#include <stddef.h>
#include <yak/vm/vmem.h>

#define KM_SLEEP 0x1
#define KM_NOSLEEP 0x2
#define KMC_QCACHE 0x4
#define KMC_NOMAGAZINE 0x8

typedef struct kmem_cache kmem_cache_t;

/* Creates a cache of objects, each of size size, aligned on an align boundary.
 * name identifies the cache for statistics and debugging.
 * constructor and destructor convert plain memory into objects and back again;
 * constructor may fail if it needs to allocate memory but can’t.
 * reclaim is a callback issued by the allocator when system−wide resources are running low (see §5.2).
 * private is a parameter passed to the constructor, destructor and reclaim callbacks
 * to support parameterized caches (e.g. a separate packet cache for each instance of a SCSI HBA driver). 
 * vmp is the vmem source that provides memory to create slabs (see §4 and §5.1). 
 * cflags indicates special cache properties. 
 * kmem_cache_create() returns an opaque pointer to the object cache (a.k.a. kmem cache). 
 */
kmem_cache_t *
kmem_cache_create(char *name, /* descriptive name for this cache */
		  size_t size, /* size of the objects it manages */
		  size_t align, /* minimum object alignment */
		  int (*constructor)(void *obj, void *private, int kmflag),
		  void (*destructor)(void *obj, void *private),
		  void (*reclaim)(void *private), /* memory reclaim callback */
		  void *private, /* argument to the above callbacks */
		  vmem_t *vmp, /* vmem source for slab creation */
		  int cflags); /* cache creation flags */

/* Destroys the cache and releases all associated resources. 
 * All allocated objects must have been freed. */
void kmem_cache_destroy(kmem_cache_t *cp);

/* Gets an object from the cache. The object will be in its constructed state.
 * kmflag is either KM_SLEEP or KM_NOSLEEP, indicating whether it’s acceptable to wait
 * for memory if none is currently available. */
void *kmem_cache_alloc(kmem_cache_t *cp, int kmflag);

/* Returns an object to the cache. The object must be in its constructed state. */
void kmem_cache_free(kmem_cache_t *cp, void *obj);
