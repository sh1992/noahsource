#ifndef _HAVE_TMPFILE_FOPENCOOKIE_H
FILE *tmpfile_fopencookie(void);
#ifdef tmpfile
#undef tmpfile
#endif
#define tmpfile tmpfile_fopencookie
#define _HAVE_TMPFILE_FOPENCOOKIE_H
#endif
