#pragma once

#include <optional>
#include <yak/panic.h>

namespace yak {
template <typename T> T expect(std::optional<T> opt, const char *msg) {
  if (!opt) [[unlikely]] {
    panic(msg);
  }
  return std::move(*opt);
}

#define ENUM_BIT_OPS(name)                                \
  inline constexpr name operator|(name a, name b) {       \
    return static_cast<name>(static_cast<OptionBits>(a) | \
                             static_cast<OptionBits>(b)); \
  }                                                       \
  inline constexpr name operator&(name a, name b) {       \
    return static_cast<name>(static_cast<OptionBits>(a) & \
                             static_cast<OptionBits>(b)); \
  }

template <typename T1, typename T2>
inline size_t constexpr offset_of(T1 T2::*member) {
  constexpr T2 object{};
  return size_t(&(object.*member)) - size_t(&object);
}

} // namespace yak
