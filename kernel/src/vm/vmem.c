#define pr_fmt(fmt) "vmem: " fmt

#include "yak/init.h"
#include "yak/types.h"
#include "yak/vm/map.h"
#include "yak/vm/pmm.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <yak/vm/pmap.h>
#include <yak/vm/kmem.h>
#include <yak/queue.h>
#include <yak/arch-mm.h>
#include <yak/vm/vmem.h>
#include <yak/macro.h>
#include <yak/mutex.h>
#include <yak/log.h>

#define INTERNAL_NAME_MAX 32

#define STATIC_BT_COUNT 128
#define BT_MINRESERVED 4

#define HASH_BUCKET_COUNT 16
#define FREELIST_COUNT (sizeof(int *) * CHAR_BIT)

#define VMEM_QCACHE_MAX 16

typedef LIST_HEAD(vmem_taglist, vmem_tag) vmem_taglist_t;
typedef TAILQ_HEAD(vmem_tagqueue, vmem_tag) vmem_tagqueue_t;

typedef struct vmem {
	struct kmutex mutex;

	char name[INTERNAL_NAME_MAX];

	size_t quantum;
	size_t qcache_max;

	void *(*afunc)(vmem_t *, size_t, int);
	void (*ffunc)(vmem_t *, void *, size_t);
	vmem_t *source;

	vmem_taglist_t freetags;
	size_t n_freetags;

	kmem_cache_t *qcaches[VMEM_QCACHE_MAX];

	vmem_tagqueue_t all_tags;
	vmem_taglist_t freelists[FREELIST_COUNT];
	vmem_taglist_t hashlists[HASH_BUCKET_COUNT];
	vmem_taglist_t spanlist;
} vmem_t;

typedef enum vmem_tag_type {
	VMEM_TAG_NULL,
	VMEM_TAG_SPAN,
	VMEM_TAG_SPAN_IMPORTED,
	VMEM_TAG_FREE,
	VMEM_TAG_ALLOCATED,
} vmem_tag_type_t;

typedef struct vmem_tag {
	vmem_tag_type_t type : 4;
	bool is_static : 1;

	void *start;
	size_t size;

	LIST_ENTRY(vmem_tag) taglist;
	TAILQ_ENTRY(vmem_tag) tagqueue;
} vmem_tag_t;

vmem_t heap_arena;

vmem_t vmem_internal_arena;
vmem_t vmem_vmem_arena;

vmem_t *kmem_va_arena;
vmem_t *kmem_default_arena;

static vmem_tag_t static_tags[STATIC_BT_COUNT];
static int static_tag_count = STATIC_BT_COUNT;

static struct kmutex vmem_refill_lock;
static struct kmutex vmem_tag_lock;
static LIST_HEAD(, vmem_tag) vmem_tag_list;
static size_t vmem_tag_list_count;

static const char *vmem_seg_type_str[] = {
	[VMEM_TAG_FREE] = " free",
	[VMEM_TAG_ALLOCATED] = "alloc",
	[VMEM_TAG_SPAN] = " span",
	[VMEM_TAG_SPAN_IMPORTED] = "spani",
};

void vmem_dump(const vmem_t *vmem)
{
	if (vmem->source)
		vmem_dump(vmem->source);

	vmem_tag_t *span;

	pr_debug("VMem arena <%s> segment queue:\n", vmem->name);
	TAILQ_FOREACH(span, &vmem->all_tags, tagqueue)
	{
		pr_debug("[%s:0x%zx-0x%zx]\n", vmem_seg_type_str[span->type],
			 (uintptr_t)span->start,
			 (uintptr_t)span->start + span->size);
	}
}

void *kmem_alloc(vmem_t *vmp, size_t size, int vmflag)
{
	void *addr = vmem_alloc(vmp, size, vmflag);
	if (addr == NULL)
		return NULL;

	for (size_t i = 0; i < size; i += PAGE_SIZE) {
		struct page *pg = pmm_alloc_order(0);
		pmap_map(&kmap()->pmap, (vaddr_t)addr + i, page_to_addr(pg), 0,
			 VM_RW, VM_CACHE_DEFAULT);
	}

	//printf("kmem_alloc: %p\n", addr);
	memset(addr, 0xAA, size);

	return addr;
}

void kmem_free(vmem_t *vmp, void *addr, size_t size)
{
	assert(addr);
	assert(size > 0);

	pmap_unmap_range_and_free(&kmap()->pmap, (vaddr_t)addr, size, 0);

	vmem_free(vmp, addr, size);
}

void *vm_kalloc(size_t size, [[maybe_unused]] int vmflag)
{
	void *addr = kmem_alloc(&heap_arena, size, vmflag);
	if (addr == NULL)
		return NULL;

	return addr;
}

void vm_kfree(void *addr, size_t size)
{
	kmem_free(&heap_arena, addr, size);
}

static void tag_static_init()
{
	while (static_tag_count-- > 0) {
		vmem_tag_t *tag = &static_tags[static_tag_count];
		tag->is_static = true;

		LIST_INSERT_HEAD(&vmem_tag_list, tag, taglist);
	}
}

static int tag_refill(vmem_t *vmp)
{
	vmem_tag_t *tag;

retry:
	if (vmp->n_freetags > BT_MINRESERVED) {
		return 0;
	}

	kmutex_acquire(&vmem_tag_lock, TIMEOUT_INFINITE);
	while (!LIST_EMPTY(&vmem_tag_list) &&
	       vmp->n_freetags <= BT_MINRESERVED) {
		tag = LIST_FIRST(&vmem_tag_list);
		assert(tag);
		LIST_REMOVE(tag, taglist);

		LIST_INSERT_HEAD(&vmp->freetags, tag, taglist);
		vmp->n_freetags++;

		vmem_tag_list_count--;
	}
	kmutex_release(&vmem_tag_lock);

	while (LIST_EMPTY(&vmem_tag_list)) {
		kmutex_release(&vmp->mutex);

		kmutex_acquire(&vmem_refill_lock, TIMEOUT_INFINITE);

		void *addr = vm_kalloc(PAGE_SIZE, VM_SLEEP | VM_POPULATE);

		vmem_tag_t *block = addr;
		memset(block, 0, PAGE_SIZE);

		kmutex_acquire(&vmem_tag_lock, TIMEOUT_INFINITE);
		for (size_t i = 0; i < (PAGE_SIZE / sizeof(vmem_tag_t)); i++) {
			tag = &block[i];
			LIST_INSERT_HEAD(&vmem_tag_list, tag, taglist);
			vmem_tag_list_count++;
		}
		kmutex_release(&vmem_tag_lock);

		kmutex_release(&vmem_refill_lock);

		kmutex_acquire(&vmp->mutex, TIMEOUT_INFINITE);
	}

	goto retry;
}

static vmem_tag_t *tag_alloc(vmem_t *vmp, int vmflag)
{
	vmem_tag_t *tag;

	while (vmp->n_freetags <= BT_MINRESERVED &&
	       (vmflag & VM_POPULATE) == 0) {
		tag_refill(vmp);
	}

	tag = LIST_FIRST(&vmp->freetags);
	LIST_REMOVE(tag, taglist);
	vmp->n_freetags--;

	return tag;
}

static void tag_free(vmem_t *vmp, vmem_tag_t *tag)
{
	LIST_INSERT_HEAD(&vmp->freetags, tag, taglist);
	vmp->n_freetags++;
}

static inline size_t freelist_idx(size_t size)
{
	// get the freelist which contains
	// [2^n, 2^n+1)
	//
	// example for 4096 on 64bit
	// 64 - 51 = 13
	// 1<<13 = 8192 -> therefor - 1
	return FREELIST_COUNT - __builtin_clzl(size) - 1;
}

static vmem_taglist_t *freelist_list(vmem_t *vmp, size_t size)
{
	return &vmp->freelists[freelist_idx(size) - 1];
}

static void insert_free(vmem_t *vmp, vmem_tag_t *tag)
{
	LIST_INSERT_HEAD(freelist_list(vmp, tag->size), tag, taglist);
}

#include "murmur.h"
static inline size_t hash_index(const void *ptr)
{
	return murmur64((uint64_t)ptr) & (HASH_BUCKET_COUNT - 1);
}

static void insert_alloc(vmem_t *vmp, vmem_tag_t *tag)
{
	assert(tag->type == VMEM_TAG_ALLOCATED);

	size_t idx = hash_index(tag->start);
#if 0
	printf("insert %p -> %lx\n", tag->start, idx);
#endif

	vmem_taglist_t *list = &vmp->hashlists[idx];

	LIST_INSERT_HEAD(list, tag, taglist);
}

static vmem_tag_t *lookup_alloc(vmem_t *vmp, void *addr, size_t size)
{
	size_t idx = hash_index(addr);
#if 0
	printf("lookup %p -> %lx\n", addr, idx);
#endif

	vmem_taglist_t *list = &vmp->hashlists[idx];

	vmem_tag_t *tag;
	LIST_FOREACH(tag, list, taglist)
	{
		assert(tag->type == VMEM_TAG_ALLOCATED);
		if (tag->start == addr) {
			assert(tag->size == size);
			return tag;
		}
	}

	return NULL;
}

static void *vmem_add_locked(vmem_t *vmp, void *addr, size_t size,
			     vmem_tag_type_t span_type, int vmflag)
{
	(void)vmflag;

	vmem_tag_t *span_tag, *free_tag, *tmp, *after;

	span_tag = tag_alloc(vmp, vmflag);
	if (span_tag == NULL) {
		return NULL;
	}

	free_tag = tag_alloc(vmp, vmflag);
	if (free_tag == NULL) {
		tag_free(vmp, span_tag);
		return NULL;
	}

	after = NULL;

	if (span_type != VMEM_TAG_SPAN_IMPORTED) {
		// insert spans sorted by address
		// this happens after allocating all tags as tag refills may release and re-acquire the vmp mutex
		LIST_FOREACH(tmp, &vmp->spanlist, taglist)
		{
			if (tmp->start > addr) {
				break;
			}
			after = tmp;
		}
	}

	span_tag->start = addr;
	span_tag->size = size;
	span_tag->type = span_type;

	free_tag->start = addr;
	free_tag->size = size;
	free_tag->type = VMEM_TAG_FREE;

	if (after != NULL) {
		vmem_tag_t *next = LIST_NEXT(after, taglist);

		LIST_INSERT_AFTER(after, span_tag, taglist);

		if (next != NULL) {
			TAILQ_INSERT_BEFORE(next, span_tag, tagqueue);
		} else {
			TAILQ_INSERT_TAIL(&vmp->all_tags, span_tag, tagqueue);
		}
	} else {
		LIST_INSERT_HEAD(&vmp->spanlist, span_tag, taglist);
		TAILQ_INSERT_HEAD(&vmp->all_tags, span_tag, tagqueue);
	}

	TAILQ_INSERT_AFTER(&vmp->all_tags, span_tag, free_tag, tagqueue);

	insert_free(vmp, free_tag);

	return addr;
}

vmem_t *vmem_init(vmem_t *vmp, char *name, void *base, size_t size,
		  size_t quantum, void *(*afunc)(vmem_t *, size_t, int),
		  void (*ffunc)(vmem_t *, void *, size_t), vmem_t *source,
		  size_t qcache_max, int vmflag)
{
	if (vmp == NULL) {
		vmp = vmem_alloc(&vmem_vmem_arena, sizeof(*vmp), VM_SLEEP);
		assert(vmp);
	}

	if (vmp == NULL) {
		return NULL;
	}

	assert(vmp);
	assert(quantum != 0);
	assert((qcache_max % quantum) == 0);
	assert(P2CHECK(quantum));
	assert(base == NULL || source == NULL);

	kmutex_init(&vmp->mutex, "vmem");

	strncpy(vmp->name, name, INTERNAL_NAME_MAX - 1);
	vmp->name[INTERNAL_NAME_MAX - 1] = '\0';

	vmp->quantum = quantum;
	vmp->qcache_max = qcache_max;

	vmp->afunc = afunc;
	vmp->ffunc = ffunc;
	vmp->source = source;

	LIST_INIT(&vmp->freetags);
	vmp->n_freetags = 0;

	TAILQ_INIT(&vmp->all_tags);

	for (size_t i = 0; i < FREELIST_COUNT; i++) {
		LIST_INIT(&vmp->freelists[i]);
	}

	for (size_t i = 0; i < HASH_BUCKET_COUNT; i++) {
		LIST_INIT(&vmp->hashlists[i]);
	}

	LIST_INIT(&vmp->spanlist);

	if (base != NULL && size != 0)
		vmem_add_locked(vmp, base, size, VMEM_TAG_SPAN, vmflag);

	if (qcache_max > 0) {
		qcache_max = MIN(qcache_max, quantum * VMEM_QCACHE_MAX);

		for (size_t i = 0; i < VMEM_QCACHE_MAX; i++) {
			vmp->qcaches[i] = NULL;
		}

		for (size_t i = 0; i < qcache_max / quantum; i++) {
			vmp->qcaches[i] = kmem_cache_create(
				"qcache", quantum + quantum * i, quantum, NULL,
				NULL, NULL, (void *)qcache_max, vmp,
				KMC_QCACHE);
		}
	}

	return vmp;
}

void vmem_destroy(vmem_t *vmp)
{
	(void)vmp;
	pr_warn("implement destroying vmem_t\n");
}

void *vmem_alloc(vmem_t *vmp, size_t size, int vmflag)
{
	assert(vmp);
	assert(size != 0);
	assert(!(vmflag & VM_EXACT));

	if (size <= vmp->qcache_max) {
		int kmflag = vmflag & (VM_SLEEP | VM_NOSLEEP);
		void *addr = kmem_cache_alloc(
			vmp->qcaches[size / vmp->quantum - 1], kmflag);
		assert(addr);
		return addr;
	}

	return vmem_xalloc(vmp, size, 0, 0, 0, NULL, NULL, vmflag);
}

static void split_seg(vmem_t *vmp, vmem_tag_t *tag, void *at_addr, size_t size,
		      vmem_tag_t *left, vmem_tag_t *right)
{
	assert(tag->type == VMEM_TAG_FREE);
	LIST_REMOVE(tag, taglist);

	uintptr_t at = (uintptr_t)at_addr;
	uintptr_t start = (uintptr_t)tag->start;

	if (at > start) {
		left->type = VMEM_TAG_FREE;
		left->start = tag->start;
		left->size = at - start;
		TAILQ_INSERT_BEFORE(tag, left, tagqueue);
		insert_free(vmp, left);
	} else {
		tag_free(vmp, left);
	}

	if (at + size < start + tag->size) {
		right->type = VMEM_TAG_FREE;
		right->start = (void *)(at + size);
		right->size = (start + tag->size) - (at + size);
		TAILQ_INSERT_AFTER(&vmp->all_tags, tag, right, tagqueue);
		insert_free(vmp, right);
	} else {
		tag_free(vmp, right);
	}

	tag->type = VMEM_TAG_ALLOCATED;
	tag->start = at_addr;
	tag->size = size;

	insert_alloc(vmp, tag);
}

// note to self: instant fit freelist can be expressed like this in python
// (64 - (64 - (n-1).bit_length()) - 1 ) + 1
void *vmem_xalloc(vmem_t *vmp, size_t size, size_t align, size_t phase,
		  size_t nocross, void *minaddr, void *maxaddr, int vmflag)
{
	kmutex_acquire(&vmp->mutex, TIMEOUT_INFINITE);

	bool tried_import = false;

	assert(align == 0);
	assert(phase == 0);
	assert(nocross == 0);

	assert(size != 0);

	if (!(vmflag & (VM_INSTANTFIT | VM_BESTFIT))) {
		vmflag |= VM_INSTANTFIT;
	}

	vmem_tag_t *left, *right;
	left = tag_alloc(vmp, vmflag);
	if (!left) {
		kmutex_release(&vmp->mutex);
		return NULL;
	}
	right = tag_alloc(vmp, vmflag);
	if (!right) {
		kmutex_release(&vmp->mutex);
		tag_free(vmp, left);
		return NULL;
	}

	vmem_taglist_t *first_list = freelist_list(vmp, size),
		       *end_list = &vmp->freelists[FREELIST_COUNT], *list;

	vmem_tag_t *best = NULL;

	for (;;) {
		void *alloc_start = NULL;

		for (list = first_list; list < end_list; list++) {
			if (best != NULL)
				goto found;

			if (LIST_EMPTY(list))
				continue;

			vmem_tag_t *elm;
			LIST_FOREACH(elm, list, taglist)
			{
				void *seg_start = elm->start;
				void *seg_end = (void *)((uintptr_t)elm->start +
							 elm->size);

				if (minaddr && seg_end <= minaddr)
					continue;
				if (maxaddr && seg_start >= maxaddr)
					continue;

				void *cur_start = seg_start;
				size_t cur_size = elm->size;

				if (minaddr && cur_start < minaddr) {
					size_t diff = (uintptr_t)minaddr -
						      (uintptr_t)cur_start;
					cur_start = minaddr;
					cur_size -= diff;
				}

				if (maxaddr && (uintptr_t)cur_start + cur_size >
						       (uintptr_t)maxaddr) {
					cur_size = (uintptr_t)maxaddr -
						   (uintptr_t)cur_start;
				}

				if (cur_size < size)
					continue;

				// -> suitable block found

				if (vmflag & VM_INSTANTFIT) {
					best = elm;
					alloc_start = cur_start;
					goto found;
				} else if (vmflag & VM_BESTFIT) {
					if (!best || elm->size < best->size) {
						best = elm;
						alloc_start = cur_start;
					}
				}
			}
		}

		if (best == NULL) {
			if (tried_import || vmp->source == NULL) {
				kmutex_release(&vmp->mutex);
				return NULL;
			}

			assert(minaddr == NULL);
			assert(maxaddr == NULL);

			tried_import = true;

			size_t import_size = ALIGN_UP(
				size, MAX(vmp->source->quantum, align));

			//printf("import_size: %lx\n", import_size);

			void *rv = vmp->afunc(vmp->source, import_size, vmflag);
			if (rv == NULL) {
				kmutex_release(&vmp->mutex);
				return NULL;
			}

			rv = vmem_add_locked(vmp, rv, import_size,
					     VMEM_TAG_SPAN_IMPORTED, vmflag);

			if (rv == NULL) {
				kmutex_release(&vmp->mutex);
				return NULL;
			}

			continue;
		}

found:
		assert(best);
		assert(best->size >= size);
		assert(!minaddr || alloc_start >= minaddr);
		assert(!maxaddr ||
		       (uintptr_t)alloc_start + size <= (uintptr_t)maxaddr);

		LIST_REMOVE(best, taglist);
		split_seg(vmp, best, alloc_start, size, left, right);

		kmutex_release(&vmp->mutex);
		return alloc_start;
	}
}

void vmem_free(vmem_t *vmp, void *addr, size_t size)
{
	assert(addr);
	assert(size != 0);

	if (size <= vmp->qcache_max) {
		return kmem_cache_free(vmp->qcaches[size / vmp->quantum - 1],
				       addr);
	}

	vmem_xfree(vmp, addr, size);
}

void vmem_xfree(vmem_t *vmp, void *addr, size_t size)
{
	kmutex_acquire(&vmp->mutex, TIMEOUT_INFINITE);

	assert(addr != NULL);
	assert(size != 0);

	vmem_tag_t *tag = lookup_alloc(vmp, addr, size);
	assert(tag);
	assert(tag->size == size);

	// remove from hashtable
	LIST_REMOVE(tag, taglist);

	tag->type = VMEM_TAG_FREE;

	vmem_tag_t *left = TAILQ_PREV(tag, vmem_tagqueue, tagqueue);
	// left is always either SPAN or SPAN_IMPORTED
	if (left->type == VMEM_TAG_FREE) {
		TAILQ_REMOVE(&vmp->all_tags, tag, tagqueue);

		LIST_REMOVE(left, taglist);
		left->size += tag->size;

		tag_free(vmp, tag);
		tag = left;

		left = TAILQ_PREV(tag, vmem_tagqueue, tagqueue);
	}

	vmem_tag_t *right = TAILQ_NEXT(tag, tagqueue);
	if (right && right->type == VMEM_TAG_FREE) {
		// atp tag may be left or tag, but it always is on the tailq
		TAILQ_REMOVE(&vmp->all_tags, tag, tagqueue);

		LIST_REMOVE(right, taglist);
		right->start = tag->start;
		right->size += tag->size;

		tag_free(vmp, tag);
		tag = right;

		right = TAILQ_NEXT(tag, tagqueue);
	}

	if (left->type == VMEM_TAG_SPAN_IMPORTED && tag->size == left->size) {
		assert(!right || right->type == VMEM_TAG_SPAN_IMPORTED ||
		       right->type == VMEM_TAG_SPAN);

		vmp->ffunc(vmp->source, left->start, left->size);

		TAILQ_REMOVE(&vmp->all_tags, tag, tagqueue);
		tag_free(vmp, tag);

		LIST_REMOVE(left, taglist);
		TAILQ_REMOVE(&vmp->all_tags, left, tagqueue);
		tag_free(vmp, left);
	} else {
		insert_free(vmp, tag);
	}

	kmutex_release(&vmp->mutex);
}

void *vmem_add(vmem_t *vmp, void *addr, size_t size, int vmflag)
{
	kmutex_acquire(&vmp->mutex, TIMEOUT_INFINITE);
	void *rv = vmem_add_locked(vmp, addr, size, VMEM_TAG_SPAN, vmflag);
	kmutex_release(&vmp->mutex);
	return rv;
}

#if 0
void *testfn([[maybe_unused]] void *arg)
{
	size_t heap_length = PAGE_SIZE * 2000;
	void *heap_base = mmap(NULL, heap_length, PROT_NONE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (heap_base == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	vmem_add(&heap_arena, heap_base, heap_length, VM_SLEEP);

	void *arr[10] = { 0 };

	for (int i = 0; i < 10; i++) {
		arr[i] = vmem_alloc(&heap_arena, PAGE_SIZE, VM_SLEEP);
	}

	for (int i = 0; i < 10; i++) {
		vmem_free(&heap_arena, arr[i], PAGE_SIZE);
	}

	pthread_exit(NULL);
}
#endif

extern void kmem_init();

void vmem_earlyinit()
{
	kmutex_init(&vmem_refill_lock, "vmem_refill_lock");
	kmutex_init(&vmem_tag_lock, "vmem_tag_lock");

	LIST_INIT(&vmem_tag_list);
	vmem_tag_list_count = 0;

	tag_static_init();

	vmem_init(&heap_arena, "heap", (void *)KERNEL_HEAP_BASE,
		  KERNEL_HEAP_LENGTH, PAGE_SIZE, NULL, NULL, NULL, 0, VM_SLEEP);

	vmem_init(&vmem_internal_arena, "vmem_internal", NULL, 0, PAGE_SIZE,
		  kmem_alloc, kmem_free, &heap_arena, 0, VM_SLEEP);

	vmem_init(&vmem_vmem_arena, "vmem_vmem", NULL, 0, _Alignof(vmem_t),
		  vmem_alloc, vmem_free, &vmem_internal_arena, 0, VM_SLEEP);

	kmem_init();

	kmem_va_arena = vmem_init(NULL, "kmem_va", NULL, 0, PAGE_SIZE,
				  vmem_alloc, vmem_free, &heap_arena,
				  PAGE_SIZE * 8, VM_SLEEP);

	kmem_default_arena = vmem_init(NULL, "kmem_default", NULL, 0, PAGE_SIZE,
				       kmem_alloc, kmem_free, kmem_va_arena, 0,
				       VM_SLEEP);
}

INIT_ENTAILS(vmem_node, heap_ready);
INIT_DEPS(vmem_node, pmm_node);
INIT_NODE(vmem_node, vmem_earlyinit);
