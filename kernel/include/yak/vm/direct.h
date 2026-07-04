#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <yak/arch-mm.h>
#include <yak/types.h>
#include <yak/vm/address.h>

namespace yak {

enum class DirectMapError {
  None,           // no error
  InvalidRange,   // len==0, or [pa,pa+len) overflows the address type
  OutOfWindows,   // no temporary mappings slots available
  BackendFailure, // generic backend failure (e.g. PTE allocation failed)
};

// arch-interface for direct mappings
namespace arch {

// Returns true if every byte of [pa, pa+len) is already reachable via
// the kernel's permanent direct map, i.e. it can be accessed as
// `direct_map_offset() + pa` without any page-table changes.
//
//  - On a backend with a full direct map of all RAM (e.g. x86_64), this
//   can simply check that pa+len doesn't run past the top of physical
//   memory.
// - On a backend with no direct map at all (e.g. a memory-constrained
//   32-bit architecture), this can unconditionally `return false;`. Every
//   request then goes through map_window()/unmap_window() below.
// - Partial direct maps (e.g. only low memory is direct-mapped) are also
//   fine: just check pa+len against whatever the direct-mapped ceiling
//   is.
bool is_direct_mapped(paddr_t pa, size_t len);

// The offset so that `direct_map_offset() + pa` is the virtual address
// of physical address `pa`, for any pa where is_direct_mapped() would
// return true.
vaddr_t direct_map_offset();

// Establish a temporary RW mapping of the page-aligned range [pa_page, pa_page
// + page_count * PAGE_SIZE]. Returns the virtual address of the start of the
// mapping on success, or std::nullopt if no window slot / pte memory is
// available.
//
// The function is only called when is_direct_map() returns false.
//
// If map slots are shared, this function has to handle synchronization itself.
std::optional<vaddr_t> map_window(paddr_t pa_page, size_t page_count);

// Tear down a mapping previously returned by map_window(). `va` and
// `page_count` are exactly the values map_window() was called with /
// returned.
void unmap_window(vaddr_t va, size_t page_count);
} // namespace arch

class MapWindow {
public:
  MapWindow(const MapWindow &) = delete;
  MapWindow &operator=(const MapWindow &) = delete;

  MapWindow(MapWindow &&other)
      : pa_(other.pa_),
        data_(other.data_),
        len_(other.len_),
        window_base_(other.window_base_),
        window_page_count_(other.window_page_count_) {
    other.pa_ = 0;
    other.data_ = nullptr;
    other.len_ = 0;
    other.window_base_ = 0;
    other.window_page_count_ = 0;
  }

  MapWindow &operator=(MapWindow &&other) noexcept {
    if (this != &other) {
      release();
      pa_ = other.pa_;
      data_ = other.data_;
      len_ = other.len_;
      window_base_ = other.window_base_;
      window_page_count_ = other.window_page_count_;

      other.pa_ = 0;
      other.data_ = nullptr;
      other.len_ = 0;
      other.window_base_ = 0;
      other.window_page_count_ = 0;
    }
    return *this;
  }

  // Unmaps the window (via arch::unmap_window) if it was a windowed
  // mapping. A no-op for direct-mapped windows, and a no-op on a
  // moved-from object.
  ~MapWindow() {
    release();
  }

  // Maps `len` bytes of physical memory starting at `pa` and returns a
  // handle to it, or a DirectMapError on failure
  //
  // `pa` and `len` need NOT be page-aligned. The request
  // is rounded to whole pages before being handed to the backend.
  static std::optional<MapWindow> create(paddr_t pa, size_t len,
                                         DirectMapError *err_out = nullptr) {
    auto fail = [&](DirectMapError e) -> std::optional<MapWindow> {
      if (err_out)
        *err_out = e;
      return std::nullopt;
    };

    if (len == 0 || pa + len < pa) {
      return fail(DirectMapError::InvalidRange);
    }

    // Short-circuit to direct map
    if (arch::is_direct_mapped(pa, len)) {
      vaddr_t va = arch::direct_map_offset() + pa;
      return MapWindow(pa, reinterpret_cast<void *>(va), len, 0, 0);
    }

    // Slow path: borrow a temporary window from the backend, covering
    // whole pages, and point into it at the right offset.
    const size_t page_size = arch::PAGE_SIZE;
    const size_t page_off = static_cast<size_t>(pa % page_size);
    const paddr_t pa_page = pa - page_off;
    const size_t span = page_off + len;
    const size_t page_count = (span + page_size - 1) / page_size;

    std::optional<vaddr_t> window_base = arch::map_window(pa_page, page_count);
    if (!window_base) {
      return fail(DirectMapError::OutOfWindows);
    }

    void *data = reinterpret_cast<void *>(*window_base + page_off);
    return MapWindow(pa, data, len, *window_base, page_count);
  }

  // Raw access to the mapped range.
  void *data() {
    return data_;
  }
  const void *data() const {
    return data_;
  }
  size_t size() const {
    return len_;
  }

  std::span<std::byte> bytes() {
    return {static_cast<std::byte *>(data_), len_};
  }
  std::span<const std::byte> bytes() const {
    return {static_cast<const std::byte *>(data_), len_};
  }

  // Typed access convenience
  template <typename T> T *as() {
    static_assert(std::is_trivially_copyable_v<T>,
                  "as<T>() is meant for trivial structures");
    return static_cast<T *>(data_);
  }
  template <typename T> const T *as() const {
    static_assert(std::is_trivially_copyable_v<T>,
                  "as<T>() is meant for trivial structures");
    return static_cast<const T *>(data_);
  }

  // True if this window was satisfied via the direct-map short
  // circuit
  bool is_direct_mapped() const {
    return window_page_count_ == 0;
  }

  // True for a default-constructed-by-move-from object; size() == 0
  // and data() == nullptr in that state.
  bool empty() const {
    return len_ == 0;
  }

  // The physical address this window was created for (== `pa` passed
  // to create(), not the page-rounded backend address).
  paddr_t phys_addr() const {
    return pa_;
  }

private:
  MapWindow() = default;
  MapWindow(paddr_t pa, void *data, size_t len, vaddr_t window_base,
            size_t window_page_count)
      : pa_(pa),
        data_(data),
        len_(len),
        window_base_(window_base),
        window_page_count_(window_page_count) {}

  void release() {
    if (window_page_count_ != 0) {
      arch::unmap_window(window_base_, window_page_count_);
    }
    pa_ = 0;
    data_ = nullptr;
    len_ = 0;
    window_base_ = 0;
    window_page_count_ = 0;
  }

  paddr_t pa_ = 0;
  void *data_ = nullptr;
  size_t len_ = 0;

  vaddr_t window_base_ = 0;
  size_t window_page_count_ = 0;
};

void zero_physical_memory(paddr_t pa, size_t len);

} // namespace yak
