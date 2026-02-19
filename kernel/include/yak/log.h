#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

enum {
	LOG_DEBUG = 1,
	LOG_TRACE,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR,
	LOG_FAIL,
};

void vprintk(unsigned short level, const char *fmt, va_list args);

void kputs(const char *buf);

[[gnu::format(printf, 2, 3)]]
void printk(unsigned short level, const char *fmt, ...);

// this is similar/the same? to what linux does, it seemed like a nice way of doing it

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_extra_debug(...) do {} while(0)

#if CONFIG_DEBUG
#define pr_debug(fmt, ...) printk(LOG_DEBUG, pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug(...) do {} while(0)
#endif
#define pr_trace(fmt, ...) printk(LOG_TRACE, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(LOG_INFO, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk(LOG_WARN, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_error(fmt, ...) printk(LOG_ERROR, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_fail(fmt, ...) printk(LOG_FAIL, pr_fmt(fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
