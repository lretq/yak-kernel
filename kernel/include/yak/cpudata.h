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
	struct cpu *self;

	struct cpu_md md;

	size_t cpu_id;

	ipl_t hw_ipl;

#if CONFIG_LAZY_IPL
	ipl_t soft_ipl;
#endif

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

#define curcpu() PERCPU_FIELD_LOAD(self)
#define cpuid() PERCPU_FIELD_LOAD(cpu_id)
#define curthread() PERCPU_FIELD_LOAD(current_thread)
#define curproc() curthread()->owner_process

extern struct cpu **__all_cpus;
#define getcpu(i) __all_cpus[i]

void cpudata_init(struct cpu *cpu, void *stack_top);

#ifdef __cplusplus
}
#endif
