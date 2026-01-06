#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <heap.h>
#include <yak/spinlock.h>
#include <yak/object.h>
#include <yak/types.h>
#include <yak/dpc.h>
#include <yak/status.h>

enum {
	TIMER_STATE_UNUSED = 1,
	TIMER_STATE_QUEUED,
	TIMER_STATE_CANCELED,
	TIMER_STATE_FIRED,
};

#define USTIME(us) (us * 1000ULL)
#define MSTIME(ms) (ms * 1000ULL * 1000ULL)
#define STIME(s) (s * 1000ULL * 1000ULL * 1000ULL)
#define BIGTIME(s, m, h, d)                              \
	(STIME(s) + STIME(m * 60) + STIME(h * 60 * 60) + \
	 STIME(d * 24 * 60 * 60))

#define TIMER_INFINITE UINT64_MAX

struct timer;

// get nanoseconds from a monotonic ns clock
nstime_t plat_getnanos();
// arm to deadline relative to clock from above
void plat_arm_timer(nstime_t deadline);

struct timespec time_now();

struct timer {
	struct kobject_header hdr;

	struct cpu *cpu;

	nstime_t deadline;
	short state;

	HEAP_ENTRY(timer) entry;
};

void timer_init(struct timer *timer);
void timer_reset(struct timer *timer);

status_t timer_install(struct timer *timer, nstime_t ns_delta);
void timer_uninstall(struct timer *timer);

void ksleep(nstime_t ns);
void kstall(nstime_t ns);

#ifdef __cplusplus
}
#endif
