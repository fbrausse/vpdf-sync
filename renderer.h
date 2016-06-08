
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

typedef void vpdf_image_prepare_f(
	struct vpdf_image *img, const struct img_prep_args *args,
	unsigned page_idx, char *label
);

struct vpdf_ren {
	void * (*create )(int argc, char **argv, unsigned w, unsigned h);
	void   (*destroy)(void *);
	int    (*n_pages)(void *);
	void   (*render )(void *, int page_from, int page_to,
	                  vpdf_image_prepare_f *prep,
	                  const struct img_prep_args *args);
	enum AVPixelFormat fmt;
	int can_render;
};


C_NAMESPACE_END

#endif
