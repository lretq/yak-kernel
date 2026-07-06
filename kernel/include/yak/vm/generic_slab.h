#pragma once

#include <frg/string.hpp>
#include <yak/mutex.h>
#include <yak/vm/page.h>

namespace yak {

using SlabConstructor = bool (*)(void *obj, void *data, int kmflag);
using SlabDestructor = void (*)(void *obj, void *data);
using SlabReclaim = void (*)(void *data);

struct SlabConfig {
  size_t page_count;   // Number of system pages per slab
  size_t chunk_size;   // How many chunks fit in this slab
  size_t chunk_count;  // How many objects fit in this slab
  size_t wasted_bytes; // Remaining wasted space
};

class GenericSlabCache {
public:
  GenericSlabCache(frg::string_view name, size_t size, size_t alignment);
  ~GenericSlabCache();

  GenericSlabCache(const GenericSlabCache &) = delete;
  GenericSlabCache &operator=(const GenericSlabCache &) = delete;
  GenericSlabCache(GenericSlabCache &&other) noexcept = delete;
  GenericSlabCache &operator=(GenericSlabCache &&other) noexcept = delete;

  void *allocate();
  void deallocate(void *ptr);

private:
  Page *alloc_slab();

private:
  Mutex lock_;

  size_t object_size_;
  size_t alignment_;

  SlabConfig slab_conf_;

  Page *slabs_full_;
  Page *slabs_partial_;
  Page *slabs_empty_;
};

} // namespace yak
