#pragma once

#include <atomic>
#include <yak/event.h>
#include <yak/spinlock.h>
#include <yak/thread.h>

namespace yak {

class Mutex {
public:
  Mutex();

  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  Mutex(Mutex &&other) noexcept = delete;
  Mutex &operator=(Mutex &&other) noexcept = delete;

  void lock();
  void unlock();
  bool try_lock();

private:
  void slow_lock(Thread *current);

  std::atomic<Thread *> owner;
  Event mutex_wake_;
  Spinlock wait_lock_;
};

} // namespace yak
