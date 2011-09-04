/** \file ga-spectroscopy.c
 *
 * Solve the spectroscopy problem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include "ga-spectroscopy.checksum.h"

#if THREADS
#include <pthread.h>
#endif

#ifdef CLIENT_ONLY
#include "ga-clientonly.h"
#else
#include "ga.h"
#include "ga-spectroscopy.usage.h"
#endif

#ifdef USE_SPCAT_OBJ
#include "spcat-obj/spcat-obj.h"
#endif

#ifdef _WIN32
#include <windows.h> /* We use CreateProcess to replace invisible_system */
#else
#include <sys/socket.h>
#include <netdb.h>
#include <sys/wait.h>
int invisible_system(int stdoutfd, int argc, ...);
#endif

#ifndef O_NOFOLLOW
/* If O_NOFOLLOW is unsupported, we probably don't support symlinks anyway. */
#define O_NOFOLLOW 0
#endif

#define BINS 600		/* Number of bins to use for comparison */
#define RANGEMIN 8700		/* Ignore below this frequency */
#define RANGEMAX 18300		/* Ignore above this frequency */

#define SEGMENTS 8		/* See also fprintf in generate_input_files */

/* char tempdir[16]; */

/* Use same order as spcat_ext and spcat_efile in spcat-obj.h */
const char input_suffixes[2][4] = {"int", "var"};
const char output_suffixes[2][4] = {"out", "cat"};
#define QN_COUNT 2
#define QN_DIGITS 3
/** One row of a .CAT file */
typedef struct {
  float frequency, error, intensity;
  int qn[QN_COUNT*QN_DIGITS];
} datarow;
typedef struct {
  int qn[QN_COUNT*QN_DIGITS];
  int seen;
} dblres_check;
typedef struct {
  int npeaks;
  float *peaks;
} dblres_relation;
/** Thread-local data */
typedef struct {
  datarow *compdata;		/* Data storage for comparison */
  int compdatasize, compdatacount;
#ifndef USE_SPCAT_OBJ
  char *basename_temp;		/* Basename of temporary file */
#endif
  dblres_check *drlist;
  int drsize;
} specthreadopts_t;
/** Settings */
typedef struct {
  char *basename_out;		/* Basename of output file */
  char *template_fn;		/* Filename of template file */
  char *obsfile;		/* Observation file */
  char *spcatbin;		/* SPCAT program file */
  unsigned int bins;		/* Number of bins */
  float distanceweight;
  int stdoutfd, devnullfd;	/* File descriptors used to hide SPCAT output */
  datarow *observation;		/* Data storage for observation */
  int observationsize, observationcount;
  char template[2][2048];	/* Input file template */
  int userange[SEGMENTS];
  int rangetemp[SEGMENTS];
  GA_segment rangemin[SEGMENTS];
  GA_segment rangemax[SEGMENTS];
  char *popfile;
  GA_segment *popdata;
  char *drfile;
  unsigned int doublereslen;
  dblres_relation *doubleres;
  float doublerestol;
  unsigned obsrangemin, obsrangemax; /* Ignore outside these frequencies */
  int linkbc;
  char *distributor;
  char *tempdir;
} specopts_t;

#ifndef USE_SPCAT_OBJ
char *make_spec_temp(char *dir) {
  int i = 0;
  int touched = 0;
  int fnsize = 0;
  char *basename, *filename;
  basename = malloc(fnsize = strlen(dir)+100);
  if ( !basename ) return NULL;
  filename = malloc(fnsize = strlen(dir)+100);
  if ( !filename ) return NULL;
  /* Try to find temporary filename that does not already exist */
  while ( touched < 4 ) {
    int j;
    touched = 0;
    i++;
    snprintf(basename, fnsize, "%s/tmp-gaspec-%d-%d", dir, getpid(),i);
    for ( j = 0; j < 2; j++ ) {
      int rc;
      int k;
      for ( k = 0; k < 2; k++ ) {
	snprintf(filename, fnsize, "%s.%s", basename,
		 k == 0 ? input_suffixes[j] : output_suffixes[j]);
	rc = open(filename,
		  O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY,S_IRUSR|S_IWUSR);
	if ( rc < 0 ) break;
	touched++;
	close(rc);
      }
      if ( rc < 0 ) break;
    }
  }
  free(filename);
  return basename;
}
#endif

int load_spec_templates(specopts_t *opts) {
  int i;
  char *filename;
  if ( ( filename = malloc(strlen(opts->template_fn)+5) ) == NULL ) {
    return 50;
  }
  memset(opts->template, 0, sizeof(opts->template));
  for ( i = 0; i < 2; i++ ) {
    FILE *fh;
    sprintf(filename, "%s.%s", opts->template_fn, input_suffixes[i]);
    /* Load template */
    if ( ( fh = fopen(filename, "r") ) == NULL ) {
      printf("Failed to open template file, errno=%d\n", errno);
      free(filename);
      return i+10;
    }
    fread(opts->template[i], sizeof(opts->template[i]), 1, fh);
    if ( ferror(fh) ) {
      printf("Failed to read template file, errno=%d\n", errno);
      free(filename);
      return i+20;
    }
    fclose(fh);
  }
  free(filename);
  return 0;
}

/** Parse Quantum Numbers from SPCAT .CAT files (See spinv.pdf, note
 * that documentation incorrectly claims valid range is -259..359)
 *
 * \returns Quantum number in range -269..359, 9999 to denote
 *     overflow ("**"), -9999 to denote parse error.
 */
int parseqn(const char *str) {
  int val;
  //printf("'%s'\n",str);
  if ( str[0] == '*' || str[1] == '*' ) return 9999; // >359 or <-269
  if ( !isdigit(str[1]) ) return -9999;
  val = str[1]-'0';
  if ( isspace(str[0]) ) ; // Do nothing
  else if ( isdigit(str[0]) ) val += 10*(str[0]-'0');
  else if ( isalpha(str[0]) ) {
    if ( islower(str[0]) ) val = -val-10*(str[0]-'a'+1);
    else val = val+10*(str[0]-'A')+100;
  }
  else if ( str[0] == '-' ) val = -val;
  else return -9999;
  return val;
}

int load_catfile(FILE *fh, datarow **storage, int *size, int *count) {
/*
  float a, b, maxb = 0;
  char trailing[1024];
*/
  float maxlgint = 0;
  int i = 0, j = 0;
  while ( 1 ) {
    float freq, err, lgint;
    struct {
      /* This struct stores the split fields of the cat file. */
      char freq[14], err[9], lgint[9], dr[3], elo[11], gup[4], tag[8],
           qnfmt[5], qn[128];
    } s;
    /* Using a struct makes it very easy to zero out. */
    memset(&s, 0, sizeof(s));

    if ( !fh ) break; /* Empty memfile wasn't opened */

    /* Kill newlines characters */
    fscanf(fh, "%*[\r\n]");

    /* Format of lines of .CAT file is described in spinv.pdf */
    /* QN is only 24 characters, but also read any extra trailing data. */
    i = fscanf(fh, "%13c%8c%8c%2c%10c%3c%7c%4c%127[^\r\n]", s.freq, s.err,
               s.lgint, s.dr, s.elo, s.gup, s.tag, s.qnfmt, s.qn);
    if ( i == EOF && ferror(fh) ) {
      printf("Failed to read template file, errno=%d\n", errno);
      return 3;
    }
    else if ( i == EOF ) break;
    else if ( i != 9 ) {
      printf("File format error, fscanf errno=%d\n", errno);
      return 4;
    }

    /* Convert string values */
    errno = 0;
    if ( errno == 0 ) freq  = strtof(s.freq,  NULL);
    if ( errno == 0 ) err   = strtof(s.err,   NULL);
    if ( errno == 0 ) lgint = strtof(s.lgint, NULL);
    if ( errno != 0 ) {
      printf("File format error, strtof errno=%d\n", errno);
      return 4;
    }

    /* Allocate additional memory, if neccessary */
    if ( *count >= *size ) {
      *size = ((*size)+1)*2;
      //printf("Increasing memory allocation to %u\n", thrs->compdatasize);
      *storage = realloc(*storage, sizeof(datarow)*(*size));
      if ( *storage == NULL ) {
	printf("Out of memory, errno=%d\n", errno);
	return 5;
      }
    }

    /* Store frequency, intensity values */
    if ( *count == 0 || lgint > maxlgint ) maxlgint = lgint;
    (*storage)[*count].frequency = freq;
    (*storage)[*count].error = err;
    /* Exponentiation occurs during scaling, below */
    (*storage)[*count].intensity = lgint;

    /* Double resonance from trailing data */
    /* Ensure quantum numbers are of type 303 */
    /* PROGRAM DOES NOT SUPPORT VARIABLE QUANTUM NUMBER TYPES */
    if ( strcmp(s.qnfmt, " 303") != 0 ) {
      printf("CAT file does not has QNFMT %s, not 303.\n", s.qnfmt);
      return 4;
    }
    for ( i = 0; i < 2; i++ ) {
      for ( j = 0; j < 3; j++ ) {
        char str[3]; int val;
        str[0] = s.qn[12*i+j*2];
        str[1] = s.qn[12*i+j*2+1];
        str[2] = 0;
        val = parseqn(str);
        //printf("'%s'\n",trailing);
        if ( val == -9999 ) return 6;
        //printf(" %d", val);
        (*storage)[*count].qn[i*3+j] = val;
      }
    }
    //printf("\n");
    /* Next item */
    (*count)++;
  }
  //printf("MAXB %g",maxb);
  for ( i=0; i < *count; i++ ) {
    //printf("POW 10^%f => ", (*storage)[i].b);
    (*storage)[i].intensity = powf(10,(*storage)[i].intensity-maxlgint);
    //printf("%g\n", (*storage)[i].b);
  }
  return 0;
}

int load_spec_observation(specopts_t *opts) {
  FILE *fh;
  int rc = 0;
  if ( ( fh = fopen(opts->obsfile, "r") ) == NULL ) {
    printf("Failed to open observation file, errno=%d\n", errno);
    return 12;
  }
  rc = load_catfile(fh, &(opts->observation), &(opts->observationsize),
                    &(opts->observationcount));
  if ( rc > 0 ) rc += 10;
  fclose(fh);
  return 0;
}

#ifdef USE_SPCAT_OBJ
int generate_input_buffers(specopts_t *opts, char *buffers[], size_t bufsizes[], GA_segment *x) {
  int i;
  for ( i = 0; i < 2; i++ ) {
    if ( ( bufsizes[i] = asprintf(&buffers[i], opts->template[i], x[0], x[1], x[2]) ) < 0 )
      return i+1;
  }
  return 0;
}
#else
int generate_input_files(specopts_t *opts, char *basename, GA_segment *x) {
  char filename[128];
  int i;
  for ( i = 0; i < 2; i++ ) {
    FILE *fh;
    int j;
    char djkstr[5][16];
    snprintf(filename, sizeof(filename), "%s.%s", basename,
	     input_suffixes[i]);
    if ( ( fh = fopen(filename, "w") ) == NULL ) {
      printf("Failed to open input file, errno=%d\n", errno);
      return i+1;
    }
    // 
    for ( j = 0; j < 5; j++ ) {
      GA_segment v = x[3+j];
      const int zero = ~(1<<(GA_segment_size-1)); // (0xfff...fff)/2
      sprintf(djkstr[j], "%s%u", (v>0)?"-":"",
              (v > zero) ? (v-zero) : (zero-v));
    }
    fprintf(fh, opts->template[i], x[0] /* A */,
            opts->linkbc?((x[1]+x[2])/2):x[1] /* B */,
            opts->linkbc?((x[1]-x[2])/2):x[2] /* C */,
            djkstr[0], djkstr[1], djkstr[2], djkstr[3], djkstr[4]
           ); /* FIXME */
    //B+C/B-C
    //fprintf(fh, opts->template[i], x[0], (x[1]+x[2])/2, (x[1]-x[2])/2); /* FIXME */
    /* Error checking for fprintf? */
    if ( fclose(fh) ) {
      printf("Failed to close input file, errno=%d\n", errno);
      return i+1;
    }
  }
  return 0;
}
#endif

/* getline is a GNU extension. It reads a line of text, malloc/reallocing the
 * output buffer as necessary. Implement it using fgets. */
ssize_t my_getline(char **lineptr, size_t *n, FILE *stream) {
  unsigned index = 0;
  while ( index == 0 || (*lineptr)[index-1] != '\n' ) {
    if ( index+1 >= *n ) { /* Increase buffer size */
      *n = (*n+1)*2;
      *lineptr = realloc(*lineptr, *n);
      if ( *lineptr == NULL ) return -1; /* Failed to allocate memory */
    }
    /* Try to read some characters */
    if ( fgets(*lineptr+index, *n-index, stream) ) {
      index += strlen(*lineptr+index);
    }
    else break; /* Error or EOF */
  }
  return index;
}

int my_parseopt(const struct option *long_options, GA_settings *settings,
		int c, int option_index) {
  switch (c) {
  case 'o':
    ((specopts_t *)settings->ref)->basename_out = optarg;
    break;
  case 't':
    ((specopts_t *)settings->ref)->template_fn = optarg;
    break;
  case 'm':
    ((specopts_t *)settings->ref)->obsfile = optarg;
    break;
  case 'S':
    ((specopts_t *)settings->ref)->spcatbin = optarg;
    break;
  case 'b':
    ((specopts_t *)settings->ref)->bins = atoi(optarg);
    break;
  case 'w':
    ((specopts_t *)settings->ref)->distanceweight = atof(optarg);
    break;
  case 'P':
    ((specopts_t *)settings->ref)->popfile = optarg;
    break;
  case 20: /* amin */
  case 22: /* bmin */
  case 24: /* cmin */
  case 26: /* djmin */
  case 28: /* djkmin */
  case 30: /* dkmin */
  case 32: /* deljmin */
  case 34: /* delkmin */
    {
      int i = c/2-10;
      ((specopts_t *)settings->ref)->rangemin[i] = atoi(optarg);
      ((specopts_t *)settings->ref)->userange[i] = 1;
    }
    break;
  case 21: /* amax */
  case 23: /* bmax */
  case 25: /* cmax */
  case 27: /* djmax */
  case 29: /* djkmax */
  case 31: /* dkmax */
  case 33: /* deljmax */
  case 35: /* delkmax */
    {
      int i = (c-1)/2-10;
      ((specopts_t *)settings->ref)->rangemax[i] = atoi(optarg);
      ((specopts_t *)settings->ref)->rangetemp[i] = 1;
    }
    break;
  case 38: /* rangemin */
    ((specopts_t *)settings->ref)->obsrangemin = atoi(optarg);
    break;
  case 39: /* rangemax */
    ((specopts_t *)settings->ref)->obsrangemax = atoi(optarg);
    break;
  case 40: /* drfile */
    ((specopts_t *)settings->ref)->drfile = optarg;
    break;
  case 41: /* drtol */
    ((specopts_t *)settings->ref)->doublerestol = atof(optarg);
    break;
  case 42: /* linkbc */
    ((specopts_t *)settings->ref)->linkbc = 1;
    break;
  case 43: /* distributed */
    ((specopts_t *)settings->ref)->distributor = optarg;
    break;
  case 44: /* tempdir */
    ((specopts_t *)settings->ref)->tempdir = optarg;
    break;
  default:
    printf("Aborting: %c\n",c);
    abort ();
  }
  return 0;
}

int main(int argc, char *argv[]) {
  GA_settings settings;
  specopts_t specopts;
  time_t starttime = time(NULL); /* To compute runtime */
  int rc = 0;
  int i;
  /* After updating usage comments, run generate-usage.pl on this file
   * to update usage message header files.
   */
  static const struct option my_long_options[] = {
    /** -o, --output FILE
     *
     * Use FILE.{cat,int,out,var} for temporary and output storage.
     * (default is random name of the form "tmp-gaspec-XXX")
     */
    {"output",   required_argument, 0, 'o'},
    /** -t, --template FILE
     *
     * Use FILE.{int,var} as a template to generate SPCAT input.
     * (default "template") */
    {"template", required_argument, 0, 't'},
    /** -m, --match FILE
     *
     * Match against FILE, formatted as an SPCAT .cat file.  (default
     * "isopropanol.cat")
     */
    {"match",    required_argument, 0, 'm'},
    /** -S, --spcat FILE
     *
     * SPCAT program file. (default "./spcat") */
    {"spcat",    required_argument, 0, 'S'},
    /** -b, --bins NUMBER
     *
     * Number of bins for matching. (default at time of writing was
     * 600) */
    {"bins",     required_argument, 0, 'b'},
    /** -w, --weight NUMBER
     *
     * Weight of intensity vs peak count when evaluating each bin. */
    {"weight",     required_argument, 0, 'w'},
    /** -P, --popfile FILE
     *
     * File to load initial population from. Lines should be formatted
     * as "%d %d GD %u %u %u" (same as population output file).
     */
    {"popfile",    required_argument, 0, 'P'},
    /** --amin NUMBER
     *
     * Minimum A value (in units compatible with template file).
     */
    {"amin",       required_argument, 0, 20},
    /** --amax NUMBER
     *
     * Maximum A value (in units compatible with template file).
     */
    {"amax",       required_argument, 0, 21},
    /** --bmin NUMBER
     *
     * Minimum B value (in units compatible with template file).
     */
    {"bmin",       required_argument, 0, 22},
    /** --bmax NUMBER
     *
     * Maximum B value (in units compatible with template file).
     */
    {"bmax",       required_argument, 0, 23},
    /** --cmin NUMBER
     *
     * Minimum C value (in units compatible with template file).
     */
    {"cmin",       required_argument, 0, 24},
    /** --cmax NUMBER
     *
     * Maximum C value (in units compatible with template file).
     */
    {"cmax",       required_argument, 0, 25},
    /** --djmin NUMBER
     *
     * Minimum DJ value (in units compatible with template file).
     */
    {"djmin",      required_argument, 0, 26},
    /** --djmax NUMBER
     *
     * Maximum DJ value (in units compatible with template file).
     */
    {"djmax",      required_argument, 0, 27},
    /** --djkmin NUMBER
     *
     * Minimum DJK value (in units compatible with template file).
     */
    {"djkmin",     required_argument, 0, 28},
    /** --djkmax NUMBER
     *
     * Maximum DJK value (in units compatible with template file).
     */
    {"djkmax",     required_argument, 0, 29},
    /** --dkmin NUMBER
     *
     * Minimum DK value (in units compatible with template file).
     */
    {"dkmin",      required_argument, 0, 30},
    /** --dkmax NUMBER
     *
     * Maximum DK value (in units compatible with template file).
     */
    {"dkmax",      required_argument, 0, 31},
    /** --deljmin NUMBER
     *
     * Minimum delJ value (in units compatible with template file).
     */
    {"deljmin",    required_argument, 0, 32},
    /** --deljmax NUMBER
     *
     * Maximum delJ value (in units compatible with template file).
     */
    {"deljmax",    required_argument, 0, 33},
    /** --delkmin NUMBER
     *
     * Minimum delK value (in units compatible with template file).
     */
    {"delkmin",    required_argument, 0, 34},
    /** --delkmax NUMBER
     *
     * Maximum delK value (in units compatible with template file).
     */
    {"delkmax",    required_argument, 0, 35},
    /** --rangemin NUMBER
     *
     * Minimum observation range.
     */
    {"rangemin",   required_argument, 0, 38},
    /** --rangemax NUMBER
     *
     * Maximum observation range.
     */
    {"rangemax",   required_argument, 0, 39},
    /** --drfile FILE
     *
     * File containing double resonance data.
     */
    {"drfile",     required_argument, 0, 40},
    /** --drtol TOLERANCE
     *
     * Matching tolerance 
     */
    {"drtol",      required_argument, 0, 41},
    /** --linkbc
     *
     * Run algorithm using B+C and B-C instead of B and C.
     */
    {"linkbc",           no_argument, 0, 42},
    /** --distributed DISTRIBUTOR
     *
     * Use DISTRIBUTOR for distributed computation.
     */
    {"distributed", required_argument, 0, 43},
    /** --tempdir DIR
     *
     * Use DIR for temporary files.
     */
    {"tempdir",    required_argument, 0, 44},
    {0, 0, 0, 0}
  };
#ifdef CLIENT_ONLY
  FILE *config;
#else
  char *my_optstring = "o:t:m:b:S:w:P:";
  char *optlog = malloc(128);	/* Initial allocation */

  if ( !optlog ) { printf("Out of memory (optlog)\n"); exit(1); }
  optlog[0] = 0;

  GA_defaultsettings(&settings);
  settings.debugmode = 0;	/* Use non-verbose output */
  settings.popsize = 64;
  settings.generations = 200;
#endif

  settings.ref = &specopts;
  specopts.basename_out = NULL;
  specopts.template_fn = "template-404";
  specopts.obsfile = "isopropanol-404.cat";
#ifdef _WIN32
  specopts.spcatbin = "./spcat.exe";
#else
  specopts.spcatbin = "./spcat";
#endif
  specopts.tempdir = "temp";
  specopts.bins = BINS;
  specopts.distanceweight = 1.0;
  for ( i = 0; i < SEGMENTS; i++ )
    specopts.userange[i] = specopts.rangetemp[i] = specopts.rangemin[i] =
      specopts.rangemax[i] = 0;
  specopts.popfile = NULL;
  specopts.drfile = NULL;
  specopts.doubleres = NULL;
  specopts.doublereslen = 0;
  specopts.doublerestol = 2;
  specopts.linkbc = 0;
  specopts.obsrangemin = RANGEMIN;
  specopts.obsrangemax = RANGEMAX;
  specopts.distributor = NULL;

#ifdef CLIENT_ONLY
  if ( argc != 3 ) {
    printf("Usage: %s <configfile> <outfile>\nChecksum: %s\n",
           argv[0], CHECKSUM);
    exit(1);
  }
  if ( ( config = fopen(argv[1], "r") ) == NULL ) {
    printf("Could not open config file %s, errno=%d\n", argv[1], errno);
    exit(1);
  }
  {
    char *line = NULL; size_t linelen = 0;
    char key[512], value[4096];
    while ( my_getline(&line, &linelen, config) > 0 ) {
      int rc = sscanf(line, "CFG%*c %511s%*[ \t]%4095[^\r\n]", key, value);
      /* Handle this command-line argument */
      if ( rc == 1 || rc == 2 ) {
        //printf("FIXME arg %d '%s' '%s'\n",rc, key, value);
        for ( i = 0; my_long_options[i].name != 0; i++ ) {
          if ( strcmp(key, my_long_options[i].name) != 0 ) continue;
          /* Check for required argument */
          if ( my_long_options[i].has_arg == 1 && rc == 1 ) {
            printf("Option %s missing a required argument\n", key);
            exit(1);
          }
          /* Pass to handler */
          optarg = strdup(value); /* Don't free this */
          if ( my_long_options[i].flag != NULL ) {
            printf("Unsupported option structure: non-NULL flag\n");
            exit(1);
          }
          my_parseopt(my_long_options, &settings, my_long_options[i].val, i);
          break;
        }
      }
      else break;
    }
    /* Check VERSION field */
    if ( sscanf(line, "VERSION %s", key) != 1 ) {
      printf("Version tag is missing from configuration.\n");
      exit(1);
    }
    if ( strcmp(key, "ANY") == 0 ) {
      printf("Warning: Ignoring version from configuration.\n");
    }
    else if ( strcmp(key, CHECKSUM) != 0 ) {
      printf("Configuration version %s is incorrect.\n", key);
      exit(1);
    }
    free(line);
  }
#else /* not CLIENT_ONLY */
  GA_getopt(argc,argv, &settings, my_optstring, my_long_options, my_parseopt,
	    gaspectroscopy_usage, &optlog);

  /* Check segment ranges */
  for ( i = 0; i < SEGMENTS; i++ ) {
    if ( specopts.userange[i]+specopts.rangetemp[i] == 1 ) {
      /* Must specify both */
      printf("Cannot specify only one of minimum and maximum range.\n");
      exit(1);
    }
    else if ( specopts.rangemin[i] > specopts.rangemax[i] ) {
      /* Must be in correct order */
      printf("Minimum range may not be greater than maximum range.\n");
      exit(1);
    }
  }

  /* Connect to distributor */
  if ( specopts.distributor ) {
    struct addrinfo hints, *result, *rp;
    int sock;
    /* Parse distributor string for port number */
    char *port = NULL;
    if ( (port = strchr(specopts.distributor, ':')) != NULL ) {
      port[0] = 0;
      port++;
    }
    else port = "2222";

    /* Look up the host and service */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
    rc = getaddrinfo(specopts.distributor, port, &hints, &result);
    if ( rc != 0 ) {
      printf("Could not look up %s:%s, getaddrinfo returned %d\n",
             specopts.distributor, port, rc);
      exit(1);
    }

    /* Try to connect */
    for ( rp = result; rp != NULL; rp = rp->ai_next ) {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if ( sock == -1 ) continue;
      if ( connect(sock, rp->ai_addr, rp->ai_addrlen) != -1 ) break;
      close(sock);
    }
    if ( rp == NULL ) {
      printf("Could not connect to %s:%s\n", specopts.distributor, port);
      exit(1);
    }
    freeaddrinfo(result);

    /* Connected */
    if ( (settings.distributor = fdopen(sock, "r+")) == NULL ) {
      printf("Could not open socket handle, errno=%d\n", errno);
      exit(1);
    }
    fprintf(settings.distributor, "%s\nVERSION %s\n", optlog+1, CHECKSUM);
    settings.threadcount = 1;
  }
#endif /* not CLIENT_ONLY */

  /* Initialize observation storage */
  specopts.observation = NULL;
  specopts.observationsize = 0;
  specopts.observationcount = 0;

#ifdef CLIENT_ONLY
  specopts.basename_out = strdup(argv[2]);
#else
  /* Choose an output file */
  if ( specopts.basename_out == NULL ) specopts.basename_out = ".";
  {
    DIR *x = opendir(specopts.basename_out);
    if ( x != NULL ) {		/* Given a directory */
      char *dir = specopts.basename_out;
      unsigned len = strlen(dir)+32;
      specopts.basename_out = malloc(len);
      if ( !specopts.basename_out ) {
	printf("basename malloc failed\n");
	exit(1);
      }
      if ( snprintf(specopts.basename_out, len, "%s/tmp-gaspec-%d",
                    dir, getpid()) >= len ) {
	printf("basename overflow\n");
	exit(1);
      }
    }
    else {
      /* Requires dynamically allocated string */
      specopts.basename_out = strdup(specopts.basename_out);
      if ( !specopts.basename_out ) {
	printf("Out of memory (basename dup)\n");
	exit(1);
      }
    }
  }

  /* Debug mode */
  if ( !settings.debugmode ) {
    FILE *fh;
    char *filename;
    int stdoutfd = dup(STDOUT_FILENO); /* Save real STDOUT. */
    if ( ( filename = malloc(strlen(specopts.basename_out)+5) ) == NULL ) {
      printf("Out of memory (log file)\n");
      exit(1);
    }
    sprintf(filename, "%s.log", specopts.basename_out);
    /* Open new STDOUT handle */
    fflush(NULL);		       /* Flush output streams. */
    if ( ( fh = freopen(filename, "w", stdout) ) == NULL ) {
      printf("Failed to open log file, errno=%d\n", errno);
      free(filename);
      exit(1);
    }
    free(filename);
    dup2(fileno(fh), STDOUT_FILENO); /* Make sure fd #1 is STDOUT */
    if ( ( settings.stdoutfh = fdopen(stdoutfd, "w") ) == NULL ) {
      printf("Failed to reopen stdout, errno=%d\n", errno);
      exit(1);
    }
    qprintf(&settings, "Details saved in %s.log\n", specopts.basename_out);
  }
#endif
#ifndef _WIN32
  /* Open /dev/null to allow hiding of SPCAT output. Non-portable. */
  {
    FILE *devnull;
    if ( ( devnull  = fopen("/dev/null", "w") ) == NULL ) {
      printf("Can't open /dev/null, errno=%d\n", errno);
      exit(1);
    }
    if ( ( specopts.stdoutfd = dup(fileno(stdout)) ) == -1 ) {
      printf("Can't dup stdout, errno=%d\n", errno);
      exit(1);
    }
    if ( ( specopts.devnullfd = fileno(devnull) ) == -1 ) {
      printf("Can't get fileno of devnull, errno=%d\n", errno);
      exit(1);
    }
  }
#endif
#ifndef CLIENT_ONLY
  printf("%s\n", optlog+1); free(optlog);
#endif

  qprintf(&settings, "Using output file %s\n", specopts.basename_out);

  /* Load SPCAT .int & .var input template files */
  printf("Loading template files %s.{var,int}\n", specopts.template_fn); 
  if ( (rc = load_spec_templates(&specopts)) != 0 ) {
    qprintf(&settings, "load_spec_templates failed: %d\n", rc);
    return rc;
  }
  /* Load observed data file */
  printf("Loading observation file %s\n", specopts.obsfile);
  if ( (rc = load_spec_observation(&specopts)) != 0 ) {
    qprintf(&settings, "load_spec_observation failed: %d\n", rc);
    return rc;
  }
#ifndef CLIENT_ONLY
  /* Load starting population */
  if ( specopts.popfile ) {
    printf("Loading inital population %s\n", specopts.popfile);
    /* TODO cleanup */
    specopts.popdata = malloc(sizeof(GA_segment)*SEGMENTS*settings.popsize);
    if ( !specopts.popdata ) {
      qprintf(&settings, "load popdata failed\n");
      return 1;
    }
    memset(specopts.popdata, 0, sizeof(specopts.popdata));
    FILE *fh = fopen(specopts.popfile, "r");
    unsigned int idx = 0;
    while ( idx < settings.popsize ) {
      int rc = fscanf(fh, "%*d %*d GD");
      if ( rc == 0 ) {
        for ( i = 0; i < SEGMENTS; i++ ) {
          rc = fscanf(fh, "%u", &specopts.popdata[idx*SEGMENTS+i]);
          if ( rc != 1 ) break;
        }
      } else rc = 0; /* Pretend to be an error from the inner loop */
      if ( rc == 0 ) {
        qprintf(&settings, "load popdata failed: EOF at idx=%u\n", idx);
        return 1;
      }
      idx++;
    }
    fclose(fh);
    /* Display the loaded population (I don't feel like migrating this to
     * SEGMENT count-independent form (FIXME) */
    /*
    for ( idx=0;idx<settings.popsize;idx++ ) {
      printf("0000 %04u GD %10u %10u %10u\n", idx, specopts.popdata[idx*SEGMENTS], specopts.popdata[idx*SEGMENTS+1],specopts.popdata[idx*SEGMENTS+2]);
    }*/
  }
#endif
  /* Load double resonance */
  if ( specopts.drfile ) {
    printf("Loading double resonances %s\n", specopts.drfile);
    /* TODO cleanup */
    int dblressize = 0;
    //specopts.doubleres = NULL;
    //specopts.doublereslen = 0;
    FILE *fh = fopen(specopts.drfile, "r");
    if ( fh == NULL ) {
      qprintf(&settings, "Failed to open drfile, errno=%d\n", errno);
      return 1;
    }
    char *line = NULL; size_t linelen = 0;
    while ( my_getline(&line, &linelen, fh) > 0 ) {
      float val; int ptrbump; char *linepos = line;
      dblres_relation *row; int peaklistsize = 0;
      /* Allocate space for the new row */
      if ( specopts.doublereslen >= dblressize ) {
        dblressize = (dblressize+1)*2;
        specopts.doubleres = realloc(specopts.doubleres,
                                     sizeof(dblres_relation)*dblressize);
        if ( !specopts.doubleres ) {
          qprintf(&settings, "Double resonance list allocation failed\n");
          return 1;
        }
      }
      /* Initilize new relation */
      row = &specopts.doubleres[specopts.doublereslen];
      row->npeaks = 0;
      row->peaks = NULL;
      while ( sscanf(linepos, "%g%n", &val, &ptrbump) != EOF ) {
        /* Allocate space for the new entry in the row */
        if ( row->npeaks >= peaklistsize ) {
          peaklistsize = (peaklistsize+1)*2;
          row->peaks = realloc(row->peaks, sizeof(float)*peaklistsize);
          if ( !row->peaks ) {
            qprintf(&settings, "Double resonance row allocation failed\n");
            return 1;
          }
        }
        row->peaks[row->npeaks] = val;
        row->npeaks++;
        linepos += ptrbump;
      }
      specopts.doublereslen++;
    }
    if ( !feof(fh) ) {
      qprintf(&settings, "Double resonance parse error\n");
      return 1;
    }
    fclose(fh);
  }

  printf("Using %d bins\n", specopts.bins);

#ifdef CLIENT_ONLY
  /* Do something */
  {
    struct GAC_individual { unsigned int index; GA_individual indiv; }
      *pop = NULL;
    unsigned int popsize = 0, ncompleted = 0, pos = 0;
    /* Load from/save to the output file */
    char *line = NULL; size_t linelen = 0; FILE *outfile;
    /* Initialize GA objects */
    GA_session ga;
    GA_thread thread;
    ga.settings = &settings;
    thread.session = &ga;
    thread.ref = NULL;
    rc = GA_thread_init(&thread);
    if ( rc != 0 ) {
      printf("GA_thread_init failed: %d", rc);
      exit(1);
    }
    /* Load population from config file */
    while ( my_getline(&line, &linelen, config) > 0 ) {
      unsigned int index, offset; char *subline = line;
      int rc = sscanf(line, "ITEM %u%n", &index, &offset);
      /* Handle this item */
      if ( rc < 1 ) {
        printf("Not an item: %s", line);
        continue;
      }
      /* Grow allocation if necessary */
      if ( pos >= popsize ) {
        popsize = (popsize+1)*2;
        pop = realloc(pop, popsize*sizeof(struct GAC_individual));
        if ( !pop ) {
          printf("malloc failed while loading population.\n");
          exit(1);
        }
      }
      /* Prepare individual structure */
      memset(&(pop[pos]), 0, sizeof(struct GAC_individual));
      pop[pos].index = index;
      pop[pos].indiv.gdsegments = malloc(sizeof(GA_segment)*SEGMENTS);
      /* Load segments */
      for ( i = 0; i < SEGMENTS; i++ ) {
        subline += offset;
        rc = sscanf(subline, "%u%n", &(pop[pos].indiv.gdsegments[i]), &offset);
        if ( rc < 1 ) {
          printf("Item parse failure: %s", line);
          exit(1);
        }
      }
      pos++;
    }
    popsize = pos;
    /* Load state from output file */
    if ( ( outfile = fopen(specopts.basename_out, "r") ) != NULL ) {
      while ( my_getline(&line, &linelen, outfile) > 0 ) {
        char linecheck[4]; unsigned int index;
        /* Try to read a line from the file */
        if ( ( sscanf(line, "FITNESS %u %lf %3s", &index,
                      &(pop[ncompleted].indiv.fitness), linecheck) != 3 ) ||
              ( strcmp(linecheck, "EOL") != 0 ) ) break;
        if ( index != pop[ncompleted].index ) {
          printf("Output file (%u) does not match config (%u) at line %u\n",
                 index, pop[ncompleted].index, ncompleted+1);
          ncompleted = 0; /* Don't trust the data */
          break;
        }
        ncompleted++;
      }
      fclose(outfile);
    }
    /* Now open output file for writing */
    if ( ( outfile = fopen(specopts.basename_out, "w") ) == NULL ) {
      printf("Could not open output file %s, errno=%d\n",
             specopts.basename_out, errno);
      exit(1);
    }
    /* Start read loop */
    for ( pos = 0; pos < popsize; pos++ ) {
      if ( pos >= ncompleted ) {
        printf("Evaluating %u\n", pop[pos].index);
        rc = GA_fitness(&ga, thread.ref, &(pop[pos].indiv));
        if ( rc != 0 ) {
          printf("Fitness of item %u returned error %d\n", pop[pos].index, rc);
          exit(1);
        }
      }
      printf("FITNESS %04d %f EOL\n", pop[pos].index, pop[pos].indiv.fitness);
      fflush(stdout);
      fprintf(outfile, "FITNESS %04d %f EOL\n",
              pop[pos].index, pop[pos].indiv.fitness);
      fflush(outfile);
    }
    GA_thread_free(&thread);
    free(line);
    free(pop);
  }

  qprintf(&settings, "Finished.\nTook %u seconds\n", time(NULL)-starttime);
#else
  {
    GA_session ga;
    /* Run the genetic algorithm */
    if ( (rc = GA_init(&ga, &settings, SEGMENTS)) != 0 ) {
      qprintf(&settings, "GA_init failed: %d\n", rc);
      return rc;
    }

#if 0	       /* Debugging mode: enumeration of all values */
    {
      if ( SEGMENTS != 1 || settings.threadcount != 1 ) {
        qprintf(&settings, "Settings error\n");
        exit(1);
      }
      GA_individual *x = &(ga.population[0]);
      int rc;
      unsigned int i;
      i = -1;//2082167296;
      do {
        i+=10000;
        x->segments[0] = grayencode(i);
        rc = GA_fitness(&ga, ga.threads[0].ref, x);
        if ( rc != 0 ) {
	  qprintf(&settings, "fitness error, %d\n", rc);
	  exit(1);
        }
        printf("ENUM %u %f\n", i, x->fitness);
      } while ( x->segments[0] <= 0xFFFFFFFF); //2107767296 );
    }
#endif

    printf("Starting %d generations\n", settings.generations);
    if ( (rc = GA_evolve(&ga, 0)) != 0 ) {
      qprintf(&settings, "GA_evolve failed: %d\n", rc);
      return rc;
    }

    /* Compute runtime */
    starttime = time(NULL)-starttime;
    qprintf(&settings, "Finished.\nTook %u seconds (%f sec/gen)\n",
	    starttime, starttime/(settings.generations+1.0));

    /* Cleanup */
    if ( (rc = GA_cleanup(&ga)) != 0 ) {
      qprintf(&settings, "GA_cleanup failed: %d\n", rc);
      return rc;
    }
  }
#endif
  free(specopts.basename_out);
  free(specopts.observation);
  free(specopts.doubleres);
  rc = 0;
  /* TODO: Free more memory */
  /* Remove temporary files */
  /* printf("rmdir: %d\n", rmdir(tempdir)); */
  printf("Exiting: %d\n", rc);
  return rc;
}

#ifndef CLIENT_ONLY
int GA_random_segment(GA_session *ga, const unsigned int i,
                      const unsigned int j, int *r) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  int rc = random_r(&ga->rs, r);
  /*
  GA_segment ideal[] = {2094967296, 1600000000, 100000000};
  GA_segment range[] = {0.05*ideal[0], 0.05*ideal[1], 0.05*ideal[2]};
  */
  if ( opts->popfile ) {
    rc = 0; // Note that the random state will be completely different.
    *r = grayencode(opts->popdata[i*SEGMENTS+j]);
  }
  else if ( !rc && opts->userange[j] ) {
    unsigned int realr;
    realr = (unsigned)((double)(*r)*(opts->rangemax[j]-opts->rangemin[j])
                       /RAND_MAX)+opts->rangemin[j];
    //printf("%d\n", realr);
    *r = grayencode(realr);
    //printf("%03d %u < %u < %u\n", i, opts->rangemin[j], realr, opts->rangemax[j]);
  }
  return rc;
}

int GA_finished_generation(const GA_session *ga, int terminating) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  /* Save best result */
  unsigned int p;
  int rc = 0;
  FILE *fh;
  char filename[128];
  snprintf(filename, sizeof(filename), "%s.pop", opts->basename_out);
  if ( ( fh = fopen(filename, "w") ) == NULL ) {
    qprintf(ga->settings, "Failed to open pop output file, errno=%d\n", errno);
    return 10;
  }
  for ( p = 0; p < ga->settings->popsize; p++ ) {
    GA_segment *x = ga->population[p].gdsegments;
    int j = 0;
    fprintf(fh, "%04u %04u GD", ga->generation, p);
    for ( j = 0; j < SEGMENTS; j++ )
      fprintf(fh, "  %10u", x[j]);
    fprintf(fh, "\n");
    if ( p == ga->fittest ) {
      /* Generate SPCAT input file */
      rc = generate_input_files(opts, opts->basename_out, x);
      if ( rc != 0 ) {
        qprintf(ga->settings,
                "finished_generation generate_input_files failed: %d\n", rc);
        return rc;
      }
    }
  }
  if ( fclose(fh) ) {
    qprintf(ga->settings, "Failed to close input file, errno=%d\n", errno);
    return 11;
  }

  if ( terminating )
    qprintf(ga->settings, "Best result saved in %s\n", opts->basename_out);
  return 0;
}

int GA_fitness_quick(const GA_session *ga, GA_individual *elem) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  GA_segment *x = elem->gdsegments;
  int i = 0;

  /* Check basic constraints: A >= B >= C.
   * i.e. Fail if A < B or B < C. */
  {
    GA_segment b = x[1];
    GA_segment c = x[2];
    /* Convert B+C and B-C to B and C */
    if ( opts->linkbc ) { b = (x[1]+x[2])/2; c = (x[1]-x[2])/2; }
    if ( x[0] < b || b < c || c <= 0 )
      return 0;
    //printf("ABC %d >= %d >= %d\n", x[0], b, c);
  }
  /*
  GA_segment ideal[] = {2094967296, 1600000000, 100000000};
  GA_segment range[] = {0.05*ideal[0], 0.05*ideal[1], 0.05*ideal[2]};
  */
  for ( i = 0; i < SEGMENTS; i++ ) {
    if ( opts->userange[i] &&
         ( (x[i] < opts->rangemin[i]) || (x[i] > opts->rangemax[i]) ) ) {
      /*
      printf("Rejecting segment %03d %010u < %010u < %010u\n", i,
             opts->rangemin[i],x[i],opts->rangemax[i]);
      */
      return 0;
    }
  }
  return 1;
}

int GA_termination(const GA_session *ga) {
  if ( ga->population[ga->fittest].unscaledfitness > -0.00001 )
    return 1;
  return 0;
}
#endif /* not CLIENT_ONLY */

typedef struct { unsigned int index; double total; } sortable_bin;

int bin_comparator(const void *a, const void *b) {
  sortable_bin x = *(sortable_bin *)a, y = *(sortable_bin *)b;
  if ( x.total < y.total ) return 1;
  else if ( x.total > y.total ) return -1;
  else return 0; /* x-y; */	/* If equal sort by index to preserve order */
}

int GA_fitness(const GA_session *ga, void *thbuf, GA_individual *elem) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  specthreadopts_t *thrs = (specthreadopts_t *)thbuf;
  GA_segment *x = elem->gdsegments;
  int i = 0, j = 0;
  FILE *fh;
  int rc = 0;
  /* int xi, yi; */
  double fitness;
  const double binsize = ((double)(opts->obsrangemax-opts->obsrangemin))/opts->bins;
  double obsbin[opts->bins], compbin[opts->bins];
  int obsbincount[opts->bins], compbincount[opts->bins];
  double binweights[opts->bins]; /* 20110215, J-weighting */
#ifdef USE_SPCAT_OBJ
  spcs_t spcs;
  char *buffers[NFILE];
  size_t bufsizes[NFILE];
#else
  char filename[256];
#endif

#ifdef USE_SPCAT_OBJ
  /* Generate SPCAT input buffer */
  rc = generate_input_buffers(opts, buffers, bufsizes, x);
  if ( rc != 0 ) return rc;
  if ( init_spcs(&spcs) ) return 11;
  if ( spcat(&spcs, buffers, bufsizes) ) return 12;
  if ( free_spcs(&spcs) ) return 13;
  free(buffers[0]); free(buffers[1]);
  /* Read SPCAT output buffer */
  if ( !bufsizes[ecat] ) fh = NULL;
  else if ( ( fh = fmemopen(buffers[ecat], bufsizes[ecat], "r") ) == NULL ) {
    qprintf(ga->settings, "Failed to open file, errno=%d\n", errno);
    return 15;
  }
  /******** FIXME TODO NEED TO SORT ********/
#else
  /* Generate SPCAT input file */
  rc = generate_input_files(opts, thrs->basename_temp, x);
  if ( rc != 0 ) return rc;

  /* Run SPCAT. Append a '.' to the end of the filename so that it
   * doesn't choke on filenames with '.' in them (like ./foo).
   */

  /* This way we can avoid running /bin/sh for every fitness
   * evaluation. This doesn't quite work as expected -- ^C often
   * fails if we use the recommended signal handling code. */
  //tprintf("Running %s %s\n", opts->spcatbin, filename);

#ifdef _WIN32
  snprintf(filename, sizeof(filename), "\"%s\" \"%s.\"",
           opts->spcatbin, thrs->basename_temp);
  {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    DWORD exitCode;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    //printf("[%s] %s\n", opts->spcatbin, filename);
    i = CreateProcess(opts->spcatbin, filename, NULL, NULL, FALSE,
                      DETACHED_PROCESS /* invisible */, NULL, NULL, &si, &pi);
    if ( i == 0 ) {
      printf("CreateProcess: [%s] %s: failed, GetLastError=%d\n",
             opts->spcatbin, filename, GetLastError());
      return 11;
    }
    /* Wait until child process exits. */
    i = WaitForSingleObject(pi.hProcess, INFINITE);
    if ( i != WAIT_OBJECT_0 ) {
      printf("WaitForSingleObject [%s] %s: returned %d, GetLastError=%d\n",
             opts->spcatbin, filename, i, GetLastError());
      return 11;
    }
    i = GetExitCodeProcess(pi.hProcess, &exitCode);
    if ( i == 0 ) {
      printf("GetExitCodeProcess [%s] %s: failed, GetLastError=%d\n",
             opts->spcatbin, filename, GetLastError());
      return 11;
    }
    if ( exitCode != 0 ) {
      printf("SPCAT returned nonzero: [%s] %s: Exit code %d\n", exitCode);
      return 10;
    }
    /* Close process and thread handles. */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
#else
  snprintf(filename, sizeof(filename), "%s.", thrs->basename_temp);
  i = invisible_system(opts->devnullfd, 2, opts->spcatbin, filename);
  if ( i != 0 ) return 11;		/* Failed */
  if ( !WIFEXITED(i) || ( WEXITSTATUS(i) != 0 ) )
    return 10;		/* Subprocess did not exit normally. */
#endif

  /* Read SPCAT output file */
  snprintf(filename, sizeof(filename), "%s.cat", thrs->basename_temp);
  if ( ( fh = fopen(filename, "r") ) == NULL ) {
    qprintf(ga->settings, "Failed to open cat file, errno=%d\n", errno);
    return 15;
  }
#endif

  thrs->compdatacount = 0;
  rc = load_catfile(fh, &(thrs->compdata), &(thrs->compdatasize),
                  &(thrs->compdatacount));
  if ( rc > 0 ) return 20+rc;
  fclose(fh);

  /* Determine fitness */
  //tprintf("COUNTS: %d %d\n", opts->observationcount, thrs->compdatacount);
  fitness = 0;

  /* Check double resonance */
  
  /* BUG -- Need to check against *either* of the dblreses of first item
   * -- they don't need to to all be the same. */
  /* Isn't that fixed ? */
  
  int drfail = 0;
  for ( i = 0; i < opts->doublereslen; i++ ) { /* For each resonance */
    int drlen = 0; /* Reset the QN list */
    //printf("Starting QN:\n");
    /* For each frequency in the resonance */
    for ( j = 0; j < opts->doubleres[i].npeaks; j++ ) {
      int k = 0;
      drfail = 1; /* If we don't find a match for this frequency, fail */
      for ( k = 0; k < thrs->compdatacount; k++ ) { /* For each actual peak */
        int l = 0, m = 0;
        /* Check if this peak is near the resonance frequency */
        if ( fabsf(thrs->compdata[k].frequency-opts->doubleres[i].peaks[j])
             > opts->doublerestol ) continue;

        /* For each quantum number associated with the peak */
        for ( m = 0; m < QN_COUNT; m++ ) {
          int found = 0;
          //printf("Looking for QN %d %d %d\n", thrs->compdata[k].qn[m*QN_DIGITS], thrs->compdata[k].qn[m*QN_DIGITS+1], thrs->compdata[k].qn[m*QN_DIGITS+2]);
          /* Check all previously seen quantum numbers */
          for ( l = 0; l < drlen; l++ ) {
            int n = 0;
            /* Specifically, each of the QN_COUNT quantum numbers */
            for ( n = 0; n < QN_COUNT; n++ ) {
              if ( memcmp(&(thrs->drlist[l].qn[n*QN_DIGITS]),
                          &(thrs->compdata[k].qn[m*QN_DIGITS]),
                          sizeof(int)*QN_DIGITS) == 0 ) {
                /* We found the QN, skip it unless it was also found last time */
                if ( thrs->drlist[l].seen >= j ) {
                  //printf("  Match QN %d %d %d\n", drlist[l].qn[n*QN_DIGITS], drlist[l].qn[n*QN_DIGITS+1], drlist[l].qn[n*QN_DIGITS+2]);
                  thrs->drlist[l].seen = j+1;
                  found = 1;
                  /* break; */
                }
              }
            }
          }

          /* If any matches were found, we haven't failed yet */
          if ( found ) {
            drfail = 0;
            /*
            if ( memcmp(drlist[l].qn, thrs->compdata[k].qn,
                        sizeof(drlist[drlen].qn)) != 0 )
              found = 0; *//* Differs, so add anyway */ /* FIXME */
          }
          /* For first frequency in resonance, we can add the QN to the list */
          if ( !found && j == 0 ) {
            if ( drlen >= thrs->drsize ) {
              thrs->drsize = (thrs->drsize+1)*2;
              //printf("Increasing memory allocation to %u\n", thrs->compdatasize);
              thrs->drlist = realloc(thrs->drlist,
                                     sizeof(dblres_check)*thrs->drsize);
              if ( thrs->drlist == NULL ) {
	        qprintf(ga->settings, "Out of memory, errno=%d\n", errno);
	        return 35;
              }
            }
            thrs->drlist[drlen].seen = 1;
            memcpy(thrs->drlist[drlen].qn, thrs->compdata[k].qn,
                   sizeof(thrs->drlist[drlen].qn));
            //printf("  Added QN %d %d %d %d %d %d\n", drlist[drlen].qn[0], drlist[drlen].qn[1], drlist[drlen].qn[2], drlist[drlen].qn[3], drlist[drlen].qn[4], drlist[drlen].qn[5]);
            drlen++;
            drfail = 0;
          }
          //else if ( !found ) printf("  Not found\n");
        }
      }
      if ( drfail ) break;
    }
    if ( drfail ) break;
  }
  if ( drfail ) {
    elem->fitness = nan("fail"); /* Double resonance failed somewhere */
    return 0;
  }

  /* Initialize bins */
  for ( i = 0; i < opts->bins; i++ ) {
    obsbin[i] = 0; compbin[i] = 0;
    obsbincount[i] = 0; compbincount[i] = 0;
    binweights[i] = 0;
  }

  //double obsmax=0/*, obsmin = 0*/;
  for ( j=0;j<=1;j++ ) {	/* 0=Observed / 1=Generated */
    for ( i=0; i<((j==0)?opts->observationcount:thrs->compdatacount); i++ ) {
      datarow entry;
      if ( j == 0 ) entry = opts->observation[i]; /* Observed */
      else entry = thrs->compdata[i]; /* Generated */
      if ( ( entry.frequency < opts->obsrangemin ) || ( entry.frequency > opts->obsrangemax ) )
        continue;
      /* We're within the valid range */
      int bin = floor((entry.frequency-opts->obsrangemin)/binsize);
      if ( bin >= opts->bins ) bin = opts->bins-1;
      if ( j == 0 ) {
        obsbin[bin] += entry.intensity;
        obsbincount[bin]++;
        //if ( i == 0 || entry.b > obsmax ) obsmax = entry.b;
      }
      else {
        //double weight = fabs(entry.b/obsmax);
        //if ( weight < 1 ) weight = 1;
        /* Prediction - scale me */
        compbin[bin] += entry.intensity;//*weight;
        compbincount[bin]++;
        //printf("BW: sqrt(2/%d)\n",entry.qn[0]+entry.qn[3]);
        binweights[bin] += sqrt(2.0/(entry.qn[0]+entry.qn[3])); /* 20110215 */
      }
    }
  }
#if 0 /* EXPERIMENTAL BEHAVIOR 2010-09-26 */
  double binweights[opts->bins];
  sortable_bin binorder[opts->bins];
  for ( i = 0; i < opts->bins; i++ ) {
    binorder[i].index = i; binorder[i].total = compbin[i];
  }
  qsort(binorder, opts->bins, sizeof(sortable_bin), bin_comparator);
  double binmin = binorder[opts->bins-1].total;
  double bindiff = binorder[0].total-binmin;
  for ( i = 0; i < opts->bins; i++ ) {
    binweights[i] = (obsbin[i]-binmin)/bindiff;
  }
#endif
  /* Compute bin fitnesses using w*|X_o - X_c|^2 + (1-w)*|N_o - N_c|^2 */
  for ( i=0; i<opts->bins; i++ ) {
    float comp = opts->distanceweight *
      powf(fabs(obsbin[i]-compbin[i]),2) +
      (1-opts->distanceweight)*powf(fabs(obsbincount[i]-compbincount[i]),2);
    fitness += comp*binweights[i];
  }

#if 0
  xi=0;
  /* Iterate over all y (generated) points, comparing to nearest x
   * (observation) point. Assumes points are with ascending frequency.
   */
  for ( yi=0; yi<compdatacount; yi++ ) {
    float compa = sqrtf(powf(fabs(observation[xi].a-compdata[yi].a),2) +
			powf(fabs(observation[xi].b-compdata[yi].b),2));
    while ( xi+1 < observationcount ) {
      float compb = sqrtf(powf(fabs(observation[xi+1].a-compdata[yi].a),2) +
			  powf(fabs(observation[xi+1].b-compdata[yi].b),2));
      if ( compb < compa ) { xi++; compa = compb; }
      else break;
    }
    fitness += compa;
  }
#endif

  elem->fitness = -fitness*1000;
  /* printf("%u\n",elem->segments[0]); */
  /* elem->fitness = -fabs(64-(double)x[0]*x[0]); */
  /* elem->fitness = (fitness > MAX_FITNESS) ? 0 : (MAX_FITNESS - fitness); */

  return 0;
}

int GA_thread_init(GA_thread *thread) {
  specthreadopts_t *opts;
  /* Allocate persistant storage for computed data (this avoids having
   * to allocate it for every fitness evaluation). */
  if ( ( opts = malloc(sizeof(specthreadopts_t)) ) == NULL )
    return 1;
  memset(opts, 0, sizeof(opts));
  opts->compdata = NULL;
  opts->compdatasize = 0;
  opts->compdatacount = 0;
  opts->drlist = NULL;
  opts->drsize = 0;

#ifndef USE_SPCAT_OBJ
  /* Use separate temporary files for each thread. */
  opts->basename_temp = make_spec_temp(((specopts_t *)(thread->session->settings->ref))->tempdir);
  if ( opts->basename_temp == NULL ) return 1;
  qprintf(thread->session->settings, "Using temporary file %s\n",
	  opts->basename_temp);
#endif

  thread->ref = (void *)opts;
  return 0;
}

int GA_thread_free(GA_thread *thread) {
  specthreadopts_t *thrs = (specthreadopts_t *)(thread->ref);

  /* Remove temporary files. */
#ifndef USE_SPCAT_OBJ
  char *filename;
  if ( ( filename = malloc(strlen(thrs->basename_temp)+5) ) != NULL ) {
    int i;
    for ( i = 0; i < 2; i++ ) {
      int j;
      for ( j = 0; j < 2; j++ ) {
	sprintf(filename, "%s.%s", thrs->basename_temp,
		j == 0 ? input_suffixes[i] : output_suffixes[i]);
	unlink(filename);
      }
    }
    free(filename);
  }
  else qprintf(thread->session->settings,
	       "Could not delete temporary files: Out of memory\n");
  free(thrs->basename_temp);
#endif

  free(thrs->compdata);
  free(thrs);
  return 0;
}

#ifndef _WIN32
pthread_mutex_t GA_iomutex = PTHREAD_MUTEX_INITIALIZER;
/** Start a process with STDOUT redirected to the file descriptor
 * stdoutfd, wait for it to finish, and then return its exit status.
 * Thread-safe in conjunction with qprintf and tprintf.
 * This function based on the sample implementation of system from:
 * http://www.opengroup.org/onlinepubs/000095399/functions/system.html
 *
 * \see <a href="http://linux.die.net/man/3/system">system(3)</a>,
 *      qprintf, tprintf
 */
int invisible_system(int stdoutfd, int argc, ...) {
  int stat;
  pid_t pid;
  /*
    struct sigaction sa, savintr, savequit;
    sigset_t saveblock;
  */
  va_list ap;
  char **argv;
  int i;
  /* FIXME - Debug only */
  //struct timeval starttime, endtime;
  if ( argc <= 0 ) {
    printf("invisible_system: argc <= 0\n");
    return 1;
  }
  argv = malloc(sizeof(char*)*(argc+1));
  if ( !argv ) {
    printf("invisible_system: argv malloc failed\n");
    return 1;
  }
  va_start(ap, argc);
  for ( i = 0; i < argc; i++ ) {
    argv[i] = va_arg(ap, char *);
  }
  va_end(ap);
  argv[argc] = NULL;
  /*
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigemptyset(&savintr.sa_mask);
  sigemptyset(&savequit.sa_mask);
  sigaction(SIGINT, &sa, &savintr);
  sigaction(SIGQUIT, &sa, &savequit);
  sigaddset(&sa.sa_mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &sa.sa_mask, &saveblock);
  */

  /* LOCK io */
#if THREADS && !CLIENT_ONLY
  stat = pthread_mutex_lock(&GA_iomutex);
  if ( stat ) { printf("tprintf: mutex_lock(io): %d\n", stat); exit(1); }
#endif
  flockfile(stdout);
  fflush(NULL);
  //gettimeofday(&starttime, NULL);/* To compute runtime */
  if ((pid = fork()) == 0) {
    /*
    sigaction(SIGINT, &savintr, (struct sigaction *)0);
    sigaction(SIGQUIT, &savequit, (struct sigaction *)0);
    sigprocmask(SIG_SETMASK, &saveblock, (sigset_t *)0);
    */

    /* Disable stdout */
    if ( dup2(stdoutfd, STDOUT_FILENO) == -1 ) _exit(127);
    funlockfile(stdout);

    execv(argv[0], argv);
    /* execl("/bin/sh", "sh", "-c", arg, (char *)0); */
    _exit(127);
  }
  /* UNLOCK io */
  funlockfile(stdout);
#if THREADS && !CLIENT_ONLY
  stat = pthread_mutex_unlock(&GA_iomutex);
  if ( stat ) { printf("tprintf: mutex_unlock(io): %d\n", stat); exit(1); }
#endif
  free(argv);

  if (pid == -1) {
    printf("invisible_system: fork failed, errno=%d\n", errno);
    stat = -1; /* errno comes from fork() */
  } else {
    while (waitpid(pid, &stat, 0) == -1) {
      if (errno != EINTR){
	stat = -1;
	printf("invisible_system: waitpid fail, errno=%d\n", errno);
	break;
      }
    }
  }
  //gettimeofday(&endtime, NULL);
  //printf("System took %f seconds.\n", timeval_diff(NULL, &endtime, &starttime)/1000000.0);
  /*
  sigaction(SIGINT, &savintr, (struct sigaction *)0);
  sigaction(SIGQUIT, &savequit, (struct sigaction *)0);
  sigprocmask(SIG_SETMASK, &saveblock, (sigset_t *)0);
  */

  return(stat);
}
#endif
