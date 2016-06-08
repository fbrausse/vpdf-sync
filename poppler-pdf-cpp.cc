
#include "common.h"
#include "renderer.h"

#include <unistd.h>		/* getopt(3) */
#include <cstring>
#include <cpp/poppler-document.h>
#include <cpp/poppler-page.h>
#include <cpp/poppler-page-renderer.h>

#if __BYTE_ORDER == __BIG_ENDIAN
# define PIX_FMT	AV_PIX_FMT_ARGB
#elif __BYTE_ORDER == __LITTLE_ENDIAN
# define PIX_FMT	AV_PIX_FMT_BGRA
#else
# error __BYTE_ORDER neither big nor litte (undefined?): cannot interpret CAIRO_FORMAT
#endif

#if __cplusplus < 201103L
# define noexcept
#endif

#define POPPLER_CPP_DPI	72.0

using namespace poppler;

struct poppler_cpp_ren {
	document *pdf;
	page_renderer r;
	unsigned w, h;
	bool keep_aspect;

	poppler_cpp_ren(document *pdf, unsigned w, unsigned h, bool keep_aspect, int hints) noexcept
	: pdf(pdf), w(w), h(h), keep_aspect(keep_aspect)
	{
		r.set_render_hints(hints);
	}
};

C_NAMESPACE_BEGIN

static int n_pages(void *_ren)
{
	poppler_cpp_ren *ren = (poppler_cpp_ren *)_ren;
	return ren->pdf->pages();
}

static void destroy(void *_ren)
{
	poppler_cpp_ren *ren = (poppler_cpp_ren *)_ren;
	delete ren->pdf;
	delete ren;
}

extern struct vpdf_ren vpdf_ren_poppler_cpp;

#define USAGE	"[-k PASSWD] [-astT] [--] PDF-PATH"

static void * create(int argc, char **argv, unsigned w, unsigned h)
{
	int hints = page_renderer::antialiasing
	          | page_renderer::text_antialiasing
	          | page_renderer::text_hinting;
	bool keep_aspect = true;
	std::string password;
	int opt;
	while ((opt = getopt(argc, argv, ":ahk:stT")) != -1)
		switch (opt) {
		case 'a': hints &= ~page_renderer::antialiasing; break;
		case 'h':
			printf("Renderer '%s' [can render: %s] usage: " USAGE "\n",
			       argv[0], vpdf_ren_poppler_cpp.can_render ? "yes" : "no");
			printf("  -a           disable graphics anti-aliasing\n");
			printf("  -k PASSWD    use PASSWD to open protected PDF [(unset)]\n");
			printf("  -s           disable preservation of aspect ratio\n");
			printf("  -t           disable text anti-aliasing\n");
			printf("  -T           disable text hinting\n");
			return NULL;
		case 'k': password = optarg; break;
		case 's': keep_aspect = false; break;
		case 't': hints &= ~page_renderer::text_antialiasing; break;
		case 'T': hints &= ~page_renderer::text_hinting; break;
		case ':':
			DIE(1, "%s error: option '-%c' required a parameter\n",
			    argv[0], optopt);
		case '?':
			DIE(1,
			    "%s error: unknown option '-%c', see '-h' for help\n",
			    argv[0], optopt);
		}

	if (argc - optind != 1)
		DIE(1, "%s renderer usage: " USAGE "\n", argv[0]);

	const char *pdf_path = argv[optind++];

	document *pdf = document::load_from_file(pdf_path, password, password);
	if (!pdf)
		DIE(1, "error opening PDF");

	return new poppler_cpp_ren(pdf, w, h, keep_aspect, hints);
}

#include "unicode-convert.h"

#ifdef HAVE_POPPLER_USTRING_FIXED
static char * ustringtomb(const ustring &us)
{
	auto s = us.to_utf8();
	if (!env_is_utf8())
		return utf8tomb(s.data(), s.size());
	char *t = (char *)malloc(s.size()+1);
	memcpy(t, s.data(), s.size());
	t[s.size()] = '\0';
	return t;
}
#elif POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 44
/* here we know ustring stores UTF16 in native byte order */
# if defined(HAVE_UCHAR_H) && defined(__STDC_UTF_16__)
#  include <uchar.h> /* <cuchar> still unknown to compilers... */
static char * ustringtomb(const ustring &us)
{
	char *t = (char *)malloc(us.size() * 2 + 1), *t2 = t;
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	for (unsigned i=0; i<us.size(); i++)
		t2 += c16rtomb(t2, us[i], &ps);
	t2 += c16rtomb(t2, u'\0', &ps);
	return t;
}
# else
static char * ustringtomb(const ustring &us)
{
	char *t = (char *)malloc(us.size() * 2 + 1), *t2 = t;
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	const uint16_t *src = us.data();
	size_t len = us.size();
	uint32_t u32;
	int l;
	while ((l = ucs2_to_ucs4(src, len, &u32)) > 0) {
		src += l;
		len -= l;
		t2 += ucs4tomb(t2, u32, &ps);
	}
	t2 += ucs4tomb(t2, '\0', &ps);
	return t;
}
# endif
#else
# error Poppler page labels unusable, use a version where bugs.freedesktop.org/show_bug.cgi?id=96313 is fixed and #define HAVE_POPPLER_USTRING_FIXED
#endif


static void render(
	void *const _ren, const int page_from, const int page_to,
	vpdf_image_prepare_f *prep,
	const struct img_prep_args *const args
) {
	const poppler_cpp_ren *const ren = (poppler_cpp_ren *)_ren;
#ifdef _OPENMP
# pragma omp parallel for
#endif
	for (int page_idx = page_from; page_idx < page_to; page_idx++) {
		page *p = ren->pdf->create_page(page_idx);
		rectf r = p->page_rect();

		double wa = ren->w / r.width(); /* horizontal scale */
		double ha = ren->h / r.height();/* vertical scale */
		if (ren->keep_aspect) {
			double a = MIN(wa, ha); /* uniform scale = min of both */
			wa = a;
			ha = a;
		}

		image pi = ren->r.render_page(p, POPPLER_CPP_DPI*wa,
		                                 POPPLER_CPP_DPI*ha,
		                              MAX(r.width() * wa, 0) / 2,
		                              MAX(r.height() * ha, 0) / 2);

		char *label = ustringtomb(p->label());

		delete p;
		struct vpdf_image img = {
			(unsigned char *)pi.const_data(),
			(unsigned)pi.width(), (unsigned)pi.height(),
			(unsigned)pi.bytes_per_row()
		};

#ifdef _OPENMP
# pragma omp critical
#endif
		prep(&img, args, page_idx, label);
	}
}

struct vpdf_ren vpdf_ren_poppler_cpp = {
	create,
	destroy,
	n_pages,
	render,
	PIX_FMT,
	page_renderer::can_render()
};

C_NAMESPACE_END
