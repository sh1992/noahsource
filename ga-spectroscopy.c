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
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include "ga.h"
#include "ga-spectroscopy.usage.h"

#ifdef USE_SPCAT_OBJ
#include "spcat-obj/spcat-obj.h"
#endif

#define BINS 600		/* Number of bins to use for comparison */
#define RANGEMIN 8700		/* Ignore below this frequency */
#define RANGEMAX 18300		/* Ignore above this frequency */

#define SEGMENTS 3		/* See also fprintf in generate_input_files */

/* char tempdir[16]; */

/* Use same order as spcat_ext and spcat_efile in spcat-obj.h */
const char input_suffixes[2][4] = {"int", "var"};
const char output_suffixes[2][4] = {"out", "cat"};
#define QN_COUNT 2
#define QN_DIGITS 3
typedef struct {
  float frequency, intensity;
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
typedef struct {
  datarow *compdata;		/* Data storage for comparison */
  int compdatasize, compdatacount;
#ifndef USE_SPCAT_OBJ
  char *basename_temp;		/* Basename of temporary file */
#endif
} specthreadopts_t;
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
      perror("Failed to open template file");
      free(filename);
      return i+10;
    }
    fread(opts->template[i], sizeof(opts->template[i]), 1, fh);
    if ( ferror(fh) ) {
      perror("Failed to read template file");
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
  float a, b, maxb = 0;
  char trailing[1024];
  int i = 0, j = 0;
  while ( 1 ) {
    /* Magic incantation for: NUMBER (IGNORED NUMBER) NUMBER (REST OF LINE) */
    //if ( fh ) i = fscanf(fh, "%g %*g %g %*[^\n]", &a, &b);
    // 30310 3 7      10 3 8
    // 303231013      231014
    if ( fh ) i = fscanf(fh, "%g %*g %g %*d %*g %*g %*s%1024[^\r\n]",
                         &a, &b, trailing);
    else break; // Empty memfile wasn't opened
    if ( i == EOF && ferror(fh) ) {
      perror("Failed to read template file");
      return 3;
    }
    else if ( i == EOF ) break;
    else if ( i != 3 ) {
      perror("File format error\n");
      return 4;
    }
    //printf("%g %g", a,b);
    /* Allocate additional memory, if neccessary */
    if ( *count >= *size ) {
      *size = ((*size)+1)*2;
      //printf("Increasing memory allocation to %u\n", thrs->compdatasize);
      *storage = realloc(*storage, sizeof(datarow)*(*size));
      if ( *storage == NULL ) {
	perror("Out of memory\n");
	return 5;
      }
    }
    if ( *count == 0 || b > maxb ) maxb = b;
    (*storage)[*count].frequency = a;
    //printf("POW 10^%f => %g\n", b, powf(10,b));
    (*storage)[*count].intensity = b;//powf(10,b);
    /* Double resonance from trailing data */
    /* FIXME: Assumes quantum numbers are of type 303! */
    /* PROGRAM DOES NOT SUPPORT VARIABLE QUANTUM NUMBER TYPES */
    for ( i = 0; i < 2; i++ ) {
      for ( j = 0; j < 3; j++ ) {
        char str[3]; int val;
        str[0] = trailing[4+12*i+j*2];
        str[1] = trailing[4+12*i+j*2+1];
        str[2] = 0;
        val = parseqn(str);
        //printf("'%s'\n",trailing);
        if ( val == -9999 ) return 6;
        //printf(" %d", val);
        (*storage)[*count].qn[i*3+j] = val;
      }
    }
    //printf("\n");
    // Next item
    (*count)++;
  }
  //printf("MAXB %g",maxb);
  for ( i=0; i < *count; i++ ) {
    //printf("POW 10^%f => ", (*storage)[i].b);
    (*storage)[i].intensity = powf(10,(*storage)[i].intensity-maxb);
    //printf("%g\n", (*storage)[i].b);
  }
  return 0;
}

int load_spec_observation(specopts_t *opts) {
  FILE *fh;
  int rc = 0;
  if ( ( fh = fopen(opts->obsfile, "r") ) == NULL ) {
    perror("Failed to open observation file");
    return 12;
  }
  rc = load_catfile(fh, &(opts->observation), &(opts->observationsize),
                    &(opts->observationcount));
  if ( rc > 0 ) rc += 10;
  fclose(fh);
  return 0;
}

int generate_input_files(specopts_t *opts, char *basename, GA_segment *x) {
  char filename[128];
  int i;
  for ( i = 0; i < 2; i++ ) {
    FILE *fh;
    snprintf(filename, sizeof(filename), "%s.%s", basename,
	     input_suffixes[i]);
    if ( ( fh = fopen(filename, "w") ) == NULL ) {
      perror("Failed to open input file");
      return i+1;
    }
    fprintf(fh, opts->template[i], x[0], x[1], x[2]); /* FIXME */
    //B+C/B-C
    //fprintf(fh, opts->template[i], x[0], (x[1]+x[2])/2, (x[1]-x[2])/2); /* FIXME */
    /* Error checking for fprintf? */
    if ( fclose(fh) ) {
      perror("Failed to close input file");
      return i+1;
    }
  }
  return 0;
}

int generate_input_buffers(specopts_t *opts, char *buffers[], size_t bufsizes[], GA_segment *x) {
  int i;
  for ( i = 0; i < 2; i++ ) {
    if ( ( bufsizes[i] = asprintf(&buffers[i], opts->template[i], x[0], x[1], x[2]) ) < 0 )
      return i+1;
  }
  return 0;
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
  case 26: /* drfile */
    ((specopts_t *)settings->ref)->drfile = optarg;
    break;
  case 27:
    ((specopts_t *)settings->ref)->doublerestol = atof(optarg);
    break;
  case 20: /* amin */
    ((specopts_t *)settings->ref)->rangemin[0] = atoi(optarg);
    ((specopts_t *)settings->ref)->userange[0] = 1;
    break;
  case 21: /* amax */
    ((specopts_t *)settings->ref)->rangemax[0] = atoi(optarg);
    ((specopts_t *)settings->ref)->rangetemp[0] = 1;
    break;
  case 22: /* bmin */
    ((specopts_t *)settings->ref)->rangemin[1] = atoi(optarg);
    ((specopts_t *)settings->ref)->userange[1] = 1;
    break;
  case 23: /* bmax */
    ((specopts_t *)settings->ref)->rangemax[1] = atoi(optarg);
    ((specopts_t *)settings->ref)->rangetemp[1] = 1;
    break;
  case 24: /* cmin */
    ((specopts_t *)settings->ref)->rangemin[2] = atoi(optarg);
    ((specopts_t *)settings->ref)->userange[2] = 1;
    break;
  case 25: /* cmax */
    ((specopts_t *)settings->ref)->rangemax[2] = atoi(optarg);
    ((specopts_t *)settings->ref)->rangetemp[2] = 1;
    break;
  default:
    printf("Aborting: %c\n",c);
    abort ();
  }
  return 0;
}

int main(int argc, char *argv[]) {
  GA_session ga;
  GA_settings settings;
  specopts_t specopts;
  time_t starttime = time(NULL); /* To compute runtime */
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
    /** --drfile FILE
     *
     * File containing double resonance data.
     */
    {"drfile",     required_argument, 0, 26},
    /** --drtol TOLERANCE
     *
     * Matching tolerance 
     */
    {"drtol",      required_argument, 0, 27},
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
    {0, 0, 0, 0}
  };
  char *optlog = malloc(128);	/* Initial allocation */
  int rc = 0;
  int i;

  if ( !optlog ) { printf("Out of memory (optlog)\n"); exit(1); }
  GA_defaultsettings(&settings);
  settings.debugmode = 0;	/* Use non-verbose output */
  settings.popsize = 64;
  settings.generations = 200;
  settings.ref = &specopts;
  specopts.basename_out = NULL;
  specopts.template_fn = "template-404";
  specopts.obsfile = "isopropanol-404.cat";
  specopts.spcatbin = "./spcat";
  specopts.bins = BINS;
  specopts.distanceweight = 1.0;
  for ( i = 0; i < SEGMENTS; i++ )
    specopts.rangemin[i] = specopts.rangemax[i] = 0;
  specopts.popfile = NULL;
  specopts.drfile = NULL;
  specopts.doublereslen = 0;
  specopts.doublerestol = 2;
  GA_getopt(argc,argv, &settings, "o:t:m:b:S:w:P:", my_long_options, my_parseopt,
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
  /* Initialize observation storage */
  specopts.observation = NULL;
  specopts.observationsize = 0;
  specopts.observationcount = 0;

  /* Choose an output file */
  if ( specopts.basename_out == NULL ) specopts.basename_out = ".";
  {
    DIR *x = opendir(specopts.basename_out);
    if ( x != NULL ) {		/* Given a directory */
      char *dir = specopts.basename_out;
      if ( asprintf(&specopts.basename_out, "%s/tmp-gaspec-%d",
		    dir, getpid()) == -1 ) {
	printf("basename failed\n");
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
      perror("Failed to open log file");
      free(filename);
      exit(1);
    }
    free(filename);
    dup2(fileno(fh), STDOUT_FILENO); /* Make sure fd #1 is STDOUT */
    if ( ( settings.stdoutfh = fdopen(stdoutfd, "w") ) == NULL ) {
      perror("Failed to reopen stdout");
      exit(1);
    }
    qprintf(&settings, "Details saved in %s.log\n", specopts.basename_out);
  }
  /* Open /dev/null to allow hiding of SPCAT output. Non-portable. */
  {
    FILE *devnull;
    if ( ( devnull  = fopen("/dev/null", "w") ) == NULL ) {
      perror("Can't open /dev/null");
      exit(1);
    }
    if ( ( specopts.stdoutfd = dup(fileno(stdout)) ) == -1 ) {
      perror("Can't dup stdout");
      exit(1);
    }
    if ( ( specopts.devnullfd = fileno(devnull) ) == -1 ) {
      perror("Can't get fileno of devnull");
      exit(1);
    }
  }

  printf("%s\n", optlog+1); free(optlog);

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
      int rc = fscanf(fh, "%*d %*d GD %u %u %u", &specopts.popdata[idx*SEGMENTS], &specopts.popdata[idx*SEGMENTS+1], &specopts.popdata[idx*SEGMENTS+2]);
      if ( rc == EOF ) {
        qprintf(&settings, "load popdata failed: EOF at idx=%u\n", idx);
        return 1;
      }
      idx++;
    }
    fclose(fh);
    for ( idx=0;idx<settings.popsize;idx++ ) {
      printf("001 %03u GD %10u %10u %10u\n", idx, specopts.popdata[idx*SEGMENTS], specopts.popdata[idx*SEGMENTS+1],specopts.popdata[idx*SEGMENTS+2]);
    }
  }
  /* Load double resonance */
  if ( specopts.drfile ) {
    printf("Loading double resonances %s\n", specopts.drfile);
    /* TODO cleanup */
    int dblressize = 0;
    //specopts.doubleres = NULL;
    //specopts.doublereslen = 0;
    FILE *fh = fopen(specopts.drfile, "r");
    if ( fh == NULL ) {
      perror("Failed to open drfile");
      return 1;
    }
    char *line = NULL; size_t linelen = 0;
    while ( getline(&line, &linelen, fh) > 0 ) {
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

  /* Run the genetic algorithm */
  if ( (rc = GA_init(&ga, &settings, SEGMENTS)) != 0 ) {
    qprintf(&settings, "GA_init failed: %d\n", rc);
    return rc;
  }

#if ENUMERATE	       /* Debugging mode: enumeration of all values */
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
#else  /* !ENUMERATE */

  printf("Starting %d generations\n", settings.generations);
  if ( (rc = GA_evolve(&ga, 0)) != 0 ) {
    qprintf(&settings, "GA_evolve failed: %d\n", rc);
    return rc;
  }

  /* Compute runtime */
  starttime = time(NULL)-starttime;
  qprintf(&settings, "Finished.\nTook %u seconds (%f sec/gen)\n",
	  starttime, starttime/(settings.generations+1.0));
#endif
  /* Cleanup */
  if ( (rc = GA_cleanup(&ga)) != 0 ) {
    qprintf(&settings, "GA_cleanup failed: %d\n", rc);
    return rc;
  }
  free(specopts.basename_out);
  free(specopts.observation);
  rc = 0;
  /* TODO: Free memory */
  /* Remove temporary files */
  /* printf("rmdir: %d\n", rmdir(tempdir)); */
  printf("Exiting: %d\n", rc);
  return rc;
}

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
    *r = grayencode(realr);
    //printf("%03d %u < %u < %u\n", i, opts->rangemin[j], realr, opts->rangemax[j]);
  }
  return rc;
}

int GA_finished_generation(const GA_session *ga, int terminating) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  /* Save best result */
  int i;
  unsigned int p;
  int rc = 0;
  FILE *fh;
  char filename[128];
  snprintf(filename, sizeof(filename), "%s.pop", opts->basename_out);
  if ( ( fh = fopen(filename, "w") ) == NULL ) {
    perror("Failed to open population output file");
    return 10;
  }
  for ( p = 0; p < ga->settings->popsize; p++ ) {
    GA_segment x[SEGMENTS];
    for ( i = 0; i < SEGMENTS; i++ )
      x[i] = graydecode(ga->population[p].segments[i]);
    fprintf(fh, "%03u %03u GD %10u %10u %10u\n", ga->generation, p,
            x[0], x[1], x[2]);
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
    perror("Failed to close input file");
    return 11;
  }

  if ( terminating )
    qprintf(ga->settings, "Best result saved in %s\n", opts->basename_out);
  return 0;
}

int GA_fitness_quick(const GA_session *ga, GA_individual *elem) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  GA_segment x[SEGMENTS];
  int i = 0;
  /* WARNING: WE DO THIS TWICE NOW! */
  for ( i = 0; i < SEGMENTS; i++ ) x[i] = graydecode(elem->segments[i]);

  /* Check basic constraints */
  if ( x[0] < x[1] || x[1] < x[2] || x[2] <= 0 ) {
    return 0; // commented out for B+C / B-C
  }
  /*
  GA_segment ideal[] = {2094967296, 1600000000, 100000000};
  GA_segment range[] = {0.05*ideal[0], 0.05*ideal[1], 0.05*ideal[2]};
  */
  for ( i = 0; i < SEGMENTS; i++ ) {
    if ( (x[i] < opts->rangemin[i]) || (x[i] > opts->rangemax[i]) ) {
      /*
      printf("Rejecting segment %03d %010u < %010u < %010u\n", i,
             opts->rangemin[i],x[i],opts->rangemax[i]);
      */
      return 0;
    }
  }
  return 1;
}

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
  GA_segment x[SEGMENTS];
  int i = 0, j = 0;
  FILE *fh;
  int rc = 0;
  /* int xi, yi; */
  double fitness;
  const double binsize = ((double)(RANGEMAX-RANGEMIN))/opts->bins;
  double obsbin[opts->bins], compbin[opts->bins];
  int obsbincount[opts->bins], compbincount[opts->bins];
#ifdef USE_SPCAT_OBJ
  spcs_t spcs;
  char *buffers[NFILE];
  size_t bufsizes[NFILE];
#else
  char filename[128];
#endif
  /* WARNING: WE DO THIS TWICE NOW! */
  for ( i = 0; i < SEGMENTS; i++ ) x[i] = graydecode(elem->segments[i]);

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
    perror("Failed to open file");
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
  snprintf(filename, sizeof(filename), "%s.", thrs->basename_temp);
  tprintf("Running %s %s\n", opts->spcatbin, filename);
  i = invisible_system(opts->devnullfd, 2, opts->spcatbin, filename);

  if ( i != 0 ) return 11;		/* Failed */
  if ( !WIFEXITED(i) || ( WEXITSTATUS(i) != 0 ) )
    return 10;		/* Subprocess did not exit normally. */

  /* Read SPCAT output file */
  snprintf(filename, sizeof(filename), "%s.cat", thrs->basename_temp);
  if ( ( fh = fopen(filename, "r") ) == NULL ) {
    perror("Failed to open  file");
    return 15;
  }
#endif

  thrs->compdatacount = 0;
  rc = load_catfile(fh, &(thrs->compdata), &(thrs->compdatasize),
                  &(thrs->compdatacount));
  if ( rc > 0 ) return 20+rc;
  fclose(fh);

  /* Determine fitness */
  tprintf("COUNTS: %d %d\n", opts->observationcount, thrs->compdatacount);
  fitness = 0;

  /* Check double resonance */
  
  /* BUG -- Need to check against *either* of the dblreses of first item
   * -- they don't need to to all be the same. */
  
  dblres_check *drlist = NULL; int drsize = 0; int drfail = 0;
  for ( i = 0; i < opts->doublereslen; i++ ) { /* For each resonance */
    int drlen = 0; /* Reset the QN list */
    printf("Starting QN:\n");
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
          printf("Looking for QN %d %d %d\n", thrs->compdata[k].qn[m*QN_DIGITS], thrs->compdata[k].qn[m*QN_DIGITS+1], thrs->compdata[k].qn[m*QN_DIGITS+2]);
          /* Check all previously seen quantum numbers */
          for ( l = 0; l < drlen; l++ ) {
            int n = 0;
            /* Specifically, each of the QN_COUNT quantum numbers */
            for ( n = 0; n < QN_COUNT; n++ ) {
              if ( memcmp(&(drlist[l].qn[n*QN_DIGITS]),
                          &(thrs->compdata[k].qn[m*QN_DIGITS]),
                          sizeof(int)*QN_DIGITS) == 0 ) {
                /* We found the QN, skip it unless it was also found last time */
                if ( drlist[l].seen >= j ) {
                  printf("  Match QN %d %d %d\n", drlist[l].qn[n*QN_DIGITS], drlist[l].qn[n*QN_DIGITS+1], drlist[l].qn[n*QN_DIGITS+2]);
                  drlist[l].seen = j+1;
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
            if ( drlen >= drsize ) {
              drsize = (drsize+1)*2;
              //printf("Increasing memory allocation to %u\n", thrs->compdatasize);
              drlist = realloc(drlist, sizeof(dblres_check)*drsize);
              if ( drlist == NULL ) {
	        perror("Out of memory\n");
	        return 35;
              }
            }
            drlist[drlen].seen = 1;
            memcpy(drlist[drlen].qn, thrs->compdata[k].qn,
                   sizeof(drlist[drlen].qn));
            printf("  Added QN %d %d %d %d %d %d\n", drlist[drlen].qn[0], drlist[drlen].qn[1], drlist[drlen].qn[2], drlist[drlen].qn[3], drlist[drlen].qn[4], drlist[drlen].qn[5]);
            drlen++;
            drfail = 0;
          }
          else if ( !found ) printf("  Not found\n");
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
  }

  //double obsmax=0/*, obsmin = 0*/;
  for ( j=0;j<=1;j++ ) {	/* 0=Observed / 1=Generated */
    for ( i=0; i<((j==0)?opts->observationcount:thrs->compdatacount); i++ ) {
      datarow entry;
      if ( j == 0 ) entry = opts->observation[i]; /* Observed */
      else entry = thrs->compdata[i]; /* Generated */
      if ( ( entry.frequency < RANGEMIN ) || ( entry.frequency > RANGEMAX ) )
        continue;
      /* We're within the valid range */
      int bin = floor((entry.frequency-RANGEMIN)/binsize);
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
    fitness += comp;//*binweights[i];
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

int GA_termination(const GA_session *ga) {
  if ( ga->population[ga->fittest].unscaledfitness > -0.00001 )
    return 1;
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

#ifndef USE_SPCAT_OBJ
  /* Use separate temporary files for each thread. */
  opts->basename_temp = make_spec_temp("temp");
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
