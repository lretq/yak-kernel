#pragma once

#include <utility>

namespace yak {

enum class SchedPrio : unsigned int {
  Idle = 0,
  TimeShare = 1, // range start
  RealTime = 32, // range start
  Max = 63,
};

constexpr std::strong_ordering operator<=>(SchedPrio a, SchedPrio b) noexcept {
  return std::to_underlying(a) <=> std::to_underlying(b);
}

constexpr bool operator==(SchedPrio a, SchedPrio b) noexcept {
  return std::to_underlying(a) == std::to_underlying(b);
}

constexpr SchedPrio operator+(SchedPrio lhs, unsigned int rhs) {
  return static_cast<SchedPrio>(std::to_underlying(lhs) + rhs);
}

constexpr SchedPrio operator+(unsigned int lhs, SchedPrio rhs) {
  return rhs + lhs;
}

namespace sched_prio {
inline constexpr unsigned int PRIO_COUNT = 64;
inline constexpr unsigned int TIME_SHARE_END = 31;
inline constexpr unsigned int REAL_TIME_END = 63;

constexpr bool is_time_share(SchedPrio p) {
  auto v = std::to_underlying(p);
  return v >= std::to_underlying(SchedPrio::TimeShare) && v <= TIME_SHARE_END;
}

constexpr bool is_real_time(SchedPrio p) {
  auto v = std::to_underlying(p);
  return v >= std::to_underlying(SchedPrio::RealTime) && v <= REAL_TIME_END;
}

} // namespace sched_prio
} // namespace yak
