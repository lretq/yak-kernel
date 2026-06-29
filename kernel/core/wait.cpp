#include <expected>
#include <frg/mutex.hpp>
#include <ranges>
#include <span>
#include <yak/assert.h>
#include <yak/cpudata.h>
#include <yak/ipl-guard.h>
#include <yak/ipl.h>
#include <yak/kobject.h>
#include <yak/panic.h>
#include <yak/sched.h>
#include <yak/status.h>
#include <yak/thread.h>
#include <yak/wait.h>
#include <yak/waitblock.h>

namespace yak {

std::expected<int, Status> decode_wait_status(int raw) {
  if (raw < 0)
    return std::unexpected{static_cast<Status>(-raw)};
  return raw;
}

static void maybe_acquire(KObject &obj) {
  if (obj.type_ == KObjectType::Sync)
    obj.signal_count_--;
}

static void wb_dequeue(WaitBlock &wb) {
  auto &obj = *wb.object_;
  auto guard = frg::guard(&obj.lock_);

  // signaling an object sets WB_DEQUEUED
  if (!(wb.flags_ & WB_DEQUEUED)) {
    obj.wait_list_.erase(obj.wait_list_.iterator_to(&wb));
    obj.wait_count_--;
  }
}

static void set_timeout(Thread &thread, TimeNs timeout) {
#if 0
    auto &timer = thread.timeout_timer_;

    timer.reset();
    timer.install(timeout);

    timer.obj_.wait_list_.push_back(thread.timeout_waitblock_);
    timer.obj_.wait_count_ = 1;
#endif
  panic("install timer");
}

static TimeNs uptime() {
  return -1;
}

static std::expected<void, Status> poll_single(KObject &obj,
                                               std::optional<TimeNs> timeout) {
  const bool has_timeout = timeout.has_value() && timeout != POLL_ONCE;

  IplGuard ipl{Ipl::dispatch};

  const TimeNs deadline = has_timeout ? uptime() + timeout.value() : 0;

  do {
    {
      auto guard = frg::guard(&obj.lock_);

      if (obj.is_signaled()) {
        maybe_acquire(obj);
        return {};
      }
    }

    busyloop_hint();

    if (has_timeout && uptime() >= deadline)
      return std::unexpected(YAK_TIMEOUT);
  } while (timeout != POLL_ONCE);

  return std::unexpected(YAK_TIMEOUT);
}

static std::expected<int, Status> poll_many(std::span<KObject *> objects,
                                            std::optional<TimeNs> timeout) {
  const bool has_timeout = timeout.has_value() && timeout != POLL_ONCE;

  IplGuard ipl{Ipl::dispatch};

  const TimeNs deadline = has_timeout ? uptime() + *timeout : 0;

  do {
    for (auto [i, obj] : objects | std::views::enumerate) {
      auto guard = frg::guard(&obj->lock_);

      if (obj->is_signaled()) {
        maybe_acquire(*obj);
        return i;
      }
    }

    busyloop_hint();

    if (has_timeout && uptime() >= deadline)
      return std::unexpected{YAK_TIMEOUT};
  } while (timeout != POLL_ONCE);

  return std::unexpected{YAK_TIMEOUT};
}

static std::expected<int, Status> do_wait(Thread &thread, bool has_timeout) {
  // the idle thread must not wait!
  assert(!thread.is_idle());

  thread.wait_phase_ = WaitPhase::Committed;
  thread.state_ = ThreadState::Blocked;

  CpuData::local_scheduler().yield(&thread);
  // unlocks thread
  assert(iplget() == Ipl::dispatch);

  for (auto &wb : thread.wait_blocks_)
    wb_dequeue(wb);

  if (has_timeout) {
    wb_dequeue(thread.timeout_waitblock_);
    // This might try to uninstall a non-queued timer,
    // but that is handled in timer_uninstall
    /// timer_uninstall(&thread->timeout_timer);
    panic("uninstall");
  }

  auto tguard = frg::guard(&thread.lock_);
  thread.wait_phase_ = WaitPhase::None;

  return decode_wait_status(thread.wait_status_);
}

std::expected<int, Status> wait_for_single(KObject &obj, WaitMode mode,
                                           std::optional<TimeNs> timeout) {
  if (mode == WaitMode::Poll)
    return poll_single(obj, timeout).transform([]() -> int { return 0; });

  const bool has_timeout = timeout.has_value();
  auto &thread = *Thread::Current();

  auto &wb = thread.inline_waitblocks_[0];
  wb.thread_ = &thread;
  wb.object_ = &obj;
  wb.flags_ = 0;
  wb.wake_status_ = 0;

  IplGuard ipl{Ipl::dispatch};

  {
    auto guard = frg::guard(&thread.lock_);
    thread.wait_phase_ = WaitPhase::InProgress;
    thread.timeout_waitblock_.flags_ = 0;
    thread.wait_blocks_ = std::span<WaitBlock>(&wb, 1);
  }

  {
    auto guard = frg::guard(&obj.lock_);
    if (obj.is_signaled()) {
      maybe_acquire(obj);

      {
        auto tguard = frg::guard(&thread.lock_);
        thread.wait_phase_ = WaitPhase::None;
      }

      return 0;
    }

    obj.wait_list_.push_back(&wb);
    obj.wait_count_++;
  }

  if (has_timeout)
    set_timeout(thread, *timeout);

  thread.lock_.lock();

  // If our thread's wait was aborted, that means we DONT have to
  // decrement the signal count anymore!

  // dequeue the wait block
  if (thread.wait_phase_ == WaitPhase::Aborted) {
    int wait_status = thread.wait_status_;

    thread.lock_.unlock();

    if (has_timeout) {
      // thread.timeout_timer_.uninstall();
      wb_dequeue(thread.timeout_waitblock_);
    }

    wb_dequeue(wb);

    auto tguard = frg::guard(&thread.lock_);
    thread.wait_phase_ = WaitPhase::None;

    return decode_wait_status(wait_status);
  }

  // do_wait takes the lock ownership for thread.lock_ here.
  // the IplGuard will lower the ipl here, too.
  return do_wait(thread, has_timeout);
}

std::expected<int, Status>
wait_for_many(std::span<KObject *> objects, WaitMode mode, WaitType type,
              std::optional<TimeNs> timeout,
              std::optional<std::span<WaitBlock>> table_opt) {
  assert(!objects.empty());

  if (mode == WaitMode::Poll)
    return poll_many(objects, timeout);

  const bool has_timeout = timeout.has_value();
  auto &thread = *Thread::Current();

  std::span<WaitBlock> table;
  if (table_opt.has_value()) {
    table = *table_opt;
  } else {
    table = std::span<WaitBlock>(thread.inline_waitblocks_,
                                 THREAD_INLINE_WAIT_BLOCKS);
  }

  assert(table.size() >= objects.size());

  table = table.first(objects.size());

  for (auto [i, wb] : table | std::views::enumerate) {
    wb.thread_ = &thread;
    wb.object_ = objects[i];
    wb.flags_ = 0;
    wb.wake_status_ = i;
  }

  IplGuard ipl{Ipl::dispatch};

  {
    auto guard = frg::guard(&thread.lock_);
    thread.wait_phase_ = WaitPhase::InProgress;
    thread.timeout_waitblock_.flags_ = 0;
    thread.wait_blocks_ = table;
  }

  if (type == WaitType::Any) {
    for (auto [i, obj] : objects | std::views::enumerate) {
      auto guard = frg::guard(&obj->lock_);
      if (obj->is_signaled()) {
        maybe_acquire(*obj);

        guard.unlock();

        // Clean up the already enqueued wait blocks
        for (auto &wb : table.first(i))
          wb_dequeue(wb);

        {
          auto tguard = frg::guard(&thread.lock_);
          thread.wait_phase_ = WaitPhase::None;
        }

        return i;
      } else {
        obj->wait_list_.push_back(&table[i]);
        obj->wait_count_++;
      }
    }
  } else {
    panic("WaitType::All not implemented!");
  }

  if (has_timeout)
    set_timeout(thread, *timeout);

  thread.lock_.lock();

  if (thread.wait_phase_ == WaitPhase::Aborted) {
    int wait_status = thread.wait_status_;

    thread.lock_.unlock();

    if (has_timeout) {
      // thread.timeout_timer_.uninstall();
      wb_dequeue(thread.timeout_waitblock_);
    }

    for (auto &wb : table)
      wb_dequeue(wb);

    thread.wait_phase_ = WaitPhase::None;

    return decode_wait_status(wait_status);
  }

  // do_wait takes the lock ownership for thread.lock_ here.
  // the IplGuard will lower the ipl here, too.
  return do_wait(thread, has_timeout);
}

} // namespace yak
