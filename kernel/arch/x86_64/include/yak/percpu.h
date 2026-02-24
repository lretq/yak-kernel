#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/kernel-file.h>

#define PERCPU [[gnu::section(".percpu")]]

extern char __kernel_percpu_start[];

#define PERCPU_OFFSET(sym) \
	(((uintptr_t)&(sym)) - (uintptr_t)&__kernel_percpu_start)

#define PERCPU_FIELD_OFFSET(field) __builtin_offsetof(struct cpu, field)
#define PERCPU_FIELD_TYPE(field) __typeof__(((struct cpu *)0)->field)

#define PERCPU_FIELD_LOAD(field)                                         \
	({                                                               \
		PERCPU_FIELD_TYPE(field) __value;                        \
		switch (sizeof(PERCPU_FIELD_TYPE(field))) {              \
		case 1:                                                  \
		case 2:                                                  \
		case 4:                                                  \
		case 8:                                                  \
			asm volatile("mov %%gs:%c1, %0"                  \
				     : "=r"(__value)                     \
				     : "i"(PERCPU_FIELD_OFFSET(field))); \
			break;                                           \
		default:                                                 \
			__builtin_trap();                                \
		}                                                        \
		__value;                                                 \
	})

#define PERCPU_FIELD_STORE(field, val)                                 \
	({                                                             \
		PERCPU_FIELD_TYPE(field) __v = (val);                  \
		switch (sizeof(PERCPU_FIELD_TYPE(field))) {            \
		case 1:                                                \
		case 2:                                                \
		case 4:                                                \
		case 8:                                                \
			asm volatile("mov %0, %%gs:%c1"                \
				     :                                 \
				     : "r"(__v),                       \
				       "i"(PERCPU_FIELD_OFFSET(field)) \
				     : "memory");                      \
			break;                                         \
		default:                                               \
			__builtin_trap();                              \
		}                                                      \
	})

#define PERCPU_FIELD_XCHG(field, val)                                  \
	({                                                             \
		PERCPU_FIELD_TYPE(field) __old = (val);                \
		switch (sizeof(PERCPU_FIELD_TYPE(field))) {            \
		case 1:                                                \
		case 2:                                                \
		case 4:                                                \
		case 8:                                                \
			asm volatile("xchg %0, %%gs:%c1"               \
				     : "+r"(__old)                     \
				     : "i"(PERCPU_FIELD_OFFSET(field)) \
				     : "memory");                      \
			break;                                         \
		default:                                               \
			__builtin_trap();                              \
		}                                                      \
		__old;                                                 \
	})

#define PERCPU_BASE() ((uintptr_t)PERCPU_FIELD_LOAD(self))

#define PERCPU_PTR(sym)                               \
	({                                            \
		uintptr_t __base = PERCPU_BASE();     \
		uintptr_t __off = PERCPU_OFFSET(sym); \
		(__typeof__(&(sym)))(__base + __off); \
	})

#define PERCPU_LOAD(sym)                                         \
	({                                                       \
		__typeof__(sym) __value;                         \
		switch (sizeof(__value)) {                       \
		case 1:                                          \
		case 2:                                          \
		case 4:                                          \
		case 8:                                          \
			asm volatile("mov %%gs:(%1), %0"         \
				     : "=r"(__value)             \
				     : "r"(PERCPU_OFFSET(sym))); \
			break;                                   \
		default:                                         \
			__builtin_trap();                        \
		}                                                \
		__value;                                         \
	})

#define PERCPU_STORE(sym, val)                                           \
	do {                                                             \
		__typeof__(sym) __v = (val);                             \
		switch (sizeof(__v)) {                                   \
		case 1:                                                  \
		case 2:                                                  \
		case 4:                                                  \
		case 8:                                                  \
			asm volatile("mov %0, %%gs:(%1)"                 \
				     :                                   \
				     : "r"(__v), "r"(PERCPU_OFFSET(sym)) \
				     : "memory");                        \
			break;                                           \
		default:                                                 \
			__builtin_trap();                                \
		}                                                        \
	} while (0)

#define PERCPU_ADD(sym, val)                                             \
	do {                                                             \
		__typeof__(sym) __v = (val);                             \
		switch (sizeof(__v)) {                                   \
		case 1:                                                  \
		case 2:                                                  \
		case 4:                                                  \
		case 8:                                                  \
			asm volatile("add %0, %%gs:%c1"                  \
				     :                                   \
				     : "r"(__v), "i"(PERCPU_OFFSET(sym)) \
				     : "memory");                        \
			break;                                           \
		default:                                                 \
			__builtin_trap();                                \
		}                                                        \
	} while (0)

#ifdef __cplusplus
}
#endif
