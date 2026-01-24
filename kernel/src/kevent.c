#include <yak/object.h>
#include <yak/kevent.h>

void event_init(struct kevent *event, int sigstate)
{
	kobject_init(&event->hdr, sigstate >= 1 ? 1 : 0);
}

void event_alarm(struct kevent *event)
{
	ipl_t ipl = spinlock_lock(&event->hdr.obj_lock);

	if (event->hdr.signalstate > 0) {
		goto exit;
	}

	if (event->hdr.waitcount) {
		// wake up all waiters
		kobject_signal_locked(&event->hdr, true);
	}

	event->hdr.signalstate = 1;

exit:
	spinlock_unlock(&event->hdr.obj_lock, ipl);
}
