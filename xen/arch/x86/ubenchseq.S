	.text
	.code64

/*
 * ----------------------------------------------------------------------------
 *  Name: wrsse2
 *  Purpose: Writes 128-bit value sequentially to an area of memory.
 *  Params: rdi = ptr to memory area
 *          rsi = length in bytes
 *          rdx = time to run (cpu cycles)
 * ----------------------------------------------------------------------------
 */
	
ENTRY(wrsse2)
	pushq	%r13
	pushq	%r12
	
	movq	%rdi, %r11
	addq	%rsi, %r11		/* r11 = end of memory               */
	movq	%rdx, %rsi		/* rsi = time to run                 */
	xorq	%r13, %r13		/* r13 = count of entire walk        */
	
	rdtsc
	shlq 	$32, %rdx
	orq	%rax, %rdx
	movq	%rdx, %r12		/* r12 = current time                */

	clts				/* clear TS flag in CR0              */
					/* this prevent to trigger exception */
					/* while using media instructions    */
	
wrsse2_outer:
	movq	%rdi, %r10		/*   r10 = start of memory           */
	
wrsse2_inner:
	movntdq %xmm0,   0(%r10)	/*     write 128 bytes of memory     */
	movntdq %xmm0,  16(%r10)        /*     writting with SSE2 bypass the */
	movntdq %xmm0,  32(%r10)	/*     cache and is likely to write  */
	movntdq %xmm0,  48(%r10)	/*     directly to the main memory   */
	movntdq %xmm0,  64(%r10)
	movntdq %xmm0,  80(%r10)
	movntdq %xmm0,  96(%r10)
	movntdq %xmm0, 112(%r10)
	
	addq	$128, %r10
	cmpq	%r11, %r10
	jb	wrsse2_inner		/*   end of w_inner2                 */
	
	incq	%r13
	
	rdtsc
	shlq	$32, %rdx
	orq	%rax, %rdx
	subq	%r12, %rdx		/*   rdx = elapsed time              */
	
	cmpq 	%rsi, %rdx
	jb	wrsse2_outer		/* end of w_outer2                   */

	movq	%r13, %rax
	popq	%r12
	popq	%r13
	retq

	
/*
 * ----------------------------------------------------------------------------
 *  Name: repstos
 *  Purpose: Writes 64-bit value sequentially to an area of memory.
 *  Params: rdi = ptr to memory area
 *          rsi = length in bytes
 *          rdx = time to run (cpu cycles)
 * ----------------------------------------------------------------------------
 */

ENTRY(repstos)
	movq	%rdi, %r10		/* r10 = start of memory             */
	movq	%rdx, %r11		/* r11 = time to run                 */
	shrq	$3, %rsi		/* rsi = length in quadword          */
	xorq	%r9, %r9		/* r9  = count of entire walk        */
	
	rdtsc
	shlq 	$32, %rdx
	orq	%rax, %rdx
	movq	%rdx, %r8		/* r8 = current time                 */
	
repstos_loop:
	movq	%r10, %rdi		/*   rdi ready for rep ; stos        */
	movq	%rsi, %rcx		/*   rcx ready for rep ; stos        */
	
	rep
	stosq

	incq	%r9
	
	rdtsc
	shlq	$32, %rdx
	orq	%rax, %rdx
	subq	%r8, %rdx		/*   rdx = elapsed time              */
	
	cmpq 	%r11, %rdx
	jb	repstos_loop		/* end of w_outer2                   */

	movq	%r9, %rax
	retq
