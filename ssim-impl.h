
#ifndef SSIM_IMPL_H

#include <inttypes.h>

#define PLANE_CMP2_SSIM	(1 << 0)
#define PLANE_CMP2_PSNR	(1 << 1)

/* suggested default values */

#ifndef SSIM_K1
# define SSIM_K1	.01
#endif
#ifndef SSIM_K2
# define SSIM_K2	.03
#endif
#ifndef SSIM_L
# define SSIM_L		255	/* max pix component value */
#endif

/* sums up all values of 2 horizontally adjacent 8x8 blocks into
 * 2 times a 16-bit value *u (lower half: first block) */

#ifdef __x86_64__
/* u: 64 * mean
 * s: 64^2 * variance
 *
 * u[31..16]: 64 * µ_f1 = \sum f1_i: mean block 1
 * u[15.. 0]: 64 * µ_f0 = \sum f0_i: mean block 0
 * s[63..32]: 64^2 * s^2_f1 = 64 * (\sum f1_i^2 - 64 * µ_f1^2): variance block 1
 * s[31.. 0]: 64^2 * s^2_f0 = 64 * (\sum f0_i^2 - 64 * µ_f0^2): variance block 0
 */
void mu_var_2x8x8_2(const void *in1, const void *in2, uint32_t stride, uint32_t *u, int64_t *s);
/* mu = 64 * µ => kv = 64^3 * s_{f0,f1} (28 bit = 10.18 fixed-point) */
void covar_2(const void *in1, const void *in2, uint32_t stride, const uint32_t *mu, int64_t *covar);
void mse(const void *in1, const void *in2, uint32_t stride, uint32_t *mse);
unsigned sad_16x16(const void *in1, const void *in2, uint32_t stride);
#endif

struct cnt2 {
	uint64_t m0, m1;
	int64_t s20, s21;
	int64_t cv;
	uint64_t mse;
};

static inline void cnt2_done(struct cnt2 *c, uint32_t mean, int64_t cv, int64_t s2, uint32_t mse)
{
	c->m0  += (uint16_t)mean;
	c->m1  += mean >> 16;
	c->cv  += cv;
	c->mse += mse;
	c->s20 += (int32_t)s2;
	c->s21 += s2 >> 32;
}

static inline void plane_cmp2(
	const uint8_t *a, const uint8_t *b,
	const unsigned w, const unsigned h, const unsigned stride,
	struct cnt2 *const c, unsigned flags
) {
	uint32_t mean = 0, _mse = 0;
	int64_t s2 = 0, cv = 0;
	for (unsigned j=0; j<h; j+=8, a += 8*stride, b += 8*stride)
		for (unsigned i=0; i<w; i+=8) {
			if (flags & PLANE_CMP2_SSIM)
				mu_var_2x8x8_2(a + i, b + i, stride, &mean, &s2);
#if 1
			if (flags & PLANE_CMP2_SSIM)
				covar_2(a + i, b + i, stride, &mean, &cv);
			if (flags & PLANE_CMP2_PSNR)
				mse(a + i, b + i, stride, &_mse);
#else
			if (flags & (PLANE_CMP2_SSIM | PLANE_CMP2_PSNR)) {
				covar_3(a + i, b + i, stride, &mean, &cv);
				_mse = cv >> 32;
				cv = (int32_t)cv;
			}
#endif
			cnt2_done(c, mean, cv, s2, _mse);
		}
}


static void get_cnt2(const struct cnt2 *c, double div, double *_ssim, double *_psnr)
{
	double dm0 = c->m0 / div;
	double dm1 = c->m1 / div;
	double dmse = c->mse / div;
	double dcv = c->cv / (div * (1 << 12));
	double ds20 = c->s20 / (div * 64);
	double ds21 = c->s21 / (div * 64);

	double c1, c2;
	double k1 = SSIM_K1, k2 = SSIM_K2;
	double L = SSIM_L;
	c1 = k1 * L;
	c2 = k2 * L;
	c1 *= c1;
	c2 *= c2;

	double Y = (2*dm0*dm1 + c1) / (dm0 * dm0 + dm1 * dm1 + c1);
	double C = (2 * dcv + c2) / (ds20 + ds21 + c2);
	double ssim = Y * C;
//	double dssim = 1.0 / (1.0 - ssim);
	double psnr = 10.0 * log10(L*L / dmse);

	if (_ssim) *_ssim = ssim;
	if (_psnr) *_psnr = psnr;
}


#endif
