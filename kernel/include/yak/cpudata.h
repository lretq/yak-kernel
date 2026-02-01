#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <heap.h>
#include <yak/ipl.h>
#include <yak/dpc.h>
#include <yak/queue.h>
#include <yak/percpu.h>
#include <yak/arch-cpudata.h>
#include <yak/kernel-file.h>
#include <yak/sched.h>
#include <yak/spinlock.h>
#include <yak/ipi.h>

struct cpu {
	struct cpu_md md;
	struct cpu *self;

	size_t cpu_id;

	void *syscall_temp;
	void *kstack_top;

	struct vm_map *current_map;

	struct spinlock sched_lock;
	struct sched sched;

	struct kthread idle_thread;
	struct kthread *current_thread;
	struct kthread *next_thread;

	unsigned long softint_pending;

	struct spinlock dpc_lock;
	LIST_HEAD(, dpc) dpc_queue;

	// timers
	struct spinlock timer_lock;
	HEAP_HEAD(timer_heap, timer) timer_heap;
	struct dpc timer_update_dpc;

	// remote calls
	struct remote_call_queue rc_queue;
};

#define curthread() curcpu().current_thread
#define curproc() curthread()->owner_process

extern struct cpu **__all_cpus;
#define getcpu(i) __all_cpus[i]

void cpudata_init(struct cpu *cpu, void *stack_top);

#define PERCPU_PTR(type, var)                                   \
	((type *)((uintptr_t)curcpu_ptr() + ((uintptr_t)&var) - \
		  ((uintptr_t)__kernel_percpu_start)))

#ifdef __cplusplus
}
#endif
