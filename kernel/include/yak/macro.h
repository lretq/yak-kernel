#pragma once

#include <stdint.h>

#define _STR(x) #x
#define STR(x) _STR(x)

#define PASTE(a, b) a##b

#define EXPAND_AND_PASTE(a, b) PASTE(a, b)

#define EXPAND(a) a

#define ALIGN_UP(addr, align) (((addr) + align - 1) & ~(align - 1))
#define ALIGN_DOWN(addr, align) (((uintptr_t)(addr)) & ~((align) - 1))

#define IS_ALIGNED_POW2(addr, align) (((addr) & ((align) - 1)) == 0)

#define SWAP(a, b) (((a) ^= (b)), ((b) ^= (a)), ((a) ^= (b)))

#define DIV_ROUNDUP(a, b) (((a) + ((b) - 1)) / (b))

#define container_of(ptr, type, member)                            \
	({                                                         \
		const typeof(((type *)0)->member) *__mptr = (ptr); \
		(type *)((char *)__mptr - offsetof(type, member)); \
	})

#define elementsof(arr) (sizeof((arr)) / sizeof((arr)[0]))

// beware: 0 is incorrectly considered a power of two by this
#define P2CHECK(v) (((v) & ((v) - 1)) == 0)

#define MAX(a, b)                       \
	({                              \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})

#define MIN(a, b)                       \
	({                              \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a < _b ? _a : _b;      \
	})

#ifdef _LP64
static inline unsigned int ilog2(uint64_t number)
{
	return 64 - __builtin_clzll(number);
}

static inline uintptr_t next_ilog2(uintptr_t num)
{
	return num <= 1 ? 1 : (64 - __builtin_clzll(num - 1));
}
#else
static inline unsigned int ilog2(uint32_t number)
{
	return 32 - __builtin_clz(number);
}

static inline uintptr_t next_ilog2(uintptr_t number)
{
	return num <= 1 ? 1 : (32 - __builtin_clzll(num - 1));
}
#endif

#define CHECKED_CAST(src_expr, src_type, dst_type)                            \
	({                                                                    \
		_Static_assert(__builtin_types_compatible_p(typeof(src_expr), \
							    src_type),        \
			       "checked cast: wrong source type");            \
		(dst_type)(src_expr);                                         \
	})
