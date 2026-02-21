#include <yak/panic.h>
#include <yak/log.h>
#include <yak/arch-cpu.h>
#include <yak/hint.h>
#include <stdarg.h>

#include <yak-private/profiler.h>

static int in_panic;

int is_in_panic()
{
	return __atomic_load_n(&in_panic, __ATOMIC_SEQ_CST);
}

[[gnu::no_instrument_function, gnu::noreturn]]
__no_san void panic(const char *fmt, ...)
{
#if CONFIG_PROFILER
	prof_stop();
#endif
	va_list args;
	va_start(args, fmt);

	__atomic_store_n(&in_panic, 1, __ATOMIC_SEQ_CST);

	vprintk(LOG_FAIL, fmt, args);

	va_end(args);

	hcf();
}
