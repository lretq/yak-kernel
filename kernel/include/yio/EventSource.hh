#pragma once

#include "yio/WorkLoop.hh"
#include <yakpp/meta.hh>
#include <yakpp/Object.hh>
#include <yakpp/Status.hh>

namespace yak::io
{
class EventSource : Object {
	IO_OBJ_DECLARE(EventSource);

    public:
	using Action = Status (*)(Object *owner, void *arg0, void *arg1,
				  void *arg2, void *arg3);

    private:
	void init() override;
	void deinit() override;

    public:
	virtual void init(Object *owner, EventSource::Action action = nullptr);

	virtual bool checkForWork();
	void signalWorkAvailable();

	void enable();
	void disable();

	WorkLoop *workLoop;
	Action action;
	Object *owner;
	bool enabled;
};
}
