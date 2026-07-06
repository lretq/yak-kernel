#define pr_fmt(fmt) "slab: " fmt

#include "frg/mutex.hpp"
#include "yak/log.h"
#include "yak/math.h"
#include "yak/panic.h"
#include "yak/util.h"
#include "yak/vm/address.h"
#include "yak/vm/flags.h"
#include "yak/vm/map.h"
#include "yak/vm/page.h"
#include "yak/vm/pmm.h"
#include <algorithm>
#include <cstddef>
#include <frg/manual_box.hpp>
#include <yak/arch-mm.h>
#include <yak/arch-page.h>
#include <yak/assert.h>
#include <yak/bump-allocator.h>
#include <yak/vm/generic_slab.h>

namespace yak {
static frg::manual_box<BumpAllocator> bootstrap_kheap;
static bool heap_bootstrapped = false;

struct HeapInfo;
static HeapInfo *heapdb;

struct HeapInfo {
  Page *slab_head;

  static HeapInfo *lookup(vaddr_t heap_va) {
    heap_va = align_down(heap_va, arch::PAGE_SIZE);
    auto idx = (heap_va - arch::KERNEL_HEAP_BASE) >> arch::PAGE_SHIFT;
    return &heapdb[idx];
  }

  static Page *lookup_slab(vaddr_t heap_va) {
    return lookup(heap_va)->slab_head;
  }
};

BumpAllocator &get_boot_bump() {
  if (!bootstrap_kheap.valid()) {
    bootstrap_kheap.initialize((std::byte *) arch::KERNEL_HEAP_BASE,
                               arch::KERNEL_HEAP_SIZE);

    heapdb = (HeapInfo *) arch::KERNEL_HEAP_MAP;

    size_t heap_pages = arch::KERNEL_HEAP_SIZE / arch::PAGE_SIZE;
    size_t heapdb_bytes = heap_pages * sizeof(HeapInfo);
    size_t heapdb_pages =
        align_up(heapdb_bytes, arch::PAGE_SIZE) / arch::PAGE_SIZE;

    for (size_t i = 0; i < heapdb_pages; i++) {
      auto page = expect(pmm_alloc(0, PageUse::Slab, ALLOC_ZERO), "oom");
      kmap.page_map().enter(arch::KERNEL_HEAP_MAP + i * arch::PAGE_SIZE,
                            page->to_pa(), PROT_READ | PROT_WRITE,
                            CACHE_DEFAULT, 0);
    }

    pr_debug("mapped %ld pages for heapdb\n", heapdb_pages);
  }
  return *bootstrap_kheap;
}

#define TAG_BIT(ptr)   ((vaddr_t) (ptr) | 0b1)
#define UNTAG_PTR(ptr) ((Page *) ((vaddr_t) (ptr) & ~0b1))
#define IS_TAGGED(ptr) (((vaddr_t) (ptr) & 0b1) == 1)

Page *get_head(Page *page) {
  if (IS_TAGGED(page->slab.slab_head_or_next)) {
    return UNTAG_PTR(page->slab.slab_head_or_next);
  } else {
    return page;
  }
}

void set_head(Page *page, Page *head) {
  page->slab.slab_head_or_next = (Page *) TAG_BIT(head);
}

Page *get_next(Page *page) {
  return get_head(page)->slab.slab_head_or_next;
}

struct FreeNode {
  FreeNode *next;
};

struct SlabHeader {
  FreeNode *freelist;    // Points to the first free object *in this slab*
  size_t allocatedCount; // How many objects are currently in use
};

static SlabConfig calculate_slab_layout(size_t chunk_size,
                                        size_t page_size = arch::PAGE_SIZE) {
  size_t best_page_count = 1;
  size_t best_wasted_bytes = page_size;
  size_t best_match_page_count = 0; // Best candidate meeting threshold
  size_t best_match_waste = SIZE_MAX;
  bool found_good_match = false;

  size_t max_pages = 16;
  if (chunk_size >= page_size / 8) {
    max_pages = 64;
  }

  for (size_t pages = 1; pages <= max_pages; pages++) {
    size_t total_slab_size = pages * page_size;
    size_t chunks = total_slab_size / chunk_size;

    if (chunks == 0)
      continue;

    size_t wasted = total_slab_size % chunk_size;

    // Check if waste ratio is <= 10%
    if (wasted == 0 || total_slab_size / 10 >= wasted) {
      found_good_match = true;
      // Track best candidate meeting the threshold
      if (wasted < best_match_waste) {
        best_match_waste = wasted;
        best_match_page_count = pages;
      }
    }

    // Track best fallback regardless
    if (wasted < best_wasted_bytes) {
      best_wasted_bytes = wasted;
      best_page_count = pages;
    }
  }

  // Use best match if found, otherwise use fallback
  size_t pages_to_use =
      found_good_match ? best_match_page_count : best_page_count;

  // Determine multiplier based on waste of selected candidate
  size_t total_slab_size = pages_to_use * page_size;
  size_t wasted = total_slab_size % chunk_size;

  unsigned int multiplier = 1;
  if (wasted == 0) {
    multiplier = 8; // Perfect fit
  } else if (wasted <= total_slab_size / 80) {
    multiplier = 4; // Low waste (< 1.25%)
  }

  size_t final_page_count = pages_to_use * multiplier;
  final_page_count = std::min(final_page_count, max_pages * 4);

  size_t final_slab_size = final_page_count * page_size;
  return {final_page_count, chunk_size, final_slab_size / chunk_size,
          final_slab_size % chunk_size};
}

Page *GenericSlabCache::alloc_slab() {
  vaddr_t va_base = -1;
  if (!heap_bootstrapped) {
    static Mutex bump_mutex;
    auto guard = frg::guard(&bump_mutex);
    va_base = reinterpret_cast<vaddr_t>(
        expect(get_boot_bump().allocate(slab_conf_.page_count * arch::PAGE_SIZE,
                                        arch::PAGE_SIZE),
               "early heap oom"));
  } else {
    panic("this does not exist yet :(");
  }

  auto map = kmap.page_map();
  Page *slab_head = nullptr;

  for (size_t i = 0; i < slab_conf_.page_count; i++) {
    auto page = expect(pmm_alloc(0, PageUse::Slab, 0), "oom");
    if (i == 0) {
      slab_head = page;
      slab_head->slab.free_count = slab_conf_.chunk_count;
      slab_head->slab.freelist = nullptr;
    } else {
      page->slab.slab_head_or_next = (Page *) (slab_head);
    }
    auto va = va_base + i * arch::PAGE_SIZE;
    HeapInfo::lookup(va)->slab_head = slab_head;
    map.enter(va, page->to_pa(), PROT_READ | PROT_WRITE, arch::CACHE_DEFAULT,
              0);
  }

  // pr_debug("mapped %#lx-%#lx\n", va_base, va_base + config.page_count *
  // 4096);

  FreeNode *node = (FreeNode *) va_base;
  slab_head->slab.freelist = node;

  for (size_t i = 0; i < slab_conf_.chunk_count - 1; i++) {
    node->next = (FreeNode *) ((vaddr_t) node + slab_conf_.chunk_size);
    node = node->next;
  }

  node->next = nullptr;

  return slab_head;
}

GenericSlabCache::GenericSlabCache(frg::string_view name, size_t size,
                                   size_t alignment)
    : object_size_(size < sizeof(FreeNode) ? sizeof(FreeNode) : size),
      alignment_(align_up(alignment, 4)) {
  auto chunk_size = align_up(object_size_, alignment_);
  slab_conf_ = calculate_slab_layout(chunk_size);
  pr_debug(
      "calculated layout for '%s' with size(%ld)+align(%ld): %ld pg,%ld cs,%ld "
      "cc,%ld wb\n",
      name.data(), object_size_, alignment_, slab_conf_.page_count,
      slab_conf_.chunk_size, slab_conf_.chunk_count, slab_conf_.wasted_bytes);
}

void unlink_slab(Page *&head, Page *page) {
  auto meta = &page->slab;

  if (meta->slab_prev)
    meta->slab_prev->slab.slab_head_or_next = meta->slab_head_or_next;
  else
    head = meta->slab_head_or_next;

  if (meta->slab_head_or_next)
    meta->slab_head_or_next->slab.slab_prev = meta->slab_prev;

  meta->slab_prev = nullptr;
  meta->slab_head_or_next = nullptr;
}

void push_front_slab(Page *&head, Page *page) {
  auto meta = &page->slab;

  meta->slab_prev = nullptr;
  meta->slab_head_or_next = head;

  if (head)
    head->slab.slab_prev = page;

  head = page;
}

void *GenericSlabCache::allocate() {
  auto guard = frg::guard(&lock_);

  Page *slab_page = nullptr;

  // Find a slab that has available chunks
  if (slabs_partial_ != nullptr) {
    slab_page = slabs_partial_;
  } else if (slabs_empty_ != nullptr) {
    slab_page = slabs_empty_;
  } else {
    slab_page = alloc_slab();
    push_front_slab(slabs_empty_, slab_page);
  }

  auto slab = &slab_page->slab;
  assert(slab->freelist != nullptr);

  auto buf = slab->freelist;
  slab->freelist = buf->next;
  buf->next = nullptr;

  auto prev_count = slab->free_count;
  assert(prev_count > 0);
  slab->free_count--;

  if (prev_count == slab_conf_.chunk_count) {
    // slab: empty -> partial
    unlink_slab(slabs_empty_, slab_page);
    push_front_slab(slabs_partial_, slab_page);
  } else if (prev_count == 1) {
    // slab: partial -> full
    unlink_slab(slabs_partial_, slab_page);
    push_front_slab(slabs_full_, slab_page);
  }

  return (void *) buf;
}

void GenericSlabCache::deallocate(void *ptr) {
  auto va = (vaddr_t) ptr;
  auto slab_head_page = HeapInfo::lookup_slab(va);

  auto slab_meta = &slab_head_page->slab;

  // pr_debug("slab_head for %p: %p\n", ptr, slab_head_page);

  auto guard = frg::guard(&lock_);

  auto node = (FreeNode *) ptr;
  node->next = slab_meta->freelist;
  slab_meta->freelist = node;

  auto current_count = ++slab_meta->free_count;
  if (current_count == 1) {
    // slab: full -> partial
    unlink_slab(slabs_full_, slab_head_page);
    push_front_slab(slabs_partial_, slab_head_page);
  } else if (current_count == slab_conf_.chunk_count) {
    // slab: partial -> empty
    unlink_slab(slabs_partial_, slab_head_page);
    push_front_slab(slabs_empty_, slab_head_page);
  }
}

} // namespace yak
