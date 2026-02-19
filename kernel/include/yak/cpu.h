#pragma once

#include <stddef.h>
#include <yak/bitset.h>

#define MAX_NR_CPUS 256ULL

DECLARE_BITSET_TYPE(cpumask, MAX_NR_CPUS);

#define for_each_cpu(index, mask) for_each_bit(index, mask, MAX_NR_CPUS)

extern struct cpumask cpumask_active;

void cpu_init();
void cpu_up(size_t id);

size_t cpus_online();
