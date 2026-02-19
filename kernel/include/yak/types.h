#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

typedef size_t voff_t;

typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int64_t off64_t;

// time in seconds
typedef long time_t;

// ~600 years
typedef uint64_t nstime_t;

typedef long long pid_t;

#ifdef __cplusplus
}
#endif
