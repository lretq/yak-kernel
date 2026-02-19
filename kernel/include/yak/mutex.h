#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/types.h>
#include <yak/object.h>
#include <yak/status.h>
#include <yak/wait.h>
#include <yak/kevent.h>
#include <yak/cleanup.h>

struct kmutex {
	struct kevent event;
#if CONFIG_DEBUG
	// for debugging purposes
	const char *name;
#endif
	struct kthread *owner;
};

void kmutex_init(struct kmutex *mutex, const char *name);
status_t kmutex_acquire(struct kmutex *mutex, nstime_t timeout);
status_t kmutex_acquire_polling(struct kmutex *mutex, nstime_t timeout);
void kmutex_release(struct kmutex *mutex);

DEFINE_CLEANUP_CLASS(
	mutex, { struct kmutex *mutex; }, { kmutex_release(ctx->mutex); },
	{
		EXPECT(kmutex_acquire(mutex, TIMEOUT_INFINITE));
		GUARD_RET(mutex, .mutex = mutex);
	},
	struct kmutex *mutex);

DEFINE_CLEANUP_CLASS(
	mutex_timeout, { struct kmutex *mutex; },
	{ kmutex_release(ctx->mutex); },
	{
		EXPECT(kmutex_acquire(mutex, timeout));
		GUARD_RET(mutex_timeout, .mutex = mutex);
	},
	struct kmutex *mutex, nstime_t timeout)

#ifdef __cplusplus
}
#endif
