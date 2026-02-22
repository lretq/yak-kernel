#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <yak/log.h>
#include <yak/cpudata.h>
#include <yak/cpu.h>
#include <yak/queue.h>
#include <yak/macro.h>
#include <yak/mutex.h>
#include <yak/vm/vmem.h>
#include <yak/vm/kmem.h>
#include <yak/arch-mm.h>

#define INTERNAL_NAME_MAX 32

#define KM_ALIGN 16
#define KM_SMALLSLAB_MAX (PAGE_SIZE / 8)
#define KM_PAGES_MAX 64

#define KM_IS_SMALL(cp) ((cp)->chunk_size <= KM_SMALLSLAB_MAX)

typedef LIST_HEAD(slablist, kmem_slab) slablist_t;
typedef SLIST_HEAD(buflist, kmem_bufctl) buflist_t;

typedef struct kmem_hashtable {
	size_t entry_count;

	size_t bucket_count;
	buflist_t *buckets;
} kmem_hashtable_t;

typedef struct kmem_magazine {
	SLIST_ENTRY(kmem_magazine) entry;
	void *mag_round[];
} kmem_magazine_t;

typedef SLIST_HEAD(kmem_mag_list, kmem_magazine) kmem_maglist_t;

typedef struct kmem_cpu_cache {
	struct kmutex lock;

	int magsize;

	kmem_magazine_t *loaded;
	int rounds;

	kmem_magazine_t *prev_loaded;
	int prev_rounds;
} kmem_cpu_cache_t;

#define KMF_NOMAGAZINE 0x1

typedef struct kmem_cache {
	struct kmutex mutex;

	unsigned int cache_flags;

	char name[INTERNAL_NAME_MAX];

	vmem_t *vmp;

	slablist_t slabs_full;
	slablist_t slabs_partial;
	slablist_t slabs_empty;

	kmem_hashtable_t hashtable;

	size_t align;

	// color = color_seq++ * align
	unsigned int color_max;
	unsigned int color_seq;

	size_t chunk_size;
	size_t max_chunks;
	size_t slab_size;

	int (*constructor)(void *obj, void *private, int kmflag);
	void (*destructor)(void *obj, void *private);
	void (*reclaim)(void *private);
	void *private;

	struct kmutex ml_lock;
	kmem_maglist_t ml_full;
	kmem_maglist_t ml_free;

	kmem_cpu_cache_t cpus[];
} kmem_cache_t;

typedef struct kmem_bufctl {
	SLIST_ENTRY(kmem_bufctl) freelist_entry;
} kmem_bufctl_t;

typedef struct kmem_slab {
	kmem_cache_t *parent;

	// one word
	buflist_t freelist;

	// two words
	LIST_ENTRY(kmem_slab) slablist_entry;

	// needed for reclaim
	void *large_base;

	unsigned int color;
	unsigned int free_count;
} kmem_slab_t;

typedef struct kmem_large_bufctl {
	kmem_bufctl_t bufctl;
	void *hashlist;
	void *base;
	kmem_slab_t *slab;
} kmem_large_bufctl_t;

#define SLAB_SIZE (ALIGN_UP(sizeof(kmem_slab_t), KM_ALIGN))

extern vmem_t vmem_internal_arena;
extern vmem_t *kmem_default_arena;

vmem_t *kmem_cache_arena;
vmem_t *kmem_hash_arena;

kmem_cache_t *kmem_slab_cache;
kmem_cache_t *kmem_bufctl_cache;

#define TMP_MAGSIZE 17
kmem_cache_t *tmp_singular_magazine_cache;

#define KM_MAX_LOAD 80

#include "murmur.h"
static inline size_t hash_index(const void *ptr, size_t count)
{
	return murmur64((uint64_t)ptr) & (count - 1);
}

static inline buflist_t *hash_list(const void *ptr, kmem_cache_t *cp)
{
	size_t idx = hash_index(ptr, cp->hashtable.bucket_count);
	return &cp->hashtable.buckets[idx];
}

static void hashtable_reinsert(buflist_t *new_buckets, size_t new_bucket_count,
			       kmem_large_bufctl_t *bp)
{
	size_t idx = hash_index(bp->base, new_bucket_count);
	buflist_t *list = &new_buckets[idx];
	SLIST_INSERT_HEAD(list, &bp->bufctl, freelist_entry);
	bp->hashlist = list;
}

static unsigned long load_factor(size_t count, size_t total)
{
	return (count * 1000UL) / total;
}

#define CP_LF(cp) \
	(load_factor((cp)->hashtable.entry_count, (cp)->hashtable.bucket_count))

static int hashtable_resize(kmem_cache_t *cp, size_t new_size)
{
	assert(P2CHECK(new_size));
	kmem_hashtable_t *tbl = &cp->hashtable;

	size_t old_size = tbl->bucket_count;

	if (old_size == new_size)
		return 1;

	buflist_t *new_buckets = vmem_alloc(
		kmem_hash_arena, new_size * sizeof(buflist_t), VM_SLEEP);

	if (!new_buckets)
		return -1;

	for (size_t i = 0; i < new_size; i++) {
		SLIST_INIT(&new_buckets[i]);
	}

	for (size_t i = 0; i < old_size; i++) {
		kmem_bufctl_t *elm;
		buflist_t *l = &tbl->buckets[i];

		while (!SLIST_EMPTY(l)) {
			elm = SLIST_FIRST(l);
			SLIST_REMOVE_HEAD(l, freelist_entry);

			hashtable_reinsert(new_buckets, new_size,
					   (kmem_large_bufctl_t *)elm);
		}
	}

	cp->hashtable.buckets = new_buckets;
	cp->hashtable.bucket_count = new_size;

	return 1;
}

static kmem_large_bufctl_t *hashtable_lookup(kmem_cache_t *cp, const void *base)
{
	buflist_t *list = hash_list(base, cp);

	kmem_bufctl_t *elm;
	SLIST_FOREACH(elm, list, freelist_entry)
	{
		kmem_large_bufctl_t *bp = (kmem_large_bufctl_t *)elm;
		if (bp->base == base)
			return bp;
	}
	return NULL;
}

static void hashtable_insert(kmem_cache_t *cp, kmem_large_bufctl_t *bp)
{
	if (cp->hashtable.bucket_count == 0) {
		hashtable_resize(cp, 8);
	} else if (CP_LF(cp) > 800) {
		hashtable_resize(cp, cp->hashtable.bucket_count * 2);
	}

	assert(bp->hashlist == NULL);
	buflist_t *list = hash_list(bp->base, cp);
	SLIST_INSERT_HEAD(hash_list(bp->base, cp), &bp->bufctl, freelist_entry);
	bp->hashlist = list;
	cp->hashtable.entry_count++;
}

static void hashtable_remove(kmem_cache_t *cp, kmem_large_bufctl_t *bp)
{
	assert(bp->hashlist == hash_list(bp->base, cp));
	SLIST_REMOVE(hash_list(bp->base, cp), &bp->bufctl, kmem_bufctl,
		     freelist_entry);
	bp->hashlist = NULL;
	cp->hashtable.entry_count--;

	if (cp->hashtable.bucket_count > 8 && CP_LF(cp) < 250) {
		size_t new_size =
			MAX(8, 1 << next_ilog2(cp->hashtable.entry_count * 2));

		if (new_size < cp->hashtable.bucket_count) {
			hashtable_resize(cp, new_size);
		}
	}
}

static void cache_enable_magazine(kmem_cache_t *cp)
{
	if (cp->cache_flags & KMF_NOMAGAZINE)
		return;

	for (size_t i = 0; i < cpus_total(); i++) {
		kmem_cpu_cache_t *ccp = &cp->cpus[i];
		guard(mutex)(&ccp->lock);
		ccp->magsize = TMP_MAGSIZE;
	}
}

kmem_cache_t *kmem_cache_create(char *name, size_t size, size_t align,
				int (*constructor)(void *obj, void *private,
						   int kmflag),
				void (*destructor)(void *obj, void *private),
				void (*reclaim)(void *private), void *private,
				vmem_t *vmp, int cflags)
{
	assert(size > 0);

	size_t cache_size =
		sizeof(kmem_cache_t) + sizeof(kmem_cpu_cache_t) * cpus_total();

	kmem_cache_t *cp = vmem_alloc(kmem_cache_arena, cache_size, VM_SLEEP);

	if (cp == NULL) {
		return NULL;
	}

	memset(cp, 0, cache_size);

	if (vmp == NULL) {
		vmp = kmem_default_arena;
	}

	if (align == 0) {
		align = KM_ALIGN;
	} else {
		align = MAX(align, (size_t)KM_ALIGN);
	}

	if (cflags & KMC_NOMAGAZINE)
		cp->cache_flags |= KMF_NOMAGAZINE;

	kmutex_init(&cp->mutex, "kmem_cache");

	strncpy(cp->name, name, INTERNAL_NAME_MAX - 1);
	cp->name[INTERNAL_NAME_MAX - 1] = '\0';

	if (vmp == NULL) {
		vmp = kmem_default_arena;
	}
	cp->vmp = vmp;

	LIST_INIT(&cp->slabs_full);
	LIST_INIT(&cp->slabs_partial);
	LIST_INIT(&cp->slabs_empty);

	cp->hashtable.bucket_count = 0;
	cp->hashtable.entry_count = 0;
	cp->hashtable.buckets = NULL;

	cp->align = align;

	cp->color_max = 0;
	cp->color_seq = 0;

	cp->chunk_size = 0;
	cp->max_chunks = 0;
	cp->slab_size = 0;

	cp->constructor = constructor;
	cp->destructor = destructor;
	cp->reclaim = reclaim;
	cp->private = private;

	kmutex_init(&cp->ml_lock, "ml_lock");
	SLIST_INIT(&cp->ml_free);
	SLIST_INIT(&cp->ml_full);

	for (size_t i = 0; i < cpus_total(); i++) {
		kmem_cpu_cache_t *ccp = &cp->cpus[i];
		kmutex_init(&ccp->lock, "ccp_lock");
		ccp->rounds = -1;
		ccp->prev_rounds = -1;
		ccp->magsize = 0;
	}

	size_t chunksize = ALIGN_UP(size, align);

	if (chunksize <= KM_SMALLSLAB_MAX) {
		pr_debug("init small slab (%ld)\n", chunksize);
		cp->chunk_size = chunksize;
		cp->max_chunks = (PAGE_SIZE - SLAB_SIZE) / chunksize;
		cp->slab_size = PAGE_SIZE;

		size_t unused_size = PAGE_SIZE - SLAB_SIZE -
				     (cp->chunk_size * cp->max_chunks);

		cp->color_max = unused_size / align;
	} else {
		cp->chunk_size = chunksize;

		//printf("init large slab\n");
		if (cflags & KMC_QCACHE) {
			// qcache_max is passed through private
			cp->slab_size = 1 << next_ilog2(3 * (uintptr_t)private);
			cp->private = NULL;
			cp->color_max = 0;

			//printf("qcache color max %d\n", cp->color_max);
		} else {
			size_t min_waste = -1, best;
			for (int i = 1; i < KM_PAGES_MAX; i++) {
				size_t slab_size = i * PAGE_SIZE;
				size_t max_chunks = slab_size / cp->chunk_size;

				if (max_chunks < 16)
					continue;

				size_t waste = slab_size -
					       (cp->chunk_size * max_chunks);
				if (waste < min_waste) {
					min_waste = waste;
					best = i;
				}
			}

			cp->slab_size = best * PAGE_SIZE;
		}

		cp->max_chunks = cp->slab_size / cp->chunk_size;
		size_t unused_space =
			cp->slab_size - (cp->chunk_size * cp->max_chunks);

		if ((cflags & KMC_QCACHE) == 0)
			cp->color_max = unused_space / align;

		//printf("chunksize: %lx\n", chunksize);
	}

	cache_enable_magazine(cp);

#if 0
	printf("max color: %d, chunksize: 0x%lx, max chunks: %ld, slabsize: 0x%lx, small: %d, align: %ld\n",
	       cp->color_max, cp->chunk_size, cp->max_chunks, cp->slab_size,
	       KM_IS_SMALL(cp), align);
#endif

	return cp;
}

static unsigned int next_color(kmem_cache_t *cp)
{
	unsigned int color = cp->color_seq++;
	if (color == cp->color_max) {
		cp->color_seq = 0;
	}
	return color;
}

static void slab_init(kmem_cache_t *parent, kmem_slab_t *sp, void *large_base)
{
	sp->parent = parent;
	SLIST_INIT(&sp->freelist);
	sp->large_base = large_base;
	sp->color = next_color(parent);
	//sp->color = 0;
	sp->free_count = 0;
}

static kmem_slab_t *create_small_slab(kmem_cache_t *cp)
{
	kmem_slab_t *sp;

	void *pg = vmem_alloc(cp->vmp, cp->slab_size, VM_SLEEP);
	assert(pg);

	sp = (kmem_slab_t *)((uintptr_t)pg + PAGE_SIZE - SLAB_SIZE);

	slab_init(cp, sp, NULL);

	for (size_t i = 0; i < cp->max_chunks; i++) {
		kmem_bufctl_t *bufctl =
			(void *)((uintptr_t)pg + cp->chunk_size * i +
				 sp->color * cp->align);

		assert((uintptr_t)bufctl < (uintptr_t)sp);

		SLIST_INSERT_HEAD(&sp->freelist, bufctl, freelist_entry);
		sp->free_count++;
	}

#if 0
	printf("Create small slab with color %u and %u chunks\n", sp->color,
	       sp->free_count);
#endif

	return sp;
}

static kmem_slab_t *create_large_slab(kmem_cache_t *cp)
{
	kmem_slab_t *sp = kmem_cache_alloc(kmem_slab_cache, KM_SLEEP);
	assert(sp);

	void *pg = vmem_alloc(cp->vmp, cp->slab_size, VM_SLEEP);
	if (!pg) {
		vmem_dump(cp->vmp);
		assert(pg);
		return NULL;
	}

	slab_init(cp, sp, pg);

	//printf("%s: init slab %p (color: %d)\n", cp->name, pg, sp->color);

	for (size_t i = 0; i < cp->max_chunks; i++) {
		kmem_large_bufctl_t *bp =
			kmem_cache_alloc(kmem_bufctl_cache, KM_SLEEP);
		//printf(" * large bufctl %p\n", bp);

		bp->hashlist = NULL;
		bp->base = (void *)((uintptr_t)pg + cp->chunk_size * i +
				    sp->color * cp->align);
		bp->slab = sp;
		SLIST_INSERT_HEAD(&sp->freelist, &bp->bufctl, freelist_entry);
		sp->free_count++;
	}

	return sp;
}

static kmem_slab_t *create_slab(kmem_cache_t *cp)
{
	if (KM_IS_SMALL(cp)) {
		return create_small_slab(cp);
	} else {
		return create_large_slab(cp);
	}
}

static void *slab_alloc(kmem_cache_t *cp, kmem_slab_t *sp,
			kmem_bufctl_t **bufctlp)
{
	assert(cp);
	assert(sp);
	assert(bufctlp);
	assert(sp->free_count > 0);

	kmem_bufctl_t *bufctl = SLIST_FIRST(&sp->freelist);
	//pr_debug("slab_alloc: %p\n", bufctl);

	SLIST_REMOVE_HEAD(&sp->freelist, freelist_entry);

	assert(sp->free_count > 0);
	size_t prev_count = sp->free_count--;

	if (prev_count == cp->max_chunks) {
		LIST_REMOVE(sp, slablist_entry);
		LIST_INSERT_HEAD(&cp->slabs_partial, sp, slablist_entry);
	} else if (prev_count == 1) {
		LIST_REMOVE(sp, slablist_entry);
		LIST_INSERT_HEAD(&cp->slabs_full, sp, slablist_entry);
	}

	*bufctlp = bufctl;

	if (KM_IS_SMALL(cp)) {
		return (void *)bufctl;
	} else {
		kmem_large_bufctl_t *lbp = (kmem_large_bufctl_t *)bufctl;
		assert(lbp->slab == sp);
		hashtable_insert(cp, lbp);
		return lbp->base;
	}
}

static void slab_free(kmem_cache_t *cp, kmem_slab_t *sp, kmem_bufctl_t *bp)
{
	assert(cp);
	assert(sp);
	assert(bp);
	assert(sp->free_count < cp->max_chunks);

	//printf("insert head: %p\n", bp);
	SLIST_INSERT_HEAD(&sp->freelist, bp, freelist_entry);

	size_t new_count = ++sp->free_count;

	if (new_count == cp->max_chunks) {
		LIST_REMOVE(sp, slablist_entry);
		LIST_INSERT_HEAD(&cp->slabs_empty, sp, slablist_entry);
	} else if (new_count == 1) {
		LIST_REMOVE(sp, slablist_entry);
		LIST_INSERT_HEAD(&cp->slabs_partial, sp, slablist_entry);
	}
}

static kmem_magazine_t *magazine_alloc(kmem_cache_t *cp, kmem_maglist_t *list)
{
	guard(mutex)(&cp->ml_lock);
	kmem_magazine_t *mag = SLIST_FIRST(list);
	if (mag) {
		SLIST_REMOVE_HEAD(list, entry);
	}
	return mag;
}

static void magazine_free(kmem_cache_t *cp, kmem_maglist_t *list,
			  kmem_magazine_t *mag)
{
	guard(mutex)(&cp->ml_lock);
	SLIST_INSERT_HEAD(list, mag, entry);
}

static void magazine_reload(kmem_cpu_cache_t *ccp, kmem_magazine_t *mag,
			    size_t rounds)
{
	ccp->prev_loaded = ccp->loaded;
	ccp->prev_rounds = ccp->rounds;

	ccp->loaded = mag;
	ccp->rounds = rounds;
}

void kmem_cache_free(kmem_cache_t *cp, void *obj)
{
	kmem_cpu_cache_t *ccp = &cp->cpus[curcpu().cpu_id];

	kmutex_acquire(&ccp->lock, TIMEOUT_INFINITE);

	for (;;) {
		if ((unsigned int)ccp->rounds < ccp->magsize) {
			kmem_magazine_t *mp = ccp->loaded;
			mp->mag_round[ccp->rounds++] = obj;
			kmutex_release(&ccp->lock);
			return;
		}

		if ((unsigned int)ccp->prev_rounds < ccp->magsize) {
			magazine_reload(ccp, ccp->prev_loaded,
					ccp->prev_rounds);
			continue;
		}

		if (ccp->magsize == 0)
			break;

		kmem_magazine_t *mag;

		mag = magazine_alloc(cp, &cp->ml_free);
		if (mag) {
			if (ccp->prev_loaded) {
				magazine_free(cp, &cp->ml_full,
					      ccp->prev_loaded);
			}
			magazine_reload(ccp, mag, 0);
			continue;
		}

		kmutex_release(&ccp->lock);
		mag = kmem_cache_alloc(tmp_singular_magazine_cache, 0);
		kmutex_acquire(&ccp->lock, TIMEOUT_INFINITE);

		if (mag) {
			// later: check if still the same magzine
			magazine_free(cp, &cp->ml_free, mag);
			continue;
		}

		break;
	}

	kmutex_release(&ccp->lock);

	kmutex_acquire(&cp->mutex, TIMEOUT_INFINITE);

	if (cp->destructor != NULL) {
		cp->destructor(obj, cp->private);
	}

	kmem_slab_t *sp;
	kmem_bufctl_t *bp;

	if (KM_IS_SMALL(cp)) {
		sp = (kmem_slab_t *)(ALIGN_UP((uintptr_t)obj + 1, PAGE_SIZE) -
				     SLAB_SIZE);

		bp = obj;
	} else {
		kmem_large_bufctl_t *lbp;
		lbp = hashtable_lookup(cp, obj);
		assert(lbp);
		hashtable_remove(cp, lbp);

		sp = lbp->slab;
		bp = &lbp->bufctl;
	}

	assert(sp);
	assert(bp);

	slab_free(cp, sp, bp);

	kmutex_release(&cp->mutex);
}

void *kmem_cache_alloc(kmem_cache_t *cp, int kmflag)
{
	kmem_cpu_cache_t *ccp = &cp->cpus[curcpu().cpu_id];
	kmutex_acquire(&ccp->lock, TIMEOUT_INFINITE);

	for (;;) {
		if (ccp->rounds > 0) {
			kmem_magazine_t *mp = ccp->loaded;
			void *obj = mp->mag_round[--ccp->rounds];
			kmutex_release(&ccp->lock);
			return obj;
		}

		if (ccp->prev_rounds > 0) {
			magazine_reload(ccp, ccp->prev_loaded,
					ccp->prev_rounds);
			continue;
		}

		if (ccp->magsize == 0)
			break;

		kmem_magazine_t *mag = magazine_alloc(cp, &cp->ml_full);
		if (mag) {
			if (ccp->prev_loaded) {
				magazine_free(cp, &cp->ml_free,
					      ccp->prev_loaded);
			}
			magazine_reload(ccp, mag, ccp->magsize);
			continue;
		}

		break;
	}

	kmutex_release(&ccp->lock);

	// fall through to the slab layer

	kmutex_acquire(&cp->mutex, TIMEOUT_INFINITE);

	kmem_slab_t *sp;

	if (!LIST_EMPTY(&cp->slabs_partial)) {
		sp = LIST_FIRST(&cp->slabs_partial);
	} else if (!LIST_EMPTY(&cp->slabs_empty)) {
		sp = LIST_FIRST(&cp->slabs_empty);
	} else {
		sp = create_slab(cp);
		LIST_INSERT_HEAD(&cp->slabs_empty, sp, slablist_entry);
	}

	kmem_bufctl_t *bufctl;
	void *addr = slab_alloc(cp, sp, &bufctl);

	if (cp->constructor != NULL) {
		int rv = cp->constructor(addr, cp->private, kmflag);
		if (rv < 0) {
			assert(!"handle free\n");
		}
	}

	kmutex_release(&cp->mutex);
	return addr;
}

void kmem_init()
{
	kmem_cache_arena = vmem_init(NULL, "kmem_cache", NULL, 0, KM_ALIGN,
				     vmem_alloc, vmem_free,
				     &vmem_internal_arena, 0, VM_SLEEP);

	kmem_hash_arena = vmem_init(NULL, "kmem_hash", NULL, 0, KM_ALIGN,
				    vmem_alloc, vmem_free, &vmem_internal_arena,
				    0, VM_SLEEP);

	tmp_singular_magazine_cache = kmem_cache_create(
		"kmem_magazine",
		sizeof(kmem_magazine_t) + sizeof(void *) * TMP_MAGSIZE, 64,
		NULL, NULL, NULL, NULL, &vmem_internal_arena,
		KMC_NOMAGAZINE | VM_SLEEP);

	kmem_slab_cache = kmem_cache_create("kmem_slab", sizeof(kmem_slab_t), 0,
					    NULL, NULL, NULL, NULL,
					    &vmem_internal_arena, VM_SLEEP);

	kmem_bufctl_cache = kmem_cache_create("kmem_large_bufctl",
					      sizeof(kmem_large_bufctl_t), 0,
					      NULL, NULL, NULL, NULL,
					      &vmem_internal_arena, VM_SLEEP);
}
