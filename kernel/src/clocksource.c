#include <stdint.h>
#include <yak/spinlock.h>
#include <yak/log.h>
#include <yak/clocksource.h>
#include <yak/status.h>
#include <yak/percpu.h>
#include <yak/cpudata.h>

static uint64_t dummy_counter(struct clocksource *)
{
	static uint64_t ticks;
	return ticks++;
}

static status_t dummy_init(struct clocksource *)
{
	return YAK_SUCCESS;
}

static struct clocksource dummy_clock = {
	.counter = dummy_counter,
	.probe = NULL,
	.setup = NULL,
	.init = dummy_init,
	.frequency = 1000000000,
	.quality = -10000,
	.name = "dummy",
	.flags = CLOCKSOURCE_EARLY,
	.is_setup = true,
	.next = NULL,
};

static struct clocksource *clock_list = &dummy_clock;
static struct spinlock clock_lock = SPINLOCK_INITIALIZER();

struct clocksource *clocksource_current()
{
	return PERCPU_FIELD_LOAD(clocksource);
}

void clocksource_switch(struct clocksource *clock)
{
	bool ipl = spinlock_lock_interrupts(&clock_lock);
	if (!clock->is_setup) {
		EXPECT(clock->setup(clock));
		clock->is_setup = true;
	}

	clock->init(clock);

	if (clock->flags & CLOCKSOURCE_NO_OFFSET) {
		PERCPU_FIELD_STORE(clock_offset, 0);
	} else {
		PERCPU_FIELD_STORE(clock_offset, uptime());
	}

	PERCPU_FIELD_STORE(clocksource, clock);

	spinlock_unlock_interrupts(&clock_lock, ipl);

	pr_info("using clocksource '%s'\n", clock->name);
}

static struct clocksource *select_source(int min_quality, bool early)
{
	struct clocksource *elm = clock_list;
	struct clocksource *best = NULL;
	do {
		if (early && (elm->flags & CLOCKSOURCE_EARLY) == 0)
			continue;

		if (elm->probe != NULL && !elm->probe())
			continue;

		if (elm->quality < min_quality)
			continue;

		if (best == NULL || elm->quality > best->quality)
			best = elm;

	} while ((elm = elm->next));

	return best;
}

static nstime_t system_realtime_offset;

void clocksource_cpudata_init()
{
	PERCPU_FIELD_STORE(clocksource, &dummy_clock);
	PERCPU_FIELD_STORE(clock_offset, 0);
}

void clocksource_early_init(nstime_t offset)
{
	system_realtime_offset = offset;

	bool ipl = spinlock_lock_interrupts(&clock_lock);
	struct clocksource *source = select_source(-1, true);
	spinlock_unlock_interrupts(&clock_lock, ipl);
	assert(source && "no early clocksource");

	clocksource_switch(source);
}

void clocksource_init()
{
	bool ipl = spinlock_lock_interrupts(&clock_lock);
	// any is fine, we're not in boot anymore
	struct clocksource *source = select_source(-1, false);
	spinlock_unlock_interrupts(&clock_lock, ipl);

	if (clocksource_current() != source)
		clocksource_switch(source);
}
void clocksource_register(struct clocksource *clk)
{
	bool ipl = spinlock_lock_interrupts(&clock_lock);
	clk->next = NULL;
	clk->is_setup = clk->setup == NULL ? 1 : 0;

	struct clocksource *tmp = clock_list;
	while (tmp->next)
		tmp = tmp->next;

	tmp->next = clk;

	spinlock_unlock_interrupts(&clock_lock, ipl);

	pr_debug("registered clocksource '%s'\n", clk->name);
}

nstime_t uptime()
{
	struct clocksource *source = PERCPU_FIELD_LOAD(clocksource);
	nstime_t offset = PERCPU_FIELD_LOAD(clock_offset);
	uint64_t ctr = source->counter(source);
	uint64_t freq = source->frequency;

	uint64_t quotient = ctr / freq;
	uint64_t remainder = ctr % freq;

	uint64_t ns = quotient * 1000000000ULL;
	ns += (remainder * 1000000000ULL) / freq;

	return offset + ns;
}

nstime_t nettime()
{
	return uptime() + system_realtime_offset;
}
