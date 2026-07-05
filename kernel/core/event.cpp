#include <frg/mutex.hpp>
#include <yak/event.h>
#include <yak/ipl-guard.h>
#include <yak/kobject.h>
#include <yak/panic.h>

namespace yak {
namespace {
KObjectType ev_type_to_obj(Event::Type type) {
  switch (type) {
  case Event::Type::Notify:
    return KObjectType::Notify;
  case Event::Type::Synch:
    return KObjectType::Sync;
  default:
    panic("unknown event type");
  }
}
} // namespace

Event::Event(bool initial_state, Event::Type event_type)
    : KObject(initial_state ? 1 : 0, ev_type_to_obj(event_type)) {}

void Event::alarm(bool wake_all) {
  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&lock_);

  // Already signaled
  if (signal_count_ != 0)
    return;

  if (type_ == KObjectType::Notify) {
    // Notify (Manual-Reset): Stay signaled until clear()'d
    // Always wakes everyone up, ignoring wake_all
    signal_count_ = 1;
    if (wait_count_ > 0) {
      signal_locked(true);
    }
  } else {
    // Sync (Auto-Reset): Only sets signal if no one is waiting
    if (wait_count_ == 0) {
      signal_count_ = 1;
    } else {
      // Threads are waiting; wake them up and consume the signal immediately
      signal_locked(wake_all);
    }
  }
}

void Event::clear() {
  IplGuard ipl{Ipl::dispatch};
  auto guard = frg::guard(&lock_);

  signal_count_ = 0;
}
} // namespace yak
