#include <assert.h>
#include <yak/spinlock.h>
#include <yak/arch-cpudata.h>
#include <yak/ipl.h>
#include <yak/queue.h>
#include <yak/dpc.h>
#include <yak/softint.h>
#include <yak/cpudata.h>

void dpc_init(struct dpc *dpc, void (*func)(struct dpc *, void *))
{
	dpc->enqueued = 0;
	dpc->func = func;
	dpc->context = NULL;
}

void dpc_enqueue(struct dpc *dpc, void *context)
{
	struct cpu *cpu = curcpu();
	int state = spinlock_lock_interrupts(&cpu->dpc_lock);

	if (dpc->enqueued) {
		spinlock_unlock_interrupts(&cpu->dpc_lock, state);
		return;
	}

	dpc->enqueued = 1;
	dpc->context = context;
	LIST_INSERT_HEAD(&cpu->dpc_queue, dpc, list_entry);

	softint_issue(IPL_DPC);
	spinlock_unlock_interrupts(&cpu->dpc_lock, state);
}

void dpc_dequeue(struct dpc *dpc)
{
	struct cpu *cpu = curcpu();
	int state = spinlock_lock_interrupts(&cpu->dpc_lock);

	if (!dpc->enqueued)
		goto exit;

	LIST_REMOVE(dpc, list_entry);
	dpc->enqueued = 0;
exit:
	spinlock_unlock_interrupts(&cpu->dpc_lock, state);
}

void dpc_queue_run(struct cpu *cpu)
{
	assert(curipl() == IPL_DPC);

	void *context;
	struct dpc *dpc;
	while (1) {
		int state = spinlock_lock_interrupts(&cpu->dpc_lock);
		if (LIST_EMPTY(&cpu->dpc_queue)) {
			spinlock_unlock_interrupts(&cpu->dpc_lock, state);
			return;
		}

		dpc = LIST_FIRST(&cpu->dpc_queue);
		LIST_REMOVE(dpc, list_entry);
		assert(dpc);
		assert(dpc->func);

		context = dpc->context;
		dpc->enqueued = 0;

		spinlock_unlock_interrupts(&cpu->dpc_lock, state);
		dpc->func(dpc, context);
	}
}
