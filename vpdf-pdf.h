
#ifndef VPDF_PDF_H
#define VPDF_PDF_H

#include "common.h"

C_NAMESPACE_BEGIN

#include <libavutil/pixfmt.h>	/* enum PixelFormat */

/* packed 4-byte pixels; stride s */
struct vpdf_image {
	unsigned char *data;
	unsigned w, h, s;
};

struct vpdf_ren {
	void * (*create )(const char *pdf_path, unsigned w, unsigned h, void **attrs);
	void   (*destroy)(void *);
	int    (*n_pages)(void *);
	void   (*render )(void *, int page_idx, const void *img_prep_args);
	enum PixelFormat fmt;
};

void vpdf_image_prepare(struct vpdf_image *img, const void *img_prep_args);

C_NAMESPACE_END

#endif
