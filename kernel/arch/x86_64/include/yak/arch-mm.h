#pragma once

#include <cstddef>
#include <yak/types.h>
#include <yak/vm/address.h>

namespace yak::arch {

extern vaddr_t HHDM_BASE; // changes depending on PML4/5
extern size_t PMAP_LEVELS;

constexpr vaddr_t PFNDB_BASE = 0xffffc00000000000; // -64TiB
constexpr size_t PFNDB_SIZE =
    4ULL * 1024 * 1024 * 1024 * 1024; // 4TiB ought to be enough for anybody:
                                      // 4TiB/64B*4096=describe 256TiB of memory

constexpr vaddr_t KERNEL_HEAP_BASE = 0xffffc40000000000;       // -60TiB
constexpr size_t KERNEL_HEAP_SIZE = 2ULL * 1024 * 1024 * 1024; // 2GiB
constexpr vaddr_t KERNEL_HEAP_MAP = KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE;

constexpr size_t PAGE_SIZE = 4096;
constexpr unsigned int PAGE_SHIFT = 12;

enum {
  // These map to the PAT bits
  CACHE_UNCACHED = 0,
  CACHE_WRITECOMBINE,
  CACHE_WRITETHROUGH,
  CACHE_WRITEBACK,
  // And these provide the architecture constants
  CACHE_DEFAULT = CACHE_WRITEBACK,
  CACHE_DISABLE = CACHE_UNCACHED,
};

inline size_t p2pfn(paddr_t pa) {
  return pa >> PAGE_SHIFT;
}

} // namespace yak::arch
