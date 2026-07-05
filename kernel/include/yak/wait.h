#pragma once

#include <expected>
#include <optional>
#include <span>
#include <yak/status.h>
#include <yak/types.h>
#include <yak/waitblock.h>

namespace yak {

enum class WaitMode {
  Block,
  Poll,
};

enum class WaitType {
  /* unblock when any object is ready */
  Any,
  /* unblock when all objects are ready */
  All,
};

static constexpr TimeNs POLL_ONCE = -1;

WaitResult wait_for_single(KObject &obj, WaitMode mode,
                           std::optional<TimeNs> timeout = std::nullopt);

WaitResult
wait_for_many(std::span<KObject *> objects, WaitMode mode, WaitType type,
              std::optional<TimeNs> timeout = std::nullopt,
              std::optional<std::span<WaitBlock>> table_opt = std::nullopt);

} // namespace yak
