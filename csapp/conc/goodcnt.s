	.file	"goodcnt.c"
	.text
	.globl	thread
	.type	thread, @function
thread:
.LFB76:
	.cfi_startproc
	endbr64
	pushq	%r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	pushq	%rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	pushq	%rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	movl	(%rdi), %r12d
	testl	%r12d, %r12d
	jle	.L2
	movl	$0, %ebx
	leaq	mutex(%rip), %rbp
.L3:
	movq	%rbp, %rdi
	call	P@PLT
	movq	cnt(%rip), %rax
	addq	$1, %rax
	movq	%rax, cnt(%rip)
	movq	%rbp, %rdi
	call	V@PLT
	addl	$1, %ebx
	cmpl	%ebx, %r12d
	jne	.L3
.L2:
	movl	$0, %eax
	popq	%rbx
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
.LFE76:
	.size	thread, .-thread
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"usage: %s <niters>\n"
.LC1:
	.string	"BOOM! cnt=%ld\n"
.LC2:
	.string	"OK cnt=%ld\n"
	.text
	.globl	main
	.type	main, @function
main:
.LFB75:
	.cfi_startproc
	endbr64
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	subq	$32, %rsp
	.cfi_def_cfa_offset 48
	movq	%fs:40, %rax
	movq	%rax, 24(%rsp)
	xorl	%eax, %eax
	cmpl	$2, %edi
	je	.L7
	movq	(%rsi), %rdx
	leaq	.LC0(%rip), %rsi
	movl	$1, %edi
	call	__printf_chk@PLT
	movl	$0, %edi
	call	exit@PLT
.L7:
	movq	8(%rsi), %rdi
	movl	$10, %edx
	movl	$0, %esi
	call	strtol@PLT
	movl	%eax, 4(%rsp)
	movl	$1, %edx
	movl	$0, %esi
	leaq	mutex(%rip), %rdi
	call	Sem_init@PLT
	leaq	4(%rsp), %rbx
	leaq	8(%rsp), %rdi
	movq	%rbx, %rcx
	leaq	thread(%rip), %rdx
	movl	$0, %esi
	call	Pthread_create@PLT
	leaq	16(%rsp), %rdi
	movq	%rbx, %rcx
	leaq	thread(%rip), %rdx
	movl	$0, %esi
	call	Pthread_create@PLT
	movl	$0, %esi
	movq	8(%rsp), %rdi
	call	Pthread_join@PLT
	movl	$0, %esi
	movq	16(%rsp), %rdi
	call	Pthread_join@PLT
	movq	cnt(%rip), %rdx
	movl	4(%rsp), %eax
	addl	%eax, %eax
	cltq
	cmpq	%rdx, %rax
	je	.L8
	movq	cnt(%rip), %rdx
	leaq	.LC1(%rip), %rsi
	movl	$1, %edi
	movl	$0, %eax
	call	__printf_chk@PLT
.L9:
	movl	$0, %edi
	call	exit@PLT
.L8:
	movq	cnt(%rip), %rdx
	leaq	.LC2(%rip), %rsi
	movl	$1, %edi
	movl	$0, %eax
	call	__printf_chk@PLT
	jmp	.L9
	.cfi_endproc
.LFE75:
	.size	main, .-main
	.comm	mutex,32,32
	.globl	cnt
	.bss
	.align 8
	.type	cnt, @object
	.size	cnt, 8
cnt:
	.zero	8
	.ident	"GCC: (Ubuntu 9.4.0-1ubuntu1~20.04.1) 9.4.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	 1f - 0f
	.long	 4f - 1f
	.long	 5
0:
	.string	 "GNU"
1:
	.align 8
	.long	 0xc0000002
	.long	 3f - 2f
2:
	.long	 0x3
3:
	.align 8
4:
