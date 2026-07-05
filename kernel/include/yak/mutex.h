#pragma once

#include <cstdint>
#include <yak/event.h>
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
  Event mutex_wake_;
  Spinlock lock_;
  Thread *owner;
};

} // namespace yak
