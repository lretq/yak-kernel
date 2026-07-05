/*
https://web.cs.ucdavis.edu/~roper/ecs150/ULE.pdf

From my rough understanding we have three classes:
- idle
- time-shared
- real-time

We have three different queues: idle, current, next

idle queue is only ran once we don't have any thread whatsover on any other
queue time-shared threads may not preempt any other thread real-time threads may
preempt lower priority threads

Threads may be deemed interactive and end up on current queues too / handled as
realtime threads

i'll implement everything very primitively. interactivity and other features
will follow later on :^)
*/

#include <frg/mutex.hpp>
#include <yak/arch-intr.h>
#include <yak/arch.h>
#include <yak/assert.h>
#include <yak/cpudata.h>
#include <yak/ipl-guard.h>
#include <yak/ipl.h>
#include <yak/log.h>
#include <yak/panic.h>
#include <yak/percpu.h>
#include <yak/ps.h>
#include <yak/sched.h>
#include <yak/sched_prio.h>
#include <yak/softint.h>
#include <yak/thread.h>

#ifdef x86_64
#include <x86_64/cpu_features.h>
#endif

namespace yak {
Scheduler::Scheduler(CpuData *cpu, Thread *idle_thread)
    : sched_cpu_(cpu),
      idle_thread_(idle_thread) {}

void Scheduler::reinsert(Thread *thread, SchedPrio cur_prio, bool remote) {
  assert(thread->state == ThreadState::Queued);
  if (cur_prio == SchedPrio::Idle) {
    idle_queue_.erase(idle_queue_.iterator_to(thread));
  } else {
    rr_queue_.remove(thread);
  }
  insert(thread, remote);
}

// Both scheduler and thread shall be locked upon entry
void Scheduler::insert(Thread *thread, bool remote) {
  while (true) {
    thread->current_cpu = sched_cpu_;
    thread->state = ThreadState::Queued;

    auto current = next_thread_ ? next_thread_ : sched_cpu_->current_thread;

    if (sched_prio::is_real_time(thread->priority)) {
      if (thread->priority <= current->priority) {
        // thread's priority is not high enough to preempt
        rr_queue_.insert(thread);
        return;
      }
    } else {
      // Check if current is either
      // 1) the idle thread
      // 2) a thread of the Idle class
      bool can_preempt = (current->priority == SchedPrio::Idle &&
                          thread->priority > SchedPrio::Idle);
      if (!can_preempt) {
        // Anything not real time prio cannot preempt anything
        if (sched_prio::is_time_share(thread->priority)) {
          // TODO: advanced insertion stuff
          // do not starve the time share threads completely and so on
          rr_queue_.insert(thread);
        } else {
          idle_queue_.push_back(thread);
        }

        return;
      }
    }

    // We can preempt the currently running thread!

    thread->state = ThreadState::WaitingForSwitch;

    // nullptr if none
    auto evicted = next_thread_;

    next_thread_ = thread;

    if (evicted) {
      // reinsert the old thread
      thread = evicted;
      // go through the whole thing again
      // we merely replaced the next thread
      // so the loop will simply return
      continue;
    }

    // now! we can finally preempt the currently running thread!
    if (remote) {
      softint_issue_other(sched_cpu_, Ipl::dispatch);
    } else {
      softint_issue(Ipl::dispatch);
    }

    return;
  }
}

Thread *Scheduler::select_next(SchedPrio min_priority) {
  if (!rr_queue_.empty()) {
    auto ceil = rr_queue_.priority_ceil();
    if (min_priority > ceil)
      return nullptr;
    auto t = rr_queue_.pop(ceil);
    assert(t);
    return t;
  } else if (!idle_queue_.empty()) {
    return idle_queue_.pop_front();
  }

  return nullptr;
}

void Scheduler::resume_locked(Thread *thread) {
  assert(iplget() == Ipl::dispatch);
  assert(thread->lock_.is_locked());

  auto cpu = thread->affinity_cpu;
  if (cpu == nullptr) {
    cpu = CpuData::current();
  }

  auto guard = frg::guard(&lock_);
  insert(thread, cpu != CpuData::current());
}

void Scheduler::resume(Thread *thread) {
  IplGuard ipl{Ipl::dispatch};
  auto guard = thread->lock_guard();
  resume_locked(thread);
}

static inline void wait_for_switch(Thread *thread) {
  while (thread->is_switching.load(std::memory_order_acquire))
    busyloop_hint();
}

extern "C" [[gnu::no_instrument_function]]
void sched_finalize_switch(Thread *current, Thread *next) {
  current->is_switching.store(false, std::memory_order_release);
  current->lock_.unlock();
  next->state = ThreadState::Running;
}

[[gnu::no_instrument_function]]
void Scheduler::do_switch(Thread *current, Thread *thread) {
  assert(iplget() == Ipl::dispatch);
  assert(current && thread);
  assert(current != thread);
  assert(current->lock_.is_locked());
  // The thread lock can be locked legally:
  // if we come from shed_yield and we have waited for is_switching = false
  // successfully, the spinlock is unlocked only afterwards

  assert(current->state != ThreadState::Terminating ||
         thread->state != ThreadState::Undefined ||
         current->state != ThreadState::Blocked);

  current->affinity_cpu = CpuData::current();

  if (thread->is_user()) {
    if (current->effective_process != thread->effective_process) {
      panic("activate other user thread process map");
    }
  }

  CPUDATA_STORE(current_thread, thread);
  CPUDATA_STORE(kernel_stack_top, thread->kernel_stack_top);

  arch::sched_switch(current, thread);

  // we should be back now
  assert(current == CPUDATA_LOAD(current_thread));
  assert(current->state != ThreadState::Terminating);
}

void Scheduler::commit_reschedule() {
  assert(iplget() == Ipl::dispatch);

  auto guard = frg::guard(&lock_);

  auto next = next_thread_;
  if (next == nullptr) {
    return;
  }

  // no going back now
  next_thread_ = nullptr;
  next->state = ThreadState::Switching;

  guard.unlock();

  wait_for_switch(next);

  auto current = CPUDATA_LOAD(current_thread);
  current->lock_.lock();

  // in the process of switching off the stack
  current->is_switching.store(true, std::memory_order_relaxed);

  if (current != idle_thread_) {
    auto sguard = frg::guard(&lock_);
    insert(current, false);
  } else {
    // the idle thread always remains in a ready state
    current->state = ThreadState::Queued;
  }

  // will unlock current
  do_switch(current, next);
}

void Scheduler::yield(Thread *current) {
  assert(current);
  assert(current->lock_.is_locked());

  Thread *next;

  {
    auto guard = frg::guard(&lock_);

    // anything is fine now
    next = next_thread_;
    if (next) {
      next_thread_ = nullptr;
      next->state = ThreadState::Switching;
    } else {
      next = select_next(SchedPrio{0});
    }
  }

  if (next) {
    wait_for_switch(next);
    do_switch(current, next);
  } else {
    do_switch(current, idle_thread_);
  }
}
} // namespace yak
