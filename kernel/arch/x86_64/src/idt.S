.text

.set .L_NO_CODE, 1
.set .L_CODE, 2

.macro PUSHA64
	push %rbp
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rbp, 0

	push %r15
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r15, 0

	push %r14
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r14, 0

	push %r13
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r13, 0

	push %r12
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r12, 0

	push %r11
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r11, 0

	push %r10
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r10, 0

	push %r9
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r9, 0

	push %r8
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %r8, 0

	push %rdi
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rdi, 0

	push %rsi
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rsi, 0

	push %rdx
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rdx, 0

	push %rcx
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rcx, 0

	push %rbx
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rbx, 0

	push %rax
	.cfi_adjust_cfa_offset 8
	.cfi_rel_offset %rax, 0
.endm

.macro POPA64
	pop %rax
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rax

	pop %rbx
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rbx

	pop %rcx
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rcx

	pop %rdx
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rdx

	pop %rsi
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rsi

	pop %rdi
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rdi

	pop %r8
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r8

	pop %r9
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r9

	pop %r10
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r10

	pop %r11
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r11

	pop %r12
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r12

	pop %r13
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r13

	pop %r14
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r14

	pop %r15
	.cfi_adjust_cfa_offset -8
	.cfi_restore %r15

	pop %rbp
	.cfi_adjust_cfa_offset -8
	.cfi_restore %rbp
.endm

.macro ISR_STUB type, number

	.balign 16
	.type isrStub\number, @function

isrStub\number:
	.cfi_startproc

	.if \type == .L_NO_CODE
	// push pseudo error code to keep struct layout
		pushq $0
	.endif

	.cfi_adjust_cfa_offset 8
	
	testl $3, 16(%rsp)
	jz 1f
	swapgs
	1:

	// push irq number
	pushq $\number
	.cfi_adjust_cfa_offset 8

	// jump to actual handling
	jmp isr_common

	.cfi_endproc
.endm

ISR_STUB .L_NO_CODE, 0
ISR_STUB .L_NO_CODE, 1
ISR_STUB .L_NO_CODE, 2
ISR_STUB .L_NO_CODE, 3
ISR_STUB .L_NO_CODE, 4
ISR_STUB .L_NO_CODE, 5
ISR_STUB .L_NO_CODE, 6
ISR_STUB .L_NO_CODE, 7
ISR_STUB .L_CODE, 8
ISR_STUB .L_CODE, 10
ISR_STUB .L_CODE, 11
ISR_STUB .L_CODE, 12
ISR_STUB .L_CODE, 13
ISR_STUB .L_CODE, 14
ISR_STUB .L_NO_CODE, 16
ISR_STUB .L_CODE, 17
ISR_STUB .L_NO_CODE, 18
ISR_STUB .L_NO_CODE, 19

.altmacro
.set i, 32
.rept 224
	ISR_STUB .L_NO_CODE, %i
	.set i, i+1
.endr

	.balign 16
	.extern __isr_c_entry
	.type isr_common, @function
isr_common:
	.cfi_startproc

	PUSHA64

	mov %rsp, %rdi
	xor %rbp, %rbp
	call __isr_c_entry

	POPA64

	cli

	// drop code+number
	add $2*8, %rsp
	.cfi_adjust_cfa_offset -16

	testl $3, 8(%rsp)
	jz 1f
	swapgs
	1:

	iretq
	
	.cfi_endproc

.macro DEC_INT num
	.quad isrStub\num
.endm

	.section .rodata
	.global itable
	.balign 16
itable:
	DEC_INT 0
	DEC_INT 1
	DEC_INT 2
	DEC_INT 3
	DEC_INT 4
	DEC_INT 5
	DEC_INT 6
	DEC_INT 7
	DEC_INT 8
	.quad 0x00000000
	DEC_INT 10
	DEC_INT 11
	DEC_INT 12
	DEC_INT 13
	DEC_INT 14
	.quad 0x00000000
	DEC_INT 16
	DEC_INT 17
	DEC_INT 18
	DEC_INT 19
.rept 12
	.quad 0x00000000
.endr

.set i,32
.rept 224
	DEC_INT %i
.set i,i+1
.endr
