#pragma once

#include <yak-abi/syscall.h>
#include <yak/arch-context.h>
#include <yak/arch-syscall.h>

#define MAX_SYSCALLS 256

typedef struct syscall_result (*syscall_fn)(struct syscall_frame *, long, long,
					    long, long, long, long);
extern syscall_fn syscall_table[MAX_SYSCALLS];

#define DEFINE_SYSCALL(num, name, ...)                                \
	struct syscall_result sys_##name(                             \
		[[maybe_unused]] struct syscall_frame *__syscall_ctx, \
		##__VA_ARGS__)

#define SYS_RESULT(rv, errcode)              \
	(struct syscall_result)              \
	{                                    \
		.retval = rv, .err = errcode \
	}

#define SYS_OK(rv) SYS_RESULT(rv, 0)
#define SYS_ERR(errno) SYS_RESULT(-1, errno)

#define _RET_ERRNO_ON_ERR_INTERNAL(expr, resvar)              \
	do {                                                  \
		status_t resvar = expr;                       \
		if (IS_ERR(resvar) && resvar != YAK_EOF) {    \
			return SYS_ERR(status_errno(resvar)); \
		}                                             \
	} while (0)

#define RET_ERRNO_ON_ERR(expr)           \
	_RET_ERRNO_ON_ERR_INTERNAL(expr, \
				   EXPAND_AND_PASTE(__autoret, __COUNTER__))
