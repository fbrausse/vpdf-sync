
#define _XOPEN_SOURCE	500	/* realpath(3) */

#include "common.h"
#include "renderer.h"

C_NAMESPACE_BEGIN

#include <string.h>
#include <errno.h>
#include <unistd.h>		/* getopt(3) */
#include <glib/poppler.h>
#include <glib/poppler-action.h>

#define FMT	CAIRO_FORMAT_RGB24 /* 0x00rrggbb */

#if __BYTE_ORDER == __BIG_ENDIAN
# define PIX_FMT	AV_PIX_FMT_ARGB
#elif __BYTE_ORDER == __LITTLE_ENDIAN
# define PIX_FMT	AV_PIX_FMT_BGRA
#else
# error __BYTE_ORDER neither big nor litte (undefined?): cannot interpret CAIRO_FORMAT
#endif

struct poppler_glib_ren {
	PopplerDocument *pdf;
	cairo_surface_t *sf;
	cairo_t         *cr;
};

static void render(void *_ren, int page_from, int page_to, const struct img_prep_args *img_prep_args)
{
	struct poppler_glib_ren *ren = (struct poppler_glib_ren *)_ren;

	struct vpdf_image img;
	img.data = cairo_image_surface_get_data(ren->sf);
	img.w = cairo_image_surface_get_width(ren->sf);
	img.h = cairo_image_surface_get_height(ren->sf);
	img.s = cairo_image_surface_get_stride(ren->sf);

	for (int page_idx = page_from; page_idx < page_to; page_idx++) {
		double sw, sh;
		PopplerPage *page = poppler_document_get_page(ren->pdf, page_idx);
		poppler_page_get_size(page, &sw, &sh);

		double wa = img.w / sw; /* horizontal scale */
		double ha = img.h / sh; /* vertical scale */
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

		vpdf_image_prepare(&img, img_prep_args, page_idx, poppler_page_get_label(page));
	}
}

static void print_iter(PopplerDocument *pdf, PopplerIndexIter *it, int lvl)
{
	if (it) {
		do {
			PopplerAction *ac = poppler_index_iter_get_action(it);
			char buf[512] = {0};
			char *type = NULL;
			switch (ac->type) {
			case POPPLER_ACTION_UNKNOWN: type = "unknown"; break;
			case POPPLER_ACTION_NONE   : type = "none"; break;
			case POPPLER_ACTION_GOTO_DEST:
				type = "goto-dest";
				PopplerActionGotoDest *d = (PopplerActionGotoDest *)ac;
				sprintf(buf, ": %s -> %d",
				        d->title,
				        poppler_document_find_dest(pdf,
				                                   d->dest->named_dest
				                                  )->page_num);
				break;
			case POPPLER_ACTION_GOTO_REMOTE: type = "goto-remote"; break;
			case POPPLER_ACTION_LAUNCH: type = "launch"; break;
			case POPPLER_ACTION_URI: type = "uri"; break;
			case POPPLER_ACTION_NAMED: type = "named"; break;
			case POPPLER_ACTION_MOVIE: type = "movie"; break;
			case POPPLER_ACTION_RENDITION: type = "rendition"; break;
			case POPPLER_ACTION_OCG_STATE: type = "ocg-state"; break;
			case POPPLER_ACTION_JAVASCRIPT: type = "javascript"; break;
			}
			fprintf(stderr, "%*s%s%s\n", lvl, "", type, buf);
			print_iter(pdf, poppler_index_iter_get_child(it), lvl+1);
		} while (poppler_index_iter_next(it));
		poppler_index_iter_free(it);
	}
}

extern struct vpdf_ren vpdf_ren_poppler_glib;

static void * create(int argc, char **argv, unsigned w, unsigned h)
{
	const char *password = NULL;
	int opt;
	while ((opt = getopt(argc, argv, ":hk:")) != -1)
		switch (opt) {
		case 'k': password = optarg; break;
		case 'h':
			printf("Renderer '%s' [can render: %s] usage: [-k PASSWD] [--] PDF-PATH\n",
			       argv[0], vpdf_ren_poppler_glib.can_render ? "yes" : "no");
			printf("  -k PASSWD    use PASSWD to open protected PDF [(unset)]\n");
			return NULL;
		case ':':
			DIE(1, "%s error: option '-%c' required a parameter\n",
			    argv[0], optopt);
		case '?':
			DIE(1,
			    "%s error: unknown option '-%c', see '-h' for help\n",
			    argv[0], optopt);
		}

	if (argc - optind != 1)
		DIE(1, "%s renderer usage: [-k PASSWD] PDF\n", argv[0]);

	const char *pdf_path = argv[optind++];

	char path[PATH_MAX+7] = "file://";
	if (!realpath(pdf_path, path+7))
		DIE(1, "error resolving PDF path '%s': %s\n",
		    pdf_path, strerror(errno));

	GError *err = NULL;
	PopplerDocument *pdf = poppler_document_new_from_file(path,
	                                                      password,
	                                                      &err);
	if (!pdf)
		DIE(err->code, "error opening PDF '%s': %s\n",
		    pdf_path, err->message);

	print_iter(pdf, poppler_index_iter_new(pdf), 0);

	struct poppler_glib_ren *ren = (struct poppler_glib_ren *)malloc(sizeof(struct poppler_glib_ren));
	ren->pdf = pdf;
	ren->sf = cairo_image_surface_create(FMT, w, h);
	ren->cr = cairo_create(ren->sf);
	return ren;
}

static void destroy(void *_ren)
{
	struct poppler_glib_ren *ren = (struct poppler_glib_ren *)_ren;
	cairo_destroy(ren->cr);
	cairo_surface_destroy(ren->sf);
	g_object_unref(ren->pdf);
	free(ren);
}

static int n_pages(void *_ren)
{
	struct poppler_glib_ren *ren = (struct poppler_glib_ren *)_ren;
	return poppler_document_get_n_pages(ren->pdf);
}

struct vpdf_ren vpdf_ren_poppler_glib = {
	create,
	destroy,
	n_pages,
	render,
	PIX_FMT,
	1,
};

C_NAMESPACE_END
