#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <yak/vm/address.h>

namespace yak {
class BumpAllocator {
public:
  BumpAllocator(std::byte *base, size_t capacity)
      : base_{base},
        offset_{0},
        capacity_{capacity} {}

  inline std::optional<void *>
  allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
    if (size > capacity_)
      return std::nullopt;

    std::byte *current_ptr = base_ + offset_;

    size_t space_left = capacity_ - offset_;

    void *aligned_ptr = current_ptr;
    if (!std::align(alignment, size, aligned_ptr, space_left)) {
      return std::nullopt; // Not enough space or alignment impossible
    }

    offset_ = (static_cast<std::byte *>(aligned_ptr) - base_) + size;

    return aligned_ptr;
  }

  inline void reset() {
    offset_ = 0;
  }

private:
  std::byte *base_;
  size_t offset_;
  size_t capacity_;
};
} // namespace yak
