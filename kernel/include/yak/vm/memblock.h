#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <yak/vm/address.h>
#include <yak/vm/flags.h>

namespace yak {
constexpr int MEMBLOCK_MAX_REGIONS = 128;

struct MemblockRegion {
  paddr_t base_ = 0;
  size_t size_ = 0;
  int node_id_ = -1;

  paddr_t end() const {
    return base_ + size_;
  }
};

struct MemblockType {
  friend struct Memblock;

  size_t total_size_ = 0;
  int count_ = 0;
  MemblockRegion regions_[MEMBLOCK_MAX_REGIONS];

  std::span<MemblockRegion> view() {
    return {regions_, static_cast<size_t>(count_)};
  }

  std::span<const MemblockRegion> view() const {
    return {regions_, static_cast<size_t>(count_)};
  }

  bool is_full() const {
    return count_ >= MEMBLOCK_MAX_REGIONS;
  }

  void add(paddr_t base, size_t size, int nid);

  void assign_node_to_range(paddr_t base, size_t size, int nide);

  void print() const;

  void coalesce();

private:
  int find_insert_index(paddr_t base) const;
  std::optional<int> find_containing_index(paddr_t base, size_t size) const;
  void insert(int index, paddr_t base, size_t size, int nid);
  void remove(int index);
};

struct Memblock {
  // RAM safe for allocation
  MemblockType usable;
  // RAM present, but mustn't be allocated
  MemblockType reserved;
  // all physical RAM regions known to exist
  MemblockType memory;

  inline void coalesce_blocks() {
    usable.coalesce();
    reserved.coalesce();
    memory.coalesce();
  }

  std::optional<paddr_t> allocate(size_t size, size_t align, int nid);
  std::optional<paddr_t> allocate_zeroed(size_t size, size_t align, int nid);

  void free(paddr_t pa, size_t size);
  void free_virtual(vaddr_t va, size_t size);

  void assign_node_to_range(paddr_t base, size_t size, int nid);

  void done();

private:
  std::optional<paddr_t> allocate_from_region(const MemblockRegion &region,
                                              size_t size, size_t align) const;
  std::optional<paddr_t> try_allocate(size_t size, size_t align, int nid);
};

extern Memblock boot_memblock;

} // namespace yak
