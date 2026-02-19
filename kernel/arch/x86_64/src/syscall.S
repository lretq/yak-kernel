
.text

	.globl _syscall_fork_trampoline
	.balign 16
	.type _syscall_fork_trampoline, @function
_syscall_fork_trampoline:
	# disable interrupts as we are switching back 
	# to a user context
	cli
	
	# remove the alignment
	add $8, %rsp

	# the syscall frame starts now
	pop %rax
	pop %rbx
	pop %rcx
	pop %rdx
	pop %rdi
	pop %rsi
	pop %r8
	pop %r9
	pop %r10
	pop %r11
	pop %r12
	pop %r13
	pop %r14
	pop %r15
	pop %rbp
	
	# switch to user stack
	pop %rsp
	# and to user gsbase
	swapgs
	
	# finally, return
	sysretq
