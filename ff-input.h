
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
	int vid_stream_idx;
	int end_of_stream, got_frame;
	AVPacket pkt;
};

#define FF_VINPUT_INIT	{ NULL, NULL, NULL, -1, 0, 0 }

char * fferror(int errnum);
void ff_vinput_open(struct ff_vinput *ff, const char *vid_path);
int ff_vinput_read_frame(struct ff_vinput *ff, AVFrame *fr, int frame_idx);
void ff_vinput_close(struct ff_vinput *ff);

C_NAMESPACE_END

#endif
