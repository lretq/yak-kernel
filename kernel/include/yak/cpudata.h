#pragma once

#include <atomic>
#include <cstddef>
#include <frg/manual_box.hpp>
#include <yak/arch-cpudata.h>
#include <yak/dpc.h>
#include <yak/percpu.h>
#include <yak/sched.h>
#include <yak/thread.h>

namespace yak {

struct CpuData {
  CpuData *self;
  arch::ArchCpuData md;

  size_t id;
  bool bsp;

  unsigned int numa_domain;

  void *kernel_stack_top;

  Thread *current_thread;
  frg::manual_box<Scheduler> sched;

  frg::manual_box<DpcQueue> dpc_queue;

  std::atomic<uint32_t> softints_pending;

  static void initialize(CpuData *data);

  static inline CpuData *Current() {
    return CPUDATA_LOAD(self);
  }

  static inline bool OnBsp() {
    return CPUDATA_LOAD(bsp);
  }

  static inline Scheduler &local_scheduler() {
    return *Current()->sched.get();
  }

  void bootstrap_scheduler(Thread *idle_thread);
};

} // namespace yak
