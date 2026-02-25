#include <stdint.h>
#include <yak/cpudata.h>
#include <yak/cpu.h>
#include <yak/log.h>
#include <yak/types.h>
#include <yak/spinlock.h>
#include <yak/clocksource.h>
#include <yak/status.h>
#include <yak/vm/pmm.h>

#include "asm.h"

/*
 * References:
 * https://www.kernel.org/doc/html/v5.6/virt/kvm/cpuid.html
 * https://www.kernel.org/doc/html/v5.9/virt/kvm/msr.html
 */

struct pvclock_vcpu_time_info {
	uint32_t version;
	uint32_t pad0;
	uint64_t tsc_timestamp;
	uint64_t system_time;
	uint32_t tsc_to_system_mul;
	int8_t tsc_shift;
	uint8_t flags;
	uint8_t pad[2];
}; /* 32 bytes */

_Static_assert(sizeof(struct pvclock_vcpu_time_info) == 32);

#define MSR_KVM_WALL_CLOCK_NEW 0x4b564d00
#define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01

#define MSR_KVM_WALL_CLOCK 0x11
#define MSR_KVM_SYSTEM_TIME 0x12

#define KVM_SYSTEM_TIME_ENABLE (1 << 0)

static int msr_kvm_system_time;
static int msr_kvm_wall_clock;
static volatile struct pvclock_vcpu_time_info *info = NULL;

static int version;

static bool kvmclock_probe()
{
	uint32_t a, b, c, d;
	asm_cpuid(0x40000000, 0, &a, &b, &c, &d);

	if (b != 0x4b4d564b || c != 0x564b4d56 || d != 0x4d)
		return false;

	asm_cpuid(0x40000001, 0, &a, &b, &c, &d);

	if (a & (1 << 3)) {
		version = 2;
	} else if (a & (1 << 0)) {
		version = 1;
	} else {
		version = -1;
		return false;
	}

	return true;
}

static status_t kvmclock_setup(struct clocksource *)
{
	if (version == 2) {
		msr_kvm_system_time = MSR_KVM_SYSTEM_TIME_NEW;
		msr_kvm_wall_clock = MSR_KVM_WALL_CLOCK_NEW;
	} else if (version == 1) {
		msr_kvm_system_time = MSR_KVM_SYSTEM_TIME;
		msr_kvm_wall_clock = MSR_KVM_WALL_CLOCK;
	} else {
		panic("probe changed???");
	}

	size_t size = sizeof(struct pvclock_vcpu_time_info) * MAX_NR_CPUS;
	int order = pmm_bytes_to_order(size);

	pr_info("kvmclock alloc order: %d\n", order);

	struct page *page = pmm_alloc_order(order);
	info = (volatile void *)page_to_mapped_addr(page);

	return YAK_SUCCESS;
}

static status_t kvmclock_init(struct clocksource *)
{
	if (!info)
		return YAK_NOT_SUPPORTED;
	volatile struct pvclock_vcpu_time_info *cpu_info = &info[cpuid()];
	wrmsr(msr_kvm_system_time,
	      v2p((vaddr_t)cpu_info) | KVM_SYSTEM_TIME_ENABLE);
	return YAK_SUCCESS;
}

static inline uint64_t rdtsc_serialized()
{
	uint32_t low, high;
	// serialize
	asm volatile("mfence\n\t"
		     "lfence\n\t"
		     "rdtsc"
		     : "=a"(low), "=d"(high)::"rbx", "rcx");
	return ((uint64_t)high << 32) | low;
}

static uint64_t kvmclock_counter(struct clocksource *)
{
	volatile struct pvclock_vcpu_time_info *cpu_info = &info[cpuid()];

	uint32_t version;
	uint64_t ns;

	do {
		version = __atomic_load_n(&cpu_info->version, __ATOMIC_ACQUIRE);

		ns = rdtsc_serialized();
		ns -= cpu_info->tsc_timestamp;
		if (cpu_info->tsc_shift < 0)
			ns >>= -cpu_info->tsc_shift;
		else
			ns <<= cpu_info->tsc_shift;

		asm volatile("mulq %%rdx; shrd $32, %%rdx, %%rax"
			     : "=a"(ns)
			     : "a"(ns), "d"(cpu_info->tsc_to_system_mul));

		ns += cpu_info->system_time;
	} while ((cpu_info->version & 1) != 0 || cpu_info->version != version);

	return ns;
}

static struct clocksource kvm_clocksource = {
	.name = "kvmclock",
	.counter = kvmclock_counter,
	.probe = kvmclock_probe,
	.setup = kvmclock_setup,
	.init = kvmclock_init,
	// system_time is in ns
	.frequency = 1e9,
	.quality = 1000,
	.flags = CLOCKSOURCE_EARLY | CLOCKSOURCE_NO_OFFSET,
};

void kvmclock_register()
{
	clocksource_register(&kvm_clocksource);
}
