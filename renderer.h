
#ifndef RENDERER_H
#define RENDERER_H

#include "common.h"

C_NAMESPACE_BEGIN

#include <libavutil/pixfmt.h>	/* enum PixelFormat */

/* packed 4-byte pixels; stride s */
struct vpdf_image {
	unsigned char *data;
	unsigned w, h, s;
};

struct img_prep_args;

struct vpdf_ren {
	void * (*create )(int argc, char **argv, unsigned w, unsigned h);
	void   (*destroy)(void *);
	int    (*n_pages)(void *);
	void   (*render )(void *, int page_from, int page_to, const struct img_prep_args *img_prep_args);
	enum PixelFormat fmt;
	int can_render;
};

void vpdf_image_prepare(struct vpdf_image *img, const struct img_prep_args *img_prep_args, unsigned page_idx);

C_NAMESPACE_END

#endif
