#pragma once

#include <yak/status.h>
#include <yak/types.h>

#define TIMEOUT_INFINITE 0
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

status_t sched_wait_single(void *object, wait_mode_t wait_mode,
			   wait_type_t wait_type, nstime_t timeout);
