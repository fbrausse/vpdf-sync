
#include "common.h"
#include "vpdf-pdf.h"

C_NAMESPACE_BEGIN

#include <stdlib.h>
#include <string.h>
#include <glib/poppler.h>

#define FMT	CAIRO_FORMAT_RGB24 /* 0x00rrggbb */

#if __BYTE_ORDER == __BIG_ENDIAN
# define PIX_FMT	PIX_FMT_ARGB
#elif __BYTE_ORDER == __LITTLE_ENDIAN
# define PIX_FMT	PIX_FMT_BGRA
#else
# error __BYTE_ORDER neither big nor litte (undefined?): cannot interpret CAIRO_FORMAT
#endif

struct poppler_glib_ren {
	PopplerDocument *pdf;
	cairo_surface_t *sf;
	cairo_t         *cr;
};

static void render(void *_ren, int page_idx, const void *img_prep_args)
{
	struct poppler_glib_ren *ren = _ren;

	struct vpdf_image img;
	img.data = cairo_image_surface_get_data(ren->sf);
	img.w = cairo_image_surface_get_width(ren->sf);
	img.h = cairo_image_surface_get_height(ren->sf);
	img.s = cairo_image_surface_get_stride(ren->sf);

	double sw, sh;
	PopplerPage *page = poppler_document_get_page(ren->pdf, page_idx);
	poppler_page_get_size(page, &sw, &sh);

	double wa = img.w / sw;     /* horizontal scale */
	double ha = img.h / sh;     /* vertical scale */
	double a = MIN(wa, ha); /* uniform scale = min of both */

	cairo_save(ren->cr);
	/* center the rendering on img */
	cairo_translate(ren->cr, MAX(img.w - sw*a, 0) / 2, MAX(img.h - sh*a, 0) / 2);
	/* scale to match img size */
	cairo_scale(ren->cr, a, a);

	poppler_page_render(page, ren->cr);

	cairo_restore(ren->cr);
	cairo_surface_flush(ren->sf);

	g_object_unref(page);

	vpdf_image_prepare(&img, img_prep_args);
}

static void * create(const char *pdf_path, unsigned w, unsigned h, void **attrs)
{
	const char *password = NULL;
	if (attrs)
		for (; *attrs; attrs += 2) {
			if (!strcmp(attrs[0], "password"))
				password = attrs[1];
		}

	GError *err = NULL;
	PopplerDocument *pdf = poppler_document_new_from_file(pdf_path,
	                                                      password,
	                                                      &err);
	if (!pdf)
		DIE(err->code, "error opening PDF '%s': %s\n",
		    pdf_path, err->message);

	struct poppler_glib_ren *ren = malloc(sizeof(struct poppler_glib_ren));
	ren->pdf = pdf;
	ren->sf = cairo_image_surface_create(FMT, w, h);
	ren->cr = cairo_create(ren->sf);
	return ren;
}

static void destroy(void *_ren)
{
	struct poppler_glib_ren *ren = _ren;
	cairo_destroy(ren->cr);
	cairo_surface_destroy(ren->sf);
	g_object_unref(ren->pdf);
	free(ren);
}

static int n_pages(void *_ren)
{
	struct poppler_glib_ren *ren = _ren;
	return poppler_document_get_n_pages(ren->pdf);
}

struct vpdf_ren vpdf_ren_poppler_glib = {
	create,
	destroy,
	n_pages,
	render,
	PIX_FMT,
};

C_NAMESPACE_END
