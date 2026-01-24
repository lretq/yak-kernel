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
	for (size_t i = 0; i < CPUMASK_BITS_SIZE; i++) {
		__atomic_store_n(&cpumask_active.bits[i], 0, __ATOMIC_RELAXED);
	}
}

void cpu_up(size_t id)
{
	const size_t index = id / CPUMASK_BITS_PER_IDX;
	const size_t bit = id & (CPUMASK_BITS_PER_IDX - 1);

	__atomic_fetch_or(&cpumask_active.bits[index],
			  ((cpumask_word_t)1 << bit), __ATOMIC_ACQUIRE);
	__atomic_fetch_add(&num_cpus_active, 1, __ATOMIC_ACQUIRE);
}

size_t cpus_online()
{
	return __atomic_load_n(&num_cpus_active, __ATOMIC_ACQUIRE);
}

static size_t cpu_id = 0;

extern void timer_update(struct dpc *dpc, void *ctx);

static struct cpu *bsp_ptr;
struct cpu **__all_cpus = NULL;

void cpudata_init(struct cpu *cpu, void *stack_top)
{
	cpu->self = cpu;

	cpu->cpu_id = __atomic_fetch_add(&cpu_id, 1, __ATOMIC_RELAXED);
	if (cpu->cpu_id >= MAX_NR_CPUS)
		panic("CPUs >= MAX_NR_CPUS\n");
	if (cpu->cpu_id == 0) {
		bsp_ptr = cpu;
		__all_cpus = &bsp_ptr;
	}

	cpu->kstack_top = NULL;

	cpu->current_map = NULL;

	cpu->next_thread = NULL;
	cpu->current_thread = NULL;

	cpu->softint_pending = 0;

	spinlock_init(&curcpu_ptr()->sched_lock);
	struct sched *sched = &curcpu_ptr()->sched;

	for (size_t rq = 0; rq < 2; rq++) {
		for (size_t prio = 0; prio < SCHED_PRIO_MAX; prio++) {
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

	cpu->kstack_top = stack_top;
	cpu->idle_thread.kstack_top = stack_top;

	cpu->current_thread = &curcpu_ptr()->idle_thread;
	cpu->next_thread = NULL;

	char idle_name[12];
	npf_snprintf(idle_name, sizeof(idle_name), "idle%ld", cpu->cpu_id);
	kthread_init(&curcpu_ptr()->idle_thread, idle_name, 0, &kproc0, 0);
}
