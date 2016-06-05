
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

C_NAMESPACE_BEGIN

#include "ssim-impl.h"

#include <libswscale/swscale.h>

#ifdef HAVE_LZO
# include <lzo/lzo1x.h>
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
	int w, h;
	char **labels;
	struct cimg **buf;
	struct pimg *pbuf;
	struct pimg tmp, vid_ppm;
	struct SwsContext *vid_sws;
	int tmp_page_idx;
	int page_from, page_to;
	struct pix_desc pix_desc;
	char *ren_dump_pat;
	char *vid_dump_pat;
	const struct output *out;
	unsigned frame_cmp_nb_planes;
	int verbosity;
	unsigned compress;
};

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
		unsigned w = ctx->w >> (i ? d->log2_chroma_w : 0);
		unsigned h = ctx->h >> (i ? d->log2_chroma_h : 0);
		num += w * h;
		plane_cmp2(ref_planes[i], ren->planes[i], w, h, ren->strides[i],
		           &c, PLANE_CMP2_SSIM | PLANE_CMP2_EXTRA_FLAGS);
	}
	get_cnt2(&c, num, ssim, psnr);
}

#ifdef HAVE_LZO
static void cimg_uncompress(const struct cimg *c, const struct pimg *tmp_img, const struct pix_desc *d)
{
	for (unsigned i=0, k=0; i<d->nb_planes; i++) {
		unsigned long ki;
		lzo1x_decompress(c->data+k, c->lens[i], tmp_img->planes[i], &ki, NULL);
		k += c->lens[i];
	}
}
#endif

static const struct pimg * frame_render(
	const struct loc_ctx *ctx, int page_idx,
	const struct pimg *tmp_img, int *tmp_page_idx
) {
	if (!ctx->compress) {
		tmp_img = &ctx->pbuf[page_idx - ctx->page_from];
	}
#ifdef HAVE_LZO
	else if (*tmp_page_idx != page_idx) {
		cimg_uncompress(ctx->buf[page_idx - ctx->page_from], tmp_img,
		                &ctx->pix_desc);
		*tmp_page_idx = page_idx;
	}
#endif
	return tmp_img;
}

static void frame_render_cmp(
	const uint8_t *const ref_planes[static 4], const int ref_strides[static 4],
	const struct loc_ctx *ctx, int page_idx,
	const struct pimg *tmp_img, int *tmp_page_idx, double *ssim, double *psnr
) {
	frame_cmp(ref_planes, ref_strides,
	          frame_render(ctx, page_idx, tmp_img, tmp_page_idx),
	          ssim, psnr, ctx);
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
			frame_render_cmp(ref_planes, ref_strides, ctx, c.page_idx = i, &tmp_img, &(int){-1}, &c.ssim, &c.psnr);
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
		frame_render_cmp(ref_planes, ref_strides, ctx, c.page_idx = i,
		                 &ctx->tmp, &((struct loc_ctx *)ctx)->tmp_page_idx,
		                 &c.ssim, &c.psnr);
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
		locate((const uint8_t *const *)f0->data, f0->linesize, ctx, last_page, ARRAY_SIZE(r), r);
		if (ctx->vid_dump_pat) {
			struct pimg src;
			memcpy(src.planes, f0->data, sizeof(src.planes));
			memcpy(src.strides, f0->linesize, sizeof(src.strides));
			ppm_export(ctx->vid_sws, ctx->w, ctx->h,
			           &src, &ctx->vid_ppm, ctx->vid_dump_pat,
			           r->page_idx+1, frame_idx, r->ssim);
		}
	} else {
		mode = (!ctx->compress || ctx->tmp_page_idx == r->page_idx) ? REUSED0 : REUSED;
		frame_render_cmp((const uint8_t *const *)f0->data, f0->linesize, ctx, r->page_idx, &ctx->tmp,
		                 &((struct loc_ctx *)ctx)->tmp_page_idx,
		                 &r->ssim, &r->psnr);
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

struct res_item {
	struct res_item *next;
	int frame_idx[2]; /* interval */
	int64_t frame_pts[2]; /* interval */
	double ssim[2]; /* interval */
	int page_idx;
};

static struct res_item * run_vid_cmp(struct ff_vinput *vin, struct loc_ctx *ctx, const double *vid_diff_ssims)
{
	struct res_item *head = NULL, *t = NULL, **tailp = &head;
	struct res r = RES_INIT;

	AVFrame *fr[2] = { av_frame_alloc(), av_frame_alloc(), };
	if (!fr[0] || !fr[1])
		DIE(1, "error allocating AVFrame: %s\n", strerror(errno));

	for (int frame_idx = 0; ff_vinput_read_frame(vin, fr[frame_idx&1], frame_idx); frame_idx++) {
		r = decoded_frame(fr, frame_idx, ctx, r.page_idx,
		                  r.page_idx < 0 ? 0 : vid_diff_ssims[r.page_idx-ctx->page_from]);
		if (t && t->page_idx == r.page_idx) {
			int64_t ts = fr[frame_idx&1]->pts;
			if (ts == AV_NOPTS_VALUE)
				ts = fr[frame_idx&1]->pkt_pts;
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
			*tailp = t = malloc(sizeof(*t));
			tailp = &t->next;
			t->next = NULL;
			t->page_idx = r.page_idx;
			memcpy(t->frame_idx, ((int[2]){ frame_idx, frame_idx }), sizeof(t->frame_idx));
			int64_t ts = fr[frame_idx&1]->pts;
			if (ts == AV_NOPTS_VALUE)
				ts = fr[frame_idx&1]->pkt_pts;
			memcpy(t->frame_pts, ((int64_t[2]){ ts, ts }), sizeof(t->frame_pts));
			memcpy(t->ssim, ((double[2]){ r.ssim, r.ssim }), sizeof(t->ssim));
		}
	}

	av_frame_free(fr+0);
	av_frame_free(fr+1);

	return head;
}

struct img_prep_args {
	struct SwsContext *sws;
	struct loc_ctx    *ctx;
	const AVPixFmtDescriptor *s;
	const AVPixFmtDescriptor *d;
	const int         *crop;
	int is_yuv;
	struct timeval    *u;
	/* for LZO compression */
	uint8_t           *compressed_buf;
	uint8_t           *wrkmem;
};

static const uint8_t black[][4] = { { 0,0,0,0 }, { 0x10,0x80,0x80,0x00 }, };

void vpdf_image_prepare(
	struct vpdf_image *img, const struct img_prep_args *a,
	unsigned page_idx, char *label
) {
	int s = img->s;
	const uint8_t *data = img->data;

	unsigned tgt_nb_planes = pix_fmt_nb_planes(a->d);
	int tgt_pixsteps[4];
	av_image_fill_max_pixsteps(tgt_pixsteps, NULL, a->d);

	struct timeval v;
	if (a->ctx->verbosity > 0) {
		gettimeofday(&v, NULL);
		fprintf(stderr, "rendered page %4d+%4d/%4d (%s) in %5.1f ms",
		        a->ctx->page_from+1, page_idx-a->ctx->page_from,
		        a->ctx->page_to - a->ctx->page_from, label,
		        (v.tv_sec-a->u->tv_sec)*1e3+(v.tv_usec-a->u->tv_usec)*1e-3);
		*a->u = v;
	}

	struct pimg *rtgt = a->ctx->compress ? &a->ctx->tmp
	                                     : a->ctx->pbuf+(page_idx-a->ctx->page_from);
	struct pimg tgt = *rtgt;
	for (unsigned i=0; i<tgt_nb_planes; i++) {
		int hsh = a->is_yuv && i ? a->d->log2_chroma_h : 0;
		int wsh = a->is_yuv && i ? a->d->log2_chroma_w : 0;
		tgt.planes[i] +=  ( a->crop[0] >> hsh) * tgt.strides[i]
		              +  -(-a->crop[2] >> wsh) * tgt_pixsteps[i];
	}

	sws_scale(a->sws, &data, &s, 0, img->h, tgt.planes, tgt.strides);

	/* TODO: do this for VID frames, too */
	for (unsigned i=0; i<tgt_nb_planes; i++) {
		int hsh = a->is_yuv && i ? a->d->log2_chroma_h : 0;
		int wsh = a->is_yuv && i ? a->d->log2_chroma_w : 0;
		uint8_t *p = rtgt->planes[i];
		for (unsigned j=0; j < -(-a->ctx->h >> hsh); j++, p += rtgt->strides[i]) {
			if (j < a->crop[0] || j >= ((a->ctx->h-a->crop[1]) >> hsh)) {
				memset(p, black[a->is_yuv][i], rtgt->strides[i]);
			} else {
				int l = -(           -a->crop[2]  >> wsh) * tgt_pixsteps[i];
				int r = ((a->ctx->w - a->crop[3]) >> wsh) * tgt_pixsteps[i];
				memset(p    , black[a->is_yuv][i], l);
				memset(p + r, black[a->is_yuv][i], rtgt->strides[i] - r);
			}
		}
	}


	if (a->ctx->ren_dump_pat)
		ppm_export(a->ctx->vid_sws, a->ctx->w, a->ctx->h, rtgt,
		           &a->ctx->vid_ppm, a->ctx->ren_dump_pat, page_idx+1); /* TODO: -f is 1-based */

	if (a->ctx->verbosity > 0) {
		gettimeofday(&v, NULL);
		fprintf(stderr, ", tform %s -> %s in %4.1f ms", a->s->name, a->d->name,
		        (v.tv_sec-a->u->tv_sec)*1e3+(v.tv_usec-a->u->tv_usec)*1e-3);
	}

	if (a->ctx->compress) {
#ifdef HAVE_LZO
		/* LZO compress */
		unsigned long kj[4];
		unsigned k = 0;
		unsigned sz = 0;
		*a->u = v;
		for (unsigned j=0; j<tgt_nb_planes; j++) {
			unsigned ph  = -(-a->ctx->h >> (a->is_yuv && j ? a->d->log2_chroma_h : 0));
			unsigned psz = ph * a->ctx->tmp.strides[j];
			unsigned long osz;
			sz += psz;
			lzo1x_1_15_compress(a->ctx->tmp.planes[j], psz,
					    a->compressed_buf+k, kj+j, a->wrkmem);
			lzo1x_optimize(a->compressed_buf+k, kj[j],
				       a->ctx->tmp.planes[j], &osz, NULL);
			k += kj[j];
		}
		if (a->ctx->verbosity > 0) {
			gettimeofday(&v, NULL);
			fprintf(stderr, ", compressed in %4.0f us: %u -> %*u bytes (%5.1f%%)",
			        (v.tv_sec-a->u->tv_sec)*1e6+(v.tv_usec-a->u->tv_usec),
			        sz, snprintf(NULL, 0, "%u", sz), k, 100.0*k/sz);
		}
		struct cimg *c = malloc(offsetof(struct cimg,data)+k);
		for (unsigned j=0; j<tgt_nb_planes; j++)
			c->lens[j] = kj[j];
		memcpy(c->data, a->compressed_buf, k);
		a->ctx->buf[page_idx - a->ctx->page_from] = c;
#endif
	}

	a->ctx->labels[page_idx - a->ctx->page_from] = label;

	if (a->ctx->verbosity > 0) {
		fprintf(stderr, "\n");
		gettimeofday(a->u, NULL);
	}
}

static struct SwsContext * tform_sws_context(
	unsigned w, unsigned h, enum AVPixelFormat from, enum AVPixelFormat to
) {
	return sws_getContext(w, h, from, w, h, to, 0, NULL, NULL, NULL);
}

static void render(
	struct loc_ctx *ctx, struct vpdf_ren *ren, void *ren_inst,
	const int crop[static 4]
) {
	struct img_prep_args a;
	struct timeval u;

	a.sws = tform_sws_context(ctx->w - crop[2] - crop[3],
	                          ctx->h - crop[0] - crop[1],
	                          ren->fmt, ctx->pix_desc.pix_fmt);
	a.ctx = ctx;
#ifdef HAVE_LZO
	uint8_t compressed_buf[4*ctx->w*ctx->h*2];
	uint8_t wrkmem[LZO1X_1_15_MEM_COMPRESS];
	a.compressed_buf = ctx->compress ? compressed_buf : NULL;
	a.wrkmem = ctx->compress ? wrkmem : NULL;
#endif
	a.crop = crop;
	a.s = av_pix_fmt_desc_get(ren->fmt);
	a.d = av_pix_fmt_desc_get(ctx->pix_desc.pix_fmt);
	a.is_yuv = a.d->nb_components < 3 || ~a.d->flags & AV_PIX_FMT_FLAG_RGB;
	a.u = &u;

	if (a.d->flags & AV_PIX_FMT_FLAG_PAL)
		DIE(1, "error: VID's pix_fmt '%s' is palettized, that's unsupported\n", a.d->name);
	if (a.d->flags & AV_PIX_FMT_FLAG_BITSTREAM)
		DIE(1, "error: VID's pix_fmt '%s' is a bitstream, that's unsupported\n", a.d->name);

	gettimeofday(a.u, NULL);
	ren->render(ren_inst, ctx->page_from, ctx->page_to, &a);

	sws_freeContext(a.sws);
}

extern struct vpdf_ren vpdf_ren_poppler_glib;
extern struct vpdf_ren vpdf_ren_poppler_cpp;
extern struct vpdf_ren vpdf_ren_gs;

static struct {
	char *id;
	struct vpdf_ren *r;
} const renderers[] = {
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

#define VPDF_SYNC_REN_DUMP_PAT_PAT	"%s/%%04d-%s" PPM_SUFFIX
#define VPDF_SYNC_VID_DUMP_PAT_PAT	"%s/%%04d-%%05d-%%6.4f" PPM_SUFFIX

static void usage(const char *progname)
{
	printf("usage: %s [-OPTS] [--] VID REN_OPTS...\n", progname);
	printf("\n");
	printf("\
  VID          path to screen-cast video file\n\
  REN_OPTS...  options to slide renderer, see options '-R' and '-r' for details\n\
\n\
Options [defaults]:\n\
  -C T:B:L:R   pad pixels to renderings wrt. VID [0:0:0:0]\n\
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
  -L           display list of compiles/linked libraries\n\
  -p FROM:TO   interval of pages to render (1-based, inclusive, each),\n\
               FROM and TO can both be empty [1:page-num]\n\
  -r REN       use REN to render PDF [%s]\n\
  -R           display usage information for all supported renderers\n\
  -u           don't compress pages (watch out for OOM) [%s]\n\
  -v           increase verbosity\n\
  -V DIR       dump located frames into DIR (named PAGE-FRAME-SSIM" PPM_SUFFIX ")\n\
  -y           toggle compare luma plane only [YUV]\n\
",
	       VPDF_SYNC_RDIFF_TH,
	       ARRAY_SIZE(renderers) ? renderers[0].id : "",
#ifdef HAVE_LZO
	       "compress"
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
Author: Franz Brausse <dev@karlchenofhell.org>, code licensed under GPLv2.\n\
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

int main(int argc, char **argv)
{
	char *ren_id = NULL;
	char *endptr;
	double vid_diff_ssim = -INFINITY;
	double rdiff_th = VPDF_SYNC_RDIFF_TH;
	char *ren_dump_dir = NULL;
	char *vid_dump_dir = NULL;
	int crop[4] = {0,0,0,0};
	const struct output *out = &outputs[OUT_HUMAN];

	int page_from = 0;
	int page_to   = -1;
	int compress  = 0;
	int frame_cmp_luma_only = 0;
	int verbosity = 0;
#ifdef HAVE_LZO
	compress = 1;
#endif
	int opt;
	struct stat st;
	while ((opt = getopt(argc, argv, ":C:d:D:e:hjLp:r:RuvV:y")) != -1)
		switch (opt) {
		case 'C':
			endptr = optarg;
			for (unsigned i=0; i<ARRAY_SIZE(crop); i++) {
				crop[i] = strtol(endptr, &endptr, 10);
				if (*endptr++ != (i == 3 ? '\0' : ':'))
					DIE(1, "option -C expects format to be T:B:L:R\n");
				if (crop[i] < 0)
					DIE(1, "option -C requires non-negative integer parameters\n");
			}
			break;
		case 'd':
			vid_diff_ssim = strtof(optarg, &endptr);
			if (*endptr)
				DIE(1, "expected float parameter for option '-d'\n");
			break;
		case 'D':
			if (stat(ren_dump_dir = optarg, &st) != 0)
				DIE(1, "unable to stat() '%s': %s\n", optarg, strerror(errno));
			if (!S_ISDIR(st.st_mode))
				DIE(1, "option -D expects path to a directory, '%s' is none\n", optarg);
			break;
		case 'e':
			rdiff_th = strtof(optarg, &endptr);
			if (*endptr)
				DIE(1, "expected float parameter for option '-e'\n");
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'j':
			out = &outputs[OUT_JSON];
			break;
		case 'L':
			libs();
			exit(0);
		case 'p':
			endptr = optarg;
			if (*endptr == ':') {
				page_from = 0;
			} else {
				page_from = strtol(endptr, &endptr, 10);
				if (page_from-- <= 0)
					DIE(1, "expected positive decimal parameter or nothing for FROM in option '-p'\n");
			}
			if (*endptr++ != ':')
				DIE(1, "option -p requires a colon-separated range\n");
			if (!*endptr) {
				page_to = -1;
			} else {
				page_to = strtol(endptr, &endptr, 10);
				if (*endptr || page_to <= 0)
					DIE(1, "expected positive decimal parameter or nothing for TO in option '-p'\n");
			}
			break;
		case 'r':
			ren_id = optarg;
			break;
		case 'R':
			rens();
			exit(0);
		case 'u': compress = 0; break;
		case 'v': verbosity++; break;
		case 'V':
			if (stat(vid_dump_dir = optarg, &st) != 0)
				DIE(1, "unable to stat() '%s': %s\n", optarg, strerror(errno));
			if (!S_ISDIR(st.st_mode))
				DIE(1, "option -V expects path to a directory, '%s' is none\n", optarg);
			break;
		case 'y':
			frame_cmp_luma_only = !frame_cmp_luma_only;
			break;
		case ':':
			DIE(1, "error: option '-%c' required a parameter\n",
			    optopt);
		case '?':
			DIE(1,
			    "error: unknown option '-%c', see '-h' for help\n",
			    optopt);
		}

	int r;

	struct vpdf_ren *ren = NULL;
	for (unsigned i=0; !ren && i<ARRAY_SIZE(renderers); i++)
		if ((!ren_id && renderers[i].r->can_render)
		    || !strcmp(ren_id, renderers[i].id)) {
			ren_id = renderers[i].id;
			ren = renderers[i].r;
		}
	if (!ren) {
		if (!ren_id)
			DIE(1, "error: no usable PDF renderers compiled in\n");
		fprintf(stderr, "error: %s is not amongst the list of known "
		                "PDF renderers:\n", ren_id);
		for (unsigned i=0; i<ARRAY_SIZE(renderers); i++)
			fprintf(stderr, "\t%s\n", renderers[i].id);
		exit(1);
	} else if (!ren->can_render)
		DIE(1, "selected PDF renderer %s cannot render\n", ren_id);

	if (argc - optind < 1)
		DIE(1, "error: expected arguments: VID ...\n");

	const char *vid_path = argv[optind];
	argv[optind] = ren_id;
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

	unsigned w = vin.vid_ctx->width;
	unsigned h = vin.vid_ctx->height;
	if ((w|h) & (0xf >> (frame_cmp_luma_only ? 1 : 0)))
		DIE(1, "error: SSIM/PSNR computation only works for VID planes "
		       "being dimensioned as multiples of 8\n");

	if (crop[0]+crop[1] >= h || crop[2]+crop[3] >= w)
		DIE(1, "error: cropping range %d:%d:%d:%d (T:B:L:R) invalid "
		       "for %ux%u images\n",
		    crop[0], crop[1], crop[2], crop[3], w, h);

	void *ren_inst = ren->create(argc, argv, w-crop[2]-crop[3], h-crop[0]-crop[1]);
	if (!ren_inst)
		DIE(1, "error creating renderer '%s': check its params\n", ren_id);
	unsigned n_pages = ren->n_pages(ren_inst);

	if (page_to < 0 || page_to > n_pages)
		page_to = n_pages;
	if (page_from > page_to)
		DIE(1, "error: range [%d,%d] of pages to render is empty\n",
		    page_from+1, page_to);
	n_pages = page_to - page_from;
	struct cimg *imgs[n_pages];
	char *labels[n_pages];
	memset(labels, 0, sizeof(labels));
	struct loc_ctx ctx = {
		w, h, labels, NULL, NULL, PIMG_INIT, PIMG_INIT, NULL,
		-1, page_from, page_to, PIX_DESC_INIT,
		NULL,
		NULL,
		out,
		0, verbosity, compress,
	};
	pix_desc_init(&ctx.pix_desc, vin.vid_ctx->pix_fmt);
	ctx.frame_cmp_nb_planes = frame_cmp_luma_only ? 1 : ctx.pix_desc.nb_planes;
	if (ren_dump_dir)
		asprintf(&ctx.ren_dump_pat, VPDF_SYNC_REN_DUMP_PAT_PAT, ren_dump_dir, ren_id);
	if (vid_dump_dir)
		asprintf(&ctx.vid_dump_pat, VPDF_SYNC_VID_DUMP_PAT_PAT, vid_dump_dir);
	if ((r = av_image_alloc(ctx.tmp.planes, ctx.tmp.strides,
	                        w, h, ctx.pix_desc.pix_fmt, 1)) < 0)
		DIE(1, "error allocating raw video buffer: %s", fferror(r));

	if (ctx.vid_dump_pat || ctx.ren_dump_pat) {
		ctx.vid_sws = tform_sws_context(w, h, ctx.pix_desc.pix_fmt, AV_PIX_FMT_RGB24);
		int r;
		if ((r = av_image_alloc(ctx.vid_ppm.planes, ctx.vid_ppm.strides,
		                        w, h, AV_PIX_FMT_RGB24, 1)) < 0)
			DIE(1, "error allocating raw video buffer: %s", fferror(r));
	}

	if (compress) {
		ctx.buf = imgs;
	} else {
		ctx.pbuf = calloc(page_to - page_from, sizeof(*ctx.pbuf));
		for (int i=0; i<page_to-page_from; i++)
			if ((r = av_image_alloc(ctx.pbuf[i].planes, ctx.pbuf[i].strides, w, h, ctx.pix_desc.pix_fmt, 1)) < 0)
				DIE(1, "error allocating raw video buffer: %s\n", fferror(r));
	}

	render(&ctx, ren, ren_inst, crop);
	ren->destroy(ren_inst);

	double vid_diff_ssims[page_to-page_from];
	if (vid_diff_ssim >= -1) {
		for (unsigned i=0; i<page_to-page_from; i++)
			vid_diff_ssims[i] = vid_diff_ssim;
	} else {
		struct pimg diff_tmp = PIMG_INIT;
		int diff_tmp_last_page_idx = -1;
		if ((r = av_image_alloc(diff_tmp.planes, diff_tmp.strides,
					w, h, ctx.pix_desc.pix_fmt, 1)) < 0)
			DIE(1, "error allocating raw video buffer: %s", fferror(r));
		for (int i=page_from; i<page_to; i++) {
			struct res r[2];
			const struct pimg *img = frame_render(&ctx, i, &diff_tmp, &diff_tmp_last_page_idx);
			locate((const uint8_t *const *)img->planes, img->strides, &ctx, i, ARRAY_SIZE(r), r);
			/* allow 12.5% (normalized) more turbulence in the
			 * VID frame than required min. SSIM threshold to
			 * closest matching rendered frame */
			double rdiff = r[1].ssim;
			vid_diff_ssims[i-page_from] = 1-(1-rdiff)*(1-rdiff_th);
			if (ctx.verbosity > 0)
				fprintf(stderr, "page %d max ssim %6.4f -> vid_diff_ssim = %6.4f\n",
				        i+1, rdiff, vid_diff_ssims[i-page_from]);
		}
		av_freep(diff_tmp.planes);
	}

	struct res_item *ivals = run_vid_cmp(&vin, &ctx, vid_diff_ssims);

	for (struct res_item *i; (i = ivals);) {
		ivals = i->next;
		free(i);
	}

	if (ctx.pbuf) {
		for (int i=0; i<page_to-page_from; i++)
			av_freep(ctx.pbuf[i].planes);
		free(ctx.pbuf);
	} else {
		for (unsigned i=0; i<ARRAY_SIZE(imgs); i++)
			free(imgs[i]);
	}
	for (int i=0; i<page_to-page_from; i++)
		free(ctx.labels[i]);

	if (ctx.vid_sws) {
		sws_freeContext(ctx.vid_sws);
		av_freep(ctx.vid_ppm.planes);
	}

	av_freep(ctx.tmp.planes);
	free(ctx.ren_dump_pat);
	free(ctx.vid_dump_pat);

	ff_vinput_close(&vin);
}

C_NAMESPACE_END
