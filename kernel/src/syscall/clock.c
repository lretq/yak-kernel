#include <string.h>
#include <yak/clocksource.h>
#include <yak/types.h>
#include <yak/syscall.h>
#include <yak/timespec.h>
#include <yak-abi/errno.h>
#include <yak/timer.h>

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME 7

DEFINE_SYSCALL(SYS_CLOCK_GET, clock_get, int clock, struct timespec *ts)
{
	nstime_t timestamp;

	switch (clock) {
	case CLOCK_REALTIME:
		timestamp = nettime();
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_BOOTTIME:
		timestamp = uptime();
	default:
		return SYS_ERR(EINVAL);
	}

	struct timespec ts_now;
	ts_now.tv_sec = timestamp / STIME(1);
	ts_now.tv_nsec = timestamp - (ts_now.tv_sec * STIME(1));

	memcpy(ts, &ts_now, sizeof(struct timespec));

	return SYS_OK(0);
}
