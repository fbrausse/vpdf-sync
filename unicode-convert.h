
#ifndef UNICODE_CONVERT_H
#define UNICODE_CONVERT_H

#include "common.h"

C_NAMESPACE_BEGIN

#include <inttypes.h>	/* uint*_t */
#include <wchar.h>	/* mbstate_t */

#if (__STDC_VERSION__ + 0) >= 201112L || (__cplusplus + 0) >= 201103L
# define HAVE_UCHAR_H	1
#endif
#if defined(__STDC_ISO_10646__)
# define VPDF_SYNC_WCS	1
#endif

#if defined(__APPLE__) && defined(__MACH__) && defined(__APPLE_CC__)
# undef HAVE_UCHAR_H /* doesn't exist on OS X up to 10.11 */
# define VPDF_SYNC_WCS	1
#endif

int utf8_to_ucs4(const char *utf8, size_t ulen, uint32_t *r);
int ucs2_to_ucs4(const uint16_t *u, size_t ulen, uint32_t *r);
size_t ucs4tomb(char *s, uint32_t c, mbstate_t *ps);
char * utf8tomb(const char *utf8, size_t ulen);
int env_is_utf8(void);

C_NAMESPACE_END

#endif
