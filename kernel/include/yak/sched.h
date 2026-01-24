#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <yak/status.h>
#include <yak/spinlock.h>
#include <yak/types.h>
#include <yak/queue.h>
#include <yak/timer.h>
#include <yak/process.h>
#include <yak/arch-sched.h>
#include <yak/vm/map.h>

#ifdef KERNEL_PROFILER
#include <yak-private/profiler.h>
#endif

enum {
	SCHED_PRIO_IDLE = 0,
	SCHED_PRIO_TIME_SHARE = 1, /* 1-16 */
	SCHED_PRIO_TIME_SHARE_END = 16,
	SCHED_PRIO_REAL_TIME = 17, /* 17-32 */
	SCHED_PRIO_REAL_TIME_END = 32,
	SCHED_PRIO_MAX = 32,
};

struct wait_block {
	// thread waiting
	struct kthread *thread;
	// object being waited on
	void *object;
	// status to set in the thread for WAIT_TYPE_ANY
	status_t status;
	// for inserting into object wait list
	TAILQ_ENTRY(wait_block) entry;
};

enum {
	/* unblock when any object is ready */
	WAIT_TYPE_ANY = 1,
	/* unblock when all objects are ready */
	WAIT_TYPE_ALL = 2,
};

enum {
	WAIT_MODE_BLOCK = 1,
	WAIT_MODE_POLL,
};

#define KTHREAD_INLINE_WAIT_BLOCKS 4

// current/soon-to-be state
enum {
	// enqueued
	THREAD_READY = 1,
	// off-list, active
	THREAD_RUNNING,
	// off-list, set as next
	THREAD_NEXT,
	// off-list, currently switching to thread
	THREAD_SWITCHING,
	// off-list, terminating
	THREAD_TERMINATING,
	// off-list, blocked
	THREAD_WAITING,
	// off-list, freshly created
	THREAD_UNDEFINED,
};

#define KTHREAD_MAX_NAME_LEN 32

struct kthread {
	struct md_pcb pcb;

	struct spinlock thread_lock;

	unsigned int switching;

	char name[KTHREAD_MAX_NAME_LEN];

	void *kstack_top;

	int user_thread;

	struct wait_block inline_wait_blocks[KTHREAD_INLINE_WAIT_BLOCKS];
	struct wait_block *wait_blocks;
	unsigned int wait_type;
	status_t wait_status;

	struct wait_block timeout_wait_block;
	struct timer timeout_timer;

	unsigned int priority;
	unsigned int status;

	struct cpu *affinity_cpu;
	struct cpu *last_cpu;

	/* you can temporarily switch to another vm context */
	struct vm_map *vm_ctx;

	struct kprocess *owner_process;

#ifdef KERNEL_PROFILER
	call_frame_t frames[MAX_FRAMES];
	size_t cur_frame;
#endif

	LIST_ENTRY(kthread) process_entry;
	TAILQ_ENTRY(kthread) queue_entry;
};

typedef TAILQ_HEAD(thread_queue, kthread) thread_queue_t;

void kthread_init(struct kthread *thread, const char *name,
		  unsigned int initial_priority, struct kprocess *process,
		  int user_thread);

status_t kernel_thread_create(const char *name, unsigned int priority,
			      void *entry, void *context, int instant_launch,
			      struct kthread **out);

void kthread_destroy(struct kthread *thread);

void kthread_context_init(struct kthread *thread, void *kstack_top,
			  void *entrypoint, void *context1, void *context2);

void kthread_context_copy(const struct kthread *source_thread,
			  struct kthread *dest_thread);

[[gnu::noreturn]]
void kernel_enter_userspace(uint64_t ip, uint64_t sp);

struct runqueue {
	uint32_t ready_mask;
	thread_queue_t queues[SCHED_PRIO_MAX];
};

struct sched {
	struct runqueue rqs[2];

	struct runqueue *current_rq;
	struct runqueue *next_rq;

	thread_queue_t idle_rq;
};

void sched_init();
void sched_dynamic_init();

void sched_insert(struct cpu *cpu, struct kthread *thread, int isOther);

void sched_preempt(struct cpu *cpu);

void sched_resume(struct kthread *thread);
void sched_resume_locked(struct kthread *thread);

[[gnu::noreturn]]
void sched_exit_self();

void sched_yield(struct kthread *current, struct cpu *cpu);

void sched_wake_thread(struct kthread *thread, status_t status);

status_t launch_elf(struct kprocess *proc, char *path, int priority,
		    char **argv, char **envp, struct kthread **thread_out);

#ifdef __cplusplus
}
#endif
