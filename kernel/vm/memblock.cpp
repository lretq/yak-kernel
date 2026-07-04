#include "yak/vm/direct.h"
#include <algorithm>
#include <cstring>
#include <span>
#include <string.h>
#include <yak/arch-mm.h>
#include <yak/assert.h>
#include <yak/cpudata.h>
#include <yak/log.h>
#include <yak/math.h>
#include <yak/util.h>
#include <yak/vm/flags.h>
#include <yak/vm/memblock.h>
#include <yak/vm/pmm.h>

namespace yak {

Memblock boot_memblock = Memblock();

void MemblockType::add(paddr_t base, size_t size, int nid) {
  bool tried_coalesce = false;

  while (true) {
    if (is_full()) {
      if (!tried_coalesce) {
        coalesce();
        tried_coalesce = true;
        continue;
      }
      panic("too many regions; can't add more\n");
    }

    insert(find_insert_index(base), base, size, nid);
    break;
  }
}

int MemblockType::find_insert_index(paddr_t base) const {
  auto v = view();
  auto it = std::lower_bound(
      v.cbegin(), v.cend(), base,
      [](const MemblockRegion &r, paddr_t b) { return r.base_ < b; });
  return static_cast<int>(std::distance(v.cbegin(), it));
}

std::optional<int> MemblockType::find_containing_index(paddr_t pa,
                                                       size_t size) const {
  auto v = view();

  auto it =
      std::find_if(v.cbegin(), v.cend(), [pa, size](const MemblockRegion &reg) {
        return reg.base_ <= pa && reg.end() >= pa + size;
      });

  if (it != v.cend()) {
    int index = static_cast<int>(std::distance(v.cbegin(), it));
    return index;
  }

  return std::nullopt;
}

void MemblockType::insert(int index, paddr_t base, size_t size, int nid) {
  auto v = std::span(regions_, static_cast<size_t>(count_ + 1));
  std::shift_right(v.begin() + index, v.end(), 1);

  regions_[index] = {.base_ = base, .size_ = size, .node_id_ = nid};
  total_size_ += size;
  count_++;
}

void MemblockType::remove(int index) {
  auto v = view();
  total_size_ -= regions_[index].size_;
  std::shift_left(v.begin() + index, v.end(), 1);
  count_--;
}

void MemblockType::coalesce() {
  for (int i = 0; i < count_ - 1;) {
    auto &current = regions_[i];
    auto &next = regions_[i + 1];

    if (current.node_id_ == next.node_id_ && current.end() == next.base_) {
      current.size_ += next.size_;
      total_size_ += next.size_;
      remove(i + 1);
    } else {
      i++;
    }
  }
}

void MemblockType::assign_node_to_range(paddr_t base, size_t size, int nid) {
  const paddr_t range_end = base + size;

  int i = 0;
  while (i < count_) {
    auto r = regions_[i];

    const paddr_t overlap_start = std::max(r.base_, base);
    const paddr_t overlap_end = std::min(r.end(), range_end);

    // No overlap or node id already matches
    if (overlap_start >= overlap_end || r.node_id_ == nid) {
      i++;
      continue;
    }

    // Remove the original region
    remove(i);

    // Left portion (before overlap)
    if (r.base_ < overlap_start)
      add(r.base_, overlap_start - r.base_, r.node_id_);

    // Portion assigned to new node
    add(overlap_start, overlap_end - overlap_start, nid);

    // Right portion (after overlap)
    if (overlap_end < r.end())
      add(overlap_end, r.end() - overlap_end, r.node_id_);

    // Move to next region
    // DO NOT increment i here -> next region is now at index i
  }
}

void MemblockType::print() const {
  pr_debug("MemblockType: total_size=%zu count=%d\n", total_size_, count_);

  for (int i = 0; i < count_; ++i) {
    const auto &r = regions_[i];

    pr_debug("  [%d] base=%#llx end=%#llx size=%#zx nid=%d\n", i,
             static_cast<unsigned long long>(r.base_),
             static_cast<unsigned long long>(r.end()), r.size_, r.node_id_);
  }
}

std::optional<paddr_t> Memblock::try_allocate(size_t size, size_t align,
                                              int nid) {
  // Iterate memory regions from highest to lowest
  auto regions = usable.view();
  for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
    if (nid != NUMA_ANY && it->node_id_ != nid)
      continue;
    if (it->size_ < size)
      continue;

    paddr_t candidate = align_down(it->end() - size, align);
    if (candidate < it->base_)
      continue;

    const int region_nid = it->node_id_;
    const paddr_t region_base = it->base_;
    const paddr_t region_end = it->end();
    const int index =
        static_cast<int>(std::distance(regions.begin(), it.base()) - 1);

    usable.remove(index);

    if (candidate > region_base)
      usable.add(region_base, candidate - region_base, region_nid);

    if (candidate + size < region_end)
      usable.add(candidate + size, region_end - (candidate + size), region_nid);

    reserved.add(candidate, size, region_nid);

    return candidate;
  }

  return std::nullopt;
}

std::optional<paddr_t> Memblock::allocate(size_t size, size_t align, int nid) {
  if (nid == NUMA_LOCAL) {
    nid = CPUDATA_LOAD(numa_domain);
  }

  if (auto addr = try_allocate(size, align, nid))
    return addr;

  return std::nullopt;
}

std::optional<paddr_t> Memblock::allocate_zeroed(size_t size, size_t align,
                                                 int nid) {
  if (auto addr = allocate(size, align, nid)) {
    zero_physical_memory(*addr, size);
    return addr;
  }
  return std::nullopt;
}

void Memblock::free(paddr_t pa, size_t size) {
  auto index = expect(reserved.find_containing_index(pa, size),
                      "memblock free unreserved memory");

  // Create a copy: after remove() the original slot no longer contains our
  // region
  auto r = reserved.regions_[index];

  reserved.remove(index);

  paddr_t free_end = pa + size;

  // Handle leftover space to the left
  if (r.base_ < pa) {
    reserved.add(r.base_, pa - r.base_, r.node_id_);
  }

  // Handle leftover space to the right
  if (free_end < r.end()) {
    reserved.add(free_end, r.end() - free_end, r.node_id_);
  }

  // Add the free block back to usable memory
  usable.add(pa, size, r.node_id_);
}

void Memblock::assign_node_to_range(paddr_t base, size_t size, int nid) {
  usable.assign_node_to_range(base, size, nid);
  reserved.assign_node_to_range(base, size, nid);
  memory.assign_node_to_range(base, size, nid);
}

void Memblock::done() {
  static bool done_already = false;
  assert(!done_already);
  done_already = true;

  boot_memblock.coalesce_blocks();

  for (auto &entry : usable.view()) {
    pmm_add_region(entry.base_, entry.end());
  }
}

} // namespace yak
