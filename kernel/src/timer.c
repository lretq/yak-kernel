#include <heap.h>
#include <yak/timer.h>
#include <yak/percpu.h>
#include <yak/types.h>
#include <yak/cpudata.h>
#include <yak/spinlock.h>
#include <yak/object.h>
#include <yak/status.h>
#include <yak/dpc.h>
#include <yak/sched.h>

static int timer_cmp(struct timer *a, struct timer *b)
{
	return a->deadline < b->deadline;
}

HEAP_IMPL(timer_heap, timer, entry, timer_cmp);

void timer_init(struct timer *timer)
{
	kobject_init(&timer->hdr, 0);
	timer->cpu = NULL;
	timer->state = TIMER_STATE_UNUSED;
	timer->deadline = 0;
}

void timer_reset(struct timer *timer)
{
	assert(timer->state != TIMER_STATE_QUEUED);
	ipl_t ipl = spinlock_lock(&timer->hdr.obj_lock);
	timer->hdr.signalstate = 0;
	spinlock_unlock(&timer->hdr.obj_lock, ipl);
}

void timer_uninstall(struct timer *timer)
{
	ipl_t ipl = spinlock_lock(&timer->hdr.obj_lock);
	struct cpu *cpu = timer->cpu;
	if (timer->state != TIMER_STATE_QUEUED) {
		spinlock_unlock(&timer->hdr.obj_lock, ipl);
		return;
	}
	spinlock_unlock_noipl(&timer->hdr.obj_lock);
	// keep lock order
	spinlock_lock_noipl(&cpu->timer_lock);
	spinlock_lock_noipl(&timer->hdr.obj_lock);

	if (timer->cpu != cpu || timer->state != TIMER_STATE_QUEUED) {
		// another cpu got to the timer first
		goto exit;
	}

	HEAP_REMOVE(timer_heap, &cpu->timer_heap, timer);

	timer->cpu = NULL;
	timer->state = TIMER_STATE_CANCELED;

	// no need to run update dpc
	// either the timer wasn't the next timer anyway
	// or it was the next and we re-arm to the new next

exit:
	spinlock_unlock_noipl(&cpu->timer_lock);
	spinlock_unlock(&timer->hdr.obj_lock, ipl);
}

status_t timer_install(struct timer *timer, nstime_t ns_delta)
{
	// lock with interrupts, else what if timer int fires while we're here
	int state = spinlock_lock_interrupts(&curcpu_ptr()->timer_lock);
	spinlock_lock_noipl(&timer->hdr.obj_lock);
	// timer is already installed somewhere
	if (timer->cpu != NULL) {
		spinlock_unlock_noipl(&timer->hdr.obj_lock);
		spinlock_unlock_interrupts(&curcpu_ptr()->timer_lock, state);
		return YAK_BUSY;
	}

	timer->cpu = curcpu_ptr();
	timer->state = TIMER_STATE_QUEUED;
	timer->deadline = plat_getnanos() + ns_delta;

	struct timer_heap *heap = &curcpu_ptr()->timer_heap;
	HEAP_INSERT(timer_heap, heap, timer);

	timer->hdr.signalstate = 0;

	spinlock_unlock_noipl(&timer->hdr.obj_lock);
	spinlock_unlock_interrupts(&curcpu_ptr()->timer_lock, state);

	dpc_enqueue(&curcpu_ptr()->timer_update_dpc, NULL);

	return YAK_SUCCESS;
}

// run from DPC context
void timer_update([[maybe_unused]] struct dpc *dpc, [[maybe_unused]] void *ctx)
{
	struct timer_heap *heap = &curcpu_ptr()->timer_heap;

	nstime_t curr_time;

	do {
		curr_time = plat_getnanos();

		spinlock_lock_noipl(&curcpu_ptr()->timer_lock);
		if (HEAP_EMPTY(heap)) {
			plat_arm_timer(TIMER_INFINITE);
			spinlock_unlock_noipl(&curcpu_ptr()->timer_lock);
			return;
		}

		struct timer *root = HEAP_PEEK(heap);
		if (root->deadline > curr_time) {
			plat_arm_timer(root->deadline);
			spinlock_unlock_noipl(&curcpu_ptr()->timer_lock);
			return;
		}

		spinlock_lock_noipl(&root->hdr.obj_lock);

		HEAP_POP(timer_heap, heap);
		root->cpu = NULL;

		root->state = TIMER_STATE_FIRED;

		if (root->hdr.waitcount) {
			kobject_signal_locked(&root->hdr, 1);
		}

		root->hdr.signalstate = 1;

		spinlock_unlock_noipl(&root->hdr.obj_lock);
		spinlock_unlock_noipl(&curcpu_ptr()->timer_lock);
	} while (1);
}

void ksleep(nstime_t ns)
{
	struct timer timer;
	timer_init(&timer);
	timer_install(&timer, ns);
	sched_wait_single(&timer, WAIT_MODE_BLOCK, WAIT_TYPE_ANY,
			  TIMEOUT_INFINITE);
}

void kstall(nstime_t ns)
{
	nstime_t deadline = plat_getnanos() + ns;
	while (plat_getnanos() < deadline) {
		busyloop_hint();
	}
}

struct timespec time_now()
{
	struct timespec ts;
	// XXX: RTC or something
	nstime_t now = plat_getnanos();
	ts.tv_sec = now / STIME(1);
	ts.tv_nsec = now - (ts.tv_sec * STIME(1));
	return ts;
}
