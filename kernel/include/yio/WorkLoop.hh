#pragma once

#include <yakpp/SpinLock.hh>
#include <yakpp/Mutex.hh>
#include <yakpp/Event.hh>
#include <yakpp/Object.hh>
#include <yakpp/Thread.hh>

namespace yak::io
{

class WorkLoop : Object {
	IO_OBJ_DECLARE(WorkLoop);

    public:
	void init() override;

	static WorkLoop *workLoop();

	void wake();

	void threadMain();

	bool onThread();

    private:
	SpinLock queueLock_;

	Event wakeEvent_;
	Thread *workThread_;
};

}
