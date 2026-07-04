#define pr_fmt(fmt) "pmm: " fmt

#include <algorithm>
#include <cstddef>
#include <frg/mutex.hpp>
#include <optional>
#include <string.h>
#include <yak/arch-mm.h>
#include <yak/arch-page.h>
#include <yak/assert.h>
#include <yak/cpudata.h>
#include <yak/ipl-guard.h>
#include <yak/log.h>
#include <yak/math.h>
#include <yak/numa.h>
#include <yak/panic.h>
#include <yak/spinlock.h>
#include <yak/util.h>
#include <yak/vm/address.h>
#include <yak/vm/direct.h>
#include <yak/vm/flags.h>
#include <yak/vm/page.h>
#include <yak/vm/pmm.h>

namespace yak {

auto pmm_affinities = FixedArena<Affinity, MAX_AFFINITIES>();

[[gnu::const]]
static size_t pmm_block_size(unsigned int order) {
  return 1ULL << (arch::PAGE_SHIFT + order);
}

static Domain &domain_for_region(paddr_t base, paddr_t end) {
  for (auto &aff : pmm_affinities.span()) {
    if (aff.base >= base && end <= aff.end())
      return Domain::from_id(aff.domain);
  }

  // fallback to first domain
  return Domain::from_id(0);
}

void pmm_add_region(paddr_t base, paddr_t end) {
  assert(is_aligned_pow2(base, arch::PAGE_SIZE));
  assert(is_aligned_pow2(end, arch::PAGE_SIZE));

  auto &domain = domain_for_region(base, end);
  auto &mdom = domain.memory;

  size_t length = end - base;
  size_t npages_total = length >> arch::PAGE_SHIFT;

  for (size_t block_base = base; block_base < end;) {
    unsigned int max_order = 0;
    while (max_order < CONFIG_FREELIST_ORDERS - 1) {
      unsigned int next = max_order + 1;
      size_t size = pmm_block_size(next);
      if (block_base + size > end || !is_aligned_pow2(block_base, size))
        break;
      max_order++;
    }

    size_t block_size = pmm_block_size(max_order);

    assert(block_base + block_size <= end);
    assert(is_aligned_pow2(block_base, block_size));
    assert(max_order == CONFIG_FREELIST_ORDERS - 1 ||
           block_base + (block_size * 2) > end ||
           !is_aligned_pow2(block_base, block_size * 2));

    Page *block_page = &arch::p2page(block_base);
    new (block_page) Page();
    block_page->domain = domain.id;
    block_page->order = max_order;
    block_page->block_order = max_order;
    if (max_order != 0)
      block_page->buddy_needs_lazy_init = true;

    mdom.free_list[max_order].push_back(block_page);

    block_base += block_size;
  }

  pr_info("add region (to #%u): %#016lx - %#016lx (%ld pages)\n", domain.id,
          base, end, npages_total);
}

std::optional<Page *> MemoryDomain::allocate(unsigned int desired_order) {
  assert(desired_order < CONFIG_FREELIST_ORDERS);

  if (!free_list[desired_order].empty()) {
    Page *page = free_list[desired_order].pop_front();
    assert(page->usage == PageUse::Free);
    page->refcnt = 1;
    return page;
  }

  // Find the next-highest non-empty free list
  // TODO: this could just use a bitmask
  unsigned int order = desired_order;
  while (++order < CONFIG_FREELIST_ORDERS && free_list[order].empty()) {}

  if (order >= CONFIG_FREELIST_ORDERS) {
    return std::nullopt;
  }

  Page *page = free_list[order].pop_front();

  while (order != desired_order) {
    order -= 1;
    Page *buddy = page + (1 << order); // upper half at new order

    if (page->buddy_needs_lazy_init) {
      new (buddy) Page();
      buddy->domain = page->domain;
      buddy->block_order = page->block_order;
      if (order > 0) {
        page->buddy_needs_lazy_init = true;  // lower half still needs it
        buddy->buddy_needs_lazy_init = true; // upper half needs it too
      } else {
        page->buddy_needs_lazy_init = false;
        buddy->buddy_needs_lazy_init = false;
      }
    }

    buddy->order = order;
    free_list[order].push_back(buddy); // put upper half on free list
  }

  assert(page->usage == PageUse::Free);
  page->order = desired_order;
  page->refcnt = 1;

  return page;
}

void MemoryDomain::free(Page *page) {
  while (page->order < page->block_order) {
    Page *buddy_page = page->buddy(page->order);

#if 0
    auto block_size = pmm_block_size(page->order);
    paddr_t page_addr = page->to_pa();
    paddr_t buddy_addr = page_addr ^ block_size;

    Page *buddy_page = &arch::p2page(buddy_addr);
#endif

#if 0
    // The buddy page is either +block_size or -block_size away
    if (page_addr > buddy_addr) {
      auto delta = (page_addr - buddy_addr) >> arch::PAGE_SHIFT;
      buddy_page = page - delta;
    } else { /* page_addr < buddy_addr */
      auto delta = (buddy_addr - page_addr) >> arch::PAGE_SHIFT;
      buddy_page = page + delta;
    }
#endif

    // both pages must belong to the same initial block
    assert(buddy_page->block_order == page->block_order);

    if (buddy_page->usage != PageUse::Free)
      break;
    if (buddy_page->order != page->order)
      break;

    auto &list = free_list[page->order];
    list.erase(list.iterator_to(buddy_page));

    // choose the lower half
    page = std::min(page, buddy_page);

    page->order += 1;
  }

  page->usage = PageUse::Free;
  free_list[page->order].push_front(page);
}

std::optional<Page *> pmm_alloc(unsigned int order, PageUse use,
                                OptionBits flags) {
  auto *dom = &Domain::from_id(CPUDATA_LOAD(numa_domain)).memory;
  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&dom->lock);
  auto page_res = dom->allocate(order);
  auto page = expect(page_res, "handle pmm oom!");

  page->usage = use;
  page->refcnt++;

  if (flags & ALLOC_ZERO) {
    const auto addr = page->to_pa();
    const auto size = page->block_size();

    // Handled using MapWindow API
    zero_physical_memory(addr, size);
  }

  return page;
}

void Page::release() {
  auto &dom = Domain::from_id(domain).memory;

  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&dom.lock);

  if (refcnt-- == 1) {
    // NOTE: differentiate based on PageUse in the future
    dom.free(this);
  }
}

} // namespace yak
