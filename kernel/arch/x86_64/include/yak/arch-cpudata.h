#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct cpu_md {
	uint32_t apic_id;
	uint64_t apic_ticks_per_ms;
};

extern char __kernel_percpu_start[];

#define curcpu() (*(__seg_gs struct cpu *)(uintptr_t)__kernel_percpu_start)
#define curcpu_ptr() (curcpu().self)

#ifdef __cplusplus
}
#endif
