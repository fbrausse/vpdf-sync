
#ifndef PLANE_ADD_X86_64_H
#define PLANE_ADD_X86_64_H

#include "plane-add-generic.h"

C_NAMESPACE_BEGIN

#ifdef __x86_64__
void plane_add_i8x8_asm(
	unsigned *restrict q, unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned mstride
) __attribute__((sysv_abi));
void plane_add_j8x8_asm(
	unsigned *restrict q, unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned mstride
) __attribute__((sysv_abi));
void plane_add_ij8x8_asm(
	unsigned *restrict v, unsigned *restrict w, const uint8_t *restrict p,
	unsigned stride
) __attribute__((sysv_abi));
void plane_add_ij4x16_asm(
	unsigned *restrict v, unsigned *restrict w, const uint8_t *restrict p,
	unsigned stride
) __attribute__((sysv_abi));


static void plane_add_ssse3_i(
	unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned mstride
) {
	memset(v, 0, n*sizeof(unsigned));
	unsigned i = 0, j;
	for (; i+8<=m; i+=8) {
		for (j=0; j+8<=n; j+=8)
			plane_add_i8x8_asm(NULL,v+j,p+i*mstride+j,m,n,mstride);
		for (unsigned i1=i; i1<i+8; i1++)
			for (unsigned j1=j; j1<n; j1++)
				v[j1] += p[i1*mstride+j1];
	}
	for (; i<m; i++)
		for (j=0; j<n; j++)
			v[j] += p[i*mstride+j];/*
	for (unsigned j=0; j<n; j++)
		q[j] += v[j] / m;*/
}

static void plane_add_ssse3_j(
	unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned nstride
) {
	memset(v, 0, n*sizeof(unsigned));
	unsigned i, j = 0;
	for (; j+8<=n; j+=8) {
		for (i=0; i+8<=m; i+=8)
			plane_add_j8x8_asm(NULL,v+j,p+i+j*nstride,m,n,nstride);
		for (unsigned j1=j; j1<j+8; j1++)
			for (unsigned i1=i; i1<m; i1++)
				v[j1] += p[i1+j1*nstride];
	}
	for (; j<n; j++)
		for (i=0; i<m; i++)
			v[j] += p[i+j*nstride];/*
	for (unsigned j=0; j<n; j++)
		q[j] += v[j] / m;*/
}

static void plane_add_ssse3_i8x8j8x8(
	unsigned *restrict vx, unsigned *restrict vy,
	const uint8_t *restrict p, unsigned w, unsigned h, unsigned stride
) {
	plane_add_i(vx, p, h, w, stride);
	plane_add_j(vy, p, w, h, stride);
}

static void plane_add_ssse3_ij8x8(
	unsigned *restrict vx, unsigned *restrict vy,
	const uint8_t *restrict p, unsigned w, unsigned h, unsigned stride
) {
	memset(vx, 0, w*sizeof(unsigned));
	memset(vy, 0, h*sizeof(unsigned));
	unsigned i, j;
	for (i=0; i+8<=h; i+=8) {
		for (j=0; j+8<=w; j+=8)
			plane_add_ij8x8_asm(vx+j, vy+i, p+i*stride+j, stride);
		for (unsigned i1=i; i1<i+8; i1++)
			for (unsigned j1=j; j1<w; j1++) {
				vx[j1] += p[i1*stride+j1];
				vy[i1] += p[i1*stride+j1];
			}
	}
	for (; i<h; i++)
		for (j=0; j<w; j++) {
			vx[j] += p[i*stride+j];
			vy[i] += p[i*stride+j];
		}/*
	for (j=0; j<w; j++)
		qx[j] += vx[j] / h;
	for (i=0; i<h; i++)
		qy[i] += vy[i] / w;*/
}
#else
void plane_add_ij4x16_asm_sse2(
	unsigned *restrict v, unsigned *restrict w, const uint8_t *restrict p,
	unsigned stride
) __attribute((cdecl));
#define plane_add_ij4x16_asm	plane_add_ij4x16_asm_sse2
#endif

void plane_add_ssse3_ij4x16(
	unsigned *restrict vx, unsigned *restrict vy,
	const uint8_t *restrict p, unsigned w, unsigned h, unsigned stride
) {
	memset(vx, 0, w*sizeof(unsigned));
	memset(vy, 0, h*sizeof(unsigned));
	unsigned i, j;
	for (i=0; i+4<=h; i+=4) {
		for (j=0; j+16<=w; j+=16)
			plane_add_ij4x16_asm(vx+j, vy+i, p+i*stride+j, stride);
		for (unsigned i1=i; i1<i+4; i1++)
			for (unsigned j1=j; j1<w; j1++) {
				vx[j1] += p[i1*stride+j1];
				vy[i1] += p[i1*stride+j1];
			}
	}
	for (; i<h; i++)
		for (j=0; j<w; j++) {
			vx[j] += p[i*stride+j];
			vy[i] += p[i*stride+j];
		}
}

#ifndef PLANE_ADD_X86_64_IMPL
# define PLANE_ADD_X86_64_IMPL	plane_add_ssse3_ij4x16
#endif

struct x86_ext {
	unsigned sse2 : 1;
	unsigned sse3 : 1;
	unsigned ssse3 : 1;
	unsigned sse4_1 : 1;
	unsigned sse4_2 : 1;
	unsigned sse4a : 1;
	unsigned sse5_xop : 1;
	unsigned sse5_fma4 : 1;
	unsigned sse5_cvt16 : 1;
	unsigned sse5_avx : 1;
};

static plane_add_f * plane_add_x86_64(void)
{
#ifdef __GNUC__
# ifdef __x86_64__
#  define CPUID(abcd,func,id)                                                  \
	__asm__ __volatile__ (                                                \
		"xchg{q}\t{%%}rbx, %q1; cpuid; xchg{q}\t{%%}rbx, %q1"         \
		: "=a"(abcd[0]), "=&r"(abcd[1]), "=c"(abcd[2]), "=d"(abcd[3]) \
		: "0" (func), "2" (id))
# else
#  define CPUID(abcd,func,id)                                                  \
	__asm__ __volatile__ (                                                \
		"xchg\t{%%}ebx, %q1; cpuid; xchg\t{%%}ebx, %q1"         \
		: "=a"(abcd[0]), "=&r"(abcd[1]), "=c"(abcd[2]), "=d"(abcd[3]) \
		: "0" (func), "2" (id))
# endif
	unsigned abcd[4] = {0,0,0,0};
	CPUID(abcd,0x1,0);
	if (abcd[2] & (1U << 9)) { /* SSSE3 */
		//fprintf(stderr, "using SSSE3\n");
		return PLANE_ADD_X86_64_IMPL;
	} else if (abcd[3] & (1U << 26)) { /* SSE2 */
		fprintf(stderr, "using SSE2\n");
		return plane_add_ssse3_ij4x16;
	}
#endif
	//fprintf(stderr, "using generic\n");
	return plane_add_generic;
}

C_NAMESPACE_END

#endif
