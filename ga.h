/** \file ga.h
 *
 * Genetic algorithm header file.
 */
#ifndef _HAVE_GA_H
#include <stdint.h>
#include <getopt.h>
#if THREADS
#include <pthread.h>
#endif
#if HAVE_GSL
#include <gsl/gsl_rng.h>
#endif

/* Library typedefs */

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

/** A single individual of the population.
 */
typedef struct GA_individual_struct {
  /** The array of segments for the individual. Each segment contains
   * a portion of the individual's genome.
   */
  GA_segment *segments;
  /** Graydecoded segments. */
  GA_segment *gdsegments;
  /** The number of segments in the individual. */
  unsigned int segmentcount;
  /** The fitness of the individual. \see GA_fitness, GA_checkfitness */
  double fitness;
  /** The unscaled fitness of the individual. In the cache, field is
   * hijacked to denote validity of the cache entry.  \see
   * GA_checkfitness */
  double unscaledfitness;
} GA_individual;

/** Configuration settings for the run.
 */
typedef struct GA_settings_struct {
  /** The seed value used for srandom. */
  unsigned int randomseed;
  /** The number of individuals in the population. */
  unsigned int popsize;
  /** The number of generations to run for. */
  unsigned int generations;
  /** Mutation rate, in 0-1. Mutation probability per bit. Varies due to
   *  mutation weight. Modified during run by dynamic mutation.
   *
   * \see mutationweight
   */
  double mutationrate;
  /** Mutation weight, nonnegative. Controls the weight of the random
   * mutation towards more or less significant bits. Probability of a given
   * bit mutating is R*((S-B)/S)^W, where R is the mutation rate, S is the
   * GA_segment_size, B is the bit position, and W is the mutation weight.
   */
  double mutationweight;
  /** Elitism count. If odd, round down to next even number.
   *
   * If specified manually, elitismset MUST also be set to a true value,
   * or else it will be replaced with the default value. */
  unsigned int elitism;
  /** Default elitism is sqrt(popsize) unless manually set. */
  int elitismset;
  /** Use dynamic mutation. */
  unsigned int dynmut;
  /** Dynamic mutation, width for consideration of change in fitness */
  unsigned int dynmut_width;
  /** Dynamic mutation, factor used for exponential decay of old data */
  unsigned int dynmut_factor;
  /** Dynamic mutation, minimum mutation rate. */
  double dynmut_min;
  /** Dynamic mutation, range of mutation rate (min+range=max). */
  double dynmut_range;
  /** If false, assume not in "debug mode", where stdout is redirected
   * to a file, and the real stdout is available in stdoutfd. */
  int debugmode;
  /** Log file  if debug mode is not active. \see debugmode */
  FILE *logfh;
  /** Number of threads to use. */
  int threadcount;
  /** Allow caching. Set to false if fitness metric varies over time. */
  int usecaching;
  /** Distributor for distributed algorithm. If NULL, evaluate fitness
   * locally. */
  FILE *distributor;
  /** Pointer to problem-specific options structure (for use in
   * options parsing and fitness evaluation). */
  void *ref;
} GA_settings;

/** The state of the current thread.
 */
typedef struct GA_thread_struct {
  /** Pointer to the GA_session object. */
  struct GA_session_struct *session;
#if THREADS
  /** pthread object. */
  pthread_t threadid;
#endif
  /** Numeric identifier for this thread within the program. */
  int number;
  /** Pointer to problem-specific thread state structure (for example,
   * to hold persistant allocated structures) */
  void *ref;
} GA_thread;

/** The state of the entire session.
 */
typedef struct GA_session_struct {
  /** The population array for the current generation. \see oldpop */
  GA_individual *population;
  /** The population array for the next generation. After the next
   * generation is completed, the population and oldpop pointers are
   * swapped. \see population
   */
  GA_individual *oldpop;
  /** Fitness cache, to avoid unneccessary fitness evaluations. */
  GA_individual **fitnesscache;
  /** A list of indexes into the population, sorted by fitness. The
   * most fit individual is first. */
  unsigned int *sorted;
  /** The generation of the population. The initial random population
   * is generation 0.
   */
  unsigned int generation;
  /** The index of the fittest individual of the population. */
  unsigned int fittest;
  /** The sum of the fitness over all individuals. Used by the
   * roulette algorithm. */
  double fitnesssum;
  /** The size of the fitnesscache. */
  unsigned int cachesize;
  /** Dynamic mutation leading fitness. */
  double dynmut_leading;
  /** Dynamic mutation trailing fitness buffer. */
  double *dynmut_trailing;
  /** Dynamic mutation trailing fitness buffer index. */
  unsigned int dynmut_trailing_pos;

  /** Pointer to the GA_settings structure for this session. */
  GA_settings *settings;
  /** Array of GA_thread structures, representing each thread. */
  GA_thread *threads;
#if THREADS
  /** Mutex to control access to worker thread from main thread. */
  pthread_mutex_t inmutex;
  /** Conditional variable to signal worker thread of new input data. */
  pthread_cond_t incond;
  /** Index of population element to evaluate. */
  int inindex;
  /** Flag to indicate availability of new input data. */
  int inflag;

  /** Mutex to control access to main thread from worker thread. */
  pthread_mutex_t outmutex;
  /** Conditional variable to signal main thread of new output data. */
  pthread_cond_t outcond;
  /** Index of population element that was evaluated. */
  int outindex;
  /** Return value of GA_do_checkfitness. */
  int outretval;
  /** Flag to indicate availability of new output data. */
  int outflag;

  /** Mutex to control access to fitness cache. */
  pthread_mutex_t cachemutex;
#endif
#if HAVE_GSL
  /* Random number generator. */
  gsl_rng *r;
#else
  /** Random number generator state variable. */
  struct random_data rs;
  /** Random number generator state variable. */
  int32_t randtbl[32];
#endif
} GA_session;

/* Library functions */

/** Initialize a GA_settings object with the default settings.
 *
 * \param settings The GA_settings object to initialize. Must already
 *     be allocated.
 *
 * \returns 0 to indicate success.
 */
int GA_defaultsettings(GA_settings *settings);

/** Initialize the GA_session object. This function allocates the
 * dynamic fields in the object and generates a random population.
 *
 * \param session      The GA_session object to intialize.
 * \param settings     The GA_settings object containing the settings
 *     for the new session.
 * \param segmentcount The number of segments in each individual.
 *     Note that the bit-width of each segment is set by the
 *     compile-time definition of GA_segment.
 *
 * \returns 0 to indicate success, 1 through 7 if a memory allocation
 * failed, 50 if an invalid thread count is specified, 51 if an error
 * occurs starting a thread, 55 if the thread_init function fails, 90
 * if any fitness function failed.
 */
int GA_init(GA_session *session, GA_settings *settings,
	    unsigned int segmentcount);

/** Free all allocated structures.
 *
 * \param session     A previously intialized GA_session object.
 *
 * \returns 0 to indicate success.
 */
int GA_cleanup(GA_session *session);

/** Evolve the population by a number of generations.
 *
 * \param session
 *   A previously intialized GA_session object.
 * \param generations
 *   The number of generations to run, 0 to use GA_settings.generations.
 *
 * \returns 0 to indicate success, 1 if any fitness function failed.
 */
int GA_evolve(GA_session *session, unsigned int generations);

/** Generate individuals into the population starting from index i.
 *
 * Used to generate initial population of each generation (skipping elitism)
 * and for replacing rejected members of the population (skipping valid ones).
 *
 * \param session     A previously intialized GA_session object.
 * \param i           The index to start from.
 */
void GA_generate(GA_session *session, unsigned int i);

/** Choose an element of the oldpop using the roulette algorithm.
 *
 * \param session     A previously intialized GA_session object.
 *
 * \returns An index into the population.
 */
unsigned int GA_roulette(GA_session *session);

/** Comparison function for sorting individuals by fitness.
 *
 * \see qsort(3)
 */
int GA_comparator(const void *a, const void *b);

/** Check the fitness of all individuals. Updates
 * GA_individual.fitness, GA_session.fittest GA_session.fitnesssum.
 *
 * \param session     A previously intialized GA_session object.
 *
 * \returns 0 to indicate success, nonzero if any fitness evaluation
 *     returned an error.
 */
int GA_checkfitness(GA_session *session);

/** \name Problem-specific Functions
 *
 * These functions must be implemented specifically for each problem.
 *
 * \todo Consider replacing with function pointers in GA_settings.
 *
 * \{ */

/** Determine the fitness of the given individual. The implementation
 * of this function should set GA_individual.fitness to a
 * double-precision floating-point fitness value, which will be
 * maximized by the genetic algorithm.
 *
 * \returns 0 for success, nonzero for failure.
 */
extern int GA_fitness(const GA_session *ga, void *thbuf, GA_individual *elem);

/** Check if a termination condition has been reached.
 *
 * \returns 0 if the evolution should continue, nonzero if the
 *          evolution should terminate.
 */
extern int GA_termination(const GA_session *ga);

/** Initialize problem-specific thread state.
 *
 * \param thread The GA_thread object for the thread.
 *
 * \returns 0 for success, nonzero for failure.
 */
extern int GA_thread_init(GA_thread *thread);

/** Free problem-specific thread state.
 *
 * \param thread The GA_thread object for the thread.
 *
 * \returns 0 for success.
 */
extern int GA_thread_free(GA_thread *thread);

/** Quickly check the fitness of the given individual against basic
 * constraints. If this function returns a zero value (unfit), the
 * individual will be rejected and a new one generated in its place.
 *
 * \returns 0 for unfit, nonzero for fit.
 */
extern int GA_fitness_quick(const GA_session *ga, GA_individual *elem);

/** Generate a random number 
 *
 * \returns 0 for success, nonzero for error.
 */
extern GA_segment GA_random_segment(GA_session *ga, const unsigned int i,
                                    const unsigned int j);

/** Task to complete after each generation.
 * (save state, output progress, etc.)
 *
 * \returns 0 for success, nonzero for error.
 */
extern int GA_finished_generation(const GA_session *ga, int terminating);

/** Task to complete before each generation.
 * (update options that change per-generation)
 * In distributed mode, execute on both server and clients.
 *
 * \returns 0 for success, nonzero for error.
 */
extern int GA_starting_generation(GA_session *ga);

/* \} */

/* Gray code helper functions */

/** Helper function to convert from Gray code to binary.
 *
 * \see http://en.wikipedia.org/wiki/Gray_code
 */
GA_segment graydecode(GA_segment gray);

/** Helper function to convert from binary to Gray code.
 *
 * \see http://en.wikipedia.org/wiki/Gray_code
 */
GA_segment grayencode(GA_segment bin);

/** Generate a random integer using the /dev/urandom special device.
 *
 * \note The /dev/urandom special device is designed for security, not
 *     speed, so this function should not be used for large amounts of
 *     random data.
 *
 * \see <a href="http://linux.die.net/man/4/urandom">urandom(4)</a>
 */
unsigned int urandom();

/** \name Option-parsing Functions
 *
 * These functions implement centralized option parsing using
 * getopt_long. This allows programs to easily accept standard options
 * controlling the GA engine, as well as problem-specific options
 * ("my" options).
 *
 * \{ */

/** Pointer type definition for a function to parse problem-specific
 * command-line options.
 *
 * \param long_options getopt options list.
 * \param settings     GA_settings argument to store configuration in.
 * \param c            The current option character (returned by getopt)
 * \param option_index The current index into long_options (set by getopt)
 *
 * \see GA_getopt
 *
 * \returns 0 for success.
 */
typedef int (*GA_my_parseopt_t)(const struct option *long_options,
				GA_settings *settings, int c, int option_index);

/** Parse command-line options using getopt_long.
 *
 * \param argc,argv       Command-line arguments, from main.
 * \param settings        GA_settings argument to store configuration in.
 * \param my_optstring    getopt optstring for problem-specific options.
 * \param my_long_options getopt options list for problem-specific options.
 * \param my_parse_option Option parsing function for problem-specific options.
 * \param my_usage        Usage message for problem-specific options.
 * \param optlog          Reallocable string to log options into, or NULL.
 *
 * \see <a href="http://linux.die.net/man/3/getopt">getopt(3)</a>,
 *     GA_my_parseopt_t, GA_run_getopt
 *
 * \returns 0 for success, nonzero for failure.
 */
int GA_getopt(int argc, char * const argv[], GA_settings *settings,
	      const char *my_optstring, const struct option *my_long_options,
	      GA_my_parseopt_t my_parse_option, const char *my_usage,
	      char **optlog);
/* \} */

/** Print a message both to the stdout and (unless in debug mode) the
 * logfile. Thread-safe in conjunction with tprintf and invisible_system.
 *
 * \see GA_settings.debugmode, lprintf, tprintf, invisible_system
 */
int qprintf(const GA_settings *settings, const char *format, ...);

/** Print a message both to the logfile (or stdout if there is no logfile).
 *Thread-safe in conjunction with tprintf and invisible_system.
 *
 * \see GA_settings.debugmode, qprintf, tprintf, invisible_system
 */
int lprintf(const GA_settings *settings, const char *format, ...);

/** Thread-safe regular printf. Thread-safe in conjunction with
 * qprintf and invisible_system.
 *
 * \see qprintf, invisible_system
 */
int tprintf(const char *format, ...);

/** Generate a random number using the session's random number generator.
 * Arbitrary range. Not thread-safe.
 */
unsigned int GA_rand(GA_session *session);

/** Generate a random number using the session's random number generator.
 * Double-precision floating-point in [0,1). Not thread-safe.
 */
double GA_rand_double(GA_session *session);

#if THREADS
/* Consider abandoning this mutex in favor of flockfile on */
extern pthread_mutex_t GA_iomutex;/* = PTHREAD_MUTEX_INITIALIZER; */
#endif

#define _HAVE_GA_H
#endif
