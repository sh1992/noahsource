/** \file ga.c
 *
 * Genetic algorithm implementation file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ga.h"
#include "ga.usage.h"

#ifdef DEBUG
#define PRINT printf
#else
#define PRINT ;
#endif

int GA_defaultsettings(GA_settings *settings) {
    memset(settings, 0, sizeof(GA_settings));
    /* Some random defaults */
    settings->randomseed = urandom();
    settings->popsize = 64;
    settings->generations = 100;
    settings->ref = NULL;
    /* Return success */
    return 0;
}

int GA_init(GA_session *session, GA_settings *settings,
	    unsigned int segmentcount) {
  unsigned int i, j;
  size_t segmentmallocsize = sizeof(GA_segment)*segmentcount;

  /* Seed the PRNG */
  srandom(settings->randomseed);
  /* Zero out the GA_session object and set the fields from the parameters */
  memset(session, 0, sizeof(GA_session));
  session->settings = settings;
  session->popsize = settings->popsize;
  session->fittest = 0;
  /* Allocate the population array (freed in GA_cleanup) */
  session->population = malloc(sizeof(GA_individual)*session->popsize);
  if ( !session->population ) return 1;
  session->newpop     = malloc(sizeof(GA_individual)*session->popsize);
  if ( !session->newpop     ) return 2;
  /* Allocate and fill the sorted list (freed in GA_cleanup) */
  session->sorted     = malloc(sizeof(unsigned int)*session->popsize);
  if ( !session->sorted     ) return 3;
  /* Initialize each individual */
  for ( i = 0; i < session->popsize; i++ ) {
    memset(&(session->population[i]), 0, sizeof(GA_individual));
    session->population[i].segmentcount = segmentcount;
    /* Allocate individual's segments (freed in GA_cleanup) */
    session->population[i].segments = malloc(segmentmallocsize);
    if ( !session->population[i].segments ) return 4;
    /* Generate initial population */
    for ( j = 0; j < segmentcount; j++ )
      session->population[i].segments[j] = (GA_segment)random();
    session->population[i].fitness = 0;
    /* Allocate newpop element */
    memset(&(session->newpop[i]), 0, sizeof(GA_individual));
    session->newpop[i].segmentcount = segmentcount;
    session->newpop[i].segments = malloc(segmentmallocsize);
    if ( !session->newpop[i].segments ) return 5;
    /* Insert into sorted list */
    session->sorted[i] = i;
  }
  /* Allocate the fitness cache (freed in GA_cleanup) */
  session->cachesize = 2*session->popsize;
  session->fitnesscache = malloc(sizeof(GA_individual *)*session->cachesize);
  if ( !session->fitnesscache ) return 6;
  for ( i = 0; i < session->cachesize; i++ ) {
    session->fitnesscache[i] = malloc(sizeof(GA_individual)*2);
    if ( !session->fitnesscache[i] ) return 7;
    memset(session->fitnesscache[i], 0, sizeof(GA_individual)*2);
  }
  /* Evaluate final fitness for each individual */
  if ( GA_checkfitness(session) != 0 ) return 16;
  /* Return success */
  return 0;
}

int GA_cleanup(GA_session *session) {
  unsigned int i;
  for ( i = 0; i < session->popsize; i++ ) {
    free(session->population[i].segments);
    free(session->newpop[i].segments);
  }
  free(session->population);
  free(session->newpop);
  free(session->sorted);
  for ( i = 0; i < session->cachesize; i++ )
    free(session->fitnesscache[i]);
  free(session->fitnesscache);
  return 0;
}

int GA_evolve(GA_session *session,
	      unsigned int generations) {
  if ( generations == 0 ) generations = session->settings->generations;
  unsigned int gen;
  for ( gen = 0; gen < generations; gen++ ) {
    unsigned int i;
    /* Keep top 8 members of the old population. */
    for ( i = 0; i < 8; i++ ) {
      unsigned int j;
      for ( j = 0; j < session->newpop[i].segmentcount; j++ ) {
	/* Insert new segments */
	session->newpop[i].segments[j] = session->population[session->sorted[i]].segments[j];
      }
    }
    /* Create a new population by roulette wheel.
       (Consider: Top half roulette wheel/Keep top 8?) */
    for ( ; i < session->popsize; i+= 2 ) {
      unsigned int a = GA_roulette(session);
      unsigned int b = GA_roulette(session);
      GA_segment olda, oldb;
      GA_segment newa, newb;
      unsigned int j;
      for ( j = 0; j < session->newpop[i].segmentcount; j++ ) {
	int afirst = random()%2;
	int bitpos = random()%GA_segment_size;
	GA_segment mask = (1<<bitpos)-1;
	/* Crossover. Random split & recombine. */
	olda = session->population[afirst ? a : b].segments[j];
	oldb = session->population[afirst ? b : a].segments[j];
	newa = (olda & (~mask)) | (oldb & mask);
	newb = (oldb & (~mask)) | (olda & mask);

	/*
	  PRINT("\nNITM %03d %03d %03d %03d\n",
	      session->generation+1, i, i+1, j);
	*/
	PRINT("XOVR %08x %3d %08x %3d to %08x %08x mask %2d %08x %08x\n",
	      olda,a, oldb,b, newa, newb, bitpos, ~mask, mask);

	/* Mutation. Low-probability bitflip. */
	if ( (double)random()/RAND_MAX < 0.5 ) {
	  afirst = random()%3+1;
	  /* Consider: Bias towards less significant bits */
	  bitpos = random()%GA_segment_size;
	  mask = 1<<bitpos;
	  PRINT("FLP%01x %08x     %08x     ",
		afirst, newa, newb);
	  if ( afirst & 1 ) newa ^= mask;
	  if ( afirst & 2 ) newb ^= mask;
	  PRINT("to %08x %08x mask %2d %08x\n",
		newa, newb, bitpos, mask);
	}
	else PRINT("FLP0 %08x     %08x     \n", newa, newb);
	/* Insert new segments */
	session->newpop[i].segments[j] = newa;
	session->newpop[i+1].segments[j] = newb;
      }
    }
    /* Swap newpop into population */
    GA_individual *temp = session->population;
    session->population = session->newpop;
    session->newpop = temp;
    session->generation++;
    /* Check termination condition. */
    PRINT("BKTS %03d ", session->generation);
    for ( i = 0; i < session->cachesize; i++ ) {
      PRINT("%c", session->fitnesscache[i][0].unscaledfitness ? '#' : '.');
      if ( i%64 == 63 ) PRINT("\n         ");
    }
    /* PRINT("\n"); */

    PRINT("\n");
    if ( GA_checkfitness(session) != 0 ) return 1;
    if ( GA_termination(session) ) return 0;
  }

  return 0;
}

unsigned int GA_roulette(const GA_session *session) {
  double index = 1.0*random()/RAND_MAX;
  unsigned int i;
  /* If all the choices have score 0, assign uniform nonzero scores to
   * all individuals. */
  double uniformroulette = 0;
  /* PRINT("rand %d\n",(int)(index*64)); */
  if ( session->fitnesssum <= 0 ) uniformroulette = 1.0/session->popsize;
  /* PRINT("test %f %d %f\n", session->fitnesssum, session->popsize, uniformroulette); */
  for ( i = 0; i < session->popsize; i++ ) {
    double score = (session->fitnesssum <= 0) ? uniformroulette :
      (1.0*session->population[i].fitness/session->fitnesssum);
    if ( index < score ) {
      /* PRINT("roulette %7.5f < %7.5f %u\n", index, score, i); */
      return i;
    }
    index -= score;
  }
  /* Error. Let's hope this doesn't happen, because there is no return
     code for it */
  return 0;
}

int GA_comparator(const void *a, const void *b) {
  /* Static variable used to hold session, neccessary due to qsort's
     calling convention */
  static GA_session *session;
  if ( a && !b ) {
    session = (GA_session *)a;
    return 0;
  }
  else if ( !session || !a || !b ) return 0;
  int x = *(unsigned int *)a, y = *(unsigned int *)b;
  if ( session->population[x].fitness < session->population[y].fitness )
    return 1;
  else if ( session->population[x].fitness > session->population[y].fitness )
    return -1;
  else return 0; /* x-y; */	/* If equal sort by index to preserve order */
}

int GA_checkfitness(GA_session *session) {
  unsigned int i, j;
  int rc;
  double min, max;
  double offset, scale, scalelen;
  int scaleidx;
  unsigned int fevs = 0;
  session->fittest = 0;
  session->fitnesssum = 0;
  /* Evaluate fitness for each individual */
  for ( i = 0; i < session->popsize; i++ ) {
    int found = 0;
    GA_segment hashtemp = 0;
    unsigned int hashbucket = 0;

    /* Hash the individual to determine caching location */
    for ( j = 0; j < session->population[i].segmentcount; j++ )
      hashtemp ^= session->population[i].segments[j];
    hashbucket = hashtemp % session->cachesize;

    /* Check in the cache for an earlier computation of the fitness value */
    for ( j = 0; j < 2; j++ ) {
      if ( ( session->fitnesscache[hashbucket][j].unscaledfitness != 0 ) &&
	   !memcmp(session->population[i].segments,
		   session->fitnesscache[hashbucket][j].segments,
		   sizeof(GA_segment)*session->population[i].segmentcount) ) {
	session->population[i].fitness =
	  session->fitnesscache[hashbucket][j].fitness;
	found = 1 + j;
	break;
      }
    }

    /* Also check the earlier entries in the new population */
    if ( !found ) {
      for ( j = 0; j < i; j++ ) {
	if ( !memcmp(session->population[i].segments,
		     session->population[j].segments,
		     sizeof(GA_segment)*session->population[i].segmentcount)) {
	  session->population[i].fitness = session->population[j].fitness;
	  found = 5;
	  break;
	}
      }
    }

    /* Also check the old population (ironically stored in newpop) */
    if ( !found && session->generation > 0 ) {
      for ( j = 0; j < session->popsize; j++ ) {
	if ( !memcmp(session->population[i].segments,
		     session->newpop[j].segments,
		     sizeof(GA_segment)*session->population[i].segmentcount) ) {
	  session->population[i].fitness = session->newpop[j].unscaledfitness;
	  found = 6;
	  break;
	}
      }
    }

    /* If not found in the cache, compute the fitness value */
    if ( !found ) {
      fevs++;
      if ( ((rc = GA_fitness(session, &session->population[i])) != 0)
	   || isnan(session->population[i].fitness) ) {
	PRINT("fitness error %u %f => %d\n",
	      session->population[i].segments[0],
	      session->population[i].fitness, rc);
	return 1;
      }
    }

    /* Announce caching status for segment use */ 
    PRINT("FND%1d hash %08x bucket %3d orig %f\n",
	  found, hashtemp, hashbucket, session->population[i].fitness);
    /* Save the fitness in the cache */
    if ( ( found == 0 ) || ( found > 1 ) ) {
      if ( session->fitnesscache[hashbucket][0].unscaledfitness != 0 )
	/* First demote the first-level hash to second-level */
	memcpy(&(session->fitnesscache[hashbucket][1]),
	       &(session->fitnesscache[hashbucket][0]), sizeof(GA_individual));
      memcpy(&(session->fitnesscache[hashbucket][0]),
	     &(session->population[i]), sizeof(GA_individual));
      session->fitnesscache[hashbucket][0].unscaledfitness = 1;
    }

    /* Track minimum and maximum fitnesses */
    if ( i == 0 || session->population[i].fitness < min )
      min = session->population[i].fitness;
    if ( i == 0 || session->population[i].fitness > max )
      max = session->population[i].fitness;
    /* session->fitnesssum += session->population[i].fitness; */
  }
  /* Show statistics of caching effectiveness */
  PRINT("FEVS %u  -%u\n", fevs, i-fevs);

  /* Scale fitnesses to range 0.5-1.0 */
  offset = 0-min;	     /* Shift range for lower bound at zero */
  scale = max-min;	     /* Scale range for length */
  /* Scale range length. Use a shorter length for higher generations
   * to quickly remove poor contenders at the beginning and preserve
   * diversity at the end. */
  scalelen = 0.8;
  scaleidx = 5;
  if ( session->generation <= scaleidx )
    scalelen += (1-scalelen)*(scaleidx-session->generation)/scaleidx;
  /* PRINT("min %10u => %f\nmax %10u => %f\noffset   %f   scale %f\n",0,min,0,max,offset,scale); */

  for ( i = 0; i < session->popsize; i++ ) {
    session->population[i].unscaledfitness = session->population[i].fitness;
    if ( 0 )			/* Shift but don't scale fitness values */
      session->population[i].fitness += offset;
    else			/* Shift and scale fitness values */
      session->population[i].fitness =
	(session->population[i].fitness+offset)*scalelen/scale+(1-scalelen);
    if ( session->population[i].fitness >
	 session->population[session->fittest].fitness ) session->fittest = i;
    session->fitnesssum += session->population[i].fitness;
    PRINT("%-4s %03u %03u   GD 000 %10u score %9.7f orig %15.3f\n",
	  (session->fittest == i) ? "FITM" : "ITEM", session->generation, i,
	  graydecode(session->population[i].segments[0]),
	  session->population[i].fitness,
	  session->population[i].unscaledfitness);
    /* Use more lines for additional segments */
    for ( j = 1; j < session->population[i].segmentcount; j++ )
      PRINT("               GD %03d %10u\n", j,
	    graydecode(session->population[i].segments[j]));
  }

  /* Sort the sorted list. */
  GA_comparator(session, NULL);	/* Initialize comparator */
  printf("%u\n", session->sorted[0]);
  qsort(&(session->sorted[0]), session->popsize, sizeof(unsigned int),
	GA_comparator);
  printf("%u\n", session->sorted[0]);
  session->fittest = session->sorted[0]; /* Since sorting seems to work */
  if ( session->sorted[0] != session->fittest ) {
    /* Did not choose same item. Do full comparison, return error if fails */
    /* FIXME - Completely broken!!! (Fails for ga-numbers) */
    int mc = memcmp(&(session->population[session->sorted[0]].segments[0]),
		    &(session->population[session->fittest  ].segments[0]),
		    sizeof(GA_segment)*session->population[session->fittest].segmentcount);
    if ( mc ) {
      PRINT("Sort failed: S=%u vs F=%u\n",
	    session->sorted[0], session->fittest);
      return 2;
    }
  }
  /* Display the best individual */
  PRINT("BEST %03u %03u   GD 000 %10u score %9.7f orig %15.3f\n",
	session->generation, session->fittest,
	graydecode(session->population[session->fittest].segments[0]),
	session->population[session->fittest].fitness,
	session->population[session->fittest].unscaledfitness);
  /* Use more lines for additional segments */
  for ( j = 1; j < session->population[session->fittest].segmentcount; j++ )
    PRINT("               GD %03d %10u\n", j,
	  graydecode(session->population[session->fittest].segments[j]));
  PRINT("\n");
  return 0;
}

GA_segment graydecode(GA_segment gray) { 
  GA_segment bin;
  for ( bin = 0; gray; gray >>= 1 ) bin ^= gray; 
  return bin; 
}

GA_segment grayencode(GA_segment bin) { 
  return bin ^ (bin >> 1); 
}

unsigned int urandom() {
  unsigned int out;
  FILE *fh;
  if ( !(fh = fopen("/dev/urandom", "r")) ) return 0;
  if ( fread(&out, sizeof(out), 1, fh) != 1 ) return 0;
  fclose(fh);
  return out;
}

/** Helper function for GA_getopt.
 *
 * \param argc,argv       Command-line arguments, from main.
 * \param settings        GA_settings argument to store configuration in.
 * \param optstring       getopt optstring.
 * \param long_options    getopt options list.
 * \param global_count    The number of non-problem-specific options.
 * \param my_parse_option Option parsing function for problem-specific options.
 * \param my_usage        Usage message for problem-specific options.
 *
 * \see GA_getopt
 *
 * \returns 0 for success.
 */
int GA_run_getopt(int argc, char * const argv[], GA_settings *settings,
		  const char *optstring, const struct option *long_options,
		  int global_count, GA_my_parseopt_t my_parse_option,
		  char *my_usage) {
  int c;
  while (1) {
   /* getopt_long stores the option index here. */
   int option_index = 0;

   c = getopt_long(argc,argv, optstring, long_options, &option_index);

   /* Detect the end of the options. */
   if (c == -1)
     break;
   switch (c) {
   case 0:
     if ( option_index < global_count ) { /* Global option, handle here */
       /* If this option set a flag, do nothing else now. */
       if (long_options[option_index].flag != 0)
	 break;
       printf ("long_option %s", long_options[option_index].name);
       if (optarg)
	 printf (" with arg %s", optarg);
       printf ("\n");
     }
     else if (my_parse_option) /* Forward to my_parse_option */
       (*my_parse_option)(long_options, settings, c, option_index);
     break;
   case 'c':
     /* Config file - do nothing, since already parsed */
     break;
   case 'g':			/* Number of generations */
     settings->generations = atoi(optarg);
     break;
   case 'p':			/* Population size */
     settings->popsize = atoi(optarg);
     break;
   case 'h':
   case '?':
     /* getopt_long already printed an error message. */
     printf("Usage: %s [options]\n\nGeneral GA options:\n%s\nProblem-specific options:\n%s", argv[0], ga_usage, my_usage);
     exit(c == '?' ? 1 : 0);
     break;
   case 1:
     printf("Internal error/1\n");
     exit(1);
     break;
   default:			/* Revert to "my_" parser */
     if ( my_parse_option )
       (*my_parse_option)(long_options, settings, c, option_index);
     break;
   }
  }
  return 0;
}

int GA_getopt(int argc, char * const argv[], GA_settings *settings,
	      const char *my_optstring, const struct option *my_long_options,
	      GA_my_parseopt_t my_parse_option, char *my_usage) {
  /* After updating usage comments, run generate-usage.pl on this file
   * to update usage message header files.
   */
  static const struct option global_long_options[] = {
    /** -c, --config FILE
     *
     * Load a configuration file. Configuration files are formatted
     * similarly to the command line. For example, a configuration
     * file to set the population size to 64 would have the line
     * "population 64".
     */
    {"config",  required_argument, NULL, 'c'}, /* Must be first option */
    /** -h, --help
     *
     * Display usage message.
     */
    {"help", no_argument, NULL, 'h'},
    /** -g, --generations NUMBER
     *
     * The number of generations to run for before stopping.
     */
    {"generations", required_argument, NULL, 'g'},
    /** -p, --population NUMBER
     *
     * The number of individuals in the population.
     */
    {"population", required_argument, NULL, 'p'},
    /*
      {"verbose", no_argument,       &verbose_flag, 1},
      {"brief",   no_argument,       &verbose_flag, 0},
    */
    {0, 0, 0, 0}
  };
  static const char *global_optstring = "c:g:p:h";
  int c;
  struct option *long_options;
  struct option config_options[2] = {global_long_options[0],{0,0,0,0}};
  char *optstring = NULL;
  char **temp_argv = malloc(sizeof(char *)*argc); /* NULL check is below */
  char *loadconfig = NULL;

  /* Combine global_long_options and my_long_options */
  int global_count = 0;
  int my_count = 0;
  for ( global_count = 0; ; global_count++ ) /* Do not include 0 in count */
    if ( global_long_options[global_count].name == 0 ) break;
  for ( my_count = 1; ; my_count++ ) /* Include 0 in count */
    if ( my_long_options[my_count-1].name == 0 ) break;
  long_options = malloc(sizeof(struct option)*(global_count+my_count));
  if ( !long_options ) { printf("Out of memory (long_options)\n"); return 1; }
  memcpy(long_options, global_long_options,
	 sizeof(struct option)*global_count);
  memcpy(long_options+global_count, my_long_options,
	 sizeof(struct option)*my_count);
  /* Combine global_optstring and my_optstring */
  optstring = malloc(strlen(global_optstring)+strlen(my_optstring)+1);
  if ( !optstring ) { printf("Out of memory (optstring)\n"); return 1; }
  strcpy(optstring, global_optstring);
  strcat(optstring, my_optstring);

  /* Search for config file specifications */
  optind = 1; opterr = 0;
  /* Use temp_argv due to permutation by getopt */
  if ( !temp_argv ) { printf("Out of memory (temp_argv)\n"); return 1; }
  memcpy(temp_argv, argv, sizeof(char *)*argc);
  /* Search for config file parameter */
  while (1) {
   /* getopt_long stores the option index here. */
   int option_index = 0;

   c = getopt_long(argc,temp_argv, "c:", config_options,
		   &option_index);
   if ( c == 'c' ) {
     if ( loadconfig ) {
       printf("Too many configuration files specified\n");
       exit(1);
     }
     loadconfig = optarg;
   }
   else if (c == -1)		/* Detect the end of the options. */
     break;
  }
  free(temp_argv);
  opterr = 1;
  /* Now load configuration file */
  if ( loadconfig ) {
    FILE *fh;
    if ( (fh = fopen(loadconfig, "r") ) == NULL ) {
      char *str;
      asprintf(&str, "%s: %s", argv[0], loadconfig);
      perror(str);
      free(str);
      exit(1);
    }
    /* printf("Loading config file %s\n", loadconfig); */
    while ( 1 ) {
      char *key = NULL; char *value = NULL;
      char *fake_argv[3];
      int rc = fscanf(fh, " %ms%*[ \t]%m[^\n]", &key, &value);
      /* printf("%d %p %p\n", rc, key, value); */
      if ( rc == EOF && ferror(fh) ) {
	char *str;
	asprintf(&str, "%s: %s", argv[0], loadconfig);
	perror(str);
	free(str);
	exit(1);
      }
      else if ( rc == EOF ) break;
      else if ( rc < 1 || rc > 2 || !key ) {
	printf("%s: %s: Syntax error\n", argv[0], loadconfig);
	exit(1);
      }
      /* printf("'%s' '%s'\n", key, value ? value : ""); */
      fake_argv[0] = argv[0];
      if ( asprintf(&fake_argv[1], "--%s", key) == -1 ) {
	printf("%s: %s: Out of Memory\n", argv[0], loadconfig);
	exit(1);
      }
      if ( rc >= 2 ) fake_argv[2] = value;
      optind = 1;
      GA_run_getopt(rc+1, fake_argv, settings, optstring, long_options,
		    global_count, my_parse_option, my_usage);
      free(fake_argv[1]);
      free(key);
      free(value);
    }
    fclose(fh);
  }

  optind = 1;
  GA_run_getopt(argc, argv, settings, optstring, long_options,
		global_count, my_parse_option, my_usage);
  free(long_options);
  return 0;
}

