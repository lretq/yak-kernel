#pragma once

#include <stddef.h>
#include <limits.h>
#include <yak/macro.h>

typedef unsigned long bitset_word_t;

#define BITSET_WORD_BITS (sizeof(bitset_word_t) * CHAR_BIT)

// Number of words needed for N bits
#define BITSET_WORDS(nbits) DIV_ROUNDUP(nbits, BITSET_WORD_BITS)

// Word index of bit
#define BITSET_WORD_IDX(bit) ((bit) / BITSET_WORD_BITS)

// Bit offset inside word
#define BITSET_BIT_IDX(bit) ((bit) & (BITSET_WORD_BITS - 1))

// Bit mask inside word
#define BITSET_MASK(bit) ((bitset_word_t)1UL << BITSET_BIT_IDX(bit))

#define DECLARE_BITSET_TYPE(name, nbits)                 \
	struct name {                                    \
		bitset_word_t bits[BITSET_WORDS(nbits)]; \
	}

#define bitset_init(bs)                                                \
	for (size_t i = 0; i < elementsof((bs)->bits); i++) {         \
		__atomic_store_n(&(bs)->bits[i], 0, __ATOMIC_RELAXED); \
	}

#define bitset_set(bs, bit) \
	((bs)->bits[BITSET_WORD_IDX(bit)] |= BITSET_MASK(bit))

#define bitset_clear(bs, bit) \
	((bs)->bits[BITSET_WORD_IDX(bit)] &= ~BITSET_MASK(bit))

#define bitset_test(bs, bit) \
	(((bs)->bits[BITSET_WORD_IDX(bit)] & BITSET_MASK(bit)) != 0)

#define bitset_atomic_set(bs, bit)                                             \
	__atomic_fetch_or(&(bs)->bits[BITSET_WORD_IDX(bit)], BITSET_MASK(bit), \
			  __ATOMIC_RELEASE)

#define bitset_atomic_clear(bs, bit)                          \
	__atomic_fetch_and(&(bs)->bits[BITSET_WORD_IDX(bit)], \
			   ~BITSET_MASK(bit), __ATOMIC_RELEASE)

#define bitset_atomic_test(bs, bit)                         \
	(__atomic_load_n(&(bs)->bits[BITSET_WORD_IDX(bit)], \
			 __ATOMIC_ACQUIRE) &                \
	 BITSET_MASK(bit))

// bit: index variable
// bs: bitset
// nbits: bits in type
#define for_each_bit(bit, bs, nbits)                                          \
	for (size_t _w = 0; _w < BITSET_WORDS(nbits); _w++)                   \
		for (bitset_word_t _tmp = __atomic_load_n(&(bs)->bits[_w],    \
							  __ATOMIC_ACQUIRE);  \
		     _tmp != 0 &&                                             \
		     (((bit) = _w * BITSET_WORD_BITS + __builtin_ctzl(_tmp)), \
		      1);                                                     \
		     _tmp &= (_tmp - 1))
