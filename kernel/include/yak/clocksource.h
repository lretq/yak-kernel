#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <yak/types.h>
#include <yak/status.h>

#define CLOCKSOURCE_EARLY 0x1
#define CLOCKSOURCE_NO_OFFSET 0x2

struct clocksource {
	// used for identifiying cmdline clock and logs
	const char *name;

	// monotonic! get current counter value
	uint64_t (*counter)(struct clocksource *);
	bool (*probe)();
	// optional; may be NULL
	status_t (*setup)(struct clocksource *);
	// called once per-cpu
	status_t (*init)(struct clocksource *);

	// clock frequency
	// counter+frequency calculate current timestamp
	uint64_t frequency;

	// if no cmdline option is given,
	// the source with the highest quality "wins"
	int quality;

	int flags;

	// used internally
	bool is_setup;
	struct clocksource *next;
};

void clocksource_cpudata_init();
void clocksource_early_init(nstime_t offset);
void clocksource_init();

void clocksource_register(struct clocksource *clock);
struct clocksource *clocksource_current();

nstime_t uptime();
nstime_t nettime();

#ifdef __cplusplus
}
#endif
