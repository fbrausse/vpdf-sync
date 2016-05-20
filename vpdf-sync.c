
#include <stdlib.h>	/* exit(3) */
#include <stdio.h>	/* *printf(3) */
#include <unistd.h>	/* getopt(3) */
#include <sys/time.h>	/* gettimeofday(3) */
#include <assert.h>

#include "common.h"
#include "renderer.h"

#define VPDF_SYNC_VERSION		0.1
#define VPDF_SYNC_VID_DIFF_SSIM		0.98
#define VPDF_SYNC_SSIM_K1		.01
#define VPDF_SYNC_SSIM_K2		.03
#define VPDF_SYNC_N_BEST_MATCHES	4

C_NAMESPACE_BEGIN

#include "ssim/ssim-impl.h"

extern struct vpdf_ren vpdf_ren_poppler_glib;
extern struct vpdf_ren vpdf_ren_poppler_cpp;
extern struct vpdf_ren vpdf_ren_gs;

#include <libavformat/avformat.h>	/* avformat_*() */
#include <libavutil/imgutils.h>		/* av_image_alloc() */
#include <libswscale/swscale.h>

#include <lzo/lzo1x.h>

/* from ssim/f.c */
static void plane_psnr(
	struct cnt2 *c, const u8 *a, const u8 *b,
	unsigned stride, unsigned w, unsigned h
) {
	unsigned i, j;
	u32 _mse;

	for (i=0; i<h; i+=8, a += 8*stride, b += 8*stride)
		for (j=0; j<w; j+=8) {
			mse(a + j, b + j, stride, &_mse);
			cnt2_done(c, 0, 0, 0, _mse);
		}
}

/* from ssim/f.c */
static void plane_ssim(
	struct cnt2 *c, const u8 *a, const u8 *b,
	unsigned stride, unsigned w, unsigned h
) {
	unsigned i, j;
	u32 mu;
	s64 s2, cv;

	for (i=0; i<h; i+=8, a += 8*stride, b += 8*stride)
		for (j=0; j<w; j+=8) {
			mu_var_2x8x8_2(a + j, b + j, stride, &mu, &s2);
			covar_2(a + j, b + j, stride, &mu, &cv);
			cnt2_done(c, mu, cv, s2, 0);
		}
}

/* from ssim/f.c */
static double cnt2_psnr(const struct cnt2 *c, unsigned num)
{
	double dmse = c->mse / (64.0 * num);
	double L = 255;
	double psnr = 10.0 * log10(L*L / dmse);

	return psnr;
}

/* from ssim/f.c */
static double cnt2_ssim(const struct cnt2 *c, unsigned num)
{
	double k1 = VPDF_SYNC_SSIM_K1, k2 = VPDF_SYNC_SSIM_K2;

	double dm0 = c->m0 / (64.0 * num);
	double dm1 = c->m1 / (64.0 * num);
	// double dmse = r.mse / 64.0 / div;
	double dcv = c->cv / ((double)(1 << 18) * num);
	double ds20 = c->s20 / (4096.0 * num);
	double ds21 = c->s21 / (4096.0 * num);

	double c1, c2;
	double L = 255;
	c1 = k1 * L;
	c2 = k2 * L;
	c1 *= c1;
	c2 *= c2;

	double Y = (2*dm0*dm1 + c1) / (dm0 * dm0 + dm1 * dm1 + c1);
	double C = (2 * dcv + c2) / (ds20 + ds21 + c2);
	double ssim = Y * C;
	// double dssim = 1.0 / (1.0 - ssim);
	// double psnr = 10.0 * log10(L*L / dmse);

	return ssim;
}

#ifdef _OPENMP
static void plane_cmp2_omp(const u8 *_a, const u8 *_b, const unsigned w, const unsigned h, unsigned stride, struct cnt2 *const _c)
{
#pragma omp parallel shared(_a,_b)
	{
	struct cnt2 c;
	memset(&c, 0, sizeof(c));
#pragma omp for
	for (unsigned j=0; j<h; j+=8) {
		const u8 *a = _a + j*stride, *b = _b + j*stride;
		for (unsigned i=0; i<w; i+=8) {
			u32 mean, _mse;
			s64 s2, cv;
			mu_var_2x8x8_2(a + i, b + i, stride, &mean, &s2);
			#if 1
			covar_2(a + i, b + i, stride, &mean, &cv);
			mse(a + i, b + i, stride, &_mse);
			#else
			covar_3(a + i, b + i, stride, &mean, &cv);
			_mse = cv >> 32;
			cv = (s32)cv;
			#endif
			cnt2_done(&c, mean, cv, s2, _mse);
		}
	}
#pragma omp critical
	{
		_c->m0  += c.m0;
		_c->m1  += c.m1;
		_c->cv  += c.cv;
		_c->mse += c.mse;
		_c->s20 += c.s20;
		_c->s21 += c.s21;
	}
	}
}
#endif

static void plane_cmp2(const u8 *a, const u8 *b, const unsigned w, const unsigned h, const unsigned stride, struct cnt2 *const c)
{
	unsigned i, j;
	u32 mean, _mse;
	s64 s2, cv;

	for (j=0; j<h; j+=8, a += 8*stride, b += 8*stride)
		for (i=0; i<w; i+=8) {
			mu_var_2x8x8_2(a + i, b + i, stride, &mean, &s2);
			#if 1
			covar_2(a + i, b + i, stride, &mean, &cv);
			mse(a + i, b + i, stride, &_mse);
			#else
			covar_3(a + i, b + i, stride, &mean, &cv);
			_mse = cv >> 32;
			cv = (s32)cv;
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
	double k1 = .01, k2 = .03;
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

	if (1) {/*
		fprintf(f, "mean: %04hx %04hx = %.3f %.3f, "
			"cv: %016llx = %.3f, s2: %08x %08x = %.3f %.3f, mse: %08x = %.3f",
			c->m0, c->m1, dm0, dm1, c->cv, dcv, c->s20, c->s21, ds20, ds21, c->mse, dmse);*/
	} else {
		fprintf(stderr, "mean: %.3f %.3f, cv: %.3f, s2: %.3f %.3f, mse: %.3f",
			dm0, dm1, dcv, ds20, ds21, dmse);
	}
	// fprintf(f, " -> SSIM: %.6f, DSSIM: %g, PSNR: %g dB", ssim, dssim, psnr);

	if (_ssim) *_ssim = ssim;
	if (_psnr) *_psnr = psnr;
}

struct pimg {
	uint8_t *planes[4];
	int      strides[4];
};

static int frame_cmp_luma_only = 0;

static void frame_cmp(const AVFrame *ref, const struct pimg *ren, double *ssim, double *psnr)
{
	struct cnt2 c;
	memset(&c, 0, sizeof(c));
	const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(ref->format);

	unsigned num = 0; /* #blocks per frame, needed for avg */
	for (unsigned i=0; i<d->nb_components && (!frame_cmp_luma_only || !i); i++) {
		unsigned w = ref->width  >> (i ? d->log2_chroma_w : 0);
		unsigned h = ref->height >> (i ? d->log2_chroma_h : 0);
		num += w * h;
		plane_cmp2(ref->data[i], ren->planes[i], w, h, ref->linesize[i], &c);
	}
	get_cnt2(&c, num, ssim, psnr);
}

struct cimg {
	unsigned lens[4];
	uint8_t data[];
};

struct loc_ctx {
	unsigned w, h;
	struct cimg **buf;
	struct pimg tmp;
	int page_from, page_to;
	enum AVPixelFormat pix_fmt;
};
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
	const struct pimg *tmp_img, double *ssim, double *psnr
) {
	const struct cimg *c = ctx->buf[page_idx - ctx->page_from];
	const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(ctx->pix_fmt);
	for (unsigned i=0, k=0; i<d->nb_components; i++) {
		unsigned long ki;
		lzo1x_decompress(c->data+k, c->lens[i], tmp_img->planes[i], &ki, NULL);
		k += c->lens[i];
	}
	frame_cmp(ref, tmp_img, ssim, psnr);
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

static void locate_omp(
	const AVFrame *const ref, const struct loc_ctx *const ctx,
	int last_page, const unsigned n_best, struct res r[static n_best]
) {
	struct res rr[n_best];
	struct pimg tmp_img;
	struct res c;
	for (unsigned i=0; i<n_best; i++)
		r[i] = (struct res)RES_INIT;
#pragma omp parallel private(rr,tmp_img,c)
	{
		memcpy(rr, r, sizeof(*r)*n_best);
		int ret;
		if ((ret = av_image_alloc(tmp_img.planes, tmp_img.strides,
		                          ref->width, ref->height, ref->format, 1)) < 0)
			DIE(1, "error allocating raw video buffer: %s", fferror(ret));
#pragma omp for
		for (int i=ctx->page_from; i<ctx->page_to; i++) {
			frame_render_cmp(ref, ctx, c.page_idx = i, &tmp_img, &c.ssim, &c.psnr);
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
#endif

static void locate_single(
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
			frame_render_cmp(ref, ctx, d, &ctx->tmp, &ssim, &psnr);
			res_list_sorted_insert(r, &(struct res){ d, ssim, psnr }, n_best);
			d--;
		}
		if (u < ctx->page_to) {
			frame_render_cmp(ref, ctx, u, &ctx->tmp, &ssim, &psnr);
			res_list_sorted_insert(r, &(struct res){ u, ssim, psnr }, n_best);
			u++;
		}
	}
}

typedef void locate_f(const AVFrame *, const struct loc_ctx *,
                      int, unsigned, struct res r[*]);

static int decoded_frame(
	AVFrame *fr[static 2], int frame_idx, const struct loc_ctx *ctx,
	int last_page, locate_f *locate, double vid_diff_ssim
) {
	struct res r[VPDF_SYNC_N_BEST_MATCHES] = { RES_INIT };
	assert(fr[frame_idx&1]->format == ctx->pix_fmt);
	if (frame_idx) {
		struct pimg n;
		memcpy(n.planes, fr[frame_idx&1]->data, sizeof(n.planes));
		memcpy(n.strides, fr[frame_idx&1]->linesize, sizeof(n.strides));
		frame_cmp(fr[1-(frame_idx&1)], &n, &r->ssim, &r->psnr);
		if (r->ssim >= vid_diff_ssim)
			r->page_idx = last_page;
	}
	fprintf(stderr, "frame %5d cmp-to-prev: %6.4f %7.3f ",
	        frame_idx, r->ssim, r->psnr);
	if (r->page_idx < 0) {
		locate(fr[frame_idx&1], ctx, last_page, ARRAY_SIZE(r), r);
		fprintf(stderr, "located ");
	} else {
		frame_render_cmp(fr[frame_idx&1], ctx, r->page_idx, &ctx->tmp, &r->ssim, &r->psnr);
		fprintf(stderr, "reused  ");
	}
	fprintf(stderr, "cmp-to-page:");
	for (unsigned i=0; i<ARRAY_SIZE(r); i++)
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
	const AVPixFmtDescriptor *d;
	int is_yuv;
	struct timeval    *u;
	/* for LZO compression */
	uint8_t           *compressed_buf;
	uint8_t           *wrkmem;
};

void vpdf_image_prepare(
	struct vpdf_image *img, const struct img_prep_args *a, unsigned page_idx
) {
	int s = img->s;
	const uint8_t *data = img->data;
	sws_scale(a->sws, &data, &s, 0, img->h, a->ctx->tmp.planes, a->ctx->tmp.strides);

	struct timeval v;
	gettimeofday(&v, NULL);
	fprintf(stderr, "rendered page %4d/%4d in %4.1f ms",
	        page_idx+1, a->ctx->page_to - a->ctx->page_from,
	        (v.tv_sec-a->u->tv_sec)*1e3+(v.tv_usec-a->u->tv_usec)*1e-3);

	/* LZO compress */
	unsigned long kj[4];
	unsigned k = 0;
	unsigned sz = 0;
	*a->u = v;
	for (unsigned j=0; j<a->d->nb_components; j++) {
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
	fprintf(stderr, ", compressed in %4.0f us: %u -> %u bytes\t-> %5.1f%%\n",
		(v.tv_sec-a->u->tv_sec)*1e6+(v.tv_usec-a->u->tv_usec),
		sz, k, 100.0*k/sz);
	struct cimg *c = malloc(offsetof(struct cimg,data)+k);
	for (unsigned j=0; j<a->d->nb_components; j++)
		c->lens[j] = kj[j];
	memcpy(c->data, a->compressed_buf, k);
	a->ctx->buf[page_idx - a->ctx->page_from] = c;

	gettimeofday(a->u, NULL);
}

static void render(
	struct loc_ctx *ctx, struct vpdf_ren *ren, void *ren_inst
) {
	struct img_prep_args a;
	uint8_t compressed_buf[4*ctx->w*ctx->h*2];
	uint8_t wrkmem[LZO1X_1_15_MEM_COMPRESS];
	struct timeval u;

	a.sws = sws_getContext(ctx->w, ctx->h, ren->fmt,
	                       ctx->w, ctx->h, ctx->pix_fmt,
	                       0, NULL, NULL, NULL);
	a.ctx = ctx;
	a.compressed_buf = compressed_buf;
	a.wrkmem = wrkmem;
	a.d = av_pix_fmt_desc_get(ctx->pix_fmt);
	a.is_yuv = a.d->nb_components < 2 || ~a.d->flags & AV_PIX_FMT_FLAG_RGB;
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

static void ff_vinput_close(struct ff_vinput *ff)
{
	av_packet_unref(&ff->pkt);
	avcodec_close(ff->vid_ctx);
	avformat_close_input(&ff->fmt_ctx);
}

static struct {
	char *id;
	struct vpdf_ren *r;
} const renderers[] = {
#ifdef HAVE_POPPLER_GLIB
	{ "poppler-glib", &vpdf_ren_poppler_glib, },
#endif
#ifdef HAVE_POPPLER_CPP
	{ "poppler-c++" , &vpdf_ren_poppler_cpp , },
#endif
#ifdef HAVE_GS
	{ "gs"          , &vpdf_ren_gs          , },
#endif
};

static void usage(const char *progname)
{
	printf("usage: %s [-OPTS] [--] VID REN_OPTS...\n", progname);
	printf("\n");
	printf("\
Options [defaults]:\n\
  -c           toggle compare luma plane only [%s]\n\
  -d VID_DIFF  interpret consecutive frames as equal if SSIM >= VID_DIFF [%g]\n\
  -f PG_FROM   page interval start (inclusive) [0]\n\
  -h           display this help message\n\
  -l IMPL_ID   select implementation for locate: [single]%s\n\
  -r REN       use REN to render PDF [%s]\n\
  -t PG_TO     page interval stop (exclusive) [page-num]\n\
",
	       frame_cmp_luma_only ? "yes" : "no",
	       VPDF_SYNC_VID_DIFF_SSIM,
#ifdef _OPENMP
	       " omp",
#else
	       "",
#endif
	       renderers[0].id);

	for (unsigned i=0; i<ARRAY_SIZE(renderers); i++) {
		printf("\n");
		char *ren_argv[] = { renderers[i].id, "-h" };
		optind = 0;
		renderers[i].r->create(ARRAY_SIZE(ren_argv), ren_argv, 0, 0);
	}

	fprintf(stderr, "\nLibraries   (compiled,\tlinked):\n");
	fprintf(stderr, "  LZO       (%s,\t%s)\n",
	        LZO_VERSION_STRING, lzo_version_string());
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

int main(int argc, char **argv)
{
	char *ren_id = NULL;
	locate_f *locate = locate_single;
	char *endptr;
	double vid_diff_ssim = VPDF_SYNC_VID_DIFF_SSIM;

	int page_from = 0;
	int page_to   = -1;
	int opt;
	while ((opt = getopt(argc, argv, ":cd:f:hl:r:t:")) != -1)
		switch (opt) {
		case 'c':
			frame_cmp_luma_only = !frame_cmp_luma_only;
			break;
		case 'd':
			vid_diff_ssim = strtof(optarg, &endptr);
			if (*endptr)
				DIE(1, "expected float parameter for option '-d'\n");
			break;
		case 'f':
			page_from = strtol(optarg, &endptr, 10);
			if (*endptr)
				DIE(1, "expected decimal parameter for option '-f'\n");
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'l':
			if (!strcmp(optarg, "single")) locate = locate_single;
#ifdef _OPENMP
			else if (!strcmp(optarg, "omp")) locate = locate_omp;
#endif
			else
				DIE(1, "unrecognized implementation identifier for '-l'\n");
			break;
		case 'r':
			ren_id = optarg;
			break;
		case 't':
			page_to = strtol(optarg, &endptr, 10);
			if (*endptr)
				DIE(1, "expected decimal parameter for option '-t'\n");
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
	lzo_init();

	struct ff_vinput vin = FF_VINPUT_INIT;
	ff_vinput_open(&vin, vid_path);

	unsigned w = vin.vid_ctx->width;
	unsigned h = vin.vid_ctx->height;
	if (w & 0xf || h & 0xf)
		DIE(1, "error: SSIM/PSNR computation only works for VID width "
		       "and height being multiples of 16\n");

	void *ren_inst = ren->create(argc, argv, w, h);
	unsigned n_pages = ren->n_pages(ren_inst);
	if (page_to < 0 || page_to > n_pages)
		page_to = n_pages;
	n_pages = page_to - page_from;
	struct cimg *imgs[n_pages];
	struct loc_ctx ctx = {
		w, h, imgs, {}, page_from, page_to, vin.vid_ctx->pix_fmt,
	};
	if ((r = av_image_alloc(ctx.tmp.planes, ctx.tmp.strides,
	                        w, h, vin.vid_ctx->pix_fmt, 1)) < 0)
		DIE(1, "error allocating raw video buffer: %s", fferror(r));
	render(&ctx, ren, ren_inst);
	ren->destroy(ren_inst);

	AVFrame *fr[2] = { av_frame_alloc(), av_frame_alloc(), };
	if (!fr[0] || !fr[1])
		DIE(1, "error allocating AVFrame: %s\n", strerror(errno));

	for (int frame_idx = 0, last_page = -1;
	     ff_vinput_read_frame(&vin, fr[frame_idx&1], frame_idx); frame_idx++)
		last_page = decoded_frame(fr, frame_idx, &ctx, last_page, locate, vid_diff_ssim);

	av_frame_free(fr+0);
	av_frame_free(fr+1);
	av_freep(ctx.tmp.planes);

	for (unsigned i=0; i<ARRAY_SIZE(imgs); i++)
		free(imgs[i]);

	ff_vinput_close(&vin);
}

C_NAMESPACE_END
