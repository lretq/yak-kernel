#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/object.h>

struct semaphore {
	struct kobject hdr;
};

void semaphore_init(struct semaphore *sem, int sigstate);
void semaphore_signal(struct semaphore *sem);
void semaphore_reset(struct semaphore *sem);

#ifdef __cplusplus
}
#endif
