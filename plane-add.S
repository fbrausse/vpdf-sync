
#include "mangling.h"

# amd64 code, SSSE3
# Registers RBP, RBX, and R12–R15 are callee-save registers;
# all others must be saved by the caller if it wishes to preserve their values.

	.text

	.p2align	5
	.globl		ASM_C_FUNC(plane_add_i8x8_asm)
#	.type		ASM_C_FUNC(plane_add_i8x8_asm), @function
################################################################################
# %rdi: unsigned *restrict q,
# %rsi: unsigned *restrict v,
# %rdx: const uint8_t *restrict p,
# %rcx: unsigned m,
# %r8 : unsigned n,
# %r9 : unsigned mstride
################################################################################
ASM_C_FUNC(plane_add_i8x8_asm):
	pxor		%xmm15, %xmm15

	lea		(%rdx,%r9), %r10	# p+mstride
	lea		(%r9,%r9,2), %rax	# 3*mstride

	movq		(%rdx), %xmm0
	movq		(%r10), %xmm1
	movq		(%rdx,%r9,2), %xmm2
	movq		(%r10,%r9,2), %xmm3
	movq		(%rdx,%r9,4), %xmm4
	movq		(%r10,%r9,4), %xmm5
	movq		(%rdx,%rax,2), %xmm6
	movq		(%r10,%rax,2), %xmm7

#	add		$8, %rdx
#	add		$8, %r10

	punpcklbw	%xmm15, %xmm0
	punpcklbw	%xmm15, %xmm1
	punpcklbw	%xmm15, %xmm2
	punpcklbw	%xmm15, %xmm3
	punpcklbw	%xmm15, %xmm4
	punpcklbw	%xmm15, %xmm5
	punpcklbw	%xmm15, %xmm6
	punpcklbw	%xmm15, %xmm7

	paddw		%xmm1, %xmm0
	paddw		%xmm3, %xmm2
	paddw		%xmm5, %xmm4
	paddw		%xmm7, %xmm6
	paddw		%xmm2, %xmm0
	paddw		%xmm6, %xmm4
	paddw		%xmm4, %xmm0

	mov		%rsi, %rax
	add		$0x10, %rax

	movdqa		%xmm0, %xmm1
	punpcklwd	%xmm15, %xmm0
	punpckhwd	%xmm15, %xmm1
	paddd		(%rsi), %xmm0
	paddd		(%rax), %xmm1
	movdqa		%xmm0, (%rsi)
	movdqa		%xmm1, (%rax)

#	add		$0x10, %rdi

	ret
#	.size ASM_C_FUNC(plane_add_i8x8_asm), .-ASM_C_FUNC(plane_add_i8x8_asm)


	.p2align	5
	.globl		ASM_C_FUNC(plane_add_j8x8_asm)
#	.type		ASM_C_FUNC(plane_add_j8x8_asm), @function
################################################################################
# %rdi: unsigned *restrict q,
# %rsi: unsigned *restrict v,
# %rdx: const uint8_t *restrict p,
# %rcx: unsigned m,
# %r8 : unsigned n,
# %r9 : unsigned nstride
################################################################################
ASM_C_FUNC(plane_add_j8x8_asm):
	pxor		%xmm15, %xmm15

	lea		(%rdx,%r9), %r10	# p+nstride
	lea		(%r9,%r9,2), %rax	# 3*nstride

	movq		(%rdx), %xmm0
	movq		(%r10), %xmm1
	movq		(%rdx,%r9,2), %xmm2
	movq		(%r10,%r9,2), %xmm3
	movq		(%rdx,%r9,4), %xmm4
	movq		(%r10,%r9,4), %xmm5
	movq		(%rdx,%rax,2), %xmm6
	movq		(%r10,%rax,2), %xmm7

#	add		$8, %rdx
#	add		$8, %r10

	punpcklbw	%xmm15, %xmm0
	punpcklbw	%xmm15, %xmm1
	punpcklbw	%xmm15, %xmm2
	punpcklbw	%xmm15, %xmm3
	punpcklbw	%xmm15, %xmm4
	punpcklbw	%xmm15, %xmm5
	punpcklbw	%xmm15, %xmm6
	punpcklbw	%xmm15, %xmm7

	phaddw		%xmm1, %xmm0
	phaddw		%xmm3, %xmm2
	phaddw		%xmm5, %xmm4
	phaddw		%xmm7, %xmm6
	phaddw		%xmm2, %xmm0
	phaddw		%xmm6, %xmm4
	phaddw		%xmm4, %xmm0

	mov		%rsi, %rax
	add		$0x10, %rax

	movdqa		%xmm0, %xmm1
	punpcklwd	%xmm15, %xmm0
	punpckhwd	%xmm15, %xmm1
	paddd		(%rsi), %xmm0
	paddd		(%rax), %xmm1
	movdqa		%xmm0, (%rsi)
	movdqa		%xmm1, (%rax)

#	add		$0x10, %rdi

	ret
#	.size ASM_C_FUNC(plane_add_j8x8_asm), .-ASM_C_FUNC(plane_add_j8x8_asm)



	.p2align	5
	.globl		ASM_C_FUNC(plane_add_ij8x8_asm)
#	.type		ASM_C_FUNC(plane_add_ij8x8_asm), @function
################################################################################
# %rdi: unsigned *restrict v,
# %rsi: unsigned *restrict w,
# %rdx: const uint8_t *restrict p,
# %rcx: unsigned stride
################################################################################
ASM_C_FUNC(plane_add_ij8x8_asm):
	pxor		%xmm15, %xmm15

	lea		(%rdx,%rcx  ), %r8	# p+stride
	lea		(%rcx,%rcx,2), %r9	# 3*stride

	movq		(%rdx       ), %xmm0
	movq		(%r8        ), %xmm1
	movq		(%rdx,%rcx,2), %xmm2
	movq		(%r8 ,%rcx,2), %xmm3
	movq		(%rdx,%rcx,4), %xmm4
	movq		(%r8 ,%rcx,4), %xmm5
	movq		(%rdx,%r9 ,2), %xmm6
	movq		(%r8 ,%r9 ,2), %xmm7

	punpcklbw	%xmm15, %xmm0
	punpcklbw	%xmm15, %xmm1
	punpcklbw	%xmm15, %xmm2
	punpcklbw	%xmm15, %xmm3
	punpcklbw	%xmm15, %xmm4
	punpcklbw	%xmm15, %xmm5
	punpcklbw	%xmm15, %xmm6
	punpcklbw	%xmm15, %xmm7

	movdqa		%xmm0, %xmm8
	movdqa		%xmm1, %xmm9
	movdqa		%xmm2, %xmm10
	movdqa		%xmm3, %xmm11
	movdqa		%xmm4, %xmm12
	movdqa		%xmm5, %xmm13
	movdqa		%xmm6, %xmm14
	movdqa		%xmm7, %xmm15

	paddw		%xmm1, %xmm0
	paddw		%xmm3, %xmm2
	paddw		%xmm5, %xmm4
	paddw		%xmm7, %xmm6
	paddw		%xmm2, %xmm0
	paddw		%xmm6, %xmm4
	paddw		%xmm4, %xmm0

	phaddw		%xmm9, %xmm8
	phaddw		%xmm11, %xmm10
	phaddw		%xmm13, %xmm12
	phaddw		%xmm15, %xmm14
	phaddw		%xmm10, %xmm8
	phaddw		%xmm14, %xmm12
	phaddw		%xmm12, %xmm8

	mov		%rdi, %r8
	mov		%rsi, %r9
	add		$0x10, %r8
	add		$0x10, %r9

	pxor		%xmm15, %xmm15

	movdqa		%xmm0, %xmm1
	movdqa		%xmm8, %xmm9
	punpcklwd	%xmm15, %xmm0
	punpckhwd	%xmm15, %xmm1
	punpcklwd	%xmm15, %xmm8
	punpckhwd	%xmm15, %xmm9
	paddd		(%rdi), %xmm0
	paddd		(%r8 ), %xmm1
	paddd		(%rsi), %xmm8
	paddd		(%r9 ), %xmm9
	movdqa		%xmm0, (%rdi)
	movdqa		%xmm1, (%r8 )
	movdqa		%xmm8, (%rsi)
	movdqa		%xmm9, (%r9 )

	ret
#	.size ASM_C_FUNC(plane_add_ij8x8_asm), .-ASM_C_FUNC(plane_add_ij8x8_asm)



	.p2align	5
	.globl		ASM_C_FUNC(plane_add_ij4x16_asm)
#	.type		ASM_C_FUNC(plane_add_ij4x16_asm), @function
################################################################################
# %rdi: unsigned *restrict v,
# %rsi: unsigned *restrict w,
# %rdx: const uint8_t *restrict p,
# %rcx: unsigned stride
################################################################################
ASM_C_FUNC(plane_add_ij4x16_asm):
	pxor		%xmm15, %xmm15

	lea		(%rdx,%rcx  ), %rax	# p+stride

	movdqa		(%rdx       ), %xmm0	# line a
	movdqa		%xmm0, %xmm8
	movdqa		(%rax       ), %xmm1	# line b
	movdqa		%xmm1, %xmm9
	movdqa		(%rdx,%rcx,2), %xmm2	# line c
	movdqa		%xmm2, %xmm10
	movdqa		(%rax,%rcx,2), %xmm3	# line d
	movdqa		%xmm3, %xmm11

	punpcklbw	%xmm15, %xmm0		# [ 00 a7 .. 00 a0 ]
	punpcklbw	%xmm15, %xmm1		# [ 00 b7 .. 00 b0 ]
	punpcklbw	%xmm15, %xmm2		# [ 00 c7 .. 00 c0 ]
	punpcklbw	%xmm15, %xmm3		# [ 00 d7 .. 00 d0 ]
	punpckhbw	%xmm15, %xmm8		# [ 00 aF .. 00 a8 ]
	punpckhbw	%xmm15, %xmm9		# [ 00 bF .. 00 b8 ]
	punpckhbw	%xmm15, %xmm10		# [ 00 cF .. 00 c8 ]
	punpckhbw	%xmm15, %xmm11		# [ 00 dF .. 00 d8 ]

	movdqa		%xmm0, %xmm4		# [ a7 .. a0 ]
	movdqa		%xmm1, %xmm5		# [ b7 .. b0 ]
	movdqa		%xmm2, %xmm6		# [ c7 .. c0 ]
	movdqa		%xmm3, %xmm7		# [ d7 .. d0 ]

	paddw		%xmm8, %xmm4		# [ aF7 .. a80 ]
	paddw		%xmm9, %xmm5		# [ bF7 .. b80 ]
	paddw		%xmm10, %xmm6		# [ cF7 .. c80 ]
	paddw		%xmm11, %xmm7		# [ dF7 .. d80 ]

	paddw		%xmm1, %xmm0		# [ ab7 .. ab0 ]
	paddw		%xmm3, %xmm2		# [ cd7 .. cd0 ]
	paddw		%xmm9, %xmm8		# [ abF .. ab8 ]
	paddw		%xmm11, %xmm10		# [ cdF .. cd8 ]
	paddw		%xmm2, %xmm0		# [ abcd7 .. abcd0 ]
	paddw		%xmm10, %xmm8		# [ abcdF .. abcd8 ]

	phaddw		%xmm5, %xmm4		# 2 [ bF7E6 .. b9180 aF7E6 .. a9180 ]
	phaddw		%xmm7, %xmm6		# 2 [ dF7E6 .. d9180 cF7E6 .. c9180 ]
						# h = F7E6D5C4, l = B3A29180
	phaddw		%xmm6, %xmm4		# 2 [ dh dl ch cl bh bl ah al ]
	phaddw		%xmm15, %xmm4		# 2 [ 0 0 0 0 d c b a ]
	punpcklwd	%xmm15, %xmm4		# 4 [ d c b a ]
	paddd		(%rsi), %xmm4
	movdqa		%xmm4, (%rsi)

	movdqa		%xmm0, %xmm1		# 2 [ abcd7 .. abcd0 ]
	movdqa		%xmm8, %xmm9		# 2 [ abcdF .. abcd8 ]
	punpcklwd	%xmm15, %xmm0		# 4 [ abcd3 .. abcd0 ]
	punpckhwd	%xmm15, %xmm1		# 4 [ abcd7 .. abcd4 ]
	punpcklwd	%xmm15, %xmm8		# 4 [ abcdB .. abcd8 ]
	punpckhwd	%xmm15, %xmm9		# 4 [ abcdF .. abcdC ]
	paddd		0x00(%rdi), %xmm0
	paddd		0x10(%rdi), %xmm1
	paddd		0x20(%rdi), %xmm8
	paddd		0x30(%rdi), %xmm9
	movdqa		%xmm0, 0x00(%rdi)
	movdqa		%xmm1, 0x10(%rdi)
	movdqa		%xmm8, 0x20(%rdi)
	movdqa		%xmm9, 0x30(%rdi)

	ret
#	.size ASM_C_FUNC(plane_add_ij4x16_asm), .-ASM_C_FUNC(plane_add_ij4x16_asm)



	.p2align	5
	.globl		ASM_C_FUNC(plane_madd_ij4x16_asm)
#	.type		ASM_C_FUNC(plane_madd_ij4x16_asm), @function
################################################################################
# %rdi: unsigned *restrict v,
# %rsi: unsigned *restrict w,
# %rdx: const uint8_t *restrict p,
# %rcx: unsigned stride
# %r8 : unsigned *restrict x
################################################################################
ASM_C_FUNC(plane_madd_ij4x16_asm):
	pxor		%xmm15, %xmm15

	lea		(%rdx,%rcx  ), %rax	# p+stride

	movdqa		(%rdx       ), %xmm0	# line a
	movdqa		%xmm0, %xmm8
	movdqa		(%rax       ), %xmm1	# line b
	movdqa		%xmm1, %xmm9
	movdqa		(%rdx,%rcx,2), %xmm2	# line c
	movdqa		%xmm2, %xmm10
	movdqa		(%rax,%rcx,2), %xmm3	# line d
	movdqa		%xmm3, %xmm11

	punpcklbw	%xmm15, %xmm0		# [ 00 a7 .. 00 a0 ]
	punpcklbw	%xmm15, %xmm1		# [ 00 b7 .. 00 b0 ]
	punpcklbw	%xmm15, %xmm2		# [ 00 c7 .. 00 c0 ]
	punpcklbw	%xmm15, %xmm3		# [ 00 d7 .. 00 d0 ]
	punpckhbw	%xmm15, %xmm8		# [ 00 aF .. 00 a8 ]
	punpckhbw	%xmm15, %xmm9		# [ 00 bF .. 00 b8 ]
	punpckhbw	%xmm15, %xmm10		# [ 00 cF .. 00 c8 ]
	punpckhbw	%xmm15, %xmm11		# [ 00 dF .. 00 d8 ]

	movdqa		%xmm0, %xmm4		# [ a7 .. a0 ]
	movdqa		%xmm1, %xmm5		# [ b7 .. b0 ]
	movdqa		%xmm2, %xmm6		# [ c7 .. c0 ]
	movdqa		%xmm3, %xmm7		# [ d7 .. d0 ]

	paddw		%xmm8, %xmm4		# [ aF7 .. a80 ]
	paddw		%xmm9, %xmm5		# [ bF7 .. b80 ]
	paddw		%xmm10, %xmm6		# [ cF7 .. c80 ]
	paddw		%xmm11, %xmm7		# [ dF7 .. d80 ]

	paddw		%xmm1, %xmm0		# [ ab7 .. ab0 ]
	paddw		%xmm3, %xmm2		# [ cd7 .. cd0 ]
	paddw		%xmm9, %xmm8		# [ abF .. ab8 ]
	paddw		%xmm11, %xmm10		# [ cdF .. cd8 ]
	paddw		%xmm2, %xmm0		# [ abcd7 .. abcd0 ]
	paddw		%xmm10, %xmm8		# [ abcdF .. abcd8 ]

	phaddw		%xmm5, %xmm4		# 2 [ bF7E6 .. b9180 aF7E6 .. a9180 ]
	phaddw		%xmm7, %xmm6		# 2 [ dF7E6 .. d9180 cF7E6 .. c9180 ]
						# h = F7E6D5C4, l = B3A29180
	phaddw		%xmm6, %xmm4		# 2 [ dh dl ch cl bh bl ah al ]
	phaddw		%xmm15, %xmm4		# 2 [ 0 0 0 0 d c b a ]
	punpcklwd	%xmm15, %xmm4		# 4 [ d c b a ]
	paddd		(%rsi), %xmm4
	movdqa		%xmm4, (%rsi)

	movdqa		%xmm0, %xmm1		# 2 [ abcd7 .. abcd0 ]
	movdqa		%xmm8, %xmm9		# 2 [ abcdF .. abcd8 ]
	punpcklwd	%xmm15, %xmm0		# 4 [ abcd3 .. abcd0 ]
	punpckhwd	%xmm15, %xmm1		# 4 [ abcd7 .. abcd4 ]
	punpcklwd	%xmm15, %xmm8		# 4 [ abcdB .. abcd8 ]
	punpckhwd	%xmm15, %xmm9		# 4 [ abcdF .. abcdC ]
	paddd		0x00(%rdi), %xmm0
	paddd		0x10(%rdi), %xmm1
	paddd		0x20(%rdi), %xmm8
	paddd		0x30(%rdi), %xmm9
	movdqa		%xmm0, 0x00(%rdi)
	movdqa		%xmm1, 0x10(%rdi)
	movdqa		%xmm8, 0x20(%rdi)
	movdqa		%xmm9, 0x30(%rdi)

	ret
#	.size ASM_C_FUNC(plane_madd_ij4x16_asm), .-ASM_C_FUNC(plane_madd_ij4x16_asm)
