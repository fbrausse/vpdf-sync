
#ifndef FF_INPUT_H
#define FF_INPUT_H

#include "common.h"

C_NAMESPACE_BEGIN

#include <libavformat/avformat.h>	/* avformat_*() */
#include <libavutil/imgutils.h>		/* av_image_alloc() */

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

static inline char * fferror(int errnum)
{
	static char str[AV_ERROR_MAX_STRING_SIZE] = {0};
	return av_make_error_string(str, sizeof(str), errnum);
}

static inline void ff_vinput_open(struct ff_vinput *ff, const char *vid_path)
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

static inline int ff_vinput_read_frame(struct ff_vinput *ff, AVFrame *fr, int frame_idx)
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

static inline void ff_vinput_close(struct ff_vinput *ff)
{
	av_packet_unref(&ff->pkt);
	avcodec_close(ff->vid_ctx);
	avformat_close_input(&ff->fmt_ctx);
}

C_NAMESPACE_END

#endif
