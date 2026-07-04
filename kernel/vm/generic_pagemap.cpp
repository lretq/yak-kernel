/**
 * Generic radix-tree PageMap implementation
 *
 * This backend implements the common hierarchical page-table model
 * used by most modern MMU-based architectures. Address translation
 * is represented as a radix tree of page-table levels, where each
 * level indexes a subset of the virtual address bits and ultimately
 * resolves to a leaf PTE describing the final physical mapping.
 *
 * The implementation is intended to provide the machine-independent
 * mechanics of page-table walking, entry creation, removal, and
 * permission updates, while delegating architecture-specific details
 *
 * Architectures with fundamentally different translation models may require a
 * dedicated port rather than this generic implementation.
 *
 * This file should contain only the generic page-table traversal and
 * mapping algorithms. Architecture-specific PTE encoding and MMU
 * operations should remain in arch-specific helpers.
 */

#define pr_fmt(fmt) "generic-pagemap: " fmt

#include <iterator>
#include <yak/arch-mm.h>
#include <yak/arch-pagemap.h>
#include <yak/assert.h>
#include <yak/log.h>
#include <yak/math.h>
#include <yak/panic.h>
#include <yak/util.h>
#include <yak/vm/direct.h>
#include <yak/vm/flags.h>
#include <yak/vm/generic_pagemap.h>
#include <yak/vm/memblock.h>

namespace yak {

template <typename Traits> void GenericPageMap<Traits>::bootstrap_kernel() {
  top_level = expect(boot_memblock.allocate_zeroed(arch::PAGE_SIZE,
                                                   arch::PAGE_SIZE, NUMA_LOCAL),
                     "boot pagemap bootstrap oom");

  auto window = expect(MapWindow::create(top_level, arch::PAGE_SIZE),
                       "failed to map PageMap top level");
  Pte *l1 = window.as<Pte>();

  // The kernel L1 entries are precreated because
  // the higher half is shared across all maps.
  auto l1_entries = Traits::LEVEL_ENTRIES[0];
  for (size_t i = l1_entries / 2; i < l1_entries; i++) {
    auto dir_pa = expect(boot_memblock.allocate_zeroed(
                             arch::PAGE_SIZE, arch::PAGE_SIZE, NUMA_LOCAL),
                         "boot pagemap l1 init oom");
    l1[i] = Traits::make_dir(dir_pa);
  }
}

template <typename Traits>
void GenericPageMap<Traits>::enter_boot(vaddr_t va, paddr_t pa, VMProt prot,
                                        VMCache cache, size_t level) {
  AtomicPte *ptep = fetch(va, level, true, true);
  assert(ptep);

  Pte pte = ptep->load(std::memory_order_acquire);
  if (!Traits::pte_is_zero(pte)) [[unlikely]] {
    panic("try to overwrite mapping directly during boot phase!");
  }

  Pte new_pte = Traits::make_leaf(pa, level, prot, cache);
  if (!ptep->compare_exchange_strong(pte, new_pte, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
    panic("PageMap::enter race!\n");
  }

  if (!Traits::pte_is_zero(pte)) {
    // Overwrote old mapping: Check if we need to TLB shootdown
    // TODO: invalidate
  }
}

template <typename Traits>
void GenericPageMap<Traits>::enter_boot_large(paddr_t phys_base,
                                              vaddr_t virt_base, size_t length,
                                              VMProt prot, VMCache cache) {
  assert(is_aligned_pow2<arch::PAGE_SIZE>(phys_base));
  assert(is_aligned_pow2<arch::PAGE_SIZE>(virt_base));
  assert(is_aligned_pow2<arch::PAGE_SIZE>(length));

  paddr_t end = phys_base + length;
  paddr_t addr = phys_base;

  if constexpr (std::size(Traits::PAGE_SIZES) == 1) {
    // This MMU does not support Page Sizes > arch::PAGE_SIZE
    size_t pages = (end - addr) >> arch::PAGE_SHIFT;
    while (pages--) {
      vaddr_t virt_addr = virt_base + (addr - phys_base);
      enter_boot(virt_addr, addr, prot, cache, 0);
      addr += arch::PAGE_SIZE;
    }
    return;
  }

  // map until aligned to a larger page size
  while (addr < end) {
    vaddr_t virt_addr = virt_base + (addr - phys_base);

    size_t chosen_level = 0;
    size_t chosen_size = arch::PAGE_SIZE;

    for (size_t i = Traits::max_page_size_idx() + 1; i-- > 0;) {
      size_t page_size = Traits::PAGE_SIZES[i];

      if (addr + page_size > end)
        continue;

      if (!is_aligned_pow2(addr, page_size))
        continue;

      if (!is_aligned_pow2(virt_addr, page_size))
        continue;

      chosen_level = i;
      chosen_size = page_size;
      break;
    }

    if (chosen_level != 0) {
      // map as large page!
      enter_boot(virt_addr, addr, prot, cache, chosen_level);
      addr += chosen_size;
      continue;
    }

    size_t next_large = Traits::PAGE_SIZES[1];
    size_t misalign = addr & (next_large - 1);
    size_t advance = misalign ? (next_large - misalign) : next_large;

    if (addr + advance > end)
      advance = end - addr;

    size_t pages = advance >> arch::PAGE_SHIFT;

    while (pages--) {
      if (addr >= end)
        break;
      virt_addr = virt_base + (addr - phys_base);
      enter_boot(virt_addr, addr, prot, cache, 0);
      addr += arch::PAGE_SIZE;
    }
  }
}

template <typename Traits> void GenericPageMap<Traits>::activate() {
  Traits::activate(top_level);
}

template <typename Traits>
GenericPageMap<Traits>::AtomicPte *
GenericPageMap<Traits>::fetch(vaddr_t va, size_t to_level, bool allocate,
                              bool is_boot) {

  auto window = expect(MapWindow::create(top_level, arch::PAGE_SIZE),
                       "failed to map top level");
  AtomicPte *table = window.as<AtomicPte>();

  for (size_t lvl = Traits::levels() - 1;; lvl--) {
    AtomicPte *ptep = &table[get_index(va, lvl)];

    // acquire: child table contents become visible after reading the PTE
    Pte pte = ptep->load(std::memory_order_acquire);

    if (lvl == to_level) {
      return ptep;
    }

    if (Traits::pte_is_zero(pte)) {
      if (!allocate) {
        return nullptr;
      }

      // TODO: alloc
      paddr_t pa;
      if (is_boot) {
        pa = expect(boot_memblock.allocate_zeroed(arch::PAGE_SIZE,
                                                  arch::PAGE_SIZE, NUMA_LOCAL),
                    "boot pagemap oom");
      } else {
        panic("not defined\n");
      }
      assert(pa != 0);

      Pte new_table = Traits::make_dir(pa);

      // release: publish fully initialized table
      // acquire: see another threads table
      if (!ptep->compare_exchange_strong(pte, new_table,
                                         std::memory_order_release,
                                         std::memory_order_acquire)) {
        // We raced another walk: pte now contains the new value
        if (is_boot) {
          boot_memblock.free(pa, arch::PAGE_SIZE);
        } else {
          panic("not defined\n");
        }
      } else {
        pte = new_table;
      }
    } else {
      assert(!Traits::pte_is_large(pte, lvl));
    }

    window = expect(MapWindow::create(Traits::pte_addr(pte), arch::PAGE_SIZE),
                    "failed to map sub table");
    table = window.as<AtomicPte>();
  }

  return nullptr;
}

template class GenericPageMap<GenericPageMapTraits>;

} // namespace yak
