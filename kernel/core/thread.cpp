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
Thread::Thread(frg::string_view thread_name, SchedPrio initial_priority,
               Process *parent_process, Thread::Type thread_type)
    : state{ThreadState::Undefined},
      base_priority{initial_priority},
      priority{initial_priority},
      parent_process{parent_process},
      effective_process{parent_process},
      wait_phase{WaitPhase::None},
      thread_type_{thread_type} {
  auto name_copy_len = std::min(thread_name.size(), THREAD_MAX_NAME_LEN - 1);
  memcpy(name, thread_name.data(), name_copy_len);
  name[THREAD_MAX_NAME_LEN - 1] = '\0';
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

  pr_warn("add a thread reaper! Thread <%s> exited!\n", current->name);

  current->state = ThreadState::Terminating;

  CpuData::local_scheduler().yield(current);
  __builtin_unreachable();
}

extern "C" [[noreturn]] void __thread_exit_trampoline() {
  Thread::exit_current();
}

void Thread::unwait(WaitResult res) {
  assert(lock_.is_locked());
  assert(state == ThreadState::Blocked || wait_phase == WaitPhase::InProgress);

  // If we did not commit to the wait yet, abort
  if (wait_phase == WaitPhase::InProgress)
    wait_phase = WaitPhase::Aborted;

  wait_status = res;

  // Set all wait blocks to unwaited
  for (auto &wb : wait_blocks)
    wb.flags_ |= WB_UNWAITED;

  timeout_waitblock.flags_ |= WB_UNWAITED;

  // Unblock the thread
  if (state == ThreadState::Blocked)
    CpuData::local_scheduler().resume_locked(this);
}

} // namespace yak
