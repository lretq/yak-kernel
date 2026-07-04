#pragma once

#include <yak/kobject.h>
#include <yak/types.h>

namespace yak {

class Event : public KObject {
    public:
    Event(bool initial_state, bool notify);

    void alarm(bool wake_all = false);
    void clear();
};
} // namespace yak
