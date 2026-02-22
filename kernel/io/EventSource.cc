#include <yio/EventSource.hh>
#include <yakpp/meta.hh>
#include <yakpp/Object.hh>

namespace yak::io
{

IO_OBJ_DEFINE(EventSource, Object);
#define super Object

void EventSource::init()
{
	super::init();

	owner = nullptr;
	workLoop = nullptr;
	action = nullptr;
}

void EventSource::init(Object *owner, EventSource::Action action)
{
	this->owner = owner;
	owner->retain();

	this->action = action;
}

void EventSource::deinit()
{
	if (owner) {
		owner->release();
		owner = nullptr;
	}
}

bool EventSource::checkForWork()
{
	return false;
}

void EventSource::signalWorkAvailable()
{
	if (!workLoop)
		return;
	workLoop->wake();
}

void EventSource::enable()
{
	enabled = true;
	signalWorkAvailable();
}

void EventSource::disable()
{
	enabled = false;
}

}
