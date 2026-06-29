#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>

namespace yak {
template <auto V>
concept pow2 = std::integral<decltype(V)> && V != 0 &&
               (static_cast<std::make_unsigned_t<decltype(V)>>(V) &
                (static_cast<std::make_unsigned_t<decltype(V)>>(V) - 1)) == 0;

template <auto Align, std::integral T>
  requires pow2<Align>
constexpr bool is_aligned_pow2(T addr) noexcept {
  using U = std::common_type_t<std::make_unsigned_t<T>,
                               std::make_unsigned_t<decltype(Align)>>;
  return (static_cast<U>(addr) & (static_cast<U>(Align) - 1)) == 0;
}

template <std::integral T1, std::integral T2>
[[gnu::const]]
constexpr bool is_aligned_pow2(T1 addr, T2 align) noexcept {
  using U =
      std::common_type_t<std::make_unsigned_t<T1>, std::make_unsigned_t<T2>>;
  return (static_cast<U>(addr) & (static_cast<U>(align) - 1)) == 0;
}

template <std::integral T>
[[gnu::const]]
constexpr bool is_pow2(T v) noexcept {
  using U = std::make_unsigned_t<T>;
  return v != 0 && (static_cast<U>(v) & (static_cast<U>(v) - 1)) == 0;
}

template <std::integral T1, std::integral T2>
[[gnu::const]]
constexpr auto div_roundup(T1 a, T2 b) noexcept {
  using U =
      std::common_type_t<std::make_unsigned_t<T1>, std::make_unsigned_t<T2>>;
  return (static_cast<U>(a) + static_cast<U>(b) - 1) / static_cast<U>(b);
}

template <auto Align, std::integral T>
  requires pow2<Align>
constexpr auto align_up(T addr) noexcept {
  using U = std::make_unsigned_t<T>;
  return static_cast<T>((static_cast<U>(addr) + Align - 1) &
                        ~(static_cast<U>(Align) - 1));
}

template <auto Align, std::integral T>
  requires pow2<Align>
constexpr auto align_down(T addr) noexcept {
  using U = std::make_unsigned_t<T>;
  return static_cast<T>(static_cast<U>(addr) & ~(static_cast<U>(Align) - 1));
}

template <std::integral T1, std::integral T2>
[[gnu::const]]
constexpr auto align_up(T1 addr, T2 align) noexcept {
  using U =
      std::common_type_t<std::make_unsigned_t<T1>, std::make_unsigned_t<T2>>;
  return (static_cast<U>(addr) + static_cast<U>(align) - 1) &
         ~(static_cast<U>(align) - 1);
}

template <std::integral T1, std::integral T2>
[[gnu::const]]
constexpr auto align_down(T1 addr, T2 align) noexcept {
  using U =
      std::common_type_t<std::make_unsigned_t<T1>, std::make_unsigned_t<T2>>;
  return static_cast<U>(addr) & ~(static_cast<U>(align) - 1);
}

} // namespace yak
