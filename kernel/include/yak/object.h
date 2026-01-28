#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <yak/spinlock.h>
#include <yak/queue.h>

// Lock order: object->thread

enum kobject_type {
	// An OBJ_NOTIF object will not have
	// it's signalstate decremented on wait
	OBJ_NOTIF,
	// While an OBJ_SYNC object will have it's
	// signalstate decremented on wait!
	//
	// Examples include:
	// semaphores, (sync) events, ...
	OBJ_SYNC,
};

struct kobject_header {
	struct spinlock obj_lock;
	// immutable after creation
	enum kobject_type obj_type;
	// <=0 unsignaled, >0 signaled
	long obj_signal_count;
	// number of threads waiting on object
	size_t obj_wait_count;
	// list of wait_blocks
	TAILQ_HEAD(, wait_block) obj_wait_list;
};

void kobject_init(struct kobject_header *hdr, int signalstate,
		  enum kobject_type type);

// returns amount of threads woken
int kobject_signal_locked(struct kobject_header *hdr, bool unblock_all);

#ifdef __cplusplus
}
#endif
