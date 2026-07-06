#pragma once

#include <new>
#include <yak/vm/generic_slab.h>

namespace yak {
template <typename T> class ObjectCache {
public:
  // Automatically deduces size and alignment from the template parameter
  ObjectCache(frg::string_view name) : _backend{name, sizeof(T), alignof(T)} {}

  ~ObjectCache() = default;

  // Allocate memory and construct the object
  template <typename... Args> T *construct(Args &&...args) {
    void *mem = _backend.allocate();
    if (!mem)
      return nullptr;

    // Use placement new to call the constructor in-place
    return ::new (mem) T(std::forward<Args>(args)...);
  }

  // Destroy the object and free the memory
  void destroy(T *ptr) {
    if (!ptr)
      return;

    // Manually invoke the destructor
    ptr->~T();
    _backend.deallocate(ptr);
  }

private:
  GenericSlabCache _backend;
};
} // namespace yak
