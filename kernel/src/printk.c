#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <yak/console.h>
#include <yak/spinlock.h>
#include <yak/hint.h>
#include <yak/cpudata.h>
#include <yak/log.h>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_SNPRINTF_SAFE_EMPTY_STRING_ON_OVERFLOW 1
#include <nanoprintf.h>

#define LOG_BUF_SIZE 256

struct log_ctx {
	[[gnu::aligned(_Alignof(char))]]
	char buf[LOG_BUF_SIZE + 1];

	const char *msg;
	size_t size;
};

void console_print(struct console *console, void *private)
{
	struct log_ctx *log_ctx = private;
	if (console->write)
		console->write(console, log_ctx->msg, log_ctx->size);
}

SPINLOCK(printk_lock);

__no_san void kputs(const char *buf)
{
	struct log_ctx ctx;
	ctx.msg = buf;
	ctx.size = strlen(buf);
	int state = spinlock_lock_interrupts(&printk_lock);
	sink_foreach(console_print, &ctx);
	spinlock_unlock_interrupts(&printk_lock, state);
}

__no_san void vprintk(unsigned short level, const char *fmt, va_list args)
{
	struct log_ctx ctx;
	ctx.size = 0;
	ctx.msg = ctx.buf;

#define CASE(LEVEL, PREFIX)                                                  \
	case LEVEL:                                                          \
		ctx.size = npf_snprintf(ctx.buf, LOG_BUF_SIZE,               \
					"\x1b[0;37m[#%02zu]\x1b[0m " PREFIX, \
					cpuid());                   \
		break;

	switch (level) {
		CASE(LOG_DEBUG, "[ \x1b[35mDEBUG \x1b[0m] ");
		CASE(LOG_TRACE, "[ \x1b[36mTRACE \x1b[0m] ");
		CASE(LOG_INFO, "[ \x1b[32mINFO  \x1b[0m] ");
		CASE(LOG_WARN, "[ \x1b[33mWARN  \x1b[0m] ");
		CASE(LOG_ERROR, "[ \x1b[31mERROR \x1b[0m] ");
		CASE(LOG_FAIL, "[ \x1b[31mFAILURE \x1b[0m] ");
	default:
		break;
	}

#undef CASE

	ctx.size += npf_vsnprintf(ctx.buf + ctx.size, LOG_BUF_SIZE - ctx.size,
				  fmt, args);

	if (ctx.size > LOG_BUF_SIZE) {
		ctx.size = LOG_BUF_SIZE - 1;
	}

	ctx.buf[LOG_BUF_SIZE] = '\0';

	// interrupt >IPL_DPC might come in
	int state = spinlock_lock_interrupts(&printk_lock);
	sink_foreach(console_print, &ctx);
	spinlock_unlock_interrupts(&printk_lock, state);
}

__no_san void printk(unsigned short level, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintk(level, fmt, args);
	va_end(args);
}
