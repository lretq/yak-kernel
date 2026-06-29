#pragma once

#include <yak/runqueue.h>
#include <yak/thread.h>
#include <yak/types.h>

namespace yak {

class Scheduler {
  friend bool Thread::is_idle();

public:
  static void init(CpuData *cpu, Thread *idle_thread);

  Scheduler(CpuData *cpu, Thread *idle_thread)
      : sched_cpu_(cpu),
        idle_thread_(idle_thread) {}

  void insert(Thread *thread, bool remote);

  void resume_locked(Thread *thread);
  void resume(Thread *thread);

  void yield(Thread *current);

  Thread *select_next(SchedPrio priority);

  void commit_reschedule();

  [[noreturn]]
  void idle_loop();

private:
  Spinlock lock_;

  RunQueue rr_queue_;
  ThreadQueue idle_queue_;

  CpuData *sched_cpu_;

  Thread *idle_thread_;
  Thread *next_thread_;
};

} // namespace yak
