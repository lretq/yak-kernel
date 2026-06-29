#include <algorithm>
#include <span>
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
               Process *parent_process, bool user_thread)
    : state_{ThreadState::Undefined},
      priority_{initial_priority},
      parent_process_{parent_process},
      effective_process_{parent_process},
      is_user_{user_thread},
      wait_phase_{WaitPhase::None} {
  auto name_copy_len = std::min(name.size(), THREAD_MAX_NAME_LEN - 1);
  memcpy(name_, name.data(), name_copy_len);
}

Thread *Thread::Current() {
  return CPUDATA_LOAD(current_thread);
}

[[noreturn]]
void Thread::exit() {
  iplr(Ipl::dispatch);

  // XXX: switch to kernel map?
  if (is_user_)
    panic("switch");

  lock_.lock();

  pr_warn("add a thread reaper!\n");

  state_ = ThreadState::Terminating;

  CpuData::local_scheduler().yield(this);
  __builtin_unreachable();
}

extern "C" [[noreturn]] void __thread_exit_trampoline() {
  Thread::Current()->exit();
}

void Thread::unwait(int wait_status) {
  assert(lock_.is_locked());
  assert(state_ == ThreadState::Blocked ||
         wait_phase_ == WaitPhase::InProgress);

  // If we did not commit to the wait yet, abort
  if (wait_phase_ == WaitPhase::InProgress)
    wait_phase_ = WaitPhase::Aborted;

  wait_status_ = wait_status;

  // Set all wait blocks to unwaited
  for (auto &wb : wait_blocks_)
    wb.flags_ |= WB_UNWAITED;

  timeout_waitblock_.flags_ |= WB_UNWAITED;

  // Unblock the thread
  if (state_ == ThreadState::Blocked) {
    CpuData::local_scheduler().resume_locked(this);
  }
}

bool Thread::is_idle() {
  return last_cpu_->sched->idle_thread_ == this;
}

} // namespace yak
