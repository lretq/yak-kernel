/*
https://web.cs.ucdavis.edu/~roper/ecs150/ULE.pdf

From my rough understanding we have three classes:
- idle
- time-shared
- real-time

We have three different queues: idle, current, next

idle queue is only ran once we don't have any thread whatsover on any other queue
time-shared threads may not preempt any other thread
real-time threads may preempt lower priority threads

Threads may be deemed interactive and end up on current queues too / handled as realtime threads

i'll implement everything very primitively. interactivity and other features will follow when userspace :^)

The idea for the scheduler mechanisms are from MINTIA (by @hyenasky)
See: https://github.com/xrarch/mintia2
*/

#define pr_fmt(fmt) "sched: " fmt

#include <assert.h>
#include <string.h>
#include <yak/log.h>
#include <yak/init.h>
#include <yak/cpu.h>
#include <yak/kevent.h>
#include <yak/dpc.h>
#include <yak/sched.h>
#include <yak/hint.h>
#include <yak/softint.h>
#include <yak/cpudata.h>
#include <yak/spinlock.h>
#include <yak/queue.h>
#include <yak/macro.h>
#include <yak/heap.h>
#include <yak/vm/pmm.h>
#include <yak/vm/map.h>
#include <yak/arch-cpudata.h>
#include <yak/timer.h>
#include <yak/panic.h>

static struct kevent reaper_ev;
static SPINLOCK(reaper_lock);
static thread_queue_t reaper_queue = TAILQ_HEAD_INITIALIZER(reaper_queue);
static struct kthread *reaper_thread = NULL;

void sched_init()
{
	event_init(&reaper_ev, 0, 0);
}

static inline void wait_for_switch(struct kthread *thread)
{
	while (__atomic_load_n(&thread->switching, __ATOMIC_ACQUIRE)) {
		busyloop_hint();
	}
}

[[gnu::no_instrument_function]]
extern void plat_swtch(struct kthread *current, struct kthread *new);

// called after switching off stack is complete
[[gnu::no_instrument_function]]
void sched_finalize_swtch(struct kthread *current, struct kthread *next)
{
	__atomic_store_n(&current->switching, 0, __ATOMIC_RELEASE);

	spinlock_unlock_noipl(&current->thread_lock);

	next->status = THREAD_RUNNING;
}

[[gnu::no_instrument_function]]
static void swtch(struct kthread *current, struct kthread *thread)
{
	assert(curipl() == IPL_DPC);
	assert(current && thread);
	assert(current != thread);
	assert(spinlock_held(&current->thread_lock));
	// This can happen legally:
	// if we come from sched_yield and we have waited for switching=0 sucessfully,
	// the spinlock is unlocked only afterwards
	//assert(!spinlock_held(&thread->thread_lock));
	assert(current->status != THREAD_TERMINATING ||
	       current->status != THREAD_WAITING);

	current->affinity_cpu = curcpu_ptr();

	if (thread->vm_ctx != NULL) {
		assert(thread->owner_process == &kproc0);
		vm_map_activate(thread->vm_ctx);
	} else if (thread->user_thread) {
		if (current->owner_process != thread->owner_process) {
			vm_map_activate(thread->owner_process->map);
		}
	}

	curcpu().current_thread = thread;
	curcpu().kstack_top = thread->kstack_top;

	plat_swtch(current, thread);

	assert(current == curthread());

	assert(current->status != THREAD_TERMINATING);

	// we should be back now
	assert(current == curthread());
}

[[gnu::no_instrument_function]]
void sched_preempt(struct cpu *cpu)
{
	spinlock_lock_noipl(&cpu->sched_lock);
	struct kthread *next = cpu->next_thread;
	if (!next) {
		spinlock_unlock_noipl(&cpu->sched_lock);
		return;
	}

	// retrieve the next thread
	cpu->next_thread = NULL;
	next->status = THREAD_SWITCHING;

	spinlock_unlock_noipl(&cpu->sched_lock);

	wait_for_switch(next);

	struct kthread *current = cpu->current_thread;

	spinlock_lock_noipl(&current->thread_lock);

	// in the process of switching off-stack
	__atomic_store_n(&current->switching, 1, __ATOMIC_RELAXED);

	if (current != &cpu->idle_thread) {
		spinlock_lock_noipl(&cpu->sched_lock);
		sched_insert(cpu, current, 0);
		spinlock_unlock_noipl(&cpu->sched_lock);
	} else {
		// the idle thread remains ready
		current->status = THREAD_READY;
	}

	swtch(current, next);
}

static struct kthread *select_next(struct cpu *cpu, unsigned int priority)
{
	assert(spinlock_held(&cpu->sched_lock));
	struct sched *sched = &cpu->sched;

	// threads on current runqueue
	if (sched->current_rq->ready_mask) {
		unsigned int next_priority =
			31 - __builtin_clz(sched->current_rq->ready_mask);
		if (priority > next_priority)
			return NULL;

		thread_queue_t *rq = &sched->current_rq->queues[next_priority];

		struct kthread *thread = TAILQ_FIRST(rq);
		assert(thread);
		TAILQ_REMOVE(rq, thread, queue_entry);
		assert(rq->tqh_last != NULL);
		// if runqueue is now empty, update mask
		if (TAILQ_EMPTY(rq)) {
			sched->current_rq->ready_mask &=
				~(1UL << next_priority);
		}
		thread->status = THREAD_SWITCHING;
		return thread;
	} else if (sched->next_rq->ready_mask) {
#if 0
		pr_warn("swap next and current\n");
#endif
		// NOTE: should I check priority here too?
		struct runqueue *tmp = sched->current_rq;
		sched->current_rq = sched->next_rq;
		sched->next_rq = tmp;

		// safe: can only recurse once
		return select_next(cpu, priority);
	} else if (!TAILQ_EMPTY(&sched->idle_rq)) {
		// only run idle priority if no other threads are ready
		struct kthread *thread = TAILQ_FIRST(&sched->idle_rq);
		TAILQ_REMOVE(&sched->idle_rq, thread, queue_entry);
		assert(sched->idle_rq.tqh_last != NULL);
		thread->status = THREAD_SWITCHING;
		return thread;
	}

	return NULL;
}

#if 0
// call this from preemption handler
static void do_reschedule()
{
	struct kthread *current = curthread(), *next;
	ipl_t ipl = spinlock_lock(&curcpu_ptr()->sched_lock);
	// check if there is something ready to preempt us
	if ((next = select_next(curcpu_ptr(), current->priority)) != NULL) {
		assert(curcpu_ptr()->next_thread == NULL);
		curcpu_ptr()->next_thread = next;
	}
	spinlock_unlock(&curcpu_ptr()->sched_lock, ipl);
}
#endif

void kthread_init(struct kthread *thread, const char *name,
		  unsigned int initial_priority, struct kprocess *process,
		  bool user_thread)
{
	spinlock_init(&thread->thread_lock);

	thread->switching = 0;

	size_t namelen = strlen(name);
	if (namelen > KTHREAD_MAX_NAME_LEN)
		namelen = KTHREAD_MAX_NAME_LEN - 1;
	memcpy(thread->name, name, namelen);
	thread->name[namelen] = '\0';

	thread->kstack_top = NULL;

	thread->user_thread = user_thread;

	thread->wait_blocks = NULL;

	thread->timeout_wait_block.status = YAK_TIMEOUT;
	thread->timeout_wait_block.thread = thread;
	thread->timeout_wait_block.object = &thread->timeout_timer;
	timer_init(&thread->timeout_timer);

	thread->priority = initial_priority;
	thread->status = THREAD_UNDEFINED;

	thread->affinity_cpu = NULL;
	thread->last_cpu = NULL;

	thread->vm_ctx = NULL;

	ipl_t ipl = spinlock_lock(&process->thread_list_lock);
	__atomic_fetch_add(&process->thread_count, 1, __ATOMIC_ACQUIRE);

	LIST_INSERT_HEAD(&process->thread_list, thread, process_entry);

	spinlock_unlock(&process->thread_list_lock, ipl);

	thread->owner_process = process;
}

void sched_yield(struct kthread *current, struct cpu *cpu)
{
	assert(current);
	assert(cpu);
	assert(spinlock_held(&current->thread_lock));
	spinlock_lock_noipl(&cpu->sched_lock);
	// anything is fine now
	struct kthread *next = cpu->next_thread;
	if (next) {
		cpu->next_thread = NULL;
		next->status = THREAD_SWITCHING;
	} else {
		next = select_next(cpu, 0);
	}

	spinlock_unlock_noipl(&cpu->sched_lock);

	if (next) {
		wait_for_switch(next);
		swtch(current, next);
	} else {
		swtch(current, &cpu->idle_thread);
	}
}

static void sched_rq_insert(struct runqueue *rq, struct kthread *thread)
{
	assert(thread);
	assert(rq);

	thread_queue_t *queue = &rq->queues[thread->priority];
	assert(queue);

	assert(queue->tqh_last);
	TAILQ_INSERT_TAIL(queue, thread, queue_entry);
	assert(TAILQ_FIRST(queue) != NULL);

	rq->ready_mask |= (1UL << thread->priority);
}

void sched_insert(struct cpu *cpu, struct kthread *thread, int isOther)
{
loop:
	// TODO: update thread stats maybe?
	thread->last_cpu = cpu;
	thread->status = THREAD_READY;

	struct sched *sched = &cpu->sched;
	assert(spinlock_held(&cpu->sched_lock));

	struct kthread *current = cpu->current_thread, *next = cpu->next_thread;
	// Compare with either an already selected thread, or the currently running thread
	struct kthread *comp = next ? next : current;

	// TODO: check interactivity ??
	if (thread->priority >= SCHED_PRIO_REAL_TIME) {
		if (thread->priority <= comp->priority) {
#if 0
			pr_debug("f: %p l: %p\n", list->tqh_first,
				 list->tqh_last);
#endif

			// We aren't important enough to preempt
			sched_rq_insert(sched->current_rq, thread);

			return;
		}
	} else /* priority < SCHED_PRIO_REAL_TIME */ {
		if (comp != &cpu->idle_thread) {
			// Time shared threads mustn't preempt anything other than idle threads
			if (thread->priority == SCHED_PRIO_TIME_SHARE) {
				sched_rq_insert(sched->next_rq, thread);
			} else {
				TAILQ_INSERT_TAIL(&sched->idle_rq, thread,
						  queue_entry);
			}

			return;
		}

		// We can preempt the idle thread!
	}

	// thread can preempt the currently running thread
	thread->status = THREAD_NEXT;
	cpu->next_thread = thread;

	if (next) {
		// reinsert old next thread if we preempted
		// next prio < our prio

		thread = next;
		goto loop;
	} else {
		if (isOther) {
			softint_issue_other(cpu, IPL_DPC);
		} else {
			softint_issue(IPL_DPC);
		}
	}
}

static size_t count = 0;

// Round Robin the next CPU
static struct cpu *find_cpu()
{
	assert(cpus_online() != 0);

	size_t desired =
		__atomic_fetch_add(&count, 1, __ATOMIC_RELAXED) % cpus_online();
	size_t i = 0;

	size_t tmp;
	for_each_cpu(tmp, &cpumask_active) {
		if (i++ == desired)
			return getcpu(tmp);
	}

	pr_warn("find_cpu(): fallback to local core?\n");

	return curcpu_ptr();
}

void sched_resume_locked(struct kthread *thread)
{
	assert(spinlock_held(&thread->thread_lock));

	struct cpu *cpu = thread->affinity_cpu;
	if (!cpu) {
		cpu = find_cpu();
	}

	spinlock_lock_noipl(&cpu->sched_lock);
	sched_insert(cpu, thread, cpu != curcpu_ptr());
	spinlock_unlock_noipl(&cpu->sched_lock);
}

void sched_resume(struct kthread *thread)
{
	ipl_t ipl = spinlock_lock(&thread->thread_lock);
	sched_resume_locked(thread);
	spinlock_unlock(&thread->thread_lock, ipl);
}

// This is the thread reaper
// Once a process calls sched_destroy_self it frees all it's ressources
// and wakes the reaper
void thread_reaper_fn()
{
	for (;;) {
		sched_wait(&reaper_ev, WAIT_MODE_BLOCK, TIMEOUT_INFINITE);

		ipl_t ipl = spinlock_lock(&reaper_lock);

		struct kthread *thread;

		while (!TAILQ_EMPTY(&reaper_queue)) {
			thread = TAILQ_FIRST(&reaper_queue);
			TAILQ_REMOVE(&reaper_queue, thread, queue_entry);

			spinlock_unlock(&reaper_lock, ipl);

			kthread_destroy(thread);

			ipl = spinlock_lock(&reaper_lock);
		}

		spinlock_unlock(&reaper_lock, ipl);
	}
}

void sched_dynamic_init()
{
	kernel_thread_create("reaper_thread", SCHED_PRIO_REAL_TIME,
			     thread_reaper_fn, NULL, 1, &reaper_thread);
}

INIT_ENTAILS(sched_dyn);
INIT_DEPS(sched_dyn);
INIT_NODE(sched_dyn, sched_dynamic_init);

[[gnu::noreturn]]
void sched_exit_self()
{
	ripl(IPL_DPC);

	struct kthread *thread = curthread();

	//pr_debug("sched_exit_self: %p\n", thread);

	spinlock_lock_noipl(&thread->thread_lock);

	spinlock_lock_noipl(&reaper_lock);
	TAILQ_INSERT_HEAD(&reaper_queue, thread, queue_entry);
	spinlock_unlock_noipl(&reaper_lock);

	event_alarm(&reaper_ev, false);

	thread->status = THREAD_TERMINATING;

	//pr_debug("pre-yield: %p\n", thread);

	sched_yield(thread, curcpu_ptr());
	__builtin_unreachable();
	__builtin_trap();
}

void idle_loop()
{
	setipl(IPL_PASSIVE);
	while (1) {
		assert(curipl() == IPL_PASSIVE);
		asm volatile("sti; hlt");
	}
}
