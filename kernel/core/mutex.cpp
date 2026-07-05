#include <frg/mutex.hpp>
#include <yak/event.h>
#include <yak/ipl-guard.h>
#include <yak/mutex.h>
#include <yak/spinlock.h>
#include <yak/thread.h>
#include <yak/wait.h>

namespace yak {
Mutex::Mutex() : mutex_wake_(false, Event::Type::Synch) {}

bool Mutex::try_lock() {
  IplGuard ipl{Ipl::dispatch};

  auto guard = frg::guard(&lock_);

  if (owner == nullptr) {
    owner = Thread::current();
    return true;
  }
  return false;
}

void Mutex::lock() {
  IplGuard ipl{Ipl::dispatch};
  auto current = Thread::current();

  if (lock_.try_lock() && owner->state == ThreadState::Running) {
    lock_.unlock();

    for (int spin = 0; spin < 100; ++spin) {
      if (try_lock()) {
        return;
      }
      busyloop_hint();
    }
  }

  auto guard = frg::guard(&lock_);
  while (owner != nullptr) {
    {
      auto owner_guard = owner->lock_guard();
      if (owner->priority < current->priority) {
        // We have to boost the owner's priority
        // Stash the owner's current priority if the first in chain!
        owner->set_priority_locked(current->priority);
      }
    }
    guard.unlock();
    auto _ = wait_for_single(mutex_wake_, WaitMode::Block);
    guard.lock();
  }

  owner = current;

  if (current->locks_held++ == 0) {
    current->stashed_priority = current->priority;
  }
}

void Mutex::unlock() {
  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&lock_);

  owner = nullptr;
  mutex_wake_.alarm(true);

  auto current = Thread::current();
  if (--current->locks_held == 0) {
    // Lower priority back to stashed priority
    current->set_priority(current->stashed_priority);
  }
}

} // namespace yak
