#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/arch-cpu.h>
#include <yak/ipl.h>

// #define SPINLOCK_DEBUG_OWNER

#if defined(SPINLOCK_DEBUG_OWNER)
#define SPINLOCK_DEBUG 1
#endif

#define SPINLOCK_UNLOCKED 0
#define SPINLOCK_LOCKED 1

struct spinlock {
	int state;
#ifdef SPINLOCK_DEBUG_OWNER
	struct kthread *owner;
#endif
};

#define SPINLOCK(name) struct spinlock name = SPINLOCK_INITIALIZER()

#ifdef SPINLOCK_DEBUG_OWNER
#define SPINLOCK_INITIALIZER() { .state = SPINLOCK_UNLOCKED, .owner = NULL }
#define spinlock_init(spinlock)                        \
	do {                                           \
		(spinlock)->state = SPINLOCK_UNLOCKED; \
		(spinlock)->owner = NULL;              \
	} while (0)
#else
#define SPINLOCK_INITIALIZER() { .state = SPINLOCK_UNLOCKED }
#define spinlock_init(spinlock)                        \
	do {                                           \
		(spinlock)->state = SPINLOCK_UNLOCKED; \
	} while (0)
#endif

#ifdef x86_64
#define busyloop_hint() asm volatile("pause" ::: "memory");
#else
#warning "unimplemented busyloop_hint"
#define busyloop_hint()
#endif

static inline int spinlock_trylock(struct spinlock *lock)
{
	int unlocked = SPINLOCK_UNLOCKED;
	return __atomic_compare_exchange_n(&lock->state, &unlocked,
					   SPINLOCK_LOCKED, 0, __ATOMIC_ACQ_REL,
					   __ATOMIC_RELAXED);
}

#ifndef SPINLOCK_DEBUG
static inline void spinlock_lock_noipl(struct spinlock *lock)
{
	while (!spinlock_trylock(lock)) {
		busyloop_hint();
	}
}

static inline void spinlock_unlock_noipl(struct spinlock *lock)
{
	__atomic_store_n(&lock->state, SPINLOCK_UNLOCKED, __ATOMIC_RELEASE);
}
#else
void spinlock_lock_noipl(struct spinlock *lock);
void spinlock_unlock_noipl(struct spinlock *lock);
#endif

static inline ipl_t spinlock_lock(struct spinlock *lock)
{
	ipl_t ipl = ripl(IPL_DPC);
	spinlock_lock_noipl(lock);
	return ipl;
}

static inline int spinlock_lock_interrupts(struct spinlock *lock)
{
	int state = disable_interrupts();
	spinlock_lock_noipl(lock);
	return state;
}

static inline void spinlock_unlock_interrupts(struct spinlock *lock, int state)
{
	spinlock_unlock_noipl(lock);
	if (state)
		enable_interrupts();
}

static inline void spinlock_unlock(struct spinlock *lock, ipl_t ipl)
{
	spinlock_unlock_noipl(lock);
	xipl(ipl);
}

static inline int spinlock_held(struct spinlock *lock)
{
	return __atomic_load_n(&lock->state, __ATOMIC_RELAXED) ==
	       SPINLOCK_LOCKED;
}

struct ticket_spinlock {
	int ticket;
	int current;
};

#define TICKET_SPINLOCK(name) \
	struct ticket_spinlock name = TICKET_SPINLOCK_INITIALIZER()

#define TICKET_SPINLOCK_INITIALIZER() { .ticket = 0, .current = 0 }

#define ticket_spinlock_init(lock)                                       \
	do {                                                             \
		__atomic_store_n(&(lock)->ticket, 0, __ATOMIC_RELEASE);  \
		__atomic_store_n(&(lock)->current, 0, __ATOMIC_RELEASE); \
	} while (0)

static inline void ticket_spinlock_lock_noipl(struct ticket_spinlock *lock)
{
	int t = __atomic_fetch_add(&lock->ticket, 1, __ATOMIC_RELAXED);
	while (__atomic_load_n(&lock->current, __ATOMIC_ACQUIRE) != t) {
		busyloop_hint();
	}
}

static inline ipl_t ticket_spinlock_lock(struct ticket_spinlock *lock)
{
	ipl_t ipl = ripl(IPL_DPC);
	ticket_spinlock_lock_noipl(lock);
	return ipl;
}

#ifdef __cplusplus
}
#endif
