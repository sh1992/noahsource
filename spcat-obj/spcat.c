/* Wrapper around spcat-obj.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spcat-obj.h"

int main(int argc, char *argv[]) {
  char *buffers[NFILE];
  size_t bufsizes[NFILE];
  char *fn;
  int i = 0;
  memset(buffers, 0, sizeof(buffers));
  memset(bufsizes, 0, sizeof(bufsizes));
  if ( argc != 2 ) { printf("Usage: spcat BASENAME\n"); exit(1); }
  fn = strrchr(argv[1], '.');
  if ( fn ) fn[0] = (char)0; /* Remove filename extension */
  fn = malloc(strlen(argv[1])+5);
  if ( fn == NULL ) { fprintf(stderr,"Memory allocation failed\n"); exit(1); }
  for ( ; i < 2; i++ ) { /* Read input files */
    FILE *hmem, *hfile;
    char buf[256];
    size_t tempsizer = 0, tempsizew = 0;
    sprintf(fn, "%s.%s", argv[1], spcat_ext[i]);
    printf("Reading file %d (%s) into memory: ", i, fn);
    hmem = open_memstream(&buffers[i], &bufsizes[i]);
    if( hmem == NULL ) {
      fprintf(stderr,"Can't open input memory buffer %d: ", i); perror(NULL);
      exit(1);
    }
    hfile = fopen(fn, "r");
    if ( hfile == NULL ) {
      fprintf(stderr,"Can't open input file %d: ", i); perror(NULL);
      exit(1);
    }
    while ( ( tempsizer = fread(buf, 1, sizeof(buf), hfile) ) ) {
      tempsizew = fwrite(buf, 1, tempsizer, hmem);
      if ( tempsizew != tempsizer ) {
        fprintf(stderr,"Write error on file %d\n", i);
        exit(1);
      }
      /* else printf("Loaded %d bytes\n", tempsizer); */
    }
    if ( ferror(hfile) ) { fprintf(stderr,"Read error on file %d\n", i); exit(1); }
    fclose(hfile);
    fclose(hmem);
    printf("Loaded %d bytes\n", bufsizes[i]);
  }
  {
    spcs_t x;
    if ( init_spcs(&x) ) { fprintf(stderr,"init_spcs error\n"); exit(1); }
    spcat(&x, buffers, bufsizes);
    if ( free_spcs(&x) ) { fprintf(stderr,"free_spcs error\n"); exit(1); }
    /* STILL NEED TO SORT, CLEANUP, ETC */
  }
  for ( ; i < NFILE; i++ ) { /* Write output files */
    FILE *hfile;
    size_t tempsizew = 0;
    sprintf(fn, "%s.%s", argv[1], spcat_ext[i]);
    if ( buffers[i] == NULL ) {
      printf("Skipping file %d (%s): Not written\n", i, fn);
      continue;
    }
    printf("Writing file %d (%s) from memory: ", i, fn);
    hfile = fopen(fn, "w");
    if ( hfile == NULL ) {
      fprintf(stderr,"Can't open output file %d: ", i); perror(NULL);
      exit(1);
    }
    tempsizew = fwrite(buffers[i], 1, bufsizes[i], hfile);
    if ( tempsizew != bufsizes[i] ) {
      fprintf(stderr,"Write error on file %d\n", i);
      exit(1);
    }
    else printf("Saved %d bytes\n", tempsizew);
    fclose(hfile);
  }
  for ( i=0; i < NFILE; i++ ) if ( buffers[i] ) free(buffers[i]);
  free(fn);
  return 0;
}
