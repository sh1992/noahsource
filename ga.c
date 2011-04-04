/** \file ga.c
 *
 * Genetic algorithm implementation file.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include "ga.h"
#include "ga.usage.h"

static int32_t randtbl[32] = {
  3,
  -1726662223, 379960547, 1735697613, 1040273694, 1313901226,
  1627687941, -179304937, -2073333483, 1780058412, -1989503057,
  -615974602, 344556628, 939512070, -1249116260, 1507946756,
  -812545463, 154635395, 1388815473, -1926676823, 525320961,
  -1009028674, 968117788, -123449607, 1284210865, 435012392,
  -2017506339, -911064859, -370259173, 1132637927, 1398500161,
  -205601318,
};

#if THREADS
static void *GA_do_thread (void * arg);
/* Consider abandoning this mutex in favor of flockfile on */
static pthread_mutex_t iomutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int astrcat(char **s, const char *append);

int GA_defaultsettings(GA_settings *settings) {
    memset(settings, 0, sizeof(GA_settings));
    /* Some random defaults */
    settings->randomseed = urandom();
    settings->popsize = 64;
    settings->generations = 100;
    settings->mutationrate = 0.015625; /* Expecting 1/2 in 32 bits flipped */
    settings->mutationweight = 0;
    settings->elitism = 8;
    settings->dynmut = 0;
    settings->dynmut_width = 50;
    settings->dynmut_factor = 10;
    settings->dynmut_min = 0.005;
    settings->dynmut_range = 0.5; /* Derived from notamol-20100715a runs */
    settings->ref = NULL;
#ifdef THREADS
    settings->threadcount = 2;
#else
    settings->threadcount = 1;
#endif
    /* Default debugmode to true, since the infrastructure to renumber
     * file descriptors is not implemented in this library */
    settings->debugmode = 1;
    /* Return success */
    return 0;
}

int GA_init(GA_session *session, GA_settings *settings,
	    unsigned int segmentcount) {
  unsigned int i, j, rc;
  size_t segmentmallocsize = sizeof(GA_segment)*segmentcount;

  /* Enforce even numbers by rounding (down) to next even number */
  if ( settings->elitism % 2 ) {
    settings->elitism--;
    qprintf(settings, "Elitism must be even, new value is %u\n",
	    settings->elitism);
  }
  if ( settings->popsize % 2 ) {
    settings->popsize--;
    qprintf(settings, "Population size must be even, new value is %u\n",
	    settings->popsize);
  }

  /* Zero out the GA_session object */
  memset(session, 0, sizeof(GA_session));

  /* Seed the PRNG */
  memcpy(&session->randtbl, randtbl, sizeof(session->randtbl));
  qprintf(settings, "SEED %u\n", settings->randomseed);
  /* Initialize random_r() to be the same as random(). This is how
   * it's done in my version of libc (eglibc-2.11.1, Ubunu 10.04) */
  if ( initstate_r(settings->randomseed, (char *)(session->randtbl),
		   128, &session->rs) ) perror("initstate_r");

  /* Set the fields from the parameters */
  session->settings = settings;
  session->fittest = 0;
  /* Allocate the population array (freed in GA_cleanup) */
  session->population = malloc(sizeof(GA_individual)*settings->popsize);
  if ( !session->population ) return 1;
  session->oldpop     = malloc(sizeof(GA_individual)*settings->popsize);
  if ( !session->oldpop     ) return 2;
  /* Allocate and fill the sorted list (freed in GA_cleanup) */
  session->sorted     = malloc(sizeof(unsigned int)*settings->popsize);
  if ( !session->sorted     ) return 3;
  /* Initialize each individual */
  for ( i = 0; i < settings->popsize; i++ ) {
    /* Allocate individual's segments (freed in GA_cleanup) */
    memset(&(session->population[i]), 0, sizeof(GA_individual));
    session->population[i].segmentcount = segmentcount;
    session->population[i].segments = malloc(segmentmallocsize);
    if ( !session->population[i].segments ) return 4;
    session->population[i].gdsegments = malloc(segmentmallocsize);
    if ( !session->population[i].gdsegments ) return 4;

    /* Allocate oldpop element */
    memset(&(session->oldpop[i]), 0, sizeof(GA_individual));
    session->oldpop[i].segmentcount = segmentcount;
    session->oldpop[i].segments = malloc(segmentmallocsize);
    if ( !session->oldpop[i].segments ) return 5;
    session->oldpop[i].gdsegments = malloc(segmentmallocsize);
    if ( !session->oldpop[i].gdsegments ) return 5;

    /* Generate initial population */
    do {
      printf("Generating initial pop %03d\n", i);
      for ( j = 0; j < segmentcount; j++ ) {
        int r;
        /*
        if ( random_r(&session->rs, &r) ) perror("random");
        */
        if ( GA_random_segment(session, i, j, &r) ) perror("random");
        /* Insert new segment, also graydecode it. */
        session->population[i].segments[j] = (GA_segment)r;
        session->population[i].gdsegments[j] = graydecode((GA_segment)r);
      }
      session->population[i].fitness = 0;
    } while ( !GA_fitness_quick(session, &session->population[i]) );
    /* Insert into sorted list */
    session->sorted[i] = i;
  }
  /* Allocate the fitness cache (freed in GA_cleanup) */
  session->cachesize = 2*settings->popsize;
  session->fitnesscache = malloc(sizeof(GA_individual *)*session->cachesize);
  if ( !session->fitnesscache ) return 6;
  for ( i = 0; i < session->cachesize; i++ ) {
    session->fitnesscache[i] = malloc(sizeof(GA_individual)*2);
    if ( !session->fitnesscache[i] ) return 7;
    memset(session->fitnesscache[i], 0, sizeof(GA_individual)*2);
    for ( j = 0; j < 2; j++ ) {
      session->fitnesscache[i][j].segments = malloc(segmentmallocsize);
      if ( !session->fitnesscache[i][j].segments ) return 8;
      /* Fitness cache does not store graydecoded segments */
    }
  }
  /* Allocate the dynmut buffer (freed in GA_cleanup) */
  session->dynmut_trailing =
    malloc(sizeof(double)*session->settings->dynmut_width);
  if ( !session->dynmut_trailing ) return 9;
  /* Check settings for validity */
  if ( settings->mutationrate < 0 || settings->mutationrate > 1 ) return 31;
  if ( session->settings->dynmut_min+session->settings->dynmut_range > 1 )
      return 32;
  if ( settings->threadcount < 1 ) return 50;
#if THREADS
  session->inflag = 0;
  session->outflag = 0;

  /* Initialize mutexes */
  rc = pthread_mutex_init(&(session->inmutex), NULL);
  if ( rc ) { qprintf(session->settings,
		      "GA_init: mutex_init(in): %d\n", rc); exit(1); }
  rc = pthread_mutex_init(&(session->outmutex), NULL);
  if ( rc ) { qprintf(session->settings,
		      "GA_init: mutex_init(out): %d\n", rc); exit(1); }
  rc = pthread_cond_init(&(session->incond), NULL);
  if ( rc ) { qprintf(session->settings,
		      "GA_init: cond_init(in): %d\n", rc); exit(1); }
  rc = pthread_cond_init(&(session->outcond), NULL);
  if ( rc ) { qprintf(session->settings,
		      "GA_init: cond_init(out): %d\n", rc); exit(1); }
  rc = pthread_mutex_init(&(session->cachemutex), NULL);
  if ( rc ) { qprintf(session->settings,
		      "GA_init: mutex_init(cache): %d\n", rc); exit(1); }
#else
  /* No threads supported */
  if ( settings->threadcount > 1 ) return 50; rc = 0;
#endif
  session->threads = malloc(sizeof(GA_thread)*settings->threadcount);
  if ( !session->threads ) return 51;
  for ( i = 0; i < session->settings->threadcount; i++ ) {
    int rc = 0;
    session->threads[i].session = session;
    session->threads[i].number = i+1;
#if THREADS
    if ( pthread_create (&session->threads[i].threadid, NULL,
			 GA_do_thread, (void *)&(session->threads[i])) != 0 ) {
      return 51;
    }
#endif
    rc = GA_thread_init(&session->threads[i]);
    if ( rc != 0 ) return 55;
  }
  /* Evaluate final fitness for each individual */
  if ( GA_checkfitness(session) != 0 ) return 90;
  /* Return success */
  return 0;
}

int GA_cleanup(GA_session *session) {
  unsigned int i, j;
  /*
    if ( random_r(&session->rs, &i) ) perror("random");
    qprintf(session->settings, "RNDB %u\n", i);
  */
  for ( i = 0; i < session->settings->threadcount; i++ ) {
    GA_thread_free(&session->threads[i]);
  }
  free(session->threads);
  for ( i = 0; i < session->settings->popsize; i++ ) {
    free(session->population[i].segments);
    free(session->population[i].gdsegments);
    free(session->oldpop[i].segments);
    free(session->oldpop[i].gdsegments);
  }
  free(session->population);
  free(session->oldpop);
  free(session->sorted);
  for ( i = 0; i < session->cachesize; i++ ) {
    for ( j = 0; j < 2; j++ ) free(session->fitnesscache[i][j].segments);
    free(session->fitnesscache[i]);
  }
  free(session->fitnesscache);
  free(session->dynmut_trailing);
  return 0;
}

int GA_evolve(GA_session *session, unsigned int generations) {
  if ( generations == 0 ) generations = session->settings->generations;
  unsigned int gen;
  for ( gen = 0; gen < generations; gen++ ) {
    unsigned int i;

    /* Swap population into oldpop */
    GA_individual *temp = session->population;
    session->population = session->oldpop;
    session->oldpop = temp;

    /* Keep top 8 members of the old population. */
    for ( i = 0; i < session->settings->elitism; i++ ) {
      unsigned int j;
      for ( j = 0; j < session->population[i].segmentcount; j++ ) {
	/* Insert new segments */
	session->population[i].segments[j] = session->oldpop[session->sorted[i]].segments[j];
	session->population[i].gdsegments[j] = session->oldpop[session->sorted[i]].gdsegments[j];
      }
    }
    /* Create a new population by roulette wheel.
       (Consider: Top half roulette wheel/Keep top 8?) */
    GA_generate(session, i);

    /* Bump generation */
    session->generation++;
    /* Check termination condition. */
    printf("BKTS %03d ", session->generation);
    for ( i = 0; i < session->cachesize; i++ ) {
      printf("%c", session->fitnesscache[i][0].unscaledfitness ? '#' : '.');
      if ( i%64 == 63 ) printf("\n         ");
    }
    /* printf("\n"); */

    printf("\n");
    if ( GA_checkfitness(session) != 0 ) return 1;
    // Save output on each generation
    int rc = GA_termination(session);
    if ( GA_finished_generation(session, rc) != 0 ) return 2;
    if ( rc ) return 0;
  }

  return 0;
}

void GA_generate(GA_session *session, unsigned int i) {
  unsigned int ntimes = 0;
  for ( ; i < session->settings->popsize; /* See bottom of loop */ ) {
    unsigned int a, b, j;
    /* Special case first generation */
    if ( session->generation == 0 ) {
      do {
        for ( j = 0; j < session->population[i].segmentcount; j++ ) {
          int r;
          /*
          if ( random_r(&session->rs, &r) ) perror("random");
          */
          if ( GA_random_segment(session, i, j, &r) ) perror("random");
          /* Insert new segment, also graydecode it. */
          session->population[i].segments[j] = (GA_segment)r;
          session->population[i].gdsegments[j] = graydecode((GA_segment)r);
        }
        session->population[i].fitness = 0;
      } while ( !GA_fitness_quick(session, &session->population[i]) );
      i++;
      continue;
    }
    /* Proceed in general */
    a = GA_roulette(session);
    b = GA_roulette(session);
    for ( j = 0; j < session->population[i].segmentcount; j++ ) {
      int afirst;
      int bitpos;
      GA_segment olda, oldb;
      GA_segment newa, newb;
      GA_segment mask;
      if ( random_r(&session->rs, &afirst) ) perror("random");
      if ( random_r(&session->rs, &bitpos) ) perror("random");
      afirst = afirst%2;
      bitpos = bitpos%GA_segment_size;
      mask = (1<<bitpos)-1;
      /* Crossover. Random split & recombine. */
      olda = session->oldpop[afirst ? a : b].segments[j];
      oldb = session->oldpop[afirst ? b : a].segments[j];
      newa = (olda & (~mask)) | (oldb & mask);
      newb = (oldb & (~mask)) | (olda & mask);

      /*
      printf("\nNITM %03d %03d %03d %03d\n",
             session->generation+1, i, i+1, j);
      printf("XOVR %08x %3d %08x %3d to %08x %08x mask %2d %08x %08x\n",
             olda,a, oldb,b, newa, newb, bitpos, ~mask, mask);
      */

      /* Mutation. Low-probability bitflip. */
      for ( bitpos = 0; bitpos < GA_segment_size; bitpos++ ) {
        unsigned int k;
        mask = 1<<bitpos;
        for ( k = 0; k < 2; k++ ) { /* Outer loop is pairwise */
          double threshold = session->settings->mutationrate *
            powf((double)(GA_segment_size-bitpos)/GA_segment_size,
                 session->settings->mutationweight);
          /* Mutation probability */
          if ( random_r(&session->rs, &afirst) ) perror("random");
          if ( !( (double)afirst/RAND_MAX < threshold ) ) continue;
          /* printf("FLIP %08x     ", (k == 0) ? newa : newb); */
          if ( k == 0 ) newa ^= mask;
          else newb ^= mask;
          /*
          printf("to %08x mask %2d %08x\n",
                 (k == 0) ? newa : newb, bitpos, mask); */
        }
      }
      /* Insert new segments. Graydecode each segment and store the
       * result in the graydecode cache. This allows us to graydecode
       * each segment only once. */

      session->population[i].segments[j] = newa;
      session->population[i].gdsegments[j] = graydecode(newa);
      if ( i+1 < session->settings->popsize ) {
        session->population[i+1].segments[j] = newb;
        session->population[i+1].gdsegments[j] = graydecode(newb);
        //printf("%d\n", graydecode(newa));
      }
    }
    /* Verify that new population elements are valid */
    if ( GA_fitness_quick(session, &session->population[i]) &&
         ( i+1 >= session->settings->popsize ||
           GA_fitness_quick(session, &session->population[i+1]) ) ) {
      /* Continue to next loop iteration */
      printf("REGENERATED %04d %04d %3d\n", i, i+1, ntimes);
      i += 2;
      ntimes = 0;
    }
    else ntimes++;
  }
}

unsigned int GA_roulette(GA_session *session) {
  double index;
  unsigned int i;
  /* If all the choices have score 0, assign uniform nonzero scores to
   * all individuals. */
  double uniformroulette = 0;
  /* printf("rand %d\n",(int)(index*64)); */
  int r;
  if ( random_r(&session->rs, &r) ) perror("random");
  index = 1.0*r/RAND_MAX;
  if ( session->fitnesssum <= 0 )
    uniformroulette = 1.0/session->settings->popsize;
  /* printf("test %f %d %f\n", session->fitnesssum, session->popsize, uniformroulette); */
  for ( i = 0; i < session->settings->popsize; i++ ) {
    double score = (session->fitnesssum <= 0) ? uniformroulette :
      (1.0*session->oldpop[i].fitness/session->fitnesssum);
    if ( index < score ) {
      /* printf("roulette %7.5f < %7.5f %u\n", index, score, i); */
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

static int GA_do_checkfitness(GA_thread *thread, unsigned int i) {
  GA_session *session = thread->session;
  int j;
  int found = 0;
  GA_segment hashtemp = 0;
  unsigned int hashbucket = 0;
  /* GA_individual founditem; */

  /* Hash the individual to determine caching location */
  for ( j = 0; j < session->population[i].segmentcount; j++ )
    hashtemp ^= session->population[i].segments[j];
  hashbucket = hashtemp % session->cachesize;

  /* Check in the cache for an earlier computation of the fitness value */
#if THREADS
  /* LOCK fitnesscache */
  j = pthread_mutex_lock(&(session->cachemutex));
  if ( j ) { qprintf(session->settings,
		     "GA_dcf: mutex_lock(cache_r): %d\n", j); exit(1); }
#endif
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
#if THREADS
  /* UNLOCK fitnesscache */
  j = pthread_mutex_unlock(&(session->cachemutex));
  if ( j ) { qprintf(session->settings,
		     "GA_dcf: mutex_unlock(cache_r): %d\n", j); exit(1); }
#endif

#if !THREADS
  /* Also check the earlier entries in the new population. Not
   * threadsafe. */
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
#endif

  /* Also check the old population.  No lock necessary,
   * oldpop only modified in GA_evolve */
  if ( !found && session->generation > 0 ) {
    for ( j = 0; j < session->settings->popsize; j++ ) {
      if ( !memcmp(session->population[i].segments,
		   session->oldpop[j].segments,
		   sizeof(GA_segment)*session->population[i].segmentcount) ) {
	session->population[i].fitness = session->oldpop[j].unscaledfitness;
	found = 6;
	break;
      }
    }
  }

  /* If not found in the cache, compute the fitness value */
  /* memcpy(&founditem, &(session->population[i]), sizeof(GA_individual)); */
  if ( !found ) {
    int rc = 0;
    if ( ((rc = GA_fitness(session, thread->ref, /* FIXME */
			   &session->population[i])) != 0)
	 /* || isnan(session->population[i].fitness) */ ) { /* nan okay now */
      qprintf(session->settings, "fitness error %u %f => %d\n",
	      session->population[i].segments[0],
	      session->population[i].fitness, rc);
      return 51;
    }
  }
  /*
  if ( found && founditem.fitness != session->population[i].fitness ) {
    qprintf(session->settings, "cache error %d %08x %08x %08x vs %08x %08x %08x\n  %f != %f\n", i, founditem.segments[0], founditem.segments[1],
	    founditem.segments[2], session->population[i].segments[0],
	    session->population[i].segments[1],
	    session->population[i].segments[2],
	    founditem.fitness, session->population[i].fitness);
    return 52;
  }
  */

  /* Announce caching status for segment use */ 
  tprintf("FND%1d hash %08x bucket %4d orig %f\n",
	  found, hashtemp, hashbucket, session->population[i].fitness);
  /* Save the fitness in the cache */
  if ( ( found == 0 ) || ( found > 1 ) ) {
    GA_segment *ptr;
#if THREADS
    /* LOCK fitnesscache */
    j = pthread_mutex_lock(&(session->cachemutex));
    if ( j ) { qprintf(session->settings,
		       "GA_dcf: mutex_lock(cache_w): %d\n", j); exit(1); }
#endif
    if ( session->fitnesscache[hashbucket][0].unscaledfitness != 0 ) {
      /* First demote the first-level hash to second-level */
      ptr = session->fitnesscache[hashbucket][1].segments;
      memcpy(&(session->fitnesscache[hashbucket][1]),
	     &(session->fitnesscache[hashbucket][0]), sizeof(GA_individual));
      session->fitnesscache[hashbucket][1].segments = ptr;
      memcpy(ptr, session->fitnesscache[hashbucket][0].segments,
	     sizeof(GA_segment)*session->population[i].segmentcount);
    }
    ptr = session->fitnesscache[hashbucket][0].segments;
    memcpy(&(session->fitnesscache[hashbucket][0]),
	   &(session->population[i]), sizeof(GA_individual));
    session->fitnesscache[hashbucket][0].unscaledfitness = 1;
    session->fitnesscache[hashbucket][0].segments = ptr;
    memcpy(ptr, session->population[i].segments,
	   sizeof(GA_segment)*session->population[i].segmentcount);
#if THREADS
    /* UNLOCK fitnesscache */
    j = pthread_mutex_unlock(&(session->cachemutex));
    if ( j ) { qprintf(session->settings,
		       "GA_dcf: mutex_unlock(cache_w): %d\n", j); exit(1); }
#endif
  }
  return found;
}

#if THREADS
static void *GA_do_thread (void * arg) {
  GA_thread *thread = (GA_thread *)arg;
  GA_session *session = thread->session;
  while ( 1 ) {
    int in, found;
    int rc;

    /* Find a job to process */
    /* printf("Waiting for mutex...\n"); */
    rc = pthread_mutex_lock(&(session->inmutex));
    if ( rc ) { qprintf(session->settings,
			"GA_do_thread: mutex_lock(in): %d\n", rc); exit(1); }
    /* Wait until the job-available flag is set */
    while ( !(session->inflag) ) {
      /* printf("Waiting for cond...\n"); */
      rc = pthread_cond_wait(&(session->incond), &(session->inmutex));
      if ( rc ) { qprintf(session->settings,
			  "GA_do_thread: cond_wait(in): %d\n", rc); exit(1); }
    }
    in = session->inindex;	/* We got data to process! */

    /* Is there another job to dispatch? */
    session->inindex++;
    if ( session->inindex < session->settings->popsize ) {
      rc = pthread_cond_signal(&(session->incond));
      if ( rc )
	{ qprintf(session->settings,
		  "GA_do_thread: cond_signal(in): %d\n", rc); exit(1); }
    }
    else session->inflag = 0;	/* No more input data */
    rc = pthread_mutex_unlock(&(session->inmutex));
    if ( rc ) { qprintf(session->settings,
			"GA_do_thread: mutex_unlock(in): %d\n", rc); exit(1); }

    /* Process item */
    found = GA_do_checkfitness(thread, in);

    /* Return the result to main program */
    rc = pthread_mutex_lock(&(session->outmutex));
    if ( rc ) { qprintf(session->settings,
			"GA_do_thread: mutex_lock(out): %d\n", rc); exit(1); }
    while ( session->outflag ) {
      /* printf("Waiting for output queue to empty...\n"); */
      rc = pthread_cond_wait(&(session->outcond), &(session->outmutex));
      if ( rc ) { qprintf(session->settings,
			  "GA_do_thread: cond_wait(out): %d\n", rc); exit(1); }
      /* printf("Retrying output\n"); */
    }
    session->outindex  = in;
    session->outretval = found;
    session->outflag = 1;
    rc = pthread_cond_broadcast(&(session->outcond));
    if ( rc ) { qprintf(session->settings,
			"GA_do_thread: cond_bcast(out): %d\n", rc); exit(1); }
    rc = pthread_mutex_unlock(&(session->outmutex));
    if ( rc )
      { qprintf(session->settings,
		"GA_do_thread: mutex_unlock(out): %d\n", rc); exit(1); }
    /* printf("Output completed\n"); */
  }
  return NULL;
}
#endif

static void display_individual(GA_session *session, unsigned int i,
                               int always, char *type) {
  unsigned int j;
  char *str = NULL;
  int rc = asprintf(&str,
     "%-4s %04u %04u   GD 000 %10u score %9.7f orig %15.3f",
     type, session->generation, i, session->population[i].gdsegments[0],
     session->population[i].fitness, session->population[i].unscaledfitness);
  if ( rc < 0 ) return;
  /* Use more lines for additional segments */
  for ( j = 1; j < session->population[i].segmentcount; j++ ) {
    char *substr = NULL;
    rc = asprintf(&substr, "                 GD %03d %10u", j,
                  session->population[i].gdsegments[j]);
    if ( rc < 0 ) return;
    if ( astrcat(&str, substr) ) return; /* failed */
    free(substr);
  }

  if ( always ) qprintf(session->settings, "%s\n", str);
  else printf("%s\n", str);
  free(str);
}

int GA_checkfitness(GA_session *session) {
  unsigned int i, j, cfinite;
  int rc;
  double min, max, mean;
  double offset, scale, scalelen;
  int scaleidx;
  unsigned int fevs = 0;
  session->fittest = 0;
  session->fitnesssum = 0;
  /* Evaluate fitness for each individual */
#if 0
  for ( i = 0; i < session->popsize; i++ ) {
    int found = GA_do_checkfitness(session->threads[0], i);
    if ( found > 50 ) return found; /* Error */
    if ( !found ) fevs++;	    /* Had to do fitness evaluation */
    /* Track minimum and maximum fitnesses */
    if ( i == 0 || session->population[i].fitness < min )
      min = session->population[i].fitness;
    if ( i == 0 || session->population[i].fitness > max )
      max = session->population[i].fitness;
  }
#else
  cfinite = 0; /* Number of individuals with finite fitness */
  /* Continue until we have a full population */
  while ( cfinite < session->settings->popsize ) {
    /* If repeating, move all infinites to the end */
    if ( cfinite > 0 ) {
      j = session->settings->popsize;
      for ( i = 0; i < j; i++ ) {
        GA_individual temp = session->population[i];
        if ( !isnan(temp.fitness) ) continue;
        /* Move us to the end. */
        do {
          j--;
        } while ( isnan(session->population[j].fitness) && j > i );
        if ( j <= i ) break; /* We've overshot */
        session->population[i] = session->population[j];
        session->population[j] = temp;
      }
      /* Check that this startat / cfinite is correct */
      if ( j != cfinite || i != cfinite ) {
        qprintf(session->settings, "GA_checkfitness: nan migration failed: %d, %d, %d\n", i, j, cfinite);
        exit(1);
      }
      qprintf(session->settings, "Still only %d valid individuals...\n", i);
      /* Generate new individuals */
      GA_generate(session, i);
    }
#if THREADS
    /* Initiate dispatch among worker threads */
    rc = pthread_mutex_lock(&(session->inmutex));
    if ( rc ) { qprintf(session->settings,
		        "GA_checkfitness: mutex_lock(in): %d\n", rc); exit(1); }
    session->inindex = cfinite;
    session->inflag = 1;
    rc = pthread_mutex_unlock(&(session->inmutex));
    if ( rc )
      { qprintf(session->settings,
	        "GA_checkfitness: mutex_unlock(in): %d\n", rc); exit(1); }
    rc = pthread_cond_signal(&(session->incond));
    if ( rc ) { qprintf(session->settings,
		        "GA_checkfitness: cond_signal(in): %d\n", rc); exit(1); }
#endif
    j = cfinite; i = 0;
    while ( j < session->settings->popsize ) {
      int found;
#if THREADS
      /* Check the worker thread output buffer. */
      rc = pthread_mutex_lock(&(session->outmutex));
      if ( rc )
        { qprintf(session->settings,
		  "GA_checkfitness: mutex_lock(out): %d\n", rc); exit(1); }
      /* Wait until the job-completed flag is set. */
      while ( !session->outflag ) {
        /* printf("Waiting for reply...\n"); */
        rc = pthread_cond_wait(&(session->outcond), &(session->outmutex));
        if ( rc )
	  { qprintf(session->settings,
		    "GA_checkfitness: cond_wait(out): %d\n", rc); exit(1); }
        /* printf("Waking up\n"); */
      }
      /* printf("outflag! \n"); */
      i = session->outindex;
      found = session->outretval;
      session->outflag = 0;
      /* Unlock output buffer */
      rc = pthread_mutex_unlock(&(session->outmutex));
      if ( rc )
        { qprintf(session->settings,
		  "GA_checkfitness: mutex_unlock(out): %d\n", rc); exit(1); }
      rc = pthread_cond_broadcast(&(session->outcond));
      if ( rc )
        { qprintf(session->settings,
		  "GA_checkfitness: cond_broadcast(out): %d\n", rc); exit(1); }
#else
      /* Non-threaded: Just do this fitness evaluation */
      i = j; rc = 0;
      found = GA_do_checkfitness(&(session->threads[0]), i);
#endif

      if ( found > 50 ) return found; /* Error */
      if ( !found ) fevs++;	    /* Had to do a real fitness evaluation */

      printf("Got %d %d.\n", j, i);

      /* Track minimum and maximum fitnesses */
      j++;
      if ( isnan(session->population[i].fitness) ) continue; /* Killed item */

      /* session->fitnesssum += session->population[i].fitness; */
      if ( cfinite == 0 || session->population[i].fitness < min )
        min = session->population[i].fitness;
      if ( cfinite == 0 || session->population[i].fitness > max )
        max = session->population[i].fitness;
      mean += session->population[i].fitness;
      cfinite++;
    }
  }
#endif	/* unless 0 */
  printf("cfinite=%u vs %u\n", cfinite, session->settings->popsize);
  mean = mean/(cfinite ? cfinite : 1);/* session->settings->popsize; */

  /* Dynamic Mutation */
  if ( session->settings->dynmut ) {
    double a, b = session->dynmut_trailing[session->dynmut_trailing_pos], d;
    session->dynmut_leading = session->dynmut_leading*
      (session->settings->dynmut_factor-1.0)/session->settings->dynmut_factor
      + mean;
    a = session->dynmut_leading/session->settings->dynmut_factor;
    session->dynmut_trailing[session->dynmut_trailing_pos] = a;
    session->dynmut_trailing_pos =
      (session->dynmut_trailing_pos+1) % session->settings->dynmut_width;
    d = (session->generation < session->settings->dynmut_width) ? -0.693147 : (a-b);
    session->settings->mutationrate = session->settings->dynmut_min +
      exp(-fabs(d))*session->settings->dynmut_range;
    printf("DYNM %03u AVG %10.3f  A %10.3f  B %10.3f  D %10.3f  R %10.3f\n",
           session->generation, mean, a, b, d,
           session->settings->mutationrate);
  }
  else printf("DYNM %03u AVG %10.3f\n", session->generation, mean);
  /* Show statistics of caching effectiveness */
  printf("FEVS %u  -%u\n", fevs, j-fevs);

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
  /* printf("min %10u => %f\nmax %10u => %f\noffset   %f   scale %f\n",0,min,0,max,offset,scale); */

  for ( i = 0; i < session->settings->popsize; i++ ) {
    session->population[i].unscaledfitness = session->population[i].fitness;
    if ( isnan(session->population[i].fitness) ) /* Killed: Minimal fitness */
      session->population[i].fitness = 0; /* FIXME: Incorrect Constant? */
    else			/* Shift and scale fitness values */
      session->population[i].fitness =
	(session->population[i].fitness+offset)*scalelen/scale+(1-scalelen);
    if ( session->population[i].fitness >
	 session->population[session->fittest].fitness ) session->fittest = i;
    session->fitnesssum += session->population[i].fitness;
    display_individual(session, i, 0,
                       (session->fittest == i) ? "FITM" : "ITEM");
#if 0
    printf("%-4s %03u %03u   GD 000 %10u score %9.7f orig %15.3f\n",
	   (session->fittest == i) ? "FITM" : "ITEM", session->generation, i,
	   session->population[i].gdsegments[0],
	   session->population[i].fitness,
	   session->population[i].unscaledfitness);
    /* Use more lines for additional segments */
    for ( j = 1; j < session->population[i].segmentcount; j++ )
      printf("               GD %03d %10u\n", j,
	     session->population[i].gdsegments[j]);
#endif
  }

  /* Sort the sorted list. */
  GA_comparator(session, NULL);	/* Initialize comparator */
  /* printf("%u\n", session->sorted[0]); */
  qsort(&(session->sorted[0]), session->settings->popsize,
        sizeof(unsigned int), GA_comparator);
  /* printf("%u\n", session->sorted[0]); */
  session->fittest = session->sorted[0]; /* Since sorting seems to work */
  if ( session->sorted[0] != session->fittest ) {
    /* Did not choose same item. Do full comparison, return error if fails */
    /* FIXME - Completely broken!!! (Fails for ga-numbers) */
    /* Does this work now? */
    int mc = memcmp(&(session->population[session->sorted[0]].segments[0]),
		    &(session->population[session->fittest  ].segments[0]),
		    sizeof(GA_segment)*session->population[session->fittest].segmentcount);
    if ( mc ) {
      qprintf(session->settings, "Sort failed: S=%u vs F=%u\n",
	      session->sorted[0], session->fittest);
      return 2;
    }
  }
  /* Display the best individual */
  display_individual(session, session->fittest, 1, "BEST");
#if 0
  qprintf(session->settings,
	  "BEST %03u %03u   GD 000 %10u score %9.7f orig %15.3f\n",
	  session->generation, session->fittest,
	  session->population[session->fittest].gdsegments[0],
	  session->population[session->fittest].fitness,
	  session->population[session->fittest].unscaledfitness);
  /* Use more lines for additional segments */
  for ( j = 1; j < session->population[session->fittest].segmentcount; j++ )
    qprintf(session->settings, "               GD %03d %10u\n", j,
	    session->population[session->fittest].gdsegments[j]);
#endif
  printf("\n");
  return 0;
}

GA_segment graydecode(GA_segment gray) { 
  GA_segment bin;
#if GA_segment_size == 32
  /* Optimization: b = b^(g>>1);b = b^(b>>2); ...4; ... 8; ... 16;
     IF(64BIT,...32) -- can be much faster. */
  bin = gray;
  bin ^= bin>>1;
  bin ^= bin>>2;
  bin ^= bin>>4;
  bin ^= bin>>8;
  bin ^= bin>>16;
#else
  for ( bin = 0; gray; gray >>= 1 ) bin ^= gray; 
#endif
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

/** Append second string into first. Repeated appends to the same
 * string are cached, making this an O(n) function, where n =
 * strlen(append).
 *
 * From public domain file src/usr.bin/sdiff/sdiff.c in
 * ftp://ftp.netbsd.org/pub/NetBSD/NetBSD-current/tar_files/src/usr.bin.tar.gz
 */
static int astrcat(char **s, const char *append) {
  /* Length of string in previous run. */
  static size_t offset = 0;
  size_t newsiz;
  /* String from previous run.  Compared to *s to see if we are
   * dealing with the same string.  If so, we can use offset. */
  static const char *oldstr = NULL;
  char *newstr;

  /* First string is NULL, so just copy append. */
  if (!*s) {
    if (!(*s = strdup(append))) return 1;

    /* Keep track of string. */
    offset = strlen(*s);
    oldstr = *s;

    return 0;
  }

  /* *s is a string so concatenate. */
  
  /* Did we process the same string in the last run? If this is a
   * different string from the one we just processed cache new
   * string. */
  if (oldstr != *s) {
    offset = strlen(*s);
    oldstr = *s;
  }
  
  /* Size = strlen(*s) + \n + strlen(append) + '\0'. */
  newsiz = offset + 1 + strlen(append) + 1;
  
  /* Resize *s to fit new string. */
  newstr = realloc(*s, newsiz);
  if (newstr == NULL) return 2;
  *s = newstr;
  
  /* *s + offset should be end of string. */
  /* Concatenate. */
  strncpy(*s + offset, "\n", newsiz - offset);
  strncat(*s + offset, append, newsiz - offset);
  
  /* New string length should be exactly newsiz - 1 characters. */
  /* Store generated string's values. */
  offset = newsiz - 1;
  oldstr = *s;
  return 0;
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
 * \param optlog          Reallocable string to log options into, or NULL.
 * \param optlogtype      Origin of option ("F"ile or Command-line "A"rguments)
 *
 * \see GA_getopt
 *
 * \returns 0 for success.
 */
int GA_run_getopt(int argc, char * const argv[], GA_settings *settings,
		  const char *optstring, const struct option *long_options,
		  int global_count, GA_my_parseopt_t my_parse_option,
		  const char *my_usage, char **optlog, char optlogtype) {
  int c;
  while (1) {
   /* getopt_long stores the option index here. */
   int option_index = 0;

   /* tprintf("%d %s %s\n%d %x\n%d\n", argc, argv[1], argv[2], optind, argv); */
   c = getopt_long(argc, argv, optstring, long_options, &option_index);

   /* Record to optlog */
   if ( optlog && *optlog ) {
     char *msg = NULL;
     int rc = 0;
     if ( c == 0 )
       rc = asprintf(&msg, "CFG%c %s %s", optlogtype,
		     long_options[option_index].name, optarg ? optarg : "");
     else {
       int i = 0;
       for ( i = 0; long_options[i].name; i++ ) {
	 if ( long_options[i].flag == NULL && long_options[i].val == c ) {
	   rc = asprintf(&msg, "CFG%c %s %s", optlogtype,
			 long_options[i].name, optarg ? optarg : "");
	   break;
	 }
       }
     }
     if ( msg && rc > 0 ) { astrcat(optlog, msg); free(msg); }
   }

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
   case 's':
     settings->randomseed = strtoul(optarg, NULL, 10);
     break;
   case 'D':
     settings->debugmode = 1;
     break;
   case 'T':
     settings->threadcount = atoi(optarg);
     break;
   case 'E':
     settings->elitism = atoi(optarg);
     break;
   case 'M':
     settings->mutationrate = atof(optarg);
     break;
   case 'W':
     settings->mutationweight = atof(optarg);
     break;
   case 10: /* --dynamic-mutation */
     settings->dynmut = 1;
     break;
   case 11: /* --dynamic-mutation-width */
     settings->dynmut_width = atoi(optarg);
     break;
   case 12: /* --dynamic-mutation-factor */
     settings->dynmut_factor = atoi(optarg);
     break;
   case 13: /* --dynamic-mutation-min */
     settings->dynmut_min = atof(optarg);
     break;
   case 14: /* --dynamic-mutation-range */
     settings->dynmut_range = atof(optarg);
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
	      GA_my_parseopt_t my_parse_option, const char *my_usage,
	      char **optlog) {
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
     * The number of individuals in the population. Must be even.
     */
    {"population", required_argument, NULL, 'p'},
    /** -s, --seed NUMBER
     *
     * The random seed to use when initializing the random number
     * generator. Use the same seed to obtain the same result. Note
     * that the random numbers generated may be different on different
     * computers, even when using the same seed.
     */
    {"seed", required_argument, NULL, 's'},
    /** -D, --debug
     *
     * If specified, write all log messages to stdout INSTEAD of
     * FILE.log (see --output). Only applicable to ga-spectroscopy.
     */
    {"debug",          no_argument, 0, 'D'},
    /** -T, --threads NUMBER
     *
     * Specify the number of fitness evalutions to perform at once. If
     * threading is not available, must specify 1.
     */
    {"threads",   required_argument, 0, 'T'},
    /** -E, --elitism NUMBER
     *
     * Number of top performers to be carried over into the next
     * generation.  Must be even.
     */
    {"elitism",   required_argument, 0, 'E'},
    /** -M, --mutationrate NUMBER
     *
     * Mutation rate, in 0-1. Mutation probability per bit, subject to
     * mutation weight and dynamic mutation.
     */
    {"mutationrate",   required_argument, 0, 'M'},
    /** -W, --mutationweight NUMBER
     *
     * Mutation weight, nonnegative. Controls the weight of the random
     * mutation towards less significant bits. 0 is unweighted, >0 weights
     * to less significant bits.
     */
    {"mutationweight",  required_argument, 0, 'W'},
    /** --dynamic-mutation
     *
     * Enable dynamic mutation (increase mutation rate when fitness is
     * stagnant). --mutationrate has little effect in this mode.
     *
     * How it works: Basically, it maintains "leading" and "trailing" values,
     * with the trailing value T equal to the leading value from w ("width")
     * generations ago, and leading value L is updated each generation by
     * L=L*(f-1)/f+F, where F is the average fitness of the generation and f
     * is the "factor". From this a new mutation rate M is computed by
     * M=m+exp(-|D|)*r, where D=(L-T)/f, m is "min" and r is "range".
     */
    {"dynamic-mutation", no_argument, 0, 10},
    /** --dynamic-mutation-width NUMBER
     *
     * No effect unless dynamic mutation is enabled.
     * Width for consideration of change in fitness.
     */
    {"dynamic-mutation-width", required_argument, 0, 11},
    /** --dynamic-mutation-factor NUMBER
     *
     * No effect unless dynamic mutation is enabled.
     * Arbitrary factor used in computation of dynamic mutation.
     */
    {"dynamic-mutation-factor", required_argument, 0, 12},
    /** --dynamic-mutation-min NUMBER
     *
     * No effect unless dynamic mutation is enabled.
     * Minimum mutation rate, in 0-1.
     */
    {"dynamic-mutation-min", required_argument, 0, 13},
    /** --dynamic-mutation-range NUMBER
     *
     * No effect unless dynamic mutation is enabled.
     * Range of mutation rates, in 0-1 (Maximum mutation rate is
     * minimum+range, which must be less than or equal to 1).
     */
    {"dynamic-mutation-range", required_argument, 0, 14},
    /*
      {"verbose", no_argument,       &verbose_flag, 1},
      {"brief",   no_argument,       &verbose_flag, 0},
    */
    {0, 0, 0, 0}
  };
  static const char *global_optstring = "c:g:p:s:hDT:E:M:W:";
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
      char *key = malloc(512*sizeof(char));
      char *value = malloc(4096*sizeof(char));
      typedef char *foo;
      foo fake_argv[4];
      fake_argv[2] = fake_argv[3] = 0;
      if ( !key || !value ) {
        printf("%s: %s: Allocation error", argv[0], loadconfig);
        exit(1);
      }
      int rc = fscanf(fh, " %511s%*[ \t]%4095[^\n]", key, value);
      /* printf("%d %p %p\n", rc, key, value); */
      if ( rc == EOF && ferror(fh) ) {
	char *str;
	asprintf(&str, "%s: %s", argv[0], loadconfig);
	perror(str);
	free(str);
	exit(1);
      }
      else if ( rc == EOF ) break;
      else if ( rc >= 1 && ( key[0] == '#' || key[0] == ';' || key[0] == '%' ||
                             key[0] == '!' ) ) { /* Common comment characters */
        free(key);
        free(value);
        continue;
      }
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
		    global_count, my_parse_option, my_usage, optlog, 'F');
      /* free(fake_argv[1]); */
      free(key);
      /* free(value); */ /* Don't free value, we might keep a pointer to it */
    }
    fclose(fh);
  }

  optind = 1;
  GA_run_getopt(argc, argv, settings, optstring, long_options,
		global_count, my_parse_option, my_usage, optlog, 'A');
  free(optstring);
  free(long_options);
  return 0;
}

int qprintf(const GA_settings *settings, const char *format, ...) {
  va_list ap;
  int rc = 0;
#if THREADS
  { /* LOCK io */
    int trc = pthread_mutex_lock(&iomutex);
    if ( trc ) { printf("qprintf: mutex_lock(io): %d\n", trc); exit(1); }
  }
  flockfile(stdout);
  /* flockfile(settings->stdoutfh); */
#endif
  va_start(ap, format);
  rc = vprintf(format, ap);
  va_end(ap);
  if ( !settings->debugmode ) {
    va_start(ap, format);
    rc = vfprintf(settings->stdoutfh, format, ap);
    va_end(ap);
  }
#if THREADS
  /* funlockfile(settings->stdoutfh); */
  funlockfile(stdout);
  { /* UNLOCK io */
    int trc = pthread_mutex_unlock(&iomutex);
    if ( trc ) { printf("qprintf: mutex_unlock(io): %d\n", trc); exit(1); }
  }
#endif
  return rc;
}

int tprintf(const char *format, ...) {
  va_list ap;
  int rc = 0;
#if THREADS
  { /* LOCK io */
    int trc = pthread_mutex_lock(&iomutex);
    if ( trc ) { printf("tprintf: mutex_lock(io): %d\n", trc); exit(1); }
  }
  flockfile(stdout);
#endif
  va_start(ap, format);
  rc = vprintf(format, ap);
  va_end(ap);
#if THREADS
  funlockfile(stdout);
  { /* UNLOCK io */
    int trc = pthread_mutex_unlock(&iomutex);
    if ( trc ) { printf("tprintf: mutex_unlock(io): %d\n", trc); exit(1); }
  }
#endif
  return rc;
}

/* From http://www.linuxquestions.org/questions/programming-9/how-to-calculate-time-difference-in-milliseconds-in-c-c-711096/ */
long long
timeval_diff(struct timeval *difference,
             struct timeval *end_time,
             struct timeval *start_time
            )
{
  struct timeval temp_diff;

  if(difference==NULL)
  {
    difference=&temp_diff;
  }

  difference->tv_sec =end_time->tv_sec -start_time->tv_sec ;
  difference->tv_usec=end_time->tv_usec-start_time->tv_usec;

  /* Using while instead of if below makes the code slightly more robust. */

  while(difference->tv_usec<0)
  {
    difference->tv_usec+=1000000;
    difference->tv_sec -=1;
  }

  return 1000000LL*difference->tv_sec+
                   difference->tv_usec;

} /* timeval_diff() */

/** Start a process with STDOUT redirected to the file descriptor
 * stdoutfd, wait for it to finish, and then return its exit status.
 * This function based on the sample implementation of system from:
 * http://www.opengroup.org/onlinepubs/000095399/functions/system.html
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
  struct timeval starttime, endtime;
  if ( argc <= 0 ) return 1;
  argv = malloc(sizeof(char*)*(argc+1));
  if ( !argv ) return 1;
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
#if THREADS
  stat = pthread_mutex_lock(&iomutex);
  if ( stat ) { printf("tprintf: mutex_lock(io): %d\n", stat); exit(1); }
#endif
  flockfile(stdout);
  fflush(NULL);
  gettimeofday(&starttime, NULL);/* To compute runtime */
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
#if THREADS
  stat = pthread_mutex_unlock(&iomutex);
  if ( stat ) { printf("tprintf: mutex_unlock(io): %d\n", stat); exit(1); }
#endif
  free(argv);

  if (pid == -1) {
    stat = -1; /* errno comes from fork() */
  } else {
    while (waitpid(pid, &stat, 0) == -1) {
      if (errno != EINTR){
	stat = -1;
	break;
      }
    }
  }
  gettimeofday(&endtime, NULL);
  printf("System took %f seconds.\n", timeval_diff(NULL, &endtime, &starttime)/1000000.0);
  /*
  sigaction(SIGINT, &savintr, (struct sigaction *)0);
  sigaction(SIGQUIT, &savequit, (struct sigaction *)0);
  sigprocmask(SIG_SETMASK, &saveblock, (sigset_t *)0);
  */

  return(stat);
}

