
#include "unicode-convert.h"

#include <langinfo.h>		/* nl_langinfo(3) */
#include <locale.h>
#include <string.h>		/* strstr(3), memset(3) */

int utf8_to_ucs4(const char *utf8, size_t ulen, uint32_t *r)
{
	int ret = 0;
	uint8_t c, d, e, f;
	if (!ulen)
		return 0;
	c = (uint8_t)*utf8++;
	if (~c & 0x80) {
		*r = c;
		ret = 1;
	} else if (ulen > 1 && (c & 0xe0) == 0xc0) {
		d = (uint8_t)*utf8++;
		*r = (c & ~0xe0) << 6 | (d & 0x3f);
		ret = 2;
	} else if (ulen > 2 && (c & 0xf0) == 0xe0) {
		d = (uint8_t)*utf8++;
		e = (uint8_t)*utf8++;
		*r = (c & ~0xf0) << 12 | (d & 0x3f) << 6 | (e & 0x3f);
		ret = 3;
	} else if (ulen > 3 && (c & 0xf8) == 0xf0) {
		d = (uint8_t)*utf8++;
		e = (uint8_t)*utf8++;
		f = (uint8_t)*utf8++;
		*r = (c & ~0xf8) << 18 | (d & 0x3f) << 12 | (e & 0x3f) << 6 | (f & 0x3f);
		ret = 4;
	} else
		return -1;
	return ret;
}

int ucs2_to_ucs4(const uint16_t *u, size_t ulen, uint32_t *r)
{
	if (!ulen)
		return 0;
	uint16_t a = *u++;
	if ((a & 0xf800) != 0xd800) {
		*r = a;
		return 1;
	} else if (ulen > 1) {
		*r = ((uint32_t)(a & 0x3ff) << 10 | (*u++ & 0x3ff)) + 0x10000;
		return 2;
	} else
		return -1;
}

#ifdef VPDF_SYNC_WCS
# include <wchar.h>
size_t ucs4tomb(char *s, uint32_t c, mbstate_t *ps) { return wcrtomb(s, (wchar_t)c, ps); }
#elif defined(HAVE_UCHAR_H) && (defined(__STDC_UTF_32__) || defined(__STDC_UTF_16__))
# include <uchar.h>
# if defined(__STDC_UTF_32__)
size_t ucs4tomb(char *s, uint32_t c, mbstate_t *ps) { return c32rtomb(s, (char32_t)c, ps); }
# elif defined(__STDC_UTF_16__)
size_t ucs4tomb(char *s, uint32_t c, mbstate_t *ps)
{
	if (!(c >> 16))
		return c16rtomb(s, c & 0xffff, ps);
	size_t p, q;
	uint16_t a, b;
	c -= 0x10000;
	assert(!(c >> 16));
	a = 0xd800 | (c >> 10) & 0x3ff;
	b = 0xdc00 | (c      ) & 0x3ff;
	p = c16rtomb(s, a, ps);
	if (p == (size_t)-1)
		return p;
	s += p;
	q = c16rtomb(s, b, ps);
	if (q == (size_t)-1)
		return q;
	return p+q;
}
# else
#  error internal header logic error
# endif
#else
# error C compiler does not support Unicode
#endif

char * utf8tomb(const char *utf8, size_t ulen)
{
	char *r = (char *)malloc(MB_CUR_MAX * ulen + 1);
	uint32_t c;
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	char *s;
	int l;
	for (s = r; (l = utf8_to_ucs4(utf8, ulen, &c)) > 0;) {
		utf8 += l;
		ulen -= l;
		s += ucs4tomb(s, c, &ps);
	}
	s += ucs4tomb(s, '\0', &ps);
	return r;
}

int env_is_utf8(void)
{
	char *codeset = nl_langinfo(CODESET);
	if (!codeset || !*codeset)
		codeset = setlocale(LC_CTYPE, NULL);
	if (!codeset || !*codeset)
		codeset = setlocale(LC_ALL, NULL);
	if (!codeset || !*codeset)
		return 0;
	return !strstr(codeset, "UTF-8") || !strstr(codeset, "utf8");
}

