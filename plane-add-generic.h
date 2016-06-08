
#ifndef PLANE_ADD_GENERIC
#define PLANE_ADD_GENERIC

#include "common.h"

C_NAMESPACE_BEGIN

typedef void plane_add_f(
	unsigned *restrict, unsigned *restrict,
	const uint8_t *restrict, unsigned, unsigned, unsigned);

#if 0
static void plane_add1(
	unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned mstride, unsigned nstride
) {
	memset(v, 0, n*sizeof(unsigned));
	for (unsigned i=0; i<m; i++)
		for (unsigned j=0; j<n; j++)
			v[j] += p[i*mstride+j*nstride];/*
	for (unsigned j=0; j<n; j++)
		q[j] += v[j] / m;*/
}

static void plane_add_generic(
	unsigned *restrict vx, unsigned *restrict vy,
	const uint8_t *restrict p, unsigned w, unsigned h, unsigned stride
) {
	plane_add1(vx, p, h, w, stride, 1);
	plane_add1(vy, p, w, h, 1, stride);
}
#else
static void plane_add_i(
	unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned mstride
) {
	memset(v, 0, n*sizeof(unsigned));
	for (unsigned i=0; i<m; i++)
		for (unsigned j=0; j<n; j++)
			v[j] += p[i*mstride+j];/*
	for (unsigned j=0; j<n; j++)
		q[j] += v[j] / m;*/
}

static void plane_add_j(
	unsigned *restrict v, const uint8_t *restrict p,
	unsigned m, unsigned n, unsigned nstride
) {
	memset(v, 0, n*sizeof(unsigned));
	for (unsigned j=0; j<n; j++)
		for (unsigned i=0; i<m; i++)
			v[j] += p[i+j*nstride];/*
	for (unsigned j=0; j<n; j++)
		q[j] += v[j] / m;*/
}

static void plane_add_generic(
	unsigned *restrict vx, unsigned *restrict vy,
	const uint8_t *restrict p, unsigned w, unsigned h, unsigned stride
) {
	plane_add_i(vx, p, h, w, stride);
	plane_add_j(vy, p, w, h, stride);
}
#endif

C_NAMESPACE_END

#endif
