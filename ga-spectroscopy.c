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
#include "ga.h"
#include "ga-spectroscopy.usage.h"

#define BINS 600		/* Number of bins to use for comparison */
#define RANGEMIN 8700		/* Ignore below this frequency */
#define RANGEMAX 18300		/* Ignore above this frequency */

#define SEGMENTS 3		/* See also fprintf in generate_input_files */

/* char tempdir[16]; */

const char input_suffixes[2][4] = {"var", "int"};
const char output_suffixes[2][4] = {"out", "cat"};
typedef struct {
  float a, b;
} datarow;
typedef struct {
  datarow *compdata;		/* Data storage for comparison */
  int compdatasize, compdatacount;
  char *basename_temp;		/* Basename of temporary file */
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
} specopts_t;

char *make_spec_temp(char *dir) {
  int i = 0;
  int touched = 0;
  int fnsize = 0;
  char *basename, *filename;
  basename = malloc(fnsize = strlen(dir)+100);
  if ( !basename ) return NULL;
  filename = malloc(fnsize = strlen(dir)+100);
  if ( !filename ) return NULL;
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
  return basename;
}
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

int load_spec_observation(specopts_t *opts) {
  FILE *fh;
  float a,b;
  int rc = 0;
  if ( ( fh = fopen(opts->obsfile, "r") ) == NULL ) {
    perror("Failed to open observation file");
    return 12;
  }
  while ( 1 ) {
    /* Magic incantation for: NUMBER (IGNORED NUMBER) NUMBER (REST OF LINE) */
    rc = fscanf(fh, "%g %*g %g %*[^\n]", &a, &b);
    if ( rc == EOF && ferror(fh) ) {
      perror("Failed to read template file");
      return 13;
    }
    else if ( rc == EOF ) break;
    else if ( rc != 2 ) {
      printf("File format error\n");
      return 14;
    }
    /* Allocate additional memory, if neccessary */
    if ( opts->observationcount >= opts->observationsize ) {
      opts->observationsize = (opts->observationsize+1)*2;
      opts->observation = realloc(opts->observation,
				  sizeof(datarow)*opts->observationsize);
      if ( opts->observation == NULL ) {
	printf("Out of memory\n");
	return 15;
      }
    }
    /* Add point */
    /*
    if ( (observation[observationcount] = malloc(sizeof(float)*2)) == NULL ) {
      printf("Out of memory\n");
      return 16;
    }
    */
    opts->observation[opts->observationcount].a = a;
    opts->observation[opts->observationcount].b = b;
    opts->observationcount++;
  }
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
    /* Error checking for fprintf? */
    if ( fclose(fh) ) {
      perror("Failed to close input file");
      return i+1;
    }
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
  specopts.template_fn = "template";
  specopts.obsfile = "isopropanol.cat";
  specopts.spcatbin = "./spcat";
  specopts.bins = BINS;
  specopts.distanceweight = 1.0;
  GA_getopt(argc,argv, &settings, "o:t:m:b:S:w:", my_long_options, my_parseopt,
	    gaspectroscopy_usage, &optlog);

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
  /* Save best result */
  {
    GA_segment x[SEGMENTS];
    for ( i = 0; i < SEGMENTS; i++ )
      x[i] = graydecode(ga.population[ga.fittest].segments[i]);
    /* Generate SPCAT input file */
    rc = generate_input_files(&specopts, specopts.basename_out, x);
    if ( rc != 0 ) {
      qprintf(&settings, "Final generate_input_files failed: %d\n", rc);
      return rc;
    }
    qprintf(&settings, "Best result saved in %s\n", specopts.basename_out);
  }
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

int GA_fitness(const GA_session *ga, void *thbuf, GA_individual *elem) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  specthreadopts_t *thrs = (specthreadopts_t *)thbuf;
  GA_segment x[SEGMENTS];
  int i = 0;
  char filename[128];
  FILE *fh;
  float a,b;
  int rc = 0;
  /* int xi, yi; */
  double fitness;
  const double binsize = ((double)(RANGEMAX-RANGEMIN))/opts->bins;
  double obsbin[opts->bins], compbin[opts->bins];
  int obsbincount[opts->bins], compbincount[opts->bins];
  for ( i = 0; i < SEGMENTS; i++ ) x[i] = graydecode(elem->segments[i]);

  /* Generate SPCAT input file */
  rc = generate_input_files(opts, thrs->basename_temp, x);
  if ( rc != 0 ) return rc;

  /* Run SPCAT. Append a '.' to the end of the filename so that it
   * doesn't choke on filenames with '.' in them (like ./foo).
   */
#if 1
  /* This way we can avoid running /bin/sh for every fitness
   * evaluation. This doesn't quite work as expected -- ^C often
   * fails if we use the recommended signal handling code. */
  snprintf(filename, sizeof(filename), "%s.", thrs->basename_temp);
  tprintf("Running %s %s\n", opts->spcatbin, filename);
  i = invisible_system(opts->devnullfd, 2, opts->spcatbin, filename);
#else
  snprintf(filename, sizeof(filename), "%s %s.", opts->spcatbin,
	   thrs->basename_temp);
  tprintf("Running %s\n", filename);
  i = invisible_system(opts->devnullfd, 3, "/bin/sh", "-c", filename);
  /* i = system(filename); */
#endif

  if ( i != 0 ) return 11;		/* Failed */
  if ( !WIFEXITED(i) || ( WEXITSTATUS(i) != 0 ) )
    return 10;		/* Subprocess did not exit normally. */

  /* Read SPCAT output file */
  snprintf(filename, sizeof(filename), "%s.cat", thrs->basename_temp);
  if ( ( fh = fopen(filename, "r") ) == NULL ) {
    perror("Failed to open  file");
    return 15;
  }
  thrs->compdatacount = 0;
  while ( 1 ) {
    /* Magic incantation for: NUMBER [IGNORED NUMBER] NUMBER [REST OF LINE] */
    rc = fscanf(fh, "%g %*g %g %*[^\n]", &a, &b);
    if ( rc == EOF && ferror(fh) ) {
      perror("Failed to read template file");
      return 21;
    }
    else if ( rc == EOF ) break;
    else if ( rc != 2 ) {
      qprintf(ga->settings, "File format error\n");
      return 22;
    }
    /* Allocate additional memory, if neccessary */
    if ( thrs->compdatacount >= thrs->compdatasize ) {
      thrs->compdatasize = (thrs->compdatasize+1)*2;
      tprintf("Increasing memory allocation to %u\n", thrs->compdatasize);
      thrs->compdata = realloc(thrs->compdata,
			       sizeof(datarow)*thrs->compdatasize);
      if ( thrs->compdata == NULL ) {
	qprintf(ga->settings, "Out of memory\n");
	return 30;
      }
    }
    thrs->compdata[thrs->compdatacount].a = a;
    thrs->compdata[thrs->compdatacount].b = b;
    thrs->compdatacount++;
  }
  fclose(fh);
  
  /* Determine fitness */
  tprintf("COUNTS: %d %d\n", opts->observationcount, thrs->compdatacount);
  fitness = 0;

  /* Initialize bins */
  for ( i = 0; i < opts->bins; i++ ) {
    obsbin[i] = 0; compbin[i] = 0;
    obsbincount[i] = 0; compbincount[i] = 0;
  }

  for ( i=0; i<opts->observationcount || i<thrs->compdatacount; i++ ) {
    int j=0;
    for ( j=0;j<=1;j++ ) {	/* 0=Observed / 1=Generated */
      datarow entry;
      if ( j == 0 ) {		/* j == 0, observation (Observed) */
	if ( i < opts->observationcount ) entry = opts->observation[i];
	else continue;
      }
      else {			/* j == 1, compdata (Generated) */
	if ( i < thrs->compdatacount ) entry = thrs->compdata[i];
	else continue;
      }
      if ( ( entry.a >= RANGEMIN ) && ( entry.a <= RANGEMAX ) ) {
	/* We're within the valid range */
	int bin = floor((entry.a-RANGEMIN)/binsize);
	if ( bin >= opts->bins ) bin = opts->bins-1;
	if ( j == 0 ) { obsbin[bin] += entry.b; obsbincount[bin]++; }
	else { compbin[bin] += entry.b; compbincount[bin]++; }
      }
    }
  }
  /* Compute bin fitnesses using w*|X_o - X_c|^2 + (1-w)*|N_o - N_c|^2 */
  for ( i=0; i<opts->bins; i++ ) {
    float comp = opts->distanceweight *
      powf(fabs(expf(obsbin[i])-expf(compbin[i])),2) +
      (1-opts->distanceweight)*powf(fabs(obsbincount[i]-compbincount[i]),2);
    fitness += comp;
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

  elem->fitness = -fitness;
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

  /* Use separate temporary files for each thread. */
  opts->basename_temp = make_spec_temp("temp");
  if ( opts->basename_temp == NULL ) return 1;
  qprintf(thread->session->settings, "Using temporary file %s\n",
	  opts->basename_temp);

  thread->ref = (void *)opts;
  return 0;
}

int GA_thread_free(GA_thread *thread) {
  specthreadopts_t *thrs = (specthreadopts_t *)(thread->ref);
  char *filename;

  /* Remove temporary files. */
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
  }
  else qprintf(thread->session->settings,
	       "Could not delete temporary files: Out of memory\n");

  free(thrs->basename_temp);
  free(thrs->compdata);
  free(thrs);
  return 0;
}
