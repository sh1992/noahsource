#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reimplement tmpfile(3) using memory buffers only */

/* Implementation is from fopencookie(3) from man-pages v3.23 */
struct memfile_cookie {
   char   *buf;        /* Dynamically sized buffer for data */
   size_t  allocated;  /* Size of buf */
   size_t  endpos;     /* Number of characters in buf */
   off_t   offset;     /* Current file offset in buf */
};

ssize_t memfile_write(void *c, const char *buf, size_t size) {
   char *new_buff;
   struct memfile_cookie *cookie = c;

   /* Buffer too small? Keep doubling size until big enough */

   while (size + cookie->offset > cookie->allocated) {
       new_buff = realloc(cookie->buf, cookie->allocated * 2);
       if (new_buff == NULL) {
           return -1;
       } else {
           cookie->allocated *= 2;
           cookie->buf = new_buff;
       }
   }

   memcpy(cookie->buf + cookie->offset, buf, size);

   cookie->offset += size;
   if (cookie->offset > cookie->endpos)
       cookie->endpos = cookie->offset;

   return size;
}

ssize_t memfile_read(void *c, char *buf, size_t size) {
   ssize_t xbytes;
   struct memfile_cookie *cookie = c;

   /* Fetch minimum of bytes requested and bytes available */

   xbytes = size;
   if (cookie->offset + size > cookie->endpos)
       xbytes = cookie->endpos - cookie->offset;
   if (xbytes < 0)     /* offset may be past endpos */
      xbytes = 0;

   memcpy(buf, cookie->buf + cookie->offset, xbytes);

   cookie->offset += xbytes;
   return xbytes;
}

int memfile_seek(void *c, off64_t *offset, int whence) {
   off64_t new_offset;
   struct memfile_cookie *cookie = c;

   if (whence == SEEK_SET)
       new_offset = *offset;
   else if (whence == SEEK_END)
       new_offset = cookie->endpos + *offset;
   else if (whence == SEEK_CUR)
       new_offset = cookie->offset + *offset;
   else
       return -1;

   if (new_offset < 0)
       return -1;

   cookie->offset = new_offset;
   *offset = new_offset;
   return 0;
}

int memfile_close(void *c) {
   struct memfile_cookie *cookie = c;

   free(cookie->buf);
   cookie->allocated = 0;
   cookie->buf = NULL;
   free(c); /* Defined in tmpfile_fopencookie, no other way to free it */

   return 0;
}

#define INIT_BUF_SIZE 512
FILE *tmpfile_fopencookie(void) {
  static const cookie_io_functions_t memfile_func = {
    .read  = memfile_read,
    .write = memfile_write,
    .seek  = memfile_seek,
    .close = memfile_close
  };
  struct memfile_cookie *mycookie;
  mycookie = malloc(sizeof(struct memfile_cookie));
  if ( !mycookie ) return NULL;
  mycookie->buf = malloc(INIT_BUF_SIZE);
  mycookie->allocated = INIT_BUF_SIZE;
  mycookie->offset = 0;
  mycookie->endpos = 0;
  return fopencookie(&mycookie,"w+",memfile_func);
}
