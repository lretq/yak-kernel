#pragma once

#include <yak/kobject.h>
#include <yak/types.h>

namespace yak {

class Event : public KObject {
public:
  enum class Type {
    Notify,
    Synch,
  };

  Event(bool initial_state, Event::Type event_type);

  void alarm(bool wake_all = false);
  void clear();
};
} // namespace yak
