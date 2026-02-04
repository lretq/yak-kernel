#include <nanoprintf.h>
#include <flanterm.h>
#include <yak/sched.h>
#include <yak/vm/pmm.h>
#include <yak/cpu.h>
#include <yak/init.h>

extern size_t n_pagefaults;
extern size_t n_shootdowns;

static void kinfo_update_thread()
{
	extern struct flanterm_context *kinfo_flanterm_context;
	if (kinfo_flanterm_context == NULL)
		sched_exit_self();

	struct pmm_stat pmm_stat;

	char buf[1024];
	size_t len = 0;

	while (1) {
		len = 0;
#define bufwrite(msg, ...) \
	len += npf_snprintf(&buf[len], sizeof(buf) - len, msg, ##__VA_ARGS__);

		bufwrite("\e[H");
		bufwrite("\e[?25l");

		nstime_t boot_time = plat_getnanos();
		// convert to seconds
		boot_time /= STIME(1);

		bufwrite("system uptime: %02ld:%02ld:%02ld\n",
			 (boot_time / 60 / 60), (boot_time / 60) % 60,
			 boot_time % 60);

		pmm_get_stat(&pmm_stat);

		bufwrite(
			"%ld MiB free of %ld MiB usable (reserved: %ld MiB) pagefaults: %ld tlb shootdowns: %ld\n",
			pmm_stat.free_pages >> 8, pmm_stat.usable_pages >> 8,
			(pmm_stat.total_pages - pmm_stat.usable_pages) >> 8,
			__atomic_load_n(&n_pagefaults, __ATOMIC_RELAXED),
			__atomic_load_n(&n_shootdowns, __ATOMIC_RELAXED));
		// replace with system avg load
		bufwrite("%ld active threads, %ld online CPUs", -1UL,
			 cpus_online());

		flanterm_write(kinfo_flanterm_context, buf, len);

		ksleep(STIME(1));

#undef bufwrite
	}

	sched_exit_self();
}

void kinfo_launch()
{
	kernel_thread_create("kinfo", SCHED_PRIO_REAL_TIME, kinfo_update_thread,
			     NULL, 1, NULL);
}

INIT_ENTAILS(kinfo);
INIT_DEPS(kinfo);
INIT_NODE(kinfo, kinfo_launch);
