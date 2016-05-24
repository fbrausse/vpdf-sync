	.text

	.p2align	5
	.globl		mse
	.type		mse, @function
################################################################################
# (const void *in1, const void *in2, unsigned stride, unsigned *mse)
################################################################################
mse:
	pxor		%xmm15, %xmm15

	movq		(%rdi), %xmm0
	movq		(%rdi,%rdx), %xmm1
	movq		(%rdi,%rdx,2), %xmm2
	movq		(%rdi,%rdx,4), %xmm3

	movq		(%rsi), %xmm4
	movq		(%rsi,%rdx), %xmm5
	movq		(%rsi,%rdx,2), %xmm6
	movq		(%rsi,%rdx,4), %xmm7

	punpcklbw	%xmm15, %xmm0
	punpcklbw	%xmm15, %xmm1
	punpcklbw	%xmm15, %xmm2
	punpcklbw	%xmm15, %xmm3
	punpcklbw	%xmm15, %xmm4
	punpcklbw	%xmm15, %xmm5
	punpcklbw	%xmm15, %xmm6
	punpcklbw	%xmm15, %xmm7

	psubw		%xmm4, %xmm0
	psubw		%xmm5, %xmm1
	psubw		%xmm6, %xmm2
	psubw		%xmm7, %xmm3

	pmaddwd		%xmm0, %xmm0
	pmaddwd		%xmm1, %xmm1
	pmaddwd		%xmm2, %xmm2
	pmaddwd		%xmm3, %xmm3

	paddd		%xmm1, %xmm0
	paddd		%xmm3, %xmm2
	paddd		%xmm2, %xmm0
	movdqa		%xmm0, %xmm8

	lea		(%rdx,%rdx), %r9	# 2*stride
	add		%r9, %rdx		# 3*stride
	add		%rdx, %rdi		# line 3

	movq		(%rdi), %xmm0		# line 3
	movq		(%rdi,%r9), %xmm1	# line 5
	movq		(%rdi,%rdx), %xmm2	# line 6
	movq		(%rdi,%r9,2), %xmm3	# line 7

	add		%rdx, %rsi		# line 3

	movq		(%rsi), %xmm4		# line 3
	movq		(%rsi,%r9), %xmm5	# line 5
	movq		(%rsi,%rdx), %xmm6	# line 6
	movq		(%rsi,%r9,2), %xmm7	# line 7

	punpcklbw	%xmm15, %xmm0
	punpcklbw	%xmm15, %xmm1
	punpcklbw	%xmm15, %xmm2
	punpcklbw	%xmm15, %xmm3
	punpcklbw	%xmm15, %xmm4
	punpcklbw	%xmm15, %xmm5
	punpcklbw	%xmm15, %xmm6
	punpcklbw	%xmm15, %xmm7

	psubw		%xmm4, %xmm0
	psubw		%xmm5, %xmm1
	psubw		%xmm6, %xmm2
	psubw		%xmm7, %xmm3

	pmaddwd		%xmm0, %xmm0
	pmaddwd		%xmm1, %xmm1
	pmaddwd		%xmm2, %xmm2
	pmaddwd		%xmm3, %xmm3

	paddd		%xmm1, %xmm0
	paddd		%xmm3, %xmm2
	paddd		%xmm2, %xmm0

	paddd		%xmm8, %xmm0

	pshufd		$0x4e, %xmm0, %xmm1
	paddd		%xmm1, %xmm0
	pshufd		$0xb1, %xmm0, %xmm1
	paddd		%xmm1, %xmm0
	movd		%xmm0, (%rcx)
	ret

	.p2align 5
	.globl	covar_2
	.type	covar_2, @function
################################################################################
# (const void *in1, const void *in2, unsigned stride, const unsigned *mu, long long *covar)
################################################################################
covar_2:
	movd		(%rcx), %xmm9		# 2 [ 00 .. 00  µy µx ]
	punpcklwd	%xmm9, %xmm9		# 2 [ 00 .. 00  µy µy µx µx ]
	pshufd		$0x55, %xmm9, %xmm10	# 2 [ µy .. µy ]
	pshufd		$0x00, %xmm9, %xmm9	# 2 [ µx .. µx ]

	pxor		%xmm8, %xmm8

	movq		(%rdi), %xmm0
	movq		(%rdi,%rdx), %xmm1
	movq		(%rdi,%rdx,2), %xmm2
	movq		(%rdi,%rdx,4), %xmm3

	punpcklbw	%xmm8, %xmm0
	punpcklbw	%xmm8, %xmm1
	punpcklbw	%xmm8, %xmm2
	punpcklbw	%xmm8, %xmm3
	psllw		$6, %xmm0
	psllw		$6, %xmm1
	psllw		$6, %xmm2
	psllw		$6, %xmm3
	psubsw		%xmm9, %xmm0
	psubsw		%xmm9, %xmm1
	psubsw		%xmm9, %xmm2
	psubsw		%xmm9, %xmm3

	movq		(%rsi), %xmm12
	movq		(%rsi,%rdx), %xmm13
	movq		(%rsi,%rdx,2), %xmm14
	movq		(%rsi,%rdx,4), %xmm15

	punpcklbw	%xmm8, %xmm12
	punpcklbw	%xmm8, %xmm13
	punpcklbw	%xmm8, %xmm14
	punpcklbw	%xmm8, %xmm15
	psllw		$6, %xmm12
	psllw		$6, %xmm13
	psllw		$6, %xmm14
	psllw		$6, %xmm15
	psubsw		%xmm10, %xmm12
	psubsw		%xmm10, %xmm13
	psubsw		%xmm10, %xmm14
	psubsw		%xmm10, %xmm15

	pmaddwd		%xmm0, %xmm12
	pmaddwd		%xmm1, %xmm13
	pmaddwd		%xmm2, %xmm14
	pmaddwd		%xmm3, %xmm15

	pxor		%xmm8, %xmm8

	lea		(%rdx,%rdx), %r9	# 2*stride
	add		%r9, %rdx		# 3*stride
	add		%rdx, %rdi		# line 3
	
	movq		(%rdi), %xmm0		# line 3
	movq		(%rdi,%r9), %xmm1	# line 5
	movq		(%rdi,%rdx), %xmm2	# line 6
	movq		(%rdi,%r9,2), %xmm3	# line 7

	punpcklbw	%xmm8, %xmm0
	punpcklbw	%xmm8, %xmm1
	punpcklbw	%xmm8, %xmm2
	punpcklbw	%xmm8, %xmm3
	psllw		$6, %xmm0
	psllw		$6, %xmm1
	psllw		$6, %xmm2
	psllw		$6, %xmm3
	psubsw		%xmm9, %xmm0
	psubsw		%xmm9, %xmm1
	psubsw		%xmm9, %xmm2
	psubsw		%xmm9, %xmm3

	add		%rdx, %rsi		# line 3

	movq		(%rsi), %xmm4		# line 3
	movq		(%rsi,%r9), %xmm5	# line 5
	movq		(%rsi,%rdx), %xmm6	# line 6
	movq		(%rsi,%r9,2), %xmm7	# line 7

	punpcklbw	%xmm8, %xmm4
	punpcklbw	%xmm8, %xmm5
	punpcklbw	%xmm8, %xmm6
	punpcklbw	%xmm8, %xmm7
	# 8.6u
	psllw		$6, %xmm4
	psllw		$6, %xmm5
	psllw		$6, %xmm6
	psllw		$6, %xmm7
	# 8.6s
	psubsw		%xmm10, %xmm4
	psubsw		%xmm10, %xmm5
	psubsw		%xmm10, %xmm6
	psubsw		%xmm10, %xmm7

	# 16.12s
	pmaddwd		%xmm0, %xmm4
	pmaddwd		%xmm1, %xmm5
	pmaddwd		%xmm2, %xmm6
	pmaddwd		%xmm3, %xmm7

	# 17.12s
	paddd		%xmm13, %xmm12
	paddd		%xmm15, %xmm14
	paddd		%xmm5, %xmm4
	paddd		%xmm7, %xmm6

	# 18.12s
	paddd		%xmm14, %xmm12
	paddd		%xmm6, %xmm4

	# 19.12s
	paddd		%xmm12, %xmm4

	# all 32 bits are used in xmm4, so we have to sign-extend into quads
	pcmpgtd		%xmm4, %xmm8		# xmm4_i < 0 ? -1 : 0

	movdqa		%xmm4, %xmm5
	punpckhdq	%xmm8, %xmm5
	punpckldq	%xmm8, %xmm4

	paddq		%xmm5, %xmm4
	pshufd		$0x4e, %xmm4, %xmm5
	paddq		%xmm5, %xmm4
	movq		%xmm4, (%r8)

	ret



	.p2align 5
	.globl	mu_var_2x8x8_2
	.type	mu_var_2x8x8_2, @function
################################################################################
# (const void *in1, const void *in2, unsigned stride, unsigned *mu, long *var)
################################################################################
mu_var_2x8x8_2:
	mov		%rdx, %r9

	pxor		%xmm8, %xmm8

	# in1 = f0 = a
	# in2 = f1 = b

	movq		(%rdi), %xmm0
	movq		(%rdi,%rdx), %xmm1
	movq		(%rdi,%rdx,2), %xmm2
	movq		(%rdi,%rdx,4), %xmm4
	lea		(%rdx,%r9,2), %rdx
	movq		(%rdi,%rdx,1), %xmm3
	movq		(%rdi,%rdx,2), %xmm6
	lea		(%rdx,%r9,2), %rdx
	movq		(%rdi,%rdx,1), %xmm5
	lea		(%rdx,%r9,2), %rdx
	movq		(%rdi,%rdx,1), %xmm7

	movdqa		%xmm0, %xmm9
	punpcklqdq	%xmm1, %xmm9	# 1 [ a17 .. a10 a07 .. a00 ]
	movdqa		%xmm2, %xmm10
	punpcklqdq	%xmm3, %xmm10	# 1 [ a37 .. a30 a27 .. a20 ]
	movdqa		%xmm4, %xmm11
	punpcklqdq	%xmm5, %xmm11	# 1 [ a57 .. a50 a47 .. a40 ]
	movdqa		%xmm6, %xmm12
	punpcklqdq	%xmm7, %xmm12	# 1 [ a77 .. a70 a67 .. a60 ]

	# free: 13,14,15

	punpcklbw	%xmm8, %xmm0	# 2 [ a07 .. a00 ]
	punpcklbw	%xmm8, %xmm1	# 2 [ a17 .. a10 ]
	punpcklbw	%xmm8, %xmm2	# 2 [ a27 .. a20 ]
	punpcklbw	%xmm8, %xmm3	# 2 [ a37 .. a30 ]
	punpcklbw	%xmm8, %xmm4	# 2 [ a47 .. a40 ]
	punpcklbw	%xmm8, %xmm5	# 2 [ a57 .. a50 ]
	punpcklbw	%xmm8, %xmm6	# 2 [ a67 .. a60 ]
	punpcklbw	%xmm8, %xmm7	# 2 [ a77 .. a70 ]

	psadbw		%xmm8, %xmm9	# 2 [ 00 00 00 sum(a1?)  00 00 00 sum(a0?) ]
	psadbw		%xmm8, %xmm10	# 2 [ 00 00 00 sum(a3?)  00 00 00 sum(a2?) ]
	psadbw		%xmm8, %xmm11	# 2 [ 00 00 00 sum(a5?)  00 00 00 sum(a4?) ]
	psadbw		%xmm8, %xmm12	# 2 [ 00 00 00 sum(a7?)  00 00 00 sum(a6?) ]

	# free: 8,13,14,15

	pmaddwd		%xmm0, %xmm0	# 4 [ a07^2+a06^2 .. a01^2+a00^2 ]
	pmaddwd		%xmm1, %xmm1	# 4 [ a17^2+a16^2 .. a11^2+a10^2 ]
	pmaddwd		%xmm2, %xmm2	# 4 [ a27^2+a26^2 .. a21^2+a20^2 ]
	pmaddwd		%xmm3, %xmm3	# 4 [ a37^2+a36^2 .. a31^2+a30^2 ]
	pmaddwd		%xmm4, %xmm4	# 4 [ a47^2+a46^2 .. a41^2+a40^2 ]
	pmaddwd		%xmm5, %xmm5	# 4 [ a57^2+a56^2 .. a51^2+a50^2 ]
	paddw		%xmm10, %xmm9	# 2 [ 00 00 00 sum(a[13]?)  00 00 00 sum(a[02]?) ]
	paddw		%xmm12, %xmm11	# 2 [ 00 00 00 sum(a[57]?)  00 00 00 sum(a[46]?) ]
	pmaddwd		%xmm6, %xmm6	# 4 [ a67^2+a66^2 .. a61^2+a60^2 ]
	pmaddwd		%xmm7, %xmm7	# 4 [ a77^2+a76^2 .. a71^2+a70^2 ]

	paddd		%xmm1, %xmm0	# 4 [ sum(a[01][67]^2) .. sum(a[01][01]^2) ]
	paddd		%xmm3, %xmm2	# 4 [ sum(a[23][67]^2) .. sum(a[23][01]^2) ]
	paddd		%xmm5, %xmm4	# 4 [ sum(a[45][67]^2) .. sum(a[45][01]^2) ]
	paddd		%xmm7, %xmm6	# 4 [ sum(a[67][67]^2) .. sum(a[67][01]^2) ]
	paddd		%xmm2, %xmm0	# 4 [ sum(a[0-3][67]^2) .. sum(a[0-3][01]^2) ]
	paddd		%xmm6, %xmm4	# 4 [ sum(a[4-7][67]^2) .. sum(a[4-7][01]^2) ]

	movdqa		%xmm0, %xmm10
	paddd		%xmm4, %xmm10	# 4 [ sum(a?[67]^2) .. sum(a?[01]^2) ]
	paddw		%xmm11, %xmm9	# 2 [ 00 00 00 sum(a[1357]?)  00 00 00 sum(a[0246]?) ]

	# f0: squares: 10, sums: 9

	pxor		%xmm8, %xmm8

	mov		%r9, %rdx

	movq		(%rsi), %xmm0
	movq		(%rsi,%rdx), %xmm1
	movq		(%rsi,%rdx,2), %xmm2
	movq		(%rsi,%rdx,4), %xmm4
	lea		(%rdx,%r9,2), %rdx
	movq		(%rsi,%rdx,1), %xmm3
	movq		(%rsi,%rdx,2), %xmm6
	lea		(%rdx,%r9,2), %rdx
	movq		(%rsi,%rdx,1), %xmm5
	lea		(%rdx,%r9,2), %rdx
	movq		(%rsi,%rdx,1), %xmm7

	movdqa		%xmm0, %xmm11
	punpcklqdq	%xmm1, %xmm11
	movdqa		%xmm2, %xmm12
	punpcklqdq	%xmm3, %xmm12
	movdqa		%xmm4, %xmm13
	punpcklqdq	%xmm5, %xmm13
	movdqa		%xmm6, %xmm14
	punpcklqdq	%xmm7, %xmm14

	punpcklbw	%xmm8, %xmm0
	punpcklbw	%xmm8, %xmm1
	punpcklbw	%xmm8, %xmm2
	punpcklbw	%xmm8, %xmm3
	punpcklbw	%xmm8, %xmm4
	punpcklbw	%xmm8, %xmm5
	punpcklbw	%xmm8, %xmm6
	punpcklbw	%xmm8, %xmm7

	psadbw		%xmm8, %xmm11
	psadbw		%xmm8, %xmm12
	psadbw		%xmm8, %xmm13
	psadbw		%xmm8, %xmm14

	pmaddwd		%xmm0, %xmm0
	pmaddwd		%xmm1, %xmm1
	pmaddwd		%xmm2, %xmm2
	pmaddwd		%xmm3, %xmm3
	pmaddwd		%xmm4, %xmm4
	pmaddwd		%xmm5, %xmm5
	paddw		%xmm12, %xmm11
	paddw		%xmm14, %xmm13
	pmaddwd		%xmm6, %xmm6
	pmaddwd		%xmm7, %xmm7
	paddw		%xmm13, %xmm11

	paddd		%xmm1, %xmm0
	paddd		%xmm3, %xmm2
	paddd		%xmm5, %xmm4
	paddd		%xmm7, %xmm6
	paddd		%xmm2, %xmm0
	paddd		%xmm6, %xmm4
	paddd		%xmm4, %xmm0

	# f0: squares in 10 (dwords), sums in  9 ([79:64], [15:0])
	# f1: squares in  0 (dwords), sums in 11 ([79:64], [15:0])

	packssdw	%xmm11, %xmm9		# [ 0, µy1, 0, µy0, 0, µx1, 0, µx0 ]
	pshufd		$0xb1, %xmm9, %xmm2	# [ 0, µy0, 0, µy1, 0, µx0, 0, µx1 ]
	paddw		%xmm2, %xmm9	# [ 0, µy, 0, µy, 0, µx, 0, µx ]

	movdqa		%xmm9, %xmm2
	pmuludq		%xmm2, %xmm2		# [ 0, µy^2, 0, µx^2 ]
	pshufd		$0xf8, %xmm2, %xmm2	# [ 0, 0, µy^2, µx^2 ]

	movdqa		%xmm9, %xmm11
	psrldq		$6, %xmm11	# [ 0,  0, 0,  0, µy,  0, µy,  0 ]
	por		%xmm11, %xmm9	# [ 0, µy, 0, µy, µy, µx, µy, µx ]
	movd		%xmm9, (%rcx)

				# xmm0:	  4 [ sum(b?[67]^2) .. sum(b?[01]^2) ]
	movdqa		%xmm10, %xmm1	# 4 [ sum(a?[67]^2) .. sum(a?[01]^2) ]
	punpckldq	%xmm0, %xmm1	# 4 [ sum(b?[23]^2)   sum(a?[23]^2)   sum(b?[01]^2)   sum(a?[01]^2)   ]
	punpckhdq	%xmm0, %xmm10	# 4 [ sum(b?[67]^2)   sum(a?[67]^2)   sum(b?[45]^2)   sum(a?[45]^2)   ]
	paddd		%xmm1, %xmm10	# 4 [ sum(b?[2367]^2) sum(a?[2367]^2) sum(b?[0145]^2) sum(a?[0145]^2) ]
	pshufd		$0x4e, %xmm10, %xmm0
	paddd		%xmm0, %xmm10	# 4 [ sum(b??^2)   sum(a??^2)   sum(b??^2)   sum(a??^2)  ]
	pslld		$6, %xmm10	# 4 [ sum(b^2)*64  sum(a^2)*64  sum(b^2)*64  sum(a^2)*64 ]

	psubd		%xmm2, %xmm10
	movq		%xmm10, (%r8)

	# clobbered rdx and r9, but these are caller-saved
	ret



	.p2align 5
	.globl	mu_var_2x8x8_2_covar
	.type	mu_var_2x8x8_2_covar, @function
################################################################################
# (const void *in1, const void *in2, unsigned stride, unsigned *mu, long *var, long long *covar)
################################################################################
mu_var_2x8x8_2_covar:
	mov		%rdx, %r9	# TODO: covar!!

	pxor		%xmm8, %xmm8

	# in1 = f0 = a; µx = sum(a??)
	# in2 = f1 = b; µy = sum(b??)

	movq		(%rdi), %xmm0
	movq		(%rdi,%rdx), %xmm1
	movq		(%rdi,%rdx,2), %xmm2
	movq		(%rdi,%rdx,4), %xmm4
	lea		(%rdx,%r9,2), %rdx
	movq		(%rdi,%rdx,1), %xmm3
	movq		(%rdi,%rdx,2), %xmm6
	lea		(%rdx,%r9,2), %rdx
	movq		(%rdi,%rdx,1), %xmm5
	lea		(%rdx,%r9,2), %rdx
	movq		(%rdi,%rdx,1), %xmm7

	punpcklqdq	%xmm1, %xmm0	# 1 [ a17 .. a10 a07 .. a00 ]
	punpcklqdq	%xmm3, %xmm2	# 1 [ a37 .. a30 a27 .. a20 ]
	punpcklqdq	%xmm5, %xmm4	# 1 [ a57 .. a50 a47 .. a40 ]
	punpcklqdq	%xmm7, %xmm6	# 1 [ a77 .. a70 a67 .. a60 ]

	movdqa		%xmm0, %xmm9
	psadbw		%xmm8, %xmm9	# 2 [ 00 00 00 sum(a1?)  00 00 00 sum(a0?) ]
	movdqa		%xmm2, %xmm10
	psadbw		%xmm8, %xmm10	# 2 [ 00 00 00 sum(a3?)  00 00 00 sum(a2?) ]
	paddw		%xmm10, %xmm9	# 2 [ 00 00 00 sum(a[13]?)  00 00 00 sum(a[02]?) ]

	movdqa		%xmm4, %xmm11
	psadbw		%xmm8, %xmm11	# 2 [ 00 00 00 sum(a5?)  00 00 00 sum(a4?) ]
	movdqa		%xmm6, %xmm12
	psadbw		%xmm8, %xmm12	# 2 [ 00 00 00 sum(a7?)  00 00 00 sum(a6?) ]
	paddw		%xmm12, %xmm11	# 2 [ 00 00 00 sum(a[57]?)  00 00 00 sum(a[46]?) ]

	paddw		%xmm11, %xmm9	# 2 [ 00 00 00 sum(a[1357]?)  00 00 00 sum(a[0246]?) ]
	pshufd		$0x4e, %xmm9, %xmm11
	paddw		%xmm11, %xmm9	# 2 [ 00 00 00 sum(a??) 00 00 00 sum(a??) ]
	packssdw	%xmm9, %xmm9	# 2 [ 00 sum(a??) 00 sum(a??) 00 sum(a??) 00 sum(a??) ]
	packssdw	%xmm9, %xmm9	# 2 [ sum(a??) .. sum(a??) ]

	# free: 1,3,5,7,8,10,11,12,13,14,15
	# used: 0,2,4,6,9

	movdqa		%xmm0, %xmm1
	punpckhbw	%xmm8, %xmm1	# 2 [ a17 .. a10 ]
	punpcklbw	%xmm8, %xmm0	# 2 [ a07 .. a00 ]
	movdqa		%xmm2, %xmm3
	punpckhbw	%xmm8, %xmm3	# 2 [ a37 .. a30 ]
	punpcklbw	%xmm8, %xmm2	# 2 [ a27 .. a20 ]
	movdqa		%xmm4, %xmm5
	punpckhbw	%xmm8, %xmm5	# 2 [ a57 .. a50 ]
	punpcklbw	%xmm8, %xmm4	# 2 [ a47 .. a40 ]
	movdqa		%xmm6, %xmm7
	punpckhbw	%xmm8, %xmm7	# 2 [ a77 .. a70 ]
	punpcklbw	%xmm8, %xmm6	# 2 [ a67 .. a60 ]

	# free: 8,10,11,12,13,14,15

	movdqa		%xmm0, %xmm12
	movdqa		%xmm1, %xmm13
	movdqa		%xmm2, %xmm14
	movdqa		%xmm3, %xmm15
	psllw		$6, %xmm12
	psllw		$6, %xmm13
	psllw		$6, %xmm14
	psllw		$6, %xmm15
	psubsw		%xmm9, %xmm12
	psubsw		%xmm9, %xmm13
	psubsw		%xmm9, %xmm14
	psubsw		%xmm9, %xmm15

	# free: 8,10,11

	pmaddwd		%xmm0, %xmm0	# 4 [ a07^2+a06^2 .. a01^2+a00^2 ]
	pmaddwd		%xmm1, %xmm1	# 4 [ a17^2+a16^2 .. a11^2+a10^2 ]
	pmaddwd		%xmm2, %xmm2	# 4 [ a27^2+a26^2 .. a21^2+a20^2 ]
	pmaddwd		%xmm3, %xmm3	# 4 [ a37^2+a36^2 .. a31^2+a30^2 ]
	paddd		%xmm1, %xmm0	# 4 [ sum(a[01][67]^2) .. sum(a[01][01]^2) ]
	paddd		%xmm3, %xmm2	# 4 [ sum(a[23][67]^2) .. sum(a[23][01]^2) ]
	paddd		%xmm2, %xmm0	# 4 [ sum(a[0-3][67]^2) .. sum(a[0-3][01]^2) ]

	# free: 1,2,3,8,10,11

	movdqa		%xmm4, %xmm1
	movdqa		%xmm5, %xmm2
	movdqa		%xmm6, %xmm3
	movdqa		%xmm7, %xmm11
	psllw		$6, %xmm1
	psllw		$6, %xmm2
	psllw		$6, %xmm3
	psllw		$6, %xmm11
	psubsw		%xmm9, %xmm1
	psubsw		%xmm9, %xmm2
	psubsw		%xmm9, %xmm3
	psubsw		%xmm9, %xmm11

	# free: 8,10

	pmaddwd		%xmm4, %xmm4	# 4 [ a47^2+a46^2 .. a41^2+a40^2 ]
	pmaddwd		%xmm5, %xmm5	# 4 [ a57^2+a56^2 .. a51^2+a50^2 ]
	pmaddwd		%xmm6, %xmm6	# 4 [ a67^2+a66^2 .. a61^2+a60^2 ]
	pmaddwd		%xmm7, %xmm7	# 4 [ a77^2+a76^2 .. a71^2+a70^2 ]
	paddd		%xmm5, %xmm4	# 4 [ sum(a[45][67]^2) .. sum(a[45][01]^2) ]
	paddd		%xmm7, %xmm6	# 4 [ sum(a[67][67]^2) .. sum(a[67][01]^2) ]
	paddd		%xmm6, %xmm4	# 4 [ sum(a[4-7][67]^2) .. sum(a[4-7][01]^2) ]

	movdqa		%xmm0, %xmm10
	paddd		%xmm4, %xmm10	# 4 [ sum(a?[67]^2) .. sum(a?[01]^2) ]
	pshufd		$0x4e, %xmm10, %xmm0
	paddd		%xmm0, %xmm10	# 4 [ sum(a?[2367]2) sum(a?[0145]^2) sum(a?[2367]2) sum(a?[0145]^2) ]

	movd		%xmm9, %edi
	movq		%xmm10, %rax

	# free: 0,4,5,6,7,8,9,10

	# f0: squares: rax, mean: edi

	pxor		%xmm8, %xmm8

	mov		%r9, %rdx	# TODO: covar!!

	movq		(%rsi), %xmm4		# b0
	movq		(%rsi,%rdx), %xmm5	# b1
	movq		(%rsi,%rdx,2), %xmm6	# b2
	movq		(%rsi,%rdx,4), %xmm7	# b4

	punpcklqdq	%xmm5, %xmm4	# 1 [ b17 .. b10 b07 .. b00 ]
	punpcklqdq	%xmm7, %xmm6	# 1 [ b47 .. b40 b27 .. b20 ]

	movdqa		%xmm4, %xmm9
	psadbw		%xmm8, %xmm9	# 2 [ 00 00 00 sum(b1?)  00 00 00 sum(b0?) ]
	movdqa		%xmm6, %xmm10
	psadbw		%xmm8, %xmm10	# 2 [ 00 00 00 sum(b4?)  00 00 00 sum(b2?) ]
	paddw		%xmm10, %xmm9	# 2 [ 00 00 00 sum(b[14]?)  00 00 00 sum(b[02]?) ]

	# free: 0,5,7,8,10

	lea		(%rdx,%r9,2), %rdx	# 3*stride
	movq		(%rsi,%rdx,1), %xmm0	# b3
	movq		(%rsi,%rdx,2), %xmm5	# b6
	lea		(%rdx,%r9,2), %rdx	# 5*stride
	movq		(%rsi,%rdx,1), %xmm7	# b5
	lea		(%rdx,%r9,2), %rdx	# 7*stride
	movq		(%rsi,%rdx,1), %xmm10	# b7

	punpcklqdq	%xmm5, %xmm0	# 1 [ b67 .. b60 b37 .. b30 ]
	punpcklqdq	%xmm10, %xmm7	# 1 [ b77 .. b70 b57 .. b50 ]

	# free: 5,8,10

	movdqa		%xmm0, %xmm5
	psadbw		%xmm8, %xmm5	# 2 [ 00 00 00 sum(b6?)  00 00 00 sum(b3?) ]
	movdqa		%xmm7, %xmm10
	psadbw		%xmm8, %xmm10	# 2 [ 00 00 00 sum(b7?)  00 00 00 sum(b5?) ]
	paddw		%xmm5, %xmm10	# 2 [ 00 00 00 sum(b[67]?)  00 00 00 sum(b[35]?) ]

	paddw		%xmm10, %xmm9	# 2 [ 00 00 00 sum(b[1467]?)  00 00 00 sum(b[0235]?) ]
	pshufd		$0x4e, %xmm9, %xmm10
	paddw		%xmm10, %xmm9	# 2 [ 00 00 00 sum(b??) 00 00 00 sum(b??) ]
	packssdw	%xmm9, %xmm9	# 2 [ 00 sum(b??) 00 sum(b??) 00 sum(b??) 00 sum(b??) ]
	packssdw	%xmm9, %xmm9	# 2 [ sum(b??) .. sum(b??) ]

	# free: 5,8,10
	# 0 : 1 [ b67 .. b60 b37 .. b30 ]
	# 1 : 2 [ µx-a4? ]
	# 2 : 2 [ µx-a5? ]
	# 3 : 2 [ µx-a6? ]
	# 4 : 1 [ b17 .. b10 b07 .. b00 ]
	# 6 : 1 [ b47 .. b40 b27 .. b20 ]
	# 7 : 1 [ b77 .. b70 b57 .. b50 ]
	# 9 : 2 [ sum(b??) .. sum(b??) ]
	# 11: 2 [ µx-a7? ]
	# 12: 2 [ µx-a0? ]
	# 13: 2 [ µx-a1? ]
	# 14: 2 [ µx-a2? ]
	# 15: 2 [ µx-a3? ]

	movdqa		%xmm4, %xmm5
	punpckhbw	%xmm8, %xmm5
	movdqa		%xmm4, %xmm10
	punpcklbw	%xmm8, %xmm10
	psllw		$6, %xmm5
	psllw		$6, %xmm10
	psubsw		%xmm9, %xmm5	# 2 [ µy-b1? ]
	psubsw		%xmm9, %xmm10	# 2 [ µy-b0? ]
	pmaddwd		%xmm5, %xmm13	# 4 [ cv:1? ]
	pmaddwd		%xmm10, %xmm12	# 4 [ cv:0? ]
	paddd		%xmm13, %xmm12	# 4 [ cv:[01]? ]

	movdqa		%xmm4, %xmm10
	punpckhbw	%xmm8, %xmm10
	punpcklbw	%xmm8, %xmm4
	pmaddwd		%xmm4, %xmm4	# 4 [ b0?^2 ]
	pmaddwd		%xmm10, %xmm10	# 4 [ b1?^2 ]
	paddd		%xmm10, %xmm4	# 4 [ b[01]?^2 ]

	# free: 5,8,10,13

	movdqa		%xmm6, %xmm5
	punpcklbw	%xmm8, %xmm5	# 2 [ b2? ]
	punpckhbw	%xmm8, %xmm6	# 2 [ b4? ]
	movdqa		%xmm5, %xmm10
	psllw		$6, %xmm10
	movdqa		%xmm6, %xmm13
	psllw		$6, %xmm13
	psubw		%xmm9, %xmm10	# 2 [ µy-b2? ]
	psubw		%xmm9, %xmm13	# 2 [ µy-b4? ]
	pmaddwd		%xmm10, %xmm14	# 4 [ cv:2? ]
	pmaddwd		%xmm13, %xmm1	# 4 [ cv:4? ]
	paddd		%xmm14, %xmm1	# 4 [ cv:[24]? ]
	# 18.12s
	paddd		%xmm12, %xmm1	# 4 [ cv:[0124]? ]

	pmaddwd		%xmm5, %xmm5	# 4 [ b2?^2 ]
	pmaddwd		%xmm6, %xmm6	# 4 [ b4?^2 ]
	paddd		%xmm6, %xmm5	# 4 [ b[24]?^2 ]
	paddd		%xmm5, %xmm4	# 4 [ b[0124]?^2 ]

	# free: 5,6,8,10,12,13,14

	movdqa		%xmm0, %xmm5
	movdqa		%xmm7, %xmm10
	punpcklbw	%xmm8, %xmm0	# 2 [ b3? ]
	punpckhbw	%xmm8, %xmm5	# 2 [ b6? ]
	punpcklbw	%xmm8, %xmm7	# 2 [ b5? ]
	punpckhbw	%xmm8, %xmm10	# 2 [ b7? ]
	movdqa		%xmm0, %xmm6
	psllw		$6, %xmm6
	movdqa		%xmm5, %xmm12
	psllw		$6, %xmm12
	psubw		%xmm9, %xmm6
	psubw		%xmm9, %xmm12
	pmaddwd		%xmm6, %xmm15	# 4 [ cv:3? ]
	pmaddwd		%xmm12, %xmm3	# 4 [ cv:6? ]
	paddd		%xmm15, %xmm3	# 4 [ cv:[36]? ]

	movdqa		%xmm7, %xmm6
	psllw		$6, %xmm6
	movdqa		%xmm10, %xmm12
	psllw		$6, %xmm12
	psubw		%xmm9, %xmm6
	psubw		%xmm9, %xmm12
	pmaddwd		%xmm6, %xmm2	# 4 [ cv:5? ]
	pmaddwd		%xmm12, %xmm11	# 4 [ cv:7? ]
	paddd		%xmm11, %xmm2	# 4 [ cv:[57]? ]
	# 18.12s
	paddd		%xmm2, %xmm3	# 4 [ cv:[3567]? ]

	pmaddwd		%xmm0, %xmm0	# 4 [ b3?^2 ]
	pmaddwd		%xmm5, %xmm5	# 4 [ b6?^2 ]
	pmaddwd		%xmm7, %xmm7	# 4 [ b5?^2 ]
	pmaddwd		%xmm10, %xmm10	# 4 [ b7?^2 ]
	paddd		%xmm5, %xmm0	# 4 [ b[36]?^2 ]
	paddd		%xmm10, %xmm7	# 4 [ b[57]?^2 ]
	paddd		%xmm7, %xmm0	# 4 [ b[3567]?^2 ]
	paddd		%xmm0, %xmm4	# 4 [ b??^2 ]


	# 1: 4 [ cv:[0124]? ]
	# 3: 4 [ cv:[3567]? ]
	# 4: 4 [ b??^2 ]
	# 8: zero
	# 9: 2 [ µy .. µy ]

	# 19.12s
	paddd		%xmm3, %xmm1

	# all 32 bits are used in xmm4, so we have to sign-extend into quads
	pcmpgtd		%xmm1, %xmm8		# xmm1_i < 0 ? -1 : 0




	movdqa		%xmm4, %xmm5
	punpckhdq	%xmm8, %xmm5
	punpckldq	%xmm8, %xmm4

	paddq		%xmm5, %xmm4
	pshufd		$0x4e, %xmm4, %xmm5
	paddq		%xmm5, %xmm4
	movq		%xmm4, (%r8)

	ret















	# f0: squares in 10 (dwords), sums in  9 ([79:64], [15:0])
	# f1: squares in  0 (dwords), sums in 11 ([79:64], [15:0])

	packssdw	%xmm11, %xmm9		# [ 0, µy1, 0, µy0, 0, µx1, 0, µx0 ]
	pshufd		$0xb1, %xmm9, %xmm2	# [ 0, µy0, 0, µy1, 0, µx0, 0, µx1 ]
	paddw		%xmm2, %xmm9	# [ 0, µy, 0, µy, 0, µx, 0, µx ]

	movdqa		%xmm9, %xmm2
	pmuludq		%xmm2, %xmm2		# [ 0, µy^2, 0, µx^2 ]
	pshufd		$0xf8, %xmm2, %xmm2	# [ 0, 0, µy^2, µx^2 ]

	movdqa		%xmm9, %xmm11
	psrldq		$6, %xmm11	# [ 0,  0, 0,  0, µy,  0, µy,  0 ]
	por		%xmm11, %xmm9	# [ 0, µy, 0, µy, µy, µx, µy, µx ]
	movd		%xmm9, (%rcx)

				# xmm0:	  4 [ sum(b?[67]^2) .. sum(b?[01]^2) ]
	movdqa		%xmm10, %xmm1	# 4 [ sum(a?[67]^2) .. sum(a?[01]^2) ]
	punpckldq	%xmm0, %xmm1	# 4 [ sum(b?[23]^2)   sum(a?[23]^2)   sum(b?[01]^2)   sum(a?[01]^2)   ]
	punpckhdq	%xmm0, %xmm10	# 4 [ sum(b?[67]^2)   sum(a?[67]^2)   sum(b?[45]^2)   sum(a?[45]^2)   ]
	paddd		%xmm1, %xmm10	# 4 [ sum(b?[2367]^2) sum(a?[2367]^2) sum(b?[0145]^2) sum(a?[0145]^2) ]
	pshufd		$0x4e, %xmm10, %xmm0
	paddd		%xmm0, %xmm10	# 4 [ sum(b??^2)   sum(a??^2)   sum(b??^2)   sum(a??^2)  ]
	pslld		$6, %xmm10	# 4 [ sum(b^2)*64  sum(a^2)*64  sum(b^2)*64  sum(a^2)*64 ]

	psubd		%xmm2, %xmm10
	movq		%xmm10, (%r8)

	# clobbered rdx and r9, but these are caller-saved
	ret
