#include <atomic>
#include <frg/mutex.hpp>
#include <yak/event.h>
#include <yak/ipl-guard.h>
#include <yak/mutex.h>
#include <yak/spinlock.h>
#include <yak/thread.h>
#include <yak/wait.h>

namespace yak {
Mutex::Mutex() : mutex_wake_(false, Event::Type::Synch) {}

[[gnu::hot]]
bool Mutex::try_lock() {
  Thread *expected = nullptr;

  return owner.compare_exchange_strong(expected, Thread::current(),
                                       std::memory_order_acq_rel,
                                       std::memory_order_relaxed);
}

void Mutex::slow_lock(Thread *current) {
  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&wait_lock_);

  while (true) {
    Thread *o = owner.load(std::memory_order_acquire);

    if (!o) {
      if (try_lock()) {
        ++current->locks_held;
        return;
      }

      continue;
    }

    {
      auto owner_guard = o->lock_guard();
      if (owner.load(std::memory_order_acquire) != o)
        continue;
      if (o->priority < current->priority)
        o->set_priority_locked(current->priority);
    }

    guard.unlock();
    wait_for_single(mutex_wake_, WaitMode::Block);
    guard.lock();
  }
}

[[gnu::hot]]
void Mutex::lock() {
  auto current = Thread::current();

  if (current->locks_held == 0)
    current->stashed_priority = current->priority;

  if (try_lock())
    return;

  for (int spin = 0; spin < 20; ++spin) {
    if (try_lock())
      return;
    busyloop_hint();
  }

  slow_lock(current);
}

void Mutex::unlock() {
  auto current = Thread::current();
  assert(owner.load(std::memory_order_relaxed) == current);

  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&wait_lock_);

  owner.store(nullptr, std::memory_order_release);

  if (--current->locks_held == 0) {
    // Lower priority back to stashed priority
    current->set_priority(current->stashed_priority);
  }

  mutex_wake_.alarm(true);
}

} // namespace yak
