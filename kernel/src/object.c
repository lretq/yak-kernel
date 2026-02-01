#include <assert.h>
#include <yak/sched.h>
#include <yak/log.h>
#include <yak/queue.h>
#include <yak/object.h>

void kobject_init(struct kobject *hdr, int signalstate,
		  enum kobject_type type)
{
	spinlock_init(&hdr->obj_lock);
	hdr->obj_type = type;
	hdr->obj_signal_count = signalstate;
	hdr->obj_wait_count = 0;
	TAILQ_INIT(&hdr->obj_wait_list);
}

int kobject_signal_locked(struct kobject *hdr, bool unblock_all)
{
	assert(spinlock_held(&hdr->obj_lock));
	struct wait_block *wb;

	int unblocked = 0;

	while (hdr->obj_wait_count) {
		wb = TAILQ_FIRST(&hdr->obj_wait_list);
		assert(wb);

		struct kthread *thread = wb->thread;

		spinlock_lock_noipl(&thread->thread_lock);

		wb->flags |= WB_DEQUEUED;

		TAILQ_REMOVE(&hdr->obj_wait_list, wb, entry);

		hdr->obj_wait_count -= 1;

		if (wb->flags & WB_UNWAITED) {
			// already unwaited elsewhere
			spinlock_unlock_noipl(&thread->thread_lock);
			continue;
		}

		thread_unwait(thread, wb->status);

		spinlock_unlock_noipl(&thread->thread_lock);

		if (!unblock_all) {
			return 1;
		}

		++unblocked;
	}

	return unblocked;
}
