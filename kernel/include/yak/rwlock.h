#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/object.h>
#include <yak/status.h>
#include <yak/types.h>
#include <yak/kevent.h>
#include <yak/cleanup.h>

struct rwlock {
	struct kevent event;
#if CONFIG_DEBUG
	const char *name;
#endif
	struct kthread *exclusive_owner;
	uint32_t state;
	unsigned int exclusive_count;
};

void rwlock_init(struct rwlock *rwlock, const char *name);

status_t rwlock_acquire_shared(struct rwlock *rwlock, nstime_t timeout);
void rwlock_release_shared(struct rwlock *rwlock);

status_t rwlock_acquire_exclusive(struct rwlock *rwlock, nstime_t timeout);
void rwlock_release_exclusive(struct rwlock *rwlock);

void rwlock_upgrade_to_exclusive(struct rwlock *rwlock);

static inline struct kthread *rwlock_fetch_owner(struct rwlock *rwlock)
{
	return __atomic_load_n(&rwlock->exclusive_owner, __ATOMIC_ACQUIRE);
}

enum {
	RWLOCK_GUARD_SHARED,
	RWLOCK_GUARD_EXCLUSIVE,
	RWLOCK_GUARD_SKIP,
};

DEFINE_CLEANUP_CLASS(
	rwlock,
	{
		struct rwlock *lock;
		int type;
	},
	{
		switch (ctx->type) {
		case RWLOCK_GUARD_EXCLUSIVE:
			rwlock_release_exclusive(ctx->lock);
			break;
		case RWLOCK_GUARD_SHARED:
			rwlock_release_shared(ctx->lock);
			break;
		}
	},
	{
		status_t result = YAK_SUCCESS;
		if (type == RWLOCK_GUARD_EXCLUSIVE) {
			result = rwlock_acquire_exclusive(lock, timeout);
		} else if (type == RWLOCK_GUARD_SHARED) {
			result = rwlock_acquire_shared(lock, timeout);
		}

		EXPECT(result);

		GUARD_RET(rwlock, lock, type);
	},
	struct rwlock *lock, nstime_t timeout, int type);

#ifdef __cplusplus
}
#endif
