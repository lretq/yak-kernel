#include <stddef.h>
#include <nanoprintf.h>
#include <yak/queue.h>
#include <yak/cpudata.h>
#include <yak/spinlock.h>
#include <yak/cpu.h>

struct cpumask cpumask_active;
size_t num_cpus_active = 0;

void cpu_init()
{
	bitset_init(&cpumask_active);
}

void cpu_up(size_t id)
{
	bitset_atomic_set(&cpumask_active, id);
	__atomic_fetch_add(&num_cpus_active, 1, __ATOMIC_RELAXED);
}

size_t cpus_online()
{
	return __atomic_load_n(&num_cpus_active, __ATOMIC_RELAXED);
}

extern void timer_update(struct dpc *dpc, void *ctx);

static struct cpu *bsp_ptr = NULL;
struct cpu **__all_cpus = NULL;

static size_t next_cpu_id = 0;

void cpudata_init(struct cpu *cpu, void *stack_top)
{
	cpu->self = cpu;

	cpu->cpu_id = __atomic_fetch_add(&next_cpu_id, 1, __ATOMIC_RELAXED);

	if (cpu->cpu_id >= MAX_NR_CPUS) {
		panic("CPUs >= MAX_NR_CPUS\n");
	}

	if (cpu->cpu_id == 0) {
		bsp_ptr = cpu;
		__all_cpus = &bsp_ptr;
	}

	cpu->current_map = NULL;

	cpu->softint_pending = 0;

	spinlock_init(&curcpu_ptr()->sched_lock);
	struct sched *sched = &curcpu_ptr()->sched;

	for (size_t rq = 0; rq < 2; rq++) {
		for (size_t prio = 0; prio < elementsof(sched->rqs[0].queues);
		     prio++) {
			TAILQ_INIT(&sched->rqs[rq].queues[prio]);
		}
		sched->rqs[rq].ready_mask = 0;
	}

	sched->current_rq = &sched->rqs[0];
	sched->next_rq = &sched->rqs[1];

	TAILQ_INIT(&sched->idle_rq);

	spinlock_init(&cpu->dpc_lock);
	LIST_INIT(&cpu->dpc_queue);

	spinlock_init(&cpu->timer_lock);
	HEAP_INIT(&cpu->timer_heap);
	dpc_init(&cpu->timer_update_dpc, timer_update);

	rcq_init(&cpu->rc_queue);

	cpu->kstack_top = stack_top;
	cpu->idle_thread.kstack_top = stack_top;
	cpu->current_thread = &cpu->idle_thread;
	cpu->next_thread = NULL;

	char idle_name[12];
	npf_snprintf(idle_name, sizeof(idle_name), "idle%ld", cpu->cpu_id);
	kthread_init(&cpu->idle_thread, idle_name, 0, &kproc0, 0);
}
