
#include <stdlib.h>	/* exit(3) */
#include <stdio.h>	/* *printf(3) */
#include <unistd.h>	/* getopt(3) */
#include <sys/time.h>	/* gettimeofday(3) */
#include <sys/stat.h>	/* stat(3) */
#include <locale.h>	/* setlocale(3) */
#include <assert.h>

#include "common.h"
#include "renderer.h"
#include "ff-input.h"

#define VPDF_SYNC_RDIFF_TH		.125
#define VPDF_SYNC_N_BEST_MATCHES	4

#ifndef VPDF_SYNC_SSIM_VAGUE
# define VPDF_SYNC_SSIM_VAGUE		.4
#endif

#ifndef VPDF_SYNC_SSIM_EXACT
# define VPDF_SYNC_SSIM_EXACT		.95
#endif

#ifndef PLANE_CMP2_EXTRA_FLAGS
# define PLANE_CMP2_EXTRA_FLAGS		0
#endif

#define VPDF_SYNC_CROPDET_FRAC		0x1p-8	/* sth like .4% */
#define VPDF_SYNC_CROPDET_THRESH	32 /* pix-value diff cut-off */
#define VPDF_SYNC_CROPDET_XTHRESH	VPDF_SYNC_CROPDET_THRESH
#define VPDF_SYNC_CROPDET_YTHRESH	VPDF_SYNC_CROPDET_THRESH

C_NAMESPACE_BEGIN

#include "ssim-impl.h"

#include <libswscale/swscale.h>

#ifdef HAVE_LZO
# include <lzo/lzo1x.h>
# define COMPRESS_INIT	1
#else
# define COMPRESS_INIT	0
#endif

#ifdef HAVE_ZLIB
# include <zlib.h>
#endif

struct cimg {
	unsigned lens[4];
	uint8_t data[];
};

struct pimg {
	uint8_t *planes[4];
	int      strides[4];
};

#define PIMG_INIT	{ { NULL, NULL, NULL, NULL, }, { 0, 0, 0, 0, }, }
/*
static void pimg_swap(struct pimg *a, struct pimg *b)
{
	struct pimg c = *a;
	*a = *b;
	*b = c;
}
*/
struct pix_desc {
	const AVPixFmtDescriptor *d;
	enum AVPixelFormat pix_fmt;
	unsigned nb_planes;
};

#define PIX_DESC_INIT	{ NULL, 0, 0, }
#define IS_YUV(d)	((d)->nb_components < 3 || ~(d)->flags & AV_PIX_FMT_FLAG_RGB)

static unsigned pix_fmt_nb_planes(const AVPixFmtDescriptor *d)
{
	unsigned nb_planes = 0;
	for (unsigned i=0; i<d->nb_components; i++)
		nb_planes = MAX(nb_planes, d->comp[i].plane+1);
	return nb_planes;
}

static void pix_desc_init(struct pix_desc *desc, enum AVPixelFormat pix_fmt)
{
	desc->pix_fmt = pix_fmt;
	desc->d = av_pix_fmt_desc_get(pix_fmt);
	desc->nb_planes = pix_fmt_nb_planes(desc->d);
}

#define TS_FMT_MAX	16

static char * ts_fmt_hmsc(char *r, AVRational tbase, int64_t ts)
{
	int neg = ts < 0;
	int64_t t = av_rescale_q(neg ? -ts : ts, tbase, (AVRational){1, 100});
	int l = t % 100; t /= 100;
	int s = t % 60; t /= 60;
	int m = t % 60; t /= 60;
	int h = t;
	snprintf(r, TS_FMT_MAX, "%s%02d:%02d:%02d.%02d", neg ? "-" : "", h, m, s, l);
	return r;
}

static char * ts_fmt_sec1(char *r, AVRational tbase, int64_t ts)
{
	double d = ts * (double)tbase.num/tbase.den;
	snprintf(r, TS_FMT_MAX, "%.1f", d);
	return r;
}

enum { OUT_HUMAN, OUT_JSON, };

static struct output {
	char * (*ts_fmt)(char *, AVRational, int64_t);
	const char *delim;
	const char *fmt;
} const outputs[] = {
	[OUT_HUMAN] = {
		ts_fmt_hmsc, "",
"%1$s - %2$s frames %3$5u to %4$5u show page %5$4d (%6$s) w/ ssim %7$6.4f to %8$6.4f %9$s\n",
	},
	[OUT_JSON] = {
		ts_fmt_sec1, ",",
"{\n\
\t\"pos\": {\n\
\t\t\"from\": { \"ts_sec\": %1$s, \"idx\": %3$5u },\n\
\t\t\"to\"  : { \"ts_sec\": %2$s, \"idx\": %4$5u }\n\
\t},\n\
\t\"page\": { \"num\": %5$4d, \"label\": \"%6$s\" },\n\
\t\"ssim\": { \"from\": %7$6.4f, \"to\": %8$6.4f },\n\
\t\"classification\": \"%9$s\"\n\
}\n", },
};

struct loc_ctx {
	int w, h;                       /* stage 1: vid open */
	char **labels;                  /* stage 2: pdf render */
	struct cimg **buf;              /* stage 2 */
	struct pimg *pbuf;              /* stage 2 */
	struct pimg tmp, vid_ppm;       /* stage 2, 3 */
	struct SwsContext *vid_sws;     /* stage 3 */
	int tmp_page_idx;               /* stage 2, 3 */
	int page_from, page_to;         /* stage 0, 2 */
	struct pix_desc pix_desc;       /* stage 1 */
	char *ren_dump_pat;             /* stage 0: arg parse, 2 */
	char *vid_dump_pat;             /* stage 0, 3 */
	const struct output *out;       /* stage 0, 3 */
	unsigned frame_cmp_nb_planes;   /* stage 1 */
	int verbosity;                  /* stage 0 */
	unsigned compress;              /* stage 0 */
	int crop[4];                    /* stage 0, 2 */
};

#define LOC_CTX_INIT { \
	-1, -1, NULL, NULL, NULL, PIMG_INIT, PIMG_INIT, NULL, -1, 0, -1, \
	PIX_DESC_INIT, NULL, NULL, NULL, 0, 0, COMPRESS_INIT, {0,0,0,0}, \
}

static void frame_cmp(
	const uint8_t *const ref_planes[static 4], const int ref_strides[static 4],
	const struct pimg *ren, double *ssim, double *psnr,
	const struct loc_ctx *ctx
) {
	struct cnt2 c;
	memset(&c, 0, sizeof(c));
	const AVPixFmtDescriptor *d = ctx->pix_desc.d;

	for (unsigned i=0; i<4; i++)
		assert(ref_strides[i] == ren->strides[i]);

	unsigned num = 0; /* #blocks per frame, needed for avg */
	for (unsigned i=0; i<ctx->frame_cmp_nb_planes; i++) {
		unsigned w = -(-ctx->w >> (i ? d->log2_chroma_w : 0));
		unsigned h = -(-ctx->h >> (i ? d->log2_chroma_h : 0));
		num += w * h;
		plane_cmp2(ref_planes[i], ren->planes[i], w, h, ren->strides[i],
		           &c, PLANE_CMP2_SSIM | PLANE_CMP2_EXTRA_FLAGS);
	}
	get_cnt2(&c, num, ssim, psnr);
}

#ifdef HAVE_LZO
static void cimg_uncompress(
	const struct cimg *c, const struct pimg *tmp_img,
	const struct pix_desc *d
) {
	for (unsigned i=0, k=0; i<d->nb_planes; i++) {
		unsigned long ki;
		lzo1x_decompress(c->data+k, c->lens[i], tmp_img->planes[i], &ki,
		                 NULL);
		k += c->lens[i];
	}
}
#endif

static const struct pimg * frame_render(
	const struct loc_ctx *ctx, int page_idx,
	const struct pimg *tmp_img, int *tmp_page_idx
) {
	if (ctx->compress) {
#ifdef HAVE_LZO
		if (*tmp_page_idx == page_idx)
			return tmp_img;
		cimg_uncompress(ctx->buf[page_idx - ctx->page_from], tmp_img,
		                &ctx->pix_desc);
		*tmp_page_idx = page_idx;
		return tmp_img;
#endif
	}
	return &ctx->pbuf[page_idx - ctx->page_from];
}

struct res {
	int page_idx;
	double ssim, psnr;
};

#define RES_INIT	{ -1, -INFINITY, 0 }

static struct res * res_list_sorted_insert(struct res *r, const struct res *c, unsigned n_best)
{
	for (unsigned i=0; i<n_best; i++)
		if (c->ssim > r[i].ssim) {
			memmove(r+i+1, r+i, sizeof(*r)*(n_best-i-1));
			return memcpy(r+i, c, sizeof(*c));
		}
	return NULL;
}

#ifdef _OPENMP
static void locate(
	const uint8_t *const ref_planes[static 4], const int ref_strides[static 4],
	const struct loc_ctx *const ctx,
	int last_page, const unsigned n_best, struct res r[static n_best]
) {
	struct res rr[n_best];
	struct pimg tmp_img;
	struct res c;
	for (unsigned i=0; i<n_best; i++)
		r[i] = (struct res)RES_INIT;
#pragma omp parallel private(rr,tmp_img,c) shared(r,ref_planes,ref_strides)
	{
		memcpy(rr, r, sizeof(*r)*n_best);
		int ret;
		if ((ret = av_image_alloc(tmp_img.planes, tmp_img.strides,
		                          ctx->w, ctx->h, ctx->pix_desc.pix_fmt, 1)) < 0)
			DIE(1, "error allocating raw video buffer: %s", fferror(ret));
#pragma omp for
		for (int i=ctx->page_from; i<ctx->page_to; i++) {
			frame_cmp(ref_planes, ref_strides,
			          frame_render(ctx, c.page_idx = i, &tmp_img,
			                       &(int){-1}),
			          &c.ssim, &c.psnr, ctx);
			res_list_sorted_insert(rr, &c, n_best);
		}
		av_freep(tmp_img.planes);
#pragma omp critical
		{
			struct res *xr = r;
			for (unsigned j=0; j<n_best; j++)
				if (!(xr = res_list_sorted_insert(xr, rr+j, n_best-(xr-r))))
					break;
		}
	}
}
#else
static void locate(
	const uint8_t *const ref_planes[static 4], const int ref_strides[static 4],
	const struct loc_ctx *ctx,
	int last_page, unsigned n_best, struct res r[static n_best]
) {
	for (unsigned i=0; i<n_best; i++)
		r[i] = (struct res)RES_INIT;

	for (int i=ctx->page_from; i<ctx->page_to; i++) {
		struct res c;
		frame_cmp(ref_planes, ref_strides,
		          frame_render(ctx, c.page_idx = i, &ctx->tmp,
		                       &((struct loc_ctx *)ctx)->tmp_page_idx),
		          &c.ssim, &c.psnr, ctx);
		res_list_sorted_insert(r, &c, n_best);
	}
}
#endif

static void ppm_export(
	struct SwsContext *ppm_sws, unsigned w, unsigned h,
	const struct pimg *src, const struct pimg *tmp, const char *path_fmt,
	...
) {
	sws_scale(ppm_sws, (const uint8_t *const *)src->planes, src->strides,
	          0, h, tmp->planes, tmp->strides);

	va_list ap;
	va_start(ap, path_fmt);
	int len = vsnprintf(NULL, 0, path_fmt, ap);
	va_end(ap);
	char buf[len+1];
	va_start(ap, path_fmt);
	vsnprintf(buf, sizeof(buf), path_fmt, ap);
	va_end(ap);

#ifdef HAVE_ZLIB
# define PPM_SUFFIX		".ppm.gz"
# define FFILE			gzFile
# define FOPEN(path,mode)	gzopen(path,mode)
# define FPRINTF(file,fmt,...)	gzprintf(file,fmt,__VA_ARGS__)
# define FWRITE(file,buf,sz)	gzwrite(file,buf,sz)
# define FCLOSE(file)		gzclose(file)
#else
# define PPM_SUFFIX		".ppm"
# define FFILE			FILE *
# define FOPEN(path,mode)	fopen(path,mode)
# define FPRINTF(file,fmt,...)	fprintf(file,fmt,__VA_ARGS__)
# define FWRITE(file,buf,sz)	fwrite(buf,sz,1,file)
# define FCLOSE(file)		fclose(file)
#endif
	FFILE f = FOPEN(buf, "wb");
	FPRINTF(f, "P6 %u %u 255\n", w, h);
	unsigned char *d = tmp->planes[0];
	for (unsigned i=0; i<h; i++, d += tmp->strides[0])
		FWRITE(f, d, 3*w);
	FCLOSE(f);
#undef FFILE
#undef FOPEN
#undef FPRINTF
#undef FWRITE
#undef FCLOSE
}

struct res_frame {
	struct res r_last, r[VPDF_SYNC_N_BEST_MATCHES];
	enum frame_cmp_mode {
		LOCATED, REUSED, REUSED0,
	} mode;
};

#define RES_FRAME_INIT	{ RES_INIT, { RES_INIT,RES_INIT,RES_INIT,RES_INIT, }, }

static struct res decoded_frame(
	AVFrame *const fr[static 2], int frame_idx, const struct loc_ctx *ctx,
	int last_page, double vid_diff_ssim
) {
	struct res r[VPDF_SYNC_N_BEST_MATCHES] = { RES_INIT, };
	const AVFrame *f0 = fr[frame_idx&1], *f1 = fr[1-(frame_idx&1)];
	assert(f0->format == ctx->pix_desc.pix_fmt);
	if (frame_idx) {
		struct pimg n;
		memcpy(n.planes, f0->data, sizeof(n.planes));
		memcpy(n.strides, f0->linesize, sizeof(n.strides));
		frame_cmp((const uint8_t *const *)f1->data, f1->linesize, &n, &r->ssim, &r->psnr, ctx);
		if (r->ssim >= vid_diff_ssim)
			r->page_idx = last_page;
	}
	if (ctx->verbosity > 0) {
		fprintf(stderr, "frame %5d cmp-to-prev: %6.4f %7.3f ",
		        frame_idx, r->ssim, r->psnr);
	}

	enum {
		LOCATED, REUSED, REUSED0,
	} mode;

	static const char *mode_desc[] = { "located", "reused", "reused0", };

	if (r->page_idx < 0) {
		mode = LOCATED;
		locate((const uint8_t *const *)f0->data, f0->linesize, ctx,
		       last_page, ARRAY_SIZE(r), r);
		if (ctx->vid_dump_pat) {
			struct pimg src;
			memcpy(src.planes, f0->data, sizeof(src.planes));
			memcpy(src.strides, f0->linesize, sizeof(src.strides));
			ppm_export(ctx->vid_sws, ctx->w, ctx->h,
			           &src, &ctx->vid_ppm, ctx->vid_dump_pat,
			           r->page_idx+1, frame_idx, r->ssim);
		}
	} else {
		mode = (!ctx->compress || ctx->tmp_page_idx == r->page_idx)
		       ? REUSED0 : REUSED;
		frame_cmp((const uint8_t *const *)f0->data, f0->linesize,
		          frame_render(ctx, r->page_idx, &ctx->tmp,
		                       &((struct loc_ctx *)ctx)->tmp_page_idx),
		          &r->ssim, &r->psnr, ctx);
	}
	if (ctx->verbosity > 0) {
		fprintf(stderr, "%-7s cmp-to-page:", mode_desc[mode]);
		for (unsigned i=0; i<(mode == LOCATED ? ARRAY_SIZE(r) : 1); i++)
			fprintf(stderr, " -> %6.4f %7.3f page %d",
			        r[i].ssim, r[i].psnr, r[i].page_idx+1);
		if (r->ssim < VPDF_SYNC_SSIM_VAGUE)
			fprintf(stderr, " vague");
		fprintf(stderr, "\n");
	}
	return r[0];
}

static const uint8_t black[][4] = { { 0,0,0,0 }, { 0x10,0x80,0x80,0x00 }, };

static void do_crop(
	const struct loc_ctx *ctx,
	uint8_t *const planes[static 4], const int strides[static 4]
) {
	int tgt_pixsteps[4];
	const struct pix_desc *desc = &ctx->pix_desc;
	const AVPixFmtDescriptor *d = desc->d;
	int is_yuv = IS_YUV(d);
	av_image_fill_max_pixsteps(tgt_pixsteps, NULL, d);

	for (unsigned i=0; i<desc->nb_planes; i++) {
		int hsh = is_yuv && i ? d->log2_chroma_h : 0;
		int wsh = is_yuv && i ? d->log2_chroma_w : 0;
		uint8_t *p = planes[i];
		for (unsigned j=0; j < -(-ctx->h >> hsh); j++, p += strides[i]) {
			if (j < ctx->crop[0] || j >= ((ctx->h-ctx->crop[1]) >> hsh)) {
				memset(p, black[is_yuv][i], strides[i]);
			} else if (ctx->crop[2] > 0 || ctx->crop[3] > 0) {
				int l = -(        -ctx->crop[2]  >> wsh) * tgt_pixsteps[i];
				int r = ((ctx->w - ctx->crop[3]) >> wsh) * tgt_pixsteps[i];
				memset(p    , black[is_yuv][i], l);
				memset(p + r, black[is_yuv][i], strides[i] - r);
			}
		}
	}
}

struct res_item {
	struct res_item *next;
	int frame_idx[2]; /* interval */
	int64_t frame_pts[2]; /* interval */
	double ssim[2]; /* interval */
	int page_idx;
};

static struct res_item * res_item_create(
	int frame_idx, const struct res *r, int64_t ts
) {
	struct res_item *t = malloc(sizeof(struct res_item));
	t->next                           = NULL;
	t->frame_idx[0] = t->frame_idx[1] = frame_idx;
	t->frame_pts[0] = t->frame_pts[1] = ts;
	t->ssim[0]      = t->ssim[1]      = r->ssim;
	t->page_idx                       = r->page_idx;
	return t;
}

static struct res_item * run_vid_cmp(
	struct ff_vinput *vin, struct loc_ctx *ctx, const double *vid_diff_ssims
) {
	struct res_item *head = NULL, *t = NULL, **tailp = &head;
	struct res r = RES_INIT;

	AVFrame *fr[2], *f;
	if (!(fr[0] = av_frame_alloc()) ||
	    !(fr[1] = av_frame_alloc()))
		DIE(1, "error allocating AVFrame: %s\n", strerror(errno));

	for (int frame_idx = 0;
	     ff_vinput_read_frame(vin, f = fr[frame_idx&1], frame_idx);
	     frame_idx++) {
		do_crop(ctx, f->data, f->linesize);
		double diff_thresh = r.page_idx < 0 ? 0
		                   : vid_diff_ssims[r.page_idx-ctx->page_from];
		r = decoded_frame(fr, frame_idx, ctx, r.page_idx, diff_thresh);
		if (t && t->page_idx == r.page_idx) {
			int64_t ts = f->pts;
			if (ts == AV_NOPTS_VALUE)
				ts = f->pkt_pts;
			t->frame_idx[1] = frame_idx;
			t->frame_pts[1] = ts;
			t->ssim[0] = MIN(t->ssim[0], r.ssim);
			t->ssim[1] = MAX(t->ssim[1], r.ssim);
		} else if (t) {
			char *label = ctx->labels[t->page_idx - ctx->page_from];
			char *c = t->ssim[0] < VPDF_SYNC_SSIM_VAGUE ? "vague"
			        : t->ssim[1] < VPDF_SYNC_SSIM_EXACT ? "fuzzy"
			        :                                     "exact";
			if (t != head)
				printf("%s", ctx->out->delim);
			printf(ctx->out->fmt,
			       ctx->out->ts_fmt((char[TS_FMT_MAX]){0},
			                        vin->vid_stream->time_base,
			                        t->frame_pts[0]),
			       ctx->out->ts_fmt((char[TS_FMT_MAX]){0},
			                        vin->vid_stream->time_base,
			                        t->frame_pts[1]),
			       t->frame_idx[0], t->frame_idx[1],
			       t->page_idx+1, label ? label : "",
			       t->ssim[0], t->ssim[1], c);
			t = NULL;
		}
		if (!t) {
			int64_t ts = f->pts;
			if (ts == AV_NOPTS_VALUE)
				ts = f->pkt_pts;
			*tailp = t = res_item_create(frame_idx, &r, ts);
			tailp = &t->next;
		}
	}

	av_frame_free(fr+0);
	av_frame_free(fr+1);

	return head;
}

#include "plane-add-generic.h"

#if defined(__i386__) || defined(__x86_64__)
# include "plane-add-x86_64.h"
# ifndef PLANE_ADD_IMPL
#  define PLANE_ADD_IMPL	plane_add_x86_64()
# endif
#endif

#ifndef PLANE_ADD_IMPL
# define PLANE_ADD_IMPL		plane_add_generic
#endif

struct range {
	unsigned a, b;
};

static struct range range_det(
	unsigned n, const unsigned a[static n], unsigned lo, unsigned hi,
	unsigned thresh
) {
	struct range r;
	for (r.a=0; r.a<n; r.a++) {
		int d = (int)a[r.a] - (int)lo;
		if (d < 0)
			d = -d;
		if (d < thresh)
			break;
	}
	for (r.b=n; r.b>r.a; r.b--) {
		int d = (int)a[r.b-1] - (int)hi;
		if (d < 0)
			d = -d;
		if (d < thresh)
			break;
	}
	return r;
}

struct plane {
	struct plane_data {
		uint32_t *x, *y;
	} slide, frame;
	unsigned w, h;
	struct range rx, ry;
};

static void plane_add0(
	plane_add_f *plane_add, const struct plane_data *d,
	unsigned w, unsigned h,
	const uint8_t *data, const unsigned stride
) {
	uint32_t vx[w], vy[h];
	plane_add(vx, vy, data, w, h, stride);
	for (unsigned k=0; k<w; k++)
		d->x[k] += (vx[k] + h/2-1) / h;
	for (unsigned k=0; k<h; k++)
		d->y[k] += (vy[k] + w/2-1) / w;
}

static void plane_fini(
	struct plane *p, unsigned n_slides, unsigned n_frames,
	unsigned xthresh, unsigned ythresh
) {
	for (int i=0; i<p->w; i++) {
		p->slide.x[i] /= n_slides;
		p->frame.x[i] /= n_frames;
	}
	for (int i=0; i<p->h; i++) {
		p->slide.y[i] /= n_slides;
		p->frame.y[i] /= n_frames;
	}

	struct {
		uint32_t a, b;
	} x = { 0, 0 }, y = { 0, 0 };

	unsigned nx = ceil(VPDF_SYNC_CROPDET_FRAC*p->w);
	for (unsigned i=0; i<nx; i++) {
		x.a += p->slide.x[i];
		x.b += p->slide.x[p->w-1-i];
	}
	unsigned ny = ceil(VPDF_SYNC_CROPDET_FRAC*p->h);
	for (unsigned i=0; i<ny; i++) {
		y.a += p->slide.y[i];
		y.b += p->slide.y[p->h-1-i];
	}

	p->rx = range_det(p->w, p->frame.x, (x.a + nx/2-1) / nx,
	                                    (x.b + nx/2-1) / nx, xthresh);
	p->ry = range_det(p->h, p->frame.y, (y.a + ny/2-1) / ny,
	                                    (y.b + ny/2-1) / ny, ythresh);
}

static int cropdet(
	struct loc_ctx *ctx, struct ff_vinput *vin,
	unsigned crop_detect, unsigned xthresh, unsigned ythresh
) {
	int crop_save[4];
	memcpy(crop_save, ctx->crop, sizeof(crop_save));

	/* assumption: uniformity in slides and video
	 *        and  slides take up major spatial space in video
	 *        and  video's non-slide background differs enough from slides'
	 *             borders average intensity/color */

	unsigned n = ctx->pix_desc.nb_planes;
	int is_yuv = IS_YUV(ctx->pix_desc.d);

	struct plane v[n];

	for (unsigned j=0; j<n; j++) {
		struct plane *p = v+j;
		unsigned wsh = is_yuv && j ? ctx->pix_desc.d->log2_chroma_w : 0;
		unsigned hsh = is_yuv && j ? ctx->pix_desc.d->log2_chroma_h : 0;
		p->w = -(-ctx->w >> wsh);
		p->h = -(-ctx->h >> hsh);
		p->slide.x = calloc(p->w, sizeof(uint32_t));
		p->slide.y = calloc(p->h, sizeof(uint32_t));
		p->frame.x = calloc(p->w, sizeof(uint32_t));
		p->frame.y = calloc(p->h, sizeof(uint32_t));
	}

	plane_add_f *plane_add = PLANE_ADD_IMPL;
	for (int i=ctx->page_from; i<ctx->page_to; i++) {
		const struct pimg *img = frame_render(ctx, i, &ctx->tmp,
		                                      &ctx->tmp_page_idx);
		const struct plane *p = v;
		for (unsigned j=0; j<n; j++, p++)
			plane_add0(plane_add, &p->slide, p->w, p->h,
			           img->planes[j], img->strides[j]);
	}

	AVFrame *fr = av_frame_alloc();
	int64_t ts = 0;
	unsigned frame_idx;
	for (frame_idx = 0; ff_vinput_read_frame(vin, fr, frame_idx); frame_idx++) {
		if (frame_idx == 0 && (ts = fr->pts) == AV_NOPTS_VALUE)
			ts = fr->pkt_pts;
		if (ctx->verbosity > 0) {
			fprintf(stderr, "averaging video frames: %u\r", frame_idx);
			fflush(stderr);
		}
		const struct plane *p = v;
		for (unsigned j=0; j<n; j++, p++)
			plane_add0(plane_add, &p->frame, p->w, p->h,
			           fr->data[j], fr->linesize[j]);
	}
	if (ctx->verbosity > 0)
		fprintf(stderr, "\n");
	av_frame_free(&fr);

	for (unsigned j=0; j<n; j++)
		plane_fini(&v[j], ctx->page_to - ctx->page_from, frame_idx,
		           xthresh, ythresh);

	if (ctx->verbosity > 1) {
		for (unsigned i=0; i<v[0].w; i++) {
			fprintf(stderr, "x:%u", i);
			for (unsigned j=0; j<n && i<v[j].w; j++)
				fprintf(stderr, "\tf[%u]:%u\ts[%u]:%u",
				        j, v[j].frame.x[i], j, v[j].slide.x[i]);
			fprintf(stderr, "\n");
		}
		for (unsigned i=0; i<v[0].h; i++) {
			fprintf(stderr, "y:%u", i);
			for (unsigned j=0; j<n && i<v[j].h; j++)
				fprintf(stderr, "\tf[%u]:%u\ts[%u]:%u",
				        j, v[j].frame.y[i], j, v[j].slide.y[i]);
			fprintf(stderr, "\n");
		}
	}
	if (ctx->verbosity > 0) {
		const struct plane *p = v;
		for (unsigned j=0; j<n; j++, p++) {
			fprintf(stderr, "range_w[%u]: %u:%u\n", j, p->rx.a, p->rx.b);
			fprintf(stderr, "range_h[%u]: %u:%u\n", j, p->ry.a, p->ry.b);
		}
	}

	/* seek vin back to initial ts */
	int ret = av_seek_frame(vin->fmt_ctx, vin->vid_stream_idx, ts,
	                        AVSEEK_FLAG_BACKWARD);
	if (ret < 0)
		DIE(1, "error rewinding VID stream: %s\n", fferror(ret));
	vin->end_of_stream = 0;

	for (unsigned j=0; j<n; j++) {
		const struct plane *p = v+j;
		free(p->slide.x);
		free(p->slide.y);
		free(p->frame.x);
		free(p->frame.y);
	}

	if (is_yuv) {
		const struct range *rx = &v[0].rx, *ry = &v[0].ry;
		const unsigned ncrop[4] = {
			ry->a, ctx->h - ry->b,
			rx->a, ctx->w - rx->b,
		};
		for (unsigned i=0; i<4; i++)
			if (crop_detect & (1U << i))
				ctx->crop[i] = ncrop[i];
			else if (ctx->crop[i] != ncrop[i])
				fprintf(stderr, "crop-detect found %c = %u"
				                " better than given %u\n",
				        "TBLR"[i], ncrop[i], ctx->crop[i]);
	} else
		DIE(1, "haven't thought about cropping non-YUV streams yet,"
		       " sorry\n");

	fprintf(stderr, "crop-det: %u:%u:%u:%u\n",
	        ctx->crop[0], ctx->crop[1], ctx->crop[2], ctx->crop[3]);

	return memcmp(ctx->crop, crop_save, sizeof(crop_save)) != 0;
}

struct img_prep_args {
	struct SwsContext *sws;
	struct loc_ctx    *ctx;
	const AVPixFmtDescriptor *s;
	struct timeval    *u;
	/* for LZO compression */
	uint8_t           *compressed_buf;
	uint8_t           *wrkmem;
};

static void vpdf_image_prepare(
	struct vpdf_image *img, const struct img_prep_args *a,
	unsigned page_idx, char *label
) {
	int s = img->s;
	const uint8_t *data = img->data;
	struct loc_ctx *ctx = a->ctx;

	const AVPixFmtDescriptor *tgt_fmt = ctx->pix_desc.d;
	unsigned tgt_nb_planes = ctx->pix_desc.nb_planes;
	int is_yuv = IS_YUV(tgt_fmt);
	int tgt_pixsteps[4];
	av_image_fill_max_pixsteps(tgt_pixsteps, NULL, tgt_fmt);

	struct timeval v;
	if (a->ctx->verbosity > 0) {
		gettimeofday(&v, NULL);
		fprintf(stderr, "rendered page %4d+%4d/%4d (%3s) in %5.1f ms",
		        ctx->page_from+1, page_idx-ctx->page_from,
		        ctx->page_to - ctx->page_from, label,
		        (v.tv_sec-a->u->tv_sec)*1e3+(v.tv_usec-a->u->tv_usec)*1e-3);
		*a->u = v;
	}

	struct pimg *rtgt = ctx->compress ? &ctx->tmp
	                                  : ctx->pbuf+(page_idx-ctx->page_from);
	struct pimg tgt = *rtgt;
	for (unsigned i=0; i<tgt_nb_planes; i++) {
		int hsh = is_yuv && i ? tgt_fmt->log2_chroma_h : 0;
		int wsh = is_yuv && i ? tgt_fmt->log2_chroma_w : 0;
		tgt.planes[i] +=  ( ctx->crop[0] >> hsh) * tgt.strides[i]
		              +  -(-ctx->crop[2] >> wsh) * tgt_pixsteps[i];
	}

	sws_scale(a->sws, &data, &s, 0, img->h, tgt.planes, tgt.strides);

	do_crop(ctx, rtgt->planes, rtgt->strides);

	if (ctx->ren_dump_pat)
		ppm_export(ctx->vid_sws, ctx->w, ctx->h, rtgt,
		           &ctx->vid_ppm, ctx->ren_dump_pat, page_idx+1);

	if (ctx->verbosity > 0) {
		gettimeofday(&v, NULL);
		fprintf(stderr, ", tform %s -> %s in %4.1f ms",
		        a->s->name, tgt_fmt->name,
		         (v.tv_sec -a->u->tv_sec )*1e3
		        +(v.tv_usec-a->u->tv_usec)*1e-3);
	}

	if (ctx->compress) {
#ifdef HAVE_LZO
		/* LZO compress */
		unsigned long kj[4];
		unsigned k = 0;
		unsigned sz = 0;
		*a->u = v;
		for (unsigned j=0; j<tgt_nb_planes; j++) {
			unsigned hsh = is_yuv && j ? tgt_fmt->log2_chroma_h : 0;
			unsigned ph  = -(-ctx->h >> hsh);
			unsigned psz = ph * ctx->tmp.strides[j];
			unsigned long osz;
			sz += psz;
			lzo1x_1_15_compress(ctx->tmp.planes[j], psz,
			                    a->compressed_buf+k, kj+j,
			                    a->wrkmem);
			lzo1x_optimize(a->compressed_buf+k, kj[j],
			               ctx->tmp.planes[j], &osz, NULL);
			k += kj[j];
		}
		if (ctx->verbosity > 0) {
			gettimeofday(&v, NULL);
			fprintf(stderr, ", compressed in %4.0f us: %u"
			                " -> %*u bytes (%5.1f%%)",
			        (v.tv_sec-a->u->tv_sec)*1e6
			        +(v.tv_usec-a->u->tv_usec),
			        sz, snprintf(NULL, 0, "%u", sz), k, 100.0*k/sz);
		}
		struct cimg *c = malloc(offsetof(struct cimg,data) + k);
		for (unsigned j=0; j<tgt_nb_planes; j++)
			c->lens[j] = kj[j];
		memcpy(c->data, a->compressed_buf, k);
		struct cimg **cimg_ptr = &a->ctx->buf[page_idx - ctx->page_from];
		if (*cimg_ptr)
			free(*cimg_ptr);
		*cimg_ptr = c;
#endif
	}

	char **lbl_ptr = &ctx->labels[page_idx - ctx->page_from];
	if (*lbl_ptr)
		free(*lbl_ptr);
	*lbl_ptr = label;

	if (ctx->verbosity > 0) {
		fprintf(stderr, "\n");
		gettimeofday(a->u, NULL);
	}
}

static struct SwsContext * tform_sws_context(
	unsigned w, unsigned h, enum AVPixelFormat from, enum AVPixelFormat to
) {
	return sws_getContext(w, h, from, w, h, to, 0, NULL, NULL, NULL);
}

static void render(struct loc_ctx *ctx, struct vpdf_ren *ren, void *ren_inst)
{
	struct img_prep_args a;
	struct timeval u;
	const AVPixFmtDescriptor *tgt_fmt = ctx->pix_desc.d;

	a.sws = tform_sws_context(ctx->w - ctx->crop[2] - ctx->crop[3],
	                          ctx->h - ctx->crop[0] - ctx->crop[1],
	                          ren->fmt, ctx->pix_desc.pix_fmt);
	a.ctx = ctx;
#ifdef HAVE_LZO
	uint8_t compressed_buf[4*ctx->w*ctx->h*2];
	uint8_t wrkmem[LZO1X_1_15_MEM_COMPRESS];
	a.compressed_buf = ctx->compress ? compressed_buf : NULL;
	a.wrkmem = ctx->compress ? wrkmem : NULL;
#endif
	a.s = av_pix_fmt_desc_get(ren->fmt);
	a.u = &u;

	if (tgt_fmt->flags & AV_PIX_FMT_FLAG_PAL)
		DIE(1, "error: VID's pix_fmt '%s' is palettized,"
		       " that's unsupported\n", tgt_fmt->name);
	if (tgt_fmt->flags & AV_PIX_FMT_FLAG_BITSTREAM)
		DIE(1, "error: VID's pix_fmt '%s' is a bitstream,"
		       " that's unsupported\n", tgt_fmt->name);

	gettimeofday(a.u, NULL);
	ren->render(ren_inst, ctx->page_from, ctx->page_to,
	            vpdf_image_prepare, &a);

	sws_freeContext(a.sws);
}

struct renderer {
	char *id;
	struct vpdf_ren *r;
};

#ifndef _GNU_SOURCE
static int asprintf(char **ptr, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (len < 0) {
		perror("vsnprintf");
		return len;
	}
	char *buf = malloc(len+1);
	if (!buf)
		return -1;
	va_start(ap, fmt);
	len = vsnprintf(buf, len+1, fmt, ap);
	va_end(ap);
	*ptr = len < 0 ? free(buf), NULL : buf;
	return len;
}
#endif

#define VPDF_SYNC_REN_DUMP_PAT_PAT	"%s/%%04d-%s" PPM_SUFFIX
#define VPDF_SYNC_VID_DUMP_PAT_PAT	"%s/%%04d-%%05d-%%6.4f" PPM_SUFFIX

static void loc_ctx_init_vid(
	struct loc_ctx *ctx, const struct renderer *rr, struct ff_vinput *vin,
	int frame_cmp_luma_only,
	const char *ren_dump_dir, const char *vid_dump_dir
) {
	int r;

	pix_desc_init(&ctx->pix_desc, vin->vid_ctx->pix_fmt);
	if (ren_dump_dir)
		asprintf(&ctx->ren_dump_pat, VPDF_SYNC_REN_DUMP_PAT_PAT, ren_dump_dir, rr->id);
	if (vid_dump_dir)
		asprintf(&ctx->vid_dump_pat, VPDF_SYNC_VID_DUMP_PAT_PAT, vid_dump_dir);
	ctx->frame_cmp_nb_planes = frame_cmp_luma_only ? 1 : ctx->pix_desc.nb_planes;

	if ((r = av_image_alloc(ctx->tmp.planes, ctx->tmp.strides,
	                        ctx->w, ctx->h, ctx->pix_desc.pix_fmt, 1)) < 0)
		DIE(1, "error allocating raw video buffer: %s", fferror(r));

	if (ctx->vid_dump_pat || ctx->ren_dump_pat) {
		ctx->vid_sws = tform_sws_context(ctx->w, ctx->h,
		                                 ctx->pix_desc.pix_fmt,
		                                 AV_PIX_FMT_RGB24);
		int r = av_image_alloc(ctx->vid_ppm.planes, ctx->vid_ppm.strides,
		                       ctx->w, ctx->h, AV_PIX_FMT_RGB24, 1);
		if (r < 0)
			DIE(1, "error allocating raw video buffer: %s",
			    fferror(r));
	}
}

/* ctx needs valid values for: .w, .h, .verbosity, .compress
 *           reused are      : .page_{from,to}
 */
static void loc_ctx_init_ren(struct loc_ctx *ctx, unsigned *n_pages)
{
	int r;

	if (ctx->page_to < 0 || ctx->page_to > *n_pages)
		ctx->page_to = *n_pages;
	if (ctx->page_from > ctx->page_to)
		DIE(1, "error: range [%d,%d] of pages to render is empty\n",
		    ctx->page_from+1, ctx->page_to);
	*n_pages = ctx->page_to - ctx->page_from;

	ctx->labels = calloc(*n_pages, sizeof(char *));

	if (ctx->compress) {
		ctx->buf = calloc(*n_pages, sizeof(struct cimg *));
	} else {
		ctx->pbuf = calloc(*n_pages, sizeof(*ctx->pbuf));
		for (int i=0; i<*n_pages; i++)
			if ((r = av_image_alloc(ctx->pbuf[i].planes,
			                        ctx->pbuf[i].strides,
			                        ctx->w, ctx->h,
			                        ctx->pix_desc.pix_fmt, 1)) < 0)
				DIE(1,"error allocating raw video buffer: %s\n",
				    fferror(r));
	}
}

static void loc_ctx_fini_vid(struct loc_ctx *ctx)
{
	if (ctx->vid_sws) {
		sws_freeContext(ctx->vid_sws);
		av_freep(ctx->vid_ppm.planes);
	}

	av_freep(ctx->tmp.planes);
	free(ctx->ren_dump_pat);
	free(ctx->vid_dump_pat);
}

static void loc_ctx_fini_ren(struct loc_ctx *ctx)
{
	unsigned n_pages = ctx->page_to-ctx->page_from;

	if (ctx->pbuf) {
		for (int i=0; i<n_pages; i++)
			av_freep(ctx->pbuf[i].planes);
		free(ctx->pbuf);
	} else {
		for (unsigned i=0; i<n_pages; i++)
			free(ctx->buf[i]);
		free(ctx->buf);
	}

	for (int i=0; i<n_pages; i++)
		free(ctx->labels[i]);
	free(ctx->labels);
}

static void loc_ctx_fini(struct loc_ctx *ctx)
{
	loc_ctx_fini_ren(ctx);
	loc_ctx_fini_vid(ctx);
}

extern struct vpdf_ren vpdf_ren_poppler_glib;
extern struct vpdf_ren vpdf_ren_poppler_cpp;
extern struct vpdf_ren vpdf_ren_gs;

static struct renderer const renderers[] = {
#ifdef HAVE_POPPLER_GLIB
	{ "poppler-cairo" , &vpdf_ren_poppler_glib, },
#endif
#ifdef HAVE_POPPLER_CPP
	{ "poppler-splash", &vpdf_ren_poppler_cpp , },
#endif
#ifdef HAVE_GS
	{ "ghostscript"   , &vpdf_ren_gs          , },
#endif
};

static const struct renderer * renderer_find_working(const char *ren_id)
{
	const struct renderer *rr = NULL;
	for (unsigned i=0; !rr && i<ARRAY_SIZE(renderers); i++)
		if ((!ren_id && renderers[i].r->can_render)
		    || !strcmp(ren_id, renderers[i].id))
			rr = &renderers[i];
	if (!rr) {
		if (!ren_id)
			DIE(1, "error: no usable PDF renderers compiled in\n");
		fprintf(stderr, "error: %s is not amongst the list of known "
		                "PDF renderers:\n", ren_id);
		for (unsigned i=0; i<ARRAY_SIZE(renderers); i++)
			fprintf(stderr, "\t%s\n", renderers[i].id);
		exit(1);
	} else if (!rr->r->can_render)
		DIE(1, "selected PDF renderer %s cannot render\n", ren_id);
	return rr;
}

#ifdef HAVE_GS
# define LICENSE_REQ_AGLPv3
#endif
#if defined(HAVE_POPPLER_GLIB) || defined(HAVE_POPPLER_CPP) || defined(HAVE_LZO)
# define LICENSE_REQ_GPLv2_PLUS
#endif

#define LICENSE_CODE	"GPLv2+"

#if defined(LICENSE_REQ_AGLPv3)
# define LICENSE	"AGPLv3"
#else
# define LICENSE	LICENSE_CODE
#endif

static void usage(const char *progname)
{
	printf("usage: %s [-OPTS] [--] VID REN_OPTS...\n", progname);
	printf("\n");
	printf("\
  VID          path to screen-cast video file\n\
  REN_OPTS...  options to slide renderer, see options '-R' and '-r' for details\n\
\n\
Options [defaults]:\n\
  -c X:Y       set cut-off thresholds of abs-diff luma components of averaged\n\
               slides and frames for crop-detection, either can be empty [%u:%u]\n\
  -C T:B:L:R   pad pixels to renderings wrt. VID; comp < 0 to detect [0:0:0:0]\n\
  -C detect    detect the cropping area based on the average intensity of slides\n\
  -d VID_DIFF  interpret consecutive frames as equal if SSIM >= VID_DIFF [unset]\n\
               (overrides -e)\n\
  -D DIR       dump rendered images into DIR (named PAGE-REN" PPM_SUFFIX ")\n\
  -e RDIFF_TH  interpret consecutive frames as equal if SSIM >= RDIFF + TH where\n\
               RDIFF is computed as max SSIM from this to another rendered\n\
               frame and TH = (1-RDIFF)*RDIFF_TH, i.e. RDIFF_TH is the max.\n\
               expected decrease of turbulence of VID frames wrt. RDIFF till\n\
               which they're still not regarded as equal [%g]\n\
  -h           display this help message\n\
  -j           format output as JSON records [human readable single line]\n\
  -L           display list of compiled/linked libraries\n\
  -p FROM:TO   interval of pages to render (1-based, inclusive, each),\n\
               FROM and TO can both be empty [1:page-num]\n\
  -r REN       use REN to render PDF [%s]\n\
  -R           display usage information for all supported renderers\n\
  -u           don't compress pages (watch out for OOM) [%s]\n\
  -v           increase verbosity\n\
  -V DIR       dump located frames into DIR (named PAGE-FRAME-SSIM" PPM_SUFFIX ")\n\
  -y           toggle compare luma plane only [YUV]\n\
",
	       VPDF_SYNC_CROPDET_THRESH, VPDF_SYNC_CROPDET_YTHRESH,
	       VPDF_SYNC_RDIFF_TH,
	       ARRAY_SIZE(renderers) ? renderers[0].id : "(unsupported)",
#ifdef HAVE_LZO
	       "LZO-compress"
#else
	       "don't compress"
#endif
	);
	printf("\n");
	printf("\
Classification of match certainty:\n\
  'exact' when SSIM >= %-4g (pretty much sure),\n\
  'vague' when SSIM <  %-4g (most probably no match found in page range),\n\
  'fuzzy' otherwise         (match unclear, try adjusting '-C')\n\
",
	       VPDF_SYNC_SSIM_EXACT, VPDF_SYNC_SSIM_VAGUE);
	printf("\n\
Author: Franz Brausse <dev@karlchenofhell.org>; vpdf-sync licensed under " LICENSE ".\n\
");
}

static void rens(void)
{
	for (unsigned i=0; i<ARRAY_SIZE(renderers); i++) {
		char *ren_argv[] = { renderers[i].id, "-h" };
		optind = 1;
		renderers[i].r->create(ARRAY_SIZE(ren_argv), ren_argv, 0, 0);
		printf("\n");
	}
}

static void libs(void)
{
	fprintf(stderr, "Libraries   (compiled,\tlinked):\n");
#ifdef HAVE_ZLIB
	fprintf(stderr, "  zlib      (%s,\t%s)\n",
	        ZLIB_VERSION, zlibVersion());
#endif
#ifdef HAVE_LZO
	fprintf(stderr, "  LZO       (%s,\t%s)\n",
	        LZO_VERSION_STRING, lzo_version_string());
#endif
#ifdef _OPENMP
	fprintf(stderr, "  OpenMP    (%d)\n", _OPENMP);
#endif
	unsigned v;
	v = avformat_version();
	fprintf(stderr, "  avformat  (%s,\t%d.%d.%d)\n",
	        AV_STRINGIFY(LIBAVFORMAT_VERSION),
	        v >> 16, (v >> 8) & 0xff, v & 0xff);
	v = avcodec_version();
	fprintf(stderr, "  avcodec   (%s,\t%d.%d.%d)\n",
	        AV_STRINGIFY(LIBAVCODEC_VERSION),
	        v >> 16, (v >> 8) & 0xff, v & 0xff);
	v = avutil_version();
	fprintf(stderr, "  avutil    (%s,\t%d.%d.%d)\n",
	        AV_STRINGIFY(LIBAVUTIL_VERSION),
	        v >> 16, (v >> 8) & 0xff, v & 0xff);
	v = swscale_version();
	fprintf(stderr, "  swscale   (%s,\t%d.%d.%d)\n",
	        AV_STRINGIFY(LIBSWSCALE_VERSION),
	        v >> 16, (v >> 8) & 0xff, v & 0xff);
}

static void opt_parse_cdet_th(unsigned *xthresh, unsigned *ythresh)
{
	char *endptr = optarg;
	unsigned *p[] = { xthresh, ythresh, };
	for (unsigned i=0; i<ARRAY_SIZE(p); i++) {
		char *s = endptr;
		long v = strtol(s, &endptr, 0);
		if (*endptr != (i+1 < ARRAY_SIZE(p) ? ':' : '\0') || v < 0)
			DIE(1, "expected format X:Y for non-negative integers "
			       "X, Y for option '-c'\n");
		if (endptr++ != s)
			*p[i] = v;
	}
}

static void opt_parse_crop(struct loc_ctx *ctx)
{
	if (!strcmp(optarg, "detect")) {
		ctx->crop[0] = -1;
		ctx->crop[1] = -1;
		ctx->crop[2] = -1;
		ctx->crop[3] = -1;
	} else {
		char *endptr = optarg;
		for (unsigned i=0; i<ARRAY_SIZE(ctx->crop); i++) {
			ctx->crop[i] = strtol(endptr, &endptr, 10);
			if (*endptr++ != (i == 3 ? '\0' : ':'))
				DIE(1, "option -C expects format to be"
				       " T:B:L:R or 'detect'\n");
		}
	}
}

static void opt_parse_pages(struct loc_ctx *ctx)
{
	char *endptr = optarg;
	if (*endptr == ':') {
		ctx->page_from = 0;
	} else {
		ctx->page_from = strtol(endptr, &endptr, 10);
		if (ctx->page_from-- <= 0)
			DIE(1, "expected positive decimal parameter or nothing"
			       " for FROM in option '-p'\n");
	}
	if (*endptr++ != ':')
		DIE(1, "option -p requires a colon-separated range\n");
	if (!*endptr) {
		ctx->page_to = -1;
	} else {
		ctx->page_to = strtol(endptr, &endptr, 10);
		if (*endptr || ctx->page_to <= 0)
			DIE(1, "expected positive decimal parameter or nothing"
			       " for TO in option '-p'\n");
	}
}

static int is_dir(const char *dir)
{
	struct stat st;
	if (stat(dir, &st) != 0)
		DIE(1, "unable to stat() '%s': %s\n",
		    optarg, strerror(errno));
	return S_ISDIR(st.st_mode);
}

int main(int argc, char **argv)
{
	struct loc_ctx ctx = LOC_CTX_INIT;
	char *ren_id = NULL;
	double vid_diff_ssim = -INFINITY;
	double rdiff_th = VPDF_SYNC_RDIFF_TH;
	char *ren_dump_dir = NULL;
	char *vid_dump_dir = NULL;
	int frame_cmp_luma_only = 0;
	unsigned xthresh = VPDF_SYNC_CROPDET_XTHRESH;
	unsigned ythresh = VPDF_SYNC_CROPDET_YTHRESH;

	ctx.out = &outputs[OUT_HUMAN];
	ctx.page_from = 0;
	ctx.page_to   = -1;
	ctx.compress  = COMPRESS_INIT;
	ctx.verbosity = 0;

	int opt;
	char *endptr;
	while ((opt = getopt(argc, argv, ":c:C:d:D:e:hjLp:r:RuvV:y")) != -1)
		switch (opt) {
		case 'c': opt_parse_cdet_th(&xthresh, &ythresh); break;
		case 'C': opt_parse_crop(&ctx); break;
		case 'd':
			vid_diff_ssim = strtof(optarg, &endptr);
			if (*endptr)
				DIE(1, "expected float parameter for option '-d'\n");
			break;
		case 'D':
			if (!is_dir(ren_dump_dir = optarg))
				DIE(1, "option -D expects path to a directory,"
				       " '%s' is none\n", optarg);
			break;
		case 'e':
			rdiff_th = strtof(optarg, &endptr);
			if (*endptr)
				DIE(1, "expected float parameter for option '-e'\n");
			break;
		case 'h': usage(argv[0]); exit(0);
		case 'j': ctx.out = &outputs[OUT_JSON]; break;
		case 'L': libs(); exit(0);
		case 'p': opt_parse_pages(&ctx); break;
		case 'r': ren_id = optarg; break;
		case 'R': rens(); exit(0);
		case 'u': ctx.compress = 0; break;
		case 'v': ctx.verbosity++; break;
		case 'V':
			if (!is_dir(vid_dump_dir = optarg))
				DIE(1, "option -V expects path to a directory,"
				       " '%s' is none\n", optarg);
			break;
		case 'y': frame_cmp_luma_only = !frame_cmp_luma_only; break;
		case ':':
			DIE(1, "error: option '-%c' required a parameter\n",
			    optopt);
		case '?':
			DIE(1, "error: unknown option '-%c', see '-h' for help\n",
			    optopt);
		}

	int r;

	const struct renderer *rr = renderer_find_working(ren_id);

	if (argc - optind < 1)
		DIE(1, "error: expected arguments: VID ...\n");

	const char *vid_path = argv[optind];
	argv[optind] = rr->id;
	argc -= optind;
	argv += optind;
	optind = 1;

	setlocale(LC_CTYPE, "");
	av_register_all();
#ifdef HAVE_LZO
	lzo_init();
#endif

	struct ff_vinput vin = FF_VINPUT_INIT;
	ff_vinput_open(&vin, vid_path);

	ctx.w = vin.vid_ctx->width;
	ctx.h = vin.vid_ctx->height;
	if ((ctx.w|ctx.h) & (0xf >> (frame_cmp_luma_only ? 1 : 0)))
		DIE(1, "error: SSIM/PSNR computation only works for VID planes "
		       "being dimensioned as multiples of 8\n");

	loc_ctx_init_vid(&ctx, rr, &vin, frame_cmp_luma_only,
	                 ren_dump_dir, vid_dump_dir);

	unsigned crop_detect = 0;
	for (unsigned i=0; i<ARRAY_SIZE(ctx.crop); i++)
		if (ctx.crop[i] < 0) { crop_detect |= 1 << i; ctx.crop[i] = 0; }

	if (ctx.crop[0]+ctx.crop[1] >= ctx.h ||
	    ctx.crop[2]+ctx.crop[3] >= ctx.w)
		DIE(1, "error: cropping range %d:%d:%d:%d (T:B:L:R) invalid "
		       "for %ux%u images\n",
		    ctx.crop[0], ctx.crop[1], ctx.crop[2], ctx.crop[3],
		    ctx.w, ctx.h);

	struct vpdf_ren *ren = rr->r;
	void *ren_inst = ren->create(argc, argv,
	                             ctx.w - ctx.crop[2] - ctx.crop[3],
	                             ctx.h - ctx.crop[0] - ctx.crop[1]);
	if (!ren_inst)
		DIE(1, "error creating renderer '%s': check its params\n",
		    rr->id);

	unsigned n_pages = ren->n_pages(ren_inst);
	loc_ctx_init_ren(&ctx, &n_pages);

	render(&ctx, ren, ren_inst);
	ren->destroy(ren_inst);

	if (crop_detect && cropdet(&ctx, &vin, crop_detect, xthresh, ythresh)) {
		/* cropdet() modified crop */
		if (ctx.crop[0]+ctx.crop[1] >= ctx.h ||
		    ctx.crop[2]+ctx.crop[3] >= ctx.w)
			DIE(1, "error: cropping range %d:%d:%d:%d (T:B:L:R)"
			       " invalid for %ux%u images\n",
			    ctx.crop[0], ctx.crop[1], ctx.crop[2], ctx.crop[3],
			    ctx.w, ctx.h);

		optind = 1;
		ren_inst = ren->create(argc, argv,
		                       ctx.w - ctx.crop[2] - ctx.crop[3],
		                       ctx.h - ctx.crop[0] - ctx.crop[1]);
		if (!ren_inst)
			DIE(1, "error creating renderer '%s': check its params\n",
			    rr->id);
		render(&ctx, ren, ren_inst);
		ren->destroy(ren_inst);
	}

	double vid_diff_ssims[n_pages];
	if (vid_diff_ssim >= -1) {
		for (unsigned i=0; i<n_pages; i++)
			vid_diff_ssims[i] = vid_diff_ssim;
	} else {
		struct pimg diff_tmp = PIMG_INIT;
		int diff_tmp_last_page_idx = -1;
		r = av_image_alloc(diff_tmp.planes, diff_tmp.strides,
		                   ctx.w, ctx.h, ctx.pix_desc.pix_fmt, 1);
		if (r < 0)
			DIE(1, "error allocating raw video buffer: %s",
			    fferror(r));
		for (int i=0; i<n_pages; i++) {
			struct res r[2];
			const struct pimg *img;
			img = frame_render(&ctx, ctx.page_from+i,
			                   &diff_tmp, &diff_tmp_last_page_idx);
			locate((const uint8_t *const *)img->planes, img->strides,
			       &ctx, ctx.page_from+i, ARRAY_SIZE(r), r);
			/* allow more turbulence in the VID frame than required
			 * min. SSIM threshold to closest matching rendered
			 * frame */
			double rdiff = r[1].ssim;
			vid_diff_ssims[i] = 1-(1-rdiff)*(1-rdiff_th);
			if (ctx.verbosity > 0)
				fprintf(stderr, "page %d max ssim %6.4f"
				                " -> vid_diff_ssim = %6.4f\n",
				        ctx.page_from+i+1, rdiff,
				        vid_diff_ssims[i]);
		}
		av_freep(diff_tmp.planes);
	}

	struct res_item *ivals = run_vid_cmp(&vin, &ctx, vid_diff_ssims);

	for (struct res_item *i; (i = ivals);) {
		ivals = i->next;
		free(i);
	}

	loc_ctx_fini(&ctx);

	ff_vinput_close(&vin);
}

C_NAMESPACE_END
