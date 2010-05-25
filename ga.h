/** \file ga.h
 *
 * Genetic algorithm header file.
 */
#ifndef _HAVE_GA_H
#include <stdint.h>
#include <getopt.h>

/* Library typedefs */

#if !defined(GA_segment) || !defined(GA_segment_size)
/** The type of a segment. This is expected to be some kind of
 * integer-type. The bit-width of the type must be specified in
 * GA_segment_size.
 */
#define GA_segment uint32_t
/** The size, in bits, of a segment. */
#define GA_segment_size 32
#endif

/** A single individual of the population.
 */
typedef struct GA_individual_struct {
  /** The array of segments for the individual. Each segment contains
   * a portion of the individual's genome.
   */
  GA_segment *segments;
  /** The number of segments in the individual. */
  unsigned int segmentcount;
  /** The fitness of the individual. \see GA_fitness, GA_checkfitness */
  double fitness;
  /** The unscaled fitness of the individual. \see GA_checkfitness */
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
  /** If false, assume not in "debug mode", where stdout is redirected
   * to a file, and the real stdout is available in stdoutfd. */
  int debugmode;
  /** Real stdout if debug mode is not active. \see debugmode */
  FILE *stdoutfh;
  /** Pointer to problem-specific options structure (for use in
   * options parsing and fitness evaluation). */
  void *ref;
} GA_settings;

/** The state of the entire session.
 */
typedef struct GA_session_struct {
  /** The population array for the current generation. \see newpop */
  GA_individual *population;
  /** The population array for the next generation. After the next
   * generation is completed, the population and newpop pointers are
   * swapped. \see population
   */
  GA_individual *newpop;
  /** Fitness cache, to avoid unneccessary fitness evaluations. */
  GA_individual **fitnesscache;
  /** A list of indexes into the population, sorted by fitness. The
   * most fit individual is first. */
  unsigned int *sorted;
  /** The number of individuals in the population. */
  unsigned int popsize;
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
  /** Pointer to the GA_settings structure for this session. */
  GA_settings *settings;
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
 * failed, 16 if any fitness function failed.
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
int GA_evolve(GA_session *session,
	      unsigned int generations);

/** Choose an element of the population using the roulette algorithm.
 *
 * \param session     A previously intialized GA_session object.
 *
 * \returns An index into the population.
 */
unsigned int GA_roulette(const GA_session *session);

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
extern int GA_fitness(const GA_session *ga, GA_individual *elem);

/** Check if a termination condition has been reached.
 *
 * \returns 0 if the evolution should continue, nonzero if the
 *          evolution should terminate.
 */
extern int GA_termination(const GA_session *session);

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
 *
 * \see <a href="http://linux.die.net/man/3/getopt">getopt(3)</a>,
 *     GA_my_parseopt_t, GA_run_getopt
 *
 * \returns 0 for success, nonzero for failure.
 */
int GA_getopt(int argc, char * const argv[], GA_settings *settings,
	      const char *my_optstring, const struct option *my_long_options,
	      GA_my_parseopt_t my_parse_option, char *my_usage);
/* \} */

/** Print a message both to the stdout and (unless in debug mode) the
 * "real" stdout saved in GA_settings.stdoutfd.
 *
 * \see Ga_settings.debugmode
 */
int qprintf(GA_settings *settings, const char *format, ...);


#define _HAVE_GA_H
#endif
