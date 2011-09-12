#ifndef _HAVE_GA_CLIENTONLY_H
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

/* The following is a subset of ga.h.
 *  - Not all items from the header are included.
 *  - Not all included structs have all elements.
 *  - tprintf is #defined to printf rather than a thread-safe wrapper for it.
 *  - qprintf is a plain wrapper for vprintf.
 */
#if !defined(GA_segment) || !defined(GA_segment_size)
/** The type of a segment. This is expected to be some kind of
 * integer-type. The bit-width of the type must be specified in
 * GA_segment_size.
 */
#define GA_segment uint32_t
/** The size, in bits, of a segment. */
#define GA_segment_size 32
#warning Using default GA_segment definition
#endif

typedef struct GA_individual_struct {
  GA_segment *gdsegments;
  double fitness;
} GA_individual;
typedef struct { void *ref; } GA_settings;
typedef struct { GA_settings *settings; unsigned int generation; } GA_session;
typedef struct { void *ref; GA_session *session; } GA_thread;

extern int GA_fitness(const GA_session *ga, void *thbuf, GA_individual *elem);
extern int GA_thread_init(GA_thread *thread);
extern int GA_thread_free(GA_thread *thread);

#define tprintf printf
#define lprintf qprintf
int qprintf(const GA_settings *settings, const char *format, ...) {
  va_list ap;
  int rc = 0;
  va_start(ap, format);
  rc = vprintf(format, ap);
  va_end(ap);
  return rc;
}

/* From getopt */
struct option {
   const char *name;
   int         has_arg;
   int        *flag;
   int         val;
};
#define no_argument 0
#define required_argument 1
#define optional_argument 2
char *optarg;

#define _HAVE_GA_CLIENTONLY_H
#endif
