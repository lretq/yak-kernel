#include <yak/ipl.h>
#include <yak/object.h>
#include <yak/spinlock.h>
#include <yak/semaphore.h>

void semaphore_init(struct semaphore *sem, int sigstate)
{
	kobject_init(&sem->hdr, sigstate, OBJ_SYNC);
}

void semaphore_signal(struct semaphore *sem)
{
	ipl_t ipl = spinlock_lock(&sem->hdr.obj_lock);

	if (0 == kobject_signal_locked(&sem->hdr, 0)) {
		// no one to be woken
		sem->hdr.obj_signal_count += 1;
	}

	spinlock_unlock(&sem->hdr.obj_lock, ipl);
	return;
}

void semaphore_reset(struct semaphore *sem)
{
	sem->hdr.obj_signal_count = 0;
}
