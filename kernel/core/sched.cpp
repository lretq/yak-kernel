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
void Scheduler::init(CpuData *cpu, Thread *idle_thread) {
  idle_thread->state_ = ThreadState::Running;
  cpu->current_thread = idle_thread;
  cpu->kernel_stack_top = idle_thread->kernel_stack_top_;

  cpu->sched.initialize(cpu, idle_thread);

  auto sched = cpu->sched.get();
  sched->idle_thread_ = idle_thread;
}

// Both scheduler and thread shall be locked upon entry
void Scheduler::insert(Thread *thread, bool remote) {
  while (true) {
    thread->last_cpu_ = sched_cpu_;
    thread->state_ = ThreadState::Queued;

    auto current = next_thread_ ? next_thread_ : sched_cpu_->current_thread;

    if (sched_prio::is_real_time(thread->priority_)) {
      if (thread->priority_ <= current->priority_) {
        // thread's priority is not high enough to preempt
        rr_queue_.insert(thread);
        return;
      }
    } else {
      // Check if current is either
      // 1) the idle thread
      // 2) a thread of the Idle class
      bool can_preempt = (current->priority_ == SchedPrio::Idle &&
                          thread->priority_ > SchedPrio::Idle);
      if (!can_preempt) {
        // Anything not real time prio cannot preempt anything
        if (sched_prio::is_time_share(thread->priority_)) {
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

    thread->state_ = ThreadState::WaitingForSwitch;

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
  assert(thread->lock_.is_locked());
  assert(iplget() == Ipl::dispatch);

  auto cpu = thread->affinity_cpu_;
  if (cpu == nullptr) {
    cpu = CpuData::Current();
  }

  auto guard = frg::guard(&lock_);
  insert(thread, cpu != CpuData::Current());
}

void Scheduler::resume(Thread *thread) {
  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&thread->lock_);
  resume_locked(thread);
}

static inline void wait_for_switch(Thread *thread) {
  while (thread->is_switching_.load(std::memory_order_acquire))
    busyloop_hint();
}

extern "C" [[gnu::no_instrument_function]]
void sched_finalize_switch(Thread *current, Thread *next) {
  current->is_switching_.store(false, std::memory_order_release);
  current->lock_.unlock();
  next->state_ = ThreadState::Running;
}

[[gnu::no_instrument_function]]
static void do_switch(Thread *current, Thread *thread) {
  assert(iplget() == Ipl::dispatch);
  assert(current && thread);
  assert(current != thread);
  assert(current->lock_.is_locked());
  // The thread lock can be locked legally:
  // if we come from shed_yield and we have waited for is_switching = false
  // successfully, the spinlock is unlocked only afterwards

  assert(current->state_ != ThreadState::Terminating ||
         thread->state_ != ThreadState::Undefined ||
         current->state_ != ThreadState::Blocked);

  current->affinity_cpu_ = CpuData::Current();

  if (thread->is_user_) {
    if (current->effective_process_ != thread->effective_process_) {
      panic("activate other user thread process map");
    }
  }

  CPUDATA_STORE(current_thread, thread);
  CPUDATA_STORE(kernel_stack_top, thread->kernel_stack_top_);

  arch::sched_switch(current, thread);

  // we should be back now
  assert(current == CPUDATA_LOAD(current_thread));
  assert(current->state_ != ThreadState::Terminating);
}

void Scheduler::commit_reschedule() {
  assert(iplget() == Ipl::dispatch);

  auto guard = frg::guard(&lock_);

  auto next = next_thread_;
  if (next == nullptr) {
    return;
  }

  next_thread_ = nullptr;
  next->state_ = ThreadState::Switching;

  guard.unlock();

  wait_for_switch(next);

  auto current = CPUDATA_LOAD(current_thread);
  current->lock_.lock();

  // in the process of switching off the stack
  current->is_switching_.store(true, std::memory_order_relaxed);

  if (current != idle_thread_) {
    auto sguard = frg::guard(&lock_);
    insert(current, false);
  } else {
    // the idle thread remains in a ready state
    current->state_ = ThreadState::Queued;
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
      next->state_ = ThreadState::Switching;
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

#if !YAK_HOSTED_MODE
[[noreturn]]
void Scheduler::idle_loop() {
  iplx(Ipl::passive);

  while (true) {
    assert(iplget() == Ipl::passive);

    pr_debug("core %zu is idle!\n", CPUDATA_LOAD(id));

    arch::enable_interrupts();

#if defined x86_64
    // TODO: implement monitor/mwait, proper idle driver?
    arch::interrupt_wait();
#elif defined riscv64
    arch::interrupt_wait();
#else
#error "Port idle_loop to this architecture!"
#endif
  }
}
#endif

} // namespace yak
