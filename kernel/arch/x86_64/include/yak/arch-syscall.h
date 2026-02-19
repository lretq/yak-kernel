#pragma once

#include <stdint.h>

#define ARCHCTL_SET_FSBASE 1
#define ARCHCTL_SET_GSBASE 2

struct syscall_result {
	uintptr_t retval;
	long err;
};

static inline struct syscall_result __syscall0(int num)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num)
		     : "rcx", "r11", "memory");
	return res;
}

static inline struct syscall_result __syscall1(int num, uint64_t arg1)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	register uint64_t _arg1 asm("rdi") = arg1;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num), "r"(_arg1)
		     : "rcx", "r11", "memory");
	return res;
}

static inline struct syscall_result __syscall2(int num, uint64_t arg1,
					       uint64_t arg2)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	register uint64_t _arg1 asm("rdi") = arg1;
	register uint64_t _arg2 asm("rsi") = arg2;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num), "r"(_arg1), "r"(_arg2)
		     : "rcx", "r11", "memory");
	return res;
}

static inline struct syscall_result __syscall3(int num, uint64_t arg1,
					       uint64_t arg2, uint64_t arg3)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	register uint64_t _arg1 asm("rdi") = arg1;
	register uint64_t _arg2 asm("rsi") = arg2;
	register uint64_t _arg3 asm("rdx") = arg3;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3)
		     : "rcx", "r11", "memory");
	return res;
}

static inline struct syscall_result
__syscall4(int num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	register uint64_t _arg1 asm("rdi") = arg1;
	register uint64_t _arg2 asm("rsi") = arg2;
	register uint64_t _arg3 asm("rdx") = arg3;
	register uint64_t _arg4 asm("r10") = arg4;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)
		     : "rcx", "r11", "memory");
	return res;
}

static inline struct syscall_result __syscall5(int num, uint64_t arg1,
					       uint64_t arg2, uint64_t arg3,
					       uint64_t arg4, uint64_t arg5)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	register uint64_t _arg1 asm("rdi") = arg1;
	register uint64_t _arg2 asm("rsi") = arg2;
	register uint64_t _arg3 asm("rdx") = arg3;
	register uint64_t _arg4 asm("r10") = arg4;
	register uint64_t _arg5 asm("r8") = arg5;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3),
		       "r"(_arg4), "r"(_arg5)
		     : "rcx", "r11", "memory");
	return res;
}

static inline struct syscall_result __syscall6(int num, uint64_t arg1,
					       uint64_t arg2, uint64_t arg3,
					       uint64_t arg4, uint64_t arg5,
					       uint64_t arg6)
{
	struct syscall_result res;
	register uint64_t _num asm("rax") = num;
	register uint64_t _arg1 asm("rdi") = arg1;
	register uint64_t _arg2 asm("rsi") = arg2;
	register uint64_t _arg3 asm("rdx") = arg3;
	register uint64_t _arg4 asm("r10") = arg4;
	register uint64_t _arg5 asm("r8") = arg5;
	register uint64_t _arg6 asm("r9") = arg6;
	asm volatile("syscall"
		     : "=a"(res.retval), "=d"(res.err)
		     : "a"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3),
		       "r"(_arg4), "r"(_arg5), "r"(_arg6)
		     : "rcx", "r11", "memory");
	return res;
}
