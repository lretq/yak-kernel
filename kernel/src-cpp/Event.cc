#include "yak/wait.h"
#include <yak/kevent.h>
#include <yakpp/meta.hh>
#include <yakpp/Event.hh>

namespace yak
{

IO_OBJ_DEFINE(Event, Object);
#define super Object
#define Self Event

void Event::init()
{
	super::init();
}

void Event::init(bool startSignalled, Event::Type type)
{
	Self::init();

	int flags = 0;
	if (type == kEventNotif)
		flags |= KEVENT_NOTIF;

	event_init(&kevent_, startSignalled, flags);
}

void Event::alarm(bool wakeAll)
{
	event_alarm(&kevent_, wakeAll);
}

void Event::clear()
{
	event_clear(&kevent_);
}

void Event::wait()
{
	EXPECT(sched_wait(&kevent_, WAIT_MODE_BLOCK, TIMEOUT_INFINITE));
}

}
