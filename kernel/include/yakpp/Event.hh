#pragma once

#include <yakpp/meta.hh>
#include <yak/kevent.h>
#include <yakpp/Object.hh>

namespace yak
{

class Event : Object {
	IO_OBJ_DECLARE(Event);

    private:
	void init() override;

    public:
	enum Type {
		kEventSync,
		kEventNotif,
	};

	Event(const Event &) = delete;
	Event &operator=(const Event &) = delete;
	Event(Event &&other) noexcept = delete;
	Event &operator=(Event &&other) noexcept = delete;

	void init(bool startSignalled, Event::Type type);

	void alarm(bool wakeAll);

	void clear();

	void wait();

	struct kevent *get()
	{
		return &kevent_;
	}

    private:
	struct kevent kevent_;
};

}
