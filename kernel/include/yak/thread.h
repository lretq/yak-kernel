#pragma once

#include <frg/list.hpp>
#include <frg/mutex.hpp>
#include <frg/string.hpp>
#include <span>
#include <yak/arch-pcb.h>
#include <yak/sched_prio.h>
#include <yak/spinlock.h>
#include <yak/wait.h>
#include <yak/waitblock.h>

namespace yak {
enum class ThreadState {
  // freshly created
  Undefined,
  // enqueued on runqueue
  Queued,
  // off-list, set as next thread
  WaitingForSwitch,
  // off-list, currently switching to this thread
  Switching,
  // off-list, currently active
  Running,
  // off-list, block until wakeup
  Blocked,
  // off-list, terminating
  Terminating,
  // enqueued on reaper queue
  Dead
};

// This implementation is derived from microsoft's channel9:
// "Inside Windows 7: Arun Kishan - Farewell to the Windows Kernel
// Dispatcher Lock"
enum class WaitPhase {
  None,
  // Thread currently setting up wait machinery
  InProgress,
  // Thread comitted to waiting
  Committed,
  // The wait was aborted whilst InProgress
  Aborted,
};

class Process;
struct CpuData;

static constexpr size_t THREAD_MAX_NAME_LEN = 32;
static constexpr size_t THREAD_INLINE_WAIT_BLOCKS = 4;

using ThreadEntryFn = void (*)(void *, void *);

extern "C" void sched_finalize_switch(Thread *current, Thread *next);

class Thread {
  friend class Scheduler;
  friend WaitResult
  wait_for_impl(std::span<KObject *> objects, WaitMode mode, WaitType type,
                std::optional<TimeNs> timeout,
                std::optional<std::span<WaitBlock>> table_opt);
  friend void sched_finalize_switch(Thread *current, Thread *next);

public:
  enum class Type {
    KernelThread,
    UserThread,
    IdleThread,
  };

  static Thread *current();

  [[noreturn]]
  static void exit_current();

  Thread(frg::string_view name, SchedPrio initial_priority,
         Process *parent_process, Type thread_type = Type::KernelThread);

  void init_context(void *kstack_top, ThreadEntryFn entry, void *ctx1,
                    void *ctx2);

  void unwait(WaitResult res);

  bool is_kernel() const {
    return thread_type_ != Thread::Type::KernelThread;
  }

  bool is_user() const {
    return thread_type_ == Thread::Type::UserThread;
  }

  bool is_idle() const {
    return thread_type_ == Thread::Type::IdleThread;
  }

  auto lock_guard() {
    return frg::guard(&lock_);
  }

public:
  arch::ThreadPcb md;

private:
  Spinlock lock_;

public:
  ThreadState state_;

  SchedPrio base_priority_;
  SchedPrio priority_;

  Process *parent_process_;
  Process *effective_process_;

  CpuData *affinity_cpu_ = nullptr;
  CpuData *last_cpu_ = nullptr;

  void *kernel_stack_top_ = nullptr;

  std::atomic<bool> is_switching_;

  char name_[THREAD_MAX_NAME_LEN];

  WaitBlock inline_waitblocks_[THREAD_INLINE_WAIT_BLOCKS];
  WaitBlock timeout_waitblock_;

  WaitPhase wait_phase_;
  WaitResult wait_status_;
  std::span<WaitBlock> wait_blocks_;

  frg::default_list_hook<Thread> list_hook;
  frg::default_list_hook<Thread> queue_hook;

private:
  const Type thread_type_;
};

using ThreadList = frg::intrusive_list<
    Thread, frg::locate_member<Thread, frg::default_list_hook<Thread>,
                               &Thread::list_hook>>;

using ThreadQueue = frg::intrusive_list<
    Thread, frg::locate_member<Thread, frg::default_list_hook<Thread>,
                               &Thread::queue_hook>>;

} // namespace yak
