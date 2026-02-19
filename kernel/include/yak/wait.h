#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <yak/status.h>
#include <yak/types.h>
#include <yak/queue.h>

#define TIMEOUT_INFINITE (((nstime_t)UINT64_MAX))
#define POLL_ONCE 0

typedef enum {
	/* unblock when any object is ready */
	WAIT_TYPE_ANY = 1,
	/* unblock when all objects are ready */
	WAIT_TYPE_ALL = 2,
} wait_type_t;

typedef enum {
	WAIT_MODE_BLOCK = 1,
	WAIT_MODE_POLL,
} wait_mode_t;

#define WB_DEQUEUED 0x1
#define WB_UNWAITED 0x2

struct wait_block {
	// thread waiting
	struct kthread *thread;
	// object being waited on
	void *object;
	// status to set in the thread for WAIT_TYPE_ANY
	status_t status;
	unsigned short flags;
	// for inserting into object wait list
	TAILQ_ENTRY(wait_block) entry;
};

status_t sched_wait_many(struct wait_block *table, void **objects, size_t count,
			 wait_mode_t wait_mode, wait_type_t wait_type,
			 nstime_t timeout);

// Wait Type is only available for sched_wait_many
// as ALL/ANY distinction does not make sense for a single object
status_t sched_wait(void *object, wait_mode_t wait_mode, nstime_t timeout);

#ifdef __cplusplus
}
#endif
