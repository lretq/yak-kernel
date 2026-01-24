#include <assert.h>
#include <yak/sched.h>
#include <yak/log.h>
#include <yak/queue.h>
#include <yak/object.h>

void kobject_init(struct kobject_header *hdr, int signalstate)
{
	spinlock_init(&hdr->obj_lock);
	hdr->signalstate = signalstate;
	hdr->waitcount = 0;
	TAILQ_INIT(&hdr->wait_list);
}

int kobject_signal_locked(struct kobject_header *hdr, bool unblock_all)
{
	assert(spinlock_held(&hdr->obj_lock));
	struct wait_block *wb;

	size_t unblocked = 0;

	while (hdr->waitcount) {
		wb = TAILQ_FIRST(&hdr->wait_list);
#if 0
		if (!wb) {
			pr_warn("object (%p) has no entries but waitcount >0\n",
				hdr);
			hdr->waitcount = 0;
			break;
		}
#else
		assert(wb);
#endif
		TAILQ_REMOVE(&hdr->wait_list, wb, entry);

		hdr->waitcount -= 1;

		struct kthread *thread = wb->thread;
		spinlock_lock_noipl(&thread->thread_lock);
		sched_wake_thread(thread, wb->status);
		spinlock_unlock_noipl(&thread->thread_lock);

		if (!unblock_all) {
			return 1;
		}

		++unblocked;
	}

	return unblocked;
}
