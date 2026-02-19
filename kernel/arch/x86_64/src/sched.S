
# struct md_pcb {
# 	uint64_t rbx, rsp, rbp, r12, r13, r14, r15;
# };

.extern sched_finalize_swtch

.globl asm_swtch
.type asm_swtch, @function
# rdi = current
# rsi = next
asm_swtch:
	# save
	mov %rbx, 0*8(%rdi)
	mov %rsp, 1*8(%rdi)
	mov %rbp, 2*8(%rdi)
	mov %r12, 3*8(%rdi)
	mov %r13, 4*8(%rdi)
	mov %r14, 5*8(%rdi)
	mov %r15, 6*8(%rdi)

	# switch off of our stack
	mov 1*8(%rsi), %rsp

	# we need rsi again, and align to 16
	push %rsi
	call sched_finalize_swtch
	pop %rsi

	# fully restore other registers
	mov 0*8(%rsi), %rbx
	mov 2*8(%rsi), %rbp
	mov 3*8(%rsi), %r12
	mov 4*8(%rsi), %r13
	mov 5*8(%rsi), %r14
	mov 6*8(%rsi), %r15

	ret

.extern xipl
.extern panic

.globl asm_thread_trampoline
.type asm_thread_trampoline, @function
# r12 => entry
# r13 => context1
# r14 => context2
asm_thread_trampoline:
	xor %rbp, %rbp
	push %rbp
	mov %rsp, %rbp

	mov $0, %rdi
	call xipl

	sti

	mov %r13, %rdi
	mov %r14, %rsi
	call *%r12

	lea trampoline_exit(%rip), %rdi
	xor %rax, %rax
	call panic

	ud2

.section .rodata
trampoline_exit:
	.asciz "thread trampoline has returned :(\n"
