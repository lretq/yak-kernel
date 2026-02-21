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

#define KTHREAD_MAX_NAME_LEN 32
#define KTHREAD_INLINE_WAIT_BLOCKS 4

enum {
	SCHED_PRIO_IDLE = 0,
	SCHED_PRIO_TIME_SHARE = 1, /* 1-16 */
	SCHED_PRIO_TIME_SHARE_END = 16,
	SCHED_PRIO_REAL_TIME = 17, /* 17-32 */
	SCHED_PRIO_REAL_TIME_END = 32,
	SCHED_PRIO_MAX = 32,
};

// current/soon-to-be state
enum thread_state {
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

// This solution comes from microsoft's channel9:
// "Inside Windows 7: Arun Kishan - Farewell to the Windows Kernel Dispatcher Lock"
enum wait_phase {
	WAIT_PHASE_NONE,
	// Thread setting up wait logic
	WAIT_PHASE_IN_PROGRESS,
	// Thread comitted to waiting
	WAIT_PHASE_COMITTED,
	// Wait was aborted, for example by an object
	// that was signaled in the middle of IN_PROGRESS
	WAIT_PHASE_ABORTED,
};

struct kthread {
	struct md_pcb pcb;

	struct spinlock thread_lock;

	unsigned int switching;

	char name[KTHREAD_MAX_NAME_LEN];

	void *kstack_top;

	bool user_thread;

	struct wait_block *wait_blocks;
	size_t wait_blocks_count;

	struct wait_block inline_wait_blocks[KTHREAD_INLINE_WAIT_BLOCKS];

	unsigned short wait_phase;

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
		  bool user_thread);

typedef void (*kernel_thread_fn)(void *);

status_t kernel_thread_create(const char *name, unsigned int priority,
			      kernel_thread_fn entry, void *context,
			      bool instant_launch, struct kthread **out);

status_t kernel_thread_init(struct kthread *thread, const char *name,
			    unsigned int priority, kernel_thread_fn entry,
			    void *context, bool instant_launch);

void kthread_destroy(struct kthread *thread);

void kthread_context_init(struct kthread *thread, void *kstack_top,
			  void *entrypoint, void *context1, void *context2);

void kthread_context_copy(const struct kthread *source_thread,
			  struct kthread *dest_thread);

[[gnu::noreturn]]
void kernel_enter_userspace(uint64_t ip, uint64_t sp);

struct runqueue {
	uint32_t ready_mask;
	thread_queue_t queues[SCHED_PRIO_MAX + 1];
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

void thread_unwait(struct kthread *thread, status_t status);

status_t launch_elf(struct kprocess *proc, struct vm_map *map, char *path,
		    int priority, char **argv, char **envp,
		    struct kthread **thread_out);

#ifdef __cplusplus
}
#endif
