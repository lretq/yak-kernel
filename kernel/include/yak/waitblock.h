#pragma once

#include <expected>
#include <frg/list.hpp>
#include <yak/status.h>
#include <yak/types.h>

namespace yak {

enum {
  WB_DEQUEUED = 1 << 0,
  WB_UNWAITED = 1 << 1,
};

class Thread;
struct KObject;

using WaitResult = std::expected<int, Status>;

struct WaitBlock {
  constexpr WaitBlock() = default;

  WaitBlock(const WaitBlock &) = delete;
  WaitBlock &operator=(const WaitBlock &) = delete;
  WaitBlock(WaitBlock &&) = delete;
  WaitBlock &operator=(WaitBlock &&) = delete;

  Thread *thread_;         /* thread waiting */
  KObject *object_;        /* object being waited on */
  OptionBits flags_;       /* internal flags */
  WaitResult wake_status_; /* status passed to Thread::unwait() */
  frg::default_list_hook<WaitBlock> hook_; /* linkage for object list*/
};

using WaitBlockList = frg::intrusive_list<
    WaitBlock, frg::locate_member<WaitBlock, frg::default_list_hook<WaitBlock>,
                                  &WaitBlock::hook_>>;

} // namespace yak
