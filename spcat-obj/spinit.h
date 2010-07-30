#ifndef _HAVE_SPINIT_H
#define _HAVE_SPINIT_H

#include <stddef.h>

/*   Copyright (C) 1989, California Institute of Technology */
/*   All rights reserved.  U. S. Government Sponsorship under */
/*   NASA Contract NAS7-918 is acknowledged. */

/*   Herbert M. Pickett, 20 March 1989 */
#define MAXITOT 6
typedef struct {
  /*@dependent@*/ double *eigvec;
  /*@dependent@*/ short *qsym;
  /*@dependent@*/ short *offset;
  /*@dependent@*/ short *qn;
  int n, neven;
} EITMIX;
typedef struct str_itmix {
  /*@null@*/ /*@owned@*/ struct str_itmix *next;
  /*@owned@*/ double *eigvecv;
  /*@owned@*/ short *qnv;
  /*@owned@*/ EITMIX *mix;
  size_t nditot; 
  int ii, neqi, nitot;
} ITMIX;

typedef struct str_itot {
  /*@null@*/ /*@owned@*/ struct str_itot *next;
  double *val;
  int *ioff;
  /*@dependent@*/ ITMIX *pret;
  int ii, ltot, lv1, lv2, neqi;
} SITOT;

#include "spcat-obj.h"

int tensor(spcs_t *spcs, double *z, const int *iscom, const int *jscom,
           const int *lscom, const int *smap, int npair, int alpha);
void getzitot(spcs_t *spcs, double *z, int lls, int ii, const int *lscom, 
              const int *iscom, const int* jscom, int alpha, int neqi);
void setzitot(spcs_t *spcs, int lv1, int lv2, int ltot, int ii, int neqi);
/*@dependent@*/  /*@null@*/ 
ITMIX *get_itmix(spcs_t *spcs, const int ii, const int nitot);

#endif
