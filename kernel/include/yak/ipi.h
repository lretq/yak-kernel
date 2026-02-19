#pragma once

#include <yak/spinlock.h>
#include <yak/ringbuffer.h>
#include <yak/cpu.h>

#define RC_MAX_QUEUE 32

typedef void (*remote_fn_t)(void *);

struct remote_call {
	remote_fn_t rc_fn;
	void *rc_ctx;
	// optional ack counter
	size_t *rc_done;
};

struct remote_call_queue {
	struct spinlock rcq_lock;
	struct ringbuffer rcq_buffer;
	struct remote_call rcq_backing[RC_MAX_QUEUE];
};

void rcq_init(struct remote_call_queue *rcq);

#define IPI_SEND_OTHERS (~0ULL)

void ipi_send(size_t cpu, remote_fn_t fn, void *arg);
void ipi_send_wait(size_t cpu, remote_fn_t fn, void *ctx);
void ipi_mask_send(struct cpumask *mask, remote_fn_t fn, void *ctx, bool self,
		   bool wait);
