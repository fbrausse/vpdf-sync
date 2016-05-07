
#include <stdlib.h>	/* exit(3) */
#include <stdio.h>	/* *printf(3) */
#include <unistd.h>	/* getopt(3) */

#include <libavformat/avformat.h>	/* avformat_*() */
#include <libavutil/imgutils.h>		/* av_image_alloc() */

#include <glib/poppler.h>

#define DIE(code,...) do { fprintf(stderr, __VA_ARGS__); exit(code); } while (0)

static void usage(const char *progname)
{
	printf("usage: %s [-OPTS] [--] PDF VIDEO\n", progname);
	printf("\n");
	printf("\
Options:\n\
  -h          display this help message\n\
  -p          password to unlock PDF\n\
	");
}

int main(int argc, char **argv)
{
	char *password = NULL;

	int opt;
	while ((opt = getopt(argc, argv, ":h")) != -1)
		switch (opt) {
		case 'h':
			usage(argv[0]);
			exit(0);
		case 'p':
			password = optarg;
			break;
		case ':':
			DIE(1, "error: option '-%c' required a parameter\n",
			    optopt);
		case '?':
			DIE(1,
			    "error: unknown option '-%c', see '-h' for help\n",
			    optopt);
		}

	if (argc - optind != 2)
		DIE(1, "error: expected: PDF VIDEO; given are %d arguments\n",
		    argc - optind);

	const char *pdf_path = argv[optind++];
	const char *vid_path = argv[optind++];
	int r;

	av_register_all();

	AVFormatContext *fmt_ctx;
	AVStream *vid_stream;
	AVCodecContext *vid_ctx;
	AVCodec *vid_codec;
	int vid_stream_idx = -1;

	uint8_t *video_dst_data[4] = {NULL};
	int      video_dst_linesize[4];
	int      video_dst_bufsize;

	if ((r = avformat_open_input(&fmt_ctx, vid_path, NULL, NULL)) < 0)
		DIE(1, "error opening VID '%s': %s\n", vid_path, av_err2str(r));
	if ((r = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
		DIE(1, "error finding stream info of VID '%s': %s\n", vid_path,
		    av_err2str(r));
	if ((r = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
		DIE(1, "error finding %s stream in VID '%s': %s\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO), vid_path,
		    av_err2str(r));
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
		    av_err2str(r));
	if ((r = av_image_alloc(video_dst_data, video_dst_linesize,
	                        vid_ctx->width, vid_ctx->height,
	                        vid_ctx->pix_fmt, 1)) < 0)
		DIE(1, "error allocation raw video buffer: %s", av_err2str(r));
	video_dst_bufsize = r;


	GError *err = NULL;
	PopplerDocument *pdf = poppler_document_new_from_file(pdf_path,
	                                                      password,
	                                                      &err);
	if (!pdf)
		DIE(err->code, "error opening PDF '%s': %s\n",
		    pdf_path, err->message);

	int pdf_n_pages = poppler_document_get_n_pages(pdf);
}
