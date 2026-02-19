#pragma once

#define __hot __attribute__((hot))
#define __cold __attribute__((cold))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define __no_san __attribute__((no_sanitize("undefined"))) __attribute__((no_sanitize("address")))

#define __noreturn __attribute__((noreturn))
#define __always_inline __attribute__((always_inline))
#define __no_prof __attribute__((no_instrument_function))

#define __used __attribute__((used))
#define __unused __attribute__((unused))
