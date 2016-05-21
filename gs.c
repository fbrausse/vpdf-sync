
#include "common.h"
#include "renderer.h"

C_NAMESPACE_BEGIN

#include <string.h>		/* memset(3) */
#include <inttypes.h>		/* PRIxPTR */
#include <unistd.h>		/* getopt(3) */

#include <ghostscript/iapi.h>
#include <ghostscript/ierrors.h>
#include <ghostscript/gdevdsp.h>

#define PIX_FMT		AV_PIX_FMT_RGB24

struct dsp {
	struct vpdf_image img;
	const struct img_prep_args *img_prep_args;
	unsigned page_idx;
};

static int display_open(void *handle, void *device)
{
	/*fprintf(stderr, "%s(handle=%p, device=%p)\n", __func__, handle, device);*/

	struct dsp *d = handle;
	memset(d, 0, sizeof(*d));
	return 0;
}

static int display_preclose(void *handle, void *device)
{
	/*fprintf(stderr, "%s(handle=%p, device=%p)\n", __func__, handle, device);*/
	return 0;
}

static int display_close(void *handle, void *device)
{
	/*fprintf(stderr, "%s(handle=%p, device=%p)\n", __func__, handle, device);*/
	return 0;
}

static int display_presize(void *handle, void *device, int width, int height, int raster, unsigned int format)
{
	/*fprintf(stderr, "%s(width=%d, height=%d, raster=%d, format=0x%x)\n", __func__, width, height, raster, format);*/
	return 0;
}

static int display_size(void *handle, void *device, int width, int height, int raster, unsigned int format, unsigned char *pimage)
{
	/*fprintf(stderr, "%s(width=%d, height=%d, raster=%d, format=0x%x, pimage: %p)\n", __func__, width, height, raster, format, pimage);*/

	struct dsp *d = handle;
	d->img.w = width;
	d->img.h = height;
	d->img.s = raster;
	d->img.data = pimage;
	return 0;
}

static int display_sync(void *handle, void *device)
{
	/*fprintf(stderr, "%s\n", __func__);*/
	return 0;
}

static int display_page(void *handle, void *device, int copies, int flush)
{/*
	fprintf(stderr, "%s(copies=%d, flush=%d)\n", __func__, copies, flush);
*/
	struct dsp *d = handle;
	vpdf_image_prepare(&d->img, d->img_prep_args, d->page_idx++);
	return 0;
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

struct buf {
	char *data;
	size_t valid, sz;
};

struct gs {
	void *minst;
	struct dsp d;
};

#include <stdatomic.h>

static atomic_flag gs_stdio_blocked;
static struct buf out;

static int gs_stdin(void *handle, char *buf, int len)
{
	/*fprintf(stderr, "%s(handle=%p, buf='%s', len=%d)\n", __func__, handle, buf, len);*/
	return -1;
}

static int gs_stdout(void *handle, const char *str, int len)
{
	/*fprintf(stderr, "%s(handle=%p, buf='%s', len=%d)\n", __func__, handle, str, len);*/
	if (out.sz - out.valid < len)
		out.data = realloc(out.data, out.sz = MAX(2*out.sz, len)+1);
	memcpy(out.data + out.valid, str, len+1);
	out.valid += len;
	return len;
}

static int gs_stderr(void *handle, const char *str, int len)
{
	/*fprintf(stderr, "%s(handle=%p, buf='%s', len=%d)\n", __func__, handle, str, len);*/
	return len;
}

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

	struct gs *gs = calloc(1, sizeof(struct gs));

	int r;
	if ((r = gsapi_new_instance(&gs->minst, NULL)) < 0)
		DIE(1, "error: gsapi_new_instance() failed w/ code %d\n", r);
	if ((r = gsapi_set_display_callback(gs->minst, &display_cb)) < 0)
		DIE(1, "error: gsapi_set_display_callback() failed w/ code %d\n", r);
	gsapi_set_stdio(gs->minst, gs_stdin, gs_stdout, gs_stderr);

	char display_handle[64];
	snprintf(display_handle, sizeof(display_handle),
		"-sDisplayHandle=16#%" PRIxPTR, (uintptr_t)&gs->d);

	char display_format[64];
	snprintf(display_format, sizeof(display_format), "-dDisplayFormat=16#%x",
		DISPLAY_COLORS_RGB | DISPLAY_ALPHA_NONE | DISPLAY_DEPTH_8 |
		DISPLAY_BIGENDIAN | DISPLAY_TOPFIRST |
		DISPLAY_ROW_ALIGN_DEFAULT);

	char output_dim[64];
	snprintf(output_dim, sizeof(output_dim), "-g%dx%d", w, h);

	char *gsargv[] = {
		"gs",
		"-q",
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
	if ((r = gsapi_init_with_args(gs->minst, ARRAY_SIZE(gsargv), gsargv)) < 0)
		DIE(1, "error: gsapi_init_with_args(..., %s, ...) failed w/ code %d\n",
		    display_format, r);

#define GS_CMD_PDF_BEGIN	"(%s) (r) file runpdfbegin\n"
	int user_error;
	int len = snprintf(NULL, 0, GS_CMD_PDF_BEGIN, pdf_path);
	char cmd[len+1];
	snprintf(cmd, sizeof(cmd), GS_CMD_PDF_BEGIN, pdf_path);
	if ((r = gsapi_run_string(gs->minst, cmd, 0, &user_error)) < 0)
		DIE(1, "gs-create error: gsapi_run_string(%s) failed w/ code %d\n", cmd, r);
	return gs;
}

static void render(void *_ren, int page_from, int page_to, const struct img_prep_args *img_prep_args)
{
	struct gs *gs = _ren;
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "%d %d dopdfpages\n", page_from+1, page_to);
	gs->d.img_prep_args = img_prep_args;
	int r, user_error;
	fprintf(stderr, "gs-render(%d,%d)\n", page_from, page_to);
	gs->d.page_idx = page_from;
	if ((r = gsapi_run_string(gs->minst, cmd, 0, &user_error)) < 0)
		DIE(1, "gs-render error: gsapi_run_string(%s) failed w/ code %d\n", cmd, r);
}

static void destroy(void *_ren)
{
	struct gs *gs = _ren;
	int r, user_error;
#define GS_CMD_PDF_END	"runpdfend\n"
	if ((r = gsapi_run_string_with_length(gs->minst, GS_CMD_PDF_END, sizeof(GS_CMD_PDF_END)-1, 0, &user_error)) < 0)
		DIE(1, "gs-destroy error: gs_run_string_with_length(%s) failed w/ code %d\n",
		    GS_CMD_PDF_END, r);
	if ((r = gsapi_exit(gs->minst)) < 0)
		DIE(1, "gs-destroy error: gsapi_exit() failed w/ code %d\n", r);
	gsapi_delete_instance(gs->minst);
	free(gs);
}

static int n_pages(void *_ren)
{
#define GS_CMD_PAGE_CNT	"pdfpagecount = flush\n"
	struct gs *gs = _ren;
	int user_error, r;

	while (atomic_flag_test_and_set(&gs_stdio_blocked));

	if ((r = gsapi_run_string_with_length(gs->minst, GS_CMD_PAGE_CNT, sizeof(GS_CMD_PAGE_CNT)-1, 0, &user_error)) < 0)
		DIE(1, "gs-n_pages error: gsapi_run_string_with_length(%s) failed w/ code %d\n",
		    GS_CMD_PAGE_CNT, r);
	int n_pages = atoi(out.data);
	out.valid = 0;

	atomic_flag_clear(&gs_stdio_blocked);

	return n_pages;
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
