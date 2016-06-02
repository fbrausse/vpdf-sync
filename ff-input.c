
#include "common.h"
#include "ff-input.h"

#define FF_VINPUT_API_LAVF_AVCTX	(LIBAVFORMAT_VERSION_MAJOR < 57)

char * fferror(int errnum)
{
	static char str[AV_ERROR_MAX_STRING_SIZE] = {0};
	return av_make_error_string(str, sizeof(str), errnum);
}

void ff_vinput_open(struct ff_vinput *ff, const char *vid_path)
{
	AVCodec *vid_codec;
	int r;
	int codec_id;
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
#if FF_VINPUT_API_LAVF_AVCTX
	ff->vid_ctx    = ff->vid_stream->codec;
	codec_id       = ff->vid_ctx->codec_id;
#else
	codec_id       = ff->vid_stream->codecpar->codec_id;
#endif
	if (!(vid_codec = avcodec_find_decoder(codec_id)))
		DIE(1, "error finding %s codec %s for VID '%s'\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
		    avcodec_get_name(codec_id), vid_path);
#if !FF_VINPUT_API_LAVF_AVCTX
	ff->vid_ctx = avcodec_alloc_context3(vid_codec);
	if (!ff->vid_ctx)
		DIE(1, "error allocating %s decoder context %s for VID '%s'\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
		    avcodec_get_name(codec_id), vid_path);
	if ((avcodec_parameters_to_context(ff->vid_ctx, ff->vid_stream->codecpar)))
		DIE(1, "error copying %s decoder context %s for VID '%s'\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
		    avcodec_get_name(codec_id), vid_path);
#endif
	if ((r = avcodec_open2(ff->vid_ctx, vid_codec, NULL)) < 0)
		DIE(1, "error opening %s codec for VID '%s': %s\n",
		    av_get_media_type_string(AVMEDIA_TYPE_VIDEO), vid_path,
		    fferror(r));
	av_init_packet(&ff->pkt);
}

int ff_vinput_read_frame(struct ff_vinput *ff, AVFrame *fr, int frame_idx)
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

void ff_vinput_close(struct ff_vinput *ff)
{
	av_packet_unref(&ff->pkt);
	avcodec_close(ff->vid_ctx);
#if !FF_VINPUT_API_LAVF_AVCTX
	avcodec_free_context(&ff->vid_ctx);
#endif
	avformat_close_input(&ff->fmt_ctx);
}

