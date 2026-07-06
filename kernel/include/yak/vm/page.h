#pragma once

#include <bit>
#include <cstdint>
#include <frg/intrusive.hpp>
#include <frg/list.hpp>
#include <yak/arch-mm.h>
#include <yak/vm/address.h>

namespace yak {

enum class PageUse {
  null,
  Reserved,
  Free,
  Wired,
  Slab,
};

class GenericSlabCache;
struct FreeNode;
struct Page;

struct [[gnu::packed]] SlabMeta {
  // The lower two bits are masked before accessing
  Page *slab_head_or_next;
  Page *slab_prev;
  FreeNode *freelist;
  uint16_t free_count;
};

struct [[gnu::aligned(64)]] Page {
  unsigned int domain = 0;
  PageUse usage = PageUse::Free;
  uint32_t refcnt = 0;
  unsigned short order = -1, block_order = -1;

  union {
    // When PageUse = Slab
    SlabMeta slab;

    // When PageUse = Free/Reserved
    struct {
      bool buddy_needs_lazy_init = false;
      frg::default_list_hook<Page> buddy_page_hook;
    };
  };

  inline size_t to_pfn() const {
    size_t off = (vaddr_t) this - arch::PFNDB_BASE;
    return off >> std::countr_zero(sizeof(Page));
  }

  inline paddr_t to_pa() const {
    return to_pfn() << arch::PAGE_SHIFT;
  }

  inline size_t block_size() const {
    return 1ULL << (order + arch::PAGE_SHIFT);
  }

  inline Page *buddy(unsigned int at_order) {
    size_t pfn = to_pfn();
    size_t buddy_pfn = pfn ^ (1 << at_order);
    return this + (buddy_pfn - pfn);
  }

  void retain();
  void release();
};

static_assert(sizeof(Page) == 64);

} // namespace yak
