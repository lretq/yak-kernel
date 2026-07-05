#pragma once

#include <frg/manual_box.hpp>
#include <yak/runqueue.h>
#include <yak/thread.h>
#include <yak/types.h>

namespace yak {

extern "C" void sched_finalize_switch(Thread *current, Thread *next);

class Scheduler {
  friend class Thread;

public:
  Scheduler(CpuData *cpu, Thread *idle_thread);

  ~Scheduler() = delete;
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;
  Scheduler(Scheduler &&other) noexcept = delete;
  Scheduler &operator=(Scheduler &&other) noexcept = delete;

  void resume_locked(Thread *thread);
  void resume(Thread *thread);

  void yield(Thread *current);

  void commit_reschedule();

private:
  void do_switch(Thread *from, Thread *to);
  void reinsert(Thread *thread, SchedPrio cur_prio, bool remote);
  void insert(Thread *thread, bool remote);
  Thread *select_next(SchedPrio priority);

  Spinlock lock_;

  RunQueue rr_queue_;
  ThreadQueue idle_queue_;

  CpuData *sched_cpu_;

  Thread *idle_thread_;
  Thread *next_thread_;
};

} // namespace yak
