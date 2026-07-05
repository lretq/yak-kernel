#include <algorithm>
#include <string.h>
#include <yak/assert.h>
#include <yak/cpudata.h>
#include <yak/ipl.h>
#include <yak/log.h>
#include <yak/panic.h>
#include <yak/sched.h>
#include <yak/thread.h>
#include <yak/waitblock.h>

namespace yak {
Thread::Thread(frg::string_view name, SchedPrio initial_priority,
               Process *parent_process, Thread::Type thread_type)
    : state_{ThreadState::Undefined},
      base_priority_{initial_priority},
      priority_{base_priority_},
      parent_process_{parent_process},
      effective_process_{parent_process},
      wait_phase_{WaitPhase::None},
      thread_type_{thread_type} {
  auto name_copy_len = std::min(name.size(), THREAD_MAX_NAME_LEN - 1);
  memcpy(name_, name.data(), name_copy_len);
}

Thread *Thread::current() {
  return CPUDATA_LOAD(current_thread);
}

[[noreturn]]
void Thread::exit_current() {
  iplr(Ipl::dispatch);

  auto current = Thread::current();

  // XXX: switch to kernel map?
  if (current->is_user())
    panic("user switch");

  current->lock_.lock();

  pr_warn("add a thread reaper!\n");

  current->state_ = ThreadState::Terminating;

  CpuData::local_scheduler().yield(current);
  __builtin_unreachable();
}

extern "C" [[noreturn]] void __thread_exit_trampoline() {
  Thread::exit_current();
}

void Thread::unwait(WaitResult res) {
  assert(lock_.is_locked());
  assert(state_ == ThreadState::Blocked ||
         wait_phase_ == WaitPhase::InProgress);

  // If we did not commit to the wait yet, abort
  if (wait_phase_ == WaitPhase::InProgress)
    wait_phase_ = WaitPhase::Aborted;

  wait_status_ = res;

  // Set all wait blocks to unwaited
  for (auto &wb : wait_blocks_)
    wb.flags_ |= WB_UNWAITED;

  timeout_waitblock_.flags_ |= WB_UNWAITED;

  // Unblock the thread
  if (state_ == ThreadState::Blocked)
    CpuData::local_scheduler().resume_locked(this);
}

} // namespace yak
