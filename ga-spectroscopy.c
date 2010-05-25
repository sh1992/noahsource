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
#include <dirent.h>
#include <errno.h>
#include "ga.h"
#include "ga-spectroscopy.usage.h"

#define BINS 600		/* Number of bins to use for comparison */
#define RANGEMIN 8700		/* Ignore below this frequency */
#define RANGEMAX 18300		/* Ignore above this frequency */

/* char tempdir[16]; */

char spec_template[2][2048];
char input_suffixes[2][4] = {"var", "int"};
const char output_suffixes[2][4] = {"out", "cat"};
int load_spec_templates(char *template_filename) {
  int i;
  memset(spec_template, 0, sizeof(spec_template));
  for ( i = 0; i < 2; i++ ) {
    FILE *fh;
    char filename[128];
    snprintf(filename, sizeof(filename), "%s.%s",
	     template_filename, input_suffixes[i]);
    /* Load template */
    if ( ( fh = fopen(filename, "r") ) == NULL ) {
      perror("Failed to open template file");
      return i+1;
    }
    fread(spec_template[i], sizeof(spec_template[i]), 1, fh);
    if ( ferror(fh) ) {
      perror("Failed to read template file");
      return i+1;
    }
    fclose(fh);
  }
  return 0;
}

typedef struct {
  float a, b;
} datarow;

datarow *compdata;		/* Data storage for comparison */
int compdatasize = 0, compdatacount = 0;
datarow *observation;		/* Data storage for observation */
int observationsize = 0, observationcount = 0;
int load_spec_observation(char *observation_filename) {
  FILE *fh;
  float a,b;
  int rc = 0;
  if ( ( fh = fopen(observation_filename, "r") ) == NULL ) {
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
    if ( observationcount >= observationsize ) {
      observationsize = (observationsize+1)*2;
      observation = realloc(observation, sizeof(datarow)*observationsize);
      if ( observation == NULL ) {
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
    observation[observationcount].a = a;
    observation[observationcount].b = b;
    observationcount++;
  }
  fclose(fh);
  return 0;
}

typedef struct {
  char *basename;		/* Basename of temporary file */
  char *template;
  char *obsfile;
  char *spcatbin;
  unsigned int bins;
} specopts_t;
specopts_t specopts;

int generate_input_files(GA_segment *x) {
  char filename[128];
  int i;
  for ( i = 0; i < 2; i++ ) {
    FILE *fh;
    snprintf(filename, sizeof(filename), "%s.%s", specopts.basename,
	     input_suffixes[i]);
    if ( ( fh = fopen(filename, "w") ) == NULL ) {
      perror("Failed to open input file");
      return i+1;
    }
    fprintf(fh, spec_template[i], x[0], x[1], x[2]);
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
    ((specopts_t *)settings->ref)->basename = optarg;
    break;
  case 't':
    ((specopts_t *)settings->ref)->template = optarg;
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

  default:
    printf("Aborting: %c\n",c);
    abort ();
  }
  return 0;
}

int main(int argc, char *argv[]) {
  GA_session ga;
  GA_settings settings;
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
    {0, 0, 0, 0}
  };
  int rc = 0;
  int i;

  GA_defaultsettings(&settings);
  settings.popsize = 64;
  settings.generations = 200;
  settings.ref = &specopts;
  specopts.basename = NULL;
  specopts.template = "template";
  specopts.obsfile = "isopropanol.cat";
  specopts.spcatbin = "./spcat";
  specopts.bins = BINS;
  GA_getopt(argc,argv, &settings, "o:t:m:b:S:", my_long_options, my_parseopt,
	    gaspectroscopy_usage);

  /* Load SPCAT .int & .var input template files */
  printf("Loading template files %s.{var,int}\n", specopts.template); 
  if ( (rc = load_spec_templates(specopts.template)) != 0 ) {
    printf("load_spec_templates failed: %d\n", rc);
    return rc;
  }
  /* Load observed data file */
  printf("Loading observation file %s\n", specopts.obsfile);
  if ( (rc = load_spec_observation(specopts.obsfile)) != 0 ) {
    printf("load_spec_observation failed: %d\n", rc);
    return rc;
  }
  /* Choose temporary file */
  if ( specopts.basename == NULL ) specopts.basename = ".";
  {
    DIR *x = opendir(specopts.basename);
    if ( x != NULL ) { /* || ( errno != ENOTDIR) ) { */
      char *dir = specopts.basename;
      if ( asprintf(&specopts.basename, "%s/tmp-gaspec-%d",
		    dir, getpid()) == -1 ) {
	printf("basename failed\n");
	exit(1);
      }
    }
    else {
      specopts.basename = strdup(specopts.basename);
      if ( !specopts.basename ) {
	printf("Out of memory (basename dup)\n");
	exit(1);
      }
    }
  }

  printf("Using temporary file %s\n", specopts.basename);
  printf("Using %d bins\n", specopts.bins);

  /* Run the genetic algorithm */
  if ( (rc = GA_init(&ga, &settings, 3)) != 0 ) {
    printf("GA_init failed: %d\n", rc);
    return rc;
  }
  if ( (rc = GA_evolve(&ga, 0)) != 0 ) {
    printf("GA_evolve failed: %d\n", rc);
    return rc;
  }

  {
    /* Remove stray temporary files */
    char *filename;
    if ( ( filename = malloc(strlen(specopts.template)+5) ) != NULL ) {
      int i;
      for ( i = 0; i < 2; i++ ) {
	sprintf(filename, "%s.%s", specopts.basename, output_suffixes[i]);
	unlink(filename);
      }
    }
    else printf("Out of memory (nonfatal, but cleanup failed)\n");
  }

  /* Save best result */
  {
    GA_segment x[3];
    for ( i = 0; i<3; i++ )
      x[i] = graydecode(ga.population[ga.fittest].segments[i]);
    /* Generate SPCAT input file */
    rc = generate_input_files(x);
    if ( rc != 0 ) {
      printf("Final generate_input_files failed: %d\n", rc);
      return rc;
    }
    printf("Best result saved in %s\n", specopts.basename);
  }
  /* Cleanup */
  if ( (rc = GA_cleanup(&ga)) != 0 ) {
    printf("GA_cleanup failed: %d\n", rc);
    return rc;
  }
  free(specopts.basename);
  free(compdata);
  free(observation);
  rc = 0;
  /* TODO: Free memory */
  /* Remove temporary files */
  /* printf("rmdir: %d\n", rmdir(tempdir)); */
  printf("Exiting: %d\n", rc);
  return rc;
}

int GA_fitness(const GA_session *ga, GA_individual *elem) {
  specopts_t *opts = (specopts_t *)ga->settings->ref;
  GA_segment x[3];
  int i = 0;
  char filename[128];
  FILE *fh;
  float a,b;
  int rc = 0;
  //int xi, yi;
  double fitness;
  const double binsize = ((double)(RANGEMAX-RANGEMIN))/opts->bins;
  double obsbin[opts->bins], compbin[opts->bins];
  int obsbincount[opts->bins], compbincount[opts->bins];
  for ( i = 0; i<3; i++ ) x[i] = graydecode(elem->segments[i]);

  /* Generate SPCAT input file */
  rc = generate_input_files(x);
  if ( rc != 0 ) return rc;
  
  /* Run SPCAT. Append a '.' to the end of the filename so that it
   * doesn't choke on filenames with '.' in them (like ./foo).
   */
  snprintf(filename, sizeof(filename), "'%s' %s. > /dev/null",
	   specopts.spcatbin, specopts.basename);
  printf("Running %s\n", filename);
  fflush(NULL); /* Flush output streams to avoid weirdness with printf. */
  i = system(filename);
  if ( i != 0 ) return 11;		/* Failed */
  if ( !WIFEXITED(i) || ( WEXITSTATUS(i) != 0 ) )
    return 10;		/* Subprocess did not exit normally. */

  /* Read SPCAT output file */
  snprintf(filename, sizeof(filename), "%s.cat", specopts.basename);
  if ( ( fh = fopen(filename, "r") ) == NULL ) {
    perror("Failed to open  file");
    return 12;
  }
  compdatacount = 0;
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
    if ( compdatacount >= compdatasize ) {
      compdatasize = (compdatasize+1)*2;
      printf("Increasing memory allocation to %u\n", compdatasize);
      compdata = realloc(compdata, sizeof(datarow)*compdatasize);
      if ( compdata == NULL ) {
	printf("Out of memory\n");
	return 15;
      }
    }
    compdata[compdatacount].a = a;
    compdata[compdatacount].b = b;
    compdatacount++;
  }
  fclose(fh);
  
  /* Determine fitness */
  printf("COUNTS: %d %d\n", observationcount, compdatacount);
  fitness = 0;

  for ( i = 0; i < opts->bins; i++ ) {
    obsbin[i] = 0; compbin[i] = 0;
    obsbincount[i] = 0; compbincount[i] = 0;
  }

  for ( i=0; i<observationcount || i<compdatacount; i++ ) {
    int j=0;
    for ( j=0;j<=1;j++ ) {	/* Observed/Generated */
      datarow entry;
      if ( j == 0 ) {		/* j == 0, observation (Observed) */
	if ( i < observationcount ) entry = observation[i];
	else continue;
      }
      else {			/* j == 1, compdata (Generated) */
	if ( i < compdatacount ) entry = compdata[i];
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
  /* ???: Divide bin values by bincounts? */
  for ( i=0; i<opts->bins; i++ ) {
    float comp = sqrtf(powf(fabs(expf(obsbin[i])-expf(compbin[i])),2));
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

int GA_termination(const GA_session *session) {
  if ( session->population[session->fittest].unscaledfitness > -0.00001 )
    return 1;
  return 0;
}

