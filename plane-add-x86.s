# x86 code, SSE2

	.p2align	4
	.globl		plane_add_ij4x16_asm_sse2
	.type		plane_add_ij4x16_asm_sse2, @function
################################################################################
# cdecl calling convention, parameters on the stack, reverse order:
# unsigned *restrict v,
# unsigned *restrict w,
# const uint8_t *restrict p,
# unsigned stride
################################################################################
plane_add_ij4x16_asm_sse2:
	pxor		%xmm7, %xmm7

	mov		0x10(%esp), %ecx	# stride
	mov		0x0c(%esp), %edx	# p
	lea		(%edx,%ecx  ), %eax	# p+stride

	movdqa		(%edx       ), %xmm0	# line a
	movdqa		%xmm0, %xmm4
	movdqa		(%eax       ), %xmm1	# line b
	movdqa		%xmm1, %xmm5

	punpcklbw	%xmm7, %xmm0		# [ 00 a7 .. 00 a0 ]
	punpcklbw	%xmm7, %xmm1		# [ 00 b7 .. 00 b0 ]
	punpckhbw	%xmm7, %xmm4		# [ 00 aF .. 00 a8 ]
	punpckhbw	%xmm7, %xmm5		# [ 00 bF .. 00 b8 ]

	movdqa		%xmm0, %xmm6		# [ 00 a7 .. 00 a0 ]
	paddw		%xmm4, %xmm6		# [ aF7 .. a80 ]
	paddw		%xmm1, %xmm0		# [ ab7 .. ab0 ]
	paddw		%xmm5, %xmm1		# [ bF7 .. b80 ]
	paddw		%xmm5, %xmm4		# [ abF .. ab8 ]

	movdqa		%xmm6, %xmm5		# [ aF7 .. a80 ]
	punpcklwd	%xmm1, %xmm6		# [ bB3 aB3 .. b80 a80 ]
	punpckhwd	%xmm1, %xmm5		# [ bF7 aF7 .. bC4 aC4 ]
	paddw		%xmm5, %xmm6		# [ bFB73 aFB73 .. bC840 aC840 ]

	# free: xmm7, xmm5, xmm3, xmm2, xmm1

	movdqa		(%edx,%ecx,2), %xmm2	# line c
	movdqa		%xmm2, %xmm1
	movdqa		(%eax,%ecx,2), %xmm3	# line d
	movdqa		%xmm3, %xmm5

	punpcklbw	%xmm7, %xmm1		# [ c7 .. c0 ]
	punpckhbw	%xmm7, %xmm2		# [ cF .. c8 ]
	punpcklbw	%xmm7, %xmm3		# [ d7 .. d0 ]
	punpckhbw	%xmm7, %xmm5		# [ dF .. d8 ]

	movdqa		%xmm1, %xmm7		# [ c7 .. c0 ]
	paddw		%xmm2, %xmm7		# [ cF7 .. c80 ]
	paddw		%xmm3, %xmm1		# [ cd7 .. cd0 ]
	paddw		%xmm5, %xmm3		# [ dF7 .. d80 ]
	paddw		%xmm5, %xmm2		# [ cdF .. cd8 ]

	movdqa		%xmm7, %xmm5		# [ cF7 .. c80 ]
	punpcklwd	%xmm3, %xmm5		# [ dB3 cB3 .. d80 c80 ]
	punpckhwd	%xmm3, %xmm7		# [ dF7 cF7 .. dC4 cC4 ]
	paddw		%xmm7, %xmm5		# [ dFB73 cFB73 .. dC840 cC840 ]

	# free: xmm7, xmm3

	paddw		%xmm1, %xmm0		# [ abcd7 .. abcd0 ]
	paddw		%xmm2, %xmm4		# [ abcdF .. abcd8 ]

	# free: xmm7, xmm3, xmm2, xmm1

	pxor		%xmm7, %xmm7
	movdqa		%xmm6, %xmm3		# [ bFB73 aFB73 .. bC840 aC840 ]
	punpckldq	%xmm5, %xmm3		# [ dD951 cD951 bD951 aD951  dC840 cC840 bC840 aC840 ]
	punpckhdq	%xmm5, %xmm6		# [ dFB73 cFB73 bFB73 aFB73  dEA62 cEA62 bEA62 aEA62 ]
	paddw		%xmm6, %xmm3		# 2 [ dh ch bh ah  dl cl bl al ]

	movdqa		%xmm3, %xmm6
	punpcklwd	%xmm7, %xmm3		# 4 [ dl cl bl al ]
	punpckhwd	%xmm7, %xmm6		# 4 [ dh ch bh ah ]
	paddd		%xmm6, %xmm3		# 4 [ d c b a ]
	mov		0x08(%esp), %ecx
	paddd		(%ecx), %xmm3
	movdqa		%xmm3, (%ecx)

	movdqa		%xmm0, %xmm1		# 2 [ abcd7 .. abcd0 ]
	movdqa		%xmm4, %xmm5		# 2 [ abcdF .. abcd8 ]
	punpcklwd	%xmm7, %xmm0		# 4 [ abcd3 .. abcd0 ]
	punpckhwd	%xmm7, %xmm1		# 4 [ abcd7 .. abcd4 ]
	punpcklwd	%xmm7, %xmm4		# 4 [ abcdB .. abcd8 ]
	punpckhwd	%xmm7, %xmm5		# 4 [ abcdF .. abcdC ]
	mov		0x04(%esp), %ecx
	paddd		0x00(%ecx), %xmm0
	paddd		0x10(%ecx), %xmm1
	paddd		0x20(%ecx), %xmm4
	paddd		0x30(%ecx), %xmm5
	movdqa		%xmm0, 0x00(%ecx)
	movdqa		%xmm1, 0x10(%ecx)
	movdqa		%xmm4, 0x20(%ecx)
	movdqa		%xmm5, 0x30(%ecx)

	ret
	.size plane_add_ij4x16_asm_sse2, .-plane_add_ij4x16_asm_sse2


