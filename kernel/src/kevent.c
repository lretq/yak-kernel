#include <yak/object.h>
#include <yak/kevent.h>

void event_init(struct kevent *event, bool sigstate, int flags)
{
	enum kobject_type type = OBJ_SYNC;
	if (flags & KEVENT_NOTIF)
		type = OBJ_NOTIF;

	kobject_init(&event->hdr, sigstate ? 1 : 0, type);
}

void event_alarm(struct kevent *event, bool wake_all)
{
	ipl_t ipl = spinlock_lock(&event->hdr.obj_lock);

	if (event->hdr.obj_signal_count == 0) {
		if (event->hdr.obj_wait_count) {
			if (event->hdr.obj_type == OBJ_SYNC) {
				// Wake a single waiter, signal is consumed
				kobject_signal_locked(&event->hdr, wake_all);
				// (remain at signal_count=0)
			} else {
				// OBJ_NOTIF: wake all waiters (manual-reset)
				kobject_signal_locked(&event->hdr, true);
				// obj_signal_count remains 1
				event->hdr.obj_signal_count = 1;
			}
		} else {
			// no waiters: mark the event signaled
			event->hdr.obj_signal_count = 1;
		}
	}

	spinlock_unlock(&event->hdr.obj_lock, ipl);
}

void event_clear(struct kevent *event)
{
	ipl_t ipl = spinlock_lock(&event->hdr.obj_lock);
	event->hdr.obj_signal_count = 0;
	spinlock_unlock(&event->hdr.obj_lock, ipl);
}
