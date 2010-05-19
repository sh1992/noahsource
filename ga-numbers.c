/** \file ga-numbers.c
 *
 * Find the (integer) square root of a number using a genetic algorithm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "ga.h"

#define POPULATION  64
#define GENERATIONS 150 

int my_parseopt(const struct option *long_options, GA_settings *settings,
		int c, int option_index) {
  switch (c) {
  case 'n':
    *(int *)(settings->ref) = atoi(optarg);
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
  static const struct option my_long_options[] = {
    {"number",     required_argument,       0, 'n'},
    {0, 0, 0, 0}
  };
  int rc = 0;
  int target = 64;

  GA_defaultsettings(&settings);
  settings.popsize = POPULATION;
  settings.generations = GENERATIONS;
  settings.ref = &target;
  GA_getopt(argc,argv, &settings, "n:", my_long_options, my_parseopt, "");

  if ( (rc = GA_init(&ga, &settings, 1)) != 0 ) {
    printf("GA_init failed: %d\n", rc);
    return rc;
  }
  /*
  for ( rc = 0; rc < 100000; rc++ ) {
    unsigned int a = GA_roulette(&ga);
    printf("picked %u\n",a);
  }
  */
  if ( (rc = GA_evolve(&ga, 0)) != 0 ) {
    printf("GA_evolve failed: %d\n", rc);
    return rc;
  }
  if ( (rc = GA_cleanup(&ga)) != 0 ) {
    printf("GA_cleanup failed: %d\n", rc);
    return rc;
  }
  rc = 0;
  printf("Exiting: %d\n", rc);
  return rc;
}

int GA_fitness(const GA_session *ga, GA_individual *elem) {
  GA_segment x = graydecode(elem->segments[0]);
  /* printf("%u\n",elem->segments[0]); */
  elem->fitness = -log(1+fabs(*(int *)(ga->settings->ref)-(double)x*x));
  /* elem->fitness = (fitness > MAX_FITNESS) ? 0 : (MAX_FITNESS - fitness); */
  return 0;
}

int GA_termination(const GA_session *session) {
  if ( session->population[session->fittest].unscaledfitness > -0.00001 )
    return 1;
  return 0;
}

