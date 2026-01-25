#include <yak/cpudata.h>
#include <yak/status.h>
#include <yak/kevent.h>
#include <yak/sched.h>
#include <yak/object.h>
#include <yak/rwlock.h>

#define RWLOCK_UNLOCKED 0
#define RWLOCK_EXCLUSIVE (1U << 31)
#define RWLOCK_READER_MASK (~RWLOCK_EXCLUSIVE)

void rwlock_init(struct rwlock *rwlock, [[maybe_unused]] const char *name)
{
	event_init(&rwlock->event, 0);
#ifdef CONFIG_DEBUG
	rwlock->name = name;
#endif
	rwlock->state = 0;
	rwlock->exclusive_count = 0;
	rwlock->exclusive_owner = NULL;
}

status_t rwlock_acquire_shared(struct rwlock *rwlock, nstime_t timeout)
{
	assert(rwlock);

	status_t status;

	do {
		uint32_t state =
			__atomic_load_n(&rwlock->state, __ATOMIC_RELAXED);

		// either locked exclusive or waiting to clear shared locks
		if ((state & RWLOCK_EXCLUSIVE) != 0 ||
		    __atomic_load_n(&rwlock->exclusive_count,
				    __ATOMIC_ACQUIRE)) {
			goto wait;
		}

		// try to increase shared lock count
		if (likely(__atomic_compare_exchange_n(
			    &rwlock->state, &state, state + 1, 0,
			    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))) {
			// the rwlock now has one more shared lock
			break;
		}

wait:
		status = sched_wait_single(&rwlock->event, WAIT_MODE_BLOCK,
					   WAIT_TYPE_ALL, timeout);

		IF_ERR(status)
		{
			return status;
		}

	} while (1);

	return YAK_SUCCESS;
}

void rwlock_release_shared(struct rwlock *rwlock)
{
	assert((__atomic_load_n(&rwlock->state, __ATOMIC_RELAXED) &
		~RWLOCK_EXCLUSIVE) > 0);
	uint32_t prev = __atomic_fetch_sub(&rwlock->state, 1, __ATOMIC_RELEASE);
	// we should never underflow
	assert((prev & RWLOCK_READER_MASK) > 0);
	// only alarm when we're the last one
	// No need to wake up exclusive waiters if there is still someone left
	if ((prev & RWLOCK_READER_MASK) == 1)
		event_alarm(&rwlock->event);
}

status_t rwlock_acquire_exclusive(struct rwlock *rwlock, nstime_t timeout)
{
	status_t status;

	__atomic_fetch_add(&rwlock->exclusive_count, 1, __ATOMIC_ACQ_REL);

	do {
		uint32_t state =
			__atomic_load_n(&rwlock->state, __ATOMIC_RELAXED);

		if (state != 0) {
			// already locked or still has shared readers
			goto wait;
		}

		// try to set the RWLOCK_EXCLUSIVE flag
		if (likely(__atomic_compare_exchange_n(
			    &rwlock->state, &state, RWLOCK_EXCLUSIVE, 0,
			    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))) {
			assert(rwlock_fetch_owner(rwlock) == NULL);
			// TODO: what atomicity is needed here
			__atomic_store_n(&rwlock->exclusive_owner, curthread(),
					 __ATOMIC_SEQ_CST);
			return YAK_SUCCESS;
		}

		// someone was faster than us, wait for release

wait:
		status = sched_wait_single(&rwlock->event, WAIT_MODE_BLOCK,
					   WAIT_TYPE_ALL, timeout);

		IF_ERR(status)
		{
			__atomic_fetch_sub(&rwlock->exclusive_count, 1,
					   __ATOMIC_ACQ_REL);
			return status;
		}

	} while (1);
}

void rwlock_release_exclusive(struct rwlock *rwlock)
{
	assert(rwlock_fetch_owner(rwlock) == curthread());
	__atomic_fetch_sub(&rwlock->exclusive_count, 1, __ATOMIC_ACQ_REL);
	__atomic_store_n(&rwlock->exclusive_owner, NULL, __ATOMIC_RELEASE);
	__atomic_fetch_and(&rwlock->state, ~RWLOCK_EXCLUSIVE, __ATOMIC_RELEASE);
	event_alarm(&rwlock->event);
}

void rwlock_upgrade_to_exclusive(struct rwlock *rwlock)
{
	// we still currently hold our shared lock

	uint32_t state = __atomic_load_n(&rwlock->state, __ATOMIC_RELAXED);

	__atomic_fetch_add(&rwlock->exclusive_count, 1, __ATOMIC_ACQ_REL);

	if (state == 1) {
		// try to replace our shared reader lock with an exclusive lock
		if (likely(__atomic_compare_exchange_n(
			    &rwlock->state, &state, RWLOCK_EXCLUSIVE, 0,
			    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))) {
			assert(rwlock_fetch_owner(rwlock) == NULL);
			// TODO: what atomicity is needed here
			__atomic_store_n(&rwlock->exclusive_owner, curthread(),
					 __ATOMIC_SEQ_CST);
			return;
		}
	}

	__atomic_fetch_sub(&rwlock->exclusive_count, 1, __ATOMIC_ACQ_REL);

	// Fallback: can't upgrade right now, release and re-acquire lock exclusively

	rwlock_release_shared(rwlock);
	EXPECT(rwlock_acquire_exclusive(rwlock, TIMEOUT_INFINITE));
}
