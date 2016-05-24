
#include <stdlib.h>	/* exit(3) */
#include <stdio.h>	/* *printf(3) */
#include <unistd.h>	/* getopt(3) */
#include <sys/time.h>	/* gettimeofday(3) */
#include <sys/stat.h>	/* stat(3) */
#include <assert.h>

#include "common.h"
#include "renderer.h"

#define VPDF_SYNC_VID_DIFF_SSIM		0.98
#define VPDF_SYNC_SSIM_K1		.01
#define VPDF_SYNC_SSIM_K2		.03
#define VPDF_SYNC_N_BEST_MATCHES	4

C_NAMESPACE_BEGIN

#include "ssim/ssim-impl.h"

#include <libavformat/avformat.h>	/* avformat_*() */
#include <libavutil/imgutils.h>		/* av_image_alloc() */
#include <libswscale/swscale.h>

#ifdef HAVE_LZO
# include <lzo/lzo1x.h>
#endif

#ifdef HAVE_ZLIB
# include <zlib.h>
#endif

#define PLANE_CMP2_SSIM	(1 << 0)
#define PLANE_CMP2_PSNR	(1 << 1)

static void plane_cmp2(
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
				cv = (s32)cv;
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
	double k1 = VPDF_SYNC_SSIM_K1, k2 = VPDF_SYNC_SSIM_K2;
	double L;
	L = 255;
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

struct cimg {
	unsigned lens[4];
	uint8_t data[];
};

struct pimg {
	uint8_t *planes[4];
	int      strides[4];
};

#define PIMG_INIT	{ { NULL, NULL, NULL, NULL, }, { 0, 0, 0, 0, }, }

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

struct loc_ctx {
	int w, h;
	struct cimg **buf;
	struct pimg *pbuf;
	struct pimg tmp, vid_ppm;
	struct SwsContext *vid_sws;
	int tmp_page_idx;
	int page_from, page_to;
	struct pix_desc pix_desc;
	char *ren_dump_pat;
	char *vid_dump_pat;
	unsigned frame_cmp_nb_planes;
	unsigned compress : 1;
};

static void frame_cmp(const AVFrame *ref, const struct pimg *ren, double *ssim, double *psnr, const struct loc_ctx *ctx)
{
	struct cnt2 c;
	memset(&c, 0, sizeof(c));
	const AVPixFmtDescriptor *d = ctx->pix_desc.d;

	unsigned num = 0; /* #blocks per frame, needed for avg */
	for (unsigned i=0; i<ctx->frame_cmp_nb_planes; i++) {
		unsigned w = ref->width  >> (i ? d->log2_chroma_w : 0);
		unsigned h = ref->height >> (i ? d->log2_chroma_h : 0);
		num += w * h;
		plane_cmp2(ref->data[i], ren->planes[i], w, h, ref->linesize[i],
		           &c, PLANE_CMP2_SSIM | 0*PLANE_CMP2_PSNR);
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

/*
static void pimg_swap(struct pimg *a, struct pimg *b)
{
	struct pimg c = *a;
	*a = *b;
	*b = c;
}
*/
static void frame_render_cmp(
	const AVFrame *ref, const struct loc_ctx *ctx, int page_idx,
	const struct pimg *tmp_img, int *tmp_page_idx, double *ssim, double *psnr
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
	frame_cmp(ref, tmp_img, ssim, psnr, ctx);
}

static char * fferror(int errnum)
{
	static char str[AV_ERROR_MAX_STRING_SIZE] = {0};
	return av_make_error_string(str, sizeof(str), errnum);
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
#include <omp.h>

static void locate(
	const AVFrame *const ref, const struct loc_ctx *const ctx,
	int last_page, const unsigned n_best, struct res r[static n_best]
) {
	struct res rr[n_best];
	struct pimg tmp_img;
	struct res c;
	for (unsigned i=0; i<n_best; i++)
		r[i] = (struct res)RES_INIT;
#pragma omp parallel private(rr,tmp_img,c) shared(r)
	{
		memcpy(rr, r, sizeof(*r)*n_best);
		int ret;
		if ((ret = av_image_alloc(tmp_img.planes, tmp_img.strides,
		                          ref->width, ref->height, ref->format, 1)) < 0)
			DIE(1, "error allocating raw video buffer: %s", fferror(ret));
#pragma omp for
		for (int i=ctx->page_from; i<ctx->page_to; i++) {
			frame_render_cmp(ref, ctx, c.page_idx = i, &tmp_img, &(int){-1}, &c.ssim, &c.psnr);
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
	const AVFrame *ref, const struct loc_ctx *ctx,
	int last_page, unsigned n_best, struct res r[static n_best]
) {
	for (unsigned i=0; i<n_best; i++)
		r[i] = (struct res)RES_INIT;

	double ssim, psnr;
	int pg = MAX(ctx->page_from, last_page);
	int d = pg, u = pg+1;
	while (d >= ctx->page_from || u < ctx->page_to) {
		if (d >= ctx->page_from) {
			frame_render_cmp(ref, ctx, d, &ctx->tmp, &((struct loc_ctx *)ctx)->tmp_page_idx, &ssim, &psnr);
			res_list_sorted_insert(r, &(struct res){ d, ssim, psnr }, n_best);
			d--;
		}
		if (u < ctx->page_to) {
			frame_render_cmp(ref, ctx, u, &ctx->tmp, &((struct loc_ctx *)ctx)->tmp_page_idx, &ssim, &psnr);
			res_list_sorted_insert(r, &(struct res){ u, ssim, psnr }, n_best);
			u++;
		}
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
	char buf[len+3+1];
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

static int decoded_frame(
	AVFrame *fr[static 2], int frame_idx, const struct loc_ctx *ctx,
	int last_page, double vid_diff_ssim
) {
	struct res r[VPDF_SYNC_N_BEST_MATCHES] = { RES_INIT };
	const AVFrame *f0 = fr[frame_idx&1], *f1 = fr[1-(frame_idx&1)];
	assert(f0->format == ctx->pix_desc.pix_fmt);
	if (frame_idx) {
		struct pimg n;
		memcpy(n.planes, f0->data, sizeof(n.planes));
		memcpy(n.strides, f0->linesize, sizeof(n.strides));
		frame_cmp(f1, &n, &r->ssim, &r->psnr, ctx);
		if (r->ssim >= vid_diff_ssim)
			r->page_idx = last_page;
	}
	fprintf(stderr, "frame %5d cmp-to-prev: %6.4f %7.3f ",
	        frame_idx, r->ssim, r->psnr);

	enum {
		LOCATED, REUSED, REUSED0,
	} mode;

	static const char *mode_desc[] = { "located", "reused", "reused0", };

	if (r->page_idx < 0) {
		mode = LOCATED;
		locate(f0, ctx, last_page, ARRAY_SIZE(r), r);
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
		frame_render_cmp(f0, ctx, r->page_idx, &ctx->tmp,
		                 &((struct loc_ctx *)ctx)->tmp_page_idx,
		                 &r->ssim, &r->psnr);
	}
	fprintf(stderr, "%-7s cmp-to-page:", mode_desc[mode]);
	for (unsigned i=0; i<(mode == LOCATED ? ARRAY_SIZE(r) : 1); i++)
		fprintf(stderr, " -> %6.4f %7.3f page %d",
			r[i].ssim, r[i].psnr, r[i].page_idx+1);
	if (r->ssim < .3)
		fprintf(stderr, " vague");
	fprintf(stderr, "\n");
	return r->page_idx;
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
	struct vpdf_image *img, const struct img_prep_args *a, unsigned page_idx
) {
	int s = img->s;
	const uint8_t *data = img->data;

	unsigned tgt_nb_planes = pix_fmt_nb_planes(a->d);
	int tgt_pixsteps[4];
	av_image_fill_max_pixsteps(tgt_pixsteps, NULL, a->d);

	struct timeval v;
	gettimeofday(&v, NULL);
	fprintf(stderr, "rendered page %4d+%4d/%4d in %5.1f ms",
	        a->ctx->page_from+1, page_idx-a->ctx->page_from,
	        a->ctx->page_to - a->ctx->page_from,
	        (v.tv_sec-a->u->tv_sec)*1e3+(v.tv_usec-a->u->tv_usec)*1e-3);

	*a->u = v;

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


	gettimeofday(&v, NULL);
	fprintf(stderr, ", tform %s -> %s in %4.1f ms", a->s->name, a->d->name,
	        (v.tv_sec-a->u->tv_sec)*1e3+(v.tv_usec-a->u->tv_usec)*1e-3);

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
		gettimeofday(&v, NULL);
		fprintf(stderr, ", compressed in %4.0f us: %u -> %*u bytes (%5.1f%%)",
			(v.tv_sec-a->u->tv_sec)*1e6+(v.tv_usec-a->u->tv_usec),
			sz, snprintf(NULL, 0, "%u", sz), k, 100.0*k/sz);
		struct cimg *c = malloc(offsetof(struct cimg,data)+k);
		for (unsigned j=0; j<tgt_nb_planes; j++)
			c->lens[j] = kj[j];
		memcpy(c->data, a->compressed_buf, k);
		a->ctx->buf[page_idx - a->ctx->page_from] = c;
#endif
	}

	fprintf(stderr, "\n");
	gettimeofday(a->u, NULL);
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

struct ff_vinput {
	AVFormatContext *fmt_ctx;
	AVStream *vid_stream;
	AVCodecContext *vid_ctx;
	AVCodec *vid_codec;
	int vid_stream_idx;
	int end_of_stream, got_frame;
	AVPacket pkt;
};

#define FF_VINPUT_INIT	{ NULL, NULL, NULL, NULL, -1, 0, 0 }

static void ff_vinput_open(struct ff_vinput *ff, const char *vid_path)
{
	int r;
	if ((r = avformat_open_input(&ff->fmt_ctx, vid_path, NULL, NULL)) < 0)
		DIE(1, "error opening VID '%s': %s\n", vid_path, fferror(r));
	if ((r = avformat_find_stream_info(ff->fmt_ctx, NULL)) < 0)
		DIE(1, "error finding stream info of VID '%s': %s\n", vid_path,
		    fferror(r));
	if ((r = av_find_best_stream(ff->fmt_ctx, AVMEDIA_TYPE_VIDEO,
	                             -1, -1, NULL, 0)) < 0)
		DIE(1, "error finding %s stream in VID '%s': %s\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO), vid_path,
		    fferror(r));
	ff->vid_stream_idx = r;
	ff->vid_stream = ff->fmt_ctx->streams[ff->vid_stream_idx];
	ff->vid_ctx    = ff->vid_stream->codec;
	if (!(ff->vid_codec = avcodec_find_decoder(ff->vid_ctx->codec_id)))
		DIE(1, "error finding %s codec %s for VID '%s'\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
		    avcodec_get_name(ff->vid_ctx->codec_id), vid_path);
	if ((r = avcodec_open2(ff->vid_ctx, ff->vid_codec, NULL)) < 0)
		DIE(1, "error opening %s codec for VID '%s': %s\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO), vid_path,
		    fferror(r));
	av_init_packet(&ff->pkt);
}

static int ff_vinput_read_frame(struct ff_vinput *ff, AVFrame *fr, int frame_idx)
{
	while (!ff->end_of_stream || ff->got_frame) {
		if (!ff->end_of_stream && av_read_frame(ff->fmt_ctx, &ff->pkt) < 0)
			ff->end_of_stream = 1;
		if (ff->end_of_stream) {
			ff->pkt.data = NULL;
			ff->pkt.size = 0;
		}
		if (ff->pkt.stream_index == ff->vid_stream_idx || ff->end_of_stream) {
			ff->got_frame = 0;
			if (ff->pkt.pts == AV_NOPTS_VALUE)
				ff->pkt.pts = ff->pkt.dts = frame_idx;
			int r = avcodec_decode_video2(ff->vid_ctx, fr, &ff->got_frame, &ff->pkt);
			if (r < 0) {
				fprintf(stderr, "error decoding VID frame %d: %s\n",
				        frame_idx, fferror(r));
				return r;
			}
			av_packet_unref(&ff->pkt);
			av_init_packet(&ff->pkt);
			if (ff->got_frame)
				return 1;
		}
	}
	return 0;
}

static void run_vid_cmp(struct ff_vinput *vin, struct loc_ctx *ctx, double vid_diff_ssim)
{
	AVFrame *fr[2] = { av_frame_alloc(), av_frame_alloc(), };
	if (!fr[0] || !fr[1])
		DIE(1, "error allocating AVFrame: %s\n", strerror(errno));

	for (int frame_idx = 0, last_page = -1;
	     ff_vinput_read_frame(vin, fr[frame_idx&1], frame_idx); frame_idx++)
		last_page = decoded_frame(fr, frame_idx, ctx, last_page, vid_diff_ssim);

	av_frame_free(fr+0);
	av_frame_free(fr+1);
}

static void ff_vinput_close(struct ff_vinput *ff)
{
	av_packet_unref(&ff->pkt);
	avcodec_close(ff->vid_ctx);
	avformat_close_input(&ff->fmt_ctx);
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
Options [defaults]:\n\
  -C T:B:L:R   pad pixels to renderings wrt. VID [0:0:0:0]\n\
  -d VID_DIFF  interpret consecutive frames as equal if SSIM >= VID_DIFF [%g]\n\
  -D DIR       dump rendered images into DIR (named PAGE-REN" PPM_SUFFIX ")\n\
  -f PG_FROM   page interval start (1-based, inclusive) [1]\n\
  -h           display this help message\n\
  -r REN       use REN to render PDF [%s]\n\
  -t PG_TO     page interval stop (1-based, inclusive) [page-num]\n\
  -u           don't compress pages (watch out for OOM) [%s]\n\
  -V DIR       dump located frames into DIR (named PAGE-FRAME-SSIM" PPM_SUFFIX ")\n\
  -y           toggle compare luma plane only [YUV]\n\
",
	       VPDF_SYNC_VID_DIFF_SSIM,
	       renderers[0].id,
#ifdef HAVE_LZO
	       "compress"
#else
	       "don't compress"
#endif
	);

	for (unsigned i=0; i<ARRAY_SIZE(renderers); i++) {
		printf("\n");
		char *ren_argv[] = { renderers[i].id, "-h" };
		optind = 0;
		renderers[i].r->create(ARRAY_SIZE(ren_argv), ren_argv, 0, 0);
	}

	fprintf(stderr, "\nLibraries   (compiled,\tlinked):\n");
#ifdef HAVE_LZO
	fprintf(stderr, "  LZO       (%s,\t%s)\n",
	        LZO_VERSION_STRING, lzo_version_string());
#endif
	unsigned v;
	v = avformat_version();
	fprintf(stderr, "  avformat  (%s,\t%d.%d.%d)\n",
	        AV_STRINGIFY(LIBAVFORMAT_VERSION),
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
	if (len < 0)
		goto out;
	char *buf = malloc(len+1);
	if (!buf)
		return -1;
	va_start(ap, fmt);
	len = vsnprintf(buf, len+1, fmt, ap);
	va_end(ap);
out:
	return len;
}
#endif

int main(int argc, char **argv)
{
	char *ren_id = NULL;
	char *endptr;
	double vid_diff_ssim = VPDF_SYNC_VID_DIFF_SSIM;
	char *ren_dump_dir = NULL;
	char *vid_dump_dir = NULL;
	int crop[4] = {0,0,0,0};

	int page_from = 0;
	int page_to   = -1;
	int compress  = 0;
	int frame_cmp_luma_only = 0;
#ifdef HAVE_LZO
	compress = 1;
#endif
	int opt;
	struct stat st;
	while ((opt = getopt(argc, argv, ":C:d:D:f:hr:t:uV:y")) != -1)
		switch (opt) {
		case 'C':
			endptr = optarg;
			for (unsigned i=0; i<ARRAY_SIZE(crop); i++) {
				crop[i] = strtol(endptr, &endptr, 10);
				if (*endptr++ != (i == 3 ? '\0' : ':'))
					DIE(1, "option -C expects format to be T:B:L:R\n");
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
		case 'f':
			page_from = strtol(optarg, &endptr, 10);
			if (*endptr || page_from-- <= 0)
				DIE(1, "expected positive decimal parameter for option '-f'\n");
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'r':
			ren_id = optarg;
			break;
		case 't':
			page_to = strtol(optarg, &endptr, 10);
			if (*endptr || page_to <= 0)
				DIE(1, "expected positive decimal parameter for option '-t'\n");
			break;
		case 'u': compress = 0; break;
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
	optind = 0;

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

	void *ren_inst = ren->create(argc, argv, w-crop[2]-crop[3], h-crop[0]-crop[1]);
	if (!ren_inst)
		DIE(1, "error creating renderer '%s': check its params\n", ren_id);
	unsigned n_pages = ren->n_pages(ren_inst);

	if (page_to < 0 || page_to > n_pages)
		page_to = n_pages;
	n_pages = page_to - page_from;
	struct cimg *imgs[n_pages];
	struct loc_ctx ctx = {
		w, h, NULL, NULL, PIMG_INIT, PIMG_INIT, NULL,
		-1, page_from, page_to, PIX_DESC_INIT,
		NULL,
		NULL,
		0, compress,
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

	run_vid_cmp(&vin, &ctx, vid_diff_ssim);

	if (ctx.pbuf) {
		for (int i=0; i<page_to-page_from; i++)
			av_freep(ctx.pbuf[i].planes);
		free(ctx.pbuf);
	} else {
		for (unsigned i=0; i<ARRAY_SIZE(imgs); i++)
			free(imgs[i]);
	}

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
