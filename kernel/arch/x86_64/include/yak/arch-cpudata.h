#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct cpu_md {
	uint32_t apic_id;
	uint64_t apic_ticks_per_ms;
};

#ifdef __cplusplus
}
#endif
