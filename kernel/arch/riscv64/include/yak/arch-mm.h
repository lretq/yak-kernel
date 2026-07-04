#pragma once

#include <cstddef>
#include <yak/types.h>
#include <yak/vm/address.h>

namespace yak::arch {

extern vaddr_t HHDM_BASE;
extern size_t PMAP_LEVELS;

// Cannot be constexpr: must fit in Sv39 (128 GiB high half) through Sv57
// Set at boot alongside HHDM_BASE.
extern vaddr_t PFNDB_BASE;
extern vaddr_t KERNEL_WIRED_HEAP_BASE;

constexpr size_t PAGE_SIZE = 4096;
constexpr unsigned int PAGE_SHIFT = 12;

enum {
  CACHE_UNCACHED = 0,
  CACHE_WRITECOMBINE,
  CACHE_WRITETHROUGH,
  CACHE_WRITEBACK,
  // And these provide the architecture constants
  CACHE_DEFAULT = CACHE_WRITEBACK,
  CACHE_DISABLE = CACHE_UNCACHED,
};

[[gnu::const]]
inline size_t p2pfn(paddr_t pa) {
  return pa >> PAGE_SHIFT;
}

} // namespace yak::arch
