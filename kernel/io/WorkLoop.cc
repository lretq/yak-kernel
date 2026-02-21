#include <yak/wait.h>
#include <yak/sched.h>
#include <yakpp/meta.hh>
#include <yakpp/Event.hh>
#include <yio/WorkLoop.hh>
#include <yak/log.h>

namespace yak::io
{

IO_OBJ_DEFINE(WorkLoop, Object);
#define super Object

void WorkLoop::threadMain()
{
	while (true) {
		wakeEvent_.wait();
	}
}

static void enterWorkLoopMain(void *ctx)
{
	auto loop = static_cast<WorkLoop *>(ctx);
	loop->threadMain();
	sched_exit_self();
}

void WorkLoop::init()
{
	wakeEvent_.init(false, Event::kEventSync);

	kernel_thread_create("workloop", SCHED_PRIO_TIME_SHARE_END,
			     enterWorkLoopMain, this, true, &wlThread_);
}

WorkLoop *WorkLoop::workLoop()
{
	WorkLoop *wl;
	ALLOC_INIT(wl, WorkLoop);
	return wl;
}

}
