
#include "common.h"
#include "renderer.h"

C_NAMESPACE_BEGIN

#include <string.h>		/* memset(3) */
#include <inttypes.h>		/* PRIxPTR */
#include <unistd.h>		/* getopt(3) */

#include <ghostscript/iapi.h>
#include <ghostscript/gdevdsp.h>

#define PIX_FMT		PIX_FMT_ARGB

struct dsp {
	struct vpdf_image img;
	const struct img_prep_args *img_prep_args;
};

static int display_open(void *handle, void *device)
{
	fprintf(stderr, "%s(handle=%p, device=%p)\n", __func__, handle, device);

	struct dsp *d = handle;
	memset(d, 0, sizeof(*d));
	return 0;
}

static int display_preclose(void *handle, void *device)
{
	fprintf(stderr, "%s(handle=%p, device=%p)\n", __func__, handle, device);
	return 0;
}

static int display_close(void *handle, void *device)
{
	fprintf(stderr, "%s(handle=%p, device=%p)\n", __func__, handle, device);

	free(handle);
	return 0;
}

static int display_presize(void *handle, void *device, int width, int height, int raster, unsigned int format) { fprintf(stderr, "%s(width=%d, height=%d, raster=%d, format=0x%x)\n", __func__, width, height, raster, format); return 0; }

static int display_size(void *handle, void *device, int width, int height, int raster, unsigned int format, unsigned char *pimage)
{
	fprintf(stderr, "%s(width=%d, height=%d, raster=%d, format=0x%x, pimage: %p)\n", __func__, width, height, raster, format, pimage);

	struct dsp *d = handle;
	d->img.w = width;
	d->img.h = height;
	d->img.s = raster;
	d->img.data = pimage;
	return 0;
}

static int display_sync(void *handle, void *device) { fprintf(stderr, "%s\n", __func__); return 0; }

static int display_page(void *handle, void *device, int copies, int flush)
{
	fprintf(stderr, "%s(copies=%d, flush=%d)\n", __func__, copies, flush);
	return display_sync(handle, device);
}

static int display_update(void *handle, void *device, int x, int y, int w, int h)
{
	/*fprintf(stderr, "%s\n", __func__);*/
	return 0;
}

static display_callback display_cb = {
	sizeof(display_callback), DISPLAY_VERSION_MAJOR, DISPLAY_VERSION_MINOR,
	.display_open       = display_open,
	.display_preclose   = display_preclose,
	.display_close      = display_close,
	.display_presize    = display_presize,
	.display_size       = display_size,
	.display_sync       = display_sync,
	.display_page       = display_page,
	.display_update     = display_update,
	.display_memalloc   = NULL,
	.display_memfree    = NULL,
	.display_separation = NULL,
};

extern struct vpdf_ren vpdf_ren_gs;

static void * create(int argc, char **argv, unsigned w, unsigned h)
{
	int opt;
	while ((opt = getopt(argc, argv, ":h")) != -1)
		switch (opt) {
		case 'h':
			printf("Renderer '%s' [can render: %s] usage: [--] PDF-PATH\n",
			       argv[0], vpdf_ren_gs.can_render ? "yes" : "no");
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
		DIE(1, "%s renderer usage: PDF\n", argv[0]);

	const char *pdf_path = argv[optind++];

	void *minst;
	int r;
	if ((r = gsapi_new_instance(&minst, NULL)) < 0)
		DIE(1, "error: gsapi_new_instance() failed w/ code %d\n", r);
//	gsapi_set_stdio(minst, gsdll_stdin, gsdll_stdout, gsdll_stderr);
	if ((r = gsapi_set_display_callback(minst, &display_cb)) < 0)
		DIE(1, "error: gsapi_set_display_callback() failed w/ code %d\n", r);

	struct dsp *d = malloc(sizeof(struct dsp));

	char display_handle[64];
	snprintf(display_handle, sizeof(display_handle),
		"-sDisplayHandle=16#%" PRIxPTR, (uintptr_t)d);

	char display_format[64];
	snprintf(display_format, sizeof(display_format), "-dDisplayFormat=16#%x",
		DISPLAY_COLORS_RGB | DISPLAY_ALPHA_NONE | DISPLAY_DEPTH_8 |
		DISPLAY_LITTLEENDIAN | DISPLAY_TOPFIRST |
		DISPLAY_ROW_ALIGN_DEFAULT);

	char output_dim[64];
	snprintf(output_dim, sizeof(output_dim), "-g%dx%d", w, h);

	char *gsargv[] = {
		"ps2pdf",
		"-dBATCH",
		"-dNOPAUSE",
		"-sDEVICE=display",
//		"-dSAFER",
//		"-dNumRenderingThreads=4",
		"-dFitPage",
		"-dTextAlphaBits=4",
		"-dGraphicsBits=4",
		output_dim,
		display_handle,
		display_format,/*
		"-f",
		pdf_path,
		"-c",
		"quit",*/
	};
	if ((r = gsapi_init_with_args(minst, ARRAY_SIZE(gsargv), gsargv)) < 0)
		DIE(1, "error: gsapi_init_with_args(..., %s, ...) failed w/ code %d\n", display_format, r);

	int user_error;
	char buf[strlen(pdf_path)+4];
	snprintf(buf, sizeof(buf), "(%s)\n", pdf_path);
	if ((r = gsapi_run_string(minst, buf, 0, &user_error)) < 0)
		DIE(1, "gs-create error: gsapi_run_string(%s) failed w/ code %d\n", buf, r);
	return minst;
}

static void render(void *_ren, int page_from, int page_to, const struct img_prep_args *img_prep_args) {}

static void destroy(void *_ren)
{
	void *minst = _ren;
	int r;
	if ((r = gsapi_exit(minst)) < 0)
		DIE(1, "error: gsapi_exit() failed w/ code %d\n", r);
	gsapi_delete_instance(minst);
}

static int n_pages(void *_ren)
{
	void *minst = _ren;
#define GS_PAGE_CNT_CMD	"dup (r) file runpdfbegin pdfpagecount =\n"
	int user_error, r;
	if ((r = gsapi_run_string(minst, GS_PAGE_CNT_CMD, 0, &user_error)) < 0)
		DIE(1, "gs-n_pages error: gsapi_run_string(%s) failed w/ code %d\n",
		    GS_PAGE_CNT_CMD, r);

	return 0;
}

struct vpdf_ren vpdf_ren_gs = {
	create,
	destroy,
	n_pages,
	render,
	PIX_FMT,
	1,
};

C_NAMESPACE_END
