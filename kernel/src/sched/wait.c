/*
* A threads wait cycle is two-phase:
*
* Running -> IN_PROGRESS (abortable) -> COMITTED -> Waiting
*
* If the wait is aborted before COMITTED,
* the thread never blocks (i.e. signal, APC, timeout).
*
* After reaching COMITTED, wakeup must dequeue wait blocks
*
* Only if a wait is alertable will user mode APCs be delivered.
*/

#include <assert.h>
#include <yak/log.h>
#include <yak/timer.h>
#include <yak/ipl.h>
#include <yak/spinlock.h>
#include <yak/hint.h>
#include <yak/sched.h>
#include <yak/object.h>
#include <yak/cpudata.h>
#include <yak/queue.h>
#include <yak/types.h>
#include <yak/status.h>

static inline bool is_obj_signaled(struct kobject *obj)
{
	return likely(obj->obj_signal_count > 0);
}

static inline void obj_maybe_acquire(struct kobject *obj)
{
	if (obj->obj_type == OBJ_SYNC) {
		obj->obj_signal_count -= 1;
	}
}

static void wb_dequeue(struct wait_block *wb)
{
	struct kobject *obj = wb->object;
	spinlock_lock_noipl(&obj->obj_lock);
	// signaling an object sets WB_DEQUEUED
	if ((wb->flags & WB_DEQUEUED) == 0) {
		TAILQ_REMOVE(&obj->obj_wait_list, wb, entry);
		obj->obj_wait_count -= 1;
	}
	spinlock_unlock_noipl(&obj->obj_lock);
}

static status_t do_wait(struct kthread *thread, bool has_timeout)
{
	// the idle thread must not wait!
	assert(thread != &curcpu_ptr()->idle_thread);

	thread->wait_phase = WAIT_PHASE_COMITTED;

	thread->status = THREAD_WAITING;

	sched_yield(thread, thread->last_cpu);
	assert(!spinlock_held(&thread->thread_lock));
	assert(curipl() == IPL_DPC);

	for (size_t i = 0; i < thread->wait_blocks_count; i++) {
		wb_dequeue(&thread->wait_blocks[i]);
	}

	if (has_timeout) {
		wb_dequeue(&thread->timeout_wait_block);
		// This might try to uninstall a non-queued timer,
		// but that is handled in timer_uninstall
		timer_uninstall(&thread->timeout_timer);
	}

	return thread->wait_status;
}

static void set_timeout(struct kthread *thread, nstime_t timeout)
{
	struct timer *ttim = &thread->timeout_timer;

	timer_reset(ttim);
	timer_install(ttim, timeout);

	// We cannot miss the timeout, we are at ipl = DPC
	// Also safety: only this thread ever changes
	// this timeout timer
	TAILQ_INSERT_TAIL(&ttim->hdr.obj_wait_list, &thread->timeout_wait_block,
			  entry);
	ttim->hdr.obj_wait_count = 1;
}

static status_t poll_many(struct kobject **objs, size_t count, nstime_t timeout)
{
	status_t ret = YAK_TIMEOUT;
	bool has_timeout = (timeout != POLL_ONCE) &&
			   (timeout != TIMEOUT_INFINITE);
	nstime_t deadline = 0;

	ipl_t ipl = ripl(IPL_DPC);

	// deadline after which we stop polling
	if (has_timeout)
		deadline = plat_getnanos() + timeout;

	do {
		if (has_timeout && plat_getnanos() >= deadline) {
			// polling took too long!
			break;
		}

		for (size_t i = 0; i < count; i++) {
			struct kobject *obj = objs[i];

			spinlock_lock_noipl(&obj->obj_lock);

			if (is_obj_signaled(obj)) {
				obj_maybe_acquire(obj);
				spinlock_unlock_noipl(&obj->obj_lock);
				ret = YAK_WAIT_SUCCESS + i;
				break;
			}

			spinlock_unlock_noipl(&obj->obj_lock);
		}

		busyloop_hint();
	} while (timeout != POLL_ONCE);

	xipl(ipl);
	return ret;
}

static status_t poll_single(struct kobject *obj, nstime_t timeout)
{
	status_t ret = YAK_TIMEOUT;
	bool has_timeout = (timeout != POLL_ONCE) &&
			   (timeout != TIMEOUT_INFINITE);
	nstime_t deadline = 0;

	ipl_t ipl = ripl(IPL_DPC);

	// deadline after which we stop polling
	if (has_timeout)
		deadline = plat_getnanos() + timeout;

	do {
		if (has_timeout && plat_getnanos() >= deadline) {
			// polling took too long!
			break;
		}

		spinlock_lock_noipl(&obj->obj_lock);

		if (is_obj_signaled(obj)) {
			obj_maybe_acquire(obj);
			spinlock_unlock_noipl(&obj->obj_lock);
			ret = YAK_SUCCESS;
			break;
		}

		spinlock_unlock_noipl(&obj->obj_lock);

		busyloop_hint();
	} while (timeout != POLL_ONCE);

	xipl(ipl);
	return ret;
}

status_t sched_wait_many(struct wait_block *table, void **objects_,
			 size_t count, wait_mode_t wait_mode,
			 wait_type_t wait_type, nstime_t timeout)
{
	assert(objects_);
	assert(count >= 1);
	assert(wait_type != WAIT_TYPE_ALL);

	struct kthread *thread = curthread();
	struct kobject **objects = (struct kobject **)objects_;

	bool has_timeout = (timeout != TIMEOUT_INFINITE);

	if (wait_mode == WAIT_MODE_POLL) {
		if (wait_type == WAIT_TYPE_ALL)
			return YAK_INVALID_ARGS;

		return poll_many(objects, count, timeout);
	}

	if (table == NULL) {
		assert(count <= KTHREAD_INLINE_WAIT_BLOCKS);
		table = thread->inline_wait_blocks;
	}

	for (size_t i = 0; i < count; i++) {
		struct wait_block *wb = &table[i];
		wb->thread = thread;
		wb->object = objects[i];
		wb->status = YAK_WAIT_SUCCESS + i;
		wb->flags = 0;
	}

RetryWait:
	ipl_t ipl = ripl(IPL_DPC);

	spinlock_lock_noipl(&thread->thread_lock);
	thread->wait_phase = WAIT_PHASE_IN_PROGRESS;
	thread->timeout_wait_block.flags = 0;
	thread->wait_blocks = table;
	thread->wait_blocks_count = count;
	spinlock_unlock_noipl(&thread->thread_lock);

	for (size_t i = 0; i < count; i++) {
		struct kobject *obj = objects[i];
		spinlock_lock_noipl(&obj->obj_lock);

		if (is_obj_signaled(obj)) {
			obj_maybe_acquire(obj);
			spinlock_unlock_noipl(&obj->obj_lock);

			thread->wait_phase = WAIT_PHASE_NONE;

			xipl(ipl);
			return YAK_WAIT_SUCCESS + i;
		} else {
			TAILQ_VERIFY(&obj->obj_wait_list);
			TAILQ_INSERT_TAIL(&obj->obj_wait_list, &table[i],
					  entry);
			obj->obj_wait_count += 1;
		}

		spinlock_unlock_noipl(&obj->obj_lock);
	}

	if (has_timeout)
		set_timeout(thread, timeout);

	spinlock_lock_noipl(&thread->thread_lock);

	if (thread->wait_phase == WAIT_PHASE_ABORTED) {
		spinlock_unlock_noipl(&thread->thread_lock);

		if (has_timeout) {
			timer_uninstall(&thread->timeout_timer);
			wb_dequeue(&thread->timeout_wait_block);
		}

		status_t wait_status = thread->wait_status;

		for (size_t i = 0; i < count; i++) {
			wb_dequeue(&table[i]);
		}

		thread->wait_phase = WAIT_PHASE_NONE;

		xipl(ipl);

		return wait_status;
	}

	status_t wait_status = do_wait(thread, has_timeout);
	// thread is unlocked upon returning
	xipl(ipl);

	// XXX: APCs -> Retry

	return wait_status;
}

status_t sched_wait(void *object, wait_mode_t wait_mode, nstime_t timeout)
{
	assert(object);
	struct kthread *thread = curthread();
	struct kobject *obj = object;

	bool has_timeout = (timeout != TIMEOUT_INFINITE);

	if (wait_mode == WAIT_MODE_POLL) {
		return poll_single(obj, timeout);
	}

	// Grab the first waitblock
	// Also, minimize the time spent at IPL_DPC
	struct wait_block *wb = &thread->inline_wait_blocks[0];
	wb->thread = thread;
	wb->object = object;
	wb->status = YAK_SUCCESS;
	wb->flags = 0;

RetryWait:
	ipl_t ipl = ripl(IPL_DPC);

	spinlock_lock_noipl(&thread->thread_lock);
	thread->wait_phase = WAIT_PHASE_IN_PROGRESS;
	thread->timeout_wait_block.flags = 0;
	thread->wait_blocks = wb;
	thread->wait_blocks_count = 1;
	spinlock_unlock_noipl(&thread->thread_lock);

	spinlock_lock_noipl(&obj->obj_lock);

	if (is_obj_signaled(obj)) {
		obj_maybe_acquire(obj);
		spinlock_unlock_noipl(&obj->obj_lock);

		thread->wait_phase = WAIT_PHASE_NONE;

		xipl(ipl);
		return YAK_SUCCESS;
	}

	TAILQ_VERIFY(&obj->obj_wait_list);
	TAILQ_INSERT_TAIL(&obj->obj_wait_list, wb, entry);
	obj->obj_wait_count += 1;

	spinlock_unlock_noipl(&obj->obj_lock);

	if (has_timeout)
		set_timeout(thread, timeout);

	spinlock_lock_noipl(&thread->thread_lock);

	// If our thread was signaled, that means we DONT have to
	// decrement the signal count anymore!

	// dequeue the wait block
	if (thread->wait_phase == WAIT_PHASE_ABORTED) {
		spinlock_unlock_noipl(&thread->thread_lock);

		if (has_timeout) {
			timer_uninstall(&thread->timeout_timer);
			wb_dequeue(&thread->timeout_wait_block);
		}

		status_t wait_status = thread->wait_status;

		wb_dequeue(wb);

		thread->wait_phase = WAIT_PHASE_NONE;

		xipl(ipl);

		return wait_status;
	}

	status_t wait_status = do_wait(thread, has_timeout);
	// thread is unlocked upon returning
	xipl(ipl);

	// XXX: APCs -> Retry

	return wait_status;
}

void thread_unwait(struct kthread *thread, status_t status)
{
	assert(spinlock_held(&thread->thread_lock));
	//pr_debug("unwait %p\n", thread);
	assert(thread->status == THREAD_WAITING ||
	       thread->wait_phase == WAIT_PHASE_IN_PROGRESS);

	if (thread->status == THREAD_RUNNING) {
		thread->wait_phase = WAIT_PHASE_ABORTED;
	}

	thread->wait_status = status;

	// set all wait blocks to unwaited
	for (size_t i = 0; i < thread->wait_blocks_count; i++) {
		struct wait_block *wb = &thread->wait_blocks[i];
		wb->flags |= WB_UNWAITED;
	}

	thread->timeout_wait_block.flags |= WB_UNWAITED;

	if (thread->status == THREAD_WAITING) {
		sched_resume_locked(thread);
	}
}
