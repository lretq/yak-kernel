#include <stddef.h>
#include <yak/log.h>
#include <yak/ringbuffer.h>
#include <yak/spinlock.h>
#include <yak/cpudata.h>
#include <yak/cpu.h>
#include <yak/ipi.h>

void rcq_init(struct remote_call_queue *rcq)
{
	spinlock_init(&rcq->rcq_lock);
	ringbuffer_static_init(&rcq->rcq_buffer, sizeof(rcq->rcq_backing),
			       &rcq->rcq_backing);
}

void ipi_handler()
{
	struct cpu *cpu = curcpu();
	struct remote_call_queue *rcq = &cpu->rc_queue;
	spinlock_lock_noipl(&rcq->rcq_lock);
	while (ringbuffer_available(&rcq->rcq_buffer)) {
		struct remote_call rc;
		ringbuffer_get(&rcq->rcq_buffer, &rc, sizeof(rc));

		spinlock_unlock_noipl(&rcq->rcq_lock);

		rc.rc_fn(rc.rc_ctx);

		if (rc.rc_done) {
			__atomic_fetch_add(rc.rc_done, 1, __ATOMIC_RELEASE);
		}

		spinlock_lock_noipl(&rcq->rcq_lock);
	}
	spinlock_unlock_noipl(&rcq->rcq_lock);
}

extern void plat_ipi(struct cpu *cpu);

static void do_ipi(size_t cpu, remote_fn_t fn, void *ctx, size_t *done)
{
	struct cpu *dest = getcpu(cpu);
	struct remote_call rc;
	rc.rc_fn = fn;
	rc.rc_ctx = ctx;
	rc.rc_done = done;

	size_t put_size = 0;
	do {
		struct remote_call_queue *rcq = &dest->rc_queue;
		ipl_t ipl = spinlock_lock(&rcq->rcq_lock);
		put_size = ringbuffer_put(&rcq->rcq_buffer, &rc,
					  sizeof(struct remote_call));
		spinlock_unlock(&rcq->rcq_lock, ipl);
	} while (put_size == 0);

	plat_ipi(dest);
}

void ipi_send(size_t cpu, remote_fn_t fn, void *ctx)
{
	if (cpu == IPI_SEND_OTHERS) {
		size_t our_id = cpuid();
		bitset_word_t tmp;
		for_each_cpu(tmp, &cpumask_active) {
			if (tmp != our_id)
				do_ipi(tmp, fn, ctx, NULL);
		}
	} else {
		do_ipi(cpu, fn, ctx, NULL);
	}
}

void ipi_send_wait(size_t cpu, remote_fn_t fn, void *ctx)
{
	size_t done_count = 0;
	size_t target_count = 0;

	if (cpu == IPI_SEND_OTHERS) {
		size_t our_id = cpuid();
		bitset_word_t tmp;
		for_each_cpu(tmp, &cpumask_active) {
			if (tmp != our_id) {
				target_count++;
				do_ipi(tmp, fn, ctx, &done_count);
			}
		}
	} else {
		do_ipi(cpu, fn, ctx, &done_count);
		target_count = 1;
	}

	// wait until all calls executed
	while (__atomic_load_n(&done_count, __ATOMIC_ACQUIRE) != target_count) {
		busyloop_hint();
	}
}

void ipi_mask_send(struct cpumask *mask, remote_fn_t fn, void *ctx, bool self,
		   bool wait)
{
	size_t done_count = 0;
	size_t target_count = 0;

	size_t our_id = cpuid();
	bitset_word_t tmp;
	for_each_cpu(tmp, mask) {
		if (!self && tmp == our_id)
			continue;

		if (wait) {
			do_ipi(tmp, fn, ctx, &done_count);
			target_count++;
		} else {
			do_ipi(tmp, fn, ctx, NULL);
		}
	}

	if (!wait)
		return;

	// wait until all calls executed
	while (__atomic_load_n(&done_count, __ATOMIC_ACQUIRE) != target_count) {
		busyloop_hint();
	}
}
