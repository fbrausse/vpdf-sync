
#include <stdlib.h>	/* exit(3) */
#include <stdio.h>	/* *printf(3) */
#include <unistd.h>	/* getopt(3) */

#include "common.h"
#include "vpdf-pdf.h"

C_NAMESPACE_BEGIN

extern struct vpdf_ren vpdf_ren_poppler_glib;
extern struct vpdf_ren vpdf_ren_poppler_cpp;

#include <libavformat/avformat.h>	/* avformat_*() */
#include <libavutil/imgutils.h>		/* av_image_alloc() */
#include <libswscale/swscale.h>

static char * fferror(int errnum)
{
	static char str[AV_ERROR_MAX_STRING_SIZE] = {0};
	return av_make_error_string(str, sizeof(str), errnum);
}

struct img_prep_args {
	struct SwsContext *sws;
	uint8_t *dst_data[4];
	int      dst_stride[4];
};

void vpdf_image_prepare(struct vpdf_image *img, const void *args)
{
	const struct img_prep_args *a = args;
	int s = img->s;
	const uint8_t *data = img->data;
	int r = sws_scale(a->sws, &data, &s, 0, img->h, a->dst_data, a->dst_stride);
}

static void usage(const char *progname)
{
	printf("usage: %s [-OPTS] [--] VID PDF\n", progname);
	printf("\n");
	printf("\
Options:\n\
  -h          display this help message\n\
  -p          password to unlock PDF\n\
	");
}

static struct {
	const char *id;
	struct vpdf_ren *r;
} const renderers[] = {
#ifdef HAVE_POPPLER_GLIB
	{ "poppler-glib", &vpdf_ren_poppler_glib, },
#endif
#ifdef HAVE_POPPLER_CPP
	{ "poppler-c++" , &vpdf_ren_poppler_cpp , },
#endif
};

int main(int argc, char **argv)
{
	char *password = NULL;
	const char *ren_id = NULL;

	int opt;
	while ((opt = getopt(argc, argv, ":hp:r:")) != -1)
		switch (opt) {
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'p':
			password = optarg;
			break;
		case 'r':
			ren_id = optarg;
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
		if (!ren_id || !strcmp(ren_id, renderers[i].id))
			ren = renderers[i].r;
	if (!ren) {
		if (!ren_id)
			DIE(1, "error: no PDF renderers compiled in\n");
		else {
			fprintf(stderr, "error: %s is not amongst the list of known PDF renderers:\n", ren_id);
			for (unsigned i=0; i<ARRAY_SIZE(renderers); i++)
				fprintf(stderr, "\t%s\n", renderers[i].id);
			exit(1);
		}
	}

	if (argc - optind != 2)
		DIE(1, "error: expected: VID PDF; given are %d arguments\n",
		    argc - optind);

	const char *vid_path = argv[optind++];
	const char *pdf_path = argv[optind++];

	av_register_all();

	AVFormatContext *fmt_ctx = NULL;
	AVStream *vid_stream = NULL;
	AVCodecContext *vid_ctx = NULL;
	AVCodec *vid_codec = NULL;
	int vid_stream_idx = -1;

	int      video_dst_bufsize;

	if ((r = avformat_open_input(&fmt_ctx, vid_path, NULL, NULL)) < 0)
		DIE(1, "error opening VID '%s': %s\n", vid_path, fferror(r));
	if ((r = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
		DIE(1, "error finding stream info of VID '%s': %s\n", vid_path,
		    fferror(r));
	if ((r = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
		DIE(1, "error finding %s stream in VID '%s': %s\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO), vid_path,
		    fferror(r));
	vid_stream_idx = r;
	vid_stream = fmt_ctx->streams[vid_stream_idx];
	vid_ctx    = vid_stream->codec;
	if (!(vid_codec = avcodec_find_decoder(vid_ctx->codec_id)))
		DIE(1, "error finding %s codec %s for VID '%s'\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
		    avcodec_get_name(vid_ctx->codec_id), vid_path);
	if ((r = avcodec_open2(vid_ctx, vid_codec, NULL)) < 0)
		DIE(1, "error opening %s codec for VID '%s': %s\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO), vid_path,
		    fferror(r));

	struct img_prep_args a;
	unsigned w = vid_ctx->width;
	unsigned h = vid_ctx->height;
	if ((r = av_image_alloc(a.dst_data, a.dst_stride, w, h,
	                        vid_ctx->pix_fmt, 1)) < 0)
		DIE(1, "error allocation raw video buffer: %s", fferror(r));
	video_dst_bufsize = r;
	a.sws = sws_getContext(w, h, ren->fmt,
	                       w, h, vid_ctx->pix_fmt,
	                       0, NULL, NULL, NULL);

	void *attrs[] = { "password", password, NULL, };
	void *ren_inst = ren->create(pdf_path, vid_ctx->width, vid_ctx->height, attrs);

	unsigned n_pages = ren->n_pages(ren_inst);
	for (unsigned i=0; i<n_pages; i++)
		ren->render(ren_inst, i, &a);

	av_freep(a.dst_data);
	sws_freeContext(a.sws);

	(void)video_dst_bufsize;
}

C_NAMESPACE_END
